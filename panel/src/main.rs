mod model;
mod runner;
use eframe::egui::{self, Color32, RichText, Stroke, Vec2};
use model::{Envelope, State, shell_quote};
use runner::Message;
use std::io::Read;
use std::path::PathBuf;
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
}
// Bound idle file reads by the same limit used by the validator. Validation is
// still repeated by the backend immediately before spawning an experiment.
fn read_plan_contents(session: &std::path::Path) -> Option<Vec<u8>> {
    let file = std::fs::File::open(session.join("plan.json")).ok()?;
    let mut contents = Vec::new();
    file.take(256 * 1024 + 1).read_to_end(&mut contents).ok()?;
    Some(contents)
}

impl Panel {
    fn new(cc: &eframe::CreationContext<'_>, root: PathBuf, session: PathBuf) -> Self {
        cc.egui_ctx.set_visuals(egui::Visuals::light());
        cc.egui_ctx.style_mut_of(egui::Theme::Light, |style| {
            style.spacing.item_spacing = Vec2::new(12.0, 12.0);
            style.spacing.button_padding = Vec2::new(14.0, 10.0);
            style
                .text_styles
                .insert(egui::TextStyle::Body, egui::FontId::proportional(16.0));
            style
                .text_styles
                .insert(egui::TextStyle::Button, egui::FontId::proportional(15.0));
            style
                .text_styles
                .insert(egui::TextStyle::Small, egui::FontId::proportional(13.0));
            style.visuals.override_text_color = Some(Color32::from_rgb(26, 45, 60));
            style.visuals.panel_fill = Color32::from_rgb(246, 248, 251);
            style.visuals.selection.bg_fill = Color32::from_rgb(210, 229, 254);
            style.visuals.hyperlink_color = Color32::from_rgb(25, 94, 200);
        });
        let (tx, rx) = mpsc::sync_channel(128);
        let plan_contents = read_plan_contents(&session);
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
                self.state.status = "Avvio della prova…".into();
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
        while let Ok(message) = self.rx.try_recv() {
            match message {
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
                        Err(error) => {
                            self.error = error;
                            if self.worker.is_none() {
                                self.plan = None;
                            }
                        }
                    }
                }
                Message::Event(event) => self.state.apply(event),
                Message::Error(error) => {
                    self.stop();
                    self.controls.take();
                    self.state.fail(error);
                }
                Message::Exited => {
                    self.controls.take();
                    if let Some(handle) = self.worker.take() {
                        let _ = handle.join();
                    }
                    if self.state.active {
                        self.state
                            .fail("Backend terminato senza un esito valido".into());
                    }
                    if let Some(plan) = self.pending.take() {
                        self.plan = Some(plan);
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
fn fingerprint(ui: &mut egui::Ui, color: Color32) {
    let (rect, _) = ui.allocate_exact_size(Vec2::new(176.0, 160.0), egui::Sense::hover());
    let c = rect.center();
    ui.painter()
        .circle_filled(c, 77.0, Color32::from_rgb(235, 244, 242));
    for ridge in 0..10 {
        let radius = 10.0 + ridge as f32 * 4.2;
        let points = (0..90)
            .map(|i| {
                let t = i as f32 / 89.0;
                let angle = -0.2 + t * 4.9;
                c + Vec2::new(
                    radius * angle.cos() + 4.0 * (t * 3.0).sin(),
                    radius * 1.18 * angle.sin(),
                )
            })
            .collect();
        ui.painter()
            .add(egui::Shape::line(points, Stroke::new(1.8, color)));
    }
}
fn simulation_visual(ui: &mut egui::Ui, state: &State) {
    let (rect, _) = ui.allocate_exact_size(Vec2::new(250.0, 110.0), egui::Sense::hover());
    let y = rect.center().y;
    let painter = ui.painter();
    painter.line_segment(
        [
            egui::pos2(rect.left() + 35.0, y),
            egui::pos2(rect.right() - 35.0, y),
        ],
        Stroke::new(2.0, Color32::from_rgb(211, 219, 226)),
    );
    for index in 0..3 {
        let center = egui::pos2(rect.left() + 35.0 + index as f32 * 90.0, y);
        let done = state.completed > index;
        painter.circle_filled(
            center,
            20.0,
            if done {
                Color32::from_rgb(165, 113, 28)
            } else {
                Color32::from_rgb(232, 237, 242)
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
                Color32::from_rgb(82, 99, 115)
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
        egui::CentralPanel::default().frame(egui::Frame::new().inner_margin(22).fill(Color32::from_rgb(246,248,251))).show(ui, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
                ui.horizontal(|ui| {
                    ui.heading(RichText::new("REA-FS").size(25.0));
                    ui.label("Banco prove");
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        if ui.add_enabled(!self.loading,egui::Button::new("Ricarica piano")).clicked(){self.load(&ctx);}
                    });
                });
                ui.add_space(12.0);
                if !self.error.is_empty(){ui.colored_label(Color32::from_rgb(165,38,38),&self.error);}
                let Some(envelope)=self.plan.clone() else {
                    ui.heading("L’agente sta preparando la prova");
                    ui.label("Nessuna azione parte automaticamente.");
                    ui.monospace(self.session.join("plan.json").display().to_string());
                    return;
                };
                ui.horizontal_top(|ui| {
                    ui.allocate_ui_with_layout(Vec2::new(245.0,0.0),egui::Layout::top_down(egui::Align::Min),|ui| {
                        ui.strong("Scegli la prova");
                        for (index,task) in envelope.plan.experiments.iter().enumerate() {
                            if ui.add_enabled(self.worker.is_none(),egui::Button::selectable(self.selected==index,&task.title).min_size(Vec2::new(230.0,44.0))).clicked(){self.selected=index;self.state=State::default();}
                        }
                        ui.add_space(12.0);
                        ui.small(&envelope.plan.title);
                        ui.small(format!("Revisione {}",envelope.plan.revision));
                        ui.small("L’agente prepara le prove. Tu avvii quella concordata e segui le istruzioni.");
                    });
                    ui.add_space(16.0);
                    ui.vertical(|ui| {
                        ui.set_width(ui.available_width());
                        let task=&envelope.plan.experiments[self.selected];
                        let sim=task.adapter=="simulation";
                        let accent=if sim{Color32::from_rgb(145,94,18)}else{Color32::from_rgb(23,105,93)};
                        ui.heading(RichText::new(&task.title).size(24.0));
                        ui.colored_label(accent,RichText::new(if sim{"Dimostrazione • il lettore non viene usato"}else if task.adapter=="plain"{"Controllo offline • nessuno stato di contatto"}else{"Prova hardware • attendi lo stato osservato"}).strong());
                        if sim {ui.label("Non appoggiare il dito. I contatti e i rilasci qui sotto sono generati dal programma.");}
                        ui.add_space(8.0);
                        ui.vertical_centered(|ui| {
                            if sim {simulation_visual(ui,&self.state);}
                            else if task.adapter=="fprintd" {
                                let color=if self.state.present{accent}else if self.state.needed{Color32::from_rgb(43,104,184)}else{Color32::from_rgb(130,148,160)};
                                fingerprint(ui,color);
                                ui.small("Indicatore di contatto, non immagine o mappa del dito.");
                            }
                            ui.label(RichText::new(&self.state.status).size(23.0).strong());
                        });
                        ui.add_space(8.0);
                        ui.label(if self.worker.is_some() || !self.state.run_id.is_empty(){&self.state.detail}else{&task.instructions});
                        if self.state.total>0 {
                            let unit=if sim{"passaggi simulati"}else{"appoggi"};
                            ui.add(egui::ProgressBar::new((self.state.completed as f32/self.state.total as f32).min(1.0)).fill(accent).text(format!("{} / {} {}",self.state.completed,self.state.total,unit)));
                        }
                        if self.state.active {ui.small(format!("Tempo residuo: {:.0} s",self.state.remaining()));}
                        else if self.state.run_id.is_empty(){ui.small(format!("Durata massima: {} s",task.timeout_seconds));}
                        ui.add_space(8.0);
                        ui.horizontal_wrapped(|ui| {
                            if ui.add_enabled(self.worker.is_none() && self.error.is_empty(),egui::Button::new(RichText::new(if sim{"Avvia dimostrazione"}else{"Esegui la prova"}).color(Color32::WHITE).strong()).fill(accent).min_size(Vec2::new(170.0,44.0))).clicked(){self.start(&ctx);}
                            if ui.add_enabled(self.worker.is_some(),egui::Button::new("Interrompi").min_size(Vec2::new(110.0,44.0))).clicked(){self.stop();}
                        });
                        if self.worker.is_some() && task.adapter=="fprintd" && ui.button("Registra conferma manuale").clicked(){if let Some(tx)=&self.controls {let _=tx.send("confirm".into());}}
                        ui.add_space(8.0);
                        ui.collapsing("Comando e dettagli della prova", |ui| {
                            let command=task.argv.iter().map(|a|shell_quote(a)).collect::<Vec<_>>().join(" ");
                            ui.monospace(&command);
                            ui.small(self.root.join(&task.cwd).display().to_string());
                            ui.small(format!("Categoria: {}",task.category));
                            if task.raw_log{ui.small("Anche stdout e stderr vengono salvati nei log privati.");}
                            if ui.button("Copia comando").clicked(){ctx.copy_text(command);self.copy_requested=Some(Instant::now());}
                        });
                    });
                });
                ui.add_space(16.0);
                ui.separator();
                if self.pending.is_some(){ui.colored_label(Color32::from_rgb(143,85,8),"Piano aggiornato: la prova attiva conserva la revisione iniziale.");}
                if !self.state.run_directory.is_empty() && self.worker.is_none(){
                    ui.strong("Risultato salvato • l’agente può continuare");
                    ui.label(if self.state.next_steps.is_empty(){"L’agente può leggere i file della sessione. Non serve copiare l’output."}else{&self.state.next_steps});
                    ui.small(format!("Riepilogo: {}/summary.json",self.state.run_directory));
                    if ui.button("Copia messaggio per l’agente").clicked(){ctx.copy_text(format!("Leggi {}/handoff.txt e riprendi dai log privati della prova.",self.state.run_directory));self.copy_requested=Some(Instant::now());}
                } else {ui.small("Log salvati automaticamente. Nessuna copia manuale necessaria per l’agente che legge questa cartella.");}
                if self.copy_requested.is_some_and(|time|time.elapsed()<Duration::from_secs(3)){ui.small("Testo inviato agli appunti.");}
                ui.collapsing("Log e informazioni per l’agente",|ui| {
                    ui.monospace(self.session.join("latest-run.json").display().to_string());
                    if !self.state.run_directory.is_empty(){ui.monospace(&self.state.run_directory);}
                    for event in self.state.events.iter().rev().take(8){ui.small(event);}
                    ui.small(format!("Piano: {}",envelope.plan.plan_id));
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
            .with_inner_size([940.0, 710.0])
            .with_min_inner_size([820.0, 620.0]),
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
            };
            panel.state.run_id = "old-simulation".into();
            panel.state.simulation = true;
            panel.state.run_directory = "old-log".into();
            let envelope:Envelope=serde_json::from_value(serde_json::json!({"fingerprint":"new","plan":{"plan_id":"new","title":"Hardware","revision":2,"experiments":[]}})).unwrap();
            if mode == 1 {
                panel.pending = Some(envelope);
                panel.tx.send(Message::Exited).unwrap();
            } else if mode == 2 {
                // Error has closed controls, but cleanup has not sent Exited yet.
                panel.worker=Some(std::thread::spawn(||{}));
                panel.tx.send(Message::Plan(Ok(envelope))).unwrap();
                panel.process(&egui::Context::default());
                assert!(panel.pending.is_some());
                assert_eq!(panel.state.run_id,"old-simulation");
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
