use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

use std::collections::HashMap;

use crate::{AppDirs, Column, CoreError, Result, SortKey};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default, serde::Serialize, serde::Deserialize)]
pub enum Theme {
    #[default]
    Dark,
    Light,
    System,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default, serde::Serialize, serde::Deserialize)]
pub enum ViewMode {
    #[default]
    Grid,
    List,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default, serde::Serialize, serde::Deserialize)]
pub enum FilterScope {
    #[default]
    Folder,
    Library,
}

/// Persisted user settings (`settings.json`). Unknown keys are ignored, missing keys default,
/// out-of-range values are clamped on load. Saved atomically (temp file + rename).
/// Every field MUST have a runtime consumer (the Python app had four dead settings).
#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
#[serde(default)]
pub struct Settings {
    /// Grid thumbnail edge in px: one of 64, 128, 256, 512 (default 128).
    pub thumbnail_size: u32,
    pub show_hidden: bool,
    /// Auto-play videos/sequences when selected in the preview pane.
    pub video_preview: bool,
    /// Thumbnail decoder threads (1..=16, default 2).
    pub thumbnail_threads: usize,
    /// Disk thumbnail cache budget in MB (default 1000).
    pub max_cache_mb: u64,
    /// Playback frame cache budget in MB (default 2048).
    pub ram_preview_mb: u64,
    pub collapse_sequences: bool,
    /// Frame rate used for image sequences (default 24).
    pub sequence_fps: f64,
    pub view_mode: ViewMode,
    pub sort_key: SortKey,
    pub sort_ascending: bool,
    /// List column order, left to right. All four are always offered; a saved list that lost one is topped up.
    pub list_columns: Vec<Column>,
    /// Width in points per column - the user drags these on the header separators.
    pub column_widths: HashMap<Column, f32>,
    pub show_preview: bool,
    /// The left column: the folder tree rooted at the drives, and the favorites list.
    pub show_tree: bool,
    pub theme: Theme,
    pub volume: f32,
    pub muted: bool,
    pub loop_playback: bool,
    pub favorites: Vec<PathBuf>,
    /// Newest first, deduplicated, at most `recents_max`.
    pub recents: Vec<PathBuf>,
    pub recents_max: usize,
    pub last_folder: Option<PathBuf>,
    /// List every file, not only media.
    pub show_all_files: bool,
    /// Show a second file pane next to the first.
    pub dual_pane: bool,
    pub last_folder_secondary: Option<PathBuf>,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            thumbnail_size: 128,
            show_hidden: false,
            // Off by default: selecting a clip should load it paused so the preview panel has its transport;
            // autoplay on selection is opt-in.
            video_preview: false,
            thumbnail_threads: 2,
            max_cache_mb: 1000,
            ram_preview_mb: 2048,
            collapse_sequences: true,
            sequence_fps: 24.0,
            view_mode: ViewMode::Grid,
            sort_key: SortKey::Name,
            sort_ascending: true,
            list_columns: vec![Column::Name, Column::Modified, Column::Size, Column::Type],
            // Narrow on purpose: a tiled window's pane is a couple of hundred points wide, and it is easier
            // to drag a column open than to notice one that started off-screen.
            column_widths: HashMap::from([
                (Column::Name, 200.0),
                (Column::Modified, 115.0),
                (Column::Size, 70.0),
                (Column::Type, 80.0),
            ]),
            show_preview: true,
            show_tree: true,
            theme: Theme::Dark,
            volume: 1.0,
            muted: false,
            loop_playback: true,
            favorites: Vec::new(),
            recents: Vec::new(),
            recents_max: 15,
            last_folder: None,
            show_all_files: false,
            dual_pane: false,
            last_folder_secondary: None,
        }
    }
}

const THUMBNAIL_SIZES: [u32; 4] = [64, 128, 256, 512];

impl Settings {
    /// Never fails: unreadable/corrupt file → defaults (logged). Accepts the Python app's `settings.json`
    /// (same key names). Unknown keys are ignored; a key holding a value of the wrong type keeps its default
    /// while the other keys load; out-of-range values are clamped ([`Settings::sanitize`]).
    pub fn load(dirs: &AppDirs) -> Self {
        let path = dirs.settings_file();
        let text = match std::fs::read_to_string(&path) {
            Ok(t) => t,
            Err(e) => {
                if e.kind() != std::io::ErrorKind::NotFound {
                    log::warn!("cannot read {}: {e}; using defaults", path.display());
                }
                return Self::default();
            }
        };
        Self::from_json(&text, &path)
    }

    fn from_json(text: &str, origin: &Path) -> Self {
        let loaded = match serde_json::from_str::<serde_json::Value>(text) {
            Ok(serde_json::Value::Object(map)) => map,
            Ok(_) => {
                log::warn!("{} is not a JSON object; using defaults", origin.display());
                return Self::default();
            }
            Err(e) => {
                log::warn!("{} is corrupt ({e}); using defaults", origin.display());
                return Self::default();
            }
        };
        let mut merged = match serde_json::to_value(Self::default()) {
            Ok(serde_json::Value::Object(map)) => map,
            _ => return Self::default(),
        };
        for (key, value) in loaded {
            if !merged.contains_key(&key) {
                continue;
            }
            let previous = merged.insert(key.clone(), value);
            if serde_json::from_value::<Self>(serde_json::Value::Object(merged.clone())).is_err() {
                log::warn!("{}: ignoring invalid value for `{key}`", origin.display());
                match previous {
                    Some(prev) => merged.insert(key, prev),
                    None => merged.remove(&key),
                };
            }
        }
        let mut settings =
            serde_json::from_value::<Self>(serde_json::Value::Object(merged)).unwrap_or_default();
        settings.sanitize();
        settings
    }

    /// Clamp every field into its valid range and normalise lists. Idempotent.
    pub fn sanitize(&mut self) {
        self.thumbnail_size = THUMBNAIL_SIZES
            .iter()
            .copied()
            .min_by_key(|s| s.abs_diff(self.thumbnail_size))
            .unwrap_or(128);
        self.thumbnail_threads = self.thumbnail_threads.clamp(1, 16);
        self.volume = if self.volume.is_finite() {
            self.volume.clamp(0.0, 1.0)
        } else {
            1.0
        };
        self.recents_max = self.recents_max.clamp(1, 100);
        self.sequence_fps = if self.sequence_fps.is_finite() {
            self.sequence_fps.clamp(1.0, 240.0)
        } else {
            24.0
        };
        dedup_in_order(&mut self.favorites);
        dedup_in_order(&mut self.recents);
        self.recents.truncate(self.recents_max);
    }

    /// Atomic save: write `settings.json.tmp-*` next to the file, fsync, rename over it. The previous file
    /// stays intact on any failure. Values are clamped first (the in-memory copy is not modified).
    pub fn save(&self, dirs: &AppDirs) -> Result<()> {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let target = dirs.settings_file();
        let io = |path: &Path, source: std::io::Error| CoreError::Io {
            path: path.to_path_buf(),
            source,
        };

        let mut clean = self.clone();
        clean.sanitize();
        let mut json =
            serde_json::to_string_pretty(&clean).map_err(|e| CoreError::Settings(e.to_string()))?;
        json.push('\n');

        std::fs::create_dir_all(&dirs.config).map_err(|e| io(&dirs.config, e))?;
        let tmp = dirs.config.join(format!(
            "settings.json.tmp-{}-{}",
            std::process::id(),
            COUNTER.fetch_add(1, Ordering::Relaxed)
        ));
        let written = (|| {
            let mut file = std::fs::OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(&tmp)?;
            file.write_all(json.as_bytes())?;
            file.sync_all()
        })();
        if let Err(e) = written {
            let _ = std::fs::remove_file(&tmp);
            return Err(io(&tmp, e));
        }
        if let Err(e) = std::fs::rename(&tmp, &target) {
            let _ = std::fs::remove_file(&tmp);
            return Err(io(&target, e));
        }
        Ok(())
    }

    /// Record a visited folder. Folders already in recents keep their position (the list does not
    /// reorder on revisit); new folders go to the front. Deduplicated, truncated to `recents_max`.
    pub fn add_recent(&mut self, folder: PathBuf) {
        if !self.recents.contains(&folder) {
            self.recents.insert(0, folder);
        }
        self.recents.truncate(self.recents_max.clamp(1, 100));
    }
}

fn dedup_in_order(paths: &mut Vec<PathBuf>) {
    let mut seen = std::collections::HashSet::new();
    paths.retain(|p| seen.insert(p.clone()));
}

#[cfg(test)]
mod tests {
    // Vendored from GalleryBrowser 55a4483: test assertions may panic by design. See crates/VENDORED.md.
    #![allow(clippy::unwrap_used, clippy::expect_used)]

    use super::*;

    fn dirs_in(root: &Path) -> AppDirs {
        AppDirs {
            config: root.join("config"),
            data: root.join("data"),
            cache: root.join("cache"),
            runtime: root.join("run"),
        }
    }

    #[test]
    fn defaults_when_file_missing() {
        let t = tempfile::tempdir().expect("tempdir");
        assert_eq!(Settings::load(&dirs_in(t.path())), Settings::default());
    }

    #[test]
    fn roundtrip_preserves_everything() {
        let t = tempfile::tempdir().expect("tempdir");
        let dirs = dirs_in(t.path());
        let s = Settings {
            thumbnail_size: 256,
            show_hidden: true,
            thumbnail_threads: 8,
            sequence_fps: 30.0,
            view_mode: ViewMode::List,
            sort_key: SortKey::Size,
            sort_ascending: false,
            theme: Theme::Light,
            volume: 0.5,
            muted: true,
            loop_playback: false,
            favorites: vec![PathBuf::from("/a"), PathBuf::from("/b")],
            recents: vec![PathBuf::from("/r")],
            last_folder: Some(PathBuf::from("/last")),
            ..Settings::default()
        };
        s.save(&dirs).expect("save");
        assert_eq!(Settings::load(&dirs), s);
        let leftovers: Vec<_> = std::fs::read_dir(&dirs.config)
            .expect("read")
            .flatten()
            .map(|e| e.file_name())
            .collect();
        assert_eq!(leftovers, vec![std::ffi::OsString::from("settings.json")]);
    }

    #[test]
    fn save_overwrites_previous_file() {
        let t = tempfile::tempdir().expect("tempdir");
        let dirs = dirs_in(t.path());
        Settings::default().save(&dirs).expect("save");
        let s = Settings {
            muted: true,
            ..Settings::default()
        };
        s.save(&dirs).expect("save again");
        assert!(Settings::load(&dirs).muted);
    }

    #[test]
    fn dual_pane_fields_default_and_roundtrip() {
        let d = Settings::default();
        assert!(!d.show_all_files && !d.dual_pane && d.last_folder_secondary.is_none());
        let t = tempfile::tempdir().expect("tempdir");
        let dirs = dirs_in(t.path());
        let s = Settings {
            show_all_files: true,
            dual_pane: true,
            last_folder_secondary: Some(PathBuf::from("/second")),
            ..Settings::default()
        };
        s.save(&dirs).expect("save");
        assert_eq!(Settings::load(&dirs), s);
    }

    #[test]
    fn corrupt_file_gives_defaults() {
        let t = tempfile::tempdir().expect("tempdir");
        let dirs = dirs_in(t.path());
        std::fs::create_dir_all(&dirs.config).expect("mkdir");
        for junk in ["{not json", "", "[1,2]", "null", "\u{0}\u{1}"] {
            std::fs::write(dirs.settings_file(), junk).expect("write");
            assert_eq!(Settings::load(&dirs), Settings::default(), "input {junk:?}");
        }
        std::fs::write(dirs.settings_file(), [0xff, 0xfe, 0x00]).expect("write");
        assert_eq!(Settings::load(&dirs), Settings::default());
    }

    #[test]
    fn out_of_range_values_are_clamped() {
        let t = tempfile::tempdir().expect("tempdir");
        let dirs = dirs_in(t.path());
        std::fs::create_dir_all(&dirs.config).expect("mkdir");
        std::fs::write(
            dirs.settings_file(),
            r#"{"thumbnail_size": 200, "thumbnail_threads": 0, "volume": 7.5, "recents_max": 0, "sequence_fps": 0.0,
                "recents": ["/a","/b","/c"]}"#,
        )
        .expect("write");
        let s = Settings::load(&dirs);
        assert_eq!(s.thumbnail_size, 256);
        assert_eq!(s.thumbnail_threads, 1);
        assert_eq!(s.volume, 1.0);
        assert_eq!(s.recents_max, 1);
        assert_eq!(s.sequence_fps, 1.0);
        assert_eq!(s.recents, vec![PathBuf::from("/a")]);

        let mut s = Settings {
            thumbnail_size: 5000,
            thumbnail_threads: 99,
            volume: -1.0,
            recents_max: 1000,
            sequence_fps: 1e9,
            ..Settings::default()
        };
        s.sanitize();
        assert_eq!(
            (
                s.thumbnail_size,
                s.thumbnail_threads,
                s.volume,
                s.recents_max,
                s.sequence_fps
            ),
            (512, 16, 0.0, 100, 240.0)
        );
        let mut s = Settings {
            thumbnail_size: 0,
            volume: f32::NAN,
            sequence_fps: f64::NAN,
            ..Settings::default()
        };
        s.sanitize();
        assert_eq!(
            (s.thumbnail_size, s.volume, s.sequence_fps),
            (64, 1.0, 24.0)
        );
    }

    #[test]
    fn legacy_python_file_loads_and_wrong_types_keep_defaults() {
        let t = tempfile::tempdir().expect("tempdir");
        let dirs = dirs_in(t.path());
        std::fs::create_dir_all(&dirs.config).expect("mkdir");
        std::fs::write(
            dirs.settings_file(),
            r#"{"thumbnail_size": 512, "show_hidden": true, "video_preview": false, "thumbnail_threads": 4,
                "max_cache_mb": 500, "ram_preview_mb": 4096, "favorites": ["/fav"], "recents": ["/x", "/y"],
                "recents_max": 15, "some_python_only_key": {"a": 1}, "volume": "loud", "muted": 3}"#,
        )
        .expect("write");
        let s = Settings::load(&dirs);
        assert_eq!(s.thumbnail_size, 512);
        assert!(s.show_hidden);
        assert!(!s.video_preview);
        assert_eq!(s.thumbnail_threads, 4);
        assert_eq!(s.max_cache_mb, 500);
        assert_eq!(s.ram_preview_mb, 4096);
        assert_eq!(s.favorites, vec![PathBuf::from("/fav")]);
        assert_eq!(s.recents, vec![PathBuf::from("/x"), PathBuf::from("/y")]);
        assert_eq!(s.volume, 1.0);
        assert!(!s.muted);
    }

    #[test]
    fn add_recent_keeps_order_on_revisit_dedupes_and_truncates() {
        let mut s = Settings {
            recents_max: 3,
            ..Settings::default()
        };
        for p in ["/a", "/b", "/c"] {
            s.add_recent(PathBuf::from(p));
        }
        let before = s.recents.clone();
        assert_eq!(
            before,
            vec![
                PathBuf::from("/c"),
                PathBuf::from("/b"),
                PathBuf::from("/a")
            ]
        );
        // Revisiting existing entries must not change the list.
        for p in ["/a", "/b", "/c", "/a"] {
            s.add_recent(PathBuf::from(p));
        }
        assert_eq!(s.recents, before);
        // A new folder goes to the front and the oldest is truncated.
        s.add_recent(PathBuf::from("/d"));
        assert_eq!(
            s.recents,
            vec![
                PathBuf::from("/d"),
                PathBuf::from("/c"),
                PathBuf::from("/b")
            ]
        );
    }

    #[test]
    fn save_into_unwritable_location_errors_without_panic() {
        let t = tempfile::tempdir().expect("tempdir");
        let blocker = t.path().join("file");
        std::fs::write(&blocker, b"x").expect("write");
        let dirs = AppDirs {
            config: blocker.join("config"),
            data: t.path().into(),
            cache: t.path().into(),
            runtime: t.path().into(),
        };
        assert!(Settings::default().save(&dirs).is_err());
    }
}
