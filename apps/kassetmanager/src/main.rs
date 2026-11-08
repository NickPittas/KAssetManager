//! Entry point. `kassetmanager [--theme dark|light|system] [--dual-pane] [--folder-secondary DIR]
//! [folder|file]`: a file is selected in its parent folder **and played** (so `kassetmanager clip.mov`
//! does what it looks like). With no path the app opens `~/Pictures`, then `$HOME`.

mod app;

use std::path::{Path, PathBuf};

use gallerybrowser_core::Theme;

/// Theme named on the command line, if any.
fn parse_theme(value: &str) -> Option<Theme> {
    match value.to_ascii_lowercase().as_str() {
        "dark" => Some(Theme::Dark),
        "light" => Some(Theme::Light),
        "system" => Some(Theme::System),
        _ => None,
    }
}

/// Where to start browsing, which file to select there, and any command-line overrides.
fn start_point() -> (
    Option<Theme>,
    bool,
    Option<PathBuf>,
    PathBuf,
    Option<PathBuf>,
) {
    let mut theme = None;
    let mut dual_pane = false;
    let mut secondary = None;
    let mut positional: Vec<String> = Vec::new();
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--theme" => theme = args.next().as_deref().and_then(parse_theme),
            "--dual-pane" => dual_pane = true,
            "--folder-secondary" => secondary = args.next().map(PathBuf::from),
            _ => {
                if !arg.starts_with('-') {
                    positional.push(arg);
                }
            }
        }
    }

    let home = std::env::var_os("HOME")
        .map(PathBuf::from)
        .unwrap_or_default();
    let mut folder = {
        let pictures = home.join("Pictures");
        if pictures.is_dir() {
            pictures
        } else {
            home.clone()
        }
    };
    let mut file = None;
    if let Some(arg) = positional.first() {
        let path = PathBuf::from(arg);
        if path.is_dir() {
            folder = path;
        } else if path.is_file() {
            let parent = path.parent().map(Path::to_path_buf).unwrap_or_default();
            folder = if parent.as_os_str().is_empty() {
                PathBuf::from(".")
            } else {
                parent
            };
            file = Some(path);
        }
    }
    (theme, dual_pane, secondary, folder, file)
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
            let (theme, dual_pane, secondary, folder, file) = start_point();
            let mut app = app::App::new(folder);
            if let Some(file) = file {
                app.select(file.clone());
                app.play(file);
            }
            if let Some(dir) = secondary {
                app.set_secondary(dir);
            }
            if dual_pane {
                app.set_dual_pane(true);
            }
            if let Some(theme) = theme {
                app.set_theme(theme);
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
