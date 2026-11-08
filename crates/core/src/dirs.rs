use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

use directories::BaseDirs;

use crate::{CoreError, Result};

/// Directory name below the platform's per-user base directories. Linux keeps the lowercase XDG
/// name; macOS and Windows use the capitalised name those platforms' users expect.
#[cfg(target_os = "linux")]
const APP: &str = "gallerybrowser";
#[cfg(not(target_os = "linux"))]
const APP: &str = "GalleryBrowser";

/// Content of the `CACHEDIR.TAG` file (https://bford.info/cachedir/) so backup tools skip the cache.
const CACHEDIR_TAG: &str = "Signature: 8a477f597d28d172789f06886806bc55\n\
# This file is a cache directory tag created by GalleryBrowser.\n\
# For information about cache directory tags, see:\n\
#\thttps://bford.info/cachedir/\n";

/// Per-user directories. Every directory is created and PROBED FOR WRITABILITY (create+delete a temp file);
/// on failure falls back to `~/.local/state/<app>/<kind>` (independent of any platform base directory),
/// then to a private temp dir. (Fixes the Python bug where an existing read-only dir was accepted.)
///
/// | | Linux (XDG) | macOS | Windows |
/// |---|---|---|---|
/// | config | `~/.config/gallerybrowser` | `~/Library/Application Support/GalleryBrowser` | `%APPDATA%\GalleryBrowser` |
/// | data | `~/.local/share/gallerybrowser` | `~/Library/Application Support/GalleryBrowser` | `%LOCALAPPDATA%\GalleryBrowser` |
/// | cache | `~/.cache/gallerybrowser` | `~/Library/Caches/GalleryBrowser` | `%LOCALAPPDATA%\GalleryBrowser` |
/// | runtime | `$XDG_RUNTIME_DIR/gallerybrowser` | cache | cache |
#[derive(Clone, Debug)]
pub struct AppDirs {
    /// settings.json
    pub config: PathBuf,
    /// library catalog
    pub data: PathBuf,
    /// thumbnails (`thumbnails/`), crash logs, CACHEDIR.TAG
    pub cache: PathBuf,
    /// single-instance socket (the cache dir where the platform has no runtime dir)
    pub runtime: PathBuf,
}

impl AppDirs {
    pub fn resolve() -> Result<Self> {
        let base = BaseDirs::new();
        let home = base.as_ref().map(|b| b.home_dir().to_path_buf());
        let app_dir = |pick: fn(&BaseDirs) -> PathBuf| base.as_ref().map(|b| pick(b).join(APP));

        let config = resolve_dir(
            "config",
            app_dir(|b| b.config_dir().to_path_buf()),
            home.as_deref(),
        )?;
        // `data_dir` is the roaming profile on Windows; the catalog is machine-local state.
        #[cfg(windows)]
        let data_primary = app_dir(|b| b.data_local_dir().to_path_buf());
        #[cfg(not(windows))]
        let data_primary = app_dir(|b| b.data_dir().to_path_buf());
        let data = resolve_dir("data", data_primary, home.as_deref())?;
        let cache = resolve_dir(
            "cache",
            app_dir(|b| b.cache_dir().to_path_buf()),
            home.as_deref(),
        )?;
        let runtime = base
            .as_ref()
            .and_then(|b| b.runtime_dir())
            .map(|r| r.join(APP))
            .and_then(|p| ensure_writable_dir(&p).ok())
            .unwrap_or_else(|| cache.clone());

        if let Err(e) = write_cachedir_tag(&cache) {
            log::warn!("could not write CACHEDIR.TAG in {}: {e}", cache.display());
        }
        Ok(Self {
            config,
            data,
            cache,
            runtime,
        })
    }

    /// Python-era SQLite database (`~/.cache/gallerybrowser/database.sqlite`) if present, for one-time
    /// import. The Python app only ever existed on Linux, so other platforms never report one.
    pub fn legacy_database(&self) -> Option<PathBuf> {
        if !cfg!(target_os = "linux") {
            return None;
        }
        let path = self.cache.join("database.sqlite");
        path.is_file().then_some(path)
    }
    pub fn settings_file(&self) -> PathBuf {
        self.config.join("settings.json")
    }
    pub fn thumbnails_dir(&self) -> PathBuf {
        self.cache.join("thumbnails")
    }
    pub fn library_dir(&self) -> PathBuf {
        self.data.join("library")
    }
    pub fn instance_socket(&self) -> PathBuf {
        self.runtime.join("instance.sock")
    }
}

/// First writable of: `primary`, `~/.local/state/gallerybrowser/<kind>`, a private temp dir.
fn resolve_dir(
    kind: &'static str,
    primary: Option<PathBuf>,
    home: Option<&Path>,
) -> Result<PathBuf> {
    let mut candidates = Vec::with_capacity(3);
    candidates.extend(primary);
    if let Some(home) = home {
        candidates.push(home.join(".local").join("state").join(APP).join(kind));
    }
    candidates.push(std::env::temp_dir().join(format!("{APP}-{}-{kind}", user_tag())));
    first_writable(&candidates).ok_or(CoreError::NoWritableDir(kind))
}

fn first_writable(candidates: &[PathBuf]) -> Option<PathBuf> {
    for dir in candidates {
        match ensure_writable_dir(dir) {
            Ok(path) => return Some(path),
            Err(e) => log::warn!("directory {} is not usable: {e}", dir.display()),
        }
    }
    None
}

/// Name component distinguishing users sharing one temp dir (letters/digits only).
fn user_tag() -> String {
    let raw = std::env::var("USER")
        .or_else(|_| std::env::var("LOGNAME"))
        .or_else(|_| std::env::var("USERNAME"))
        .unwrap_or_default();
    let tag: String = raw
        .chars()
        .filter(|c| c.is_ascii_alphanumeric() || *c == '_' || *c == '-')
        .take(32)
        .collect();
    if tag.is_empty() {
        "user".to_string()
    } else {
        tag
    }
}

/// Create `dir` (mode 0700 for new directories on unix) and prove it is writable by creating and
/// deleting a file.
fn ensure_writable_dir(dir: &Path) -> std::io::Result<PathBuf> {
    let mut builder = std::fs::DirBuilder::new();
    builder.recursive(true);
    #[cfg(unix)]
    std::os::unix::fs::DirBuilderExt::mode(&mut builder, 0o700);
    builder.create(dir)?;
    if !std::fs::metadata(dir)?.is_dir() {
        return Err(std::io::Error::other("not a directory"));
    }
    probe_writable(dir)?;
    Ok(dir.to_path_buf())
}

fn probe_writable(dir: &Path) -> std::io::Result<()> {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let probe = dir.join(format!(
        ".write-probe-{}-{}",
        std::process::id(),
        COUNTER.fetch_add(1, Ordering::Relaxed)
    ));
    std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&probe)?;
    std::fs::remove_file(&probe)
}

fn write_cachedir_tag(cache: &Path) -> std::io::Result<()> {
    let tag = cache.join("CACHEDIR.TAG");
    if tag.exists() {
        return Ok(());
    }
    std::fs::write(tag, CACHEDIR_TAG)
}

#[cfg(test)]
mod tests {
    // Vendored from GalleryBrowser 55a4483: test assertions may panic by design. See crates/VENDORED.md.
    #![allow(clippy::unwrap_used, clippy::expect_used)]

    use super::*;
    #[cfg(unix)]
    use std::os::unix::fs::PermissionsExt;

    #[test]
    fn creates_missing_directories_recursively() {
        let t = tempfile::tempdir().expect("tempdir");
        let target = t.path().join("a").join("b");
        let got = ensure_writable_dir(&target).expect("create");
        assert_eq!(got, target);
        assert!(target.is_dir());
        // the probe file is cleaned up
        assert_eq!(std::fs::read_dir(&target).expect("read").count(), 0);
    }

    #[cfg(unix)]
    #[test]
    fn existing_read_only_dir_is_rejected_and_fallback_used() {
        let t = tempfile::tempdir().expect("tempdir");
        let ro = t.path().join("ro");
        std::fs::create_dir(&ro).expect("mkdir");
        std::fs::set_permissions(&ro, std::fs::Permissions::from_mode(0o555)).expect("chmod");
        let fallback = t.path().join("fallback");
        let got = first_writable(&[ro.clone(), fallback.clone()]);
        // Root ignores permission bits: then the read-only dir is genuinely writable and chosen.
        let writable_anyway = probe_writable(&ro).is_ok();
        std::fs::set_permissions(&ro, std::fs::Permissions::from_mode(0o755)).expect("chmod back");
        if writable_anyway {
            assert_eq!(got, Some(ro));
        } else {
            assert_eq!(got, Some(fallback.clone()));
            assert!(fallback.is_dir());
        }
    }

    #[test]
    fn file_in_the_way_falls_back() {
        let t = tempfile::tempdir().expect("tempdir");
        let blocked = t.path().join("blocked");
        std::fs::write(&blocked, b"x").expect("write");
        let fallback = t.path().join("fb");
        assert_eq!(
            first_writable(&[blocked.join("sub"), blocked, fallback.clone()]),
            Some(fallback)
        );
    }

    #[test]
    fn no_candidates_means_none() {
        assert_eq!(first_writable(&[]), None);
    }

    #[test]
    fn cachedir_tag_written_once_and_legacy_db_detected() {
        let t = tempfile::tempdir().expect("tempdir");
        let dirs = AppDirs {
            config: t.path().join("config"),
            data: t.path().join("data"),
            cache: t.path().to_path_buf(),
            runtime: t.path().join("run"),
        };
        write_cachedir_tag(&dirs.cache).expect("tag");
        let text = std::fs::read_to_string(dirs.cache.join("CACHEDIR.TAG")).expect("read");
        assert!(text.starts_with("Signature: 8a477f597d28d172789f06886806bc55"));
        write_cachedir_tag(&dirs.cache).expect("tag again");

        assert_eq!(dirs.legacy_database(), None);
        std::fs::write(dirs.cache.join("database.sqlite"), b"").expect("write");
        // Only Linux ever had the Python app's database.
        let expected = cfg!(target_os = "linux").then(|| dirs.cache.join("database.sqlite"));
        assert_eq!(dirs.legacy_database(), expected);
        assert_eq!(dirs.settings_file(), dirs.config.join("settings.json"));
        assert_eq!(dirs.instance_socket(), dirs.runtime.join("instance.sock"));
    }

    #[test]
    fn user_tag_is_path_safe() {
        assert!(
            user_tag()
                .chars()
                .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-')
        );
    }
}
