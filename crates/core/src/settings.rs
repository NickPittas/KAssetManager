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
    /// A details list of one folder: the columns, sorted and resizable.
    List,
    /// The same rows, but a folder carries an arrow that opens it in place and indents what is inside. Not a Qt
    /// shape - Qt had grid and details list only - it is here because walking a hierarchy one pane at a time means
    /// re-listing the pane you were reading each time.
    TreeList,
    /// One column per folder level, stepping sideways into folders (Finder's Miller columns). Not a Qt shape: it is
    /// here because walking a hierarchy should keep the path you came in on screen instead of re-listing the pane.
    Columns,
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
    pub show_hidden: bool,
    /// One thumbnail edge per pane, clamped to `THUMBNAIL_MIN..=THUMBNAIL_MAX`. Written as an array, but read
    /// as the single number older files used too - that one applied to every pane, so it becomes both panes' size.
    #[serde(default, deserialize_with = "scalar_or_seq")]
    pub thumbnail_size: Vec<u32>,
    /// Auto-play videos/sequences when selected in the preview pane.
    pub video_preview: bool,
    /// Thumbnail decoder threads (1..=16, default 2).
    pub thumbnail_threads: usize,
    /// Disk thumbnail cache budget in MB (default 1000).
    pub max_cache_mb: u64,
    /// Playback frame cache budget in MB (default 2048).
    pub ram_preview_mb: u64,
    /// Frame rate used for image sequences (default 24).
    pub sequence_fps: f64,
    /// One entry per pane. Qt restores a workspace, not one global view: each pane keeps its own grid-or-list,
    /// thumbnail size and sequence grouping across runs, so index 0 is the first pane and index 1 the second.
    /// The keys are new (`pane_*`) rather than reused because an old file spells these as single values of a type
    /// serde would reject in an array - and a rejected field takes the whole file's settings down with it.
    #[serde(rename = "pane_collapse_sequences", default)]
    pub collapse_sequences: Vec<bool>,
    #[serde(rename = "pane_view_mode", default)]
    pub view_mode: Vec<ViewMode>,
    /// Which folders each pane has opened in its tree list. Per pane, because each pane is a separate workspace;
    /// empty for a pane that has never expanded one.
    #[serde(rename = "pane_tree_expanded", default)]
    pub tree_expanded: Vec<Vec<PathBuf>>,
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
    /// Width of the favourites-and-tree panel, as you last left it. egui remembers a dragged panel for the run it
    /// was given and forgets it after that, so the width is kept here beside the folders.
    pub tree_width: f32,
    /// Width of the preview panel, as you last left it.
    pub preview_width: f32,
    /// Width of the second pane when dual-pane is on. Zero means never dragged - take half the window.
    pub pane2_width: f32,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            thumbnail_size: vec![128, 128],
            show_hidden: false,
            // Off by default: selecting a clip should load it paused so the preview panel has its transport;
            // autoplay on selection is opt-in.
            video_preview: false,
            thumbnail_threads: 2,
            max_cache_mb: 1000,
            ram_preview_mb: 2048,
            collapse_sequences: vec![true, true],
            sequence_fps: 24.0,
            view_mode: vec![ViewMode::Grid, ViewMode::Grid],
            tree_expanded: vec![Vec::new(), Vec::new()],
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
            tree_width: 210.0,
            preview_width: 300.0,
            pane2_width: 0.0,
        }
    }
}

/// A thumbnail edge you can read and that decodes in reasonable time. Qt's control ran 64-320 continuously and its
/// largest preset was 512; the app's slider spans this range and its keyboard steps stay inside it.
pub const THUMBNAIL_MIN: u32 = 64;
pub const THUMBNAIL_MAX: u32 = 512;

struct ScalarOrSeq;

/// Read a `Vec` that older files wrote as one bare value. Serde's answer to a field it cannot parse is to fail the
/// whole file, and losing someone's folders, favorites and column widths over a thumbnail size would be a poor
/// trade - so a number is taken as a list of one, which `Settings::sanitize` then tops up per pane.
impl<'de> serde::de::Visitor<'de> for ScalarOrSeq {
    type Value = Vec<u32>;

    fn expecting(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        f.write_str("a thumbnail size, or a list of them")
    }

    fn visit_seq<A>(self, mut seq: A) -> std::result::Result<Vec<u32>, A::Error>
    where
        A: serde::de::SeqAccess<'de>,
    {
        let mut sizes = Vec::new();
        while let Some(size) = seq.next_element::<u32>()? {
            sizes.push(size);
        }
        Ok(sizes)
    }

    fn visit_u64<E>(self, n: u64) -> std::result::Result<Vec<u32>, E>
    where
        E: serde::de::Error,
    {
        Ok(vec![n.min(u64::from(u32::MAX)) as u32])
    }

    fn visit_i64<E>(self, n: i64) -> std::result::Result<Vec<u32>, E>
    where
        E: serde::de::Error,
    {
        Ok(vec![n.clamp(0, i64::from(u32::MAX)) as u32])
    }

    fn visit_f64<E>(self, n: f64) -> std::result::Result<Vec<u32>, E>
    where
        E: serde::de::Error,
    {
        Ok(vec![n.clamp(0.0, f64::from(u32::MAX)) as u32])
    }
}

/// Make a per-pane list hold exactly two entries. A single value is read as one decision made for both panes -
/// that is what the older single-value settings files meant - rather than as one pane with an unnamed default.
/// A hand-written settings file can carry one pane's value where two are stored. Clone, not `Copy`, so a list of
/// paths can use it too.
fn spread_to_two<T: Clone>(values: &mut Vec<T>, fill: T) {
    if values.len() == 1 {
        let only = values[0].clone();
        values.push(only);
    }
    values.resize(2, fill);
    values.truncate(2);
}

fn scalar_or_seq<'de, D>(deserializer: D) -> std::result::Result<Vec<u32>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    deserializer.deserialize_any(ScalarOrSeq)
}

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
        // One value means "for every pane", which is what an older file's single size meant; two are taken as
        // each pane's own. Longer is trimmed and empty topped up, so the app always has one per pane.
        spread_to_two(&mut self.thumbnail_size, 128);
        for size in &mut self.thumbnail_size {
            // A hand-edited file may say anything; the range is what the app can display, not a set of presets -
            // the control is continuous and snapping would quietly undo the size someone chose.
            *size = (*size).clamp(THUMBNAIL_MIN, THUMBNAIL_MAX);
        }
        self.thumbnail_threads = self.thumbnail_threads.clamp(1, 16);
        self.volume = if self.volume.is_finite() {
            self.volume.clamp(0.0, 1.0)
        } else {
            1.0
        };
        spread_to_two(&mut self.view_mode, ViewMode::Grid);
        spread_to_two(&mut self.tree_expanded, Vec::new());
        spread_to_two(&mut self.collapse_sequences, true);
        self.recents_max = self.recents_max.clamp(1, 100);
        self.sequence_fps = if self.sequence_fps.is_finite() {
            self.sequence_fps.clamp(1.0, 240.0)
        } else {
            24.0
        };
        // Panel widths: the panels themselves refuse anything outside these ranges, so a hand-edited file cannot
        // ask for a tree you cannot see. A zero second-pane width is meaningful ("take half"), so it is not clamped
        // up to the pane minimum here.
        if !self.tree_width.is_finite() {
            self.tree_width = 210.0;
        }
        self.tree_width = self.tree_width.clamp(150.0, 420.0);
        if !self.preview_width.is_finite() {
            self.preview_width = 300.0;
        }
        self.preview_width = self.preview_width.clamp(180.0, 460.0);
        self.pane2_width = if self.pane2_width.is_finite() && self.pane2_width > 0.0 {
            self.pane2_width.clamp(240.0, 4000.0)
        } else {
            0.0
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
            thumbnail_size: vec![256, 64],
            show_hidden: true,
            thumbnail_threads: 8,
            sequence_fps: 30.0,
            view_mode: vec![ViewMode::List, ViewMode::Grid],
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
    fn each_pane_s_view_size_and_folding_survive_the_settings_file() {
        // Qt restores a workspace where the two panes were set up differently. The app copies its panes into these
        // vectors, so a save and load has to bring back two distinct entries rather than one shared value.
        let t = tempfile::tempdir().expect("tempdir");
        let dirs = dirs_in(t.path());
        let s = Settings {
            view_mode: vec![ViewMode::List, ViewMode::Grid],
            thumbnail_size: vec![64, 256],
            collapse_sequences: vec![false, true],
            ..Settings::default()
        };
        s.save(&dirs).expect("save");
        let loaded = Settings::load(&dirs);
        assert_eq!(loaded.view_mode, vec![ViewMode::List, ViewMode::Grid]);
        assert_eq!(loaded.thumbnail_size, vec![64, 256]);
        assert_eq!(loaded.collapse_sequences, vec![false, true]);
        assert_eq!(loaded, s);
    }

    #[test]
    fn panel_widths_roundtrip_and_a_hand_edited_one_is_clamped() {
        // egui forgets a dragged panel width when the run ends, so these three are app settings like any other.
        let t = tempfile::tempdir().expect("tempdir");
        let dirs = dirs_in(t.path());
        assert_eq!(Settings::default().pane2_width, 0.0);
        let s = Settings {
            tree_width: 265.0,
            preview_width: 410.0,
            pane2_width: 380.0,
            ..Settings::default()
        };
        s.save(&dirs).expect("save");
        assert_eq!(Settings::load(&dirs), s);

        // A hand-edited file can ask for a tree wider than the window, a preview narrower than its own minimum,
        // and a second pane that cannot exist. Zero stays zero: it means "take half the window", not "no width".
        std::fs::write(
            dirs.settings_file(),
            r#"{"tree_width": 9000.0, "preview_width": -20.0, "pane2_width": -5.0}"#,
        )
        .expect("write");
        let s = Settings::load(&dirs);
        assert_eq!(s.tree_width, 420.0);
        assert_eq!(s.preview_width, 180.0);
        assert_eq!(s.pane2_width, 0.0);
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
        // A size inside the range is kept as written: one number means both panes, and the control is continuous, so
        // a hand-written 200 is not snapped to a preset any more.
        assert_eq!(s.thumbnail_size, vec![200, 200]);
        assert_eq!(s.thumbnail_threads, 1);
        assert_eq!(s.volume, 1.0);
        assert_eq!(s.recents_max, 1);
        assert_eq!(s.sequence_fps, 1.0);
        assert_eq!(s.recents, vec![PathBuf::from("/a")]);

        let mut s = Settings {
            thumbnail_size: vec![5000, 30],
            thumbnail_threads: 99,
            volume: -1.0,
            recents_max: 1000,
            sequence_fps: 1e9,
            ..Settings::default()
        };
        s.sanitize();
        assert_eq!(
            (
                s.thumbnail_size[0],
                s.thumbnail_threads,
                s.volume,
                s.recents_max,
                s.sequence_fps
            ),
            (512, 16, 0.0, 100, 240.0)
        );
        // A size set by dragging a slider is not a preset, and must survive the load that follows.
        let mut s = Settings {
            thumbnail_size: vec![150, 300],
            ..Settings::default()
        };
        s.sanitize();
        assert_eq!(s.thumbnail_size, vec![150, 300]);

        let mut s = Settings {
            thumbnail_size: vec![0, 0],
            volume: f32::NAN,
            sequence_fps: f64::NAN,
            ..Settings::default()
        };
        s.sanitize();
        assert_eq!(
            (s.thumbnail_size[0], s.volume, s.sequence_fps),
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
        // The Python-era file spells one size for every pane; that number becomes both panes' size rather than
        // taking the whole settings file down with it.
        assert_eq!(s.thumbnail_size, vec![512, 512]);
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
