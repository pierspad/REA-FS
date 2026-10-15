use crate::model::Envelope;
use eframe::egui;
use serde_json::Value;
use std::io::{BufRead, BufReader, Read, Write};
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::sync::mpsc::{self, Receiver, SyncSender};
use std::thread;

pub enum Message {
    Plan(Result<Envelope, String>),
    Event(Value),
    Error(String),
    Exited,
}

pub fn load(root: PathBuf, session: PathBuf, tx: SyncSender<Message>, ctx: egui::Context) {
    thread::spawn(move || {
        let result = Command::new("python3")
            .arg(root.join("scripts/experiment-panel.py"))
            .args(["validate", "--session-dir"])
            .arg(session)
            .current_dir(root)
            .output()
            .map_err(|e| e.to_string())
            .and_then(|o| {
                if !o.status.success() {
                    return Err(String::from_utf8_lossy(&o.stderr)
                        .chars()
                        .take(2000)
                        .collect());
                }
                serde_json::from_slice(&o.stdout).map_err(|e| e.to_string())
            });
        let _ = tx.send(Message::Plan(result));
        ctx.request_repaint();
    });
}

pub fn spawn(
    root: PathBuf,
    session: PathBuf,
    id: String,
    fingerprint: String,
    tx: SyncSender<Message>,
    ctx: egui::Context,
) -> (mpsc::Sender<String>, thread::JoinHandle<()>) {
    let (controls, rx): (mpsc::Sender<String>, Receiver<String>) = mpsc::channel();
    let handle = thread::spawn(move || {
        let child = Command::new("python3")
            .arg(root.join("scripts/experiment-panel.py"))
            .args(["run", "--session-dir"])
            .arg(session)
            .args([
                "--experiment",
                &id,
                "--expected-fingerprint",
                &fingerprint,
                "--cancel-on-eof",
            ])
            .current_dir(root)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn();
        match child {
            Err(e) => {
                let _ = tx.send(Message::Error(e.to_string()));
            }
            Ok(mut child) => {
                let mut input = child.stdin.take().expect("piped stdin");
                thread::spawn(move || {
                    for command in rx {
                        if writeln!(input, "{{\"type\":\"{command}\"}}").is_err() {
                            break;
                        }
                        let _ = input.flush();
                    }
                });
                let stderr = child.stderr.take().expect("piped stderr");
                let error_reader = thread::spawn(move || {
                    let mut text = String::new();
                    let _ = stderr.take(4096).read_to_string(&mut text);
                    text
                });
                let mut reader = BufReader::new(child.stdout.take().expect("piped stdout"));
                let mut invalid = false;
                loop {
                    let mut line = Vec::new();
                    let count = std::io::Read::by_ref(&mut reader)
                        .take(65537)
                        .read_until(b'\n', &mut line);
                    match count {
                        Ok(0) => break,
                        Ok(_) if line.len() <= 65536 => {
                            match serde_json::from_slice::<Value>(&line) {
                                Ok(event)
                                    if event["schema_version"] == 1
                                        && event["run_id"].is_string() =>
                                {
                                    let _ = tx.send(Message::Event(event));
                                    ctx.request_repaint();
                                }
                                _ => {
                                    invalid = true;
                                    break;
                                }
                            }
                        }
                        _ => {
                            invalid = true;
                            break;
                        }
                    }
                }
                if invalid {
                    let _ = tx.send(Message::Error(
                        "Evento backend non valido: chiusura richiesta".into(),
                    ));
                }
                // Closing stdin requests bounded cleanup even if a protocol failure occurred.
                // The GUI drops its sender on Error, waking the writer and closing stdin.
                let status = child.wait();
                let detail = error_reader.join().unwrap_or_default();
                if !detail.is_empty() {
                    let _ = tx.send(Message::Error(detail));
                }
                if let Err(e) = status {
                    let _ = tx.send(Message::Error(e.to_string()));
                }
            }
        }
        let _ = tx.send(Message::Exited);
        ctx.request_repaint();
    });
    (controls, handle)
}
