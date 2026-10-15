# Pannello locale degli esperimenti

Il pannello egui serve all’utente durante le prove; l’agente continua a scegliere gli esperimenti e leggere i risultati. Può essere utile anche con client LLM che non inviano messaggi mentre un comando è in esecuzione. La GUI non sviluppa autonomamente un driver e non decide se un payload OEM sia adatto al dispositivo.

## Avvio

Dalla repository:

```sh
python scripts/open-panel.py
```

Il launcher crea un piano di simulazione e prove offline soltanto se `.rea-fs.local/panel/plan.json` non esiste. Non sostituisce un piano dell’agente e non avvia esperimenti. La prima esecuzione compila l’applicazione release: servono Rust/Cargo, un compilatore C e le dipendenze grafiche Linux di egui/eframe. Cargo può scaricare dipendenze fissate da Cargo.lock. Non è ancora una release con binari precompilati.

L’override del linker su Linux x86-64 vale solo per il processo Cargo e usa `cc` quando disponibile, preservando un override esplicito già presente nell’ambiente. Non modifica configurazioni globali. Avvio manuale:

```sh
cargo run --locked --release --manifest-path panel/Cargo.toml -- \
  --repo "$PWD" --session-dir "$PWD/.rea-fs.local/panel"
```

Usare `--session-dir` nel launcher per un’altra sessione. Il pannello cerca di default `plan.json` nella directory indicata; non scandisce il disco alla ricerca di file.

## Cosa vede l’utente

- Esperimenti disponibili e istruzioni dell’agente.
- Comando esatto, directory e durata massima prima di Esegui.
- Stato di contatto, progressi e countdown se ottenibili dall’adattatore.
- Interrompi; conferma manuale registrata separatamente dai segnali del dispositivo.
- Esito e directory dei log privati.

L’icona dell’impronta è stilizzata: non mostra un’immagine biometrica né stima quali zone del dito siano state coperte. La simulazione mostra tre indicatori di avanzamento, non l’impronta; dice di non toccare il lettore e non richiede appoggi reali. I suoi eventi sono marcati `simulation`. L’impronta stilizzata compare per l’adattatore hardware fprintd, con colori legati agli eventi osservati e nessuna pretesa di copertura anatomica.

## Come l’agente prepara il piano

Partire da `examples/panel-plan.json`. Il formato versione 1 richiede `plan_id`, `revision`, `title` ed `experiments`; ogni voce ha ID univoco, titolo, istruzioni, array `argv`, `cwd` relativo alla repository, timeout 1..3600 s, categoria dichiarata e adattatore `plain`, `simulation` o `fprintd`. Niente stringhe shell. Limite del piano 256 KiB e 32 esperimenti. `next_steps` opzionale (massimo 2000 caratteri) contiene il messaggio da mostrare dopo l’esito; resta visibile anche se la prova fallisce. Non viene eseguito come comando.

L’agente scrive un file temporaneo privato nella stessa directory, lo valida e sostituisce atomicamente `plan.json`. Il pannello rilegge le modifiche; una prova attiva conserva il suo piano. Un fingerprint del contenuto collega anteprima e avvio: se il file cambia prima del clic, occorre ricaricare e rileggere il comando. Nessun nuovo file attiva esecuzioni automatiche.

`{run_dir}` in un argomento viene sostituito con la nuova directory privata di quella esecuzione: è utile per output che non devono essere sovrascritti. Non è espansione shell e non esegue sostituzioni di comandi. `{finger}` consente di parametrizzare il dito scelto dall'utente nella GUI (dichiarando `"parameters": ["finger"]`).

Un piano eseguibile deve provenire da una fonte fidata. L’esecuzione senza shell e il rifiuto di sudo/su/pkexec diretti non costituiscono una sandbox: un programma autorizzato può a sua volta eseguire altro codice. Il pannello non conferisce consensi, non abilita sudo e non modifica PAM.

## Procedura passo-passo per l'agente (Checklist operativa)

Quando l'esperimento richiede contatto col sensore o supervisione dell'utente (`STATO 3`), l'agente segue rigorosamente questi passi:

### 1. Scrittura atomica del piano
Scrivi il piano in un file temporaneo con permessi `0600` nella directory della sessione (`.rea-fs.local/panel/.plan.tmp.json`) e sostituisci atomicamente `.rea-fs.local/panel/plan.json`.

### 2. Validazione esplicita
Esegui da riga di comando:
```sh
python3 scripts/experiment-panel.py validate --session-dir .rea-fs.local/panel
```
Questo comando verifica la conformità del piano e scrive atomicamente `.rea-fs.local/panel/panel-status.json` (`0600`). Se il comando fallisce con exit code 2, l'errore è registrato in `panel-status.json` per diagnosi immediata.

### 3. Esempio completo e copiabile di `plan.json` valido
```json
{
  "schema_version": 1,
  "plan_id": "test-fprintd-verify",
  "revision": 1,
  "title": "Verifica biometrica hardware",
  "experiments": [
    {
      "id": "verify-any",
      "title": "Verifica impronta",
      "instructions": "Lascia libero il sensore durante l'apertura. Quando vedi «APPOGGIA IL DITO», appoggia il dito piatto al centro del lettore e tienilo fermo finché non vedi «SOLLEVA IL DITO» o l'esito.",
      "argv": [
        "fprintd-verify"
      ],
      "cwd": ".",
      "timeout_seconds": 60,
      "category": "biometric_capture",
      "adapter": "fprintd",
      "raw_log": false,
      "next_steps": "Torna in chat e scrivi che hai concluso la prova: leggerò automaticamente il riepilogo e i log."
    }
  ]
}
```

### 4. Comunicazione e attesa dell'utente
Comunica all'utente in chat:
- Il comando per aprire il pannello (`python3 scripts/open-panel.py`).
- Il titolo della scheda o dell'esperimento da eseguire.
- Fermati (`STATO 3 -> AZIONE RICHIESTA`) e attendi che l'utente scriva in chat dopo aver eseguito la prova.

### 5. Procedura esatta di lettura dei log al ritorno dell'utente (`STATO 4`)
Quando l'utente risponde in chat (senza chiedergli di incollare output):
1. **Verifica stato validazione:** leggi `.rea-fs.local/panel/panel-status.json` per confermare `valid: true`.
2. **Sequenza cronologica dei run:** leggi `.rea-fs.local/panel/runs-index.jsonl` (linee JSON in append) per esaminare tutti i tentativi eseguiti dall'utente durante la sessione, senza perdere tentativi intermedi.
3. **Ultimo run eseguito:** leggi `.rea-fs.local/panel/latest-run.json`.
4. **Ispezione dettagliata della prova:** individua `run_dir` dal riepilogo e leggi:
   - `<run_dir>/summary.json`: riepilogo strutturato, esito finale, tempi, codice d'uscita e array `user_notes` con eventuali annotazioni lasciate dall'utente nella GUI.
   - `<run_dir>/events.jsonl`: sequenza temporale di tutti gli eventi (`device_state`, `progress`, `retry_hint`, `user_note`, `completed`/`failed`/`stopped`).
   - `<run_dir>/output.log`: solo se `raw_log: true` era stato richiesto.
5. Aggiorna `sessions/<id>/checkpoint.md` separando fatti osservati da interpretazioni prima di proporre il passo successivo.

## Eventi e log

Ogni esecuzione crea una directory con timestamp UTC e UUID (0700), piano immutabile e `events.jsonl` (0600). All’avvio e all’esito aggiorna atomicamente `summary.json` e il riferimento di sessione `latest-run.json`, con ID, percorsi, primo/ultimo evento e conteggio eventi. Inoltre, registra in append la cronologia completa in `runs-index.jsonl` e conserva lo stato dell'ultimo parsing in `panel-status.json`. `handoff.txt` indica come riprendere. Tutti questi file sono creati con permessi `0600` e directory `0700`.

- **`panel-status.json`:** scritto atomicamente alla validazione; riporta `valid: true/false`, timestamp, revisione, elenco ID esperimenti e eventuale messaggio d'errore.
- **`runs-index.jsonl`:** file append-only con una riga JSON per ciascun run completato, interrotto o fallito. Contiene: `schema_version`, `run_id`, `plan_id`, `revision`, `experiment_id`, `adapter`, `started_at`, `ended_at`, `terminal_type`, `exit_code`, `result`, `parameters`, `run_dir`, `summary_path`.
- **`summary.json` / `latest-run.json`:** riepilogo strutturato del singolo run o dell'ultimo completato, comprensivo dell'array `user_notes` con timestamp e testo delle note inserite dall'utente.
- **`events.jsonl`:** log analitico degli eventi in ordine cronologico.
- **`user_notes`:** note dell'utente salvate nel log durante la prova (via stdin `{"type": "note", "text": "..."}`) o a prova conclusa (via pulsante «Registra nota» nella GUI o comando `python3 scripts/experiment-panel.py note --session-dir <dir> --text "..."`). Il testo è sanitizzato, a singola riga e limitato a 500 caratteri.

Un comando ordinario produce eventi di avvio/esito, non eventi hardware ricavati da testo arbitrario. Il titolo e il pulsante Esegui nominano la prova selezionata. A fine esecuzione l’utente torna alla chat e l’agente riprende autonomamente leggendo i file di log indicati. L’adattatore selezionato interpreta solo i segnali previsti. Se `raw_log: true`, l’output grezzo viene salvato separatamente come `output.log`, con limite 4 MiB e righe limitate a 64 KiB. Può contenere dati sensibili del programma scelto: abilitarlo solo dopo averne controllato il logging. Non inserire chiavi, template o immagini nei messaggi dell’adattatore.

L’agente legge i run conclusi, include errori e timeout nella valutazione e decide il prossimo esperimento. Un clic «Conferma azione manuale» è una dichiarazione dell’utente; non prova un contatto misurato. Il completamento di un processo non prova da solo che un sensore sia stato fermato correttamente.

## Interruzione

Il backend avvia l’esperimento in un gruppo separato. In caso di cancel/timeout invia SIGINT, poi SIGTERM e SIGKILL solo se necessario, con attese limitate. I segnali riguardano il gruppo per includere i processi figli. Un arresto non prova il cleanup hardware: il log lo segnala come non confermato. Dopo un errore non avvengono retry automatici.

Chiudere normalmente la finestra richiede l’arresto del backend prima dell’uscita. Chiudere forzatamente processi dal sistema resta distinto dal pulsante Interrompi e richiede controllo degli eventuali processi superstiti.

## fprintd

`examples/fprintd-panel-plan.json` è un esempio separato per una macchina con fprintd, driver e credenziali già configurati. Non è il piano iniziale e non abilita automaticamente il sensore.

L’adattatore osserva il percorso del dispositivo scelto dal comando, gli esiti enrollment/verify e, tramite `gdbus`, le proprietà `finger-needed` e `finger-present`. La lettura delle proprietà inizia dopo il messaggio di avvio dell’operazione, evitando di mostrare come pronto uno stato precedente durante l’apertura/calibrazione. Non effettua polling quando non c’è una prova.

Se gdbus o le proprietà non sono disponibili non si deve dedurre un contatto dal countdown. La finestra mostra la limitazione quando il bridge manca. Numero di passaggi e protocollo dipendono dal driver effettivo; il pannello non presuppone cinque appoggi per tutti i sensori. `-f right-index-finger` seleziona il template, non identifica quale dito l’utente abbia appoggiato.

## Verifiche

Python: validazione, permessi, piani incompleti/oversized, revisioni, mancata esecuzione su fingerprint diverso, processi reali con successo/errore/timeout/cancel, processi figli, limite output e interruzione del backend. Rust: stati legati al run, origine simulazione, eventi terminali e quoting del comando copiato. Simulazione end-to-end eseguita senza hardware e finestra renderizzata sul display locale.

I test GUI e la simulazione non dimostrano una nuova prova hardware attraverso il pannello. Il driver resta soggetto ai limiti e agli esiti descritti nell’audit. Le build C più recenti non sono automaticamente installate dal pannello.

Build release iniziale misurata su questa macchina: circa 7,3 MiB, prima compilazione circa 2 min 14 s. Con il supporto appunti X11/Wayland la build del pannello aggiornato pesa circa 8,4 MiB (8.796.536 byte). Questi valori non includono toolchain e cache Cargo e non sono una misura su altre macchine. La finestra è stata aperta e verificata visivamente; due avvii dal pannello di «Esegui test e build» hanno prodotto log di avvio e completamento con exit code 0. Questo verifica quel percorso clic→run; non verifica la simulazione, il sensore o la chiusura normale. La distruzione forzata esterna della finestra X11 ha prodotto un errore winit: non viene presentata come prova della normale chiusura.

## Peso e scelta del toolkit

La finestra è Rust/egui; Python (libreria standard) esegue validazione e supervisione dei comandi. Non usa Qt, Tkinter, un browser o un server web. Python resta una dipendenza attuale del pannello, anche se gli esperimenti non sono Python. Cargo e il compilatore C servono alla compilazione; la cache di sviluppo non è il peso dell’applicazione distribuita. Le librerie grafiche di sistema e i driver rimangono necessari anche quando non compaiono tutti in `ldd`, perché possono essere caricati dinamicamente.

Il piano viene confrontato come contenuto, con letture limitate, una volta al secondo; il processo Python di validazione parte al caricamento iniziale, su modifica o ricarica esplicita. Il backend rivalida comunque prima di eseguire. Nessun polling del sensore a riposo.

Per questo pannello la scelta consigliata resta egui. [Clay](https://github.com/nicbarker/clay) è un motore di layout C senza dipendenze proprie: emette primitive da disegnare e demanda finestra, input e rendering al programma ospite. Una versione C è possibile, comprese icona, countdown e grafici, ma richiede scegliere e mantenere anche quei componenti. Non è una garanzia di meno dipendenze dell’applicazione completa.

Una futura distribuzione più semplice dovrebbe fornire un eseguibile release verificato e mantenere separati prerequisiti del pannello e prerequisiti del singolo esperimento. Se si elimina Python dalla supervisione, occorre portare e verificare anche validazione, permessi, process group, timeout e log; non basta spostare il pulsante. Questa migrazione non è stata implementata. Non sono ancora disponibili pacchetti o binari pubblicati.

## Comandi nella finestra

Il piano può già lanciare un comando da terminale: usare `argv` come elenco di argomenti e `raw_log: true` quando serve conservarne stdout/stderr. Il comando non riceve input dal terminale (`stdin` chiuso): programmi che chiedono password, una console interattiva o un TTY vanno avviati nel terminale dell’utente. Un terminale incorporato non è implementato. Questa scelta evita di aggiungere un emulatore e mantiene distinta la prova preparata da una sessione shell libera.

Esempio di indicazione finale scritta dall’agente nel singolo esperimento:

```json
"next_steps": "Torna alla chat e chiedi all’agente di analizzare il tentativo prima di preparare una nuova acquisizione."
```

## Appunti e ripresa automatica

La build include esplicitamente la feature `clipboard` di eframe per l’integrazione X11/Wayland. La prima build l’aveva omessa disattivando le feature predefinite: `copy_text` restava confinato al fallback interno e non alimentava gli appunti del desktop. Il supporto corretto aggiunge le dipendenze di integrazione necessarie, senza abilitare l’intero insieme predefinito di eframe. Il clic mostra un riscontro visivo; i file su disco restano il percorso principale e non dipendono dagli appunti.

Chi riprende l’indagine legge prima il `latest-run.json` nella directory della sessione scelta. I run precedenti non sono cancellati: per ricostruire una serie leggere anche gli altri riepiloghi. Il riferimento descrive l’ultimo aggiornamento del backend, non garantisce che non esistano altre finestre o sensori occupati. Non avviare esperimenti concorrenti sullo stesso dispositivo.
