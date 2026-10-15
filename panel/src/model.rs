use serde::Deserialize;
use serde_json::Value;
use std::time::Instant;

#[derive(Clone, Deserialize)]
pub struct Envelope {
    pub plan: Plan,
    pub fingerprint: String,
}
#[derive(Clone, Deserialize)]
pub struct Plan {
    pub plan_id: String,
    pub title: String,
    pub revision: u64,
    pub experiments: Vec<Experiment>,
}
#[derive(Clone, Deserialize)]
pub struct Experiment {
    pub id: String,
    pub title: String,
    pub instructions: String,
    pub argv: Vec<String>,
    pub cwd: String,
    pub timeout_seconds: u64,
    pub adapter: String,
    pub category: String,
    #[serde(default)]
    pub raw_log: bool,
}

pub struct State {
    pub active: bool,
    pub run_id: String,
    pub status: String,
    pub detail: String,
    pub next_steps: String,
    pub needed: bool,
    pub present: bool,
    pub simulation: bool,
    pub completed: u64,
    pub total: u64,
    pub timeout: f64,
    pub started: Option<Instant>,
    pub run_directory: String,
    pub events: Vec<String>,
}
impl Default for State {
    fn default() -> Self {
        Self {
            active: false,
            run_id: String::new(),
            status: "Pronto per iniziare".into(),
            detail: String::new(),
            next_steps: String::new(),
            needed: false,
            present: false,
            simulation: false,
            completed: 0,
            total: 0,
            timeout: 0.0,
            started: None,
            run_directory: String::new(),
            events: Vec::new(),
        }
    }
}
impl State {
    pub fn apply(&mut self, event: Value) {
        let kind = event["type"].as_str().unwrap_or("");
        let run = event["run_id"].as_str().unwrap_or("");
        if kind == "started" {
            *self = Self::default();
            self.active = true;
            self.run_id = run.into();
            self.simulation = event["adapter"] == "simulation";
            self.status = "Esperimento in corso".into();
            self.detail = event["instructions"].as_str().unwrap_or("").into();
            self.next_steps = event["next_steps"].as_str().unwrap_or("").into();
            self.timeout = event["timeout_seconds"].as_f64().unwrap_or(0.0);
            self.started = Some(Instant::now());
            self.run_directory = event["run_directory"].as_str().unwrap_or("").into();
        } else if run != self.run_id || run.is_empty() {
            return;
        }
        match kind {
            "device_state" if matches!(event["origin"].as_str(), Some("device" | "simulation")) => {
                self.needed = event["needed"].as_bool().unwrap_or(false);
                self.present = event["present"].as_bool().unwrap_or(false);
                self.status = if self.simulation {
                    if self.present {
                        "Contatto simulato"
                    } else if self.needed {
                        "Attesa simulata"
                    } else {
                        "Rilascio simulato"
                    }
                } else if self.present {
                    "Dito rilevato"
                } else if self.needed {
                    "Appoggia il dito"
                } else {
                    "Attendi"
                }
                .into();
            }
            "instruction" | "limitation" => {
                self.detail = event["text"].as_str().unwrap_or("").into()
            }
            "progress" => {
                self.completed = event["completed"].as_u64().unwrap_or(0);
                self.total = event["total"].as_u64().unwrap_or(0);
                self.status = if self.simulation {
                    "Passaggio simulato completato"
                } else {
                    "Appoggio acquisito: rimuovi il dito"
                }
                .into();
            }
            "stopping" => {
                self.status = "Interruzione in corso".into();
                self.needed = false;
                self.present = false;
            }
            "completed" | "failed" | "timeout" | "stopped" => {
                self.active = false;
                self.needed = false;
                self.present = false;
                self.status = match kind {
                    "completed" => "Esperimento completato",
                    "timeout" => "Tempo scaduto",
                    "stopped" => "Esperimento interrotto",
                    _ => "Esperimento fallito",
                }
                .into();
                if event["result"] == "verify-no-match" {
                    self.status = "Impronta non riconosciuta".into();
                }
                if event["result"] == "verify-match" {
                    self.status = "Impronta riconosciuta".into();
                }
                if event["result"] == "enroll-completed" {
                    self.status = "Registrazione completata con successo".into();
                }
                self.detail = event["message"]
                    .as_str()
                    .or_else(|| event["reason"].as_str())
                    .unwrap_or("")
                    .into();
                if self.detail.is_empty() {
                    if let Some(code) = event["exit_code"].as_i64() {
                        self.detail = format!(
                            "Il comando è terminato con codice {code}. Dettagli nei log privati."
                        );
                    }
                }
                if event["cleanup_not_confirmed"] == true {
                    self.detail.push_str(" · Cleanup hardware non confermato");
                }
            }
            _ => {}
        }
        self.events.push(format!(
            "{}  {}",
            event["timestamp"].as_str().unwrap_or(""),
            self.status
        ));
        if self.events.len() > 40 {
            self.events.remove(0);
        }
    }
    pub fn fail(&mut self, message: String) {
        self.active = false;
        self.needed = false;
        self.present = false;
        self.status = "Impossibile avviare la prova".into();
        self.detail = message;
    }
    pub fn remaining(&self) -> f64 {
        (self.timeout
            - self
                .started
                .map(|t| t.elapsed().as_secs_f64())
                .unwrap_or(0.0))
        .max(0.0)
    }
}

pub fn shell_quote(value: &str) -> String {
    format!("'{}'", value.replace('\'', "'\"'\"'"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    #[test]
    fn ready_requires_device_event_and_run_identity() {
        let mut state = State::default();
        state.apply(json!({"type":"started","run_id":"a","adapter":"plain","timeout_seconds":60}));
        assert!(!state.needed);
        state.apply(json!({"type":"device_state","run_id":"other","origin":"device","needed":true,"present":false}));
        assert!(!state.needed);
        state.apply(json!({"type":"device_state","run_id":"a","origin":"device","needed":true,"present":false}));
        assert!(state.needed);
    }
    #[test]
    fn plain_command_completion_explains_exit_code() {
        let mut state = State::default();
        state.apply(json!({"type":"started","run_id":"a","adapter":"plain"}));
        state.apply(json!({"type":"completed","run_id":"a","exit_code":0}));
        assert!(state.detail.contains("0"));
        assert!(!state.detail.is_empty());
    }
    #[test]
    fn next_steps_survive_the_terminal_event() {
        let mut state = State::default();
        state.apply(json!({"type":"started","run_id":"a","next_steps":"Continua dai log."}));
        state.apply(json!({"type":"failed","run_id":"a","exit_code":2}));
        assert_eq!(state.next_steps, "Continua dai log.");
    }
    #[test]
    fn simulation_never_requests_a_physical_touch() {
        let mut state = State::default();
        state.apply(json!({"type":"started","run_id":"a","adapter":"simulation"}));
        state.apply(json!({"type":"device_state","run_id":"a","origin":"simulation","needed":true,"present":false}));
        assert_eq!(state.status, "Attesa simulata");
        state.apply(json!({"type":"device_state","run_id":"a","origin":"simulation","needed":false,"present":true}));
        assert_eq!(state.status, "Contatto simulato");
    }
    #[test]
    fn copy_command_quotes_shell_metacharacters() {
        assert_eq!(shell_quote("$(echo secret)"), "'$(echo secret)'");
        assert_eq!(shell_quote("a'b"), "'a'\"'\"'b'");
    }
    #[test]
    fn simulation_and_failure_stay_visible() {
        let mut state = State::default();
        state.apply(
            json!({"type":"started","run_id":"a","adapter":"simulation","timeout_seconds":20}),
        );
        state.apply(json!({"type":"device_state","run_id":"a","origin":"simulation","needed":true,"present":false}));
        assert!(state.simulation);
        state.apply(json!({"type":"failed","run_id":"a","reason":"output_limit"}));
        assert!(!state.active);
        assert_eq!(state.status, "Esperimento fallito");
        assert!(state.detail.contains("output_limit"));
    }
}
