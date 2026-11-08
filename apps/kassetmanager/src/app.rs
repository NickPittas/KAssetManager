//! The egui application shell. Slice 8 of M1: the dual-pane browser with file operations — each pane is
//! an independent folder view (breadcrumb, subfolder list, grid/list of engine thumbnails, selection and
//! preview), copy/cut/paste/trash/rename run through the engine's [`FileOpsWorker`] with undo/redo, and the
//! app owns the shared parts: the engine [`gallerybrowser_media::Thumbnailer`], one
//! [`gallerybrowser_player::Player`], settings, and the wgpu textures.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use gallerybrowser_core::{
    AppDirs, Entry, MediaKind, ScanOptions, Settings, Theme, ViewMode, list_subfolders,
    scan_folder, sort_entries,
};
use gallerybrowser_fileops::{ConflictPolicy, FileOp, FileOpsWorker, OpEvent, OpOutcome};
use gallerybrowser_library::Library;
use gallerybrowser_media::{Thumbnailer, ThumbnailerConfig, Tick, frame_to_rgba, open};
use gallerybrowser_player::Player;

/// Longest preview edge we ask the decoders for (they may scale down while decoding).
const PREVIEW_EDGE: u32 = 1024;

/// Hover scrub, with GalleryBrowser's numbers (crates/ui-egui/src/file_pane.rs:42-50): the pointer's x
/// across a clip's thumbnail picks a slot out of `SCRUB_SLOTS`, or out of `SCRUB_SLOTS_FINE` with Ctrl held
/// ("real-time" scrubbing); a decoded neighbour is shown while the exact frame is made; a frame still missing
/// after `SCRUB_WAIT` gives up. Declared before porting, frozen in tests after verification.
const SCRUB_SLOTS: u32 = 24;
const SCRUB_SLOTS_FINE: u32 = 200;
const SCRUB_NEAR: u32 = 12;
const SCRUB_WAIT: std::time::Duration = std::time::Duration::from_secs(5);

/// A run of numbered frames must reach this length before it is worth folding into one entry.
const MIN_SEQUENCE_LEN: usize = 2;

/// One pane of the browser: a folder, what it contains and its selection.
struct Pane {
    path: PathBuf,
    entries: Vec<Entry>,
    subfolders: Vec<PathBuf>,
    selected: Option<PathBuf>,
    /// The entry whose preview we asked the engine for.
    wanted: Option<PathBuf>,
    /// Decoded picture, uploaded to wgpu: `(path, texture)`.
    preview: Option<(PathBuf, egui::TextureHandle)>,
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
    rename: bool,
    /// Digit 0-5 pressed to rate the selection.
    rating: Option<u8>,
}

/// One `App` per window: two panes (the second only when `dual_pane` is on) plus the shared engine
/// plumbing.
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
    clipboard: Option<(PathBuf, bool)>,
    /// Rename box state for the active pane's selection.
    renaming: bool,
    rename_buffer: String,
    /// Set once when the rename box opens, so its text field takes the keyboard.
    rename_focus: bool,
    /// Readout of the op in flight, shown until it finishes.
    op_progress: String,
    /// A cut is spent once its move lands; this is what tells `poll_ops` to clear the clipboard.
    cut_pending: bool,
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
    settings_open: bool,
    /// Theme last pushed to egui, so we only re-set visuals when it changes.
    theme_applied: Option<Theme>,
}

impl Pane {
    fn new(path: PathBuf) -> Self {
        Self {
            path,
            entries: Vec::new(),
            subfolders: Vec::new(),
            selected: None,
            wanted: None,
            preview: None,
            hover: None,
            scrub_target: None,
            scrub_wait_until: std::time::Instant::now(),
            scrub_frames: std::collections::HashMap::new(),
            scrub_shown: None,
        }
    }
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
            cut_pending: false,
            issued: std::collections::HashSet::new(),
            stall: std::time::Instant::now(),
            library,
            annotate: None,
            annotate_focus: false,
            dirs,
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
        let opts = self.scan_options();
        let path = self.panes[index].path.clone();
        match scan_folder(&path, &opts) {
            Ok(mut entries) => {
                let (key, ascending) = (self.settings.sort_key, self.settings.sort_ascending);
                sort_entries(&mut entries, key, ascending);
                self.panes[index].entries = entries;
                self.status.clear();
            }
            Err(err) => {
                self.panes[index].entries.clear();
                self.status = format!("{err}");
            }
        }
        let show_hidden = self.settings.show_hidden;
        self.panes[index].subfolders = list_subfolders(&path, show_hidden).unwrap_or_default();
        let still_there = self.panes[index].selected.as_ref().is_some_and(|selected| {
            self.panes[index]
                .entries
                .iter()
                .any(|e| e.path == *selected)
        });
        if !still_there {
            self.panes[index].selected = None;
            self.panes[index].wanted = None;
            self.panes[index].preview = None;
        }
    }

    /// Enter `dir` in pane `index`, and make it the active pane.
    fn navigate(&mut self, index: usize, dir: &Path) {
        let dir = dir.to_path_buf();
        if dir.is_dir() {
            self.panes[index].path = dir;
            self.active = index;
            self.open(index);
        } else {
            self.status = format!("{} is not a folder", dir.display());
        }
    }

    /// Select `path` in pane `index` and ask the engine for its preview (once per selection).
    pub fn select(&mut self, path: PathBuf) {
        self.select_in(self.active, path);
    }

    fn select_in(&mut self, index: usize, path: PathBuf) {
        self.active = index;
        let pane = &mut self.panes[index];
        pane.selected = Some(path.clone());
        if pane.wanted.as_deref() == Some(path.as_path()) {
            return;
        }
        let Some(entry) = pane.entries.iter().find(|e| e.path == path).cloned() else {
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

    /// Load `path` in the player. Private: a selection previews through `preview_selection`, which honours
    /// the `video_preview` setting instead of always starting playback.
    fn load(&mut self, path: PathBuf, autoplay: bool) {
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
            Err(err) => self.status = format!("{err}"),
        }
    }

    /// A selected clip belongs in the preview panel with its timeline and buttons, so load it - paused unless
    /// `video_preview` says to play on selection (that setting's Qt meaning). Stills keep the panel as it was.
    fn preview_selection(&mut self, path: &std::path::Path) {
        let media = self.panes[self.active].entries.iter().any(|e| {
            e.path == path
                && matches!(
                    e.kind,
                    MediaKind::Video | MediaKind::Audio | MediaKind::Sequence
                )
        });
        if media {
            // Always paused: a stored "play on selection" from the Qt settings must not restage playback on
            // every click here (user rule). Autoplay on selection returns with its own UI-phase surface.
            self.load(path.to_path_buf(), false);
        }
    }

    /// Stop playback and release the file.
    pub fn stop_playback(&mut self) {
        self.player.stop();
        self.playing = None;
        self.frame = None;
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

    /// Put the second pane at `dir` (Qt restores `Settings::last_folder_secondary`).
    pub fn set_secondary(&mut self, dir: PathBuf) {
        self.panes[1].path = dir;
        if self.settings.dual_pane {
            self.open(1);
        }
    }

    // --- File operations: the engine runs them on its own thread and remembers them for undo. ------

    /// The active pane's selection — what copy, cut, delete and rename act on.
    fn target(&self) -> Option<PathBuf> {
        self.panes[self.active].selected.clone()
    }

    /// Ctrl+C / Ctrl+X: remember the selection as a copy or a cut.
    pub fn copy_selection(&mut self, cut: bool) {
        if let Some(path) = self.target() {
            let name = path
                .file_name()
                .map(|n| n.to_string_lossy().into_owned())
                .unwrap_or_default();
            self.clipboard = Some((path, cut));
            self.status = format!("{}{name}", if cut { "cut: " } else { "copied: " });
        }
    }

    /// Ctrl+V: copy or move the clipboard path into the active pane's folder. Collisions get a numbered
    /// name (`KeepBoth`), never an overwrite.
    pub fn paste(&mut self) {
        let Some((source, cut)) = self.clipboard.clone() else {
            return;
        };
        let dest_dir = self.panes[self.active].path.clone();
        self.cut_pending = cut;
        self.ops.submit(if cut {
            FileOp::Move {
                sources: vec![source],
                dest_dir,
                conflict: ConflictPolicy::KeepBoth,
            }
        } else {
            FileOp::Copy {
                sources: vec![source],
                dest_dir,
                conflict: ConflictPolicy::KeepBoth,
            }
        });
    }

    /// Delete key: send the selection to the OS trash (undoable).
    pub fn trash_selection(&mut self) {
        if let Some(path) = self.target() {
            self.ops.submit(FileOp::Trash { paths: vec![path] });
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
        // A cut is spent once its move lands; a copy stays available for another paste.
        if self.cut_pending
            && finished
                .iter()
                .any(|o| !o.moved.is_empty() && o.errors.is_empty())
        {
            self.clipboard = None;
            self.cut_pending = false;
        }
        // Only folders the op touched are re-read, and all of them share one thumbnail batch. Selections
        // survive because `open_quiet` keeps a selection whose file is still there.
        let mut rescanned = false;
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
            pane.scrub_shown = Some((entry.path.clone(), tex));
            ui.ctx().request_repaint();
            return;
        }
        if let Some(image) = self.thumbs.cached_scrub(entry, edge, slot, slots) {
            let texture = Self::upload_scrub(ui, image.as_ref(), slot, slots);
            pane.scrub_frames.insert(key.clone(), texture.clone());
            pane.scrub_shown = Some((entry.path.clone(), texture));
            ui.ctx().request_repaint();
            return;
        }

        // Nothing decoded for this slot yet: ask the engine, then show the nearest frame we do have
        // (GalleryBrowser searches SCRUB_NEAR of them). Stop asking a frame after SCRUB_WAIT - it is undecodable.
        if pane.scrub_wait_until.elapsed() >= SCRUB_WAIT {
            pane.scrub_shown = None;
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
        let Some(path) = self.target() else {
            self.status = "select a file to rate".into();
            return;
        };
        match self.library.set_rating(std::slice::from_ref(&path), rating) {
            Ok(()) => {
                let name = path
                    .file_name()
                    .map(|n| n.to_string_lossy().into_owned())
                    .unwrap_or_default();
                self.status = format!("{name} rated {rating}/5");
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
                self.pane_path_bar(index, ui);
                let height = ui.available_height();
                // ponytail: folders width is a fraction with floors; make it draggable if users want it.
                ui.horizontal(|ui| {
                    let folders_width = (ui.available_width() * 0.32)
                        .clamp(70.0, 150.0)
                        .min(ui.available_width());
                    ui.allocate_ui_with_layout(
                        egui::vec2(folders_width, height),
                        egui::Layout::top_down(egui::Align::LEFT),
                        |ui| self.pane_folders(index, ui),
                    );
                    ui.separator();
                    let entries_width = (ui.available_width() - 8.0).max(0.0);
                    ui.allocate_ui_with_layout(
                        egui::vec2(entries_width, height),
                        egui::Layout::top_down(egui::Align::LEFT),
                        |ui| match self.settings.view_mode {
                            ViewMode::Grid => self.pane_grid(index, ui),
                            ViewMode::List => self.pane_list(index, ui),
                        },
                    );
                });
            });
        });
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

    /// Subfolder list of one pane: the cheap version of a folder tree, enough to move around.
    fn pane_folders(&mut self, index: usize, ui: &mut egui::Ui) {
        let folders: Vec<(String, PathBuf)> = self.panes[index]
            .subfolders
            .iter()
            .map(|f| {
                (
                    f.file_name()
                        .map(|n| n.to_string_lossy().into_owned())
                        .unwrap_or_default(),
                    f.clone(),
                )
            })
            .collect();
        let mut target: Option<PathBuf> = None;
        egui::ScrollArea::vertical()
            .id_salt(format!("folders{index}"))
            .show(ui, |ui| {
                for (name, path) in &folders {
                    if ui.selectable_label(false, name).clicked() {
                        target = Some(path.clone());
                    }
                }
            });
        if let Some(target) = target {
            self.navigate(index, &target);
        }
    }

    /// List view of one pane. Click selects; double-click plays.
    fn pane_list(&mut self, index: usize, ui: &mut egui::Ui) {
        let names: Vec<(String, PathBuf)> = self.panes[index]
            .entries
            .iter()
            .map(|e| (e.name.clone(), e.path.clone()))
            .collect();
        let selected_now = self.panes[index].selected.clone();
        let mut selected: Option<PathBuf> = None;
        let mut play: Option<PathBuf> = None;
        let mut hovered: Option<PathBuf> = None;
        egui::ScrollArea::vertical()
            .id_salt(format!("list{index}"))
            .auto_shrink(false)
            .show(ui, |ui| {
                for (name, path) in &names {
                    let response =
                        ui.selectable_label(selected_now.as_deref() == Some(path.as_path()), name);
                    if response.double_clicked() {
                        play = Some(path.clone());
                    }
                    if response.clicked() {
                        selected = Some(path.clone());
                    }
                    if response.hovered() {
                        hovered = Some(path.clone());
                    }
                }
            });
        self.finish(index, selected, play, hovered);
    }

    /// Grid view of one pane: engine thumbnails in rows that fit the column; double-click plays.
    fn pane_grid(&mut self, index: usize, ui: &mut egui::Ui) {
        // A cell wider than its column overflows the scroll area and cuts thumbnails mid-image.
        let edge_px = self.settings.thumbnail_size.clamp(32, 512);
        let edge = (edge_px as f32).min((ui.available_width() - 12.0).max(48.0));
        let cell = edge + 12.0;
        let cells: Vec<Entry> = self.panes[index].entries.clone();
        let cols = ((ui.available_width() / cell).floor() as usize).max(1);
        let selected_now = self.panes[index].selected.clone();
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
        egui::ScrollArea::vertical()
            .id_salt(format!("grid{index}"))
            .auto_shrink(false)
            .show(ui, |ui| {
                for row in cells.chunks(cols) {
                    ui.horizontal(|ui| {
                        for entry in row {
                            let chosen = selected_now.as_deref() == Some(entry.path.as_path());
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
                                        egui::Sense::click().union(egui::Sense::hover()),
                                    );
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
                                },
                            );
                        }
                    });
                }
            });
        self.finish(index, selected, play, hovered);
        if let Some((entry, pos, thumb)) = scrub {
            self.scrub_at(ui, index, &entry, edge_px, pos, thumb, fine);
        }
    }

    /// Apply what a pane's rows reported: make it active, select, and play on double-click.
    fn finish(
        &mut self,
        index: usize,
        selected: Option<PathBuf>,
        play: Option<PathBuf>,
        hovered: Option<PathBuf>,
    ) {
        if let Some(path) = selected {
            self.select_in(index, path.clone());
            // Double-click already loads the file playing; anything else selecting it gets the transport
            // paused, so the preview panel is a real preview of what you picked.
            if play.is_none() {
                self.preview_selection(&path);
            }
        }
        if let Some(path) = play {
            self.play(path);
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

    /// The preview panel: the playing frame if anything is loaded, else the active pane's **selection**.
    /// Hover scrub stays in the grid cell - a hovered clip must not restage the preview (user report).
    pub fn preview(&mut self, ui: &mut egui::Ui) {
        ui.heading("Preview");
        ui.separator();
        let picture = self
            .frame
            .as_ref()
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
        let vol = ui.add(egui::Slider::new(&mut volume, 0.0..=1.0).text("vol"));
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
        if vol.changed() || mute.changed() || looping.changed() {
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
        if let Some(selected) = &self.panes[self.active].selected {
            ui.separator();
            ui.label(
                selected
                    .file_name()
                    .map(|n| n.to_string_lossy().into_owned())
                    .unwrap_or_default(),
            );
        }
        if let Some((path, cut)) = &self.clipboard {
            ui.separator();
            ui.label(format!(
                "{}{}",
                if *cut { "cut: " } else { "copied: " },
                path.file_name()
                    .map(|n| n.to_string_lossy().into_owned())
                    .unwrap_or_default()
            ));
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
    pub fn app_bar(&mut self, ui: &mut egui::Ui) {
        let mut view: Option<ViewMode> = None;
        let mut settings = false;
        ui.horizontal(|ui| {
            let grid = self.settings.view_mode == ViewMode::Grid;
            if ui.button(if grid { "List" } else { "Grid" }).clicked() {
                view = Some(if grid { ViewMode::List } else { ViewMode::Grid });
            }
            ui.separator();
            if ui.button("Settings").clicked() {
                settings = true;
            }
        });
        if let Some(mode) = view {
            self.set_view_mode(mode);
        }
        if settings {
            self.settings_open = !self.settings_open;
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
                ui.horizontal(|ui| {
                    ui.label("View:");
                    let grid = self.settings.view_mode == ViewMode::Grid;
                    if ui.selectable_label(grid, "Grid").clicked() {
                        self.set_view_mode(ViewMode::Grid);
                    }
                    if ui.selectable_label(!grid, "List").clicked() {
                        self.set_view_mode(ViewMode::List);
                    }
                });
                ui.horizontal(|ui| {
                    ui.label("Thumbnails:");
                    let current = self.settings.thumbnail_size;
                    for size in [64, 128, 256, 512] {
                        if ui
                            .selectable_label(current == size, size.to_string())
                            .clicked()
                        {
                            self.settings.thumbnail_size = size;
                            dirty = true;
                        }
                    }
                });
                if ui
                    .checkbox(&mut self.settings.dual_pane, "Dual pane")
                    .changed()
                {
                    let dual = self.settings.dual_pane;
                    self.set_dual_pane(dual);
                    dirty = true;
                }
                if ui
                    .checkbox(&mut self.settings.show_hidden, "Show hidden files")
                    .changed()
                {
                    rescan = true;
                    dirty = true;
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
        // Shortcuts are off while a text field holds the keyboard; egui gives that focus up on a click,
        // which also closes the rename box (see `rename_window`).
        let keys = if ui.ctx().memory(|m| m.focused().is_some()) {
            FileKeys::default()
        } else {
            ui.input(|i| {
                let m = i.modifiers.command;
                let shift = i.modifiers.shift;
                FileKeys {
                    settings: i.key_pressed(egui::Key::S) && !m,
                    copy: i.key_pressed(egui::Key::C) && m && !shift,
                    cut: i.key_pressed(egui::Key::X) && m,
                    paste: i.key_pressed(egui::Key::V) && m,
                    undo: i.key_pressed(egui::Key::Z) && m && !shift,
                    redo: i.key_pressed(egui::Key::Y) && m
                        || i.key_pressed(egui::Key::Z) && m && shift,
                    trash: i.key_pressed(egui::Key::Delete),
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
                }
            })
        };
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
        if keys.undo && self.ops.undo().is_none() {
            self.status = "nothing to undo".into();
        }
        if keys.redo && self.ops.redo().is_none() {
            self.status = "nothing to redo".into();
        }
        if keys.rename {
            self.start_rename();
        }
        egui::Panel::top("app_bar").show(ui, |ui| self.app_bar(ui));
        egui::Panel::bottom("status_bar").show(ui, |ui| {
            ui.horizontal(|ui| self.status_bar(ui));
        });
        egui::Panel::right("preview")
            .resizable(true)
            .size_range(180.0..=460.0)
            .default_size(300.0)
            .show(ui, |ui| self.preview(ui));
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
        assert_eq!(
            app.panes[0].subfolders.len(),
            1,
            "the child folder should be offered"
        );
        assert_eq!(
            app.panes[0].entries.len(),
            0,
            "a folder with no media lists nothing"
        );

        let _ = std::fs::remove_dir_all(&child);
        let _ = std::fs::remove_dir_all(&parent);
    }

    /// Selecting an entry records it and points the preview request at it.
    #[test]
    fn selecting_an_entry_points_the_preview_at_it() {
        let dir = sandbox("select", &["a.png", "b.mov"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("select-thumbs", &[], &[]));

        let image = dir.join("a.png");
        app.select(image.clone());
        assert_eq!(app.panes[0].selected.as_deref(), Some(image.as_path()));
        assert_eq!(
            app.panes[0].wanted.as_deref(),
            Some(image.as_path()),
            "the engine should be asked for this entry's preview"
        );

        let video = dir.join("b.mov");
        app.select(video.clone());
        assert_eq!(app.panes[0].selected.as_deref(), Some(video.as_path()));
        assert_eq!(
            app.panes[0].wanted.as_deref(),
            Some(video.as_path()),
            "the preview request follows the new selection"
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
                harness.state().panes[0].selected
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
            harness.state().panes[0].selected.as_deref(),
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
            Some((dir.join("up.png"), false)),
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
