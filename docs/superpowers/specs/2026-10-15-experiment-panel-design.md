# Pannello locale egui per esperimenti assistiti

Stato: specifica da approvare prima dell’implementazione. Richiesta: una GUI piccola per un utente che clona REA-FS e segue un agente, con avvio degli esperimenti selezionati. Non richiede capacità interattive particolari del client LLM.

## Obiettivo e criteri di successo

L’agente prepara file locali. L’utente apre un pannello, vede cosa fare, avvia volontariamente un esperimento, riceve richieste fisiche tempestive e può interrompere. L’agente legge poi gli eventi e gli esiti. La GUI non presume un sensore Synaptics e non decide da sola pairing, reset o comandi OEM.

La prima versione deve dimostrare il flusso con una simulazione dichiarata e con strumenti offline esistenti. Il percorso hardware fprintd viene esposto solo con un adattatore che distingue stato osservato, richiesta dell’interfaccia e conferma umana. Nessun risultato simulato va etichettato hardware.

## Struttura proposta

- `panel/`: applicazione Rust egui/eframe, finestra singola con lista esperimenti, istruzioni, icona stilizzata del dito, stato, countdown, progressi, comando esatto, esito e pulsanti Esegui/Interrompi/Conferma azione quando richiesto.
- `scripts/experiment-panel.py`: backend Python con libreria standard. Valida il piano, avvia un solo processo, gestisce timeout/terminazione e scrive eventi JSONL. Utilizzabile anche senza GUI.
- `docs/experiment-panel.md` e piano di esempio senza hardware; file reali in `.rea-fs.local/panel/`, ignorati.

La GUI resta reattiva: lettura pipe e processi fuori dal thread grafico, ridisegno su eventi e tick limitati quando c’è un countdown. Dipendenze Rust fissate e Cargo.lock tracciato. La dimensione e i tempi della prima build vanno misurati: non si promette un binario piccolo senza misure.

## Protocollo dei file

Percorso predefinito `.rea-fs.local/panel/plan.json`, modificabile con `--session-dir`. Piano versionato con `schema_version`, `plan_id`, `revision`, `title`, `experiments`. Ogni esperimento contiene `id`, `title`, `instructions`, `argv` (array, mai una shell string), `cwd` relativo alla repository, `timeout_seconds`, `adapter` e categoria operativa dichiarata.

L’agente sostituisce il piano atomicamente. La GUI valida dimensioni, schema e duplicati; mostra un errore leggibile su file parziale o invalido. I cambiamenti non interrompono né sostituiscono un’azione già in corso: viene conservata la revisione avviata e la nuova è segnalata per il prossimo avvio. Nessun autorun alla comparsa di file.

Ogni esecuzione ha un ID univoco e una directory privata 0700. `events.jsonl` (0600) contiene versione, ID piano/revisione/esperimento/run, timestamp UTC e tempo monotono trascorso, tipo evento, origine (`backend`, `device`, `user`, `simulation`) e campi specifici. Tipi minimi: started, instruction, device_state, progress, user_confirmation, completed, failed, timeout, stopping, stopped. I log non contengono immagini/template/chiavi. stdout/stderr di comandi non riconosciuti non vengono automaticamente copiati nel log strutturato; eventuale log grezzo privato va dichiarato nel piano e mostrato all’utente.

Gli eventi sono prove di ciò che indica la loro origine: un clic Conferma non è un evento hardware; il conto alla rovescia non implica che il sensore sia pronto. Un’icona stilizzata mostra uno stato, non una mappa reale della copertura del dito.

## Avvio e arresto

L’utente vede argv, directory, durata e categoria prima del clic Esegui. Un solo esperimento alla volta. Il backend usa esecuzione senza shell e non esegue automaticamente sudo o modifiche PAM. Un piano è input eseguibile preparato dall’agente: non è una sandbox né può essere importato da una fonte non fidata senza revisione.

Il processo è avviato in un gruppo separato. Interrompi richiede prima uscita controllata, poi termina il gruppo dopo una grazia limitata, segnalando se il cleanup non è confermato. La chiusura della GUI segue la stessa procedura, senza lasciare processi silenziosamente attivi. Nessun retry automatico dopo un errore.

## Adattatori della prima versione

1. **Simulazione esplicita:** sequenza deterministica pronto/appoggio/rimozione/progresso per verificare UI e logging, marcata simulation in ogni evento pertinente.
2. **Strumenti offline REA-FS:** preflight e check con exit code e durata. I dati del preflight restano in un file privato, non nel log pubblico.
3. **fprintd opzionale:** enrollment/verifica standard, numero passaggi e proprietà `finger-needed`/`finger-present` tramite D-Bus usando strumenti disponibili. All’assenza del bridge o delle proprietà, mostra solo eventi realmente ottenuti e non inventa il contatto. Comando del dito seleziona il template, non prova quale dito sia stato appoggiato.

Altri sensori possono produrre lo stesso protocollo eventi attraverso un adattatore dedicato. Acquisizioni RAW specifiche, immagini biometriche e stima della copertura non fanno parte di questa prima versione.

## Verifica e consegna

Test del validatore, revisioni del piano, file malformati, rifiuto autorun, isolamento fra run, permessi, timeout, cancellazione del processo e separazione simulation/device. Build/test Rust e Python. Prova visuale locale se il display è disponibile; altrimenti dichiarare il limite e fornire un comando avviabile dall’utente. Prova hardware separata, con consensi già registrati e azioni fisiche al momento opportuno.

Lancio previsto: `cargo run --manifest-path panel/Cargo.toml -- --session-dir .rea-fs.local/panel`. Una release successiva può fornire binari verificati; clonare il sorgente non evita la toolchain Rust e le dipendenze grafiche della prima compilazione.
