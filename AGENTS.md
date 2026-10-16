# Istruzioni per agenti REA-FS

Leggi README.md, docs/repositories.md, docs/preferences.md, docs/agent-workflow.md e docs/experiment-panel.md prima dell’indagine. Leggi anche `.rea-fs.local/preferences.json` se presente; le istruzioni esplicite dell’utente prevalgono.

- REA-FS contiene pannello, workflow e documentazione; `workspace/rea` e `workspace/libfprint` sono clone indipendenti ignorati. `drivers/syna_tod` è il submodule del driver `synaptics-00dc`: inizializzarlo con `git submodule update --init --recursive` e pubblicare i commit del driver prima di aggiornare il pin. Leggi le istruzioni proprie di ogni clone prima di modificarlo.
- Verifica repository, stato Git, remote e identità effettiva prima di commit o accessi remoti. Non portare l’identità freedesktop nel repository GitHub.
- Riprendi il checkpoint locale prima di ripetere un esperimento. Distingui fatti, ipotesi e risultati ancora da verificare.
- Non inventare osservazioni hardware, supporto, build riuscite o test eseguiti.
- Comunica all’utente solo azioni fisiche utili e tempestive. Definisci timeout e criterio di successo; non interpretare il silenzio come conferma.
- Applica docs/preferences.md: default autorizzazioni `ask`; in modalità `autonomous` procedi senza riconferme nelle categorie autorizzate. Registra il consenso già dato e rispettane l’ambito; chiedi solo per categorie mancanti. Un errore richiede indagine, non retry ciechi.
- Se mancano preferenze, chiedile una volta in chat e salvale localmente. Se l’utente rifiuta o delega la scelta, registra i default scelti dall’agente; non inventare consenso all’autonomia.
- Delega solo secondo le preferenze e i vincoli della sessione: includi nel confronto il costo di contesto, coordinamento e verifica. Modello economico e minor consumo di token non sono sinonimi.
- Non eseguire push, aprire merge request o pubblicare dati senza autorizzazione. Non includere chiavi, template biometrici, scansioni o binari riservati nei file tracciati.
- Usa REA solo se disponibile e utile; clonare il progetto non significa aver installato o connesso il suo MCP.
- Non scrivere nuove GUI o demoni da zero durante l’indagine: usa il pannello esistente del repository (`scripts/open-panel.py` + `.rea-fs.local/panel/plan.json`) secondo `docs/experiment-panel.md`.

## Regola decisionale: Terminale diretto vs Pannello GUI

- **Terminale diretto (eseguito dall’agente):** consentito SOLO per comandi in sola lettura (`lsusb`, `udevadm`, `python3 scripts/rea-fs.py preflight`), compilazioni (`meson`, `ninja`, `cargo`), test offline (`python3 scripts/rea-fs.py check`, `python3 -m unittest`) e analisi statica (`rea`, `ghidra`).
- **Pannello GUI (`.rea-fs.local/panel/plan.json`):** OBBLIGATORIO per ogni test che richieda contatto fisico col sensore, attesa dell’utente, cattura biometrica, enrollment/verifica `fprintd` o osservazione fisica. L’agente prepara `plan.json`, lo valida con `python3 scripts/experiment-panel.py validate --session-dir .rea-fs.local/panel`, dice all’utente quale esperimento avviare nella GUI e attende che l’utente risponda in chat. Al rientro l’agente legge i file di log in `.rea-fs.local/panel/` (`runs-index.jsonl`, `latest-run.json`, `<run_dir>/summary.json`, `<run_dir>/events.jsonl`).
- **Fallback senza display grafico:** solo se `DISPLAY` e `WAYLAND_DISPLAY` sono assenti, o l’utente dichiara di essere via SSH puro senza X11/Wayland, usa la modalità a finestra temporale nel terminale descritta in `docs/agent-workflow.md`.

## Macchina a Stati Obbligatoria (ciclo dell’agente)

Esegui un solo stato alla volta e annota in `checkpoint.md` lo stato corrente prima di passare al successivo.

- `STATO 0 - BOOTSTRAP E PREFERENZE`: leggi i documenti indicati sopra e `.rea-fs.local/preferences.json`; riprendi `sessions/<id>/checkpoint.md` o creane uno con `python3 scripts/rea-fs.py init-session`. Se `preferences.json` non è configurato, fai le 3 domande di `docs/preferences.md` e fermati.
- `STATO 1 - DISCOVERY PASSIVA`: esegui `python3 scripts/rea-fs.py preflight`; identifica bus, VID:PID, kernel, fprintd/libfprint installati. Aggiorna `checkpoint.md`. Nessun contatto col sensore.
- `STATO 2 - RICERCA E IPOTESI`: controlla il supporto upstream in `workspace/libfprint`, issue/patch note e driver OEM. Registra in `checkpoint.md` cosa esiste già e cosa manca.
- `STATO 3 - PREPARAZIONE ESPERIMENTO`: formula una sola domanda sperimentale nel formato di `docs/templates/investigation.md`. Se il test è offline, eseguilo nel terminale. Se richiede il sensore o l’utente, scrivi e valida `.rea-fs.local/panel/plan.json`, aggiorna `checkpoint.md` con stato `AZIONE RICHIESTA` e chiedi all’utente di avviarlo dal pannello GUI. Fermati e attendi la risposta.
- `STATO 4 - LETTURA LOG E VALUTAZIONE`: quando l’utente risponde in chat, leggi `.rea-fs.local/panel/runs-index.jsonl` e `latest-run.json`, poi ispeziona `<run_dir>/summary.json` (incluse le `user_notes`), `<run_dir>/events.jsonl` ed eventuale `output.log` privato. Aggiorna `checkpoint.md` separando fatti osservati da interpretazioni.
- `STATO 5 - IMPLEMENTAZIONE E VERIFICA`: implementa decoder/driver, esegui `python3 scripts/rea-fs.py check` (offline), poi prepara il piano GUI per le prove hardware di enrollment/verifica e ripeti `STATO 3 -> STATO 4`.
