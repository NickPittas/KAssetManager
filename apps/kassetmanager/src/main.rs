//! Entry point. Opens the window and starts browsing at the folder given on the command line
//! (`kassetmanager [folder]`), falling back to the user's Pictures, then home.

mod app;

use std::path::PathBuf;

/// Where to start browsing: argument, then `~/Pictures`, then `$HOME`.
fn start_folder() -> PathBuf {
    if let Some(arg) = std::env::args().nth(1) {
        let path = PathBuf::from(arg);
        if path.is_dir() {
            return path;
        }
    }
    let home = std::env::var_os("HOME")
        .map(PathBuf::from)
        .unwrap_or_default();
    let pictures = home.join("Pictures");
    if pictures.is_dir() { pictures } else { home }
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
        Box::new(|_cc| Ok(Box::new(app::App::new(start_folder())))),
    );

    if let Err(err) = result {
        // No unwrap in this workspace: report and exit non-zero instead.
        eprintln!("KAsset Manager exited with an error: {err}");
        std::process::exit(1);
    }
}
