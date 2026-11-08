//! The egui application shell. Slice 3 of M1: browse a real folder — breadcrumb, subfolder list, media
//! entries from [`gallerybrowser_core::scan_folder`], and a preview panel fed by the engine's
//! [`gallerybrowser_media::Thumbnailer`] (decode on its own threads, results polled into wgpu textures).

use std::path::{Path, PathBuf};

use gallerybrowser_core::{
    Entry, ScanOptions, SortKey, list_subfolders, scan_folder, sort_entries,
};
use gallerybrowser_media::{Thumbnailer, ThumbnailerConfig};

/// Longest preview edge we ask the decoders for (they may scale down while decoding).
const PREVIEW_EDGE: u32 = 1024;

/// Folders we never offer to open, matching the Qt app's habit of not crawling system noise.
const SCAN: ScanOptions = ScanOptions {
    show_hidden: false,
    collapse_sequences: true,
    min_sequence_len: 2,
    show_all_files: false,
};

/// One `App` per window: current folder, what it contains, the selection and its preview picture.
pub struct App {
    path: PathBuf,
    entries: Vec<Entry>,
    subfolders: Vec<PathBuf>,
    selected: Option<PathBuf>,
    status: String,
    thumbs: Thumbnailer,
    /// The entry whose preview we asked the engine for.
    wanted: Option<PathBuf>,
    /// Decoded picture, uploaded to wgpu: `(path, texture)`.
    preview: Option<(PathBuf, egui::TextureHandle)>,
}

impl App {
    pub fn new(path: PathBuf) -> Self {
        let cache_dir = gallerybrowser_core::AppDirs::resolve()
            .map(|dirs| dirs.thumbnails_dir())
            .unwrap_or_default();
        Self::with_cache(path, cache_dir)
    }

    /// `cache_dir` is injected so tests keep their artifacts inside the project temp dir.
    pub fn with_cache(path: PathBuf, cache_dir: PathBuf) -> Self {
        let thumbs = Thumbnailer::new(ThumbnailerConfig {
            cache_dir,
            memory_bytes: 32 << 20,
            // Engine defaults from `Settings`: 1000 MB disk cache, 2 decoder threads.
            disk_bytes: 1_000 << 20,
            threads: 2,
        });
        let mut app = Self {
            path,
            entries: Vec::new(),
            subfolders: Vec::new(),
            selected: None,
            status: String::new(),
            thumbs,
            wanted: None,
            preview: None,
        };
        app.open();
        app
    }

    /// Re-read the current folder. Called on every navigation so the view is always what the engine sees.
    fn open(&mut self) {
        match scan_folder(&self.path, &SCAN) {
            Ok(mut entries) => {
                sort_entries(&mut entries, SortKey::Name, true);
                self.entries = entries;
                self.status.clear();
            }
            Err(err) => {
                self.entries.clear();
                self.status = format!("{err}");
            }
        }
        self.subfolders = list_subfolders(&self.path, SCAN.show_hidden).unwrap_or_default();
        self.selected = None;
        self.wanted = None;
        self.preview = None;
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

    /// Move finished previews from the engine into wgpu textures. Called every frame.
    fn poll_previews(&mut self, ui: &mut egui::Ui) {
        for result in self.thumbs.poll() {
            if result.scrub.is_some() || Some(&result.path) != self.wanted.as_ref() {
                continue;
            }
            if let Ok(image) = result.image {
                let color = egui::ColorImage::from_rgba_unmultiplied(
                    [image.width as usize, image.height as usize],
                    image.pixels.as_ref(),
                );
                let texture = ui
                    .ctx()
                    .load_texture("preview", color, egui::TextureOptions::LINEAR);
                self.preview = Some((result.path.clone(), texture));
            }
        }
        // ponytail: repaint continuously while a preview is in flight; event-driven wakeups when the
        // engine can notify us instead of being polled.
        let ready = self
            .preview
            .as_ref()
            .is_some_and(|(path, _)| Some(path.as_path()) == self.wanted.as_deref());
        if self.wanted.is_some() && !ready {
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
        });
        if let Some(target) = target {
            self.navigate(&target);
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
        egui::ScrollArea::vertical()
            .auto_shrink(false)
            .show(ui, |ui| {
                for (name, path) in &names {
                    if ui
                        .selectable_label(self.selected.as_deref() == Some(path.as_path()), name)
                        .clicked()
                    {
                        selected = Some(path.clone());
                    }
                }
            });
        if let Some(selected) = selected {
            self.select(selected);
        }
    }

    /// The preview panel: the decoded picture of the current selection.
    pub fn preview(&mut self, ui: &mut egui::Ui) {
        ui.heading("Preview");
        ui.separator();
        let picture = self
            .preview
            .as_ref()
            .filter(|(path, _)| Some(path.as_path()) == self.wanted.as_deref());
        match picture {
            Some((_, texture)) => {
                let avail = ui.available_size();
                let pixels = texture.size_vec2();
                let scale = (avail.x / pixels.x.max(1.0))
                    .min(avail.y / pixels.y.max(1.0))
                    .min(1.0);
                ui.image(egui::load::SizedTexture {
                    id: texture.id(),
                    size: pixels * scale,
                });
            }
            None => {
                ui.label("Select a file to preview.");
            }
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
        self.poll_previews(ui);
        egui::Panel::top("path_bar").show(ui, |ui| {
            ui.horizontal(|ui| self.path_bar(ui));
        });
        egui::Panel::bottom("status_bar").show(ui, |ui| {
            ui.horizontal(|ui| self.status_bar(ui));
        });
        egui::Panel::left("folders").show(ui, |ui| self.folders(ui));
        egui::Panel::right("preview").show(ui, |ui| self.preview(ui));
        egui::CentralPanel::default().show(ui, |ui| self.entries_list(ui));
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
}
