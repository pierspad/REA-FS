# Relinking, driver nativo e contributo upstream

Valutazione del 15 ottobre 2026. Sorgente synaTudor locale: `31dfdb06107fd1c35c9f9ceae72617e98eccc43a`. È un confronto del codice e dei checkpoint, non una prova di funzionamento della DLL Acer tramite relinking.

## Che cosa è stato effettivamente provato

All’inizio era già installato un modulo synaTudor, ma il sensore non veniva esposto da fprintd. La tabella del clone seleziona `06cb:00be`; il pacchetto predefinito scarica Lenovo `r19fp02w.exe` e incorpora `synaFpAdapter104.dll` e `synaWudfBioUsb104.dll`. Il nostro esemplare è `06cb:00dc`; il suo pacchetto Acer contiene `synaBscAdapter124.dll` e `synaWudfBioUsb124.dll`.

Sono state estratte e analizzate staticamente le DLL, gli import e le interfacce WinBio. **Non è documentata una prova di esecuzione della DLL Acer 124 nel loader synaTudor.** Quindi non si può concludere né «funziona cambiando il PID» né «il relinking è stato tentato e si è dimostrato impossibile». Il confronto testuale degli import non implementati non dimostra quali funzioni sarebbero effettivamente chiamate.

## Come funziona synaTudor

Il README descrive un progetto separatosi dal ramo di reverse engineering per eseguire il driver Windows su Linux x86-64. Il codice del loader risolve gli import e le relocation PE; il supporto WinAPI/WDF sostituisce i servizi che il driver si aspetta da Windows. `libtudor/src/tudor/driver.c` inizializza le DLL, chiama `FxDriverEntryUm`, poi richiede le interfacce sensore e motore WinBio.

L’host dedicato espone queste funzioni tramite IPC al modulo TOD e applica sandboxing. Il processo host, il launcher e il modulo libfprint sono componenti distinti. Il README della CLI avverte che la sua protezione non equivale a quella dell’integrazione: non usare una CLI privilegiata come primo esperimento indiscriminato su una nuova DLL.

In questo schema decoder, selezione campioni, enrollment e matching possono restare nel motore del produttore. Se quel motore funziona per un dispositivo, non serve riscriverlo per ottenere un primo uso pratico. Ma adattare il wrapper a una nuova generazione può richiedere API Windows, ABI WinBio o sequenze WDF aggiuntive: non è soltanto aggiungere un ID USB.

## Perché il nostro modulo ha decoder e matcher in C

Il percorso seguito ha ricostruito il protocollo USB e dimostrato acquisizioni tramite un programma indipendente. Per trasformare le acquisizioni in autenticazione senza eseguire il motore Windows servivano decodifica, enrollment e matching sull’host. Questa è la ragione funzionale della reimplementazione, non la prova che fosse l’unico modo o quello meno costoso.

Il beneficio è poter ispezionare e correggere il codice, confrontare pipeline e ridurre la dipendenza dal runtime Windows. Il costo è assumersi calibrazione, qualità delle immagini, accuratezza del matcher e validazione. La somiglianza osservata su pochi campioni non rende il matcher equivalente a quello OEM. Il caricamento di dati OEM necessari resta un limite distinto dalla presenza di codice C libero.

## Decisione pratica

Per l’obiettivo «rendere usabile il mio esemplare» entrambe le strade meritano una valutazione. Abbiamo già un percorso nativo che acquisisce e verifica, quindi lo conserviamo come base e strumento di confronto. Non abbandonarlo né duplicare settimane di lavoro sul wrapper prima di misurare il suo primo blocco reale.

Il prossimo esperimento di relinking dovrebbe essere graduato:

1. Verificare hash e architettura delle DLL Acer 124 e l’esatto layout delle interfacce esportate. Usare il materiale locale già disponibile, senza scaricare alla cieca altri pacchetti.
2. Preparare un harness separato dal sensore e dai dati reali: caricamento PE e risoluzione import senza entry point, poi report degli import risolti/stub. Uno stub mancante è un rischio di esecuzione, non un fallimento già osservato.
3. Solo dopo ispezione, eseguire entry point e inizializzazione in un processo isolato con filesystem privato e senza accesso USB. Registrare la prima chiamata non supportata e il suo percorso, con timeout. Non sostituire stub con «successo» fittizio.
4. Stimare il lavoro dopo quel primo blocco. Se è circoscritto, implementare e verificare l’API; se richiede una grossa porzione di WDF/WinBio, confrontare il costo con completare il driver nativo.
5. Consentire accesso USB solo quando il percorso di inizializzazione e gli effetti su pairing/storage sono chiari. Usare uno store separato, senza sovrascrivere credenziali o enrollment funzionanti.

Questa sequenza è un piano, non un elenco di prove già eseguite. L’esecuzione di un motore OEM non garantisce automaticamente qualità o sicurezza, ma può offrire una baseline utile se resa operativa correttamente.

## Che cosa manca per libfprint upstream

- Portare il codice dal plugin ABI TOD al driver API interno dell’albero upstream, con trasferimenti/stati e convenzioni del progetto. La registrazione di un plugin `.so` non è il formato di una MR upstream.
- Bootstrap riproducibile su un dispositivo nuovo, verifica di ROM/modulo prima di usare configurazioni specifiche, provenienza delle chiavi pubbliche e licenze dei payload.
- Gestione completa di errori OpenSSL, risposte applicative, record malformati, cancellazione, scollegamento e chiusura del canale su tutti i rami. I nuovi test su status e tipi record chiudono alcuni casi, non tutti.
- Baseline e qualità affidabili anche al primo tocco del desktop, appoggi validati per qualità/novità, valutazione indipendente del matcher e dei tempi.
- Test riproducibili senza dati biometrici reali pubblicati, documentazione del protocollo e attribuzioni del codice.

Il file HACKING letto nel clone upstream rifiuta l’integrazione di **driver proprietari**. Quindi adattare synaTudor al nostro lettore sarebbe una possibile soluzione fuori albero, non una scorciatoia per far accettare il motore Windows in upstream. Per dati/firmware/configurazioni OEM serve una discussione specifica con i manutentori e una verifica dei diritti; non è corretto dedurne l’esito dalla sola entropia.

## Fattibilità, costo e diritto

Una parte del lavoro nativo è già dimostrata, quindi il percorso è tecnicamente plausibile. L’accettazione upstream e la robustezza su altre macchine restano da dimostrare. Non è onesto assegnare giorni o euro al relinking prima di aver osservato quale API o inizializzazione blocca davvero la DLL Acer.

Nell’UE studio e decompilazione per interoperabilità hanno condizioni specifiche negli articoli 5 e 6 della direttiva 2009/24/CE. Questo non è un giudizio legale sulla singola DLL: ottenere una copia, eseguirla, studiarla, distribuire un wrapper e redistribuire il binario sono atti distinti. La licenza di REA-FS non concede diritti sui file OEM. Un eventuale pacchetto dovrebbe scaricare o ricevere materiale legittimo secondo i suoi termini, mantenendo credenziali e biometria personali fuori dalla distribuzione.

## Riferimenti

- [synaTudor upstream](https://github.com/Popax21/synaTudor), README e sorgenti locali ai percorsi citati.
- [HACKING libfprint](https://gitlab.freedesktop.org/libfprint/libfprint/-/blob/master/HACKING.md), contenuto letto nel clone locale alla revisione indicata nell’audit.
- [Direttiva 2009/24/CE](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32009L0024).
- [Audit del prototipo](audit-2026-10-15.md) e [processo completo](how-it-works.md).
