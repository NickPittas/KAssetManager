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
    // The last flag: a folder or file named on the command line beats the last session.
    bool,
) {
    let mut theme = None;
    let mut dual_pane = false;
    let mut explicit_folder = false;
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
        // A named place wins over the folder remembered from last time - that is what the argument is for.
        explicit_folder = true;
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
    (theme, dual_pane, secondary, folder, file, explicit_folder)
}

fn main() {
    let native_options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("KAsset Manager")
            .with_app_id("io.github.nickpittas.KAssetManager")
            .with_min_inner_size([640.0, 480.0])
            .with_inner_size([1400.0, 900.0]),
        renderer: eframe::Renderer::Wgpu,
        ..Default::default()
    };

    let result = eframe::run_native(
        "KAsset Manager",
        native_options,
        Box::new(|cc| {
            // A scrollbar is an affordance in a file manager: you have to be able to see that a name runs past the
            // panel. egui floats its bars and reveals them only while scrolling, so they are made to allocate space
            // and stay visible instead.
            cc.egui_ctx
                .all_styles_mut(|style| style.spacing.scroll.floating = false);
            let (theme, dual_pane, secondary, folder, file, explicit_folder) = start_point();
            let mut app = app::App::new(folder);
            if explicit_folder {
                // A folder named on the command line beats the remembered one; remembering starts from here.
                app.start_folder_memory();
            } else {
                // Nothing was named, so reopen where the panes were when the window last closed - a folder that
                // no longer exists is skipped, leaving the usual Pictures/home start.
                app.restore_folders();
            }
            // A file named on the command line arrives relative to where the shell was; the panes hold
            // absolute paths, so selection and playback have to speak the same language.
            if let Some(file) = file.as_deref().map(app::absolute) {
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
