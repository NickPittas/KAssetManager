//! The egui application shell. Slice 2 of M1: browse a real folder — breadcrumb, subfolder list and the
//! media entries that [`gallerybrowser_core::scan_folder`] reports for the current directory.

use std::path::{Path, PathBuf};

use gallerybrowser_core::{
    Entry, ScanOptions, SortKey, list_subfolders, scan_folder, sort_entries,
};

/// Folders we never offer to open, matching the Qt app's habit of not crawling system noise.
const SCAN: ScanOptions = ScanOptions {
    show_hidden: false,
    collapse_sequences: true,
    min_sequence_len: 2,
    show_all_files: false,
};

/// One `App` per window: current folder, what it contains, and the selected entry.
pub struct App {
    path: PathBuf,
    entries: Vec<Entry>,
    subfolders: Vec<PathBuf>,
    selected: Option<PathBuf>,
    status: String,
}

impl App {
    pub fn new(path: PathBuf) -> Self {
        let mut app = Self {
            path,
            entries: Vec::new(),
            subfolders: Vec::new(),
            selected: None,
            status: String::new(),
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
        if selected.is_some() {
            self.selected = selected;
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
        egui::Panel::top("path_bar").show(ui, |ui| {
            ui.horizontal(|ui| self.path_bar(ui));
        });
        egui::Panel::bottom("status_bar").show(ui, |ui| {
            ui.horizontal(|ui| self.status_bar(ui));
        });
        egui::Panel::left("folders").show(ui, |ui| self.folders(ui));
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

        let mut app = App::new(child.clone());
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
}
