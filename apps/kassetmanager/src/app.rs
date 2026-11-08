//! The egui application shell. Slice 8 of M1: the dual-pane browser with file operations — each pane is
//! an independent folder view (breadcrumb, subfolder list, grid/list of engine thumbnails, selection and
//! preview), copy/cut/paste/trash/rename run through the engine's [`FileOpsWorker`] with undo/redo, and the
//! app owns the shared parts: the engine [`gallerybrowser_media::Thumbnailer`], one
//! [`gallerybrowser_player::Player`], settings, and the wgpu textures.

use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};

use gallerybrowser_core::{
    AppDirs, Column, Entry, MediaKind, ScanOptions, Settings, SortKey, Theme, ViewMode, is_hidden,
    scan_folder, sort_entries,
};
use gallerybrowser_fileops::{ConflictPolicy, FileOp, FileOpsWorker, OpEvent, OpOutcome};
use gallerybrowser_library::Library;
use gallerybrowser_media::{Thumbnailer, ThumbnailerConfig, Tick, frame_to_rgba, open};
use gallerybrowser_player::Player;

/// A path written as a `file:` URI, the shape a file manager expects off the clipboard. Only the characters
/// that would break the URI are escaped (`ponytail:`, not percent-encoding every byte a browser would reject);
/// the upgrade path is a proper UTF-8 percent-encode when a file manager misreads a name we did not expect.
fn file_uri(path: &Path) -> String {
    let text = path.display().to_string();
    #[cfg(windows)]
    // `C:\dir\file.png` reads as `file:///C:/dir/file.png`.
    let text = text.replace('\\', "/");
    let mut out = String::with_capacity(text.len() + 8);
    out.push_str(if text.starts_with('/') {
        "file://"
    } else {
        "file:///"
    });
    for ch in text.chars() {
        match ch {
            '%' => out.push_str("%25"),
            ' ' => out.push_str("%20"),
            '#' => out.push_str("%23"),
            '?' => out.push_str("%3F"),
            _ => out.push(ch),
        }
    }
    out
}

/// A path as the app stores it. The tree's roots come from mount points and drive letters, so a relative start
/// folder - `.` when a file has no parent, or anything typed on the command line - could never be matched,
/// expanded or revealed there. Resolved against the process's own directory; symlinks are left as the user typed
/// them, because revealing a path the user wrote is not the same as resolving what it points at.
pub fn absolute(path: &Path) -> PathBuf {
    if path.is_absolute() {
        path.to_path_buf()
    } else {
        std::env::current_dir().unwrap_or_default().join(path)
    }
}

/// Local file time the way a file browser writes it. `chrono::Local` is the operating system's own clock, so
/// no tz database ships with us and daylight saving comes from the platform.
fn date_label(when: Option<std::time::SystemTime>) -> String {
    when.map(|when| {
        chrono::DateTime::<chrono::Local>::from(when)
            .format("%Y-%m-%d %H:%M")
            .to_string()
    })
    .unwrap_or_default()
}

/// Bytes the way a file browser says it: exact below a kilobyte, one decimal above.
fn format_size(bytes: u64) -> String {
    const UNITS: [&str; 5] = ["B", "KB", "MB", "GB", "TB"];
    let mut value = bytes as f64;
    let mut unit = 0usize;
    while value >= 1024.0 && unit + 1 < UNITS.len() {
        value /= 1024.0;
        unit += 1;
    }
    if unit == 0 {
        format!("{bytes} {}", UNITS[0])
    } else {
        format!("{value:.1} {}", UNITS[unit])
    }
}

/// The Type column's word for an entry. Qt spells extensions out ("PNG image"); the kind is already a tag in
/// the Name cell, so this stays short and matches how the column groups when sorted.
fn type_label(kind: MediaKind, is_dir: bool, path: &Path) -> String {
    if is_dir {
        return "Folder".to_string();
    }
    match kind {
        MediaKind::Image => "Image",
        MediaKind::Video => "Video",
        MediaKind::Sequence => "Sequence",
        MediaKind::Audio => "Audio",
        MediaKind::Other => {
            return path
                .extension()
                .map(|ext| format!("{} file", ext.to_string_lossy().to_uppercase()))
                .unwrap_or_else(|| "File".to_string());
        }
    }
    .to_string()
}

/// Longest preview edge we ask the decoders for (they may scale down while decoding).
const PREVIEW_EDGE: u32 = 1024;

/// Hover scrub, with GalleryBrowser's numbers (crates/ui-egui/src/file_pane.rs:42-50): the pointer's x
/// across a clip's thumbnail picks a slot out of `SCRUB_SLOTS`, or out of `SCRUB_SLOTS_FINE` with Ctrl held
/// ("real-time" scrubbing); a decoded neighbour is shown while the exact frame is made; a frame still missing
/// after `SCRUB_WAIT` gives up. Declared before porting, frozen in tests after verification.
/// Qt watched each pane's folder and debounced its refresh 1200 ms after a change signal
/// (`mainwindow.cpp:355-361`); this is the same interval, paid for by waking once per tick instead of carrying a
/// watcher dependency. Slower than a signal, cheap enough that nobody notices.
/// How far the pointer has to travel before a press counts as carrying files. egui's own click tolerance is 6
/// points (`InputOptions::max_click_dist`); below that it is still deciding whether this was a click.
const DRAG_MIN_POINTS: f32 = 6.0;

const LIGHT_REFRESH_SECS: f64 = 1.2;

const SCRUB_SLOTS: u32 = 24;
const SCRUB_SLOTS_FINE: u32 = 200;
const SCRUB_NEAR: u32 = 12;
const SCRUB_WAIT: std::time::Duration = std::time::Duration::from_secs(5);

/// A run of numbered frames must reach this length before it is worth folding into one entry.
const MIN_SEQUENCE_LEN: usize = 2;

/// One pane of the browser: a folder, what it contains and its selection.
struct Pane {
    path: PathBuf,
    /// Folders this pane visited, newest last. `navigate` records the folder it leaves, `go_back` pops
    /// without recording, so Back walks history without inflating it. Capped to keep memory bounded.
    back_stack: Vec<PathBuf>,
    entries: Vec<Entry>,
    /// The folders inside this pane's folder, which come first in `entries`. Qt's browser lists them; the
    /// engine's scan returns files only (`crates/core/src/scan.rs:62`), so the app synthesises these entries
    /// and keeps their paths here to tell a folder from a file without touching the engine's kinds.
    dir_paths: std::collections::HashSet<PathBuf>,
    /// The pane's folder's own modification time as last listed. A create, delete or rename *inside* a folder
    /// moves that mtime, which is what the slow poll compares against; content edits to a file do not.
    folder_mtime: Option<std::time::SystemTime>,
    /// Files clicked in this pane. The last one is the **primary**: the preview panel, the player and the
    /// status bar follow it, as Qt's extended selection follows its current item.
    selection: Vec<PathBuf>,
    /// The entry whose preview we asked the engine for.
    wanted: Option<PathBuf>,
    /// Decoded picture, uploaded to wgpu: `(path, texture)`.
    preview: Option<(PathBuf, egui::TextureHandle)>,
    /// Cells per row as the grid last drew itself; 1 in list view. The arrows need it because Down means down a
    /// row, and only the layout knows how wide a row is.
    grid_cols: usize,
    /// Row or cell to bring into view the next time it draws, set when the keyboard moves the current item.
    scroll_target: Option<PathBuf>,
    /// File the pointer is over right now; drives hover scrub and resets its state on change.
    hover: Option<PathBuf>,
    /// Slot the pointer's x picks, with its slot count: `(path, slot, slots)`.
    scrub_target: Option<(PathBuf, u32, u32)>,
    /// When the current target began; after `SCRUB_WAIT` stop asking for it (undecodable frame).
    scrub_wait_until: std::time::Instant,
    /// Decoded scrub frames for the hovered entry, by `(path, slot, slots)`.
    scrub_frames: std::collections::HashMap<(PathBuf, u32, u32), egui::TextureHandle>,
    /// The frame to draw, and the file it belongs to.
    scrub_shown: Option<(PathBuf, egui::TextureHandle)>,
}

/// Reveal a folder in the platform's file manager (Qt's "Open in Explorer", `mw.cpp:3172-3201`). The child
/// is not ours to wait for - a file manager outlives the app - so it is spawned and forgotten.
#[cfg(target_os = "linux")]
fn open_in_file_manager(path: &Path) -> std::io::Result<()> {
    std::process::Command::new("xdg-open")
        .arg(path)
        .spawn()
        .map(|_child| ())
}

#[cfg(target_os = "macos")]
fn open_in_file_manager(path: &Path) -> std::io::Result<()> {
    std::process::Command::new("open")
        .arg(path)
        .spawn()
        .map(|_child| ())
}

#[cfg(target_os = "windows")]
fn open_in_file_manager(path: &Path) -> std::io::Result<()> {
    std::process::Command::new("explorer")
        .arg(path)
        .spawn()
        .map(|_child| ())
}

/// True when the pointer was pressed inside `rect` this frame. Lets a click anywhere on a pane's
/// toolbar - whitespace, the size label, a disabled button - name the pane it belongs to, without an
/// interaction rect laid over the row and swallowing the buttons' own clicks.
fn pressed_within(rect: egui::Rect, pressed: bool, pos: Option<egui::Pos2>) -> bool {
    rect.is_positive() && pressed && pos.is_some_and(|point| rect.contains(point))
}

/// Where the arrow keys take the current item. Qt's views do this on their own (`QItemSelectionModel`), so
/// there is no key handling in its sources to port - egui has no default, and the port has to say it.
///
/// The arrows are axes, not one flat order: Down means **down a row** in a grid (a whole row of cells) and one
/// item in a list, which is what `Pane::grid_cols` tells `Pane::step` about the layout as it last drew itself.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Step {
    /// One item back - Left in both views.
    ItemBack,
    /// One item forward - Right.
    ItemForward,
    /// One row up - Up, which is one item where a row is one cell wide (list view).
    RowBack,
    /// One row down - Down.
    RowForward,
    First,
    Last,
}

/// File-operation shortcuts, read once per frame and suppressed while a text field has focus.
#[derive(Default)]
struct FileKeys {
    settings: bool,
    copy: bool,
    cut: bool,
    paste: bool,
    undo: bool,
    redo: bool,
    trash: bool,
    /// Shift+Delete: permanent delete, which is not undoable.
    perm_delete: bool,
    /// Backspace goes to the parent folder (Qt's own shortcut table calls it "Back to Parent"); the
    /// toolbar's Back button is the only way back through history.
    up_parent: bool,
    refresh: bool,
    new_folder: bool,
    rename: bool,
    /// Digit 0-5 pressed to rate the selection.
    rating: Option<u8>,
    /// Arrows / Home / End move the current item through the active pane's listing.
    step: Option<Step>,
    /// Enter activates the current item: enter a folder, play a file. Qt's views activate on Return.
    activate: bool,
}

/// One `App` per window: two panes (the second only when `dual_pane` is on) plus the shared engine
/// plumbing.
/// The left browser column's view state: which folders are open, and the subfolder listing cached per
/// opened folder. Qt fetches children lazily and asynchronously (`everything_folder_model.cpp:573-620`);
/// here a folder is read once when it first opens, because a directory of subfolders is small enough to
/// list inline and a failed read (a drive we cannot stat) simply has no children.
struct TreeState {
    computer_open: bool,
    /// Every rendered row asks "am I open?", so this is a set rather than a scanned list.
    open: HashSet<PathBuf>,
    kids: HashMap<PathBuf, Vec<PathBuf>>,
}

impl Default for TreeState {
    fn default() -> Self {
        // Drives are listed from the start: the point of the tree is to get somewhere without typing.
        Self {
            computer_open: true,
            open: HashSet::new(),
            kids: HashMap::new(),
        }
    }
}

/// The tree's roots under "Computer" - Qt uses `QDir::drives()` (`mw.cpp:1408`).
/// `/proc/mounts` escapes whitespace in a mount point as an octal code, so a drive called "My Drive" is
/// written `/mnt/My\040Drive`. Only the four escapes the kernel emits for whitespace and a literal
/// backslash appear there; the backslash is decoded last so a decoded character is never re-read.
#[cfg(target_os = "linux")]
fn unmount_escape(field: &str) -> String {
    field
        .replace("\\040", " ")
        .replace("\\011", "\t")
        .replace("\\012", "\n")
        .replace("\\134", "\\")
}

#[cfg(target_os = "linux")]
fn drive_roots() -> Vec<PathBuf> {
    // Mount points whose source is itself a path (a real device, not sysfs/proc/tmpfs).
    let mut roots: Vec<PathBuf> = std::fs::read_to_string("/proc/mounts")
        .unwrap_or_default()
        .split('\n')
        .filter_map(|line| {
            let mut fields = line.split_whitespace();
            let device = fields.next()?;
            let point = PathBuf::from(unmount_escape(fields.next()?));
            device.starts_with('/').then_some(point)
        })
        .collect();
    // Qt's roots come from `QDir::drives()`, which always carries the filesystem root - a ZFS or overlay
    // install lists `/` against a non-path source and would otherwise show no root at all. A bind mount can
    // also name a file (`/etc/hosts`), which is not a folder anyone wants to browse.
    roots.push(PathBuf::from("/"));
    roots.retain(|point| point.is_dir());
    roots.sort();
    roots.dedup();
    roots
}

#[cfg(target_os = "macos")]
fn drive_roots() -> Vec<PathBuf> {
    let mut roots = vec![PathBuf::from("/")];
    if let Ok(entries) = std::fs::read_dir("/Volumes") {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                roots.push(path);
            }
        }
    }
    roots
}

#[cfg(target_os = "windows")]
fn drive_roots() -> Vec<PathBuf> {
    // No std API for drive letters; probing A:\..Z:\ is cheap and needs no platform crate.
    ('A'..='Z')
        .filter_map(|letter| {
            let root = PathBuf::from(format!("{letter}:\\"));
            root.is_dir().then_some(root)
        })
        .collect()
}

#[cfg(not(any(target_os = "linux", target_os = "macos", target_os = "windows")))]
fn drive_roots() -> Vec<PathBuf> {
    vec![PathBuf::from("/")]
}

pub struct App {
    panes: [Pane; 2],
    /// Pane that keyboard and status-bar affordances act on.
    active: usize,
    status: String,
    /// Engine settings; drives view mode, thumbnail size, filters and theme.
    settings: Settings,
    thumbs: Thumbnailer,
    /// Uploaded thumbnails by entry path (shared by both panes).
    pictures: HashMap<PathBuf, egui::TextureHandle>,
    /// Thumbnail requests still in flight (drives repainting until the engine answers).
    outstanding: usize,
    /// Video and audio playback of the double-clicked file.
    player: Player,
    /// The file loaded in the player.
    playing: Option<PathBuf>,
    /// File operations run on their own thread with undo/redo history (engine `fileops`).
    ops: FileOpsWorker,
    /// Path to paste into a pane, and whether the source should move (cut) rather than stay (copy).
    /// Paths to paste into a pane, and whether the sources should move (cut) rather than stay (copy).
    clipboard: Option<(Vec<PathBuf>, bool)>,
    /// Rename box state for the active pane's selection.
    renaming: bool,
    rename_buffer: String,
    /// Set once when the rename box opens, so its text field takes the keyboard.
    rename_focus: bool,
    /// Readout of the op in flight, shown until it finishes.
    op_progress: String,
    /// A cut is spent once its move lands; this is what tells `poll_ops` to clear the clipboard.
    /// The sources of a cut that is waiting to move. Each is consumed when an operation actually carries
    /// it, so an unrelated move or undo cannot spend the clipboard, and failed sources stay available.
    /// True while one of our own text fields (rename box, annotation name, path edit) holds the keyboard, so
    /// it can swallow letters without the browser's shortcuts firing too. Recomputed each frame from focus.
    /// The paths this app copied or cut, published to the operating system's clipboard as one `file:` URI per
    /// line. egui-winit hands over a `Paste` event only when the OS clipboard holds text, so with an empty OS
    /// clipboard Ctrl+V produces no event at all and keyboard paste stays dead however long our own list is.
    /// URIs rather than bare paths because the same text is what a file manager reads: a plain path line pastes
    /// as a text file there, where `file:///…` at least says what it is. The ceiling is honest - KDE and GNOME
    /// want an `x-special/gnome-copied-files` offer for a real file paste, and egui's clipboard carries text
    /// only (`egui::platform::OsClipboard::set_text`), so that needs the Wayland data-device work internal
    /// drag and drop needs too.
    clipboard_publish: Option<String>,

    /// Ids of our own text fields, recorded while they hold focus, so a keystroke arriving in the same input
    /// batch as the click that focused one cannot reach the browser (see `read_file_keys`).
    rename_field_id: Option<egui::Id>,
    annotate_field_id: Option<egui::Id>,

    /// True in the frame an operation wrote its errors to the status bar, so a no-op notice ("Nothing
    /// selected") cannot overwrite what the user needs to read.
    op_error_shown: bool,

    typing_in_field: bool,

    cut_pending: Option<Vec<PathBuf>>,
    /// Requests issued for the current thumbnail batch. The engine dedupes by path and size within a
    /// batch and answers a duplicate with nothing, so counting one here per *issued* request is the only
    /// way the counter can reach zero (otherwise we repaint every frame forever).
    issued: std::collections::HashSet<(PathBuf, u32)>,
    /// When the last engine answer arrived; lets a silent engine end the repaint drive instead of
    /// spinning on a request that will never be answered.
    stall: std::time::Instant,
    /// Latest decoded video frame, uploaded (only while something is loaded).
    frame: Option<egui::TextureHandle>,
    /// Engine data directories; `None` in tests so nothing is written outside the project.
    dirs: Option<AppDirs>,
    /// Project catalog: tags, collections and ratings keyed by path. Opened from `AppDirs`; in memory
    /// when there is no writable data directory (tests), so the feature still works.
    library: Library,
    /// Annotation list a modal is editing, with its text buffer.
    annotate: Option<Annotate>,
    annotate_focus: bool,
    /// Which folders the tree has open, and its cached listings.
    tree: TreeState,
    /// Last (active pane, folder it shows). The tree follows a change here, not every frame.
    tree_revealed: Option<(usize, PathBuf)>,
    /// Folder whose tree row should be scrolled into view on the next draw, then cleared.
    tree_reveal: Option<PathBuf>,
    /// Wall clock (egui's own seconds) at which the panes' folders are next re-read. A deadline, not a timer:
    /// egui is asked to wake up at this rate and stays asleep between ticks.
    next_light_check: f64,
    /// Whether where the panes are is worth writing down. Off until the remembered folders have been read back:
    /// the window's own first listing would otherwise record the folder it guessed (Pictures) over the folder it
    /// was about to restore - which is exactly what it did before this flag existed.
    folders_remembered: bool,
    /// Which pane's header is being dragged, and which column; the drop index is computed from the pointer
    /// each frame. The owner matters: both panes draw headers, and only the one that started the drag may
    /// resolve its release - the other would answer with its own widths and coordinates.
    column_drag: Option<(usize, Column)>,
    /// How far each pane's list is scrolled sideways; the pinned header slides by the same amount.
    list_scroll_x: [f32; 2],
    settings_open: bool,
    /// Theme last pushed to egui, so we only re-set visuals when it changes.
    theme_applied: Option<Theme>,
}

impl Pane {
    /// Move the current item and return it. A row move travels `cols` items - one row of the grid as it is
    /// laid out - and a list is one cell wide, so there Down is simply the next file. Home and End jump to the
    /// ends; an arrow with nowhere to go stays put rather than wrapping or sliding into a neighbour column. A
    /// multi-selection collapses onto the new current item, what Qt's model does when you arrow after one.
    fn step(&mut self, step: Step, cols: usize) -> Option<PathBuf> {
        let paths: Vec<PathBuf> = self.entries.iter().map(|e| e.path.clone()).collect();
        if paths.is_empty() {
            return None;
        }
        let last = paths.len() - 1;
        let cols = cols.max(1) as isize;
        let at = self
            .primary()
            .and_then(|p| paths.iter().position(|x| x == p));
        let at = match (at, step) {
            (None, Step::RowBack | Step::ItemBack | Step::Last) => last,
            (None, Step::RowForward | Step::ItemForward | Step::First) => 0,
            (Some(_), Step::First) => 0,
            (Some(_), Step::Last) => last,
            (Some(i), step) => {
                let delta = match step {
                    Step::ItemBack => -1,
                    Step::ItemForward => 1,
                    Step::RowBack => -cols,
                    Step::RowForward => cols,
                    _ => 0,
                };
                let next = i as isize + delta;
                if next < 0 || next > last as isize {
                    i
                } else {
                    next as usize
                }
            }
        };
        let path = paths[at].clone();
        self.selection = vec![path.clone()];
        self.scroll_target = Some(path.clone());
        Some(path)
    }

    /// The file the preview panel, player and status bar follow: the last one clicked.
    fn primary(&self) -> Option<&Path> {
        self.selection.last().map(PathBuf::as_path)
    }

    fn is_selected(&self, path: &Path) -> bool {
        self.selection.iter().any(|p| p == path)
    }

    /// Clicking replaces the selection; Ctrl-click adds to it or takes one away. Returns the file that is
    /// now primary, or `None` when a Ctrl-click emptied the pane - Qt keeps its last preview in that case
    /// rather than clearing the panel.
    fn pick(&mut self, clicked: PathBuf, ctrl: bool) -> Option<PathBuf> {
        if ctrl {
            match self.selection.iter().position(|p| *p == clicked) {
                Some(at) => {
                    self.selection.remove(at);
                }
                None => self.selection.push(clicked),
            }
        } else {
            self.selection.clear();
            self.selection.push(clicked);
        }
        self.selection.last().cloned()
    }

    /// Drop selected files that are no longer in the listing (a delete, a move, an external change).
    fn keep_existing(&mut self) {
        self.selection
            .retain(|path| self.entries.iter().any(|e| &e.path == path));
    }

    fn new(path: PathBuf) -> Self {
        Self {
            path,
            back_stack: Vec::new(),
            entries: Vec::new(),
            dir_paths: std::collections::HashSet::new(),
            folder_mtime: None,
            selection: Vec::new(),
            wanted: None,
            preview: None,
            grid_cols: 1,
            scroll_target: None,
            hover: None,
            scrub_target: None,
            scrub_wait_until: std::time::Instant::now(),
            scrub_frames: std::collections::HashMap::new(),
            scrub_shown: None,
        }
    }
}

/// Whether the pointer has travelled far enough since it was pressed to count as carrying files rather than
/// clicking. egui decides a press is a drag after 0.8 s even if the mouse never moved, so motion - not time - is
/// what separates "I am moving these" from "I rested on this file a moment too long".
fn carried_by_drag(start: egui::Pos2, now: egui::Pos2) -> bool {
    start.distance(now) > DRAG_MIN_POINTS
}

/// What a drag inside this app carries: the files being carried and the folder they came from. egui's built-in
/// drag and drop keeps its payload in-process (`DragAndDrop` holds an `Arc<dyn Any>`; nothing goes through the
/// operating system clipboard), so paths arrive whole instead of as text something else would have to parse.
#[derive(Clone, Debug)]
struct Dragged {
    paths: Vec<PathBuf>,
    from: PathBuf,
}

/// Which catalog list a modal is editing for the selected file.
#[derive(Clone, Copy, PartialEq, Eq)]
enum AnnotateKind {
    Tag,
    Collection,
}

/// One annotation edit: which list, what text, and the file it applies to.
struct Annotate {
    kind: AnnotateKind,
    buffer: String,
    path: PathBuf,
}

/// Open the project catalog in the engine's data directory. A catalog that cannot be opened (read-only
/// disk, another instance holds the lock) must not stop browsing, so we fall back to an in-memory one
/// and report why in the status bar.
fn open_library(dirs: Option<&AppDirs>) -> (Library, Option<String>) {
    let Some(dirs) = dirs else {
        return (Library::in_memory(), None);
    };
    let dir = dirs.library_dir();
    match Library::open(&dir, dirs.legacy_database().as_deref()) {
        Ok(library) => (library, None),
        Err(err) => (
            Library::in_memory(),
            Some(format!("catalog in memory only: {err}")),
        ),
    }
}

impl App {
    pub fn new(path: PathBuf) -> Self {
        let dirs = AppDirs::resolve().ok();
        let settings = dirs.as_ref().map(Settings::load).unwrap_or_default();
        let cache_dir = dirs
            .as_ref()
            .map(|dirs| dirs.thumbnails_dir())
            .unwrap_or_default();
        Self::build(path, cache_dir, dirs, settings)
    }

    /// Test constructor: engine defaults and a project-local thumbnail cache, no data directories.
    #[cfg(test)]
    pub fn with_cache(path: PathBuf, cache_dir: PathBuf) -> Self {
        Self::build(path, cache_dir, None, Settings::default())
    }

    fn build(path: PathBuf, cache_dir: PathBuf, dirs: Option<AppDirs>, settings: Settings) -> Self {
        let path = absolute(&path);
        let thumbs = Thumbnailer::new(ThumbnailerConfig {
            cache_dir,
            memory_bytes: 32 << 20,
            // Engine default from `Settings`: 1000 MB disk cache; decoder threads come from settings.
            disk_bytes: 1_000 << 20,
            threads: settings.thumbnail_threads.clamp(1, 16),
        });
        let (library, library_note) = open_library(dirs.as_ref());
        let mut app = Self {
            // Both panes start at the same folder; navigating one never moves the other.
            panes: [Pane::new(path.clone()), Pane::new(path)],
            active: 0,
            status: String::new(),
            settings,
            thumbs,
            pictures: HashMap::new(),
            outstanding: 0,
            player: Player::new(64 << 20),
            playing: None,
            frame: None,
            ops: FileOpsWorker::new(),
            clipboard: None,
            renaming: false,
            rename_buffer: String::new(),
            rename_focus: false,
            op_progress: String::new(),
            clipboard_publish: None,
            rename_field_id: None,
            annotate_field_id: None,
            op_error_shown: false,
            typing_in_field: false,
            cut_pending: None,
            issued: std::collections::HashSet::new(),
            stall: std::time::Instant::now(),
            library,
            annotate: None,
            annotate_focus: false,
            dirs,
            tree: TreeState::default(),
            tree_revealed: None,
            tree_reveal: None,
            next_light_check: 0.0,
            folders_remembered: false,
            column_drag: None,
            list_scroll_x: [0.0, 0.0],
            settings_open: false,
            theme_applied: None,
        };
        app.open(0);
        if app.settings.dual_pane {
            app.open(1);
        }
        if let Some(note) = library_note {
            app.status = note;
        }
        app
    }

    /// How the engine should read a folder, from the current settings.
    fn scan_options(&self) -> ScanOptions {
        ScanOptions {
            show_hidden: self.settings.show_hidden,
            collapse_sequences: self.settings.collapse_sequences,
            min_sequence_len: MIN_SEQUENCE_LEN,
            show_all_files: self.settings.show_all_files,
        }
    }

    /// Re-read pane `index` and re-batch its thumbnails. Called on every navigation.
    fn open(&mut self, index: usize) {
        self.open_quiet(index);
        self.refresh_thumbs();
    }

    /// How many panes are on screen (the second only when dual pane is on).
    fn panes_visible(&self) -> usize {
        usize::from(self.settings.dual_pane) + 1
    }

    /// Ask the engine for thumbnails of everything visible, as **one** batch. `begin_batch` invalidates
    /// the previous generation's queued work and drops decoded-but-unpolled results, so it must not run
    /// once per pane. Only requests the engine will actually run are counted (it dedupes by path and size
    /// within a batch); otherwise the counter never reaches zero and we repaint every frame forever.
    fn refresh_thumbs(&mut self) {
        self.thumbs.begin_batch();
        self.issued.clear();
        let edge = self.settings.thumbnail_size;
        let visible = self.panes_visible();
        // Textures for files that left the screen are dropped: the texture manager never dedupes, so
        // without this every thumbnail ever viewed stays allocated for the session.
        let mut keep: std::collections::HashSet<PathBuf> = std::collections::HashSet::new();
        // ponytail: whole folder at once; virtualise to visible rows if a huge folder feels slow.
        for index in 0..visible {
            for entry in &self.panes[index].entries {
                keep.insert(entry.path.clone());
                // A folder has no picture to decode; asking the engine would only log a failure per folder.
                if self.panes[index].dir_paths.contains(&entry.path) {
                    continue;
                }
                if self.issued.insert((entry.path.clone(), edge)) {
                    self.thumbs.request(entry, edge, 0);
                }
            }
        }
        self.pictures.retain(|path, _| keep.contains(path));
        for index in 0..visible {
            let Some(wanted) = self.panes[index].wanted.clone() else {
                continue;
            };
            if !keep.contains(&wanted) || !self.issued.insert((wanted.clone(), PREVIEW_EDGE)) {
                continue;
            }
            let Some(entry) = self.panes[index].entries.iter().find(|e| e.path == wanted) else {
                continue;
            };
            self.thumbs.request(entry, PREVIEW_EDGE, 0);
        }
        // Count what the engine will actually run, not what we would like it to have run.
        self.outstanding = self.issued.len();
        self.stall = std::time::Instant::now();
    }

    /// Re-read pane `index` without touching the thumbnail batch, so a multi-pane rescan batches once.
    /// A selection survives when its file is still in the folder, and its preview request survives with it.
    fn open_quiet(&mut self, index: usize) {
        // A pane always shows what its folder holds right now. The tree's cached listing is a separate
        // concern: keeping it here would list a folder that stopped being one - replaced by a file of the
        // same name from outside the app - twice over, once as the file the scan found and once as the
        // synthesised folder entry from the stale cache.
        self.tree.kids.remove(&self.panes[index].path);
        let opts = self.scan_options();
        let path = self.panes[index].path.clone();
        match scan_folder(&path, &opts) {
            Ok(mut entries) => {
                let (key, ascending) = (self.settings.sort_key, self.settings.sort_ascending);
                sort_entries(&mut entries, key, ascending);
                // Qt lists the folders inside the one you are browsing, ahead of the files. They ride as
                // ordinary entries so selection and file operations reach them; `dir_paths` is what says a
                // folder is a folder (the engine has no folder kind, and adding one for a browser-side
                // listing would ripple through classification, thumbnails and scrubbing for nothing).
                let dirs = self.child_dirs(&path);
                let mut folders: Vec<Entry> = Vec::new();
                for dir in &dirs {
                    let Some(name) = dir.file_name().map(|n| n.to_string_lossy().into_owned())
                    else {
                        continue;
                    };
                    folders.push(Entry {
                        path: dir.clone(),
                        name,
                        kind: MediaKind::Other,
                        size: 0,
                        modified: std::fs::metadata(dir).ok().and_then(|m| m.modified().ok()),
                        sequence: None,
                    });
                }
                // Directories lead, and each group obeys the user's sort on its own.
                sort_entries(&mut folders, key, ascending);
                let mut all = folders;
                all.extend(entries);
                self.panes[index].dir_paths = dirs.into_iter().collect();
                self.panes[index].entries = all;
                self.status.clear();
            }
            Err(err) => {
                self.panes[index].entries.clear();
                self.status = format!("{err}");
            }
        }
        // What the listing on screen corresponds to. A folder that changed since is caught by the slow poll;
        // a folder that could not be read at all stays unset so the next tick tries again.
        self.panes[index].folder_mtime = std::fs::metadata(&path)
            .ok()
            .and_then(|meta| meta.modified().ok());
        self.remember_current_folders();
        let before = self.panes[index].primary().map(Path::to_path_buf);
        self.panes[index].keep_existing();
        if self.panes[index].primary().map(Path::to_path_buf) != before {
            // The file the preview panel follows is gone, or no longer primary: its request and its picture
            // go with it. Clearing only when the selection emptied left the departed file on screen while
            // the operations acted on the file that remained.
            self.panes[index].wanted = None;
            self.panes[index].preview = None;
        }
    }

    /// Enter `dir` in pane `index`, and make it the active pane.
    fn navigate(&mut self, index: usize, dir: &Path) {
        let dir = dir.to_path_buf();
        if dir.is_dir() {
            let previous = std::mem::replace(&mut self.panes[index].path, dir);
            if previous != self.panes[index].path && previous.exists() {
                self.panes[index].back_stack.push(previous);
                // ponytail: 50 is Qt-shaped, not needed; drop the oldest past that.
                if self.panes[index].back_stack.len() > 50 {
                    self.panes[index].back_stack.remove(0);
                }
            }
            self.active = index;
            self.open(index);
        } else {
            self.status = format!("{} is not a folder", dir.display());
        }
    }

    /// The toolbar's Back: this pane walks its own recorded folders. Going back is not a new visit.
    fn go_back(&mut self, index: usize) {
        // A visited folder can be deleted or moved by an undo, so Back must not walk into a path that
        // stopped being a folder. Dead entries are dropped as they are found.
        while let Some(dir) = self.panes[index].back_stack.pop() {
            if dir.is_dir() {
                self.panes[index].path = dir;
                self.active = index;
                self.open(index);
                return;
            }
        }
    }

    /// New Folder on the toolbar or Ctrl+Shift+N. The worker creates it, so undo removes it again.
    fn new_folder(&mut self) {
        let parent = self.panes[self.active].path.clone();
        self.new_folder_in(&parent);
    }

    /// A folder created under `parent` (the active pane, or a node clicked in the tree).
    fn new_folder_in(&mut self, parent: &Path) {
        let parent = parent.to_path_buf();
        let mut name = "New Folder".to_string();
        let mut candidate = 2;
        while parent.join(&name).exists() {
            name = format!("New Folder {candidate}");
            candidate += 1;
        }
        self.ops.submit(FileOp::NewFolder { parent, name });
    }

    /// Shift+Delete: gone rather than trashed, and not undoable (Qt's rule as well).
    fn delete_permanently(&mut self) {
        let paths = self.targets();
        if !paths.is_empty() {
            self.ops.submit(FileOp::Delete { paths });
        }
    }

    /// The toolbar's thumbnail stepper. Only the engine's four cache sizes, so a size change reuses
    /// cached thumbnails instead of asking the decoder for a new edge length.
    fn step_thumbnails(&mut self, direction: i32) {
        const SIZES: [u32; 4] = [64, 128, 256, 512];
        let position = SIZES
            .iter()
            .position(|size| *size == self.settings.thumbnail_size)
            .unwrap_or(1);
        let next = (position as i32 + direction).clamp(0, SIZES.len() as i32 - 1) as usize;
        if SIZES[next] != self.settings.thumbnail_size {
            self.settings.thumbnail_size = SIZES[next];
            self.save_settings();
            self.refresh_thumbs();
        }
    }

    /// Select `path` in pane `index` and ask the engine for its preview (once per selection).
    pub fn select(&mut self, path: PathBuf) {
        self.select_in(self.active, path, false);
    }

    fn select_in(&mut self, index: usize, clicked: PathBuf, ctrl: bool) {
        self.active = index;
        let Some(path) = self.panes[index].pick(clicked, ctrl) else {
            return;
        };
        let entry = self.panes[index]
            .entries
            .iter()
            .find(|e| e.path == path)
            .cloned();
        // The player follows the selection whichever pane it came from: a clip selected in one pane must not
        // leave the other pane's clip running the preview panel. Loaded paused, because selection must not
        // start sound (user rule), and reconciled before any early return about preview requests - those are
        // deduplicated per pane. `load` no-ops when this path is already loaded, so reselecting is cheap.
        if let Some(entry) = entry.as_ref()
            && matches!(
                entry.kind,
                MediaKind::Video | MediaKind::Audio | MediaKind::Sequence
            )
        {
            self.load(path.clone(), false);
        } else if self.frame.is_some() {
            // The primary is a still image or a folder, so the previous clip's decoded frame must not linger
            // as the picture. The transport itself stays loaded and paused: selecting a still neither starts
            // nor stops playback (verified earlier), which is why this clears the frame and not the player.
            self.frame = None;
        }
        let pane = &mut self.panes[index];
        if pane.wanted.as_deref() == Some(path.as_path()) {
            return;
        }
        let Some(entry) = entry else {
            // The file is filtered out of the listing, so there is nothing to decode: don't wait forever.
            pane.wanted = None;
            return;
        };
        pane.wanted = Some(path.clone());
        // Preview requests ride the current batch; they don't invalidate the grid's thumbnails. The
        // engine answers a request for an already-queued (path, size) with nothing, so only a *new*
        // request may raise the counter.
        if self.issued.insert((path.clone(), PREVIEW_EDGE)) {
            self.thumbs.request(&entry, PREVIEW_EDGE, 0);
            self.outstanding += 1;
            self.stall = std::time::Instant::now();
        }
    }

    /// Load `path` in the player and start it playing (double-click).
    pub fn play(&mut self, path: PathBuf) {
        self.load(path, true);
    }

    /// Load `path` in the player; `autoplay` false means the transport is there without sound starting.
    fn load(&mut self, path: PathBuf, autoplay: bool) {
        // ponytail: opening here runs on the UI thread, so a damaged clip or slow storage can stall browsing;
        // upgrade path is an open worker that discards results superseded by a newer selection.
        // A double-click selects (loading paused) and then plays that same file; don't probe it twice.
        // ponytail: keyed by path only, so replacing a file under the same name keeps the loaded player until
        // the folder is refreshed; add a content signature if that ever bites.
        if self.playing.as_deref() == Some(path.as_path()) {
            if autoplay {
                self.player.play();
            }
            return;
        }
        match open(&path) {
            Ok(source) => {
                self.playing = Some(path);
                self.frame = None;
                self.player.load(source, autoplay);
                // The player starts at its own defaults; the volume and loop the user set apply now.
                self.player.set_volume(self.settings.volume);
                self.player.set_muted(self.settings.muted);
                self.player.set_looping(self.settings.loop_playback);
                self.status.clear();
            }
            Err(err) => {
                // Don't let a previous file's transport stand in for one that failed to open, and keep the
                // selection able to retry (the file may have arrived since).
                self.player.unload();
                self.playing = None;
                self.frame = None;
                self.status = format!("{err}");
            }
        }
    }

    /// Stop and rewind, keeping the file loaded: the preview panel belongs to the selected clip, and frames
    /// are only pumped while the app owns the loaded path. Releasing here left a transport that could play
    /// with nothing pumping frames (press Play after Stop and the picture was invisible).
    pub fn stop_playback(&mut self) {
        self.player.seek(Tick::from_seconds_f64(0.0));
        self.player.pause();
    }

    /// Move finished pictures from the engine into wgpu textures. Called every frame.
    fn poll_pictures(&mut self, ui: &mut egui::Ui) {
        for result in self.thumbs.poll() {
            if let Some(scrub) = result.scrub {
                // A hover frame belongs to the pane asking for that file. These requests do not ride the
                // thumbnail batch, so they neither count nor uncount `outstanding`.
                let owner = self.panes.iter().position(|pane| {
                    pane.scrub_target
                        .as_ref()
                        .is_some_and(|(path, _, _)| path == &result.path)
                });
                match (owner, result.image) {
                    (Some(index), Ok(image)) => {
                        let color = egui::ColorImage::from_rgba_unmultiplied(
                            [image.width as usize, image.height as usize],
                            image.pixels.as_ref(),
                        );
                        let texture = ui.ctx().load_texture(
                            format!("scrub{}of{}", scrub.slot, scrub.slots),
                            color,
                            egui::TextureOptions::LINEAR,
                        );
                        let key = (result.path.clone(), scrub.slot, scrub.slots);
                        self.panes[index]
                            .scrub_frames
                            .insert(key.clone(), texture.clone());
                        // Only the slot this pane is pointing at now may take the cell. A late answer for a
                        // slot the pointer left behind - or for another slot count, from Ctrl - waits in the
                        // cache until something asks for it.
                        if self.panes[index].scrub_target.as_ref() == Some(&key) {
                            self.panes[index].scrub_shown = Some((result.path.clone(), texture));
                        }
                    }
                    (Some(_), Err(err)) => self.status = format!("hover frame: {err}"),
                    _ => {}
                }
                continue;
            }
            let preview = result.max_edge >= PREVIEW_EDGE;
            let owner = self
                .panes
                .iter()
                .position(|pane| pane.wanted.as_deref() == Some(result.path.as_path()));
            // Every result we asked for is consumed here, used or not: an uncounted drop would keep the
            // counter above zero and repaint every frame forever.
            self.outstanding = self.outstanding.saturating_sub(1);
            self.stall = std::time::Instant::now();
            if preview && owner.is_none() {
                continue; // the pane moved on to another file
            }
            let Ok(image) = result.image else {
                if let Some(index) = owner {
                    self.panes[index].wanted = None; // decode failed: stop waiting for this preview
                }
                continue;
            };
            let color = egui::ColorImage::from_rgba_unmultiplied(
                [image.width as usize, image.height as usize],
                image.pixels.as_ref(),
            );
            let texture = ui.ctx().load_texture(
                format!("pic{}", result.max_edge),
                color,
                egui::TextureOptions::LINEAR,
            );
            match (preview, owner) {
                (true, Some(index)) => {
                    self.panes[index].preview = Some((result.path.clone(), texture));
                }
                (false, _) => {
                    self.pictures.insert(result.path.clone(), texture);
                }
                (true, None) => {}
            }
        }
        // ponytail: repaint continuously while pictures are in flight; event-driven wakeups when the
        // engine can notify us instead of being polled.
        let pending = self.panes.iter().any(|pane| {
            pane.wanted.is_some()
                && pane
                    .preview
                    .as_ref()
                    .is_none_or(|(path, _)| Some(path.as_path()) != pane.wanted.as_deref())
        });
        // Hover frames schedule their own repaints (`scrub_at` asks every 30 ms while one is wanted), so
        // this guard is only about the thumbnail and preview batch.
        if self.outstanding > 0 || pending {
            if self.stall.elapsed() > std::time::Duration::from_secs(3) {
                // The engine went silent about something we asked for: stop waiting instead of repainting
                // every frame forever.
                self.outstanding = 0;
                for pane in &mut self.panes {
                    if pane
                        .preview
                        .as_ref()
                        .is_none_or(|(path, _)| Some(path.as_path()) != pane.wanted.as_deref())
                    {
                        pane.wanted = None;
                    }
                }
            } else {
                ui.ctx().request_repaint();
            }
        }
    }

    /// Advance the player and upload its frame. Called every frame while something is loaded.
    fn pump_player(&mut self, ui: &mut egui::Ui) {
        if self.playing.is_none() {
            return;
        }
        if let Some(frame) = self.player.tick(std::time::Instant::now()) {
            match frame_to_rgba(&frame) {
                Ok(rgba) => {
                    let color = egui::ColorImage::from_rgba_unmultiplied(
                        [rgba.width as usize, rgba.height as usize],
                        rgba.pixels.as_ref(),
                    );
                    // ponytail: a fresh texture per frame; reuse one with `TextureHandle::set` once the
                    // upload cost shows up in frame times.
                    self.frame = Some(ui.ctx().load_texture(
                        "frame",
                        color,
                        egui::TextureOptions::LINEAR,
                    ));
                }
                Err(err) => self.status = format!("{err}"),
            }
        }
        if self.player.needs_repaint() {
            ui.ctx().request_repaint();
        }
    }

    /// Push the theme to egui; `System` is treated as dark until we can ask the OS.
    fn apply_theme(&mut self, ctx: &egui::Context) {
        if self.theme_applied.as_ref() == Some(&self.settings.theme) {
            return;
        }
        let dark = matches!(self.settings.theme, Theme::Dark | Theme::System);
        ctx.set_visuals(if dark {
            egui::Visuals::dark()
        } else {
            egui::Visuals::light()
        });
        self.theme_applied = Some(self.settings.theme);
    }

    /// Write settings where they came from; report failure in the status bar.
    fn save_settings(&mut self) {
        if let Some(dirs) = &self.dirs
            && let Err(err) = self.settings.save(dirs)
        {
            self.status = format!("{err}");
        }
    }

    /// Command-line or script override; not persisted.
    pub fn set_theme(&mut self, theme: Theme) {
        self.settings.theme = theme;
    }

    /// Switch between the engine's grid and list view.
    pub fn set_view_mode(&mut self, mode: ViewMode) {
        self.settings.view_mode = mode;
    }

    /// Turn the second pane on or off; both panes are always kept in sync with the folder on disk.
    pub fn set_dual_pane(&mut self, dual: bool) {
        let was = self.settings.dual_pane;
        self.settings.dual_pane = dual;
        if dual && !was {
            self.open(1);
        } else if !dual && was {
            // The hidden pane must not stay active, or the shortcuts would act on an invisible selection.
            self.active = 0;
            self.refresh_thumbs();
        }
    }

    /// Write down where the panes are, so the next window opens in the same folders. Qt restored its tabs and
    /// panes for the same reason - a file manager that forgets where you were makes you find it again every
    /// launch. This runs when a pane's folder changes rather than on the way out: eframe's `save` hook turned out
    /// not to fire when this window was closed on Wayland (checked by observation, not assumption), and an exit
    /// hook is also a hook that a killed process skips. Navigating is a deliberate act, so one fsync per move is
    /// the price - not one per frame.
    fn remember_current_folders(&mut self) {
        if !self.folders_remembered {
            return;
        }
        self.settings.last_folder = Some(self.panes[0].path.clone());
        // The second pane is only worth remembering when it was open; a single-pane close should not leave a
        // folder behind that the next launch would have to guess a layout for.
        self.settings.last_folder_secondary = if self.settings.dual_pane {
            Some(self.panes[1].path.clone())
        } else {
            None
        };
        // Writing down means writing to disk: a remembered folder that only lives in memory is gone with the
        // process, and this one is killed as often as it is closed.
        self.save_settings();
    }

    /// Start writing down where the panes are without restoring anything: used when the command line named the
    /// folder to open, which is the one case where the remembered folder must not win.
    pub fn start_folder_memory(&mut self) {
        self.folders_remembered = true;
        // Record where the window is *now*, not only from the next move on: a session launched at an explicit
        // folder and closed without navigating would otherwise leave the previous session's folders remembered,
        // and the disk mtime poll has no reason to correct it.
        self.remember_current_folders();
    }

    /// Reopen the folders the panes were left in, so a window opens where you left off. Both are read out of the
    /// settings *before* anything navigates: navigating is what writes the settings back, so restoring one pane
    /// would otherwise overwrite the other's remembered folder with where it happened to start.
    ///
    /// Returns whether the first pane moved. A folder that has been deleted or moved since is skipped rather than
    /// restored into a blank pane, and a session left in one pane does not resurrect the pane the user closed.
    /// Restoring is arriving, not navigating: nothing is pushed onto the Back stacks, and which pane is active
    /// stays as it was.
    pub fn restore_folders(&mut self) -> bool {
        let remembered = (
            self.settings.last_folder.clone(),
            if self.settings.dual_pane {
                self.settings.last_folder_secondary.clone()
            } else {
                None
            },
        );
        // From here on, where the panes are is worth writing down - including what this navigation records.
        self.folders_remembered = true;
        let (Some(primary), secondary) = (remembered.0, remembered.1) else {
            return false;
        };
        let mut restored = false;
        if primary.is_dir() && primary != self.panes[0].path {
            // Not through `navigate`: that would push the folder the window had guessed (Pictures, usually) onto
            // the Back stack, and the toolbar's Back would then visit a folder the user never went to. Restoring
            // is arriving, not navigating.
            self.panes[0].path = primary;
            self.open(0);
            restored = true;
        }
        if let Some(secondary) = secondary
            && secondary.is_dir()
        {
            self.panes[1].path = secondary;
            // The layout gate above already established two panes were left open, so this pane is on screen.
            self.open(1);
        }
        restored
    }

    /// Put the second pane at `dir` (Qt restores `Settings::last_folder_secondary`).
    pub fn set_secondary(&mut self, dir: PathBuf) {
        self.panes[1].path = dir;
        if self.settings.dual_pane {
            self.open(1);
        }
    }

    // --- File operations: the engine runs them on its own thread and remembers them for undo. ------

    /// The active pane's primary file - what rename acts on (Qt renames one file at a time).
    fn target(&self) -> Option<PathBuf> {
        self.panes[self.active].primary().map(Path::to_path_buf)
    }

    /// Every file selected in the active pane - what copy, cut, trash and delete act on.
    fn targets(&self) -> Vec<PathBuf> {
        self.panes[self.active].selection.clone()
    }

    /// Ctrl+C / Ctrl+X: remember the selection as a copy or a cut.
    /// A no-op notice for the status bar. It yields to an operation error raised in the same frame, because
    /// `poll_ops` runs before the shortcut dispatch and a missing file matters more than our own nothing-to-do.
    fn note(&mut self, message: &str) {
        if !self.op_error_shown {
            self.status = message.to_owned();
        }
    }

    pub fn copy_selection(&mut self, cut: bool) {
        let paths = self.targets();
        if paths.is_empty() {
            // Otherwise Ctrl+C on an empty selection is indistinguishable from a dead shortcut.
            self.note("Nothing selected");
            return;
        }
        let what = if paths.len() == 1 {
            paths[0]
                .file_name()
                .map(|n| n.to_string_lossy().into_owned())
                .unwrap_or_default()
        } else {
            format!("{} files", paths.len())
        };
        self.clipboard = Some((paths, cut));
        // Publish the copied files where a keystroke can find them: see `clipboard_publish`.
        self.clipboard_publish = self.clipboard.as_ref().map(|(paths, _)| {
            paths
                .iter()
                .map(|path| file_uri(path))
                .collect::<Vec<_>>()
                .join("\n")
        });
        // A fresh copy or cut supersedes whatever was waiting to move.
        self.cut_pending = None;
        self.status = format!("{}{what}", if cut { "cut: " } else { "copied: " });
    }

    /// Ctrl+V: copy or move the clipboard path into the active pane's folder. Collisions get a numbered
    /// name (`KeepBoth`), never an overwrite.
    pub fn paste(&mut self) {
        let Some((sources, cut)) = self.clipboard.clone() else {
            // Ctrl+V with nothing copied here must say what is missing: the OS clipboard may well hold text
            // (which is how the keystroke reached us) while this app has no files staged.
            self.note("Nothing copied in this app");
            return;
        };
        let dest_dir = self.panes[self.active].path.clone();
        if cut
            && !sources.is_empty()
            && sources
                .iter()
                .all(|src| src.parent().map(Path::to_path_buf) == Some(dest_dir.clone()))
        {
            // Cutting and pasting back into the folder it came from is nothing to do. The engine reports
            // every source as "already in the destination", which read as a paste that had failed; single
            // pane users hit this on the first try. The clipboard stays, so pasting elsewhere still works.
            self.note("Already in this folder");
            return;
        }
        // Only a cut has sources to spend; they are consumed one by one as each one actually moves.
        self.cut_pending = cut.then(|| sources.clone());
        self.ops.submit(if cut {
            FileOp::Move {
                sources,
                dest_dir,
                conflict: ConflictPolicy::KeepBoth,
            }
        } else {
            FileOp::Copy {
                sources,
                dest_dir,
                conflict: ConflictPolicy::KeepBoth,
            }
        });
    }

    /// What dragging from `path` in pane `index` would carry: the whole selection when that file is part of it,
    /// otherwise just that file - so grabbing one of five selected files carries all five, as Qt does.
    /// Publish what this row or cell is carrying - but only once the pointer has actually moved. egui decides
    /// that a press held past `max_click_duration` (0.8 s) is a drag even with no motion at all, so without this
    /// gate a long click on a file would release as a drop and quietly copy it onto itself as `name (1).ext`.
    /// Only the widget egui is dragging answers, so this costs one payload per frame however many cells are drawn.
    fn publish_drag(&self, ui: &egui::Ui, resp: &egui::Response, index: usize, path: &Path) {
        if !resp.dragged() {
            return;
        }
        let moved = match (resp.interact_pointer_pos(), ui.ctx().pointer_interact_pos()) {
            (Some(start), Some(now)) => carried_by_drag(start, now),
            _ => false,
        };
        if moved {
            egui::DragAndDrop::set_payload(ui.ctx(), self.dragged_from(index, path));
        }
    }

    /// What dragging from `path` in pane `index` would carry: the whole selection when that file is part of it,
    /// otherwise just that file - so grabbing one of five selected files carries all five, as Qt does.
    fn dragged_from(&self, index: usize, path: &Path) -> Dragged {
        let pane = &self.panes[index];
        let paths = if pane.is_selected(path) {
            pane.selection.clone()
        } else {
            vec![path.to_path_buf()]
        };
        Dragged {
            paths,
            from: pane.path.clone(),
        }
    }

    /// Files dropped from inside the app onto a folder: copied, or moved while Shift is held - Qt's rule for
    /// drops between its panes. Dropping into the folder they are already in still copies (the engine gives
    /// `name (1).ext`), which is what a copy aimed at its own folder means; that is not the no-op paste warns
    /// about, because here you asked for a duplicate rather than for nothing to happen.
    fn drop_files(&mut self, carried: Dragged, dest_dir: PathBuf, shift: bool) {
        if carried.paths.is_empty() {
            return;
        }
        if shift && dest_dir == carried.from {
            // Moving files into the folder they are standing in is nothing to do - the engine would report every
            // source as already there. Copying into that folder still makes `name (1).ext` duplicates below.
            self.note("Already in this folder");
            return;
        }
        let paths = carried.paths;
        // Carrying `/a` into `/a/b` is how you get a copy that contains itself.
        for path in &paths {
            if dest_dir == *path || dest_dir.starts_with(path.as_path()) {
                self.note("Cannot drop a folder into itself");
                return;
            }
        }
        self.ops.submit(if shift {
            FileOp::Move {
                sources: paths,
                dest_dir,
                conflict: ConflictPolicy::KeepBoth,
            }
        } else {
            FileOp::Copy {
                sources: paths,
                dest_dir,
                conflict: ConflictPolicy::KeepBoth,
            }
        });
    }

    /// Highlight the listing while a compatible drag is over it, and act on what was released there. Both
    /// listings answer a drop through here so they behave alike.
    fn accept_drop(&mut self, ui: &mut egui::Ui, rect: egui::Rect, index: usize) {
        let (hovering, dropped) = Self::drop_check(ui, rect);
        if hovering {
            // A drop target that does not say it is one is a target you have to guess at.
            let tint = ui.visuals().selection.bg_fill.linear_multiply(0.35);
            ui.painter().rect_filled(rect.shrink(2.0), 4.0, tint);
        }
        if let Some(carried) = dropped {
            let shift = ui.input(|i| i.modifiers.shift);
            self.drop_files(carried, self.panes[index].path.clone(), shift);
        }
    }

    /// The listing area as an in-app drop target: whether something droppable is over it right now, and what was
    /// released on it this frame. The payload is read against the rect rather than through a widget laid over
    /// it: egui hands hover focus to whatever sits on top, so a drop zone drawn over a listing would leave every
    /// cell under it unhovered - and pointer position is what drives selection, hover scrubbing and previews here.
    fn drop_check(ui: &egui::Ui, rect: egui::Rect) -> (bool, Option<Dragged>) {
        if !egui::DragAndDrop::has_payload_of_type::<Dragged>(ui.ctx()) {
            return (false, None);
        }
        let over = ui
            .input(|i| i.pointer.hover_pos())
            .is_some_and(|pos| rect.contains(pos));
        if !over {
            return (false, None);
        }
        let released = ui.input(|i| i.pointer.any_released());
        let dropped = if released {
            egui::DragAndDrop::take_payload::<Dragged>(ui.ctx()).map(|carried| (*carried).clone())
        } else {
            None
        };
        (true, dropped)
    }

    /// Delete key: send the selection to the OS trash (undoable).
    pub fn trash_selection(&mut self) {
        let paths = self.targets();
        if !paths.is_empty() {
            self.ops.submit(FileOp::Trash { paths });
        }
    }

    /// F2: open the rename box for the selection. It starts empty — the current name is shown above the
    /// field, so typing gives the new name without editing the old one.
    pub fn start_rename(&mut self) {
        if self.target().is_some() {
            self.rename_buffer = String::new();
            self.renaming = true;
            self.rename_focus = true;
        }
    }

    /// Apply the rename: same folder, new name. The engine refuses to overwrite an existing target.
    pub fn apply_rename(&mut self) {
        let Some(from) = self.target() else {
            self.renaming = false;
            return;
        };
        let to = self.panes[self.active].path.join(self.rename_buffer.trim());
        self.renaming = false;
        if to == from || self.rename_buffer.trim().is_empty() {
            return;
        }
        self.ops.submit(FileOp::Rename { from, to });
    }

    /// Move finished file operations into view: rescan the panes and report what went wrong. Called every
    /// frame.
    fn poll_ops(&mut self) {
        self.op_error_shown = false;
        let mut finished: Vec<OpOutcome> = Vec::new();
        for event in self.ops.poll() {
            match event {
                OpEvent::Progress {
                    done,
                    total,
                    current,
                    ..
                } => {
                    let name = current
                        .as_ref()
                        .and_then(|p| p.file_name())
                        .map(|n| n.to_string_lossy().into_owned())
                        .unwrap_or_default();
                    self.op_progress = format!("{done}/{total} {name}");
                }
                OpEvent::Finished(outcome) => finished.push(outcome),
            }
        }
        if finished.is_empty() {
            return;
        }
        self.op_progress.clear();
        let mut errors: Vec<String> = Vec::new();
        let mut touched: Vec<PathBuf> = Vec::new();
        for outcome in &finished {
            if outcome
                .moved
                .iter()
                .any(|(from, _)| Some(from.as_path()) == self.playing.as_deref())
                || outcome
                    .removed
                    .iter()
                    .any(|p| Some(p) == self.playing.as_ref())
            {
                self.stop_playback();
            }
            for (from, to) in &outcome.moved {
                if let Some(dir) = from.parent() {
                    touched.push(dir.to_path_buf());
                }
                if let Some(dir) = to.parent() {
                    touched.push(dir.to_path_buf());
                }
            }
            for path in outcome.created.iter().chain(outcome.removed.iter()) {
                if let Some(dir) = path.parent() {
                    touched.push(dir.to_path_buf());
                }
            }
            errors.extend(outcome.errors.iter().map(|e| e.to_string()));
            // The catalog is keyed by path, so each operation's moves and deletions are replayed in arrival
            // order: `relink` maps simultaneously, so two renames merged into one batch would chain wrong.
            if !outcome.moved.is_empty()
                && let Err(err) = self.library.relink(&outcome.moved)
            {
                errors.push(format!("catalog relink: {err}"));
            }
            if !outcome.removed.is_empty()
                && let Err(err) = self.library.forget(&outcome.removed)
            {
                errors.push(format!("catalog forget: {err}"));
            }
        }
        // The annotation editor names a path. If an operation moved or deleted that file while the box was
        // open, applying the edit now would write annotations onto a path nothing owns any more.
        let stale = self.annotate.as_ref().is_some_and(|edit| {
            finished.iter().any(|outcome| {
                outcome.removed.contains(&edit.path)
                    || outcome.moved.iter().any(|(from, _)| from == &edit.path)
            })
        });
        if stale {
            self.annotate = None;
            errors.push("annotation cancelled: file changed".into());
        }
        // A cut is spent as each of its own sources actually moves; a copy stays available for another
        // paste. Matching on the source path is what keeps an unrelated clean move - a rename, an undo -
        // from spending a clipboard it never touched, and leaves a source that failed to move behind.
        if let Some(pending) = self.cut_pending.clone() {
            let moved: Vec<PathBuf> = finished
                .iter()
                .flat_map(|outcome| outcome.moved.iter().map(|(from, _)| from.clone()))
                .collect();
            let left: Vec<PathBuf> = pending
                .into_iter()
                .filter(|path| !moved.contains(path))
                .collect();
            if left.is_empty() {
                self.clipboard = None;
                self.cut_pending = None;
            } else {
                self.cut_pending = Some(left);
            }
        }
        // A cached tree listing is stale as soon as an operation changes that folder. This is the only
        // place that knows an operation landed: the toolbar's New Folder, a paste, and an undo never pass
        // through the tree menu, and invalidating when the menu was clicked would re-cache the old listing
        // before the worker had created anything.
        touched.sort();
        touched.dedup();
        for dir in &touched {
            self.tree.kids.remove(dir);
        }
        // Only folders the op touched are re-read, and all of them share one thumbnail batch. Selections
        // survive because `open_quiet` keeps a selection whose file is still there.
        let mut rescanned = false;
        // A pane sitting inside a folder that an operation renamed is looking at a path that no longer
        // exists: the operation touched its *parent*, so the pane is not in `touched` and would keep the old
        // listing, the old title and a dead path. Its path moves with the folder - including for a pane
        // deeper inside one that moved.
        let moves: Vec<(PathBuf, PathBuf)> = finished
            .iter()
            .flat_map(|outcome| outcome.moved.iter().cloned())
            .collect();
        for index in 0..2 {
            let path = self.panes[index].path.clone();
            for (from, to) in &moves {
                if path == *from {
                    self.panes[index].path = to.clone();
                } else if let Ok(rest) = path.strip_prefix(from) {
                    self.panes[index].path = to.join(rest);
                } else {
                    continue;
                }
                // Anything cached under the old name is stale from here down.
                self.tree.kids.retain(|dir, _| !dir.starts_with(from));
                self.open_quiet(index);
                rescanned = true;
                break;
            }
        }
        for index in 0..2 {
            if touched.contains(&self.panes[index].path) {
                self.open_quiet(index);
                rescanned = true;
            }
        }
        if rescanned {
            self.refresh_thumbs();
        }
        if !errors.is_empty() {
            self.op_error_shown = true;
            self.status = errors.join("; ");
        }
    }

    /// The rename box for the active pane's selection: Enter or the button applies it.
    pub fn rename_window(&mut self, ui: &mut egui::Ui) {
        let mut apply = false;
        let current = self
            .target()
            .and_then(|p| p.file_name().map(|n| n.to_string_lossy().into_owned()))
            .unwrap_or_default();
        egui::Window::new("Rename")
            .collapsible(false)
            .resizable(false)
            .show(ui, |ui| {
                ui.label(current);
                let response = ui
                    .add(egui::TextEdit::singleline(&mut self.rename_buffer).desired_width(240.0));
                if self.rename_focus {
                    response.request_focus();
                    self.rename_focus = false;
                }
                if response.has_focus() {
                    self.rename_field_id = Some(response.id);
                }
                self.typing_in_field |= response.has_focus();
                ui.horizontal(|ui| {
                    if ui.button("Rename").clicked() {
                        apply = true;
                    }
                    if ui.button("Cancel").clicked() {
                        self.renaming = false;
                    }
                });
                if ui.input(|i| i.key_pressed(egui::Key::Enter)) {
                    apply = true;
                }
                if response.lost_focus() {
                    // Focus went elsewhere (a click): don't leave a box the user cannot type into on screen.
                    self.renaming = false;
                }
            });
        if apply {
            self.apply_rename();
        }
    }

    /// GalleryBrowser's hover scrub (crates/ui-egui/src/file_pane.rs:933): the pointer's x across the
    /// thumbnail square picks a slot out of 24, or 200 with Ctrl held. The exact frame is used if we have it;
    /// otherwise the engine is asked and a decoded neighbour shows while it is made. Hovering never touches the
    /// preview panel - that is for the selection - and playback keeps running independently of it.
    fn scrub_at(
        &mut self,
        ui: &mut egui::Ui,
        index: usize,
        entry: &Entry,
        edge: u32,
        pos: egui::Pos2,
        thumb: egui::Rect,
        fine: bool,
    ) {
        if !thumb.contains(pos) || !matches!(entry.kind, MediaKind::Video | MediaKind::Sequence) {
            return;
        }
        let slots = if fine { SCRUB_SLOTS_FINE } else { SCRUB_SLOTS };
        let frac = ((pos.x - thumb.min.x) / thumb.width().max(1.0)).clamp(0.0, 1.0);
        let slot = ((frac * slots as f32) as u32).min(slots - 1);
        let pane = &mut self.panes[index];
        // The clock is per target: landing on another clip, slot or division starts a fresh wait, so scrubbing
        // stays alive while the pointer keeps asking for something new. A settle delay invented here starved
        // scrub whenever the pointer kept moving - the engine already coalesces requests (a lane's queued frame
        // is replaced by the newest), and that is what bounds a sweep across a grid.
        let target = (entry.path.clone(), slot, slots);
        if pane.scrub_target.as_ref() != Some(&target) {
            pane.scrub_target = Some(target.clone());
            pane.scrub_wait_until = std::time::Instant::now();
        }

        // A frame already in hand is shown now and asked to be drawn next pass: egui is input-driven, so a
        // frame chosen during a pass must schedule its own redraw or it surfaces late.
        let key = (entry.path.clone(), slot, slots);
        if let Some(tex) = pane.scrub_frames.get(&key).cloned() {
            if Self::show_scrub(pane, entry.path.clone(), tex) {
                ui.ctx().request_repaint();
            }
            return;
        }
        if let Some(image) = self.thumbs.cached_scrub(entry, edge, slot, slots) {
            let texture = Self::upload_scrub(ui, image.as_ref(), slot, slots);
            pane.scrub_frames.insert(key.clone(), texture.clone());
            if Self::show_scrub(pane, entry.path.clone(), texture) {
                ui.ctx().request_repaint();
            }
            return;
        }

        // Nothing decoded for this slot yet: ask the engine, then show the nearest frame we do have
        // (GalleryBrowser searches SCRUB_NEAR of them). Stop asking a frame after SCRUB_WAIT - it is undecodable.
        if pane.scrub_wait_until.elapsed() >= SCRUB_WAIT {
            if pane.scrub_shown.take().is_some() {
                // The thumbnail has to come back, and egui only redraws when asked.
                ui.ctx().request_repaint();
            }
            return;
        }
        self.thumbs.request_scrub(entry, edge, slot, slots);
        let mut replaced = false;
        for near in 1..=SCRUB_NEAR {
            let below = slot.checked_sub(near);
            let above = slot.checked_add(near).filter(|s| *s < slots);
            for s in below.into_iter().chain(above) {
                // Reuse an uploaded neighbour before converting another (otherwise ~30 uploads a second).
                let found = pane
                    .scrub_frames
                    .get(&(entry.path.clone(), s, slots))
                    .cloned()
                    .or_else(|| {
                        self.thumbs
                            .cached_scrub(entry, edge, s, slots)
                            .map(|image| Self::upload_scrub(ui, image.as_ref(), s, slots))
                    });
                if let Some(tex) = found {
                    pane.scrub_frames
                        .insert((entry.path.clone(), s, slots), tex.clone());
                    pane.scrub_shown = Some((entry.path.clone(), tex));
                    replaced = true;
                    break;
                }
            }
            if replaced {
                break;
            }
        }
        if !replaced {
            pane.scrub_shown = None; // nothing close enough: show the thumbnail, not a far-away frame
        }
        // Keep passes coming while a frame is wanted; answers arrive from the engine's own threads.
        ui.ctx()
            .request_repaint_after(std::time::Duration::from_millis(30));
    }

    /// Point the cell at a scrub frame, reporting whether that is a change worth redrawing for. egui only
    /// redraws when asked; asking on every cache hit renders continuously while the pointer rests on a
    /// decoded frame.
    fn show_scrub(pane: &mut Pane, path: PathBuf, tex: egui::TextureHandle) -> bool {
        let changed = pane
            .scrub_shown
            .as_ref()
            .is_none_or(|(p, t)| p != &path || t.id() != tex.id());
        pane.scrub_shown = Some((path, tex));
        changed
    }

    /// Turn a decoded scrub frame into a wgpu texture for this pass.
    fn upload_scrub(
        ui: &mut egui::Ui,
        image: &gallerybrowser_media::RgbaImage,
        slot: u32,
        slots: u32,
    ) -> egui::TextureHandle {
        let color = egui::ColorImage::from_rgba_unmultiplied(
            [image.width as usize, image.height as usize],
            image.pixels.as_ref(),
        );
        ui.ctx().load_texture(
            format!("scrub{slot}of{slots}"),
            color,
            egui::TextureOptions::LINEAR,
        )
    }

    /// Star the selected file 0-5. The catalog is keyed by path, so a rename follows the rating.
    fn set_rating(&mut self, rating: u8) {
        let paths = self.targets();
        if paths.is_empty() {
            self.status = "select a file to rate".into();
            return;
        }
        match self.library.set_rating(&paths, rating) {
            Ok(()) => {
                let what = if paths.len() == 1 {
                    paths[0]
                        .file_name()
                        .map(|n| n.to_string_lossy().into_owned())
                        .unwrap_or_default()
                } else {
                    format!("{} files", paths.len())
                };
                self.status = format!("{what} rated {rating}/5");
            }
            Err(err) => self.status = format!("rating: {err}"),
        }
    }

    /// Rating and tag/collection chips for the active pane's selection; clicking a chip removes it.
    fn annotations_ui(&mut self, ui: &mut egui::Ui) {
        let Some(path) = self.target() else {
            return;
        };
        if !self.panes[self.active]
            .entries
            .iter()
            .any(|e| e.path == path)
        {
            return;
        }
        let notes = self.library.annotations(&path);
        ui.separator();
        let mut rate = None;
        ui.horizontal(|ui| {
            ui.label("Rating");
            for star in 1..=5u8 {
                if ui.selectable_label(notes.rating >= star, "★").clicked() {
                    rate = Some(if notes.rating == star { 0 } else { star });
                }
            }
        });
        if let Some(rating) = rate {
            self.set_rating(rating);
        }
        for (kind, names) in [
            (AnnotateKind::Tag, notes.tags),
            (AnnotateKind::Collection, notes.collections),
        ] {
            if names.is_empty() {
                continue;
            }
            ui.horizontal(|ui| {
                ui.label(match kind {
                    AnnotateKind::Tag => "Tags",
                    AnnotateKind::Collection => "In",
                });
                for name in &names {
                    if ui.button(format!("{name} ×")).clicked() {
                        let paths = [path.clone()];
                        let result = match kind {
                            AnnotateKind::Tag => self.library.remove_tag(&paths, name),
                            AnnotateKind::Collection => {
                                self.library.remove_from_collection(&paths, name)
                            }
                        };
                        if let Err(err) = result {
                            self.status = format!("{name}: {err}");
                        }
                    }
                }
            });
        }
        ui.horizontal(|ui| {
            if ui.button("Add tag").clicked() {
                self.open_annotate(AnnotateKind::Tag, path.clone());
            }
            if ui.button("Add collection").clicked() {
                self.open_annotate(AnnotateKind::Collection, path.clone());
            }
        });
    }

    fn open_annotate(&mut self, kind: AnnotateKind, path: PathBuf) {
        self.annotate = Some(Annotate {
            kind,
            buffer: String::new(),
            path,
        });
        self.annotate_focus = true;
    }

    fn apply_annotate(&mut self, kind: AnnotateKind, name: &str, path: &Path) {
        let paths = [path.to_path_buf()];
        let result = match kind {
            AnnotateKind::Tag => self.library.add_tag(&paths, name),
            AnnotateKind::Collection => self.library.add_to_collection(&paths, name),
        };
        if let Err(err) = result {
            self.status = format!("{name}: {err}");
        }
    }

    /// Text entry that adds a tag or collection to the file chosen when the box opened.
    fn annotate_window(&mut self, ui: &mut egui::Ui) {
        let Some(mut edit) = self.annotate.take() else {
            return;
        };
        let title = match edit.kind {
            AnnotateKind::Tag => "Add tag",
            AnnotateKind::Collection => "Add collection",
        };
        let mut apply = false;
        let mut cancel = false;
        egui::Window::new(title)
            .collapsible(false)
            .resizable(false)
            .show(ui, |ui| {
                let response = ui.add(
                    egui::TextEdit::singleline(&mut edit.buffer)
                        .hint_text("name")
                        .desired_width(200.0),
                );
                if self.annotate_focus {
                    response.request_focus();
                    self.annotate_focus = false;
                }
                if response.has_focus() {
                    self.annotate_field_id = Some(response.id);
                }
                self.typing_in_field |= response.has_focus();
                ui.horizontal(|ui| {
                    if ui.button("Add").clicked() {
                        apply = true;
                    }
                    if ui.button("Cancel").clicked() {
                        cancel = true;
                    }
                });
                if ui.input(|i| i.key_pressed(egui::Key::Enter)) {
                    apply = true;
                }
            });
        if apply {
            let name = edit.buffer.trim().to_string();
            if !name.is_empty() {
                self.apply_annotate(edit.kind, &name, &edit.path);
            }
            self.annotate = None;
        } else if cancel {
            self.annotate = None;
        } else {
            self.annotate = Some(edit);
        }
    }

    /// Theme name for the settings controls.
    fn theme_label(theme: Theme) -> &'static str {
        match theme {
            Theme::Dark => "Dark",
            Theme::Light => "Light",
            Theme::System => "System",
        }
    }

    /// One pane column: breadcrumb, then its subfolder list beside its entries.
    /// One pane column: breadcrumb, then its subfolder list beside its entries.
    ///
    /// Columns are laid out by allocation, not nested panels: a `CentralPanel` inside a docked panel
    /// claims the whole parent rect and starved the first pane to ~55px on a half-screen HiDPI tile.
    fn pane(&mut self, index: usize, ui: &mut egui::Ui) {
        // Each pane is its own id scope, or the two panes' scroll areas and rows share ids and egui
        // complains about duplicate widget use (and clicks cross panes).
        ui.push_id(index, |ui| {
            ui.vertical(|ui| {
                self.pane_toolbar(index, ui);
                self.pane_path_bar(index, ui);
                // A pane shows files only. The folder tree belongs to the left browser column, once, and
                // drives whichever pane is active; a per-pane subfolder list read as a second tree inside
                // every pane. The path bar above says which folder this pane is working on.
                match self.settings.view_mode {
                    ViewMode::Grid => self.pane_grid(index, ui),
                    ViewMode::List => self.pane_list(index, ui),
                }
            });
        });
    }

    /// One pane's own toolbar. Qt keeps the day-to-day controls on the toolbar and leaves Settings to
    /// preferences, and its single File Manager toolbar acts on whichever pane is active — so a click
    /// here makes this pane active first, then the button acts on it.
    fn pane_toolbar(&mut self, index: usize, ui: &mut egui::Ui) {
        let has_selection = !self.panes[index].selection.is_empty();
        let can_back = !self.panes[index].back_stack.is_empty();
        let grid = self.settings.view_mode == ViewMode::Grid;
        let dual = self.settings.dual_pane;
        let preview = self.settings.show_preview;
        let size = self.settings.thumbnail_size;
        let paste_ready = self.clipboard.is_some();
        // Read the pointer press through the context instead of laying an interaction rect over the row:
        // that would sit on top of the buttons and swallow their clicks.
        let press = ui.input(|i| (i.pointer.any_pressed(), i.pointer.interact_pos()));
        let mut back = false;
        let mut up = false;
        let mut new_folder = false;
        let mut copy = false;
        let mut cut = false;
        let mut paste = false;
        let mut trash = false;
        let mut rename = false;
        let mut toggle_view = false;
        let mut smaller = false;
        let mut bigger = false;
        let mut refresh = false;
        let mut toggle_dual = false;
        let mut toggle_preview = false;
        // Labels stay short and the row wraps: in a tiled window the pane is ~450 px wide, so a Qt-style
        // text toolbar would push its right-hand controls off screen. Tooltips carry the full names.
        let row = ui.horizontal_wrapped(|ui| {
            back = ui
                .add_enabled(can_back, egui::Button::new("\u{21a9}"))
                .on_hover_text("Back to the previous folder")
                .clicked();
            up = ui
                .add(egui::Button::new("\u{2b06}"))
                .on_hover_text("Up one folder")
                .clicked();
            ui.separator();
            new_folder = ui
                .add(egui::Button::new("New"))
                .on_hover_text("New Folder (Ctrl+Shift+N)")
                .clicked();
            copy = ui
                .add_enabled(has_selection, egui::Button::new("Copy"))
                .on_hover_text("Copy (Ctrl+C)")
                .clicked();
            cut = ui
                .add_enabled(has_selection, egui::Button::new("Cut"))
                .on_hover_text("Cut (Ctrl+X)")
                .clicked();
            paste = ui
                .add_enabled(paste_ready, egui::Button::new("Paste"))
                .on_hover_text("Paste into this pane (Ctrl+V)")
                .clicked();
            trash = ui
                .add_enabled(has_selection, egui::Button::new("Del"))
                .on_hover_text("Move to trash (Delete); Shift+Delete deletes for good")
                .clicked();
            rename = ui
                .add_enabled(has_selection, egui::Button::new("Ren"))
                .on_hover_text("Rename (F2)")
                .clicked();
            ui.separator();
            toggle_view = ui
                .add(egui::Button::new(if grid { "List" } else { "Grid" }))
                .on_hover_text("Switch between grid and list")
                .clicked();
            smaller = ui
                .add(egui::Button::new("\u{2212}"))
                .on_hover_text("Smaller thumbnails")
                .clicked();
            ui.label(format!("{size}px"));
            bigger = ui
                .add(egui::Button::new("+"))
                .on_hover_text("Larger thumbnails")
                .clicked();
            ui.separator();
            refresh = ui
                .add(egui::Button::new("\u{27f3}"))
                .on_hover_text("Refresh this pane (F5)")
                .clicked();
            toggle_dual = ui
                .add(egui::Button::new(if dual { "1" } else { "2" }))
                .on_hover_text("Show or hide the second pane")
                .clicked();
            toggle_preview = ui
                .add(egui::Button::new(if preview {
                    "\u{25a3}"
                } else {
                    "\u{25a4}"
                }))
                .on_hover_text("Show or hide the preview panel")
                .clicked();
        });
        // A click anywhere on this toolbar is the user telling us which pane they mean.
        let acted = back
            || up
            || new_folder
            || copy
            || cut
            || paste
            || trash
            || rename
            || toggle_view
            || smaller
            || bigger
            || refresh
            || toggle_dual
            || toggle_preview
            || pressed_within(row.response.rect, press.0, press.1);
        if acted {
            self.active = index;
        }
        if back {
            self.go_back(index);
        }
        if up {
            let parent = self.panes[index].path.parent().map(Path::to_path_buf);
            if let Some(parent) = parent {
                self.navigate(index, &parent);
            }
        }
        if new_folder {
            self.new_folder();
        }
        if copy {
            self.copy_selection(false);
        }
        if cut {
            self.copy_selection(true);
        }
        if paste {
            self.paste();
        }
        if trash {
            self.trash_selection();
        }
        if rename {
            self.start_rename();
        }
        if toggle_view {
            let next = if grid { ViewMode::List } else { ViewMode::Grid };
            self.set_view_mode(next);
            self.save_settings();
        }
        if smaller {
            self.step_thumbnails(-1);
        }
        if bigger {
            self.step_thumbnails(1);
        }
        if refresh {
            self.open(index);
        }
        if toggle_dual {
            let dual = self.settings.dual_pane;
            self.set_dual_pane(!dual);
            self.save_settings();
        }
        if toggle_preview {
            self.settings.show_preview = !self.settings.show_preview;
            self.save_settings();
        }
    }

    /// Breadcrumb + up button for one pane; clicking anything makes that pane active.
    fn pane_path_bar(&mut self, index: usize, ui: &mut egui::Ui) {
        let mut ancestors: Vec<PathBuf> = self.panes[index]
            .path
            .ancestors()
            .map(|a| a.to_path_buf())
            .collect();
        ancestors.reverse();
        let segments: Vec<(String, PathBuf)> = ancestors
            .into_iter()
            .map(|a| {
                let name = a
                    .file_name()
                    .map(|n| n.to_string_lossy().into_owned())
                    .unwrap_or_else(|| a.display().to_string());
                (name, a)
            })
            .collect();
        let mut target: Option<PathBuf> = None;
        // Breadcrumbs outlive narrow panes, so let them scroll instead of clipping.
        egui::ScrollArea::horizontal()
            .id_salt(format!("path{index}"))
            .show(ui, |ui| {
                ui.horizontal(|ui| {
                    if ui.button("\u{2b06} up").clicked() {
                        target = self.panes[index].path.parent().map(Path::to_path_buf);
                    }
                    ui.separator();
                    for (position, (name, path)) in segments.iter().enumerate() {
                        if ui.button(name).clicked() {
                            target = Some(path.clone());
                        }
                        if position + 1 < segments.len() {
                            ui.label("/");
                        }
                    }
                });
            });
        if let Some(target) = target {
            self.navigate(index, &target);
        }
    }

    /// List view of one pane. Click selects; double-click plays.
    /// Qt's list view carries these columns. The set is fixed because each one maps to a `SortKey` the scanner
    /// already implements; the order and the widths are the user's, and both persist in settings.
    const COLUMNS: [Column; 4] = [Column::Name, Column::Modified, Column::Size, Column::Type];

    fn pane_list(&mut self, index: usize, ui: &mut egui::Ui) {
        let rows: Vec<(Entry, bool)> = self.panes[index]
            .entries
            .iter()
            .map(|e| (e.clone(), self.panes[index].dir_paths.contains(&e.path)))
            .collect();
        // A list row is one cell wide, so Down is the next file.
        self.panes[index].grid_cols = 1;
        let columns = self.list_columns();
        let widths: Vec<f32> = columns.iter().map(|c| self.column_width(*c)).collect();
        const ROW_H: f32 = 20.0;
        let mut selected: Option<PathBuf> = None;
        let mut play: Option<PathBuf> = None;
        let mut hovered: Option<PathBuf> = None;
        // Ctrl on Linux and Windows, Command on macOS - the platform's "add to selection" key.
        let ctrl = ui.input(|i| i.modifiers.command);

        let table_width = widths.iter().sum::<f32>() + 8.0;

        // Qt keeps its column titles pinned while the rows scroll, and answers a pane narrower than the table
        // with a horizontal scrollbar. The header is drawn outside the scroll area to stay put, and slides by
        // the rows' own offset (read from last frame, which is what makes it line up) so the two never drift.
        let x_off = self.list_scroll_x[index].max(0.0);
        let head_w = table_width.max(ui.available_width());
        let (head_rect, _) =
            ui.allocate_exact_size(egui::vec2(head_w, ROW_H), egui::Sense::hover());
        let row_start = head_rect.min.x;
        let mut drop_target: Option<usize> = None;
        // Each column boundary and whether the pointer is on it; painted after the loop so nothing covers it.
        let mut boundaries: Vec<(f32, bool)> = Vec::new();
        {
            let mut x = row_start - x_off;
            for (i, col) in columns.iter().copied().enumerate() {
                let sorted = Self::sort_key_for(col) == self.settings.sort_key;
                // egui's default font has no triangles (they arrive as tofu boxes), and the marker only has to
                // say which way a column is sorted.
                let arrow = match (sorted, self.settings.sort_ascending) {
                    (true, true) => " ^",
                    (true, false) => " v",
                    _ => "",
                };
                let cell = egui::Rect::from_min_size(
                    egui::pos2(x, head_rect.min.y),
                    egui::vec2(widths[i] - 6.0, ROW_H),
                );
                // A flat label with a click-and-drag sense, not a button: Qt's headers are flat text, and this
                // is what left-aligns them inside the cell.
                let btn = ui
                    .scope_builder(
                        egui::UiBuilder::new()
                            .max_rect(cell)
                            .layout(egui::Layout::left_to_right(egui::Align::Min)),
                        |ui| {
                            ui.add(
                                egui::Label::new(format!("{}{}", Self::column_label(col), arrow))
                                    .selectable(false)
                                    .truncate()
                                    .sense(egui::Sense::click_and_drag()),
                            )
                        },
                    )
                    .inner;
                if btn.clicked() {
                    self.sort_by_column(col);
                }
                if btn.drag_started() {
                    self.column_drag = Some((index, col));
                }
                // The handle is the band straddling the column's boundary, taken from the widths and not from
                // the title's own rect - that ends beside the text, which puts a grab point in mid-column for
                // any short name. It stays outside the label's rect so it cannot steal the click that sorts.
                let boundary = x + widths[i];
                let sep = egui::Rect::from_min_max(
                    egui::pos2(boundary - 6.0, cell.top() - 3.0),
                    egui::pos2(boundary + 6.0, cell.bottom() + 3.0),
                );
                let handle =
                    ui.interact(sep, ui.id().with(("col-resize", col)), egui::Sense::drag());
                // The cursor has to say "resize" while hovering *and* while dragging; a hover-only icon reverts
                // to an arrow the moment you press, which reads as the handle letting go of you.
                let hot = handle.hovered() || handle.dragged();
                let handle = handle.on_hover_and_drag_cursor(egui::CursorIcon::ResizeHorizontal);
                let handle = handle.on_hover_text("Drag to resize this column");
                boundaries.push((boundary, hot));
                if handle.dragged() {
                    let dx = ui.input(|i| i.pointer.delta().x);
                    self.set_column_width(col, self.column_width(col) + dx);
                }
                if handle.drag_stopped() {
                    // Settings::save fsyncs; writing it every frame would stall the drag.
                    self.save_settings();
                }
                if let Some((owner, dragged)) = self.column_drag {
                    // Only the pane whose header began the drag resolves it; the other draws its own header at
                    // different coordinates and would answer the release with its own column widths.
                    if owner == index {
                        if let Some(pos) = ui.ctx().pointer_interact_pos() {
                            drop_target = Some(Self::column_drop_index(
                                (pos.x - row_start + x_off).max(0.0),
                                &widths,
                            ));
                        }
                        if ui.input(|i| i.pointer.any_released()) {
                            if let Some(target) = drop_target {
                                self.reorder_columns(dragged, target);
                            }
                            self.column_drag = None;
                        }
                    }
                }
                x += widths[i];
            }
        }
        // An edge nobody can see is not a handle. Qt's headers show a separator between sections, so every
        // boundary gets a faint line and the one under the pointer - or being dragged - lights up.
        // The same stroke egui uses for separators and panel edges, so the lines belong to the theme.
        let faint = ui.visuals().widgets.noninteractive.bg_stroke.color;
        let lit = ui.visuals().selection.stroke.color;
        for (edge, hot) in &boundaries {
            ui.painter().line_segment(
                [
                    egui::pos2(*edge, head_rect.min.y - 2.0),
                    egui::pos2(*edge, head_rect.max.y + 2.0),
                ],
                egui::Stroke::new(if *hot { 2.5 } else { 1.0 }, if *hot { lit } else { faint }),
            );
        }
        if let (Some((owner, dragged)), Some(target)) = (self.column_drag, drop_target) {
            let _ = (owner, dragged);
            // Qt shows a line for where the dragged title will land before you let go.
            let x = row_start - x_off + widths[..target.min(widths.len())].iter().sum::<f32>();
            let top = head_rect.min.y;
            ui.painter().line_segment(
                [egui::pos2(x, top - 2.0), egui::pos2(x, top + ROW_H)],
                egui::Stroke::new(2.0, ui.visuals().text_color()),
            );
            let _ = dragged;
        }

        // The listing's region is what a drop from the other pane aims at - taken here, after the breadcrumb and
        // toolbar have been drawn, because `max_rect` would still include them and releasing over a toolbar
        // button is not dropping files into the folder.
        let body = ui.available_rect_before_wrap();
        let out = egui::ScrollArea::both()
            .id_salt(format!("list{index}"))
            .auto_shrink([false, false])
            .show(ui, |ui| {
                for (entry, is_dir) in &rows {
                    let path = entry.path.clone();
                    let chosen = self.panes[index].is_selected(&path);
                    // One rect per row is the click target for the whole row - cells included, so a click
                    // anywhere on the row selects it. It spans the table's width, not just what fits.
                    let (rect, row) = ui.allocate_exact_size(
                        egui::vec2(table_width.max(ui.available_width()), ROW_H),
                        // Rows sense drags too: a row is what you grab to carry files to the other pane. The drag
                        // state belongs to the row, so releasing over another listing is what makes it an op.
                        egui::Sense::click_and_drag(),
                    );
                    self.publish_drag(ui, &row, index, &path);
                    if self.panes[index].scroll_target.as_deref() == Some(path.as_path()) {
                        // Keyboard movement has to keep its item on screen, and the row's own rect is where
                        // that becomes known.
                        row.scroll_to_me(Some(egui::Align::Center));
                        self.panes[index].scroll_target = None;
                    }
                    if chosen {
                        ui.painter()
                            .rect_filled(rect, 0.0, ui.visuals().selection.bg_fill);
                    } else if row.hovered() {
                        ui.painter()
                            .rect_filled(rect, 0.0, ui.visuals().faint_bg_color);
                    }
                    let mut x = rect.min.x;
                    for (i, col) in columns.iter().copied().enumerate() {
                        let text = self.cell_text(col, entry, *is_dir);
                        let cell = egui::Rect::from_min_size(
                            egui::pos2(x + 4.0, rect.min.y),
                            egui::vec2((widths[i] - 12.0).max(20.0), ROW_H),
                        );
                        let _ = ui
                            .scope_builder(
                                egui::UiBuilder::new()
                                    .max_rect(cell)
                                    .layout(egui::Layout::left_to_right(egui::Align::Min)),
                                |ui| ui.add(egui::Label::new(text).selectable(false).truncate()),
                            )
                            .inner;
                        x += widths[i];
                    }
                    if row.double_clicked() {
                        play = Some(path.clone());
                    }
                    if row.clicked() {
                        selected = Some(path.clone());
                    }
                    if row.hovered() {
                        hovered = Some(path.clone());
                    }
                    if row.secondary_clicked()
                        && (self.active != index || self.panes[index].primary() != Some(&path))
                    {
                        // Qt makes the right-clicked file current before showing its menu.
                        self.select_in(index, path.clone(), false);
                    }
                    row.context_menu(|ui| self.entry_menu(ui, index));
                }
            });
        self.list_scroll_x[index] = out.state.offset.x;
        self.accept_drop(ui, body, index);
        self.finish(index, selected, ctrl, play, hovered);
    }

    fn column_label(col: Column) -> &'static str {
        match col {
            Column::Name => "Name",
            Column::Modified => "Date modified",
            Column::Size => "Size",
            Column::Type => "Type",
        }
    }

    /// Every column has a sort key; the Type column sorts by kind, which is what groups it into blocks.
    fn sort_key_for(col: Column) -> SortKey {
        match col {
            Column::Name => SortKey::Name,
            Column::Modified => SortKey::Modified,
            Column::Size => SortKey::Size,
            Column::Type => SortKey::Kind,
        }
    }

    /// Clicking a header sorts by that column; clicking the same one again flips the direction, as Qt's headers
    /// do. Directories stay ahead of files - that grouping is Qt's and a sort must not dissolve it.
    fn sort_by_column(&mut self, col: Column) {
        let key = Self::sort_key_for(col);
        if self.settings.sort_key == key {
            self.settings.sort_ascending = !self.settings.sort_ascending;
        } else {
            self.settings.sort_key = key;
            self.settings.sort_ascending = true;
        }
        self.resort_panes();
        self.save_settings();
    }

    /// Re-order the files each visible pane shows, without re-reading the folder.
    fn resort_panes(&mut self) {
        for index in 0..self.panes_visible() {
            let pane = &mut self.panes[index];
            let mut folders: Vec<Entry> = Vec::new();
            let mut files: Vec<Entry> = Vec::new();
            for entry in &pane.entries {
                if pane.dir_paths.contains(&entry.path) {
                    folders.push(entry.clone());
                } else {
                    files.push(entry.clone());
                }
            }
            // Folders lead the listing, and they obey the same column and direction as the files do.
            sort_entries(
                &mut folders,
                self.settings.sort_key,
                self.settings.sort_ascending,
            );
            sort_entries(
                &mut files,
                self.settings.sort_key,
                self.settings.sort_ascending,
            );
            folders.extend(files);
            pane.entries = folders;
        }
    }

    /// Column order from settings, deduped and topped up: a saved listing that lost a column must not hide one
    /// forever.
    fn list_columns(&self) -> Vec<Column> {
        let mut out: Vec<Column> = Vec::new();
        for col in &self.settings.list_columns {
            if !out.contains(col) {
                out.push(*col);
            }
        }
        for col in Self::COLUMNS {
            if !out.contains(&col) {
                out.push(col);
            }
        }
        out
    }

    fn column_width(&self, col: Column) -> f32 {
        self.settings
            .column_widths
            .get(&col)
            .copied()
            .unwrap_or(120.0)
            .clamp(60.0, 900.0)
    }

    fn set_column_width(&mut self, col: Column, width: f32) {
        self.settings
            .column_widths
            .insert(col, width.clamp(60.0, 900.0));
    }

    /// Which boundary a dragged header drops before (0 = first, `widths.len()` = last). Pure so the drag math
    /// can be tested without synthesizing pointer motion.
    fn column_drop_index(x: f32, widths: &[f32]) -> usize {
        let mut acc = 0.0;
        for (i, w) in widths.iter().enumerate() {
            acc += w;
            if x < acc {
                return i;
            }
        }
        widths.len()
    }

    /// Move a column so it sits before index `target` - Qt's drag-a-header-to-reorder.
    fn reorder_columns(&mut self, dragged: Column, target: usize) {
        let mut columns = self.list_columns();
        let Some(from) = columns.iter().position(|c| *c == dragged) else {
            return;
        };
        columns.remove(from);
        // The drop index counted the column being moved; removing it shifted everything after one place left.
        let at = if from < target { target - 1 } else { target };
        columns.insert(at.min(columns.len()), dragged);
        self.settings.list_columns = columns;
        self.save_settings();
    }

    /// What one cell of a list row shows.
    fn cell_text(&self, col: Column, entry: &Entry, is_dir: bool) -> String {
        match col {
            Column::Name => {
                let tag = if is_dir {
                    "DIR"
                } else {
                    Self::kind_tag(entry.kind)
                };
                format!("{tag}  {}", entry.name)
            }
            Column::Modified => date_label(entry.modified),
            // A directory's size in bytes means nothing; Qt leaves the cell empty rather than show 0.
            Column::Size => {
                if is_dir {
                    String::new()
                } else {
                    format_size(entry.size)
                }
            }
            Column::Type => type_label(entry.kind, is_dir, &entry.path),
        }
    }

    /// The short tag a cell or row shows when there is no picture for an entry. Plain text because egui's
    /// default font has no icon glyphs to imitate Qt's per-type icons with.
    fn kind_tag(kind: MediaKind) -> &'static str {
        match kind {
            MediaKind::Image => "IMG",
            MediaKind::Video => "VID",
            MediaKind::Sequence => "SEQ",
            MediaKind::Audio => "AUD",
            MediaKind::Other => "DOC",
        }
    }

    /// Grid view of one pane: engine thumbnails in rows that fit the column; double-click plays.
    fn pane_grid(&mut self, index: usize, ui: &mut egui::Ui) {
        // A cell wider than its column overflows the scroll area and cuts thumbnails mid-image.
        let edge_px = self.settings.thumbnail_size.clamp(32, 512);
        let edge = (edge_px as f32).min((ui.available_width() - 12.0).max(48.0));
        let cell = edge + 12.0;
        let cells: Vec<Entry> = self.panes[index].entries.clone();
        let cols = ((ui.available_width() / cell).floor() as usize).max(1);
        self.panes[index].grid_cols = cols;
        // Ctrl on Linux and Windows, Command on macOS - the platform's "add to selection" key.
        let ctrl = ui.input(|i| i.modifiers.command);
        // The pointer is known before layout runs, so this pass draws the frame `scrub_at` chose last pass.
        let shown = self.panes[index]
            .scrub_shown
            .as_ref()
            .map(|(path, tex)| (path.clone(), tex.id(), tex.size_vec2()));
        let fine = ui.input(|i| i.modifiers.ctrl);
        let mut selected: Option<PathBuf> = None;
        let mut play: Option<PathBuf> = None;
        let mut hovered: Option<PathBuf> = None;
        let mut scrub: Option<(Entry, egui::Pos2, egui::Rect)> = None;
        // As in the list view: only the region below the breadcrumb and toolbar accepts a drop.
        let body = ui.available_rect_before_wrap();
        egui::ScrollArea::vertical()
            .id_salt(format!("grid{index}"))
            .auto_shrink(false)
            .show(ui, |ui| {
                for row in cells.chunks(cols) {
                    ui.horizontal(|ui| {
                        for entry in row {
                            let chosen = self.panes[index].is_selected(&entry.path);
                            ui.allocate_ui_with_layout(
                                egui::vec2(cell, cell + 24.0),
                                egui::Layout::top_down(egui::Align::Center),
                                |ui| {
                                    let top = ui.cursor().min;
                                    // The cell's own rectangle: the allocation above is (cell, cell + 24).
                                    let cell_rect = egui::Rect::from_min_size(
                                        top,
                                        egui::vec2(cell, cell + 24.0),
                                    );
                                    if chosen {
                                        let tint = ui.visuals().selection.bg_fill;
                                        ui.painter().rect_filled(cell_rect, 2.0, tint);
                                    }
                                    // A hover frame wins over the still thumbnail for this entry.
                                    let picture = shown
                                        .as_ref()
                                        .filter(|(p, _, _)| p == &entry.path)
                                        .map(|(_, id, px)| (*id, *px))
                                        .or_else(|| {
                                            self.pictures
                                                .get(&entry.path)
                                                .map(|tex| (tex.id(), tex.size_vec2()))
                                        });
                                    // The scrub surface is the whole square GalleryBrowser reserves for it,
                                    // not the aspect-fitted picture: a portrait clip must scrub across its
                                    // full width and a landscape clip across its full height.
                                    let thumb = egui::Rect::from_min_size(
                                        top + egui::vec2((cell - edge) / 2.0, 0.0),
                                        egui::vec2(edge, edge),
                                    );
                                    match picture {
                                        Some((id, px)) => {
                                            let scale =
                                                (edge / px.x.max(1.0)).min(edge / px.y.max(1.0));
                                            ui.image(egui::load::SizedTexture {
                                                id,
                                                size: px * scale,
                                            });
                                        }
                                        None => {
                                            // A cell with no picture still has to say what it is: an empty square
                                            // reads as a broken app, which is how every non-media file and every
                                            // not-yet-decoded thumbnail looked. Qt shows a per-type icon here.
                                            let rect = egui::Rect::from_min_size(
                                                ui.cursor().min,
                                                egui::vec2(edge, edge),
                                            );
                                            let weak = ui.visuals().weak_text_color();
                                            ui.painter().rect_filled(
                                                rect,
                                                4.0,
                                                ui.visuals().faint_bg_color,
                                            );
                                            let tag = if self.panes[index]
                                                .dir_paths
                                                .contains(&entry.path)
                                            {
                                                "DIR"
                                            } else {
                                                Self::kind_tag(entry.kind)
                                            };
                                            let galley = ui.painter().layout_no_wrap(
                                                tag.to_owned(),
                                                egui::FontId::proportional(13.0),
                                                weak,
                                            );
                                            ui.painter().galley(
                                                rect.center() - galley.size() / 2.0,
                                                galley,
                                                weak,
                                            );
                                            ui.allocate_space(egui::vec2(edge, edge));
                                        }
                                    }
                                    // Long names clip in a narrow pane, so elide them to the cell width.
                                    let budget = (cell / 7.0) as usize;
                                    let head: String = entry.name.chars().take(budget).collect();
                                    let label = if head.chars().count() < entry.name.chars().count()
                                    {
                                        format!("{head}\u{2026}")
                                    } else {
                                        head
                                    };
                                    ui.label(label);
                                    // One target for the whole cell: picture, name and the space between
                                    // them select, play and hover.
                                    let response = ui.interact(
                                        cell_rect,
                                        ui.id().with(("cell", entry.path.clone())),
                                        egui::Sense::click_and_drag().union(egui::Sense::hover()),
                                    );
                                    self.publish_drag(ui, &response, index, &entry.path);
                                    if self.panes[index].scroll_target.as_deref()
                                        == Some(entry.path.as_path())
                                    {
                                        response.scroll_to_me(Some(egui::Align::Center));
                                        self.panes[index].scroll_target = None;
                                    }
                                    if response.double_clicked() {
                                        play = Some(entry.path.clone());
                                    }
                                    if response.clicked() {
                                        selected = Some(entry.path.clone());
                                    }
                                    if let Some(pos) = response.hover_pos() {
                                        hovered = Some(entry.path.clone());
                                        // Only the thumbnail square scrubs, as GalleryBrowser does.
                                        if thumb.contains(pos) {
                                            scrub = Some((entry.clone(), pos, thumb));
                                        }
                                    }
                                    // Qt selects the right-clicked file before showing its menu. The
                                    // selection rides the secondary click itself: egui's `context_menu_opened`
                                    // only reports true once the popup has been registered by a previous
                                    // `context_menu` call, so it cannot gate this in the same frame.
                                    // Qt makes the right-clicked file current - in that pane - even when it
                                    // was already part of the selection; otherwise a right-click in the
                                    // inactive pane switches the keyboard while playback and rename keep
                                    // following the other pane's file.
                                    if response.secondary_clicked()
                                        && (self.active != index
                                            || self.panes[index].primary()
                                                != Some(entry.path.as_path()))
                                    {
                                        self.select_in(index, entry.path.clone(), false);
                                    }
                                    response.context_menu(|ui| self.entry_menu(ui, index));
                                },
                            );
                        }
                    });
                }
            });
        self.accept_drop(ui, body, index);
        self.finish(index, selected, ctrl, play, hovered);
        if let Some((entry, pos, thumb)) = scrub {
            self.scrub_at(ui, index, &entry, edge_px, pos, thumb, fine);
        }
    }

    /// Apply what a pane's rows reported: make it active, select, and play on double-click.
    fn finish(
        &mut self,
        index: usize,
        selected: Option<PathBuf>,
        ctrl: bool,
        play: Option<PathBuf>,
        hovered: Option<PathBuf>,
    ) {
        if let Some(path) = selected {
            self.select_in(index, path, ctrl);
        }
        if let Some(path) = play {
            // Double-click means "go into" for a folder and "play" for a file.
            if self.panes[index].dir_paths.contains(&path) {
                self.navigate(index, &path);
            } else {
                self.play(path);
            }
        }
        // Landing on another file resets scrub state; leaving the entry drops the frames so the thumbnail
        // shows again. A frame belongs to one file, so a stale one must never stand in for another.
        if hovered != self.panes[index].hover {
            self.panes[index].scrub_target = None;
            self.panes[index].scrub_wait_until = std::time::Instant::now();
            self.panes[index].scrub_frames.clear();
            self.panes[index].scrub_shown = None;
        }
        self.panes[index].hover = hovered;
    }

    /// Enter activates the current item: enter a folder, play a file. The same thing double-click does, from
    /// the keyboard.
    fn activate_target(&mut self) {
        let index = self.active;
        let Some(path) = self.panes[index].primary().map(Path::to_path_buf) else {
            self.note("Nothing selected");
            return;
        };
        if self.panes[index].dir_paths.contains(&path) {
            self.navigate(index, &path);
        } else {
            self.play(path);
        }
    }

    /// A decoded frame belongs to the clip it came from, so it may only stand in for the picture while that
    /// clip is what the active pane has selected. Selecting a still must show the still, even while an
    /// earlier clip stays loaded and paused.
    fn preview_shows_frame(&self) -> bool {
        let primary = self.panes[self.active].primary();
        self.frame.is_some() && self.playing.as_deref() == primary
    }

    /// The preview panel: the playing frame if anything is loaded, else the active pane's **selection**.
    /// Hover scrub stays in the grid cell - a hovered clip must not restage the preview (user report).
    pub fn preview(&mut self, ui: &mut egui::Ui) {
        ui.heading("Preview");
        ui.separator();
        let picture = self
            .frame
            .as_ref()
            .filter(|_tex| self.preview_shows_frame())
            .map(|tex| (tex.id(), tex.size_vec2()))
            .or_else(|| {
                let pane = &self.panes[self.active];
                pane.preview
                    .as_ref()
                    .filter(|(path, _)| Some(path.as_path()) == pane.wanted.as_deref())
                    .map(|(_, tex)| (tex.id(), tex.size_vec2()))
            });
        match picture {
            Some((id, pixels)) => {
                let avail = ui.available_size();
                let scale = (avail.x / pixels.x.max(1.0))
                    .min(avail.y / pixels.y.max(1.0))
                    .min(1.0);
                ui.image(egui::load::SizedTexture {
                    id,
                    size: pixels * scale,
                });
            }
            None => {
                ui.label("Select a file to preview, double-click to play it.");
            }
        }
        self.annotations_ui(ui);
        ui.separator();
        self.transport(ui);
    }

    /// Transport for the loaded file: play/pause, stop, position readout and a seek slider.
    fn transport(&mut self, ui: &mut egui::Ui) {
        let state = self.player.state();
        if !state.loaded {
            ui.label("Nothing playing.");
            return;
        }
        let loaded = self.playing.clone().unwrap_or_default();
        ui.label(
            loaded
                .file_name()
                .map(|n| n.to_string_lossy().into_owned())
                .unwrap_or_default(),
        );
        let mut toggle = false;
        let mut stop = false;
        if ui
            .button(if state.playing { "Pause" } else { "Play" })
            .clicked()
        {
            toggle = true;
        }
        ui.separator();
        if ui.button("Stop").clicked() {
            stop = true;
        }
        // Volume, mute and loop are settings the player already honours, so they belong on the transport.
        let mut volume = self.settings.volume;
        // The slider's value display is an editable text field (click it and type), so while it holds the
        // keyboard a digit must set the volume and not rate a file, and Delete must not trash the selection.
        let vol = ui.add(egui::Slider::new(&mut volume, 0.0..=1.0).text("vol"));
        self.typing_in_field |= vol.has_focus();
        let mute = ui.checkbox(&mut self.settings.muted, "mute");
        let looping = ui.checkbox(&mut self.settings.loop_playback, "loop");
        if vol.changed() {
            self.settings.volume = volume;
            self.player.set_volume(volume);
        }
        if mute.changed() {
            self.player.set_muted(self.settings.muted);
        }
        if looping.changed() {
            self.player.set_looping(self.settings.loop_playback);
        }
        // Saving writes and fsyncs, so persist when the interaction ends rather than once per dragged frame.
        let ended = vol.drag_stopped() || (!vol.dragged() && vol.changed());
        if ended || mute.changed() || looping.changed() {
            self.save_settings();
        }
        let seconds = state.position.seconds().max(0.0);
        let total = state.duration.seconds().max(0.0);
        ui.separator();
        let mut slider = seconds;
        let response = ui.add(egui::Slider::new(&mut slider, 0.0..=total).show_value(false));
        if state.error.is_some() {
            ui.colored_label(egui::Color32::LIGHT_RED, "decode error");
        }
        if toggle {
            self.player.toggle();
        }
        if stop {
            self.stop_playback();
        }
        if response.changed() || response.dragged() {
            let target = Tick::from_seconds_f64(slider);
            self.player.seek(target);
        }
        ui.separator();
        ui.label(format!("{seconds:.1} / {total:.1}s"));
    }

    /// The file-operation shortcuts, applied in one place so their wiring is testable without
    /// input plumbing. `keys` comes from the frame's input; nothing here reads the pointer.
    fn apply_file_keys(&mut self, keys: &FileKeys) {
        if keys.settings {
            self.settings_open = !self.settings_open;
        }
        if keys.copy {
            self.copy_selection(false);
        }
        if keys.cut {
            self.copy_selection(true);
        }
        if keys.paste {
            self.paste();
        }
        if let Some(rating) = keys.rating {
            self.set_rating(rating);
        }
        if keys.trash {
            self.trash_selection();
        }
        if keys.perm_delete {
            self.delete_permanently();
        }
        if keys.new_folder {
            self.new_folder();
        }
        let active = self.active;
        if keys.up_parent {
            let parent = self.panes[active].path.parent().map(Path::to_path_buf);
            if let Some(parent) = parent {
                self.navigate(active, &parent);
            }
        }
        if keys.refresh {
            self.open(active);
        }
        if keys.undo && self.ops.undo().is_none() {
            self.status = "nothing to undo".into();
        }
        if keys.redo && self.ops.redo().is_none() {
            self.status = "nothing to redo".into();
        }
        if let Some(step) = keys.step {
            if let Some(path) = self.panes[active].step(step, self.panes[active].grid_cols) {
                // Selection is the only way the preview and the player follow, so the move goes through it.
                self.select_in(active, path, false);
            } else {
                self.note("Nothing to select in this folder");
            }
        }
        if keys.activate {
            self.activate_target();
        }
        if keys.rename {
            self.start_rename();
        }
    }

    /// The bottom status bar: the active pane's folder, how much is there, and what is selected.
    pub fn status_bar(&mut self, ui: &mut egui::Ui) {
        let pane = &self.panes[self.active];
        ui.label(pane.path.display().to_string());
        ui.separator();
        ui.label(format!("{} entries", pane.entries.len()));
        if self.settings.dual_pane {
            ui.separator();
            ui.label(format!("pane {}", self.active + 1));
        }
        let selection = &self.panes[self.active].selection;
        match selection.len() {
            0 => {}
            1 => {
                ui.separator();
                ui.label(
                    selection[0]
                        .file_name()
                        .map(|n| n.to_string_lossy().into_owned())
                        .unwrap_or_default(),
                );
            }
            count => {
                ui.separator();
                ui.label(format!("{count} selected"));
            }
        }
        if let Some((paths, cut)) = &self.clipboard {
            ui.separator();
            let what = if paths.len() == 1 {
                paths[0]
                    .file_name()
                    .map(|n| n.to_string_lossy().into_owned())
                    .unwrap_or_default()
            } else {
                format!("{} files", paths.len())
            };
            ui.label(format!("{}{what}", if *cut { "cut: " } else { "copied: " }));
        }
        if !self.op_progress.is_empty() {
            ui.separator();
            ui.label(self.op_progress.clone());
        } else if self.ops.can_undo() {
            // The user should see what Ctrl+Z gives back before pressing it.
            ui.separator();
            ui.label(format!(
                "undo: {}",
                self.ops.undo_label().unwrap_or_default()
            ));
        }
        if !self.status.is_empty() {
            ui.separator();
            ui.colored_label(egui::Color32::LIGHT_RED, &self.status);
        }
    }

    /// The global path bar: view toggle and settings button (panes carry their own breadcrumbs).
    /// The folders inside `dir`, sorted and cached per opened folder. Directories only, honouring the
    /// hidden-files setting; an unreadable directory has no children rather than being an error.
    fn child_dirs(&mut self, dir: &Path) -> Vec<PathBuf> {
        if let Some(cached) = self.tree.kids.get(dir) {
            return cached.clone();
        }
        let mut kids: Vec<PathBuf> = std::fs::read_dir(dir)
            .map(|entries| {
                entries
                    .flatten()
                    .map(|entry| entry.path())
                    .filter(|path| path.is_dir())
                    .filter(|path| {
                        // The same rule the file listing uses, which on Windows also reads the
                        // FILE_ATTRIBUTE_HIDDEN flag rather than only a leading dot.
                        self.settings.show_hidden || !is_hidden(path)
                    })
                    .collect()
            })
            .unwrap_or_default();
        kids.sort();
        self.tree.kids.insert(dir.to_path_buf(), kids.clone());
        kids
    }

    /// Drop a folder's cached listing so a new subfolder appears without reopening the tree.
    fn refresh_tree(&mut self, dir: &Path) {
        self.tree.kids.remove(dir);
    }

    /// Re-list any pane whose folder changed on disk while the app was looking the other way - a file copied in
    /// from a terminal, a folder renamed in a file manager. The engine's scan is the source of truth; this only
    /// decides when to ask it again.
    ///
    /// Ceiling worth stating: a folder's mtime moves when something is created, deleted or renamed *in* it, not
    /// when a file deeper down changes or a file's contents are edited, and the pane's own folder is all that is
    /// polled. Qt's watcher also tracked per-file changes; those still need Refresh (or a real watcher) until one
    /// earns its dependency.
    fn light_refresh(&mut self, now: f64) {
        if now < self.next_light_check {
            return;
        }
        self.next_light_check = now + LIGHT_REFRESH_SECS;
        let mut relisted = false;
        for index in 0..self.panes.len() {
            let path = self.panes[index].path.clone();
            // A folder that vanished mid-poll is left alone: the pane keeps showing what it has, and operations
            // already report the failure when something is tried in it.
            let Ok(meta) = std::fs::metadata(&path) else {
                continue;
            };
            if self.panes[index].folder_mtime != meta.modified().ok() {
                // `open_quiet` keeps the selection that still exists and drops the preview with a primary that
                // no longer does, so a refresh cannot strand the panel on a departed file. It also clears the
                // tree's cached listing for this folder, so new subfolders reach the tree in the same tick.
                self.open_quiet(index);
                relisted = true;
            }
        }
        if relisted {
            // A re-listed pane must ask the engine for its pictures, or a file that appeared outside the app
            // shows a placeholder block forever - the grid only reads pictures already in the cache. Once for
            // all panes: `begin_batch` drops the previous generation's queued work.
            self.refresh_thumbs();
        }
    }

    fn toggle_tree_open(&mut self, dir: &Path) {
        if self.tree.open.remove(dir) {
            // Closing a folder leaves its children in `kids`; reopening must not re-read the disk.
        } else {
            self.tree.open.insert(dir.to_path_buf());
            self.child_dirs(dir);
        }
    }

    fn is_favorite(&self, dir: &Path) -> bool {
        self.settings.favorites.iter().any(|fav| fav == dir)
    }

    fn add_favorite(&mut self, dir: &Path) {
        if !self.is_favorite(dir) {
            self.settings.favorites.push(dir.to_path_buf());
            self.save_settings();
        }
    }

    fn remove_favorite(&mut self, dir: &Path) {
        let before = self.settings.favorites.len();
        self.settings.favorites.retain(|fav| fav != dir);
        if self.settings.favorites.len() != before {
            self.save_settings();
        }
    }

    /// A tree row or favorite clicked: the active pane goes there.
    fn open_in_active(&mut self, dir: &Path) {
        let active = self.active;
        self.navigate(active, dir);
    }

    /// The file view's context menu. Qt keeps it separate from the folder tree's (`mw.cpp:3172-3201`), and
    /// right-clicking an unselected file selects it first, so the operations act on what was clicked. Only
    /// operations that exist today are offered: Bulk Rename, Properties, Open With, Convert Format and the
    /// project entries join their own features instead of shipping as dead menu items.
    fn entry_menu(&mut self, ui: &mut egui::Ui, index: usize) {
        // The menu belongs to the pane under the cursor, and every action below works on that pane's
        // selection (which right-click has just made the clicked file, unless it was already part of one).
        self.active = index;
        let selected = !self.panes[index].selection.is_empty();
        let folder = self.panes[index].path.clone();
        let is_favorite = self.is_favorite(&folder);
        if ui.button("Refresh").clicked() {
            self.open(index);
            ui.close();
        }
        if ui
            .add_enabled(selected, egui::Button::new("Copy"))
            .on_hover_text("Ctrl+C")
            .clicked()
        {
            self.copy_selection(false);
            ui.close();
        }
        if ui
            .add_enabled(selected, egui::Button::new("Cut"))
            .on_hover_text("Ctrl+X")
            .clicked()
        {
            self.copy_selection(true);
            ui.close();
        }
        let paste_ready = self.clipboard.is_some();
        if ui
            .add_enabled(paste_ready, egui::Button::new("Paste"))
            .on_hover_text("Ctrl+V")
            .clicked()
        {
            self.paste();
            ui.close();
        }
        let one_selected = self.panes[index].selection.len() == 1;
        if ui
            .add_enabled(one_selected, egui::Button::new("Rename"))
            .on_hover_text("F2 - one file at a time")
            .clicked()
        {
            self.start_rename();
            ui.close();
        }
        if ui
            .add_enabled(selected, egui::Button::new("Move to Trash"))
            .on_hover_text("Delete - undoable")
            .clicked()
        {
            self.trash_selection();
            ui.close();
        }
        if ui
            .add_enabled(selected, egui::Button::new("Delete Permanently"))
            .on_hover_text("Shift+Delete - not undoable")
            .clicked()
        {
            self.delete_permanently();
            ui.close();
        }
        let action = if is_favorite {
            "Remove from Favorites"
        } else {
            "Add to Favorites"
        };
        if ui.button(action).clicked() {
            if is_favorite {
                self.remove_favorite(&folder);
            } else {
                self.add_favorite(&folder);
            }
            ui.close();
        }
        if ui
            .button("Open in File Manager")
            .on_hover_text(folder.display().to_string())
            .clicked()
        {
            if let Err(err) = open_in_file_manager(&folder) {
                self.status = format!("file manager: {err}");
            }
            ui.close();
        }
    }

    /// The left column: favorites above the folder tree, as Qt arranges them in one splitter
    /// (`mw.cpp:1500-1535`). Qt's other tree-menu entries (Create Folder with Selected Files, Add Asset
    /// Library, Import Project Manager) need the project surfaces and land with P4.
    pub fn browser_tree(&mut self, ui: &mut egui::Ui) {
        let favorites = self.settings.favorites.clone();
        let active_path = self.panes[self.active].path.clone();
        egui::ScrollArea::vertical()
            .auto_shrink([false; 2])
            .show(ui, |ui| {
                ui.strong("Favorites");
                if favorites.is_empty() {
                    ui.label("right-click a folder in the tree to add one");
                }
                for favorite in favorites {
                    let label = favorite
                        .file_name()
                        .map(|name| name.to_string_lossy().into_owned())
                        .unwrap_or_else(|| favorite.display().to_string());
                    let row = ui.selectable_label(active_path == favorite, format!("* {label}"));
                    if row.clicked() {
                        self.open_in_active(&favorite);
                    }
                    row.context_menu(|ui| {
                        if ui.button("Open").clicked() {
                            self.open_in_active(&favorite);
                            ui.close();
                        }
                        if ui.button("Remove Favorite").clicked() {
                            self.remove_favorite(&favorite);
                            ui.close();
                        }
                    });
                }
                ui.separator();
                let arrow = if self.tree.computer_open { "v" } else { ">" };
                ui.horizontal(|ui| {
                    if ui
                        .button(arrow)
                        .on_hover_text("Show or hide drives")
                        .clicked()
                    {
                        self.tree.computer_open = !self.tree.computer_open;
                    }
                    ui.label("Computer");
                });
                if self.tree.computer_open {
                    for root in drive_roots() {
                        self.tree_node(&root, ui, 0);
                    }
                }
            });
    }

    /// One folder row in the tree: an arrow that loads children on first open, a label that navigates the
    /// active pane, and the tree's own context menu (Qt keeps it separate from the file view's menu,
    /// `mw.cpp:3307-3315, 3408-3413`). Each level takes its own child list before recursing, so no borrow
    /// of `self` is held across the recursion.
    /// The tree follows the pane you are looking at: switch panes or navigate and the folder you are in is
    /// opened to, with its ancestors expanded and its row scrolled into view. It is event-driven - a folder
    /// you collapsed by hand stays collapsed until the next navigation or pane switch.
    fn tree_follow_pane(&mut self) {
        let target = (self.active, self.panes[self.active].path.clone());
        if self.tree_revealed.as_ref() == Some(&target) {
            return;
        }
        self.tree_revealed = Some(target.clone());
        let (_, path) = target;
        // `ancestors()` runs from the folder itself down to the filesystem root. Opening all of them is what
        // makes a row reachable: a collapsed ancestor has no row to reveal. The Computer node is the wrapper
        // the roots hang off, so it has to be open for any of this to be visible.
        self.tree.computer_open = true;
        for ancestor in path.ancestors() {
            if ancestor.is_dir() {
                self.tree.open.insert(ancestor.to_path_buf());
            }
        }
        self.tree_reveal = Some(path);
    }

    fn tree_node(&mut self, dir: &Path, ui: &mut egui::Ui, depth: usize) {
        let open = self.tree.open.contains(dir);
        let arrow = if open { "v" } else { ">" };
        let name = dir
            .file_name()
            .map(|name| name.to_string_lossy().into_owned())
            .unwrap_or_else(|| dir.display().to_string());
        let active_here = self.panes[self.active].path == dir;
        ui.horizontal(|ui| {
            ui.add_space(depth as f32 * 10.0);
            if ui
                .button(arrow)
                .on_hover_text("Show or hide subfolders")
                .clicked()
            {
                self.toggle_tree_open(dir);
            }
            let row = ui.selectable_label(active_here, name);
            if self.tree_reveal.as_deref() == Some(dir) {
                // Asking the enclosing scroll area to bring this row in is egui's own path; doing it here (at
                // the row's own draw) is the only moment its position is known.
                row.scroll_to_me(Some(egui::Align::Center));
                self.tree_reveal = None;
            }
            if row.clicked() {
                self.open_in_active(dir);
            }
            let menu_dir = dir.to_path_buf();
            row.context_menu(|ui| {
                if ui.button("New Folder").clicked() {
                    // The listing is dropped when the operation reports back, not here.
                    self.new_folder_in(&menu_dir);
                    ui.close();
                }
                let favorite = self.is_favorite(&menu_dir);
                let action = if favorite {
                    "Remove from Favorites"
                } else {
                    "Add to Favorites"
                };
                if ui.button(action).clicked() {
                    if favorite {
                        self.remove_favorite(&menu_dir);
                    } else {
                        self.add_favorite(&menu_dir);
                    }
                    ui.close();
                }
                if ui.button("Refresh").clicked() {
                    self.refresh_tree(&menu_dir);
                    self.tree.open.insert(menu_dir.clone());
                    ui.close();
                }
            });
        });
        if open {
            for kid in self.child_dirs(dir) {
                self.tree_node(&kid, ui, depth + 1);
            }
        }
    }

    pub fn app_bar(&mut self, ui: &mut egui::Ui) {
        let mut settings = false;
        let mut tree = false;
        ui.horizontal(|ui| {
            // View mode, thumbnail size, dual pane and the file operations live on each pane's toolbar,
            // as they do in Qt; this bar is only for the global surfaces.
            if ui.button("Settings").clicked() {
                settings = true;
            }
            if ui
                .button("Folders")
                .on_hover_text("Show or hide the folder tree and favorites")
                .clicked()
            {
                tree = true;
            }
        });
        if settings {
            self.settings_open = !self.settings_open;
        }
        if tree {
            self.settings.show_tree = !self.settings.show_tree;
            self.save_settings();
        }
    }

    /// The settings window: theme, view, panes, thumbnail size and the folder filters. Changes apply at
    /// once and are written to `AppDirs::settings_file()`.
    pub fn settings_window(&mut self, ui: &mut egui::Ui) {
        let mut rescan = false;
        let mut dirty = false;
        egui::Window::new("Settings")
            .collapsible(false)
            .resizable(false)
            .show(ui, |ui| {
                ui.horizontal(|ui| {
                    ui.label("Theme:");
                    for theme in [Theme::Dark, Theme::Light, Theme::System] {
                        if ui
                            .selectable_label(
                                self.settings.theme == theme,
                                Self::theme_label(theme),
                            )
                            .clicked()
                        {
                            self.settings.theme = theme;
                            dirty = true;
                        }
                    }
                });
                // View mode, thumbnail size and dual pane moved onto the pane toolbars (Qt keeps them
                // there too); they stay persisted in `Settings`.
                if ui
                    .checkbox(&mut self.settings.show_hidden, "Show hidden files")
                    .changed()
                {
                    rescan = true;
                    dirty = true;
                    // The tree cached its listings under the old filter, so hidden folders are either
                    // missing or lingering.
                    self.tree.kids.clear();
                }
                if ui
                    .checkbox(&mut self.settings.show_all_files, "Show non-media files")
                    .changed()
                {
                    rescan = true;
                    dirty = true;
                }
                if ui
                    .checkbox(
                        &mut self.settings.collapse_sequences,
                        "Collapse image sequences",
                    )
                    .changed()
                {
                    rescan = true;
                    dirty = true;
                }
                if ui.button("Close").clicked() {
                    self.settings_open = false;
                }
            });
        if dirty {
            self.save_settings();
        }
        if rescan {
            for index in 0..2 {
                self.open_quiet(index);
            }
            self.refresh_thumbs();
        }
    }
}

impl App {
    /// Last frame's text focus decides this frame's shortcut gate, and the flag is re-armed so our text
    /// widgets can claim the keyboard again as they render. Read-and-rearm in one place: clearing it inside
    /// the reader made the gate last exactly one frame, so a field that stayed focused had to re-arm on every
    /// pass just to keep the browser silent.
    fn begin_shortcut_frame(&mut self) -> bool {
        let typing = self.typing_in_field;
        self.typing_in_field = false;
        typing
    }

    /// Keyboard shortcuts, read once per frame before the panels render. Kept out of `eframe::App::ui` so a
    /// test can drive them alongside real clicks.
    ///
    /// They are off only while one of *our* text fields holds the keyboard. Gating on egui's focus alone
    /// switched every shortcut off whenever any widget held focus - and egui takes focus for the widgets it
    /// has, so a click on a toolbar button or a selectable row silenced Ctrl+C/X/V until the pointer moved
    /// again. Live report: buttons and menu items worked, keyboard copy/cut/paste did not.
    fn read_file_keys(&mut self, ui: &egui::Ui, typing: bool) -> FileKeys {
        // A keystroke can arrive in the same input batch as the click that focuses one of our fields, so last
        // frame's flag is not enough on its own: if this frame's focus belongs to a field we rendered, the
        // browser stays silent too. The volume slider's inner editor keeps only the per-frame arm above - egui
        // gives no handle on that child id - so its same-batch edge is a known ceiling.
        let focused_now = ui.ctx().memory(|m| m.focused());
        let on_our_field = focused_now.is_some()
            && [self.rename_field_id, self.annotate_field_id]
                .into_iter()
                .flatten()
                .any(|id| Some(id) == focused_now);
        if typing || on_our_field {
            return FileKeys::default();
        }
        ui.input(|i| {
            let m = i.modifiers.command;
            let shift = i.modifiers.shift;
            // Ctrl+C/X/V do not arrive as `Key::C` here: egui-winit recognises them itself, pushes the
            // dedicated `Event::Copy`/`Cut`/`Paste`, and returns without emitting a key event at all - and it
            // suppresses `Event::Text` for Ctrl-modified keys. So `key_pressed(C) && command` can never fire
            // on Linux, which is why copy/cut/paste "did not even register" live while every test that sends a
            // clean `Key::C` passed. Read the events the platform actually sends; the key path stays for the
            // layouts and platforms where egui does hand over a key.
            let copy_event = i.events.iter().any(|e| matches!(e, egui::Event::Copy));
            let cut_event = i.events.iter().any(|e| matches!(e, egui::Event::Cut));
            let paste_event = i.events.iter().any(|e| matches!(e, egui::Event::Paste(_)));
            let step = [
                (egui::Key::ArrowDown, Step::RowForward),
                (egui::Key::ArrowUp, Step::RowBack),
                (egui::Key::ArrowRight, Step::ItemForward),
                (egui::Key::ArrowLeft, Step::ItemBack),
                (egui::Key::Home, Step::First),
                (egui::Key::End, Step::Last),
            ]
            .into_iter()
            .find(|(key, _)| i.key_pressed(*key))
            .map(|(_, step)| step);
            FileKeys {
                settings: i.key_pressed(egui::Key::S) && !m,
                copy: copy_event || i.key_pressed(egui::Key::C) && m && !shift,
                cut: cut_event || i.key_pressed(egui::Key::X) && m,
                paste: paste_event || i.key_pressed(egui::Key::V) && m,
                undo: i.key_pressed(egui::Key::Z) && m && !shift,
                redo: i.key_pressed(egui::Key::Y) && m || i.key_pressed(egui::Key::Z) && m && shift,
                trash: i.key_pressed(egui::Key::Delete) && !shift,
                perm_delete: i.key_pressed(egui::Key::Delete) && shift,
                up_parent: i.key_pressed(egui::Key::Backspace),
                refresh: i.key_pressed(egui::Key::F5),
                new_folder: i.key_pressed(egui::Key::N) && m && shift,
                rename: i.key_pressed(egui::Key::F2),
                rating: [
                    (egui::Key::Num0, 0),
                    (egui::Key::Num1, 1),
                    (egui::Key::Num2, 2),
                    (egui::Key::Num3, 3),
                    (egui::Key::Num4, 4),
                    (egui::Key::Num5, 5),
                ]
                .into_iter()
                .find(|(key, _)| i.key_pressed(*key))
                .map(|(_, value)| value),
                step,
                activate: i.key_pressed(egui::Key::Enter),
            }
        })
    }
}

impl eframe::App for App {
    /// egui 0.36 hands the app a root `Ui` (not a `Context`); panels are shown on it.
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        self.poll_pictures(ui);
        self.pump_player(ui);
        self.poll_ops();
        if self.ops.busy() {
            // ponytail: poll while an op runs; event-driven wakeups when the engine can notify us.
            ui.ctx().request_repaint();
        }
        self.apply_theme(ui.ctx());
        if let Some(text) = self.clipboard_publish.take() {
            ui.ctx().copy_text(text);
        }
        self.tree_follow_pane();
        let typing = self.begin_shortcut_frame();
        let keys = self.read_file_keys(ui, typing);

        self.apply_file_keys(&keys);
        egui::Panel::top("app_bar").show(ui, |ui| self.app_bar(ui));
        egui::Panel::bottom("status_bar").show(ui, |ui| {
            ui.horizontal(|ui| self.status_bar(ui));
        });
        if self.settings.show_tree {
            egui::Panel::left("browser_tree")
                .resizable(true)
                .size_range(150.0..=420.0)
                .default_size(210.0)
                .show(ui, |ui| self.browser_tree(ui));
        }
        if self.settings.show_preview {
            egui::Panel::right("preview")
                .resizable(true)
                .size_range(180.0..=460.0)
                .default_size(300.0)
                .show(ui, |ui| self.preview(ui));
        }
        let panes = self.settings.dual_pane;
        egui::CentralPanel::default().show(ui, |ui| {
            if panes {
                // The second pane docks to the right of the first with a draggable splitter, like Qt.
                let available = ui.available_width();
                egui::Panel::right("pane2")
                    .resizable(true)
                    .min_size(240.0)
                    // Without a ceiling the docked pane can take everything and collapse pane 1 to nothing.
                    .max_size((available - 240.0).max(240.0))
                    .default_size((available / 2.0).clamp(240.0, (available - 300.0).max(240.0)))
                    .show(ui, |ui| self.pane(1, ui));
                egui::CentralPanel::default().show(ui, |ui| self.pane(0, ui));
            } else {
                self.pane(0, ui);
            }
        });
        if self.settings_open {
            self.settings_window(ui);
        }
        if self.renaming {
            self.rename_window(ui);
        }
        if self.annotate.is_some() {
            self.annotate_window(ui);
        }
        // Nothing in this app was watching the disk: a file copied in from a terminal stayed out of the listing
        // until you pressed Refresh. Poll on the interval Qt debounced its watcher to, and ask egui to wake at
        // that rate - it stays asleep between ticks, so idle cost is one metadata call per pane.
        let now = ui.ctx().input(|i| i.time);
        self.light_refresh(now);
        ui.ctx()
            .request_repaint_after(std::time::Duration::from_secs_f64(LIGHT_REFRESH_SECS));
    }
}

#[cfg(test)]
#[allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
mod tests {
    use super::*;

    /// A throwaway folder under the temp dir (the test runner points `TMPDIR` inside the repo, so no
    /// artifact lands outside the project). Errors are ignored: a missing sandbox fails the asserts.
    fn sandbox(name: &str, files: &[&str], subdirs: &[&str]) -> PathBuf {
        let root = std::env::temp_dir().join(format!("kasset-{}-{name}", std::process::id()));
        let _ = std::fs::create_dir_all(&root);
        for file in files {
            let _ = std::fs::write(root.join(file), b"x");
        }
        for dir in subdirs {
            let _ = std::fs::create_dir_all(root.join(dir));
        }
        root
    }

    /// Browsing reads media entries and folders from the engine, and navigation re-scans.
    #[test]
    fn browsing_lists_media_and_folders_then_navigation_follows() {
        let child = sandbox("child", &["a.png", "b.mov"], &[]);
        let parent = sandbox("parent", &[], &["child"]);

        let mut app = App::with_cache(child.clone(), sandbox("thumbs", &[], &[]));
        assert_eq!(
            app.panes[0].entries.len(),
            2,
            "both media files should be listed"
        );
        assert!(
            app.status.is_empty(),
            "scanning the sandbox failed: {}",
            app.status
        );

        app.navigate(0, &parent);
        assert_eq!(app.panes[0].path, parent);
        // A pane lists files; the left tree is what offers folders.
        assert_eq!(
            app.child_dirs(&parent),
            vec![parent.join("child")],
            "the left tree can offer the folder inside the one being browsed"
        );
        assert_eq!(
            app.panes[0].entries.len(),
            1,
            "a pane shows the folders inside the one it is on, as Qt's browser does"
        );
        assert!(
            app.panes[0].dir_paths.contains(&parent.join("child")),
            "that entry is the folder, not a file"
        );

        let _ = std::fs::remove_dir_all(&child);
        let _ = std::fs::remove_dir_all(&parent);
    }

    /// Clicking a header sorts by that column, and clicking it again flips the direction. Directories stay
    /// ahead of files: that grouping is Qt's, and a sort must not dissolve it.
    #[test]
    fn a_header_sorts_by_its_column_and_folders_stay_first() {
        let root = std::env::temp_dir().join(format!("kasset-cols-{}", std::process::id()));
        let _ = std::fs::create_dir_all(root.join("aa-folder"));
        let _ = std::fs::write(root.join("small.png"), vec![0u8; 10]);
        let _ = std::fs::write(root.join("big.png"), vec![0u8; 5_000]);
        let mut app = App::with_cache(root.clone(), sandbox("cols-cache", &[], &[]));

        app.sort_by_column(Column::Size);
        let names: Vec<String> = app.panes[0]
            .entries
            .iter()
            .map(|e| e.name.clone())
            .collect();
        assert_eq!(
            names,
            vec!["aa-folder", "small.png", "big.png"],
            "smallest first, folders lead"
        );

        app.sort_by_column(Column::Size);
        let names: Vec<String> = app.panes[0]
            .entries
            .iter()
            .map(|e| e.name.clone())
            .collect();
        assert_eq!(
            names,
            vec!["aa-folder", "big.png", "small.png"],
            "the same header again flips the direction"
        );
        assert!(
            !app.settings.sort_ascending,
            "the direction is left where the user put it"
        );

        let _ = std::fs::remove_dir_all(&root);
    }

    /// Dragging a column title has to land it where the marker shows, and the order has to survive a restart.
    #[test]
    fn dragging_a_header_lands_where_the_marker_sits() {
        let widths = [320.0, 140.0, 90.0, 100.0];
        assert_eq!(
            App::column_drop_index(10.0, &widths),
            0,
            "over the first title means first"
        );
        assert_eq!(
            App::column_drop_index(325.0, &widths),
            1,
            "just past the first boundary drops before the second"
        );
        assert_eq!(App::column_drop_index(470.0, &widths), 2);
        assert_eq!(App::column_drop_index(560.0, &widths), 3);
        assert_eq!(
            App::column_drop_index(9_999.0, &widths),
            4,
            "past the end means last"
        );

        let dir = sandbox("reorder", &["a.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("reorder-cache", &[], &[]));
        assert_eq!(
            app.list_columns(),
            vec![Column::Name, Column::Modified, Column::Size, Column::Type],
            "the default order is Qt's"
        );
        app.reorder_columns(Column::Type, 0);
        assert_eq!(
            app.list_columns(),
            vec![Column::Type, Column::Name, Column::Modified, Column::Size],
            "the dragged title becomes the first column"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// The new columns are read the way a file browser reads them.
    #[test]
    fn sizes_dates_and_types_read_like_a_file_browser() {
        assert_eq!(format_size(900), "900 B");
        assert_eq!(format_size(1_536), "1.5 KB");
        assert_eq!(format_size(5_242_880), "5.0 MB");
        assert_eq!(
            date_label(None),
            "",
            "an unknown date is an empty cell, not a zero"
        );
        assert!(!date_label(Some(std::time::SystemTime::now())).is_empty());
        let png = std::path::Path::new("/x/shot.png");
        assert_eq!(type_label(MediaKind::Image, false, png), "Image");
        assert_eq!(type_label(MediaKind::Other, true, png), "Folder");
        assert_eq!(
            type_label(MediaKind::Other, false, std::path::Path::new("/x/log.txt")),
            "TXT file"
        );
    }

    /// A settings file from an older build - or edited badly - must not hide a column forever.
    #[test]
    fn a_saved_listing_that_lost_a_column_gets_it_back() {
        let dir = sandbox("cols-lost", &["a.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("cols-lost-cache", &[], &[]));
        app.settings.list_columns = vec![Column::Name, Column::Name];
        assert_eq!(
            app.list_columns(),
            vec![Column::Name, Column::Modified, Column::Size, Column::Type],
            "duplicates drop and the missing columns come back"
        );
        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Qt's item views move their current item on the arrows by themselves, so there was nothing to port and
    /// egui has no default: the panes simply ignored them.
    #[test]
    fn arrows_step_the_listing_and_clamp_at_the_ends() {
        let dir = sandbox("arrows", &["a.png", "b.mov"], &["aa-folder", "zz-folder"]);
        let mut app = App::with_cache(dir.clone(), sandbox("arrows-cache", &[], &[]));
        // Folders lead, each group in natural order: aa-folder, zz-folder, a.png, b.mov. One column wide, as a
        // list view is, so Down is the next entry and Up the one above it.
        let steps: [(&str, Step); 9] = [
            ("aa-folder", Step::RowForward), // nothing selected yet: Down picks the first item
            ("zz-folder", Step::RowForward),
            ("aa-folder", Step::RowBack),
            ("aa-folder", Step::RowBack), // nowhere above: stays put, does not wrap
            ("b.mov", Step::Last),
            ("b.mov", Step::RowForward), // nowhere below
            ("aa-folder", Step::First),
            ("zz-folder", Step::ItemForward),
            ("aa-folder", Step::ItemBack),
        ];
        for (name, step) in steps {
            let moved = app.panes[0].step(step, 1);
            assert_eq!(
                moved.as_deref().and_then(|p| p.file_name()),
                Some(std::ffi::OsStr::new(name)),
                "{name:?} should be current after {step:?}"
            );
            assert_eq!(
                app.panes[0].selection.len(),
                1,
                "the move leaves exactly one current item"
            );
            assert!(
                app.panes[0].scroll_target.is_some(),
                "the row that became current is the one to bring into view"
            );
        }

        // Arrowing after a multi-selection collapses onto the new current item, as Qt's model does.
        app.panes[0].selection = vec![dir.join("aa-folder"), dir.join("zz-folder")];
        assert_eq!(
            app.panes[0].step(Step::RowForward, 1).as_deref(),
            Some(dir.join("a.png").as_path()),
            "the step runs from the last item clicked"
        );
        assert_eq!(app.panes[0].selection.len(), 1);

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Enter does what double-click does: go into a folder, play a file.
    #[test]
    fn enter_opens_a_folder_and_plays_a_file() {
        let dir = sandbox("enter", &[], &["aa-folder"]);
        let mut app = App::with_cache(dir.clone(), sandbox("enter-cache", &[], &[]));

        app.panes[0].selection = vec![dir.join("aa-folder")];
        app.activate_target();
        assert_eq!(
            app.panes[0].path,
            dir.join("aa-folder"),
            "Enter goes into the folder that is current"
        );

        app.panes[0].selection.clear();
        app.activate_target();
        assert!(
            app.status.contains("Nothing selected"),
            "Enter with nothing current says so: {}",
            app.status
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Down is the cell below and Right the next cell - the grid's own geometry, not one flat order. Where
    /// there is no row below, Down stays put instead of sliding into another column.
    #[test]
    fn in_a_grid_down_moves_a_row_and_right_one_cell() {
        let dir = sandbox(
            "arrows-grid",
            &["a.png", "b.mov"],
            &["aa-folder", "zz-folder"],
        );
        let mut app = App::with_cache(dir.clone(), sandbox("arrows-grid-cache", &[], &[]));
        // Two cells to a row: aa-folder, zz-folder on the first row; a.png, b.mov on the second.
        let grid = 2;
        app.panes[0].selection = vec![dir.join("aa-folder")];
        let moves: [(&str, Step); 6] = [
            ("a.png", Step::RowForward),  // straight down one row
            ("b.mov", Step::ItemForward), // and right is the next cell along
            ("zz-folder", Step::RowBack), // up again
            ("a.png", Step::ItemForward), // Qt's icon view steps to the previous row's last cell
            ("a.png", Step::RowForward),  // no row below: stays put, does not slide left
            ("zz-folder", Step::ItemBack),
        ];
        for (name, step) in moves {
            assert_eq!(
                app.panes[0]
                    .step(step, grid)
                    .as_deref()
                    .and_then(|p| p.file_name()),
                Some(std::ffi::OsStr::new(name)),
                "{step:?} from a two-cell row should land on {name:?}"
            );
        }
        // The layout is what says how wide a row is, so the pane records it as it draws.
        assert_eq!(
            app.panes[0].grid_cols, 1,
            "a list is one cell wide until a grid draws itself"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Renaming a folder from one pane while the other pane is inside it: the operation touched the *parent*,
    /// so before this fix the browsing pane kept a listing, a title and a path pointing at a name that no
    /// longer exists.
    #[test]
    fn a_pane_follows_the_folder_it_is_in_when_that_folder_is_renamed() {
        let root = sandbox("rename-parent", &[], &["aa-folder"]);
        let old = root.join("aa-folder");
        let _ = std::fs::write(old.join("inner.png"), b"x");
        let mut app = App::with_cache(old.clone(), sandbox("rename-parent-cache", &[], &[]));
        app.settings.dual_pane = true;
        app.navigate(1, &root);

        app.ops.submit(FileOp::Rename {
            from: old.clone(),
            to: root.join("bb-folder"),
        });
        let new = root.join("bb-folder");
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
        while app.panes[0].path != new && std::time::Instant::now() < deadline {
            app.poll_ops();
            std::thread::sleep(std::time::Duration::from_millis(20));
        }

        assert!(
            new.is_dir(),
            "the folder was renamed (status: {})",
            app.status
        );
        assert_eq!(
            app.panes[0].path, new,
            "the pane inside the folder moved with it"
        );
        assert!(
            app.panes[0].entries.iter().any(|e| e.name == "inner.png"),
            "and it is still showing that folder's contents: {:?}",
            app.panes[0]
                .entries
                .iter()
                .map(|e| e.name.clone())
                .collect::<Vec<_>>()
        );
        // The tree follows the new path once that pane is the one in use, and nothing stale survives under
        // the folder's old name.
        app.active = 0;
        app.tree_follow_pane();
        assert!(
            app.tree.open.contains(&new),
            "the tree reveals where pane 0 now is"
        );
        assert!(
            !app.tree.kids.contains_key(&old),
            "no listing is cached under the folder's old name"
        );

        let _ = std::fs::remove_dir_all(&root);
    }

    /// A folder replaced by a file of the same name (from outside the app) must not be listed twice - once as
    /// the file the scan found and once as the synthesised folder entry from a stale cached listing.
    #[test]
    fn a_folder_that_became_a_file_is_listed_once() {
        let root = sandbox("dir-to-file", &[], &["x.png"]);
        let mut app = App::with_cache(root.clone(), sandbox("dir-to-file-cache", &[], &[]));
        assert!(
            app.panes[0].dir_paths.contains(&root.join("x.png")),
            "it starts out as a folder"
        );

        let _ = std::fs::remove_dir_all(root.join("x.png"));
        let _ = std::fs::write(root.join("x.png"), b"not a folder any more");
        app.open(0);

        let listed = app.panes[0]
            .entries
            .iter()
            .filter(|e| e.name == "x.png")
            .count();
        assert_eq!(listed, 1, "one row for the file that is there now");
        assert!(
            !app.panes[0].dir_paths.contains(&root.join("x.png")),
            "it is not a folder any more"
        );

        let _ = std::fs::remove_dir_all(&root);
    }

    /// Directories lead the listing, but they are still rows: sorting by Size descending has to put `zz`
    /// before `aa`, exactly like the files.
    #[test]
    fn folders_obey_the_column_direction_too() {
        let root = sandbox(
            "folder-sort",
            &["b.png", "a.mov"],
            &["aa-folder", "zz-folder"],
        );
        let mut app = App::with_cache(root.clone(), sandbox("folder-sort-cache", &[], &[]));

        app.settings.sort_key = SortKey::Name;
        app.settings.sort_ascending = false;
        app.resort_panes();
        let names: Vec<String> = app.panes[0]
            .entries
            .iter()
            .map(|e| e.name.clone())
            .collect();
        assert_eq!(
            names,
            vec!["zz-folder", "aa-folder", "b.png", "a.mov"],
            "descending puts the folders and the files each in descending order"
        );

        // The same must be true of a folder read fresh, not only of a re-sort.
        app.open_quiet(0);
        let names: Vec<String> = app.panes[0]
            .entries
            .iter()
            .map(|e| e.name.clone())
            .collect();
        assert_eq!(
            names,
            vec!["zz-folder", "aa-folder", "b.png", "a.mov"],
            "loading a folder obeys the direction as well"
        );

        let _ = std::fs::remove_dir_all(&root);
    }

    /// Dragging a column to the right has to land it where the marker sat. Removing the source shifts every
    /// later column one place left, and the drop index counted the column being moved - so a rightward drop
    /// used to overshoot by one.
    #[test]
    fn dragging_a_column_right_lands_it_left_of_the_marker() {
        let dir = sandbox("reorder-right", &["a.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("reorder-right-cache", &[], &[]));
        // Default order is Name, Modified, Size, Type. A marker sitting before Size (index 2) means the
        // dragged column ends up just left of Size - not left of Modified.
        app.reorder_columns(Column::Name, 2);
        assert_eq!(
            app.list_columns(),
            vec![Column::Modified, Column::Name, Column::Size, Column::Type],
            "the dragged column sits where the marker was"
        );
        // A leftward drop needs no adjustment.
        app.reorder_columns(Column::Name, 0);
        assert_eq!(
            app.list_columns(),
            vec![Column::Name, Column::Modified, Column::Size, Column::Type],
            "back to the default order"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// A start folder given relative to the shell - `.` when a file has no parent, or anything typed on the
    /// command line - could never be matched against the tree's absolute roots, so the tree never revealed it.
    #[test]
    fn a_relative_start_folder_is_stored_absolute() {
        let here = std::env::current_dir().unwrap_or_default();
        assert_eq!(
            absolute(Path::new(".")),
            here,
            "a relative path resolves against the process's own directory"
        );
        assert_eq!(
            absolute(Path::new("/already/abs")),
            PathBuf::from("/already/abs"),
            "an absolute path is left alone"
        );

        let app = App::with_cache(PathBuf::from("."), sandbox("rel-start-cache", &[], &[]));
        assert_eq!(
            app.panes[0].path, here,
            "a pane started at `.` stores the folder absolutely, where the tree can match it"
        );
    }

    /// A browser that forgets where you were makes you find it again every launch. The folders are written down
    /// at close and reopened at start - and a folder that has gone is skipped rather than restored into a blank pane.
    #[test]
    fn the_app_reopens_the_folders_it_was_closed_in() {
        let root = sandbox("restore", &["one.png"], &["inside", "other"]);
        let mut app = App::with_cache(root.clone(), sandbox("restore-cache", &[], &[]));
        // Startup decides whether folders are worth writing down: restore them, or start remembering from the
        // folder the command line named. Until then the window's own first listing must not overwrite them.
        app.start_folder_memory();

        // Moving a pane is what writes the folder down - no exit hook involved.
        app.navigate(0, &root.join("inside"));
        assert_eq!(
            app.settings.last_folder.as_deref(),
            Some(root.join("inside").as_path()),
            "the folder you were in is what gets written down"
        );
        assert_eq!(
            app.settings.last_folder_secondary, None,
            "a single-pane close remembers no second folder"
        );

        app.set_dual_pane(true);
        app.navigate(1, &root.join("other"));
        assert_eq!(
            app.settings.last_folder_secondary.as_deref(),
            Some(root.join("other").as_path()),
            "a two-pane close remembers both panes where they were"
        );

        // What the next window reads back out of the settings file.
        let mut reopened = App::with_cache(root.clone(), sandbox("restore-cache2", &[], &[]));
        reopened.settings.last_folder = Some(root.join("inside").clone());
        reopened.settings.last_folder_secondary = Some(root.join("other").clone());
        reopened.settings.dual_pane = true;
        assert!(
            reopened.restore_folders(),
            "the remembered folders are reopened, both panes at once"
        );
        assert_eq!(reopened.panes[0].path, root.join("inside"));
        assert_eq!(reopened.panes[1].path, root.join("other"));
        assert!(
            reopened.settings.dual_pane,
            "restoring a two-pane session does not quietly turn it into one pane"
        );

        // A session left in one pane must not resurrect the second pane that was closed. This runs while both
        // remembered folders still exist - after the next case deletes one, the suppression would never be
        // reached and the assertion would prove nothing.
        let mut single = App::with_cache(root.clone(), sandbox("restore-cache4", &[], &[]));
        single.settings.dual_pane = false;
        single.settings.last_folder = Some(root.join("inside").clone());
        single.settings.last_folder_secondary = Some(root.join("other").clone());
        assert!(
            single.restore_folders(),
            "the remembered folder is reopened for the pane that was left open"
        );
        assert_eq!(
            single.panes[1].path, root,
            "a folder left in a second pane that was closed does not reopen two panes"
        );

        // Restoring is arriving, not navigating: the folder the window had guessed stays off the Back stack.
        let mut fresh = App::with_cache(root.clone(), sandbox("restore-cache5", &[], &[]));
        fresh.settings.last_folder = Some(root.join("inside").clone());
        assert!(fresh.restore_folders(), "the remembered folder is reopened");
        assert!(
            fresh.panes[0].back_stack.is_empty(),
            "restoring invents no Back history: the guessed start folder is not a visit - {:?}",
            fresh.panes[0].back_stack
        );

        let mut stale = App::with_cache(root.clone(), sandbox("restore-cache3", &[], &[]));
        stale.settings.last_folder = Some(root.join("inside").clone());
        std::fs::remove_dir(root.join("inside"))
            .expect("the remembered folder is deleted before the next start");
        assert!(
            !stale.restore_folders(),
            "a folder that no longer exists is not restored"
        );
        assert_eq!(
            stale.panes[0].path, root,
            "and the folder it started in stays listed, not blanked"
        );

        let _ = std::fs::remove_dir_all(&root);
    }

    /// A press that never moved is a click, not a carry: releasing it over the same listing must not copy files
    /// onto themselves. This is the rule that keeps egui's 0.8-second hold from turning a long click into a drop.
    #[test]
    fn only_real_motion_makes_a_press_into_a_carry() {
        let pressed = egui::pos2(100.0, 50.0);
        assert!(
            !carried_by_drag(pressed, pressed),
            "a hold in place is still a click when released"
        );
        assert!(
            !carried_by_drag(pressed, egui::pos2(104.0, 50.0)),
            "a few points of drift is still deciding, not carrying"
        );
        assert!(
            carried_by_drag(pressed, egui::pos2(140.0, 60.0)),
            "travelling past the click tolerance means carrying"
        );
    }

    /// A drop inside the app is a file operation with Qt's rules for its panes: copy by default, move while Shift
    /// is held. This box cannot inject a mouse drag, so what this pins is what landing on a folder does - and
    /// what refusing to land at all does; the pointer path itself is for hand verification.
    #[test]
    fn dropping_files_onto_a_folder_copies_and_shift_moves() {
        let root = sandbox("drop", &["one.png", "two.png"], &["elsewhere"]);
        let mut app = App::with_cache(root.clone(), sandbox("drop-cache", &[], &[]));
        let dest = root.join("elsewhere");

        app.drop_files(
            Dragged {
                paths: vec![root.join("one.png")],
                from: root.clone(),
            },
            dest.clone(),
            false,
        );
        assert!(
            settle(&mut app, &dest, "one.png", true) && root.join("one.png").is_file(),
            "a drop copies the file and leaves the original where it was"
        );

        app.drop_files(
            Dragged {
                paths: vec![root.join("two.png")],
                from: root.clone(),
            },
            dest.clone(),
            true,
        );
        assert!(
            settle(&mut app, &dest, "two.png", true) && !root.join("two.png").exists(),
            "Shift moves it instead of copying"
        );

        // Moving files into the folder they are standing in is nothing to do: it says so, with no op submitted.
        app.drop_files(
            Dragged {
                paths: vec![dest.join("one.png")],
                from: dest.clone(),
            },
            dest.clone(),
            true,
        );
        assert_eq!(app.status, "Already in this folder");

        // Carrying a folder into itself is how you get a copy that contains itself; the engine is not asked.
        app.drop_files(
            Dragged {
                paths: vec![root.clone()],
                from: root.clone(),
            },
            dest.clone(),
            false,
        );
        assert_eq!(app.status, "Cannot drop a folder into itself");

        let _ = std::fs::remove_dir_all(&root);
    }

    /// Files come from outside the app - a terminal copy, another file manager. The pane re-reads its folder on
    /// the tick after that happens, and not before: the interval is Qt's debounce window, so a burst of changes
    /// costs one re-list rather than one per change.
    #[test]
    fn a_folder_that_changed_on_disk_gets_listed_again() {
        let dir = sandbox("light-refresh", &["one.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("light-refresh-cache", &[], &[]));
        let names = |app: &App| -> Vec<String> {
            app.panes[0]
                .entries
                .iter()
                .map(|e| e.name.clone())
                .collect()
        };
        assert!(
            names(&app).contains(&"one.png".to_string()),
            "the start folder is listed"
        );

        std::fs::write(dir.join("two.png"), b"x").expect("a file appears from outside the app");
        assert_ne!(
            std::fs::metadata(&dir)
                .and_then(|meta| meta.modified())
                .ok(),
            app.panes[0].folder_mtime,
            "a new file inside the folder moves the folder's own mtime"
        );

        app.light_refresh(1.0);
        assert!(
            names(&app).contains(&"two.png".to_string()),
            "the pane re-reads the folder that changed: {:?}",
            names(&app)
        );

        std::fs::write(dir.join("three.png"), b"x").expect("another one arrives");
        app.light_refresh(1.5);
        assert!(
            !names(&app).contains(&"three.png".to_string()),
            "a tick inside the debounce window must not re-list: {:?}",
            names(&app)
        );
        app.light_refresh(2.5);
        assert!(
            names(&app).contains(&"three.png".to_string()),
            "the next tick catches up: {:?}",
            names(&app)
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// What a file manager reads off the clipboard. Only the characters that would break the URI are escaped -
    /// enough for the names this app is pointed at, with the ceiling written down rather than pretended away.
    #[test]
    fn a_path_becomes_a_file_uri() {
        assert_eq!(
            file_uri(Path::new("/home/npittas/Pictures/one.png")),
            "file:///home/npittas/Pictures/one.png"
        );
        assert_eq!(
            file_uri(Path::new("/home/x/a place/#1?.png")),
            "file:///home/x/a%20place/%231%3F.png",
            "spaces and URI metacharacters are escaped"
        );
        assert_eq!(
            file_uri(Path::new("/home/x/100%.png")),
            "file:///home/x/100%25.png",
            "a percent in a name cannot eat the other escapes"
        );
    }

    /// The tree used to sit collapsed until the user clicked a node, and stayed put when the active pane
    /// changed. It has to follow the pane: open the ancestors of the folder being browsed and bring that row
    /// into view - on navigation and on a pane switch, but not every frame (that would fight the user's own
    /// scrolling).
    #[test]
    fn the_tree_follows_the_active_pane() {
        let parent = sandbox("reveal", &[], &["aa-folder", "bb-folder"]);
        let inside = parent.join("aa-folder");
        let other = parent.join("bb-folder");
        let mut app = App::with_cache(parent.clone(), sandbox("reveal-cache", &[], &[]));

        app.tree_follow_pane();
        assert!(
            app.tree.open.contains(&parent),
            "the folder being browsed is opened in the tree"
        );

        app.navigate(0, &inside);
        app.tree_follow_pane();
        assert!(
            app.tree.open.contains(&inside),
            "navigating into a folder opens it in the tree"
        );
        assert_eq!(
            app.tree_reveal.as_deref(),
            Some(inside.as_path()),
            "that row is the one to scroll into view"
        );

        // Pane 2 with a different folder moves the tree on its own: no navigation happened.
        app.settings.dual_pane = true;
        app.navigate(1, &other);
        app.active = 1;
        app.tree_follow_pane();
        assert_eq!(
            app.tree_reveal.as_deref(),
            Some(other.as_path()),
            "switching the active pane moves the tree"
        );

        // Nothing changed, so nothing is re-queued: a reveal every frame would yank the scroll back under the
        // user's own drag.
        app.tree_reveal = None;
        app.tree_follow_pane();
        assert!(
            app.tree_reveal.is_none(),
            "an unchanged pane does not re-reveal"
        );

        let _ = std::fs::remove_dir_all(&parent);
    }

    /// Qt's browser lists the folders inside the one you are on, ahead of the files. A pane that showed only
    /// media left the user navigating by tree alone, and "directories first" had nothing to sort.
    #[test]
    fn folders_come_before_files_in_a_pane() {
        let dir = sandbox(
            "pane-folders",
            &["a.png", "b.mov"],
            &["zz-folder", "aa-folder"],
        );
        let mut app = App::with_cache(dir.clone(), sandbox("pane-folders-cache", &[], &[]));

        let names: Vec<String> = app.panes[0]
            .entries
            .iter()
            .map(|e| e.name.clone())
            .collect();
        assert_eq!(
            names,
            vec!["aa-folder", "zz-folder", "a.png", "b.mov"],
            "folders first, each group in the user's order"
        );
        assert!(
            app.panes[0].dir_paths.contains(&dir.join("aa-folder")),
            "the pane knows which entries are folders"
        );

        // Double-clicking a folder enters it; double-clicking a file asks for playback.
        app.finish(0, None, false, Some(dir.join("aa-folder")), None);
        assert_eq!(
            app.panes[0].path,
            dir.join("aa-folder"),
            "double-click goes into the folder"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Selecting an entry records it and points the preview request at it.
    #[test]
    fn selecting_an_entry_points_the_preview_at_it() {
        let dir = sandbox("select", &["a.png", "b.mov"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("select-thumbs", &[], &[]));

        let image = dir.join("a.png");
        app.select(image.clone());
        assert_eq!(app.panes[0].selection, vec![image.clone()]);
        assert_eq!(
            app.panes[0].wanted.as_deref(),
            Some(image.as_path()),
            "the engine should be asked for this entry's preview"
        );

        let video = dir.join("b.mov");
        app.select(video.clone());
        assert_eq!(app.panes[0].selection, vec![video.clone()]);
        assert_eq!(
            app.panes[0].wanted.as_deref(),
            Some(video.as_path()),
            "the preview request follows the new selection"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Selecting a clip loads it into the player **paused** (user rule: no autoplay on selection), so the
    /// preview panel has its transport without sound starting. A still selects without touching playback.
    #[test]
    fn selecting_a_clip_loads_it_paused() {
        let fixture =
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/scrub.mp4");
        let dir = sandbox("paused", &["up.png"], &[]);
        let clip = dir.join("clip.mp4");
        std::fs::copy(&fixture, &clip).expect("the scrub fixture should be copyable");

        let mut app = App::with_cache(dir.clone(), sandbox("paused-cache", &[], &[]));
        app.navigate(0, &dir);
        assert_eq!(
            app.panes[0].entries.len(),
            2,
            "the clip and the still should be listed"
        );

        app.select(clip.clone());
        assert_eq!(
            app.playing.as_deref(),
            Some(clip.as_path()),
            "a selected clip belongs in the player so its transport is there"
        );
        let state = app.player.state();
        assert!(state.loaded, "the clip should be loaded");
        assert!(!state.playing, "selection must not autoplay");

        // A still selects without touching playback: the loaded clip stays loaded and paused.
        let image = dir.join("up.png");
        app.select(image.clone());
        assert_eq!(app.target().as_deref(), Some(image.as_path()));
        let state = app.player.state();
        assert!(
            state.loaded && !state.playing,
            "a still must not start or stop playback"
        );

        // Stop rewinds and pauses but keeps the file ours to pump; a transport that could play with no frames
        // was the trap here (press Play after Stop and the picture stayed invisible).
        let loaded = app.playing.clone();
        app.stop_playback();
        assert_eq!(app.playing, loaded, "stop must not release the loaded path");
        let state = app.player.state();
        assert!(
            state.loaded && !state.playing,
            "stop rewinds and pauses, still loaded"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Opening a folder asks the engine for one thumbnail per media entry; view mode is app state.
    #[test]
    fn opening_a_folder_asks_for_one_thumbnail_per_entry() {
        let dir = sandbox("thumbs", &["a.png", "b.mov", "c.txt"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("thumbs-cache", &[], &[]));

        assert_eq!(
            app.panes[0].entries.len(),
            2,
            "the text file is not media and is not listed"
        );
        assert_eq!(
            app.outstanding,
            app.panes[0].entries.len(),
            "one thumbnail request per entry should be outstanding"
        );
        assert!(
            matches!(app.settings.view_mode, ViewMode::Grid),
            "the engine default is grid"
        );

        app.set_view_mode(ViewMode::List);
        assert!(matches!(app.settings.view_mode, ViewMode::List));

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// A file the engine cannot open is reported and loads nothing; a real media file loads.
    #[test]
    fn play_loads_a_real_file_and_reports_a_broken_one() {
        let dir = sandbox("play", &["broken.mov"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("play-cache", &[], &[]));

        app.play(dir.join("broken.mov"));
        assert!(
            app.playing.is_none(),
            "a file the engine cannot open stays unloaded"
        );
        assert!(
            !app.status.is_empty(),
            "the failure is shown to the user: {}",
            app.status
        );

        let real = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Icons/Up.png");
        app.play(real.clone());
        assert_eq!(
            app.playing.as_deref(),
            Some(real.as_path()),
            "a real media file should load (status: {})",
            app.status
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// The folder filters come from settings, so the settings window's checkboxes change what a scan
    /// reports.
    #[test]
    fn folder_filters_follow_settings() {
        let dir = sandbox("filters", &["a.png", ".hidden.png", "notes.txt"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("filters-cache", &[], &[]));
        assert_eq!(
            app.panes[0].entries.len(),
            1,
            "defaults hide hidden and non-media files"
        );

        app.settings.show_hidden = true;
        app.settings.show_all_files = true;
        app.open(0);
        assert_eq!(
            app.panes[0].entries.len(),
            3,
            "both filters on lists all three files"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Wait (bounded) for the engine worker to report a finished op; then check whether `name` is in
    /// `dir` as expected. False if the worker never answers.
    /// The catalog is keyed by path, so a file operation must carry annotations with the file: a rating
    /// and tag set on a file are found at its new name once the rename finishes in the ops worker.
    #[test]
    fn annotations_follow_a_rename_through_the_worker() {
        let dir = sandbox("catalog", &["a.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("catalog-cache", &[], &[]));

        let file = dir.join("a.png");
        app.select(file.clone());
        app.set_rating(4);
        app.apply_annotate(AnnotateKind::Tag, "keepme", &file);
        assert_eq!(app.library.annotations(&file).rating, 4);

        let renamed = dir.join("renamed.png");
        app.ops.submit(FileOp::Rename {
            from: file.clone(),
            to: renamed.clone(),
        });
        // The worker runs on its own thread; `poll_ops` is what feeds the catalog the move.
        for _ in 0..100 {
            app.poll_ops();
            if app.library.annotations(&renamed).rating == 4 {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(20));
        }

        let notes = app.library.annotations(&renamed);
        assert_eq!(notes.rating, 4, "the rating should follow the renamed file");
        assert!(
            notes.tags.iter().any(|tag| tag == "keepme"),
            "the tag should follow the renamed file: {:?}",
            notes.tags
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// A move and its undo can both finish before the app next looks at the worker; the catalog must replay
    /// them in order so the rating ends up back where the file is.
    #[test]
    fn annotations_return_with_an_undo_drained_in_the_same_poll() {
        // A move and its undo can finish before the app next looks at the worker. The catalog must replay
        // them one operation at a time: `Library::relink` maps simultaneously, so a merged batch would
        // leave the rating at the moved name while the file is already back where it started.
        let dir = sandbox("catalog-undo", &["a.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("catalog-undo-cache", &[], &[]));

        let a = dir.join("a.png");
        let b = dir.join("b.png");
        app.select(a.clone());
        app.set_rating(4);

        app.ops.submit(FileOp::Rename {
            from: a.clone(),
            to: b.clone(),
        });
        // Let the rename land without taking its event, then queue the undo: both outcomes reach
        // `poll_ops` together. History is written as the op completes, so wait for it to be undoable.
        for _ in 0..100 {
            if app.ops.can_undo() {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(20));
        }
        assert!(app.ops.undo().is_some(), "the rename should be undoable");
        for _ in 0..100 {
            if !app.ops.busy() {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(20));
        }

        app.poll_ops();
        assert!(
            a.exists() && !b.exists(),
            "undo should have moved the file back to its original name"
        );
        assert_eq!(
            app.library.annotations(&a).rating,
            4,
            "the rating should be back at the original path (status: {})",
            app.status
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Hover scrub is GalleryBrowser's, not a playback loop: the pointer's x across a clip's thumbnail picks
    /// the slot, and a decoded frame reaches the pane through the engine. Driven through egui's own input
    /// pipeline because this layout maths has been wrong twice already.
    #[test]
    fn hovering_a_clip_scrubs_by_pointer_position() {
        use egui_kittest::Harness;

        let fixture =
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/scrub.mp4");
        assert!(fixture.is_file(), "the scrub fixture should be committed");

        let dir = sandbox("scrub", &[], &[]);
        let clip = dir.join("clip.mp4");
        let _ = std::fs::copy(&fixture, &clip);

        let mut app = App::with_cache(dir.clone(), sandbox("scrub-cache", &[], &[]));
        app.settings.view_mode = ViewMode::Grid;
        app.navigate(0, &dir);
        assert_eq!(app.panes[0].entries.len(), 1, "the clip should be listed");

        let mut harness = Harness::new_ui_state(
            |ui, app| {
                app.poll_pictures(ui);
                app.pane(0, ui);
            },
            app,
        );
        harness.set_size(egui::vec2(900.0, 700.0));

        // Sweep the pointer until some thumbnail square claims a scrub slot.
        let mut pos: Option<egui::Pos2> = None;
        'sweep: for row in 0..8 {
            for col in 0..16 {
                let p = egui::pos2(120.0 + col as f32 * 45.0, 60.0 + row as f32 * 35.0);
                harness.input_mut().events = vec![egui::Event::PointerMoved(p)];
                harness.run_steps(2);
                if harness.state().panes[0].scrub_target.is_some() {
                    pos = Some(p);
                    break 'sweep;
                }
            }
        }
        let pos = pos.expect("pointing at the clip's thumbnail should choose a scrub slot");

        // The engine decodes on its own threads, so keep making passes until a frame arrives.
        let mut shown = false;
        for _ in 0..60 {
            harness.input_mut().events = vec![egui::Event::PointerMoved(pos)];
            harness.run_steps(1);
            if harness.state().panes[0].scrub_shown.is_some() {
                shown = true;
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(30));
        }

        let target = harness.state().panes[0]
            .scrub_target
            .clone()
            .expect("a scrub slot should still be chosen");
        assert_eq!(target.0, clip, "the hovered clip owns the scrub target");
        assert_eq!(
            target.2, SCRUB_SLOTS,
            "no Ctrl held means the coarse 24-slot division"
        );
        assert!(target.1 < target.2, "the slot is inside the division");
        assert!(
            shown,
            "the engine should deliver a hover frame for the hovered slot"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// A shortcut must survive a click on a file. egui hands keyboard focus to any widget the user clicks,
    /// and the shortcuts were switched off whenever *any* widget held focus - so clicking a thumbnail turned
    /// Ctrl+C, Ctrl+X and Ctrl+V off until the pointer did something else. Live report: copy, cut and paste
    /// "did not work" while every button and menu item did.
    #[test]
    fn ctrl_c_after_clicking_a_cell_copies_it() {
        use egui_kittest::Harness;

        let dir = sandbox("copy-focus", &["one.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("copy-focus-cache", &[], &[]));
        app.settings.view_mode = ViewMode::Grid;
        let one = dir.join("one.png");

        // The shortcut read and the pane render must share a frame, as they do in `App::ui`.
        let mut harness = Harness::new_ui_state(
            |ui, app| {
                let typing = app.begin_shortcut_frame();
                let keys = app.read_file_keys(ui, typing);
                app.pane(0, ui);
                app.apply_file_keys(&keys);
            },
            app,
        );
        harness.set_size(egui::vec2(900.0, 700.0));

        let mut cell: Option<egui::Pos2> = None;
        'find: for row in 0..8 {
            for col in 0..16 {
                let pos = egui::pos2(120.0 + col as f32 * 45.0, 60.0 + row as f32 * 35.0);
                harness.input_mut().events = vec![egui::Event::PointerMoved(pos)];
                harness.run_steps(2);
                if harness.state().panes[0].hover.as_deref() == Some(one.as_path()) {
                    cell = Some(pos);
                    break 'find;
                }
            }
        }
        let pos = cell.unwrap_or_else(|| panic!("the only thumbnail should be hoverable"));

        let click = |pressed: bool| {
            vec![egui::Event::PointerButton {
                pos,
                button: egui::PointerButton::Primary,
                pressed,
                modifiers: egui::Modifiers::default(),
            }]
        };
        harness.input_mut().events = click(true);
        harness.run_steps(1);
        harness.input_mut().events = click(false);
        harness.run_steps(2);
        assert_eq!(
            harness.state().panes[0].selection,
            vec![one.clone()],
            "clicking the cell selects it"
        );

        // Ctrl+C with the real modifier state, as winit reports it on Linux.
        harness.input_mut().events = vec![egui::Event::ModifiersChanged(egui::Modifiers {
            command: true,
            ctrl: true,
            ..Default::default()
        })];
        harness.run_steps(1);
        harness.input_mut().events = vec![egui::Event::Key {
            key: egui::Key::C,
            physical_key: Some(egui::Key::C),
            pressed: true,
            repeat: false,
            modifiers: egui::Modifiers {
                command: true,
                ctrl: true,
                ..Default::default()
            },
        }];
        harness.run_steps(2);
        let state = harness.state();
        assert!(
            state.clipboard.is_some(),
            "Ctrl+C after clicking a file should copy it (status: {}, focused: {:?})",
            state.status,
            state.panes[0].selection
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// On Linux Ctrl+C does not arrive as `Key::C` at all: egui-winit turns it into `Event::Copy` and emits no
    /// key event. Every earlier test sent a clean `Key::C` and passed while the live app did nothing, so this
    /// pins the shape real input actually takes.
    #[test]
    fn ctrl_c_arrives_as_a_copy_event() {
        use egui_kittest::Harness;

        let dir = sandbox("keys-ctrl-char", &["one.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("keys-ctrl-char-cache", &[], &[]));
        app.settings.view_mode = ViewMode::Grid;
        let one = dir.join("one.png");

        let mut harness = Harness::new_ui_state(
            |ui, app| {
                let typing = app.begin_shortcut_frame();
                let keys = app.read_file_keys(ui, typing);
                app.pane(0, ui);
                app.apply_file_keys(&keys);
            },
            app,
        );
        harness.set_size(egui::vec2(900.0, 700.0));
        harness.state_mut().select(one.clone());

        let ctrl = egui::Modifiers {
            command: true,
            ctrl: true,
            ..Default::default()
        };
        harness.input_mut().events = vec![egui::Event::ModifiersChanged(ctrl)];
        harness.run_steps(1);
        // No `Key::C` anywhere in this frame - egui-winit hands over the dedicated event instead.
        harness.input_mut().events = vec![egui::Event::Copy];
        harness.run_steps(2);
        assert_eq!(
            harness
                .state()
                .clipboard
                .as_ref()
                .map(|(paths, cut)| (paths.len(), *cut)),
            Some((1, false)),
            "the control character for Ctrl+C copies the selection (status: {})",
            harness.state().status
        );

        // And with nothing selected, the shortcut says so rather than passing for dead.
        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Shortcuts belong to the browser, not to whichever widget egui happens to have focused: egui takes
    /// focus for its own widgets, so clicking a button or a row used to silence Ctrl+C/X/V. Only our text
    /// fields may switch them off - and while one holds the keyboard, letters must reach the field alone.
    #[test]
    fn only_our_text_fields_switch_the_shortcuts_off() {
        use egui_kittest::Harness;

        let dir = sandbox("keys-focus", &["one.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("keys-focus-cache", &[], &[]));
        app.settings.view_mode = ViewMode::Grid;
        let one = dir.join("one.png");

        let mut harness = Harness::new_ui_state(
            |ui, app| {
                let typing = app.begin_shortcut_frame();
                let keys = app.read_file_keys(ui, typing);
                app.pane(0, ui);
                app.rename_window(ui);
                app.apply_file_keys(&keys);
            },
            app,
        );
        harness.set_size(egui::vec2(900.0, 700.0));
        harness.state_mut().select(one.clone());

        let ctrl = egui::Modifiers {
            command: true,
            ctrl: true,
            ..Default::default()
        };
        let cut_key = vec![egui::Event::Key {
            key: egui::Key::X,
            physical_key: Some(egui::Key::X),
            pressed: true,
            repeat: false,
            modifiers: ctrl,
        }];

        // A widget that is not ours holds the keyboard: the shortcut must still fire.
        harness
            .ctx
            .memory_mut(|m| m.request_focus(egui::Id::new("a-toolbar-button")));
        harness.input_mut().events = vec![egui::Event::ModifiersChanged(ctrl)];
        harness.run_steps(1);
        harness.input_mut().events = cut_key.clone();
        harness.run_steps(2);
        assert_eq!(
            harness.state().clipboard.as_ref().map(|c| c.1),
            Some(true),
            "Ctrl+X cuts with an unrelated widget focused (status: {})",
            harness.state().status
        );

        // One of ours holds it: the rename box's text field keeps the keystroke to itself. The gate is armed
        // by that field's own focus, frame after frame - which is the wiring this phase pins (a flag set once
        // by hand would not survive a frame, and that is exactly what the old one-frame gate did).
        harness.state_mut().clipboard = None;
        harness.state_mut().start_rename();
        harness.run_steps(3);
        assert!(
            harness.state().typing_in_field,
            "the rename box's text field claims the keyboard while it holds focus"
        );
        harness.input_mut().events = cut_key;
        harness.run_steps(2);
        assert!(
            harness.state().clipboard.is_none(),
            "Ctrl+X while typing belongs to the field, not the browser"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Ctrl+V only reaches the app when the operating system's clipboard holds text, so a copy or cut has to
    /// publish the files there - otherwise the keystroke produces no event at all and keyboard paste stays dead.
    #[test]
    fn copied_paths_are_published_to_the_operating_system_clipboard() {
        let dir = sandbox("clip-os", &["one.png", "two.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("clip-os-cache", &[], &[]));

        app.select_in(0, dir.join("one.png"), false);
        app.copy_selection(false);
        let text = app
            .clipboard_publish
            .clone()
            .expect("a copy publishes something for Ctrl+V to arrive on");
        assert_eq!(
            text,
            format!("file://{}", dir.join("one.png").display()),
            "one file, one `file:` URI - a bare path pastes as text in a file manager"
        );

        app.select_in(0, dir.join("two.png"), true);
        app.copy_selection(true);
        let text = app.clipboard_publish.clone().expect("a cut publishes too");
        assert_eq!(
            text.lines().count(),
            2,
            "both selected files are published, one per line: {text:?}"
        );
        assert!(
            text.lines().all(|line| line.starts_with("file://")),
            "every line is a URI: {text:?}"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// A Delete that arrives in the same batch as the click focusing one of our fields must not reach the
    /// browser - it would trash the file the user was busy naming.
    #[test]
    fn a_key_with_our_field_focused_does_not_reach_the_browser() {
        use egui_kittest::Harness;

        let dir = sandbox("keys-field-focus", &["one.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("keys-field-focus-cache", &[], &[]));
        app.settings.view_mode = ViewMode::Grid;
        let one = dir.join("one.png");

        let mut harness = Harness::new_ui_state(
            |ui, app| {
                let typing = app.begin_shortcut_frame();
                let keys = app.read_file_keys(ui, typing);
                app.pane(0, ui);
                app.apply_file_keys(&keys);
            },
            app,
        );
        harness.set_size(egui::vec2(900.0, 700.0));
        harness.state_mut().select(one.clone());

        // The field's id is known from the last render and its focus lands in this same batch.
        let field = egui::Id::new("the-annotation-name-field");
        harness.state_mut().annotate_field_id = Some(field);
        harness.ctx.memory_mut(|m| m.request_focus(field));
        harness.input_mut().events = vec![egui::Event::Key {
            key: egui::Key::Delete,
            physical_key: Some(egui::Key::Delete),
            pressed: true,
            repeat: false,
            modifiers: egui::Modifiers::default(),
        }];
        harness.run_steps(2);
        let state = harness.state();
        assert!(
            !state.ops.busy() && state.panes[0].selection == vec![one.clone()],
            "Delete belongs to the field, not the browser (status: {})",
            state.status
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// A shortcut with nothing to act on says so, instead of passing for a dead key.
    #[test]
    fn a_shortcut_with_nothing_to_act_on_says_so() {
        let dir = sandbox("keys-nothing", &["one.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("keys-nothing-cache", &[], &[]));

        app.paste();
        assert!(
            app.status.contains("Nothing copied in this app"),
            "Ctrl+V with nothing staged here is named: {}",
            app.status
        );
        app.copy_selection(false);
        assert!(
            app.status.contains("Nothing selected"),
            "Ctrl+C with no selection is named: {}",
            app.status
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Cutting and pasting back into the folder the file came from is nothing to do - and must not read as a
    /// failed paste.
    #[test]
    fn cutting_pasting_into_the_same_folder_says_so() {
        let dir = sandbox("paste-home", &["one.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("paste-home-cache", &[], &[]));

        app.select(dir.join("one.png"));
        app.copy_selection(true);
        app.paste();
        assert!(
            app.status.contains("Already in this folder"),
            "the no-op is named, not reported as an error: {}",
            app.status
        );
        assert!(
            app.clipboard.is_some(),
            "the cut stays available for pasting somewhere else"
        );
        assert!(
            dir.join("one.png").is_file() && !dir.join("one (1).png").exists(),
            "nothing was copied or moved"
        );

        // Pasting into a different folder still moves it.
        let other = sandbox("paste-abroad", &[], &[]);
        app.navigate(0, &other);
        app.paste();
        settle(&mut app, &other, "the pasted file", true);
        assert!(other.join("one.png").is_file(), "the cut moved abroad");

        let _ = std::fs::remove_dir_all(&dir);
        let _ = std::fs::remove_dir_all(&other);
    }

    /// An unrelated operation must not spend a cut, and the paste that carries every source must.
    #[test]
    fn only_the_move_that_carries_a_source_spends_the_clipboard() {
        let dir = sandbox("clip-unrelated", &["up.png"], &[]);
        let other = sandbox("clip-destination", &[], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("clip-cache", &[], &[]));

        app.select(dir.join("up.png"));
        app.copy_selection(true);
        assert!(app.clipboard.is_some(), "the cut is on the clipboard");

        // A clean operation that moves none of our sources: a new folder in the same pane.
        app.new_folder();
        let mut spun = 0;
        while !dir.join("New Folder").is_dir() && spun < 250 {
            app.poll_ops();
            std::thread::sleep(std::time::Duration::from_millis(20));
            spun += 1;
        }
        app.poll_ops();
        assert!(
            app.clipboard.is_some(),
            "creating a folder did not spend the cut"
        );

        // Paste into another folder: the move carries the source, and only then is the clipboard spent.
        app.navigate(0, &other);
        app.paste();
        let mut spun = 0;
        while !other.join("up.png").is_file() && spun < 250 {
            app.poll_ops();
            std::thread::sleep(std::time::Duration::from_millis(20));
            spun += 1;
        }
        app.poll_ops();
        assert!(other.join("up.png").is_file(), "the cut file moved");
        assert!(
            app.clipboard.is_none() && app.cut_pending.is_none(),
            "the paste that carried the source spent the cut (status: {})",
            app.status
        );

        let _ = std::fs::remove_dir_all(&dir);
        let _ = std::fs::remove_dir_all(&other);
    }

    /// Taking the primary away must not leave its picture or its player standing in for the file that
    /// remains selected.
    #[test]
    fn ctrl_removing_the_primary_releases_its_player() {
        let fixture =
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/scrub.mp4");
        let dir = sandbox("primary-gone", &["still.png"], &[]);
        let clip = dir.join("clip.mp4");
        std::fs::copy(&fixture, &clip).expect("the scrub fixture should be copyable");
        let still = dir.join("still.png");
        let mut app = App::with_cache(dir.clone(), sandbox("primary-cache", &[], &[]));

        app.select(still.clone());
        app.select_in(0, clip.clone(), true);
        assert_eq!(
            app.target().as_deref(),
            Some(clip.as_path()),
            "the last clicked file is primary"
        );
        assert_eq!(
            app.playing.as_deref(),
            Some(clip.as_path()),
            "selecting a clip loads it in the player"
        );

        // Ctrl-click the clip away: the still is primary again and nothing of the clip may remain.
        app.select_in(0, clip.clone(), true);
        assert_eq!(
            app.target().as_deref(),
            Some(still.as_path()),
            "the remaining file is primary"
        );
        assert!(
            app.playing.is_some(),
            "selecting a still neither starts nor stops playback"
        );
        assert!(
            app.frame.is_none() && !app.preview_shows_frame(),
            "the departed clip's frame no longer stands in for the selected file"
        );
        assert_ne!(
            app.panes[0].wanted.as_deref(),
            Some(clip.as_path()),
            "no preview request for the file that left the selection"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Clicking replaces the selection, Ctrl-click adds to it or takes one away, and the last file clicked
    /// is the primary. Operations follow the whole set, which is what "two clips selected and pasted moves
    /// both" means in the plan's acceptance line.
    #[test]
    fn ctrl_click_extends_the_selection_and_operations_follow_it() {
        let dir = sandbox("multi", &["a.png", "b.png", "c.png"], &[]);
        let (a, b, c) = (dir.join("a.png"), dir.join("b.png"), dir.join("c.png"));
        let mut app = App::with_cache(dir.clone(), sandbox("multi-cache", &[], &[]));

        app.select(a.clone());
        app.select_in(0, b.clone(), true);
        assert_eq!(
            app.targets(),
            vec![a.clone(), b.clone()],
            "Ctrl-click adds to the selection"
        );
        assert_eq!(
            app.target().as_deref(),
            Some(b.as_path()),
            "the last file clicked is the primary"
        );

        app.select_in(0, a.clone(), true);
        assert_eq!(
            app.targets(),
            vec![b.clone()],
            "Ctrl-clicking a selected file takes it away"
        );

        app.select(c.clone());
        assert_eq!(
            app.targets(),
            vec![c.clone()],
            "a plain click replaces the selection"
        );

        // Copy and paste carry every selected file, not just the primary.
        app.select(a.clone());
        app.select_in(0, b.clone(), true);
        app.copy_selection(false);
        assert_eq!(
            app.clipboard.as_ref().map(|(paths, _)| paths.len()),
            Some(2),
            "the clipboard carries every selected file"
        );
        app.paste();
        assert!(
            settle(&mut app, &dir, "a (1).png", true),
            "the first copy landed (status: {})",
            app.status
        );
        assert!(
            dir.join("b (1).png").is_file(),
            "the second selected file was copied too"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Qt selects the right-clicked file before opening its menu, so the operations act on what was clicked
    /// rather than on whatever happened to be selected. That selection is the part this app can prove.
    #[test]
    fn right_clicking_an_unselected_cell_selects_it_first() {
        use egui_kittest::Harness;

        let dir = sandbox("menu-click", &["one.png", "two.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("menu-click-cache", &[], &[]));
        app.settings.view_mode = ViewMode::Grid;
        let two = dir.join("two.png");

        let mut harness = Harness::new_ui_state(|ui, app| app.pane(0, ui), app);
        harness.set_size(egui::vec2(900.0, 700.0));

        // Select the first cell plainly, so the second is right-clicked while unselected.
        let mut pos: Option<egui::Pos2> = None;
        'find: for row in 0..8 {
            for col in 0..16 {
                let candidate = egui::pos2(120.0 + col as f32 * 45.0, 60.0 + row as f32 * 35.0);
                harness.input_mut().events = vec![egui::Event::PointerMoved(candidate)];
                harness.run_steps(2);
                if harness.state().panes[0].hover.as_deref() == Some(two.as_path()) {
                    pos = Some(candidate);
                    break 'find;
                }
            }
        }
        let pos = pos.expect("resting on a thumbnail should hover that entry");

        let button = |pressed: bool| {
            vec![egui::Event::PointerButton {
                pos,
                button: egui::PointerButton::Secondary,
                pressed,
                modifiers: Default::default(),
            }]
        };
        harness.input_mut().events = button(true);
        harness.run_steps(1);
        harness.input_mut().events = button(false);
        harness.run_steps(1);

        assert_eq!(
            harness.state().panes[0].selection,
            vec![two.clone()],
            "right-click selects the clicked file before its menu can act on it"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// A Ctrl-click must extend the selection in the grid, and only a real click through egui's input
    /// pipeline can prove the cell answers to the modifier.
    #[test]
    fn ctrl_clicking_a_second_cell_selects_both() {
        use egui_kittest::Harness;

        let dir = sandbox("multi-cell", &["one.png", "two.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("multi-cell-cache", &[], &[]));
        app.settings.view_mode = ViewMode::Grid;
        let (one, two) = (dir.join("one.png"), dir.join("two.png"));

        let mut harness = Harness::new_ui_state(|ui, app| app.pane(0, ui), app);
        harness.set_size(egui::vec2(900.0, 700.0));

        // Cells are found by hovering them: the sweep is how the neighbouring test locates a cell too.
        let mut first: Option<egui::Pos2> = None;
        'find_one: for row in 0..8 {
            for col in 0..16 {
                let pos = egui::pos2(120.0 + col as f32 * 45.0, 60.0 + row as f32 * 35.0);
                harness.input_mut().events = vec![egui::Event::PointerMoved(pos)];
                harness.run_steps(2);
                if harness.state().panes[0].hover.as_deref() == Some(one.as_path()) {
                    first = Some(pos);
                    break 'find_one;
                }
            }
        }
        let one_pos = first.unwrap_or_else(|| {
            panic!(
                "resting on the first thumbnail should hover it, selection: {:?}",
                harness.state().panes[0].selection
            )
        });

        let press = |pos: egui::Pos2, ctrl: bool, pressed: bool| {
            let mut modifiers = egui::Modifiers::default();
            if ctrl {
                modifiers.command = true;
            }
            vec![egui::Event::PointerButton {
                pos,
                button: egui::PointerButton::Primary,
                pressed,
                modifiers,
            }]
        };
        // A click is a press and a release; sending the press twice leaves egui with an open drag.
        harness.input_mut().events = press(one_pos, false, true);
        harness.run_steps(1);
        harness.input_mut().events = press(one_pos, false, false);
        harness.run_steps(1);
        assert_eq!(
            harness.state().panes[0].selection,
            vec![one.clone()],
            "a plain click selects one cell"
        );

        // Find the second cell, then Ctrl-click it.
        let mut second: Option<egui::Pos2> = None;
        'find_two: for row in 0..8 {
            for col in 0..16 {
                let pos = egui::pos2(120.0 + col as f32 * 45.0, 60.0 + row as f32 * 35.0);
                harness.input_mut().events = vec![egui::Event::PointerMoved(pos)];
                harness.run_steps(2);
                if harness.state().panes[0].hover.as_deref() == Some(two.as_path()) {
                    second = Some(pos);
                    break 'find_two;
                }
            }
        }
        let two_pos = second.unwrap_or_else(|| {
            panic!(
                "resting on the second thumbnail should hover it, selection: {:?}",
                harness.state().panes[0].selection
            )
        });
        // The pane reads the *held* modifier state, which egui only updates from a `ModifiersChanged`
        // event (what winit sends when a real Control goes down) - carrying it on the click alone is not
        // enough. So the test presses the modifier, clicks, and releases it.
        let ctrl_mods = egui::Modifiers {
            command: true,
            ..Default::default()
        };
        harness.input_mut().events = vec![egui::Event::ModifiersChanged(ctrl_mods)];
        harness.run_steps(1);
        harness.input_mut().events = press(two_pos, true, true);
        harness.run_steps(1);
        harness.input_mut().events = press(two_pos, true, false);
        harness.run_steps(1);
        harness.input_mut().events =
            vec![egui::Event::ModifiersChanged(egui::Modifiers::default())];
        harness.run_steps(1);

        assert_eq!(
            harness.state().panes[0].selection,
            vec![one.clone(), two.clone()],
            "Ctrl-clicking the second cell keeps the first selected"
        );
        assert_eq!(
            harness.state().panes[0].primary(),
            Some(two.as_path()),
            "the Ctrl-clicked cell becomes the primary"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// The cell, not just its label, must answer to the pointer: resting on the thumbnail hovers the entry
    /// (that is what drives hover scrub) and clicking it selects. This layout maths has been wrong twice, so
    /// the test drives the real pane through egui's own input pipeline instead of trusting the arithmetic.
    #[test]
    fn resting_and_clicking_on_a_cell_targets_the_entry() {
        use egui_kittest::Harness;

        let dir = sandbox("cell", &["clip.mov"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("cell-cache", &[], &[]));
        app.settings.view_mode = ViewMode::Grid;
        let clip = dir.join("clip.mov");

        let mut harness = Harness::new_ui_state(|ui, app| app.pane(0, ui), app);
        harness.set_size(egui::vec2(900.0, 700.0));

        // Sweep the pointer until something is hovered; scanning top-down means the first hit is in the
        // picture band, not under the name label.
        let mut hit: Option<egui::Pos2> = None;
        'sweep: for row in 0..8 {
            for col in 0..16 {
                let pos = egui::pos2(120.0 + col as f32 * 45.0, 60.0 + row as f32 * 35.0);
                harness.input_mut().events = vec![egui::Event::PointerMoved(pos)];
                harness.run_steps(2);
                if harness.state().panes[0].hover.as_deref() == Some(clip.as_path()) {
                    hit = Some(pos);
                    break 'sweep;
                }
            }
        }
        let pos = hit.unwrap_or_else(|| {
            panic!(
                "resting the pointer on the thumbnail should hover the entry, selected: {:?}",
                harness.state().panes[0].selection
            )
        });

        let press = |pressed: bool| {
            vec![egui::Event::PointerButton {
                pos,
                button: egui::PointerButton::Primary,
                pressed,
                modifiers: Default::default(),
            }]
        };
        harness.input_mut().events = press(true);
        harness.run_steps(1);
        harness.input_mut().events = press(false);
        harness.run_steps(1);

        assert_eq!(
            harness.state().panes[0].primary(),
            Some(clip.as_path()),
            "clicking the thumbnail should select the entry"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    fn settle(app: &mut App, dir: &Path, name: &str, present: bool) -> bool {
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
        loop {
            for event in app.ops.poll() {
                if let OpEvent::Finished(_) = event {
                    return dir.join(name).exists() == present;
                }
            }
            if std::time::Instant::now() > deadline {
                return false;
            }
            std::thread::sleep(std::time::Duration::from_millis(20));
        }
    }

    /// Copy, paste and undo go through the engine worker: a paste makes a numbered copy (never an
    /// overwrite) and undo removes it again.
    #[test]
    fn copy_paste_and_undo_run_through_the_worker() {
        let dir = sandbox("ops-copy", &["up.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("ops-copy-cache", &[], &[]));

        app.select(dir.join("up.png"));
        assert_eq!(app.target().as_deref(), Some(dir.join("up.png").as_path()));
        app.copy_selection(false);
        assert_eq!(
            app.clipboard.as_ref().map(|(p, cut)| (p.clone(), *cut)),
            Some((vec![dir.join("up.png")], false)),
            "the selection is remembered as a copy"
        );

        app.paste();
        assert!(
            settle(&mut app, &dir, "up (1).png", true),
            "paste made a numbered copy (status: {})",
            app.status
        );
        app.ops.undo();
        assert!(
            settle(&mut app, &dir, "up (1).png", false),
            "undo removed the copy"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Delete sends the selection to the OS trash and undo restores it; rename moves the file in place.
    #[test]
    fn trash_undo_and_rename_are_reversible() {
        let dir = sandbox("ops-trash", &["gone.png"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("ops-trash-cache", &[], &[]));

        app.select(dir.join("gone.png"));
        app.trash_selection();
        assert!(
            settle(&mut app, &dir, "gone.png", false),
            "the file left the folder (status: {})",
            app.status
        );
        app.ops.undo();
        assert!(
            settle(&mut app, &dir, "gone.png", true),
            "undo restored it from trash"
        );

        app.rename_buffer = "renamed.png".into();
        app.apply_rename();
        assert!(
            settle(&mut app, &dir, "renamed.png", true),
            "the file was renamed in place (status: {})",
            app.status
        );
        assert!(!dir.join("gone.png").exists(), "the old name is gone");

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Back must not walk into a folder that an undo, a delete or a move has since removed.
    #[test]
    fn back_skips_folders_that_no_longer_exist() {
        let dir = sandbox("back-dead", &[], &["sub"]);
        let mut app = App::with_cache(dir.join("sub"), sandbox("back-cache", &[], &[]));

        app.navigate(0, &dir);
        assert_eq!(
            app.panes[0].back_stack.last(),
            Some(&dir.join("sub")),
            "the folder we left is on the history"
        );
        std::fs::remove_dir_all(dir.join("sub")).expect("remove the visited folder");

        app.go_back(0);
        assert!(
            app.panes[0].back_stack.is_empty(),
            "the dead entry was dropped rather than followed"
        );
        assert_eq!(
            app.panes[0].path, dir,
            "Back stayed put instead of opening a folder that is gone"
        );
        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Backspace is "Back to Parent" (Qt's shortcut table), and it belongs to the active pane only.
    #[test]
    fn backspace_goes_to_the_parent_of_the_active_pane() {
        let dir = sandbox("keys-up", &[], &["sub"]);
        let mut app = App::with_cache(dir.join("sub"), sandbox("keys-cache", &[], &[]));

        app.apply_file_keys(&FileKeys {
            up_parent: true,
            ..FileKeys::default()
        });
        assert_eq!(
            app.panes[0].path, dir,
            "Backspace went to the parent folder"
        );
        assert_eq!(
            app.panes[1].path,
            dir.join("sub"),
            "the other pane did not move"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn pressed_within_needs_a_real_rect_and_a_press() {
        let rect = egui::Rect::from_min_max(egui::Pos2::ZERO, egui::pos2(10.0, 10.0));
        assert!(pressed_within(rect, true, Some(egui::pos2(4.0, 4.0))));
        assert!(
            !pressed_within(rect, false, Some(egui::pos2(4.0, 4.0))),
            "a hover is not a press"
        );
        assert!(
            !pressed_within(rect, true, None),
            "a pointer outside the window presses nothing"
        );
        assert!(
            !pressed_within(
                egui::Rect::from_min_max(egui::Pos2::ZERO, egui::Pos2::ZERO),
                true,
                Some(egui::Pos2::ZERO)
            ),
            "an empty rect claims no press"
        );
    }

    /// The tree cannot be rooted at anything the platform does not have.
    #[test]
    fn drive_roots_are_real_folders() {
        let roots = drive_roots();
        assert!(!roots.is_empty(), "there is at least one folder to browse");
        for root in &roots {
            assert!(root.is_dir(), "{} is a folder", root.display());
        }
    }

    #[cfg(target_os = "linux")]
    #[test]
    fn mount_point_escapes_are_decoded() {
        assert_eq!(unmount_escape("/mnt/My\\040Drive"), "/mnt/My Drive");
        assert_eq!(unmount_escape("/data/share\\011two"), "/data/share\ttwo");
        assert_eq!(unmount_escape("/plain/path"), "/plain/path");
    }

    /// The cached tree listing must not outlive an operation that changed the folder. The toolbar's New
    /// Folder never passes through the tree menu, so the operation itself has to be what invalidates it.
    #[test]
    fn operations_invalidate_the_cached_tree_listing() {
        let dir = sandbox("tree-stale", &[], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("tree-stale-cache", &[], &[]));

        assert!(
            app.child_dirs(&dir).is_empty() && app.tree.kids.contains_key(&dir),
            "the empty listing is read and held"
        );

        app.new_folder();
        // Drive the frame's own path rather than `settle`'s raw worker poll: the tree is invalidated when
        // the operation reports back to the app, and that report is what the test is about.
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
        while app.tree.kids.contains_key(&dir) && std::time::Instant::now() < deadline {
            app.poll_ops();
            std::thread::sleep(std::time::Duration::from_millis(20));
        }
        assert!(
            dir.join("New Folder").is_dir(),
            "the folder was created (status: {})",
            app.status
        );
        // The pane refreshes the folder it is on as soon as the operation reports back, which re-reads the
        // same listing the tree had cached - so what has to be true is that nothing stale survives.
        assert!(
            app.tree.kids.contains_key(&dir),
            "the cache holds a freshly read listing rather than the stale one"
        );
        assert_eq!(
            app.child_dirs(&dir),
            vec![dir.join("New Folder")],
            "the tree now sees the new folder"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// The tree shows folders, sorted, and never files - hidden ones follow the setting.
    #[test]
    fn tree_children_are_sorted_folders_only() {
        let dir = sandbox(
            "tree-kids",
            &["a.png", "b.mov"],
            &["zz-dir", "aa-dir", ".hidden"],
        );
        let mut app = App::with_cache(dir.clone(), sandbox("tree-cache", &[], &[]));

        let kids = app.child_dirs(&dir);
        assert_eq!(
            kids,
            vec![dir.join("aa-dir"), dir.join("zz-dir")],
            "folders only, sorted, and the hidden one left out while the setting is off"
        );

        // The listing is cached, so a folder created afterwards stays invisible until it is refreshed.
        std::fs::create_dir_all(dir.join("later")).expect("create a folder after the listing");
        assert!(
            !app.child_dirs(&dir).contains(&dir.join("later")),
            "the cached listing is reused"
        );
        app.refresh_tree(&dir);
        assert!(
            app.child_dirs(&dir).contains(&dir.join("later")),
            "a refresh re-reads the folder"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// A favorite is added once and can be taken away again.
    #[test]
    fn favorites_are_added_once_and_removed() {
        let dir = sandbox("favs", &[], &["keep"]);
        let mut app = App::with_cache(dir.clone(), sandbox("favs-cache", &[], &[]));

        app.add_favorite(&dir.join("keep"));
        app.add_favorite(&dir.join("keep"));
        assert_eq!(
            app.settings.favorites,
            vec![dir.join("keep")],
            "the same folder is not listed twice"
        );
        assert!(app.is_favorite(&dir.join("keep")));

        app.remove_favorite(&dir.join("keep"));
        assert!(app.settings.favorites.is_empty(), "it can be taken away");

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Clicking a favorite moves the pane that has the keyboard, not both panes.
    #[test]
    fn opening_a_favorite_moves_the_active_pane() {
        let dir = sandbox("fav-nav", &[], &["here", "there"]);
        let mut app = App::with_cache(dir.join("here"), sandbox("fav-nav-cache", &[], &[]));
        app.settings.dual_pane = true;
        app.open(1);

        app.open_in_active(&dir.join("there"));
        assert_eq!(
            app.panes[0].path,
            dir.join("there"),
            "the active pane went there"
        );
        assert_eq!(
            app.panes[1].path,
            dir.join("here"),
            "the other pane stayed put"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// The pane toolbars must act on the pane that owns them, not on whichever pane happened to open first.
    #[test]
    fn toolbar_navigation_and_new_folder_follow_the_pane_that_owns_them() {
        let dir = sandbox("toolbar-pane", &["up.png"], &["sub"]);
        let mut app = App::with_cache(dir.clone(), sandbox("toolbar-cache", &[], &[]));

        // Back walks the folders this pane visited, and it never drags the other pane along.
        app.navigate(0, &dir.join("sub"));
        assert_eq!(app.panes[0].path, dir.join("sub"), "pane 1 moved into sub");
        assert_eq!(app.panes[1].path, dir, "pane 2 stayed put");
        app.go_back(0);
        assert_eq!(
            app.panes[0].path, dir,
            "Back returned the pane to where it came from"
        );

        // New Folder belongs to the active pane: pane 2 is in `sub`, so that is where the folder lands.
        app.navigate(1, &dir.join("sub"));
        assert_eq!(
            app.active, 1,
            "navigating a pane makes it the one buttons and shortcuts act on"
        );
        app.new_folder();
        assert!(
            settle(&mut app, &dir.join("sub"), "New Folder", true),
            "the active pane's folder appeared (status: {})",
            app.status
        );
        assert!(
            !dir.join("New Folder").exists(),
            "the other pane did not get the folder"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// The second pane is independent: navigating it never moves the first, and enabling it re-scans it.
    #[test]
    fn panes_are_independent() {
        let a = sandbox("pane-a", &["a.png"], &[]);
        let b = sandbox("pane-b", &["b.mov", "c.jpg"], &[]);
        let mut app = App::with_cache(a.clone(), sandbox("pane-cache", &[], &[]));

        app.set_dual_pane(true);
        assert!(
            app.settings.dual_pane,
            "the setting drives how many panes exist"
        );
        assert_eq!(
            app.panes[1].entries.len(),
            1,
            "the second pane starts at the same folder"
        );

        app.navigate(1, &b);
        assert_eq!(app.panes[1].path, b);
        assert_eq!(
            app.panes[1].entries.len(),
            2,
            "the second pane re-scanned its own folder"
        );
        assert_eq!(app.panes[0].path, a, "the first pane did not move");
        assert_eq!(app.panes[0].entries.len(), 1);
        assert!(matches!(app.active, 1), "navigating a pane makes it active");

        let _ = std::fs::remove_dir_all(&a);
        let _ = std::fs::remove_dir_all(&b);
    }
}
