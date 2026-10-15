# Stato della ricerca: Synaptics USB 06cb:00dc

Risultati locali del 15 ottobre 2026 su Acer Aspire A514-54, revisione USB 0154. Sono evidenze di un prototipo diagnostico, non supporto disponibile in libfprint. Nessuna modifica al login e nessuna pubblicazione delle acquisizioni.

## Verificato sul dispositivo

- ROM 6.20 build 154, product interno 0x33. Metadata modulo 0190:a2; tabella OEM: tipo 0x2449, nome 86C TM-P3568-001.
- Driver Acer 5.5.2.1124 corrispondente al modello e all’ID USB. DLL analizzata staticamente, mai eseguita. SHA256 di `synaWudfBioUsb124.dll`: `33171a48999f2415617d54e9ba44dcb90f0f05599190e948f62fbc780fc2d916`.
- Associazione riuscita dopo l’inizializzazione OEM pertinente nella stessa sessione USB. Il certificato pubblico restituito dall’associazione rappresenta la chiave host; non va confuso con la chiave ECDH del sensore.
- Firma della chiave ECDH verificata con la root OEM selezionata per questa ROM e i suoi flag. TLS completo: Finished del server e MAC dei record verificati; chiusura cifrata confermata.
- Lettura cifrata dei metadata di fabbrica; gruppi duplicati identici. Nessuna cancellazione flash o calibrazione persistente eseguita.
- Inizializzazione di cattura e comando RAW accettati. Due acquisizioni complete: baseline senza dito confermata dall’utente e prova con appoggio/rimozione confermati. Arresto `04` e chiusura TLS riusciti.
- Flusso: 152 byte per riga, di cui 8 di intestazione e 144 di campioni; 112 righe per frame. Indice riga e contatore frame coerenti su 1.971 frame per acquisizione.
- Le coppie adiacenti a indici zero-based `(0,1), (2,3), …` sono ripetizioni: differenza assoluta media circa 2,1 contro 27,9 per la parità alternativa durante il contatto. Questo sostiene una ricostruzione candidata di 144×56 pixel.
- Dopo sottrazione della baseline, il segnale cambia nettamente durante il contatto e torna vicino al livello iniziale dopo la rimozione. Ricostruzione candidata generata solo localmente; utente conferma creste visibili. Orientamento e utilità per riconoscimento ancora da verificare.

I numeri riguardano questo dispositivo e questa configurazione. Il decoder SPI di riferimento usa un’altra altezza e un’altra parità delle coppie: non copiarle senza verifica sul campione locale.

## Strumenti e riferimenti impiegati

- REA 6.5.0, Ghidra 12.1.2 e Java 21 per il driver OEM; sessioni riutilizzate e snapshot locali.
- Python, PyUSB e cryptography per esperimenti limitati e autenticazione; NumPy e Pillow per l’analisi offline e la ricostruzione candidata.
- `uunicorn/python-validity`: confronto di protocollo e routine TLS selezionate dopo ispezione. Non eseguito il suo startup automatico.
- `sandbranch/x1-tablet-gen3-fingerprint`: confronto dei programmi di acquisizione e ricostruzione SPI. Differenze rispetto a USB verificate separatamente.
- Permesso temporaneo circoscritto al nodo USB mediante ACL; le prove successive non hanno richiesto sudo. Ricontrollare l’accesso dopo riconnessione o riavvio.

La sessione privata contiene revisioni dei riferimenti, provenienza dei download, evidenze REA, esperimenti e risultati. Chiavi, materiale di associazione, blob OEM e scansioni restano fuori dal repository. Un clone di REA-FS non contiene ancora un esperimento hardware eseguibile autonomamente.

## Verifiche successive

- Decoder offline con soli dati sintetici nei test: 8 test superati. Sui dati privati valida tutti i 1.971 frame completi e coincide con un secondo decoder NumPy per il frame confrontato.
- Estrattore NBIS della libreria installata (libfprint 1.95.2+tod1): immagine candidata 9 minutiae, polarità inversa 8; controllo uniforme rifiutato con 0. Nessun matching o enrollment eseguito, nessun template salvato.
- Correzione locale della chiusura TLS su errore dell’inizializzatore: regressione offline riprodotta prima del fix, poi superata; verificati anche fallimento della chiusura e handshake non autenticato.
- Due cicli hardware consecutivi di sola autenticazione/pulizia/inizializzazione/chiusura riusciti, senza reset e senza nuove scansioni.
- Build locale del clone libfprint 1.94.100 riuscita con il solo driver `virtual_image`; nessuna installazione di sistema. È una verifica dell’ambiente di sviluppo, non una build del driver per 06cb:00dc.

Bozorth richiede almeno 10 minutiae computabili (`nbis/include/bozorth.h` della revisione studiata). La prima media temporale ne produce 9; cinque finestre separate dello stesso appoggio producono 7–10. Rimozione degli offset per colonna e smoothing non hanno migliorato questo campione. Servono appoggi indipendenti e una pipeline validata prima di valutare il matching.

La suite della build locale ha riportato 5 test riusciti, 1 fallito e 34 saltati: il fallimento AppStream riguarda il tag `provides` vuoto generato nella build col solo driver virtuale. Test driver saltati anche per introspection disabilitata. Non è una suite interamente superata.

## Passaggi ancora aperti

- [ ] Validare ricostruzione, orientamento e qualità dell’immagine senza confondere rumore o pattern fissi con creste.
- [ ] Rendere robusti chiusura TLS e ripetizione delle catture anche sui percorsi di errore.
- [ ] Definire provenienza e distribuibilità delle configurazioni necessarie prima di proporre codice upstream.
- [ ] Scegliere l’integrazione appropriata; sviluppare e compilare il driver seguendo le convenzioni del progetto scelto.
- [ ] Verificare acquisizione ripetibile, enrollment e matching reali. Solo allora valutare PR/MR con l’utente.

## Prova successiva con due appoggi

Il flusso registra due contatti separati, con ritorno alla baseline fra i due. L’avvio precedente era stato rifiutato con status `0x0450` dopo pulizia e inizializzazione accettate; chiusura TLS confermata. Un reset USB seguito dallo stesso esperimento ha consentito la cattura. Il significato dello status e la causa precisa restano da isolare: due cicli di sola inizializzazione riusciti non provano che sia ripetibile l’intera acquisizione.

Dieci finestre temporali prefissate dei due appoggi producono da 6 a 11 minutiae. Il controllo offline che confronta un’immagine con sé stessa passa la soglia 40; due finestre diverse dello stesso appoggio ottengono 23/40, e una coppia di acquisizioni distinte richieste con lo stesso dito ottiene 0/40. Questi confronti non passano. La pipeline corrente non dimostra quindi riconoscimento affidabile, anche quando il numero di minutiae supera il minimo computabile. I template dell’esperimento restano solo in memoria e non vengono serializzati.
