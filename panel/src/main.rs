mod model;
mod runner;
use eframe::egui::{self, Color32, Pos2, RichText, Stroke, Vec2};
use model::{Envelope, State, shell_quote};
use runner::Message;
use std::collections::HashMap;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{self, Receiver, Sender, SyncSender};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

struct Panel {
    root: PathBuf,
    session: PathBuf,
    plan: Option<Envelope>,
    pending: Option<Envelope>,
    selected: usize,
    state: State,
    error: String,
    tx: SyncSender<Message>,
    rx: Receiver<Message>,
    controls: Option<Sender<String>>,
    worker: Option<JoinHandle<()>>,
    loading: bool,
    last_poll: Instant,
    plan_contents: Option<Vec<u8>>,
    closing: bool,
    copy_requested: Option<Instant>,
    aliases: HashMap<String, String>,
    editing_alias: bool,
    alias_draft: String,
}

fn read_plan_contents(session: &Path) -> Option<Vec<u8>> {
    let file = std::fs::File::open(session.join("plan.json")).ok()?;
    let mut contents = Vec::new();
    file.take(256 * 1024 + 1).read_to_end(&mut contents).ok()?;
    Some(contents)
}

fn load_aliases(session: &Path) -> HashMap<String, String> {
    let parent = session.parent().unwrap_or(session);
    let path = parent.join("finger_aliases.json");
    if let Ok(bytes) = std::fs::read(&path) {
        if let Ok(map) = serde_json::from_slice(&bytes) {
            return map;
        }
    }
    let mut default_aliases = HashMap::new();
    default_aliases.insert("right-index-finger".into(), "Indice Principale (Login)".into());
    default_aliases.insert("right-thumb".into(), "Pollice Destro".into());
    default_aliases.insert("left-index-finger".into(), "Indice Sinistro".into());
    default_aliases
}

fn save_aliases(session: &Path, aliases: &HashMap<String, String>) {
    let parent = session.parent().unwrap_or(session);
    let path = parent.join("finger_aliases.json");
    if let Ok(json) = serde_json::to_string_pretty(aliases) {
        let _ = std::fs::write(&path, json);
    }
}

fn finger_key(id: &str) -> Option<&'static str> {
    if id.contains("right-index") {
        Some("right-index-finger")
    } else if id.contains("right-thumb") {
        Some("right-thumb")
    } else if id.contains("right-middle") {
        Some("right-middle-finger")
    } else if id.contains("right-ring") {
        Some("right-ring-finger")
    } else if id.contains("right-little") {
        Some("right-little-finger")
    } else if id.contains("left-index") {
        Some("left-index-finger")
    } else if id.contains("left-thumb") {
        Some("left-thumb")
    } else if id.contains("left-middle") {
        Some("left-middle-finger")
    } else if id.contains("left-ring") {
        Some("left-ring-finger")
    } else if id.contains("left-little") {
        Some("left-little-finger")
    } else {
        None
    }
}

impl Panel {
    fn new(cc: &eframe::CreationContext<'_>, root: PathBuf, session: PathBuf) -> Self {
        cc.egui_ctx.set_visuals(egui::Visuals::dark());
        cc.egui_ctx.style_mut_of(egui::Theme::Dark, |style| {
            style.spacing.item_spacing = Vec2::new(14.0, 14.0);
            style.spacing.button_padding = Vec2::new(16.0, 10.0);
            style
                .text_styles
                .insert(egui::TextStyle::Heading, egui::FontId::proportional(22.0));
            style
                .text_styles
                .insert(egui::TextStyle::Body, egui::FontId::proportional(16.5));
            style
                .text_styles
                .insert(egui::TextStyle::Button, egui::FontId::proportional(15.5));
            style
                .text_styles
                .insert(egui::TextStyle::Small, egui::FontId::proportional(14.0));
            style.visuals.override_text_color = Some(Color32::from_rgb(248, 250, 252)); // Slate 50
            style.visuals.panel_fill = Color32::from_rgb(15, 23, 42); // Slate 900
            style.visuals.window_fill = Color32::from_rgb(30, 41, 59); // Slate 800
            style.visuals.selection.bg_fill = Color32::from_rgb(2, 132, 199); // Sky 600
            style.visuals.hyperlink_color = Color32::from_rgb(56, 189, 248);
        });
        let (tx, rx) = mpsc::sync_channel(128);
        let plan_contents = read_plan_contents(&session);
        let aliases = load_aliases(&session);
        runner::load(
            root.clone(),
            session.clone(),
            tx.clone(),
            cc.egui_ctx.clone(),
        );
        Self {
            plan_contents,
            root,
            session,
            plan: None,
            pending: None,
            selected: 0,
            state: State::default(),
            error: String::new(),
            tx,
            rx,
            controls: None,
            worker: None,
            loading: true,
            last_poll: Instant::now(),
            closing: false,
            copy_requested: None,
            aliases,
            editing_alias: false,
            alias_draft: String::new(),
        }
    }
    fn load(&mut self, ctx: &egui::Context) {
        if self.loading {
            return;
        }
        self.loading = true;
        self.plan_contents = read_plan_contents(&self.session);
        self.last_poll = Instant::now();
        runner::load(
            self.root.clone(),
            self.session.clone(),
            self.tx.clone(),
            ctx.clone(),
        );
    }
    fn start(&mut self, ctx: &egui::Context) {
        if self.controls.is_some() {
            return;
        }
        if let Some(plan) = &self.plan {
            if let Some(task) = plan.plan.experiments.get(self.selected) {
                self.state = State::default();
                self.state.active = true;
                self.state.status = "Avvio del sensore in corso…".into();
                let (controls, handle) = runner::spawn(
                    self.root.clone(),
                    self.session.clone(),
                    task.id.clone(),
                    plan.fingerprint.clone(),
                    self.tx.clone(),
                    ctx.clone(),
                );
                self.controls = Some(controls);
                self.worker = Some(handle);
                self.error.clear();
            }
        }
    }
    fn stop(&self) {
        if let Some(tx) = &self.controls {
            let _ = tx.send("cancel".into());
        }
    }
    fn process(&mut self, ctx: &egui::Context) {
        while let Ok(msg) = self.rx.try_recv() {
            match msg {
                Message::Plan(result) => {
                    self.loading = false;
                    match result {
                        Ok(plan) => {
                            self.error.clear();
                            if self
                                .plan
                                .as_ref()
                                .is_none_or(|p| p.fingerprint != plan.fingerprint)
                            {
                                if self.worker.is_some() {
                                    self.pending = Some(plan);
                                } else {
                                    self.plan = Some(plan);
                                    self.selected = 0;
                                    self.state = State::default();
                                }
                            }
                        }
                        Err(e) => {
                            self.error = e;
                            if self.worker.is_none() {
                                self.plan = None;
                            }
                        }
                    }
                }
                Message::Error(e) => {
                    self.error = e;
                    self.state.status = "Errore durante l'operazione".into();
                }
                Message::Event(e) => {
                    self.state.apply(e);
                }
                Message::Exited => {
                    self.controls.take();
                    if let Some(handle) = self.worker.take() {
                        let _ = handle.join();
                    }
                    if let Some(pending) = self.pending.take() {
                        self.plan = Some(pending);
                        self.selected = 0;
                        self.state = State::default();
                    }
                    if self.closing {
                        ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                    }
                }
            }
        }
        if self.last_poll.elapsed() > Duration::from_secs(1) && !self.loading {
            self.last_poll = Instant::now();
            if read_plan_contents(&self.session) != self.plan_contents {
                self.load(ctx);
            }
        }
        if ctx.input(|i| i.viewport().close_requested()) && self.worker.is_some() {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            self.closing = true;
            self.stop();
        }
        ctx.request_repaint_after(if self.worker.is_some() {
            Duration::from_millis(100)
        } else {
            Duration::from_secs(1)
        });
    }
}

fn fingerprint_scanner(ui: &mut egui::Ui, state: &State) {
    let (rect, _) = ui.allocate_exact_size(Vec2::new(170.0, 190.0), egui::Sense::hover());
    let c = rect.center();
    let painter = ui.painter();

    let (bg_fill, border_stroke, ridge_color, glow_color) = if state.present {
        (
            Color32::from_rgb(6, 78, 59), // Emerald 900
            Stroke::new(2.8, Color32::from_rgb(52, 211, 153)), // Emerald 400
            Color32::from_rgb(52, 211, 153),
            Some(Color32::from_rgba_premultiplied(16, 185, 129, 60)),
        )
    } else if state.needed {
        (
            Color32::from_rgb(12, 74, 110), // Sky 900
            Stroke::new(2.5, Color32::from_rgb(56, 189, 248)), // Sky 400
            Color32::from_rgb(56, 189, 248),
            Some(Color32::from_rgba_premultiplied(14, 165, 233, 50)),
        )
    } else if state.active {
        (
            Color32::from_rgb(69, 26, 3), // Amber 950
            Stroke::new(2.0, Color32::from_rgb(245, 158, 11)), // Amber 500
            Color32::from_rgb(251, 191, 36),
            Some(Color32::from_rgba_premultiplied(245, 158, 11, 40)),
        )
    } else {
        (
            Color32::from_rgb(15, 23, 42), // Slate 900
            Stroke::new(1.8, Color32::from_rgb(71, 85, 105)), // Slate 600
            Color32::from_rgb(148, 163, 184), // Slate 400
            None,
        )
    };

    let pad_rect = egui::Rect::from_center_size(c, Vec2::new(136.0, 160.0));

    if let Some(glow) = glow_color {
        let aura = pad_rect.expand(6.0);
        painter.rect_filled(aura, 28.0, glow);
    }

    painter.rect_filled(pad_rect, 24.0, bg_fill);
    painter.rect_stroke(pad_rect, 24.0, border_stroke, egui::StrokeKind::Inside);

    let stroke = Stroke::new(2.6, ridge_color);
    let core_center = Pos2::new(c.x, c.y - 10.0);

    // Core loop
    let mut core_pts = Vec::new();
    core_pts.push(Pos2::new(c.x - 7.0, c.y + 12.0));
    for i in 0..=30 {
        let t = i as f32 / 30.0;
        let angle = std::f32::consts::PI + t * std::f32::consts::PI;
        core_pts.push(core_center + Vec2::new(7.0 * angle.cos(), 12.0 * angle.sin()));
    }
    core_pts.push(Pos2::new(c.x + 7.0, c.y + 16.0));
    painter.add(egui::Shape::line(core_pts, stroke));

    // Arch 1
    let mut arch1 = Vec::new();
    arch1.push(Pos2::new(c.x - 15.0, c.y + 22.0));
    for i in 0..=30 {
        let t = i as f32 / 30.0;
        let angle = std::f32::consts::PI + t * std::f32::consts::PI;
        arch1.push(core_center + Vec2::new(15.0 * angle.cos(), 20.0 * angle.sin()));
    }
    arch1.push(Pos2::new(c.x + 15.0, c.y + 24.0));
    painter.add(egui::Shape::line(arch1, stroke));

    // Arch 2
    let mut arch2 = Vec::new();
    arch2.push(Pos2::new(c.x - 23.0, c.y + 32.0));
    for i in 0..=30 {
        let t = i as f32 / 30.0;
        let angle = std::f32::consts::PI + t * std::f32::consts::PI;
        arch2.push(core_center + Vec2::new(23.0 * angle.cos(), 28.0 * angle.sin()));
    }
    arch2.push(Pos2::new(c.x + 23.0, c.y + 34.0));
    painter.add(egui::Shape::line(arch2, stroke));

    // Arch 3
    let mut arch3 = Vec::new();
    arch3.push(Pos2::new(c.x - 31.0, c.y + 40.0));
    for i in 0..=30 {
        let t = i as f32 / 30.0;
        let angle = std::f32::consts::PI + t * std::f32::consts::PI;
        arch3.push(core_center + Vec2::new(31.0 * angle.cos(), 36.0 * angle.sin()));
    }
    arch3.push(Pos2::new(c.x + 31.0, c.y + 42.0));
    painter.add(egui::Shape::line(arch3, stroke));

    // Arch 4
    let mut arch4 = Vec::new();
    arch4.push(Pos2::new(c.x - 39.0, c.y + 48.0));
    for i in 0..=30 {
        let t = i as f32 / 30.0;
        let angle = std::f32::consts::PI + t * std::f32::consts::PI;
        arch4.push(core_center + Vec2::new(39.0 * angle.cos(), 44.0 * angle.sin()));
    }
    arch4.push(Pos2::new(c.x + 39.0, c.y + 50.0));
    painter.add(egui::Shape::line(arch4, stroke));

    // Lower base whorl
    let mut base1 = Vec::new();
    base1.push(Pos2::new(c.x - 22.0, c.y + 54.0));
    base1.push(Pos2::new(c.x, c.y + 47.0));
    base1.push(Pos2::new(c.x + 22.0, c.y + 54.0));
    painter.add(egui::Shape::line(base1, Stroke::new(2.4, ridge_color)));
}

fn simulation_visual(ui: &mut egui::Ui, state: &State) {
    let (rect, _) = ui.allocate_exact_size(Vec2::new(250.0, 110.0), egui::Sense::hover());
    let y = rect.center().y;
    let painter = ui.painter();
    painter.line_segment(
        [
            Pos2::new(rect.left() + 35.0, y),
            Pos2::new(rect.right() - 35.0, y),
        ],
        Stroke::new(2.5, Color32::from_rgb(71, 85, 105)),
    );
    for index in 0..3 {
        let center = Pos2::new(rect.left() + 35.0 + index as f32 * 90.0, y);
        let done = state.completed > index;
        painter.circle_filled(
            center,
            22.0,
            if done {
                Color32::from_rgb(14, 165, 233)
            } else {
                Color32::from_rgb(30, 41, 59)
            },
        );
        painter.text(
            center,
            egui::Align2::CENTER_CENTER,
            format!("{}", index + 1),
            egui::FontId::proportional(16.0),
            if done {
                Color32::WHITE
            } else {
                Color32::from_rgb(148, 163, 184)
            },
        );
    }
}

impl eframe::App for Panel {
    fn logic(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.process(ctx);
    }
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        egui::CentralPanel::default()
            .frame(egui::Frame::new().inner_margin(24).fill(Color32::from_rgb(15, 23, 42)))
            .show(ui, |ui| {
                egui::ScrollArea::vertical().show(ui, |ui| {
                    // Header Bar (Dark Mode)
                    ui.horizontal(|ui| {
                        ui.heading(
                            RichText::new("REA-FS")
                                .size(26.0)
                                .color(Color32::from_rgb(248, 250, 252))
                                .strong(),
                        );
                        ui.label(
                            RichText::new("· Banco Prove & Gestione Impronte")
                                .size(17.0)
                                .color(Color32::from_rgb(148, 163, 184)),
                        );
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            if ui
                                .add_enabled(
                                    !self.loading,
                                    egui::Button::new(
                                        RichText::new("🔄 Ricarica piano")
                                            .size(14.5)
                                            .color(Color32::from_rgb(248, 250, 252)),
                                    ),
                                )
                                .clicked()
                            {
                                self.load(&ctx);
                            }
                        });
                    });

                    ui.add_space(14.0);

                    if !self.error.is_empty() {
                        ui.colored_label(
                            Color32::from_rgb(248, 113, 113),
                            RichText::new(&self.error).size(15.0).strong(),
                        );
                        ui.add_space(8.0);
                    }

                    let Some(envelope) = self.plan.clone() else {
                        ui.heading("Caricamento sessione in corso…");
                        ui.label("Nessuna azione parte automaticamente.");
                        ui.monospace(self.session.join("plan.json").display().to_string());
                        return;
                    };

                    ui.horizontal_top(|ui| {
                        // Left Sidebar: Experiments List (Dark Cards)
                        ui.allocate_ui_with_layout(
                            Vec2::new(280.0, 0.0),
                            egui::Layout::top_down(egui::Align::Min),
                            |ui| {
                                ui.label(
                                    RichText::new("Scegli l'operazione:")
                                        .size(16.5)
                                        .color(Color32::from_rgb(248, 250, 252))
                                        .strong(),
                                );
                                ui.add_space(6.0);

                                for (index, task) in envelope.plan.experiments.iter().enumerate() {
                                    let is_selected = self.selected == index;
                                    let mut display_title = task.title.clone();
                                    if let Some(fkey) = finger_key(&task.id) {
                                        if let Some(alias) = self.aliases.get(fkey) {
                                            display_title = format!("{} ({})", task.title, alias);
                                        }
                                    }

                                    let btn = egui::Button::selectable(is_selected, &display_title)
                                        .min_size(Vec2::new(270.0, 44.0));

                                    if ui.add_enabled(self.worker.is_none(), btn).clicked() {
                                        self.selected = index;
                                        self.state = State::default();
                                        self.editing_alias = false;
                                    }
                                }

                                ui.add_space(18.0);
                                ui.separator();
                                ui.add_space(8.0);

                                ui.label(
                                    RichText::new("Dispositivo:")
                                        .size(14.0)
                                        .color(Color32::from_rgb(148, 163, 184))
                                        .strong(),
                                );
                                ui.label(
                                    RichText::new("Synaptics 06cb:00dc (Acer Aspire A514-54)")
                                        .size(13.5)
                                        .color(Color32::from_rgb(226, 232, 240)),
                                );
                                ui.label(
                                    RichText::new("Display: Wayland nativo")
                                        .size(13.5)
                                        .color(Color32::from_rgb(52, 211, 153))
                                        .strong(),
                                );
                            },
                        );

                        ui.add_space(20.0);

                        // Right Pane: Active Experiment & Guidance Card
                        ui.vertical(|ui| {
                            ui.set_width(ui.available_width());
                            let task = &envelope.plan.experiments[self.selected];
                            let sim = task.adapter == "simulation";
                            let is_fprintd = task.adapter == "fprintd";

                            let accent_color = if sim {
                                Color32::from_rgb(245, 158, 11) // Amber 500
                            } else if is_fprintd {
                                Color32::from_rgb(56, 189, 248) // Sky 400
                            } else {
                                Color32::from_rgb(45, 212, 191) // Teal 400
                            };

                            // Title & Tag
                            ui.horizontal(|ui| {
                                ui.heading(
                                    RichText::new(&task.title)
                                        .size(24.0)
                                        .color(Color32::from_rgb(248, 250, 252))
                                        .strong(),
                                );
                            });

                            // Custom alias editor for fingerprint tasks
                            if let Some(fkey) = finger_key(&task.id) {
                                ui.horizontal(|ui| {
                                    let current_alias = self
                                        .aliases
                                        .get(fkey)
                                        .cloned()
                                        .unwrap_or_else(|| "Nessun alias".into());
                                    if self.editing_alias {
                                        ui.text_edit_singleline(&mut self.alias_draft);
                                        if ui.button("💾 Salva").clicked() {
                                            if !self.alias_draft.trim().is_empty() {
                                                self.aliases
                                                    .insert(fkey.to_string(), self.alias_draft.trim().to_string());
                                                save_aliases(&self.session, &self.aliases);
                                            }
                                            self.editing_alias = false;
                                        }
                                        if ui.button("Annulla").clicked() {
                                            self.editing_alias = false;
                                        }
                                    } else {
                                        ui.label(
                                            RichText::new(format!("Alias impronta: \"{}\"", current_alias))
                                                .size(14.5)
                                                .color(Color32::from_rgb(148, 163, 184)),
                                        );
                                        if ui.small_button("✏ Rinomina").clicked() {
                                            self.alias_draft = current_alias;
                                            self.editing_alias = true;
                                        }
                                    }
                                });
                            }

                            // Tag badge
                            let tag_text = if sim {
                                "Dimostrazione Simulata • Nessun sensore aperto"
                            } else if is_fprintd {
                                "Sensore Reale • Hardware collegato via fprintd"
                            } else {
                                "Operazione di Sistema • Esecuzione locale"
                            };
                            ui.colored_label(
                                accent_color,
                                RichText::new(tag_text).size(14.5).strong(),
                            );

                            ui.add_space(10.0);

                            // Main Interactive Card (Dark Slate 800)
                            egui::Frame::new()
                                .inner_margin(18)
                                .corner_radius(12.0)
                                .fill(Color32::from_rgb(30, 41, 59))
                                .stroke(Stroke::new(1.0, Color32::from_rgb(51, 65, 85)))
                                .show(ui, |ui| {
                                    ui.vertical_centered(|ui| {
                                        if sim {
                                            simulation_visual(ui, &self.state);
                                        } else if is_fprintd {
                                            fingerprint_scanner(ui, &self.state);
                                            ui.add_space(4.0);
                                            ui.label(
                                                RichText::new(
                                                    "Indicatore biometrico di contatto e stato sensore.",
                                                )
                                                .size(13.5)
                                                .color(Color32::from_rgb(148, 163, 184)),
                                            );
                                        }

                                        ui.add_space(8.0);

                                        // Big prominent status label
                                        let status_color = if self.state.present {
                                            Color32::from_rgb(52, 211, 153) // Emerald 400
                                        } else if self.state.needed {
                                            Color32::from_rgb(56, 189, 248) // Sky 400
                                        } else if self.state.active {
                                            Color32::from_rgb(251, 191, 36) // Amber 400
                                        } else if self.state.status.contains("riconosciuta")
                                            || self.state.status.contains("completat")
                                        {
                                            Color32::from_rgb(52, 211, 153) // Emerald 400
                                        } else if self.state.status.contains("non riconosciuta")
                                            || self.state.status.contains("fallit")
                                            || self.state.status.contains("scaduto")
                                        {
                                            Color32::from_rgb(248, 113, 113) // Red 400
                                        } else {
                                            Color32::from_rgb(248, 250, 252) // Slate 50
                                        };

                                        ui.label(
                                            RichText::new(&self.state.status)
                                                .size(23.0)
                                                .color(status_color)
                                                .strong(),
                                        );
                                    });

                                    ui.add_space(10.0);

                                    // Guidance block (Dark Slate 900)
                                    egui::Frame::new()
                                        .inner_margin(12)
                                        .corner_radius(8.0)
                                        .fill(Color32::from_rgb(15, 23, 42))
                                        .stroke(Stroke::new(1.0, Color32::from_rgb(51, 65, 85)))
                                        .show(ui, |ui| {
                                            ui.label(
                                                RichText::new("Cosa fare:")
                                                    .size(15.0)
                                                    .color(Color32::from_rgb(248, 250, 252))
                                                    .strong(),
                                            );
                                            ui.label(
                                                RichText::new(if self.worker.is_some() || !self.state.run_id.is_empty() {
                                                    &self.state.detail
                                                } else {
                                                    &task.instructions
                                                })
                                                .size(15.5)
                                                .color(Color32::from_rgb(203, 213, 225)),
                                            );
                                        });

                                    if self.state.total > 0 {
                                        ui.add_space(8.0);
                                        let unit = if sim { "passaggi simulati" } else { "appoggi registrati" };
                                        ui.add(
                                            egui::ProgressBar::new(
                                                (self.state.completed as f32 / self.state.total as f32).min(1.0),
                                            )
                                            .fill(accent_color)
                                            .text(format!(
                                                "{} / {} {}",
                                                self.state.completed, self.state.total, unit
                                            )),
                                        );
                                    }

                                    if self.state.active {
                                        ui.add_space(4.0);
                                        ui.label(
                                            RichText::new(format!(
                                                "⏱ Tempo residuo: {:.0} s",
                                                self.state.remaining()
                                            ))
                                            .size(14.0)
                                            .color(Color32::from_rgb(148, 163, 184)),
                                        );
                                    }
                                });

                            ui.add_space(14.0);

                            // Action buttons
                            ui.horizontal_wrapped(|ui| {
                                let start_label = if self.state.active || self.worker.is_some() {
                                    "In esecuzione…"
                                } else if !self.state.run_id.is_empty() {
                                    "↺ Esegui di nuovo"
                                } else if sim {
                                    "▶ Avvia dimostrazione"
                                } else {
                                    "▶ Esegui questa prova"
                                };

                                let can_start = self.worker.is_none() && self.error.is_empty();
                                let btn = egui::Button::new(
                                    RichText::new(start_label)
                                        .size(16.0)
                                        .color(Color32::WHITE)
                                        .strong(),
                                )
                                .fill(Color32::from_rgb(2, 132, 199))
                                .min_size(Vec2::new(200.0, 46.0));

                                if ui.add_enabled(can_start, btn).clicked() {
                                    self.start(&ctx);
                                }

                                if self.worker.is_some() {
                                    let stop_btn = egui::Button::new(
                                        RichText::new("⏹ Interrompi")
                                            .size(15.5)
                                            .color(Color32::WHITE)
                                            .strong(),
                                    )
                                    .fill(Color32::from_rgb(220, 38, 38))
                                    .min_size(Vec2::new(130.0, 46.0));
                                    if ui.add(stop_btn).clicked() {
                                        self.stop();
                                    }
                                }

                                if self.worker.is_some()
                                    && is_fprintd
                                    && ui.button("Registra conferma manuale").clicked()
                                {
                                    if let Some(tx) = &self.controls {
                                        let _ = tx.send("confirm".into());
                                    }
                                }
                            });

                            ui.add_space(12.0);

                            // Technical details dropdown
                            ui.collapsing("Comando e dettagli tecnici della prova", |ui| {
                                let command = task
                                    .argv
                                    .iter()
                                    .map(|a| shell_quote(a))
                                    .collect::<Vec<_>>()
                                    .join(" ");
                                ui.monospace(&command);
                                ui.label(
                                    RichText::new(format!(
                                        "Cartella: {}",
                                        self.root.join(&task.cwd).display()
                                    ))
                                    .size(13.5),
                                );
                                ui.label(
                                    RichText::new(format!("Categoria autorizzazione: {}", task.category))
                                        .size(13.5),
                                );
                                if task.raw_log {
                                    ui.label(
                                        RichText::new("Output grezzo salvato nei log privati.")
                                            .size(13.5),
                                    );
                                }
                                if ui.button("📋 Copia comando").clicked() {
                                    ctx.copy_text(command);
                                    self.copy_requested = Some(Instant::now());
                                }
                            });
                        });
                    });

                    ui.add_space(16.0);
                    ui.separator();

                    if self.pending.is_some() {
                        ui.colored_label(
                            Color32::from_rgb(251, 191, 36),
                            RichText::new("Piano aggiornato: la prova attiva conserva la revisione iniziale.")
                                .size(14.0)
                                .strong(),
                        );
                    }

                    if !self.state.run_directory.is_empty() && self.worker.is_none() {
                        ui.label(
                            RichText::new("Risultato salvato automaticamente • l’agente può continuare")
                                .size(16.0)
                                .color(Color32::from_rgb(52, 211, 153))
                                .strong(),
                        );
                        ui.label(
                            RichText::new(if self.state.next_steps.is_empty() {
                                "L’agente legge i file della sessione. Non serve copiare l’output."
                            } else {
                                &self.state.next_steps
                            })
                            .size(15.0),
                        );
                        ui.monospace(format!("Log: {}/summary.json", self.state.run_directory));
                        if ui.button("📋 Copia messaggio per l’agente").clicked() {
                            ctx.copy_text(format!(
                                "Leggi {}/handoff.txt e riprendi dai log privati della prova.",
                                self.state.run_directory
                            ));
                            self.copy_requested = Some(Instant::now());
                        }
                    } else {
                        ui.label(
                            RichText::new(
                                "Log salvati automaticamente con timestamp. Nessuna copia manuale necessaria.",
                            )
                            .size(14.0)
                            .color(Color32::from_rgb(148, 163, 184)),
                        );
                    }

                    if self.copy_requested.is_some_and(|time| time.elapsed() < Duration::from_secs(3)) {
                        ui.colored_label(
                            Color32::from_rgb(52, 211, 153),
                            RichText::new("✓ Testo copiato negli appunti.").size(14.0).strong(),
                        );
                    }

                    ui.collapsing("Log e informazioni per l’agente", |ui| {
                        ui.monospace(self.session.join("latest-run.json").display().to_string());
                        if !self.state.run_directory.is_empty() {
                            ui.monospace(&self.state.run_directory);
                        }
                        for event in self.state.events.iter().rev().take(8) {
                            ui.label(RichText::new(event).size(13.5));
                        }
                        ui.label(RichText::new(format!("Piano: {}", envelope.plan.plan_id)).size(13.5));
                    });
                });
            });
    }
    fn on_exit(&mut self, _gl: Option<&eframe::glow::Context>) {
        self.stop();
        self.controls.take();
        if let Some(handle) = self.worker.take() {
            let _ = handle.join();
        }
    }
}

fn main() -> eframe::Result {
    let mut args = std::env::args().skip(1);
    let mut root = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
    let mut session = None;
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--repo" => {
                if let Some(path) = args.next() {
                    root = PathBuf::from(path);
                } else {
                    eprintln!("--repo richiede un percorso");
                    std::process::exit(2);
                }
            }
            "--session-dir" => {
                session = args.next().map(PathBuf::from);
                if session.is_none() {
                    eprintln!("--session-dir richiede un percorso");
                    std::process::exit(2);
                }
            }
            "--help" => {
                println!("rea-fs-panel [--repo REPOSITORY] [--session-dir DIRECTORY]");
                return Ok(());
            }
            _ => {
                eprintln!("Opzione sconosciuta: {arg}");
                std::process::exit(2);
            }
        }
    }
    let root = root.canonicalize().unwrap_or(root);
    let session = session.unwrap_or_else(|| root.join(".rea-fs.local/panel"));
    let session = if session.is_absolute() {
        session
    } else {
        root.join(session)
    };
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_app_id("rea-fs-panel")
            .with_title("REA-FS · Banco Prove e Gestione Impronte")
            .with_inner_size([980.0, 740.0])
            .with_min_inner_size([860.0, 640.0]),
        ..Default::default()
    };
    eframe::run_native(
        "REA-FS · Esperimenti",
        options,
        Box::new(move |cc| Ok(Box::new(Panel::new(cc, root, session)))),
    )
}

#[cfg(test)]
mod plan_watch_tests {
    use super::*;
    #[test]
    fn new_plan_clears_previous_run_for_immediate_and_deferred_updates() {
        for mode in 0..3 {
            let (tx, rx) = mpsc::sync_channel(128);
            let mut panel = Panel {
                root: PathBuf::new(),
                session: PathBuf::new(),
                plan: None,
                pending: None,
                selected: 0,
                state: State::default(),
                error: String::new(),
                tx,
                rx,
                controls: None,
                worker: None,
                loading: false,
                last_poll: Instant::now(),
                plan_contents: None,
                closing: false,
                copy_requested: None,
                aliases: HashMap::new(),
                editing_alias: false,
                alias_draft: String::new(),
            };
            panel.state.run_id = "old-simulation".into();
            panel.state.simulation = true;
            panel.state.run_directory = "old-log".into();
            let envelope: Envelope = serde_json::from_value(serde_json::json!({
                "fingerprint": "new",
                "plan": {
                    "plan_id": "new",
                    "title": "Hardware",
                    "revision": 2,
                    "experiments": []
                }
            }))
            .unwrap();
            if mode == 1 {
                panel.pending = Some(envelope);
                panel.tx.send(Message::Exited).unwrap();
            } else if mode == 2 {
                panel.worker = Some(std::thread::spawn(|| {}));
                panel.tx.send(Message::Plan(Ok(envelope))).unwrap();
                panel.process(&egui::Context::default());
                assert!(panel.pending.is_some());
                assert_eq!(panel.state.run_id, "old-simulation");
                panel.tx.send(Message::Exited).unwrap();
            } else {
                panel.tx.send(Message::Plan(Ok(envelope))).unwrap();
            }
            panel.process(&egui::Context::default());
            assert!(panel.state.run_id.is_empty());
            assert!(panel.state.run_directory.is_empty());
            assert!(!panel.state.simulation);
        }
    }
    #[test]
    fn reads_are_bounded_and_detect_content_changes() {
        let directory = std::env::temp_dir().join(format!("rea-fs-watch-{}", std::process::id()));
        std::fs::create_dir(&directory).unwrap();
        let path = directory.join("plan.json");
        assert!(read_plan_contents(&directory).is_none());
        std::fs::write(&path, b"first").unwrap();
        let first = read_plan_contents(&directory);
        assert_eq!(first, read_plan_contents(&directory));
        std::fs::write(&path, b"other").unwrap();
        assert_ne!(first, read_plan_contents(&directory));
        std::fs::write(&path, vec![b'x'; 300_000]).unwrap();
        assert_eq!(
            read_plan_contents(&directory).unwrap().len(),
            256 * 1024 + 1
        );
        std::fs::remove_dir_all(directory).unwrap();
    }
}
