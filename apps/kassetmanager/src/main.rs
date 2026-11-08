//! Entry point. Opens the window and starts browsing at the path given on the command line
//! (`kassetmanager [folder|file]`); a file selects it in its parent folder. Falls back to `~/Pictures`,
//! then `$HOME`.

mod app;

use std::path::{Path, PathBuf};

/// Where to start browsing, and which file to select there.
fn start_point() -> (PathBuf, Option<PathBuf>) {
    if let Some(arg) = std::env::args().nth(1) {
        let path = PathBuf::from(arg);
        if path.is_dir() {
            return (path, None);
        }
        if path.is_file() {
            let folder = path.parent().map(Path::to_path_buf).unwrap_or_default();
            let folder = if folder.as_os_str().is_empty() {
                PathBuf::from(".")
            } else {
                folder
            };
            return (folder, Some(path));
        }
    }
    let home = std::env::var_os("HOME")
        .map(PathBuf::from)
        .unwrap_or_default();
    let pictures = home.join("Pictures");
    if pictures.is_dir() {
        (pictures, None)
    } else {
        (home, None)
    }
}

fn main() {
    let native_options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("KAsset Manager")
            .with_app_id("io.github.nickpittas.KAssetManager")
            .with_min_inner_size([1200.0, 800.0])
            .with_inner_size([1400.0, 900.0]),
        renderer: eframe::Renderer::Wgpu,
        ..Default::default()
    };

    let result = eframe::run_native(
        "KAsset Manager",
        native_options,
        Box::new(|_cc| {
            let (folder, file) = start_point();
            let mut app = app::App::new(folder);
            if let Some(file) = file {
                app.select(file);
            }
            Ok(Box::new(app))
        }),
    );

    if let Err(err) = result {
        // No unwrap in this workspace: report and exit non-zero instead.
        eprintln!("KAsset Manager exited with an error: {err}");
        std::process::exit(1);
    }
}
