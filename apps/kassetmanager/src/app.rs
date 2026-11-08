//! The egui application shell. Slice 5 of M1: browse a real folder — breadcrumb, subfolder list, entries
//! as engine thumbnails in a grid or a list, a preview panel, and playback through
//! [`gallerybrowser_player::Player`] (double-click a file; transport controls under the picture). Pictures
//! come from [`gallerybrowser_media::Thumbnailer`], which decodes on its own threads and is polled into
//! wgpu textures.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use gallerybrowser_core::{
    AppDirs, Entry, ScanOptions, Settings, Theme, ViewMode, list_subfolders, scan_folder,
    sort_entries,
};
use gallerybrowser_media::{Thumbnailer, ThumbnailerConfig, Tick, frame_to_rgba, open};
use gallerybrowser_player::Player;

/// Longest preview edge we ask the decoders for (they may scale down while decoding).
const PREVIEW_EDGE: u32 = 1024;

/// A run of numbered frames must reach this length before it is worth folding into one entry.
const MIN_SEQUENCE_LEN: usize = 2;

/// One `App` per window: current folder, what it contains, the selection and its pictures.
pub struct App {
    path: PathBuf,
    entries: Vec<Entry>,
    subfolders: Vec<PathBuf>,
    selected: Option<PathBuf>,
    status: String,
    /// Engine settings; drives view mode and thumbnail size. Dialog to edit it is later M1 work.
    settings: Settings,
    thumbs: Thumbnailer,
    /// Uploaded thumbnails of the current folder, by entry path.
    pictures: HashMap<PathBuf, egui::TextureHandle>,
    /// Requests still in flight (drives repainting until the engine answers).
    outstanding: usize,
    /// The entry whose preview we asked the engine for.
    wanted: Option<PathBuf>,
    /// Decoded picture, uploaded to wgpu: `(path, texture)`.
    preview: Option<(PathBuf, egui::TextureHandle)>,
    /// Video and audio playback of the double-clicked file.
    player: Player,
    /// The file loaded in the player.
    playing: Option<PathBuf>,
    /// Latest decoded video frame, uploaded (only while something is loaded).
    frame: Option<egui::TextureHandle>,
    /// Engine data directories; `None` in tests so nothing is written outside the project.
    dirs: Option<AppDirs>,
    settings_open: bool,
    /// Theme last pushed to egui, so we only re-set visuals when it changes.
    theme_applied: Option<Theme>,
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
        let mut app = Self {
            path,
            entries: Vec::new(),
            subfolders: Vec::new(),
            selected: None,
            status: String::new(),
            settings,
            thumbs,
            pictures: HashMap::new(),
            outstanding: 0,
            wanted: None,
            preview: None,
            // ponytail: fixed 64 MB decoder cache; `Settings` has no player-cache knob yet.
            player: Player::new(64 << 20),
            playing: None,
            frame: None,
            dirs,
            settings_open: false,
            theme_applied: None,
        };
        app.open();
        app
    }

    /// How the engine should read this folder, from the current settings.
    fn scan_options(&self) -> ScanOptions {
        ScanOptions {
            show_hidden: self.settings.show_hidden,
            collapse_sequences: self.settings.collapse_sequences,
            min_sequence_len: MIN_SEQUENCE_LEN,
            show_all_files: self.settings.show_all_files,
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

    /// Re-read the current folder. Called on every navigation so the view is always what the engine sees.
    fn open(&mut self) {
        let opts = self.scan_options();
        match scan_folder(&self.path, &opts) {
            Ok(mut entries) => {
                let (key, ascending) = (self.settings.sort_key, self.settings.sort_ascending);
                sort_entries(&mut entries, key, ascending);
                self.entries = entries;
                self.status.clear();
            }
            Err(err) => {
                self.entries.clear();
                self.status = format!("{err}");
            }
        }
        self.subfolders =
            list_subfolders(&self.path, self.settings.show_hidden).unwrap_or_default();
        self.selected = None;
        self.wanted = None;
        self.preview = None;
        // Ask the engine for a thumbnail of every entry: it caches, decodes on its own threads and
        // drops work it cannot finish. ponytail: whole folder at once; virtualise to visible rows if a
        // huge folder ever makes this feel slow.
        self.thumbs.begin_batch();
        self.outstanding = self.entries.len();
        for entry in &self.entries {
            self.thumbs.request(entry, self.settings.thumbnail_size, 0);
        }
    }

    /// Select `path` and ask the engine for its preview picture (once per selection).
    pub fn select(&mut self, path: PathBuf) {
        self.selected = Some(path.clone());
        if self.wanted.as_deref() == Some(path.as_path()) {
            return;
        }
        self.wanted = Some(path.clone());
        if let Some(entry) = self.entries.iter().find(|e| e.path == path) {
            self.thumbs.request(entry, PREVIEW_EDGE, 0);
        }
    }

    /// Move finished pictures from the engine into wgpu textures. Called every frame.
    fn poll_pictures(&mut self, ui: &mut egui::Ui) {
        for result in self.thumbs.poll() {
            if result.scrub.is_some() {
                continue;
            }
            let preview = result.max_edge >= PREVIEW_EDGE;
            if !preview && result.max_edge < self.settings.thumbnail_size {
                continue;
            }
            if preview && Some(&result.path) != self.wanted.as_ref() {
                continue;
            }
            self.outstanding = self.outstanding.saturating_sub(1);
            let Ok(image) = result.image else { continue };
            let color = egui::ColorImage::from_rgba_unmultiplied(
                [image.width as usize, image.height as usize],
                image.pixels.as_ref(),
            );
            let texture = ui.ctx().load_texture(
                format!("pic{}", result.max_edge),
                color,
                egui::TextureOptions::LINEAR,
            );
            if preview {
                self.preview = Some((result.path.clone(), texture));
            } else {
                self.pictures.insert(result.path.clone(), texture);
            }
        }
        // ponytail: repaint continuously while pictures are in flight; event-driven wakeups when the
        // engine can notify us instead of being polled.
        let preview_ready = self
            .preview
            .as_ref()
            .is_some_and(|(path, _)| Some(path.as_path()) == self.wanted.as_deref());
        if self.outstanding > 0 || (self.wanted.is_some() && !preview_ready) {
            ui.ctx().request_repaint();
        }
    }

    /// Load `path` in the player and start it (double-click, or Play when something is loaded).
    pub fn play(&mut self, path: PathBuf) {
        match open(&path) {
            Ok(source) => {
                self.playing = Some(path);
                self.frame = None;
                self.player.load(source, true);
                self.status.clear();
            }
            Err(err) => self.status = format!("{err}"),
        }
    }

    /// Stop playback and release the file.
    pub fn stop_playback(&mut self) {
        self.player.stop();
        self.playing = None;
        self.frame = None;
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

    /// Enter `dir` if it is a readable directory.
    fn navigate(&mut self, dir: &Path) {
        let dir = dir.to_path_buf();
        if dir.is_dir() {
            self.path = dir;
            self.open();
        } else {
            self.status = format!("{} is not a folder", dir.display());
        }
    }

    /// Parent of the current folder, if any.
    fn parent(&self) -> Option<&Path> {
        self.path.parent()
    }

    /// Breadcrumb + up button. Clicking a segment jumps to it.
    pub fn path_bar(&mut self, ui: &mut egui::Ui) {
        let mut ancestors: Vec<PathBuf> = self.path.ancestors().map(|a| a.to_path_buf()).collect();
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
        let mut view: Option<ViewMode> = None;
        let mut settings = false;
        ui.horizontal(|ui| {
            if ui.button("\u{2b06} up").clicked() {
                target = self.parent().map(Path::to_path_buf);
            }
            ui.separator();
            for (index, (name, path)) in segments.iter().enumerate() {
                if ui.button(name).clicked() {
                    target = Some(path.clone());
                }
                if index + 1 < segments.len() {
                    ui.label("/");
                }
            }
            ui.separator();
            let grid = self.settings.view_mode == ViewMode::Grid;
            if ui.button(if grid { "List" } else { "Grid" }).clicked() {
                view = Some(if grid { ViewMode::List } else { ViewMode::Grid });
            }
            ui.separator();
            if ui.button("Settings").clicked() {
                settings = true;
            }
        });
        if let Some(target) = target {
            self.navigate(&target);
        }
        if let Some(mode) = view {
            self.set_view_mode(mode);
        }
        if settings {
            self.settings_open = !self.settings_open;
        }
    }

    /// Theme name for the radio buttons.
    fn theme_label(theme: Theme) -> &'static str {
        match theme {
            Theme::Dark => "Dark",
            Theme::Light => "Light",
            Theme::System => "System",
        }
    }

    /// Switch between the engine's grid and list view.
    pub fn set_view_mode(&mut self, mode: ViewMode) {
        self.settings.view_mode = mode;
    }

    /// The settings window: theme, view, thumbnail size and the folder filters. Changes apply at once
    /// and are written to `AppDirs::settings_file()`.
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
            self.open();
        }
    }

    /// Subfolder list: the cheap version of the folder tree, enough to move around.
    pub fn folders(&mut self, ui: &mut egui::Ui) {
        ui.heading("Folders");
        ui.separator();
        let folders: Vec<(String, PathBuf)> = self
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
        egui::ScrollArea::vertical().show(ui, |ui| {
            for (name, path) in &folders {
                if ui.selectable_label(false, name).clicked() {
                    target = Some(path.clone());
                }
            }
        });
        if let Some(target) = target {
            self.navigate(&target);
        }
    }

    /// Media entries of the current folder. Click selects; the status bar reports the selection.
    pub fn entries_list(&mut self, ui: &mut egui::Ui) {
        let names: Vec<(String, PathBuf)> = self
            .entries
            .iter()
            .map(|e| (e.name.clone(), e.path.clone()))
            .collect();
        let mut selected: Option<PathBuf> = None;
        let mut play: Option<PathBuf> = None;
        egui::ScrollArea::vertical()
            .auto_shrink(false)
            .show(ui, |ui| {
                for (name, path) in &names {
                    let response =
                        ui.selectable_label(self.selected.as_deref() == Some(path.as_path()), name);
                    if response.double_clicked() {
                        play = Some(path.clone());
                    }
                    if response.clicked() {
                        selected = Some(path.clone());
                    }
                }
            });
        if let Some(selected) = selected {
            self.select(selected);
        }
        if let Some(path) = play {
            self.play(path);
        }
    }

    /// The preview panel: the playing frame if anything is loaded, else the selected picture; transport
    /// controls underneath. Everything the buttons need is copied out first so no borrow of `self`
    /// outlives the drawing.
    pub fn preview(&mut self, ui: &mut egui::Ui) {
        ui.heading("Preview");
        ui.separator();
        let picture = self
            .frame
            .as_ref()
            .map(|tex| (tex.id(), tex.size_vec2()))
            .or_else(|| {
                self.preview
                    .as_ref()
                    .filter(|(path, _)| Some(path.as_path()) == self.wanted.as_deref())
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

    /// Grid view: engine thumbnails in rows that fit the panel; a click selects and previews.
    pub fn entries_grid(&mut self, ui: &mut egui::Ui) {
        let edge = self.settings.thumbnail_size.clamp(32, 512) as f32;
        let cell = edge + 12.0;
        let cells: Vec<(String, PathBuf)> = self
            .entries
            .iter()
            .map(|e| (e.name.clone(), e.path.clone()))
            .collect();
        let cols = ((ui.available_width() / cell).floor() as usize).max(1);
        let mut selected: Option<PathBuf> = None;
        let mut play: Option<PathBuf> = None;
        egui::ScrollArea::vertical()
            .auto_shrink(false)
            .show(ui, |ui| {
                for row in cells.chunks(cols) {
                    ui.horizontal(|ui| {
                        for (name, path) in row {
                            let chosen = self.selected.as_deref() == Some(path.as_path());
                            ui.allocate_ui_with_layout(
                                egui::vec2(cell, cell + 24.0),
                                egui::Layout::top_down(egui::Align::Center),
                                |ui| {
                                    match self
                                        .pictures
                                        .get(path)
                                        .map(|tex| (tex.id(), tex.size_vec2()))
                                    {
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
                                    let response = ui.selectable_label(chosen, name);
                                    if response.double_clicked() {
                                        play = Some(path.clone());
                                    }
                                    if response.clicked() {
                                        selected = Some(path.clone());
                                    }
                                },
                            );
                        }
                    });
                }
            });
        if let Some(selected) = selected {
            self.select(selected);
        }
        if let Some(path) = play {
            self.play(path);
        }
    }

    /// The bottom status bar: where we are, how much is there, and what is selected.
    pub fn status_bar(&mut self, ui: &mut egui::Ui) {
        ui.label(self.path.display().to_string());
        ui.separator();
        ui.label(format!("{} entries", self.entries.len()));
        if let Some(selected) = &self.selected {
            ui.separator();
            ui.label(format!(
                "selected: {}",
                selected
                    .file_name()
                    .map(|n| n.to_string_lossy().into_owned())
                    .unwrap_or_default()
            ));
        }
        if !self.status.is_empty() {
            ui.separator();
            ui.colored_label(egui::Color32::LIGHT_RED, &self.status);
        }
    }
}

impl eframe::App for App {
    /// egui 0.36 hands the app a root `Ui` (not a `Context`); panels are shown on it.
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        self.poll_pictures(ui);
        self.pump_player(ui);
        self.apply_theme(ui.ctx());
        // ponytail: plain letter shortcuts; ignore them while a text field has focus once there is one.
        if ui.input(|i| i.key_pressed(egui::Key::S)) {
            self.settings_open = !self.settings_open;
        }
        egui::Panel::top("path_bar").show(ui, |ui| {
            ui.horizontal(|ui| self.path_bar(ui));
        });
        egui::Panel::bottom("status_bar").show(ui, |ui| {
            ui.horizontal(|ui| self.status_bar(ui));
        });
        egui::Panel::left("folders").show(ui, |ui| self.folders(ui));
        egui::Panel::right("preview").show(ui, |ui| self.preview(ui));
        egui::CentralPanel::default().show(ui, |ui| match self.settings.view_mode {
            ViewMode::Grid => self.entries_grid(ui),
            ViewMode::List => self.entries_list(ui),
        });
        if self.settings_open {
            self.settings_window(ui);
        }
    }
}

#[cfg(test)]
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

    /// Net under the verified M1 slice-2 behaviour: browsing reads media entries and folders from the
    /// engine, and navigating re-reads the new folder.
    #[test]
    fn browsing_lists_media_and_folders_then_navigation_follows() {
        let child = sandbox("child", &["a.png", "b.mov"], &[]);
        let parent = sandbox("parent", &[], &["child"]);

        let mut app = App::with_cache(child.clone(), sandbox("thumbs", &[], &[]));
        assert_eq!(app.entries.len(), 2, "both media files should be listed");
        assert!(
            app.status.is_empty(),
            "scanning the sandbox failed: {}",
            app.status
        );

        app.navigate(&parent);
        assert_eq!(app.path, parent);
        assert_eq!(
            app.subfolders.len(),
            1,
            "the child folder should be offered"
        );
        assert_eq!(app.entries.len(), 0, "a folder with no media lists nothing");

        let _ = std::fs::remove_dir_all(&child);
        let _ = std::fs::remove_dir_all(&parent);
    }

    /// Net under the verified slice-3 behaviour: selecting an entry records it and points the preview
    /// request at it. Decoding itself is the engine's problem (`gallerybrowser-media` tests cover it).
    #[test]
    fn selecting_an_entry_points_the_preview_at_it() {
        let dir = sandbox("select", &["a.png", "b.mov"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("select-thumbs", &[], &[]));

        let image = dir.join("a.png");
        app.select(image.clone());
        assert_eq!(app.selected.as_deref(), Some(image.as_path()));
        assert_eq!(
            app.wanted.as_deref(),
            Some(image.as_path()),
            "the engine should be asked for this entry's preview"
        );

        let video = dir.join("b.mov");
        app.select(video.clone());
        assert_eq!(app.selected.as_deref(), Some(video.as_path()));
        assert_eq!(app.wanted.as_deref(), Some(video.as_path()));

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Net under the verified slice-4 behaviour: opening a folder asks the engine for exactly one
    /// thumbnail per media entry, and the view mode is app state the path bar can change.
    #[test]
    fn opening_a_folder_asks_for_one_thumbnail_per_entry() {
        let dir = sandbox("thumbs", &["a.png", "b.mov", "c.txt"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("thumbs-cache", &[], &[]));

        assert_eq!(
            app.entries.len(),
            2,
            "the text file is not media and is not listed"
        );
        assert_eq!(
            app.outstanding,
            app.entries.len(),
            "one thumbnail request per entry should be outstanding"
        );
        assert!(
            matches!(app.settings.view_mode, ViewMode::Grid),
            "the engine default is the grid view"
        );

        app.set_view_mode(ViewMode::List);
        assert!(matches!(app.settings.view_mode, ViewMode::List));

        let _ = std::fs::remove_dir_all(&dir);
    }

    /// Net under the verified slice-5 behaviour: a file the engine cannot open is reported and loads
    /// nothing, a real media file loads into the player.
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

    /// Net under the verified slice-6 behaviour: the folder filters come from settings, so the settings
    /// window's checkboxes change what a scan reports.
    #[test]
    fn folder_filters_follow_settings() {
        let dir = sandbox("filters", &["a.png", ".hidden.png", "notes.txt"], &[]);
        let mut app = App::with_cache(dir.clone(), sandbox("filters-cache", &[], &[]));
        assert_eq!(
            app.entries.len(),
            1,
            "engine defaults hide hidden files and non-media files"
        );

        app.settings.show_hidden = true;
        app.settings.show_all_files = true;
        app.open();
        assert_eq!(
            app.entries.len(),
            3,
            "turning both filters on lists the hidden image and the text file too"
        );

        let _ = std::fs::remove_dir_all(&dir);
    }
}
