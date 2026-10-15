mod model;
mod runner;
use eframe::egui::{self, Color32, RichText, Stroke, Vec2};
use model::{Envelope, State, shell_quote};
use runner::Message;
use std::path::PathBuf;
use std::io::Read;
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
            style.spacing.item_spacing = Vec2::new(10.0, 10.0);
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
                                if self.controls.is_some() {
                                    self.pending = Some(plan);
                                } else {
                                    self.plan = Some(plan);
                                    self.selected = 0;
                                }
                            }
                        }
                        Err(error) => {
                            self.error = error;
                            if self.controls.is_none() {
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
    let (rect, _) = ui.allocate_exact_size(Vec2::new(126.0, 145.0), egui::Sense::hover());
    let c = rect.center();
    for ridge in 0..9 {
        let radius = 9.0 + ridge as f32 * 5.0;
        let mut points = vec![c + Vec2::new(-radius, 30.0 - ridge as f32)];
        points.extend((0..60).map(|i| {
            let angle = std::f32::consts::PI * (1.0 + i as f32 / 59.0);
            c + Vec2::new(radius * angle.cos(), radius * 1.17 * angle.sin())
        }));
        points.push(c + Vec2::new(radius, 38.0 - ridge as f32 * 1.8));
        ui.painter()
            .add(egui::Shape::line(points, Stroke::new(2.0, color)));
    }
}
impl eframe::App for Panel {
    fn logic(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.process(ctx);
    }
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        egui::CentralPanel::default().show(ui,|ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
            ui.add_space(8.0);
            ui.horizontal(|ui| {
                ui.heading("REA-FS");ui.label("Esperimenti assistiti");
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center),|ui| {
                    if ui.add_enabled(!self.loading,egui::Button::new("Ricarica piano")).clicked(){self.load(&ctx);}
                    if let Some(plan)=&self.plan {ui.small(format!("Revisione {}",plan.plan.revision));}
                });
            });
            ui.separator();
            if !self.error.is_empty(){ui.colored_label(Color32::from_rgb(165,38,38),&self.error);}
            if self.plan.is_none() {
                ui.add_space(24.0);ui.heading("Apri un piano di esperimenti");
                ui.label("L’agente prepara plan.json nella cartella della sessione. Nessuna azione parte automaticamente.");
                ui.monospace(self.session.join("plan.json").display().to_string());
                return;
            }
            let envelope=self.plan.as_ref().expect("checked plan").clone();
            ui.small(format!("{} · {}",envelope.plan.title,envelope.plan.plan_id));
            ui.columns(2,|columns| {
                let left=&mut columns[0];
                left.set_max_width(290.0);
                left.add_space(12.0);left.strong("Scegli una prova");
                for (index,task) in envelope.plan.experiments.iter().enumerate() {
                    if left.add_enabled(self.worker.is_none(),egui::Button::selectable(self.selected==index,&task.title).min_size(Vec2::new(260.0,35.0))).clicked(){self.selected=index;self.state=State::default();}
                }
                left.add_space(14.0);
                left.small("Le istruzioni e i comandi provengono dal piano locale. Controllali prima di eseguire.");
                let task=&envelope.plan.experiments[self.selected];
                left.add_space(10.0);left.label(format!("Durata massima: {} s",task.timeout_seconds));
                if task.raw_log {left.colored_label(Color32::from_rgb(143,85,8),"Questa prova salva anche l’output grezzo in un file privato.");}
                left.add_space(10.0);
                let command=task.argv.iter().map(|a|shell_quote(a)).collect::<Vec<_>>().join(" ");
                egui::CollapsingHeader::new("Comando e directory").default_open(true).show(left,|ui| {
                    ui.monospace(&command);ui.monospace(self.root.join(&task.cwd).display().to_string());
                    ui.small(format!("Categoria: {}",task.category));
                    if ui.button("Copia comando").clicked(){ui.ctx().copy_text(command.clone());}
                });
                let right=&mut columns[1];
                right.vertical_centered(|ui| {
                    ui.label(RichText::new(&task.title).size(18.0).strong());
                    if task.adapter == "plain" {
                        ui.small("Comando senza eventi di contatto: l’icona non segue il sensore.");
                    }
                    let sim=if self.worker.is_some(){self.state.simulation}else{task.adapter=="simulation"};
                    if sim {ui.colored_label(Color32::from_rgb(143,85,8),RichText::new("Simulazione · nessun sensore aperto").strong());}
                    let color=if self.state.present{Color32::from_rgb(23,122,110)}else if self.state.needed{Color32::from_rgb(25,94,200)}else{Color32::from_rgb(142,156,174)};
                    fingerprint(ui,color);
                    ui.label(RichText::new(&self.state.status).size(25.0).strong());
                    ui.add_space(8.0);
                    ui.label(if self.worker.is_some() || !self.state.run_id.is_empty(){&self.state.detail}else{&task.instructions});
                    if self.state.active && self.state.timeout>0.0 {ui.label(format!("Tempo residuo: {:.0} s",self.state.remaining()));}
                    if self.state.total>0 {
                        ui.add(egui::ProgressBar::new((self.state.completed as f32/self.state.total as f32).min(1.0)).text(format!("{} / {} appoggi",self.state.completed,self.state.total)));
                    }
                    ui.add_space(16.0);
                    ui.horizontal(|ui| {
                        if ui.add_enabled(self.worker.is_none() && self.error.is_empty(),egui::Button::new(RichText::new(format!("Esegui: {}", task.title)).strong()).min_size(Vec2::new(110.0,40.0))).clicked(){self.start(&ctx);}
                        if ui.add_enabled(self.worker.is_some(),egui::Button::new("Interrompi").min_size(Vec2::new(110.0,40.0))).clicked(){self.stop();}
                    });
                    if self.worker.is_some() && task.adapter != "plain" && ui.button("Conferma azione manuale").clicked() {
                        if let Some(tx)=&self.controls {let _=tx.send("confirm".into());}
                    }
                    if self.worker.is_some() && task.adapter != "plain" {ui.small("La conferma manuale resta distinta dagli eventi del sensore.");}
                });
            });
            ui.add_space(16.0);ui.separator();
            if self.pending.is_some(){ui.colored_label(Color32::from_rgb(143,85,8),"Il piano è cambiato: la prova attiva conserva la revisione iniziale.");}
            if !self.state.run_directory.is_empty(){
                ui.small(format!("Log privati: {}",self.state.run_directory));
                if self.worker.is_none() {
                    ui.strong("Prossimo passo");
                    ui.label(if self.state.next_steps.is_empty(){"Torna al tuo agente: chiedigli di leggere i log e continuare dal checkpoint."}else{&self.state.next_steps});
                    if ui.button("Copia messaggio per l’agente").clicked() {
                        ctx.copy_text(format!("Leggi {}/plan.json e {}/events.jsonl, riprendi il checkpoint e continua l’indagine. Distingui simulazione, eventi hardware e azioni dichiarate. Non ripetere la prova senza analizzarne l’esito. Indicazione del piano: {}",self.state.run_directory,self.state.run_directory,self.state.next_steps));
                    }
                }
            }
            ui.collapsing("Eventi recenti",|ui| {
                egui::ScrollArea::vertical().max_height(120.0).show(ui,|ui| {for event in self.state.events.iter().rev().take(12){ui.small(event);}});
            });
            ui.small("L’icona indica lo stato, non la copertura anatomica del dito.");
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
            .with_inner_size([860.0, 600.0])
            .with_min_inner_size([760.0, 550.0]),
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
