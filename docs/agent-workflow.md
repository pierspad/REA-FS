# Checklist dell’agente investigatore

## 1. Ripresa e preflight

- [ ] Leggi docs/preferences.md e le preferenze locali. Recupera quelle già espresse in chat; configura una sola volta le scelte mancanti e salvale. Non scambiare rifiuto o silenzio per autorizzazione completa.
- [ ] Prima di delegare valuta il risparmio nell’obiettivo scelto, compresi contesto e verifica; registra decisione e modello. Se non è dimostrato il risparmio richiesto da una condizione stretta, lavora direttamente.

- [ ] Leggi le istruzioni della radice e dei clone coinvolti. Controlla modifiche preesistenti senza sovrascriverle.
- [ ] Verifica `workspace/rea` e `workspace/libfprint`, remote e commit. Se mancano, segui il README e usa il fork scelto dall’utente.
- [ ] Controlla `git config --show-origin` per autore e SSH in ciascun clone. Identifica origin e upstream: non presumere permessi di scrittura.
- [ ] Cerca sessioni in `sessions/`. Riprendi quella pertinente; altrimenti crea `sessions/<data-ora>-<sensore>/` con `checkpoint.md`.
- [ ] Registra obiettivo, fase, ambiente, repository e revisioni, autorizzazioni concesse, prove concluse, blocchi e prossimo passo.

## 2. Discovery senza modifiche

- [ ] Rileva distribuzione, kernel, bus USB/SPI/I²C, identificativi e driver presenti con strumenti disponibili e comandi in lettura.
- [ ] Elenca solo le dipendenze necessarie. Installa pacchetti o usa privilegi secondo l’ambito autorizzato nelle preferenze; in modalità `ask` ottieni il consenso mancante. Non richiedere nuovamente autorizzazioni già concesse.
- [ ] Registra dati osservati e comandi con esito, minimizzando seriali e informazioni personali.

## 3. Ricerca e ipotesi

- [ ] Cerca prima supporto esistente in libfprint, issue e patch ufficiali; annota fonti, data e revisioni.
- [ ] Confronta sensori correlati e driver ottenuti legittimamente. Identifica ciò che manca prima di proporre acquisizioni o reverse engineering.
- [ ] Per ogni conclusione registra evidenza, confidenza e alternative. Non dedurre un protocollo dal solo nome commerciale.
- [ ] Scegli il prossimo esperimento con domanda, risultato atteso, rischi e criterio di arresto; evita prove già concluse salvo nuove evidenze.

## Controlli emersi dalla prima indagine (Condizionali: applicare solo se pertinenti all'architettura del sensore osservato)

> [!NOTE]
> Questi controlli derivano dall'indagine specifica sul sensore Synaptics 06cb:00dc (Acer Aspire A514-54). Non cercare necessariamente RomInfo, handshake TLS o frame 152×112 su sensori di altri produttori (es. Goodix, Elan, FocalTech), la cui architettura e protocollo possono essere completamente differenti.

- [ ] Distinguere libfprint upstream, fork TOD, modulo vendor e servizio fprintd: un pacchetto installato può non includere l’ID del sensore.
- [ ] Controllare la tabella dispositivi effettiva e il contesto delle liste: un VID/PID presente nel codice può essere elencato tra i dispositivi non supportati.
- [ ] Verificare percorso e corrispondenza delle regole udev, accesso al device node e attivazione dei servizi; un servizio inattivo da solo non dimostra la causa.
- [ ] Prima di scaricare nuovi binari, verificare se i pacchetti pertinenti già installati incorporano il driver; conservare hash e offset di estrazione.
- [ ] Preferire il driver OEM del modello esatto, verificare VID/PID nei file INF e distinguere compatibilità dichiarata da funzionamento osservato.
- [ ] Riutilizzare una sessione di disassemblaggio per le query correlate e salvare le evidenze complete: più comandi CLI separati possono ripetere l’importazione e l’analisi del medesimo binario.
- [ ] Distinguere registrazione MCP, strumenti visibili nella sessione e provider operativo; usare la CLI autorizzata quando serve senza dichiarare connesso un MCP non verificato.
- [ ] Conservare URL completo, hash e verifica del formato dei download: una risposta HTTP riuscita può contenere HTML invece di ZIP o eseguibile.
- [ ] Confrontare adattatore sensore, motore e storage dichiarati nell’INF; non confondere un export WinBio presente con compatibilità completa del wrapper.
- [ ] Separare gli import assenti dai percorsi realmente eseguiti: alcuni loader creano stub e falliscono solo quando li chiamano.
- [ ] Ispezionare l’inizializzazione di tool di terze parti: l’apertura può resettare il dispositivo, iniziare pairing o scrivere dati anche prima dell’enrollment.
- [ ] Prima di riprodurre un singolo comando OEM, ricostruirne i prerequisiti: un comando può richiedere un blocco di inizializzazione selezionato per ROM, flag e modalità. Separare autorizzazione al pairing da caricamento di codice/configurazione; non usare un blob di reset o di un sensore correlato al posto di quello corretto.

- [ ] Registrare separatamente VID/PID USB e product interno restituito dal protocollo. Un product condiviso tra USB e SPI è un indizio per la ricerca, non una prova di identità hardware o di identiche chiavi e memoria.
- [ ] Distinguere richieste di geometria/ID memoria dalla lettura dei contenuti. Un errore di lettura può dipendere da autenticazione o assenza di memoria: non trattarlo automaticamente come problema di permessi.

- [ ] Separare in modo esplicito lettura metadati, negoziazione TLS iniziale, autenticazione completa e modifica del pairing; il successo di una fase non prova le successive.
- [ ] Valutare separatamente esito della prova ed esito della chiusura: un exit code zero o una risposta di due byte non confermano la chiusura. Conservare lo status esatto; una query successiva riuscita prova la disponibilità di quel comando, non l’azzeramento del canale sicuro.
- [ ] Rivedere le opzioni di log dei tool crittografici prima di eseguirli: alcuni riferimenti stampano chiavi private anche a livello INFO. Salvare credenziali fuori dai log e dai file tracciati, con permessi restrittivi.

## Controlli emersi dalle acquisizioni RAW (Condizionali: applicare solo se pertinenti all'architettura del sensore osservato)

> [!NOTE]
> Applicare solo a sensori che trasmettono matrici RAW di campioni al computer host. Sensori «match-on-chip» o «match-on-host» con crittografia proprietaria non espongono necessariamente questo flusso o questo formato di frame.

- [ ] Sincronizzare con un «pronto» prima della finestra di contatto; distinguere prompt inviato, evento misurato e azione confermata. Una prova senza contatto è una baseline utile.
- [ ] Ricavare stride, intestazioni e contatori dal flusso: la larghezza nominale del programma può differire dalla riga effettivamente trasmessa.
- [ ] Verificare parità delle letture ripetute sui dati locali; sensori correlati possono richiedere un ordine diverso. Più frame temporali non significano maggiore area acquisita.
- [ ] Controllare separatamente firma OEM, TLS Finished, MAC, status applicativo, arresto acquisizione e close_notify. Chiudere anche sui ritorni anticipati.
- [ ] Gestire la configurazione rimasta dalla prova precedente prima di reinizializzare. Annotare status esatti e verificare il percorso di pulizia OEM; non dedurre il significato dal solo numero.
- [ ] Usare nomi unici e creazione esclusiva per le acquisizioni; limitare durata e dimensione, proteggere file e chiavi, conservare i tentativi falliti.
- [ ] Verificare l’encoder dichiarato dal programma e il ramo OEM che lo decodifica. Un piano di campioni codificati può contenere forme apparentemente simili a creste: aspetto visivo e minutiae estratte non provano una decodifica corretta.
- [ ] Verificare ordine di media, trasformazione, correzione baseline e clipping; la sottrazione dopo saturazione non recupera l’informazione persa. Impostare esplicitamente densità e flag nei diagnostici NBIS e distinguere ipotesi da metadati verificati.
- [ ] Distinguere segnale di contatto, immagine ricostruita e riconoscimento: ciascuno richiede una verifica propria.

## 4. Esperimento e implementazione

- [ ] Confronta l’esperimento con l’ambito autorizzato: procedi direttamente se già coperto; chiedi solo il consenso mancante per acquisizioni sensibili o modifiche al dispositivo/sistema.
- [ ] Analizza evidenze con strumenti disponibili, REA o alternative. Interrompi e documenta blocchi tecnici come cifratura o firmware firmato.
- [ ] Crea un branch dedicato in libfprint solo dopo aver controllato lo stato Git. Segui architettura, istruzioni di build e convenzioni presenti nella revisione clonata.
- [ ] Implementa la modifica minima giustificata dalle evidenze. Esegui build e test pertinenti, registrando comandi, exit code e limitazioni.
- [ ] Installa il driver o modifica servizi di sistema solo se incluso nell’ambito autorizzato. Tratta PAM/login separatamente. Una build riuscita non dimostra il funzionamento hardware.

## 5. Test interattivo

- [ ] Spiega cosa misurare, cosa verrà raccolto e dove verrà conservato. Definisci timeout, condizioni di successo e interruzione.
- [ ] Mostra **ATTENDI** durante preparazione e analisi. Avvia il test; verifica che sia pronto prima di chiedere **APPOGGIA IL DITO**.
- [ ] Indica un’azione per volta: dito da usare, durata o segnale da attendere. Chiedi **RIMUOVI IL DITO** quando il test lo richiede.
- [ ] Prosegui su evento osservato o conferma esplicita. Alla scadenza segnala il timeout, ferma il test e salva lo stato; non continuare una sequenza non sincronizzata.
- [ ] Se puoi ottenere direttamente gli eventi del test, non chiedere conferme ridondanti. Non riutilizzare risultati simulati come prove hardware.
- [ ] Registra esito reale, errori, tentativi e riproducibilità, evitando dati biometrici nel report pubblico.

- [ ] Ripetere l’intero percorso fino alla cattura: il successo di sola autenticazione/inizializzazione non garantisce che il comando di acquisizione successivo funzioni.
- [ ] Verificare estrazione, coerenza delle caratteristiche e confronto tra appoggi indipendenti separatamente. Un self-match o un numero sufficiente di minutiae non dimostra riconoscimento; conservare soglia e score del test.

## 6. Valutazione e consegna

- [ ] Aggiorna checkpoint dopo ogni fase e prima di attendere l’utente: fase, ultimo esito, processo attivo, prossimo comando e azione richiesta.
- [ ] Itera discovery → ricerca → ipotesi → esperimento → implementazione → build → test → valutazione finché esiste un prossimo passo utile autorizzato.
- [ ] A un blocco, riporta evidenza e requisito concreto per sbloccarlo; non promettere supporto universale.
- [ ] Consegna diff, revisioni, risultati verificati e limiti. Prepara una bozza di contributo upstream quando sostenuta dai test.
- [ ] Rivedi separatamente i file da pubblicare; niente push o merge request senza autorizzazione.

## Contenuto minimo di checkpoint.md

```text
Obiettivo / sensore:
Fase e stato (ATTENDI, AZIONE RICHIESTA, BLOCCATO, COMPLETATO):
Ambiente e revisioni repository:
Fatti verificati e riferimenti alle evidenze:
Ipotesi e confidenza:
Esperimenti conclusi (comando, data, esito):
Autorizzazioni e limiti:
Processi attivi / timeout / come interrompere:
Azione fisica eventualmente richiesta:
Prossimo passo riproducibile:
```

Mantieni i dati sensibili nelle sole directory locali ignorate. Prima di tracciarle altrove, verifica la policy e il consenso per ogni acquisizione.

## Modalità di esecuzione: Pannello GUI vs Terminale

In conformità con `AGENTS.md`:

- **Pannello GUI (modalità predefinita e obbligatoria):** quando un display grafico è disponibile (`DISPLAY` o `WAYLAND_DISPLAY`), ogni operazione che richiede contatto fisico con il sensore, attesa dell'utente, cattura biometrica, registrazione/verifica `fprintd` o supervisione interattiva **deve** passare dal pannello GUI locale (`scripts/open-panel.py`). L'agente prepara e valida `.rea-fs.local/panel/plan.json`, indica all'utente quale prova eseguire e attende la risposta in chat prima di esaminare i log su file.
- **Terminale diretto (eseguito dall'agente):** è limitato a comandi passivi in sola lettura (`lsusb`, `udevadm`, `rea-fs.py preflight`), compilazioni (`cargo`, `meson`, `ninja`), verifiche offline (`rea-fs.py check`, `unittest`) e analisi statica del firmware/driver.
- **Fallback a finestra temporale nel terminale:** consentito **solo** se l'ambiente è privo di display grafico (`DISPLAY` e `WAYLAND_DISPLAY` assenti o connessione SSH pura senza inoltro X11/Wayland). In questo caso l'agente presenta l’intera sequenza prima dell’esecuzione, con una durata totale delimitata e chiari segnali visibili o sonori nel terminale; al ritorno analizza la registrazione.

## Strumenti di supporto e valutazione

Usa `scripts/rea-fs.py init-session` per un checkpoint nuovo e `preflight` per i metadati locali; se esiste una sessione pertinente, riprendila invece di crearne un’altra. `check` esegue solo verifiche offline e build; i programmi diagnostici hardware non sono registrati fra i test Meson automatici.

Per valutare il matcher separa appoggi di enrollment, sviluppo della soglia e verifica finale. Più finestre dello stesso contatto non sono appoggi indipendenti. Confrontare il massimo su più template cambia la distribuzione dei punteggi: misurare di nuovo anche le false accettazioni. Mantieni le etichette dei tentativi nel CSV privato descritto nel README; `summarize` non sceglie né modifica soglie. Un successo sullo schermo di blocco prova una specifica integrazione, non una FAR/FRR di popolazione.

## Pannello locale degli esperimenti

Il pannello (`scripts/open-panel.py`) è il canale asincrono principale per le prove con l'utente (dettagli completi in `docs/experiment-panel.md`). Preparare `plan.json` con istruzioni e comandi espliciti salvandolo atomicamente; validarlo con `python3 scripts/experiment-panel.py validate --session-dir .rea-fs.local/panel`. Chiedere all’utente di aprire `python3 scripts/open-panel.py` e scegliere la prova indicata. Il pannello non concede autorizzazioni: confrontare comunque categorie e ambito con le preferenze e il consenso in chat.

Al ritorno dell'utente in chat, l'agente non chiede di incollare l'output ma legge direttamente i file su disco:
1. `.rea-fs.local/panel/panel-status.json` (conferma validità del piano caricato);
2. `.rea-fs.local/panel/runs-index.jsonl` (cronologia completa append-only di tutti i run eseguiti);
3. `.rea-fs.local/panel/latest-run.json` (ultimo tentativo registrato);
4. Nella `run_dir` del tentativo: `summary.json` (con `user_notes`), `events.jsonl` e facoltativamente `output.log`.

Distinguere sempre eventi device, simulation e user. Non inserire chiavi, template, immagini o output arbitrario nei messaggi strutturati. `raw_log` è una scelta esplicita e può contenere dati sensibili del comando; controllare prima il logging del programma. Non avviare contemporaneamente un esperimento dal terminale/agente mentre il pannello controlla lo stesso sensore.

## Qualità, enrollment e preparazione upstream

Prima di aumentare il numero di appoggi leggere `docs/image-quality-and-enrollment.md`. Verificare separatamente baseline vuota, stabilità temporale, area utile, novità fra appoggi e distribuzione dei punteggi. Non equiparare frame aggiuntivi a nuova copertura o geometria del matcher a posizione anatomica. Identificare se il sensore esegue il matching internamente o restituisce immagini; valutare il framework imaging upstream prima di mantenere un matcher dedicato.

Nel piano GUI usare titoli concreti e `next_steps` per spiegare il ritorno alla chat. A fine prova l’agente legge il piano immutabile e i log del run: il messaggio copiabile della GUI indica quei percorsi. Eseguire comandi selezionati non richiede un terminale incorporato; i programmi interattivi/privilegiati richiedono invece un terminale appropriato. Non chiedere all’utente di ricopiare comandi che il pannello può già eseguire con un clic.

Per riprendere dal pannello senza messaggi copiati: leggere `latest-run.json` nella directory di sessione, verificare ID piano/run, timestamp e `last_event`, quindi leggere piano immutabile e `events.jsonl` del percorso indicato. Se il riepilogo mostra solo `started`, controllare processo e log prima di dichiarare conclusa la prova. Cercare anche i run precedenti quando si valuta una serie; non limitarsi all’ultimo successo. Durante la simulazione non chiedere contatti fisici né interpretare gli eventi generati come prova del sensore.
