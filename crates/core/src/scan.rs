use std::cmp::Ordering;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::SystemTime;

use crate::natural::natural_cmp;
use crate::{CoreError, Entry, MediaKind, Result, classify, detect_sequences};

#[derive(
    Clone, Copy, Debug, PartialEq, Eq, Hash, serde::Serialize, serde::Deserialize, Default,
)]
pub enum SortKey {
    #[default]
    Name,
    Modified,
    Size,
    Kind,
}

#[derive(Clone, Debug)]
pub struct ScanOptions {
    pub show_hidden: bool,
    pub collapse_sequences: bool,
    /// Minimum frames to form a sequence (Python: 2).
    pub min_sequence_len: usize,
    /// Also list every non-media regular file as `MediaKind::Other`.
    pub show_all_files: bool,
}

impl Default for ScanOptions {
    fn default() -> Self {
        Self {
            show_hidden: false,
            collapse_sequences: true,
            min_sequence_len: 2,
            show_all_files: false,
        }
    }
}

/// Hidden = the file name starts with a dot; on Windows also the `FILE_ATTRIBUTE_HIDDEN` flag.
pub fn is_hidden(path: &Path) -> bool {
    path.file_name()
        .is_some_and(|n| n.as_encoded_bytes().first() == Some(&b'.'))
        || has_hidden_attribute(path)
}

#[cfg(windows)]
fn has_hidden_attribute(path: &Path) -> bool {
    use std::os::windows::fs::MetadataExt;
    const FILE_ATTRIBUTE_HIDDEN: u32 = 0x2;
    std::fs::symlink_metadata(path).is_ok_and(|m| m.file_attributes() & FILE_ATTRIBUTE_HIDDEN != 0)
}

#[cfg(not(windows))]
fn has_hidden_attribute(_path: &Path) -> bool {
    false
}

/// Media entries directly inside `dir` (non-recursive, no directories). Unreadable single files are skipped
/// (logged); an unreadable `dir` is an error. Result is naturally name-sorted.
pub fn scan_folder(dir: &Path, opts: &ScanOptions) -> Result<Vec<Entry>> {
    let read = std::fs::read_dir(dir).map_err(|source| CoreError::Io {
        path: dir.to_path_buf(),
        source,
    })?;

    let mut entries = Vec::new();
    let mut images: Vec<PathBuf> = Vec::new();
    let mut stats: HashMap<PathBuf, (u64, Option<SystemTime>)> = HashMap::new();

    for item in read {
        let item = match item {
            Ok(item) => item,
            Err(e) => {
                log::warn!("skipping unreadable entry in {}: {e}", dir.display());
                continue;
            }
        };
        let path = item.path();
        if !opts.show_hidden && is_hidden(&path) {
            continue;
        }
        let kind = match classify(&path) {
            Some(k) => k,
            None if opts.show_all_files => MediaKind::Other,
            None => continue,
        };
        // `fs::metadata` follows symlinks, so a link to a media file is browsable and a dangling one is skipped.
        let meta = match std::fs::metadata(&path) {
            Ok(m) => m,
            Err(e) => {
                log::warn!("skipping {}: {e}", path.display());
                continue;
            }
        };
        if !meta.is_file() {
            continue;
        }
        let (size, modified) = (meta.len(), meta.modified().ok());
        if kind == MediaKind::Image && opts.collapse_sequences {
            stats.insert(path.clone(), (size, modified));
            images.push(path);
        } else {
            entries.push(file_entry(path, kind, size, modified));
        }
    }

    if !images.is_empty() {
        let (sequences, leftover) = detect_sequences(&images, opts.min_sequence_len);
        for path in leftover {
            let (size, modified) = stats.get(&path).copied().unwrap_or((0, None));
            entries.push(file_entry(path, MediaKind::Image, size, modified));
        }
        for seq in sequences {
            let mut size = 0u64;
            let mut modified: Option<SystemTime> = None;
            for frame in &seq.frames {
                if let Some(&(s, m)) = stats.get(&frame.path) {
                    size = size.saturating_add(s);
                    modified = modified.max(m);
                }
            }
            entries.push(Entry {
                path: seq.pattern_path(),
                name: seq.display_name(),
                kind: MediaKind::Sequence,
                size,
                modified,
                sequence: Some(Arc::new(seq)),
            });
        }
    }

    sort_entries(&mut entries, SortKey::Name, true);
    Ok(entries)
}

fn file_entry(path: PathBuf, kind: MediaKind, size: u64, modified: Option<SystemTime>) -> Entry {
    let name = path
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default();
    Entry {
        path,
        name,
        kind,
        size,
        modified,
        sequence: None,
    }
}

/// Immediate subdirectories, naturally sorted; hidden ones only if `show_hidden`.
pub fn list_subfolders(dir: &Path, show_hidden: bool) -> Result<Vec<PathBuf>> {
    let read = std::fs::read_dir(dir).map_err(|source| CoreError::Io {
        path: dir.to_path_buf(),
        source,
    })?;
    let mut out: Vec<PathBuf> = Vec::new();
    for item in read {
        let Ok(item) = item else { continue };
        let path = item.path();
        if !show_hidden && is_hidden(&path) {
            continue;
        }
        if std::fs::metadata(&path).is_ok_and(|m| m.is_dir()) {
            out.push(path);
        }
    }
    out.sort_by(|a, b| natural_cmp(&file_name_lossy(a), &file_name_lossy(b)));
    Ok(out)
}

fn file_name_lossy(path: &Path) -> String {
    path.file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default()
}

/// Kind order matches the Python app's string sort of its type names (audio, image, sequence, video).
fn kind_rank(kind: MediaKind) -> u8 {
    match kind {
        MediaKind::Audio => 0,
        MediaKind::Image => 1,
        MediaKind::Sequence => 2,
        MediaKind::Video => 3,
        MediaKind::Other => 4,
    }
}

/// What a list column shows. The set is fixed because each column maps to a [`SortKey`] the scanner already
/// implements; the user chooses the order and the widths, not which columns exist.
#[derive(
    Clone,
    Copy,
    Debug,
    PartialEq,
    Eq,
    PartialOrd,
    Ord,
    Hash,
    Default,
    serde::Serialize,
    serde::Deserialize,
)]
pub enum Column {
    #[default]
    Name,
    Modified,
    Size,
    Type,
}

/// Stable sort; ties broken by natural name order (always ascending, as the Python app's stable
/// `sorted(..., reverse=True)` leaves ties in name order).
pub fn sort_entries(entries: &mut [Entry], key: SortKey, ascending: bool) {
    entries.sort_by(|a, b| {
        let primary = match key {
            SortKey::Name => natural_cmp(&a.name, &b.name),
            SortKey::Modified => a.modified.cmp(&b.modified),
            SortKey::Size => a.size.cmp(&b.size),
            SortKey::Kind => kind_rank(a.kind).cmp(&kind_rank(b.kind)),
        };
        let primary = if ascending {
            primary
        } else {
            primary.reverse()
        };
        match primary {
            Ordering::Equal => natural_cmp(&a.name, &b.name),
            other => other,
        }
    });
}

#[cfg(test)]
mod tests {
    // Vendored from GalleryBrowser 55a4483: test assertions may panic by design. See crates/VENDORED.md.
    #![allow(clippy::unwrap_used, clippy::expect_used)]

    use super::*;
    use std::fs;
    use std::time::Duration;

    fn touch(dir: &Path, name: &str, bytes: usize) {
        fs::write(dir.join(name), vec![0u8; bytes]).expect("write");
    }

    fn names(entries: &[Entry]) -> Vec<&str> {
        entries.iter().map(|e| e.name.as_str()).collect()
    }

    fn fixture() -> tempfile::TempDir {
        let d = tempfile::tempdir().expect("tempdir");
        for i in 1..=3 {
            touch(d.path(), &format!("shot_{i:04}.exr"), 10);
        }
        touch(d.path(), "clip.MOV", 100);
        touch(d.path(), "single.png", 5);
        touch(d.path(), "layers_001.psd", 7);
        touch(d.path(), "layers_002.psd", 7);
        touch(d.path(), "notes.txt", 1);
        touch(d.path(), ".hidden_01.png", 1);
        touch(d.path(), ".hidden_02.png", 1);
        fs::create_dir(d.path().join("subdir.png")).expect("mkdir");
        d
    }

    #[test]
    fn collapses_sequences_and_hides_dotfiles() {
        let d = fixture();
        let entries = scan_folder(d.path(), &ScanOptions::default()).expect("scan");
        assert_eq!(
            names(&entries),
            vec![
                "clip.MOV",
                "layers_001.psd",
                "layers_002.psd",
                "shot_[0001-0003].exr",
                "single.png"
            ]
        );
        let seq = entries
            .iter()
            .find(|e| e.kind == MediaKind::Sequence)
            .expect("sequence");
        assert_eq!(seq.path, d.path().join("shot_####.exr"));
        assert_eq!(seq.size, 30);
        assert_eq!(seq.member_paths().len(), 3);
        assert_eq!(seq.representative_path(), d.path().join("shot_0002.exr"));
        assert!(seq.modified.is_some());
    }

    #[test]
    fn collapse_off_returns_individual_images() {
        let d = fixture();
        let opts = ScanOptions {
            collapse_sequences: false,
            ..ScanOptions::default()
        };
        let entries = scan_folder(d.path(), &opts).expect("scan");
        assert_eq!(
            names(&entries),
            vec![
                "clip.MOV",
                "layers_001.psd",
                "layers_002.psd",
                "shot_0001.exr",
                "shot_0002.exr",
                "shot_0003.exr",
                "single.png"
            ]
        );
        assert!(
            entries
                .iter()
                .all(|e| e.kind != MediaKind::Sequence && e.sequence.is_none())
        );
        assert_eq!(
            entries
                .iter()
                .filter(|e| e.kind == MediaKind::Image)
                .count(),
            6
        );
    }

    #[test]
    fn show_hidden_includes_dotfiles_and_their_sequences() {
        let d = fixture();
        let opts = ScanOptions {
            show_hidden: true,
            ..ScanOptions::default()
        };
        let entries = scan_folder(d.path(), &opts).expect("scan");
        assert!(names(&entries).contains(&".hidden_[01-02].png"));
        let opts = ScanOptions {
            show_hidden: true,
            collapse_sequences: false,
            ..ScanOptions::default()
        };
        let entries = scan_folder(d.path(), &opts).expect("scan");
        assert!(names(&entries).contains(&".hidden_01.png"));
    }

    #[test]
    fn min_sequence_len_is_honoured() {
        let d = fixture();
        let opts = ScanOptions {
            min_sequence_len: 4,
            ..ScanOptions::default()
        };
        let entries = scan_folder(d.path(), &opts).expect("scan");
        assert!(entries.iter().all(|e| e.kind != MediaKind::Sequence));
    }

    #[test]
    fn missing_dir_is_an_error() {
        let d = tempfile::tempdir().expect("tempdir");
        assert!(scan_folder(&d.path().join("nope"), &ScanOptions::default()).is_err());
        assert!(list_subfolders(&d.path().join("nope"), false).is_err());
    }

    #[test]
    fn subfolders_are_natural_sorted_and_hidden_filtered() {
        let d = tempfile::tempdir().expect("tempdir");
        for n in ["b10", "b2", "A1", ".git"] {
            fs::create_dir(d.path().join(n)).expect("mkdir");
        }
        touch(d.path(), "file.png", 1);
        let subs = list_subfolders(d.path(), false).expect("list");
        let got: Vec<String> = subs.iter().map(|p| file_name_lossy(p)).collect();
        assert_eq!(got, vec!["A1", "b2", "b10"]);
        let subs = list_subfolders(d.path(), true).expect("list");
        assert_eq!(subs.len(), 4);
    }

    #[test]
    fn sort_entries_by_each_key() {
        let d = tempfile::tempdir().expect("tempdir");
        touch(d.path(), "b.png", 30);
        touch(d.path(), "a.mp4", 20);
        touch(d.path(), "c.png", 10);
        let mut entries = scan_folder(d.path(), &ScanOptions::default()).expect("scan");
        sort_entries(&mut entries, SortKey::Size, true);
        assert_eq!(names(&entries), vec!["c.png", "a.mp4", "b.png"]);
        sort_entries(&mut entries, SortKey::Size, false);
        assert_eq!(names(&entries), vec!["b.png", "a.mp4", "c.png"]);
        sort_entries(&mut entries, SortKey::Kind, true);
        assert_eq!(names(&entries), vec!["b.png", "c.png", "a.mp4"]);
        sort_entries(&mut entries, SortKey::Kind, false);
        assert_eq!(names(&entries), vec!["a.mp4", "b.png", "c.png"]);
        sort_entries(&mut entries, SortKey::Name, false);
        assert_eq!(names(&entries), vec!["c.png", "b.png", "a.mp4"]);
        let old = SystemTime::UNIX_EPOCH + Duration::from_secs(1_000_000);
        if let Some(e) = entries.iter_mut().find(|e| e.name == "b.png") {
            e.modified = Some(old);
        }
        sort_entries(&mut entries, SortKey::Modified, true);
        assert_eq!(names(&entries).first(), Some(&"b.png"));
    }

    #[test]
    fn show_all_files_lists_non_media_as_other() {
        let d = fixture();
        touch(d.path(), "scene.blend", 3);
        touch(d.path(), ".secret.txt", 1);
        let off = scan_folder(d.path(), &ScanOptions::default()).expect("scan");
        assert!(off.iter().all(|e| e.kind != MediaKind::Other));
        let opts = ScanOptions {
            show_all_files: true,
            ..ScanOptions::default()
        };
        let on = scan_folder(d.path(), &opts).expect("scan");
        let others: Vec<&str> = on
            .iter()
            .filter(|e| e.kind == MediaKind::Other)
            .map(|e| e.name.as_str())
            .collect();
        assert_eq!(others, vec!["notes.txt", "scene.blend"]);
        assert!(
            on.iter()
                .all(|e| e.kind != MediaKind::Other || e.sequence.is_none())
        );
        assert!(names(&on).contains(&"shot_[0001-0003].exr"));
        assert!(!names(&on).contains(&".secret.txt"));
        assert!(!names(&on).contains(&"subdir.png"));
        let hidden = ScanOptions {
            show_all_files: true,
            show_hidden: true,
            ..ScanOptions::default()
        };
        let all = scan_folder(d.path(), &hidden).expect("scan");
        assert!(names(&all).contains(&".secret.txt"));
    }

    #[test]
    fn kind_sort_puts_other_last() {
        let d = tempfile::tempdir().expect("tempdir");
        touch(d.path(), "a.txt", 1);
        touch(d.path(), "b.mp4", 1);
        touch(d.path(), "c.png", 1);
        touch(d.path(), "d.wav", 1);
        let opts = ScanOptions {
            show_all_files: true,
            ..ScanOptions::default()
        };
        let mut entries = scan_folder(d.path(), &opts).expect("scan");
        sort_entries(&mut entries, SortKey::Kind, true);
        assert_eq!(names(&entries), vec!["d.wav", "c.png", "b.mp4", "a.txt"]);
        sort_entries(&mut entries, SortKey::Kind, false);
        assert_eq!(names(&entries), vec!["a.txt", "b.mp4", "c.png", "d.wav"]);
    }

    #[test]
    fn hidden_means_leading_dot() {
        assert!(is_hidden(Path::new("/a/.x")));
        assert!(!is_hidden(Path::new("/a/.x/y")));
        assert!(!is_hidden(Path::new("/a/x.")));
    }
}
