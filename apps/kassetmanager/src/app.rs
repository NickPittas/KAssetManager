//! The egui application shell. Slice 1 of M1: a window, a status bar, and one interaction that proves
//! the wgpu surface and the headless test harness both drive this UI. Panes, tree and breadcrumb come next.

/// One `App` per window. Deliberately tiny for now: real state arrives with the browser panes.
#[derive(Default, Debug)]
pub struct App {
    /// Proves input reaches the app and is reflected in state (see the harness test below).
    clicks: u32,
}

impl App {
    pub fn new() -> Self {
        Self::default()
    }

    #[cfg(test)] // only the harness reads it back; the app itself never queries its own click count
    pub fn clicks(&self) -> u32 {
        self.clicks
    }

    /// The bottom status bar. Kept as a plain method so both the live `update` and the headless harness
    /// render exactly the same UI.
    pub fn status_bar(&mut self, ui: &mut egui::Ui) {
        if ui.button("ping").clicked() {
            self.clicks += 1;
        }
        ui.label(format!("clicks: {}", self.clicks));
        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
            ui.label("KAsset Manager");
        });
    }
}

impl eframe::App for App {
    /// egui 0.36 hands the app a root `Ui` (not a `Context`); panels are shown on it.
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        egui::Panel::bottom("status_bar").show(ui, |ui| self.status_bar(ui));
        egui::CentralPanel::default().show(ui, |ui| {
            ui.label("KAsset Manager — M1 slice 1 (window + status bar)");
        });
    }
}

#[cfg(test)]
mod tests {
    use crate::app::App;
    use egui_kittest::{Harness, kittest::Queryable};

    /// The gate for this slice: a click on the status bar's button reaches the app and shows up in state.
    #[test]
    fn ping_button_reaches_app_state() {
        let mut harness = Harness::new_ui_state(|ui, app| app.status_bar(ui), App::new());

        harness.get_by_label("ping").click();
        harness.run();

        assert_eq!(harness.state().clicks(), 1);
    }
}
