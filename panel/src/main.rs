mod model;
mod runner;
use eframe::egui::{self, Color32, CursorIcon, FontId, Pos2, Rect, RichText, Sense, Stroke, Vec2};
use model::{Envelope, Experiment, Phase, State, shell_quote};
use runner::Message;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{self, Receiver, Sender, SyncSender};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

// Palette (Tailwind slate/sky/emerald/amber/red).
const BG: Color32 = Color32::from_rgb(11, 17, 32);
const CARD: Color32 = Color32::from_rgb(22, 31, 50);
const CARD_HI: Color32 = Color32::from_rgb(30, 41, 64);
const BORDER: Color32 = Color32::from_rgb(45, 58, 82);
const TEXT: Color32 = Color32::from_rgb(241, 245, 249);
const MUTED: Color32 = Color32::from_rgb(148, 163, 184);
const SKY: Color32 = Color32::from_rgb(56, 189, 248);
const SKY_DARK: Color32 = Color32::from_rgb(2, 132, 199);
const GREEN: Color32 = Color32::from_rgb(52, 211, 153);
const AMBER: Color32 = Color32::from_rgb(251, 191, 36);
const RED: Color32 = Color32::from_rgb(248, 113, 113);
const RED_DARK: Color32 = Color32::from_rgb(185, 28, 28);

const SIDES: [(&str, &str); 2] = [("left", "Mano sinistra"), ("right", "Mano destra")];
const FINGERS: [(&str, &str); 5] = [
    ("thumb", "Pollice"),
    ("index-finger", "Indice"),
    ("middle-finger", "Medio"),
    ("ring-finger", "Anulare"),
    ("little-finger", "Mignolo"),
];

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Tab {
    Verify,
    Enroll,
    Manage,
    Tools,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Icon {
    Refresh,
    Play,
    Stop,
    Trash,
    Check,
    Cross,
    Note,
    Finger,
}

struct Panel {
    root: PathBuf,
    session: PathBuf,
    plan: Option<Envelope>,
    pending: Option<Envelope>,
    tab: Tab,
    selected_tool: usize,
    verify: State,
    enroll: State,
    manage: State,
    tools: State,
    running: Option<Tab>,
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
    side: usize,
    finger: usize,
    enrolled: Option<Vec<String>>,
    refresh_after_exit: bool,
    confirm_delete: Option<String>,
    note_draft: String,
    note_feedback: Option<(Instant, Result<(), String>)>,
}

fn read_plan_contents(session: &Path) -> Option<Vec<u8>> {
    let file = std::fs::File::open(session.join("plan.json")).ok()?;
    let mut contents = Vec::new();
    file.take(256 * 1024 + 1).read_to_end(&mut contents).ok()?;
    Some(contents)
}

/// "right-index-finger" -> "Indice destro".
fn finger_label(id: &str) -> String {
    let (side, rest) = id.split_once('-').unwrap_or(("", id));
    let name = FINGERS
        .iter()
        .find(|(k, _)| *k == rest)
        .map(|(_, v)| *v)
        .unwrap_or(rest);
    match side {
        "left" => format!("{name} sinistro"),
        "right" => format!("{name} destro"),
        _ => name.to_string(),
    }
}

fn program(task: &Experiment) -> &str {
    task.argv
        .first()
        .map(|a| a.rsplit('/').next().unwrap_or(a))
        .unwrap_or("")
}

fn has_finger(task: &Experiment) -> bool {
    task.parameters.iter().any(|p| p == "finger")
}

/// Plan entries that power the dedicated fingerprint tabs; everything else lives in Strumenti.
fn find(plan: &Envelope, tab: Tab) -> Option<usize> {
    plan.plan.experiments.iter().position(|t| match tab {
        Tab::Verify => t.adapter == "fprintd" && program(t) == "fprintd-verify",
        Tab::Enroll => t.adapter == "fprintd" && program(t) == "fprintd-enroll" && has_finger(t),
        Tab::Manage => program(t) == "fprintd-list",
        Tab::Tools => false,
    })
}

fn find_delete(plan: &Envelope) -> Option<usize> {
    plan.plan
        .experiments
        .iter()
        .position(|t| program(t) == "fprintd-delete" && has_finger(t))
}

fn is_tool(plan: &Envelope, index: usize) -> bool {
    ![
        find(plan, Tab::Verify),
        find(plan, Tab::Enroll),
        find(plan, Tab::Manage),
        find_delete(plan),
    ]
    .contains(&Some(index))
}

fn lighten(c: Color32, amount: u8) -> Color32 {
    Color32::from_rgb(
        c.r().saturating_add(amount),
        c.g().saturating_add(amount),
        c.b().saturating_add(amount),
    )
}

fn arc(center: Pos2, radius: f32, from: f32, to: f32) -> Vec<Pos2> {
    (0..=24)
        .map(|i| {
            let a = from + (to - from) * i as f32 / 24.0;
            center + Vec2::new(radius * a.cos(), radius * a.sin())
        })
        .collect()
}

/// Icons are painted as vector shapes: the bundled fonts lack most emoji glyphs.
fn draw_icon(painter: &egui::Painter, icon: Icon, c: Pos2, color: Color32) {
    use std::f32::consts::PI;
    let s = Stroke::new(2.0, color);
    match icon {
        Icon::Play => {
            painter.add(egui::Shape::convex_polygon(
                vec![c + Vec2::new(-4.5, -6.5), c + Vec2::new(6.5, 0.0), c + Vec2::new(-4.5, 6.5)],
                color,
                Stroke::NONE,
            ));
        }
        Icon::Stop => {
            painter.rect_filled(Rect::from_center_size(c, Vec2::splat(11.0)), 2.0, color);
        }
        Icon::Refresh => {
            painter.add(egui::Shape::line(arc(c, 6.5, -PI * 0.35, PI * 1.45), s));
            let tip = c + Vec2::new(6.5 * (-PI * 0.35).cos(), 6.5 * (-PI * 0.35).sin());
            painter.add(egui::Shape::convex_polygon(
                vec![tip + Vec2::new(-1.0, -4.5), tip + Vec2::new(4.0, 1.5), tip + Vec2::new(-3.5, 2.5)],
                color,
                Stroke::NONE,
            ));
        }
        Icon::Trash => {
            painter.line_segment([c + Vec2::new(-7.0, -5.0), c + Vec2::new(7.0, -5.0)], s);
            painter.line_segment([c + Vec2::new(-2.5, -7.5), c + Vec2::new(2.5, -7.5)], s);
            let body = Rect::from_min_max(c + Vec2::new(-5.0, -4.0), c + Vec2::new(5.0, 8.0));
            painter.rect_stroke(body, 1.5, s, egui::StrokeKind::Middle);
            for x in [-2.0, 2.0] {
                painter.line_segment([c + Vec2::new(x, -1.0), c + Vec2::new(x, 5.5)], Stroke::new(1.5, color));
            }
        }
        Icon::Check => {
            painter.add(egui::Shape::line(
                vec![c + Vec2::new(-6.0, 0.0), c + Vec2::new(-2.0, 4.5), c + Vec2::new(6.5, -5.0)],
                Stroke::new(2.4, color),
            ));
        }
        Icon::Cross => {
            painter.line_segment([c + Vec2::new(-5.0, -5.0), c + Vec2::new(5.0, 5.0)], Stroke::new(2.4, color));
            painter.line_segment([c + Vec2::new(5.0, -5.0), c + Vec2::new(-5.0, 5.0)], Stroke::new(2.4, color));
        }
        Icon::Note => {
            let r = Rect::from_center_size(c, Vec2::new(12.0, 14.0));
            painter.rect_stroke(r, 2.0, s, egui::StrokeKind::Middle);
            for y in [-2.5, 1.0, 4.5] {
                painter.line_segment([c + Vec2::new(-3.5, y), c + Vec2::new(3.5, y)], Stroke::new(1.4, color));
            }
        }
        Icon::Finger => {
            for r in [2.5, 5.0, 7.5] {
                painter.add(egui::Shape::line(arc(c + Vec2::new(0.0, 2.0), r, PI, 2.0 * PI), Stroke::new(1.6, color)));
                painter.line_segment(
                    [c + Vec2::new(-r, 2.0), c + Vec2::new(-r, 6.0)],
                    Stroke::new(1.6, color),
                );
                painter.line_segment(
                    [c + Vec2::new(r, 2.0), c + Vec2::new(r, 6.0)],
                    Stroke::new(1.6, color),
                );
            }
        }
    }
}

/// Rounded button with a painted icon; never depends on font glyph coverage.
fn button(ui: &mut egui::Ui, icon: Option<Icon>, label: &str, fill: Color32, size: Vec2, enabled: bool) -> egui::Response {
    let (rect, response) = ui.allocate_exact_size(size, if enabled { Sense::click() } else { Sense::hover() });
    let painter = ui.painter();
    let hovered = enabled && response.hovered();
    let base = if !enabled {
        fill.gamma_multiply(0.35)
    } else if hovered {
        lighten(fill, 18)
    } else {
        fill
    };
    painter.rect_filled(rect, 10.0, base);
    if fill == CARD_HI {
        painter.rect_stroke(rect, 10.0, Stroke::new(1.0, BORDER), egui::StrokeKind::Inside);
    }
    let color = if enabled { Color32::WHITE } else { MUTED };
    let galley = painter.layout_no_wrap(label.to_string(), FontId::proportional(15.5), color);
    let icon_width = if icon.is_some() { 24.0 } else { 0.0 };
    let start = rect.center().x - (icon_width + galley.size().x) / 2.0;
    if let Some(icon) = icon {
        draw_icon(painter, icon, Pos2::new(start + 8.0, rect.center().y), color);
    }
    painter.galley(
        Pos2::new(start + icon_width, rect.center().y - galley.size().y / 2.0),
        galley,
        color,
    );
    if enabled {
        response.on_hover_cursor(CursorIcon::PointingHand)
    } else {
        response
    }
}

/// Radio-style selectable card (used for hand and finger choice).
fn choice(ui: &mut egui::Ui, label: &str, sub: &str, selected: bool, size: Vec2, enabled: bool) -> egui::Response {
    let (rect, response) = ui.allocate_exact_size(size, if enabled { Sense::click() } else { Sense::hover() });
    let painter = ui.painter();
    let hovered = enabled && response.hovered();
    let fill = if selected {
        Color32::from_rgb(12, 50, 80)
    } else if hovered {
        CARD_HI
    } else {
        CARD
    };
    painter.rect_filled(rect, 12.0, fill);
    painter.rect_stroke(
        rect,
        12.0,
        Stroke::new(if selected { 2.0 } else { 1.0 }, if selected { SKY } else { BORDER }),
        egui::StrokeKind::Inside,
    );
    let dot = Pos2::new(rect.left() + 20.0, rect.center().y);
    painter.circle_stroke(dot, 8.0, Stroke::new(2.0, if selected { SKY } else { MUTED }));
    if selected {
        painter.circle_filled(dot, 4.5, SKY);
    }
    let text_x = rect.left() + 38.0;
    if sub.is_empty() {
        painter.text(Pos2::new(text_x, rect.center().y), egui::Align2::LEFT_CENTER, label, FontId::proportional(16.0), TEXT);
    } else {
        painter.text(Pos2::new(text_x, rect.center().y - 9.0), egui::Align2::LEFT_CENTER, label, FontId::proportional(16.0), TEXT);
        painter.text(Pos2::new(text_x, rect.center().y + 11.0), egui::Align2::LEFT_CENTER, sub, FontId::proportional(12.5), GREEN);
    }
    if enabled {
        response.on_hover_cursor(CursorIcon::PointingHand)
    } else {
        response
    }
}

fn card<R>(ui: &mut egui::Ui, add: impl FnOnce(&mut egui::Ui) -> R) -> R {
    egui::Frame::new()
        .inner_margin(20)
        .corner_radius(14.0)
        .fill(CARD)
        .stroke(Stroke::new(1.0, BORDER))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            add(ui)
        })
        .inner
}

fn fingerprint_scanner(ui: &mut egui::Ui, phase: Phase, result: Option<bool>) {
    use std::f32::consts::PI;
    let (rect, _) = ui.allocate_exact_size(Vec2::new(150.0, 170.0), Sense::hover());
    let c = rect.center();
    let painter = ui.painter();
    let (fill, accent) = match (result, phase) {
        (Some(true), _) => (Color32::from_rgb(6, 60, 45), GREEN),
        (Some(false), _) => (Color32::from_rgb(70, 20, 20), RED),
        (_, Phase::Hold) => (Color32::from_rgb(6, 60, 45), GREEN),
        (_, Phase::Touch) => (Color32::from_rgb(10, 55, 85), SKY),
        (_, Phase::Lift) | (_, Phase::Preparing) | (_, Phase::Wait) => (Color32::from_rgb(60, 35, 5), AMBER),
        (_, Phase::Idle) => (BG, MUTED),
    };
    let pad = Rect::from_center_size(c, Vec2::new(124.0, 150.0));
    if phase != Phase::Idle || result.is_some() {
        let pulse = (ui.input(|i| i.time) * 3.0).sin() as f32 * 0.5 + 0.5;
        let glow = accent.gamma_multiply(0.12 + 0.18 * if phase == Phase::Touch { pulse } else { 0.6 });
        painter.rect_filled(pad.expand(7.0), 30.0, glow);
    }
    painter.rect_filled(pad, 26.0, fill);
    painter.rect_stroke(pad, 26.0, Stroke::new(2.2, accent), egui::StrokeKind::Inside);
    let core = Pos2::new(c.x, c.y - 6.0);
    for (i, r) in [7.0f32, 15.0, 23.0, 31.0, 39.0, 47.0].iter().enumerate() {
        let mut points = arc(core, *r, PI, 2.0 * PI);
        let drop = 14.0 + i as f32 * 7.0;
        points.insert(0, core + Vec2::new(-r, drop));
        points.push(core + Vec2::new(*r, drop * 0.85));
        painter.add(egui::Shape::line(points, Stroke::new(2.4, accent)));
    }
}

fn simulation_visual(ui: &mut egui::Ui, state: &State) {
    let (rect, _) = ui.allocate_exact_size(Vec2::new(250.0, 90.0), Sense::hover());
    let y = rect.center().y;
    let painter = ui.painter();
    painter.line_segment(
        [Pos2::new(rect.left() + 35.0, y), Pos2::new(rect.right() - 35.0, y)],
        Stroke::new(2.5, BORDER),
    );
    for index in 0..3 {
        let center = Pos2::new(rect.left() + 35.0 + index as f32 * 90.0, y);
        let done = state.completed > index;
        painter.circle_filled(center, 22.0, if done { SKY_DARK } else { CARD_HI });
        painter.text(
            center,
            egui::Align2::CENTER_CENTER,
            format!("{}", index + 1),
            FontId::proportional(16.0),
            if done { Color32::WHITE } else { MUTED },
        );
    }
}

/// Big, unambiguous instruction derived from observed device events.
fn phase_banner(ui: &mut egui::Ui, state: &State, verifying: bool) {
    let (title, sub, color) = match state.phase() {
        Phase::Preparing => ("NON TOCCARE", "Apertura e calibrazione del sensore…", AMBER),
        Phase::Wait => ("ATTENDI", "Il sensore non chiede ancora il dito.", AMBER),
        Phase::Touch => ("APPOGGIA IL DITO", "Appoggialo piatto al centro del lettore.", SKY),
        Phase::Hold if verifying => ("TIENI FERMO", "Riconoscimento in corso…", GREEN),
        Phase::Hold => ("TIENI FERMO", "Lettura in corso: non muovere il dito.", GREEN),
        Phase::Lift => ("SOLLEVA IL DITO", "Staccalo completamente, poi riappoggialo.", AMBER),
        Phase::Idle => return,
    };
    ui.label(RichText::new(title).size(30.0).color(color).strong());
    ui.label(RichText::new(sub).size(15.5).color(MUTED));
    if !state.hint.is_empty() {
        ui.label(RichText::new(&state.hint).size(15.5).color(AMBER).strong());
    }
    if state.timeout > 0.0 {
        ui.label(RichText::new(format!("Tempo massimo residuo: {:.0} s", state.remaining())).size(13.0).color(MUTED));
    }
}

fn seconds(a: Option<f64>, b: Option<f64>) -> Option<f64> {
    Some(b? - a?).filter(|v| *v >= 0.0)
}

impl Panel {
    fn with_channels(root: PathBuf, session: PathBuf, tx: SyncSender<Message>, rx: Receiver<Message>) -> Self {
        Self {
            plan_contents: read_plan_contents(&session),
            root,
            session,
            plan: None,
            pending: None,
            tab: Tab::Verify,
            selected_tool: 0,
            verify: State::default(),
            enroll: State::default(),
            manage: State::default(),
            tools: State::default(),
            running: None,
            error: String::new(),
            tx,
            rx,
            controls: None,
            worker: None,
            loading: false,
            last_poll: Instant::now(),
            closing: false,
            copy_requested: None,
            side: 1,
            finger: 1,
            enrolled: None,
            refresh_after_exit: false,
            confirm_delete: None,
            note_draft: String::new(),
            note_feedback: None,
        }
    }

    fn new(cc: &eframe::CreationContext<'_>, root: PathBuf, session: PathBuf) -> Self {
        cc.egui_ctx.set_visuals(egui::Visuals::dark());
        cc.egui_ctx.style_mut_of(egui::Theme::Dark, |style| {
            style.spacing.item_spacing = Vec2::new(12.0, 10.0);
            style.spacing.button_padding = Vec2::new(14.0, 8.0);
            style.text_styles.insert(egui::TextStyle::Heading, FontId::proportional(22.0));
            style.text_styles.insert(egui::TextStyle::Body, FontId::proportional(16.0));
            style.text_styles.insert(egui::TextStyle::Button, FontId::proportional(15.0));
            style.text_styles.insert(egui::TextStyle::Small, FontId::proportional(13.5));
            style.visuals.override_text_color = Some(TEXT);
            style.visuals.panel_fill = BG;
            style.visuals.window_fill = CARD;
            style.visuals.extreme_bg_color = BG;
            style.visuals.selection.bg_fill = SKY_DARK;
            style.visuals.hyperlink_color = SKY;
        });
        let (tx, rx) = mpsc::sync_channel(128);
        let mut panel = Self::with_channels(root, session, tx, rx);
        panel.loading = true;
        runner::load(panel.root.clone(), panel.session.clone(), panel.tx.clone(), cc.egui_ctx.clone());
        panel
    }

    fn load(&mut self, ctx: &egui::Context) {
        if self.loading {
            return;
        }
        self.loading = true;
        self.plan_contents = read_plan_contents(&self.session);
        self.last_poll = Instant::now();
        runner::load(self.root.clone(), self.session.clone(), self.tx.clone(), ctx.clone());
    }

    fn state_mut(&mut self, tab: Tab) -> &mut State {
        match tab {
            Tab::Verify => &mut self.verify,
            Tab::Enroll => &mut self.enroll,
            Tab::Manage => &mut self.manage,
            Tab::Tools => &mut self.tools,
        }
    }

    fn busy(&self) -> bool {
        self.worker.is_some()
    }

    /// Every run starts from an explicit click; the plan never autoruns.
    fn start(&mut self, ctx: &egui::Context, tab: Tab, index: usize, finger: Option<String>) {
        if self.busy() {
            return;
        }
        let Some(plan) = &self.plan else { return };
        let Some(task) = plan.plan.experiments.get(index) else { return };
        let (id, fingerprint) = (task.id.clone(), plan.fingerprint.clone());
        let state = self.state_mut(tab);
        *state = State::default();
        state.active = true;
        state.status = "Avvio in corso…".into();
        let (controls, handle) = runner::spawn(
            self.root.clone(),
            self.session.clone(),
            id,
            fingerprint,
            finger,
            self.tx.clone(),
            ctx.clone(),
        );
        self.controls = Some(controls);
        self.worker = Some(handle);
        self.running = Some(tab);
        self.error.clear();
    }

    fn refresh_list(&mut self, ctx: &egui::Context) {
        if let Some(index) = self.plan.as_ref().and_then(|p| find(p, Tab::Manage)) {
            self.start(ctx, Tab::Manage, index, None);
        }
    }

    fn stop(&self) {
        if let Some(tx) = &self.controls {
            let _ = tx.send(runner::control("cancel", None));
        }
    }

    fn save_note(&mut self, ctx: &egui::Context) {
        let text = self.note_draft.trim().to_string();
        if text.is_empty() {
            return;
        }
        if let Some(tx) = &self.controls {
            let ok = tx.send(runner::control("note", Some(&text))).is_ok();
            self.note_feedback = Some((Instant::now(), if ok { Ok(()) } else { Err("Backend non raggiungibile".into()) }));
        } else {
            runner::note(self.root.clone(), self.session.clone(), text, self.tx.clone(), ctx.clone());
        }
        self.note_draft.clear();
    }

    fn reset_states(&mut self) {
        self.verify = State::default();
        self.enroll = State::default();
        self.manage = State::default();
        self.tools = State::default();
        self.selected_tool = 0;
    }

    fn process(&mut self, ctx: &egui::Context) {
        while let Ok(msg) = self.rx.try_recv() {
            match msg {
                Message::Plan(result) => {
                    self.loading = false;
                    match result {
                        Ok(plan) => {
                            self.error.clear();
                            if self.plan.as_ref().is_none_or(|p| p.fingerprint != plan.fingerprint) {
                                if self.busy() {
                                    self.pending = Some(plan);
                                } else {
                                    if find(&plan, self.tab).is_none() && self.tab != Tab::Tools {
                                        self.tab = if find(&plan, Tab::Verify).is_some() { Tab::Verify } else { Tab::Tools };
                                    }
                                    self.plan = Some(plan);
                                    self.reset_states();
                                }
                            }
                        }
                        Err(e) => {
                            self.error = e;
                            if !self.busy() {
                                self.plan = None;
                            }
                        }
                    }
                }
                Message::Error(e) => {
                    self.error = e;
                    if let Some(tab) = self.running {
                        self.state_mut(tab).status = "Errore durante l'operazione".into();
                    }
                }
                Message::Event(e) => {
                    if e["type"] == "enrolled_fingers" {
                        self.enrolled = Some(
                            e["fingers"]
                                .as_array()
                                .map(|a| a.iter().filter_map(|v| v.as_str().map(String::from)).collect())
                                .unwrap_or_default(),
                        );
                    }
                    if e["type"] == "completed" && e["result"] == "enroll-completed" {
                        // New enrollment: re-read the enrolled list afterwards.
                        self.refresh_after_exit = true;
                    }
                    if let Some(tab) = self.running {
                        self.state_mut(tab).apply(e);
                    }
                }
                Message::NoteSaved(result) => {
                    self.note_feedback = Some((Instant::now(), result));
                }
                Message::Exited => {
                    self.controls.take();
                    if let Some(handle) = self.worker.take() {
                        let _ = handle.join();
                    }
                    if let Some(tab) = self.running.take() {
                        let state = self.state_mut(tab);
                        if state.active {
                            state.active = false;
                        }
                    }
                    if let Some(pending) = self.pending.take() {
                        self.plan = Some(pending);
                        self.reset_states();
                    }
                    if self.closing {
                        ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                    } else if std::mem::take(&mut self.refresh_after_exit) {
                        self.refresh_list(ctx);
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
        if ctx.input(|i| i.viewport().close_requested()) && self.busy() {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            self.closing = true;
            self.stop();
        }
        ctx.request_repaint_after(if self.busy() { Duration::from_millis(60) } else { Duration::from_secs(1) });
    }

    fn switch_tab(&mut self, ctx: &egui::Context, tab: Tab) {
        if self.tab == tab || self.busy() {
            return;
        }
        self.tab = tab;
        self.confirm_delete = None;
        // Opening the list is read-only: refresh it so the trash view is current.
        if tab == Tab::Manage {
            self.refresh_list(ctx);
        }
    }

    fn tabs_ui(&mut self, ui: &mut egui::Ui, ctx: &egui::Context, envelope: &Envelope) {
        let entries = [
            (Tab::Verify, "Verifica", Icon::Finger),
            (Tab::Enroll, "Registra", Icon::Play),
            (Tab::Manage, "Impronte", Icon::Trash),
            (Tab::Tools, "Strumenti", Icon::Note),
        ];
        ui.horizontal(|ui| {
            for (tab, label, icon) in entries {
                let available = tab == Tab::Tools || find(envelope, tab).is_some();
                if !available {
                    continue;
                }
                let selected = self.tab == tab;
                let fill = if selected { SKY_DARK } else { CARD_HI };
                if button(ui, Some(icon), label, fill, Vec2::new(150.0, 42.0), !self.busy() || selected).clicked() {
                    self.switch_tab(ctx, tab);
                }
            }
        });
    }

    fn verify_ui(&mut self, ui: &mut egui::Ui, ctx: &egui::Context, envelope: &Envelope) {
        let Some(index) = find(envelope, Tab::Verify) else { return };
        let running = self.running == Some(Tab::Verify);
        let state = &self.verify;
        let result = match state.result.as_str() {
            "verify-match" => Some(true),
            "verify-no-match" => Some(false),
            _ => None,
        };
        card(ui, |ui| {
            ui.vertical_centered(|ui| {
                ui.label(RichText::new("Verifica automatica").size(24.0).strong());
                ui.label(
                    RichText::new("Appoggia un dito qualsiasi: il sistema lo confronta con tutte le impronte registrate.")
                        .size(15.0)
                        .color(MUTED),
                );
                ui.add_space(8.0);
                fingerprint_scanner(ui, state.phase(), if state.active { None } else { result });
                ui.add_space(6.0);
                if state.active {
                    phase_banner(ui, state, true);
                } else if let Some(matched) = result {
                    let (title, color) = if matched { ("Impronta riconosciuta", GREEN) } else { ("Impronta non riconosciuta", RED) };
                    ui.label(RichText::new(title).size(28.0).color(color).strong());
                } else if !state.terminal.is_empty() {
                    ui.label(RichText::new(&state.status).size(22.0).color(AMBER).strong());
                    ui.label(RichText::new(&state.detail).size(14.5).color(MUTED));
                } else {
                    ui.label(RichText::new("Pronto").size(22.0).color(TEXT).strong());
                }
            });
        });
        if !state.active && result.is_some() {
            ui.add_space(10.0);
            card(ui, |ui| {
                ui.label(RichText::new("Dettagli dell’esito").size(17.0).strong());
                let finger = if !state.verify_finger.is_empty() && state.verify_finger != "any" {
                    finger_label(&state.verify_finger)
                } else if result == Some(true) {
                    match self.enrolled.as_deref() {
                        Some([only]) => format!("{} (unico dito registrato)", finger_label(only)),
                        _ => "Una delle dita registrate: fprintd non comunica quale".into(),
                    }
                } else {
                    "Nessuna corrispondenza".into()
                };
                egui::Grid::new("verify-details").num_columns(2).spacing([24.0, 8.0]).show(ui, |ui| {
                    ui.label(RichText::new("Dito").color(MUTED));
                    ui.label(RichText::new(finger).strong());
                    ui.end_row();
                    ui.label(RichText::new("Accuratezza").color(MUTED));
                    ui.label("Non disponibile: fprintd restituisce solo corrisponde / non corrisponde, senza punteggio");
                    ui.end_row();
                    if let Some(open) = state.op_started_at {
                        ui.label(RichText::new("Apertura sensore").color(MUTED));
                        ui.label(format!("{open:.1} s (dall’avvio a «pronto»)"));
                        ui.end_row();
                    }
                    if let Some(match_time) = seconds(state.first_present_at, state.result_at) {
                        ui.label(RichText::new("Dal contatto all’esito").color(MUTED));
                        ui.label(format!("{match_time:.1} s (contatto letto ogni 0,5 s)"));
                        ui.end_row();
                    }
                    if let Some(total) = state.result_at {
                        ui.label(RichText::new("Totale").color(MUTED));
                        ui.label(format!("{total:.1} s"));
                        ui.end_row();
                    }
                });
            });
        }
        ui.add_space(12.0);
        ui.horizontal(|ui| {
            if running {
                if button(ui, Some(Icon::Stop), "Interrompi", RED_DARK, Vec2::new(170.0, 46.0), true).clicked() {
                    self.stop();
                }
            } else {
                let label = if self.verify.terminal.is_empty() { "Verifica impronta" } else { "Verifica di nuovo" };
                let can = !self.busy() && self.error.is_empty();
                if button(ui, Some(Icon::Play), label, SKY_DARK, Vec2::new(230.0, 46.0), can).clicked() {
                    self.start(ctx, Tab::Verify, index, None);
                }
            }
        });
    }

    fn enroll_ui(&mut self, ui: &mut egui::Ui, ctx: &egui::Context, envelope: &Envelope) {
        let Some(index) = find(envelope, Tab::Enroll) else { return };
        let running = self.running == Some(Tab::Enroll);
        let selected_id = format!("{}-{}", SIDES[self.side].0, FINGERS[self.finger].0);
        let enabled = !self.busy();
        card(ui, |ui| {
            ui.label(RichText::new("1 · Scegli la mano").size(17.0).strong());
            ui.horizontal(|ui| {
                for (i, (_, label)) in SIDES.iter().enumerate() {
                    if choice(ui, label, "", self.side == i, Vec2::new(210.0, 50.0), enabled).clicked() {
                        self.side = i;
                    }
                }
            });
            ui.add_space(8.0);
            ui.label(RichText::new("2 · Scegli il dito").size(17.0).strong());
            ui.horizontal_wrapped(|ui| {
                for (i, (key, label)) in FINGERS.iter().enumerate() {
                    let id = format!("{}-{key}", SIDES[self.side].0);
                    let done = self.enrolled.as_ref().is_some_and(|e| e.contains(&id));
                    if choice(ui, label, if done { "registrato" } else { "" }, self.finger == i, Vec2::new(150.0, 56.0), enabled)
                        .clicked()
                    {
                        self.finger = i;
                    }
                }
            });
        });
        ui.add_space(10.0);
        let state = &self.enroll;
        card(ui, |ui| {
            ui.vertical_centered(|ui| {
                let title = if state.finger.is_empty() { finger_label(&selected_id) } else { finger_label(&state.finger) };
                ui.label(RichText::new(format!("3 · Registra: {title}")).size(20.0).strong());
                let ok = state.result == "enroll-completed";
                fingerprint_scanner(ui, state.phase(), if state.active || state.terminal.is_empty() { None } else { Some(ok) });
                if state.active {
                    phase_banner(ui, state, false);
                } else if ok {
                    ui.label(RichText::new("Registrazione completata").size(26.0).color(GREEN).strong());
                } else if !state.terminal.is_empty() {
                    ui.label(RichText::new(&state.status).size(22.0).color(RED).strong());
                    ui.label(RichText::new(&state.detail).size(14.5).color(MUTED));
                } else {
                    ui.label(
                        RichText::new("Premi «Registra»: ti dirò io quando appoggiare e quando sollevare il dito.")
                            .size(15.0)
                            .color(MUTED),
                    );
                }
                if state.total > 0 {
                    ui.add_space(6.0);
                    ui.add(
                        egui::ProgressBar::new((state.completed as f32 / state.total as f32).min(1.0))
                            .fill(SKY_DARK)
                            .desired_width(360.0)
                            .text(format!("{} di {} appoggi", state.completed, state.total)),
                    );
                }
            });
        });
        ui.add_space(12.0);
        let already = self.enrolled.as_ref().is_some_and(|e| e.contains(&selected_id));
        ui.horizontal(|ui| {
            if running {
                if button(ui, Some(Icon::Stop), "Interrompi", RED_DARK, Vec2::new(170.0, 46.0), true).clicked() {
                    self.stop();
                }
            } else {
                let label = if already { "Sostituisci impronta" } else { "Registra" };
                let can = !self.busy() && self.error.is_empty();
                if button(ui, Some(Icon::Play), label, SKY_DARK, Vec2::new(230.0, 46.0), can).clicked() {
                    self.start(ctx, Tab::Enroll, index, Some(selected_id.clone()));
                }
                if already {
                    ui.label(RichText::new("Questo dito è già registrato: verrà sostituito.").color(AMBER));
                }
            }
        });
    }

    fn manage_ui(&mut self, ui: &mut egui::Ui, ctx: &egui::Context, envelope: &Envelope) {
        let delete = find_delete(envelope);
        let busy = self.busy();
        card(ui, |ui| {
            ui.horizontal(|ui| {
                ui.label(RichText::new("Impronte registrate").size(22.0).strong());
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if button(ui, Some(Icon::Refresh), "Aggiorna", CARD_HI, Vec2::new(130.0, 36.0), !busy).clicked() {
                        self.refresh_list(ctx);
                    }
                });
            });
            ui.add_space(6.0);
            match self.enrolled.clone() {
                None if self.manage.active => {
                    ui.label(RichText::new("Lettura dell’elenco…").color(MUTED));
                }
                None => {
                    ui.label(RichText::new("Elenco non ancora letto. Premi «Aggiorna».").color(MUTED));
                    if !self.manage.terminal.is_empty() {
                        ui.label(RichText::new(&self.manage.detail).color(RED));
                    }
                }
                Some(list) if list.is_empty() => {
                    ui.label(RichText::new("Nessuna impronta registrata per questo utente.").color(MUTED));
                }
                Some(list) => {
                    for id in list {
                        egui::Frame::new()
                            .inner_margin(14)
                            .corner_radius(12.0)
                            .fill(CARD_HI)
                            .stroke(Stroke::new(1.0, BORDER))
                            .show(ui, |ui| {
                                ui.set_width(ui.available_width());
                                ui.horizontal(|ui| {
                                    let (r, _) = ui.allocate_exact_size(Vec2::splat(28.0), Sense::hover());
                                    draw_icon(ui.painter(), Icon::Finger, r.center(), SKY);
                                    ui.vertical(|ui| {
                                        ui.label(RichText::new(finger_label(&id)).size(17.0).strong());
                                        ui.label(RichText::new(&id).size(12.5).color(MUTED));
                                    });
                                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                                        let Some(delete) = delete else {
                                            ui.label(RichText::new("Eliminazione non prevista dal piano").color(MUTED));
                                            return;
                                        };
                                        if self.confirm_delete.as_deref() == Some(id.as_str()) {
                                            if button(ui, Some(Icon::Cross), "Annulla", CARD_HI, Vec2::new(110.0, 36.0), !busy).clicked() {
                                                self.confirm_delete = None;
                                            }
                                            if button(ui, Some(Icon::Trash), "Elimina", RED_DARK, Vec2::new(120.0, 36.0), !busy).clicked() {
                                                self.confirm_delete = None;
                                                self.refresh_after_exit = true;
                                                self.start(ctx, Tab::Manage, delete, Some(id.clone()));
                                            }
                                            ui.label(RichText::new("Confermi?").color(AMBER).strong());
                                        } else if button(ui, Some(Icon::Trash), "", CARD, Vec2::new(44.0, 36.0), !busy)
                                            .on_hover_text("Elimina questa impronta")
                                            .clicked()
                                        {
                                            self.confirm_delete = Some(id.clone());
                                        }
                                    });
                                });
                            });
                    }
                }
            }
            if !self.manage.finger.is_empty() && !self.manage.terminal.is_empty() {
                let ok = self.manage.terminal == "completed";
                ui.label(
                    RichText::new(if ok {
                        format!("{} eliminata.", finger_label(&self.manage.finger))
                    } else {
                        format!("Eliminazione non riuscita: {}", self.manage.detail)
                    })
                    .color(if ok { GREEN } else { RED }),
                );
            }
        });
    }

    fn tools_ui(&mut self, ui: &mut egui::Ui, ctx: &egui::Context, envelope: &Envelope) {
        let tools: Vec<usize> = (0..envelope.plan.experiments.len()).filter(|i| is_tool(envelope, *i)).collect();
        if tools.is_empty() {
            card(ui, |ui| ui.label(RichText::new("Nessun altro esperimento nel piano.").color(MUTED)));
            return;
        }
        if !tools.contains(&self.selected_tool) {
            self.selected_tool = tools[0];
        }
        ui.horizontal_wrapped(|ui| {
            for &i in &tools {
                let task = &envelope.plan.experiments[i];
                let fill = if self.selected_tool == i { SKY_DARK } else { CARD_HI };
                let width = (task.title.chars().count() as f32 * 8.5 + 40.0).clamp(140.0, 360.0);
                if button(ui, None, &task.title, fill, Vec2::new(width, 38.0), !self.busy()).clicked() {
                    self.selected_tool = i;
                    self.tools = State::default();
                }
            }
        });
        ui.add_space(8.0);
        let index = self.selected_tool;
        let task = &envelope.plan.experiments[index];
        let sim = task.adapter == "simulation";
        let fprintd = task.adapter == "fprintd";
        let running = self.running == Some(Tab::Tools);
        let state = &self.tools;
        card(ui, |ui| {
            ui.label(RichText::new(&task.title).size(22.0).strong());
            ui.label(
                RichText::new(if sim {
                    "Dimostrazione simulata · nessun sensore aperto, non toccare il lettore"
                } else if fprintd {
                    "Sensore reale via fprintd"
                } else {
                    "Comando locale"
                })
                .color(if sim { AMBER } else { SKY })
                .strong(),
            );
            ui.add_space(6.0);
            ui.vertical_centered(|ui| {
                if sim {
                    simulation_visual(ui, state);
                } else if fprintd {
                    fingerprint_scanner(ui, state.phase(), None);
                    phase_banner(ui, state, program(task) == "fprintd-verify");
                }
                let color = match state.terminal.as_str() {
                    "completed" => GREEN,
                    "" if state.active => AMBER,
                    "" => TEXT,
                    _ => RED,
                };
                ui.label(RichText::new(&state.status).size(21.0).color(color).strong());
            });
            ui.label(
                RichText::new(if state.run_id.is_empty() { &task.instructions } else { &state.detail })
                    .size(15.0)
                    .color(Color32::from_rgb(203, 213, 225)),
            );
            if state.total > 0 {
                ui.add(
                    egui::ProgressBar::new((state.completed as f32 / state.total as f32).min(1.0))
                        .fill(SKY_DARK)
                        .text(format!("{} / {}", state.completed, state.total)),
                );
            }
            if !state.next_steps.is_empty() && !state.active {
                ui.label(RichText::new(&state.next_steps).color(GREEN));
            }
        });
        ui.add_space(10.0);
        ui.horizontal(|ui| {
            if running {
                if button(ui, Some(Icon::Stop), "Interrompi", RED_DARK, Vec2::new(170.0, 44.0), true).clicked() {
                    self.stop();
                }
                if fprintd && button(ui, Some(Icon::Check), "Conferma manuale", CARD_HI, Vec2::new(200.0, 44.0), true).clicked() {
                    if let Some(tx) = &self.controls {
                        let _ = tx.send(runner::control("confirm", None));
                    }
                }
            } else {
                let can = !self.busy() && self.error.is_empty() && !has_finger(task);
                let label = if self.tools.run_id.is_empty() { "Esegui" } else { "Esegui di nuovo" };
                if button(ui, Some(Icon::Play), label, SKY_DARK, Vec2::new(200.0, 44.0), can).clicked() {
                    self.start(ctx, Tab::Tools, index, None);
                }
            }
        });
        ui.collapsing("Comando e dettagli tecnici", |ui| {
            let command = task.argv.iter().map(|a| shell_quote(a)).collect::<Vec<_>>().join(" ");
            ui.monospace(&command);
            ui.label(RichText::new(format!("Cartella: {}", self.root.join(&task.cwd).display())).size(13.5));
            ui.label(RichText::new(format!("Categoria: {} · timeout {} s", task.category, task.timeout_seconds)).size(13.5));
            if button(ui, Some(Icon::Note), "Copia comando", CARD_HI, Vec2::new(170.0, 34.0), true).clicked() {
                ctx.copy_text(command);
                self.copy_requested = Some(Instant::now());
            }
        });
    }

    fn note_ui(&mut self, ui: &mut egui::Ui, ctx: &egui::Context) {
        card(ui, |ui| {
            ui.label(RichText::new("Nota per l’agente").size(16.0).strong());
            ui.label(
                RichText::new("Facoltativa: finisce nel log della prova in corso o dell’ultima conclusa.")
                    .size(13.5)
                    .color(MUTED),
            );
            ui.horizontal(|ui| {
                let edit = egui::TextEdit::singleline(&mut self.note_draft)
                    .hint_text("es. dito sbagliato / nessun segnale / ho sollevato troppo presto")
                    .char_limit(500)
                    .desired_width(ui.available_width() - 170.0);
                let response = ui.add(edit);
                let enter = response.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter));
                let can = !self.note_draft.trim().is_empty();
                if button(ui, Some(Icon::Note), "Registra nota", CARD_HI, Vec2::new(160.0, 36.0), can).clicked() || (enter && can) {
                    self.save_note(ctx);
                }
            });
            if let Some((time, result)) = &self.note_feedback {
                if time.elapsed() < Duration::from_secs(5) {
                    match result {
                        Ok(()) => ui.label(RichText::new("Nota salvata nel log.").color(GREEN)),
                        Err(e) => ui.label(RichText::new(format!("Nota non salvata: {e}")).color(RED)),
                    };
                }
            }
        });
    }
}

impl eframe::App for Panel {
    fn logic(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.process(ctx);
    }
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        egui::CentralPanel::default()
            .frame(egui::Frame::new().inner_margin(24).fill(BG))
            .show(ui, |ui| {
                egui::ScrollArea::vertical().show(ui, |ui| {
                    ui.horizontal(|ui| {
                        ui.label(RichText::new("REA-FS").size(26.0).strong());
                        ui.label(RichText::new("Impronte digitali").size(17.0).color(MUTED));
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            if button(ui, Some(Icon::Refresh), "Ricarica piano", CARD_HI, Vec2::new(170.0, 36.0), !self.loading)
                                .clicked()
                            {
                                self.load(&ctx);
                            }
                        });
                    });
                    ui.add_space(8.0);
                    if !self.error.is_empty() {
                        ui.colored_label(RED, RichText::new(&self.error).size(15.0).strong());
                    }
                    let Some(envelope) = self.plan.clone() else {
                        ui.heading("Caricamento del piano…");
                        ui.label("Nessuna azione parte automaticamente.");
                        ui.monospace(self.session.join("plan.json").display().to_string());
                        return;
                    };
                    self.tabs_ui(ui, &ctx, &envelope);
                    ui.add_space(12.0);
                    match self.tab {
                        Tab::Verify => self.verify_ui(ui, &ctx, &envelope),
                        Tab::Enroll => self.enroll_ui(ui, &ctx, &envelope),
                        Tab::Manage => self.manage_ui(ui, &ctx, &envelope),
                        Tab::Tools => self.tools_ui(ui, &ctx, &envelope),
                    }
                    ui.add_space(14.0);
                    self.note_ui(ui, &ctx);
                    if self.pending.is_some() {
                        ui.colored_label(AMBER, "Piano aggiornato: la prova attiva conserva la revisione iniziale.");
                    }
                    if self.copy_requested.is_some_and(|t| t.elapsed() < Duration::from_secs(3)) {
                        ui.colored_label(GREEN, "Testo copiato negli appunti.");
                    }
                    ui.add_space(6.0);
                    ui.collapsing("Log per l’agente", |ui| {
                        ui.label(RichText::new("Ogni prova è salvata automaticamente: non serve copiare nulla in chat.").color(MUTED));
                        ui.monospace(self.session.join("runs-index.jsonl").display().to_string());
                        ui.monospace(self.session.join("latest-run.json").display().to_string());
                        ui.label(RichText::new(format!("Piano: {} · revisione {}", envelope.plan.plan_id, envelope.plan.revision)).size(13.5));
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
    let session = if session.is_absolute() { session } else { root.join(session) };
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_app_id("rea-fs-panel")
            .with_title("REA-FS · Impronte digitali")
            .with_inner_size([960.0, 780.0])
            .with_min_inner_size([820.0, 640.0]),
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
    fn envelope(experiments: serde_json::Value) -> Envelope {
        serde_json::from_value(serde_json::json!({
            "fingerprint": "new",
            "plan": {"plan_id": "new", "title": "Hardware", "revision": 2, "experiments": experiments}
        }))
        .unwrap()
    }
    fn panel() -> Panel {
        let (tx, rx) = mpsc::sync_channel(128);
        Panel::with_channels(PathBuf::new(), PathBuf::new(), tx, rx)
    }
    #[test]
    fn new_plan_clears_previous_run_for_immediate_and_deferred_updates() {
        for mode in 0..3 {
            let mut panel = panel();
            panel.tools.run_id = "old-simulation".into();
            panel.tools.simulation = true;
            panel.tools.run_directory = "old-log".into();
            let envelope = envelope(serde_json::json!([]));
            if mode == 1 {
                panel.pending = Some(envelope);
                panel.tx.send(Message::Exited).unwrap();
            } else if mode == 2 {
                panel.worker = Some(std::thread::spawn(|| {}));
                panel.tx.send(Message::Plan(Ok(envelope))).unwrap();
                panel.process(&egui::Context::default());
                assert!(panel.pending.is_some());
                assert_eq!(panel.tools.run_id, "old-simulation");
                panel.tx.send(Message::Exited).unwrap();
            } else {
                panel.tx.send(Message::Plan(Ok(envelope))).unwrap();
            }
            panel.process(&egui::Context::default());
            assert!(panel.tools.run_id.is_empty());
            assert!(panel.tools.run_directory.is_empty());
            assert!(!panel.tools.simulation);
        }
    }
    #[test]
    fn fingerprint_tabs_are_derived_from_plan_programs() {
        let task = |id: &str, argv: serde_json::Value, adapter: &str, params: serde_json::Value| {
            serde_json::json!({"id": id, "title": id, "instructions": "x", "argv": argv, "cwd": ".",
                               "timeout_seconds": 5, "adapter": adapter, "category": "c", "parameters": params})
        };
        let plan = envelope(serde_json::json!([
            task("v", serde_json::json!(["fprintd-verify"]), "fprintd", serde_json::json!([])),
            task("e", serde_json::json!(["fprintd-enroll", "-f", "{finger}"]), "fprintd", serde_json::json!(["finger"])),
            task("l", serde_json::json!(["fprintd-list", "{user}"]), "plain", serde_json::json!([])),
            task("d", serde_json::json!(["fprintd-delete", "{user}", "-f", "{finger}"]), "plain", serde_json::json!(["finger"])),
            task("s", serde_json::json!(["python3", "x"]), "simulation", serde_json::json!([])),
        ]));
        assert_eq!(find(&plan, Tab::Verify), Some(0));
        assert_eq!(find(&plan, Tab::Enroll), Some(1));
        assert_eq!(find(&plan, Tab::Manage), Some(2));
        assert_eq!(find_delete(&plan), Some(3));
        assert!((0..4).all(|i| !is_tool(&plan, i)));
        assert!(is_tool(&plan, 4));
        assert_eq!(finger_label("right-index-finger"), "Indice destro");
        assert_eq!(finger_label("left-thumb"), "Pollice sinistro");
    }
    #[test]
    fn control_lines_escape_user_text() {
        let line = runner::control("note", Some("a\"b\nc"));
        let value: serde_json::Value = serde_json::from_str(&line).unwrap();
        assert_eq!(value["text"], "a\"b\nc");
        assert!(!line.contains('\n'));
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
        assert_eq!(read_plan_contents(&directory).unwrap().len(), 256 * 1024 + 1);
        std::fs::remove_dir_all(directory).unwrap();
    }
}
