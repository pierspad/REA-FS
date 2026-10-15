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

L’icona dell’impronta è stilizzata: non mostra un’immagine biometrica né stima quali zone del dito siano state coperte. La simulazione porta un’indicazione esplicita e tutti i suoi eventi di sensore sono marcati `simulation`.

## Come l’agente prepara il piano

Partire da `examples/panel-plan.json`. Il formato versione 1 richiede `plan_id`, `revision`, `title` ed `experiments`; ogni voce ha ID univoco, titolo, istruzioni, array `argv`, `cwd` relativo alla repository, timeout 1..3600 s, categoria dichiarata e adattatore `plain`, `simulation` o `fprintd`. Niente stringhe shell. Limite del piano 256 KiB e 32 esperimenti.

L’agente scrive un file temporaneo privato nella stessa directory, lo valida e sostituisce atomicamente `plan.json`. Il pannello rilegge le modifiche; una prova attiva conserva il suo piano. Un fingerprint del contenuto collega anteprima e avvio: se il file cambia prima del clic, occorre ricaricare e rileggere il comando. Nessun nuovo file attiva esecuzioni automatiche.

`{run_dir}` in un argomento viene sostituito con la nuova directory privata di quella esecuzione: è utile per output che non devono essere sovrascritti. Non è espansione shell e non esegue sostituzioni di comandi.

Un piano eseguibile deve provenire da una fonte fidata. L’esecuzione senza shell e il rifiuto di sudo/su/pkexec diretti non costituiscono una sandbox: un programma autorizzato può a sua volta eseguire altro codice. Il pannello non conferisce consensi, non abilita sudo e non modifica PAM.

## Eventi e log

Ogni esecuzione crea una directory UUID 0700 con piano immutabile e `events.jsonl` 0600. Gli eventi includono ID piano/revisione/esperimento/run, timestamp UTC, durata monotona, tipo e origine. Il file locale resta ignorato da Git.

Un comando ordinario produce eventi di avvio/esito, non eventi hardware ricavati da testo arbitrario. L’adattatore selezionato interpreta solo i segnali previsti. Se `raw_log: true`, l’output grezzo viene salvato separatamente come `output.log`, con limite 4 MiB e righe limitate a 64 KiB. Può contenere dati sensibili del programma scelto: abilitarlo solo dopo averne controllato il logging. Non inserire chiavi, template o immagini nei messaggi dell’adattatore.

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

Build release misurata su questa macchina: circa 7,4 MiB, prima compilazione circa 2 min 14 s. Questi valori non includono toolchain e cache Cargo e non sono una misura su altre macchine. Il controllo visuale ha usato una cattura della sola finestra; una cattura precoce del framebuffer è risultata nera, quindi non è stata usata come verifica. L’automazione del puntatore XWayland non ha attivato il pulsante: il percorso GUI clic→run non è dichiarato provato; il runner è stato verificato da CLI e i cambiamenti di stato da test Rust. Una chiusura forzata esterna della finestra ha provocato un errore del backend winit/X11, distinto dalla normale richiesta di chiusura: resta un limite da verificare interattivamente.
