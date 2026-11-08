//! Entry point. Slice 1 of the M1 port: open a wgpu window and run [`app::App`].

mod app;

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
        Box::new(|_cc| Ok(Box::new(app::App::new()))),
    );

    if let Err(err) = result {
        // No unwrap in this workspace: report and exit non-zero instead.
        eprintln!("KAsset Manager exited with an error: {err}");
        std::process::exit(1);
    }
}
