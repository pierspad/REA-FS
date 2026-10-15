# Come funziona l’indagine, dal lettore al desktop

REA-FS è un percorso di reverse engineering per l’interoperabilità, con strumenti e un prototipo di driver. Il risultato desiderato è un lettore usabile su Linux; la possibilità di arrivarci dipende dal dispositivo. Un agente può organizzare ricerca, codice e prove, ma non può dedurre un protocollo corretto dal marchio né sostituire prove hardware con una build riuscita.

## 1. Identificare il problema reale

«Il lettore non funziona» può significare hardware assente dal bus, dispositivo non supportato, pacchetto troppo vecchio, permessi insufficienti, credenziali mancanti, enrollment non eseguito o integrazione desktop incompleta. Prima si identifica il bus (USB, SPI, I²C), poi ID, revisione e driver installati. Su USB, il VID identifica il produttore e il PID un prodotto dichiarato, ma non descrivono tutta l’elettronica interna.

La ricerca confronta la lista ufficiale, il codice e le issue. Un ID in una blacklist non è supporto. Una patch per un chip simile può essere utile senza essere applicabile. Per questo il caso Synaptics del repository è un esempio concreto, non una configurazione da inviare a ogni lettore.

## 2. Salvare una base riproducibile

Si registrano sistema, kernel, pacchetti, revisioni dei repository e provenienza degli artefatti OEM. I clone in workspace sono indipendenti: cambiare libfprint non cambia automaticamente REA-FS. Git conserva il lavoro sorgente; le sessioni private conservano checkpoint ed evidenze sensibili.

Il checkpoint separa tre cose: fatti osservati, interpretazioni e prossima domanda. «Il dispositivo ha risposto» è un fatto più debole di «la cattura è riuscita». Per dimostrare una cattura servono dati validi, contatori coerenti, contatto osservato e cleanup verificato. Un report che lascia queste distinzioni implicite induce l’agente successivo a saltare fasi.

## 3. Studiare il software esistente

Si cerca prima documentazione pubblica e codice libero. Se non bastano, si può studiare un pacchetto OEM ottenuto legittimamente, conservandone hash, versione e modello supportato. Un INF Windows aiuta a identificare il driver; una DLL può contenere tabelle di moduli, configurazioni, algoritmi e protocolli.

L’analisi statica legge il file senza eseguirlo: stringhe, import, riferimenti alle funzioni, disassemblato e pseudocodice. Il pseudocodice di un decompilatore è una ricostruzione e può sbagliare tipi o confini; si confronta con byte, chiamanti e risultati osservati. REA può rendere queste analisi accessibili all’agente se il suo MCP e i provider sono disponibili; non è obbligatorio per leggere normale sorgente C.

L’analisi dinamica e le tracce mostrano invece cosa viene effettivamente scambiato. Richiedono controllo degli effetti collaterali: aprire un tool può inizializzare, resettare o effettuare pairing. Si leggono le routine di startup prima dell’esecuzione. Non si eseguono comandi OEM sconosciuti solo perché presenti in una traccia.

## 4. Autenticare host e sensore

Nel caso studiato il sensore richiede un’associazione e un canale cifrato per comandi sensibili. L’host conserva una chiave privata e materiale pubblico; il pairing non è la registrazione di un dito. Sono due procedure differenti: la prima stabilisce chi può comunicare, la seconda crea il riferimento biometrico dell’utente.

La sessione ha verificato una firma sulla chiave ECDH usando il materiale OEM pertinente. Il driver C attuale carica la chiave pubblica già estratta: il percorso riproducibile di verifica della sua provenienza e preparazione su un nuovo dispositivo deve ancora essere completato. Il successo locale dell’handshake non sostituisce questa verifica di bootstrap.

ECDH consente di derivare un segreto condiviso. La PRF deriva chiavi del canale, AES cifra i record e HMAC ne verifica l’integrità secondo il protocollo osservato. Finished lega la negoziazione al transcript atteso. Questo canale protegge la comunicazione: non misura la qualità dell’impronta e non dimostra che un dito sia autentico.

Il protocollo osservato è basato su TLS ma contiene particolarità OEM. Il client implementa HMAC-SHA256 e framing propri; non va presentato come una generica implementazione TLS 1.2 conforme. La suite standard 0xc005 è denominata `TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA` nel [registro IANA](https://www.iana.org/assignments/tls-parameters). Nomi simili non giustificano l’assunzione che ogni dettaglio OEM coincida con lo standard, né una modifica alla cieca per renderlo apparentemente conforme.

## 5. Inizializzare la cattura

Dopo l’autenticazione si prepara il percorso di acquisizione. Sul dispositivo studiato entrano in gioco pulizia `1a`, configurazione `06`, cattura `02` e stop `04`. L’ordine dipende dallo stato lasciato dalle operazioni precedenti. I payload devono corrispondere a ROM e modulo; lo stesso VID/PID non basta a scegliere un blocco sconosciuto.

Una risposta USB riuscita prova solo che il trasferimento è avvenuto. Bisogna anche leggere il codice applicativo. Per la robustezza finale del driver restano da completare i controlli sistematici degli status e tutti i cleanup in errore. La chiusura della libreria USB non è automaticamente una chiusura corretta del canale cifrato.

## 6. Da byte a immagine

Il sensore osservato trasmette 112 righe da 152 byte: 8 di intestazione e 144 di campioni. Quindi il frame è di 17.024 byte; 16.128 sono campioni e 896 sono intestazioni. Controllare prefissi, indice riga e contatore evita di trattare un flusso disallineato come una fotografia.

Le righe adiacenti sono letture ripetute: la media delle coppie produce 56 righe. In modalità encoder 2 i valori sono ancora codificati. Si applica una trasformazione a gruppi di 16, ricostruita e verificata con controlli indipendenti. L’output candidato ha 144×56 campioni. Resta distinta la questione della densità fisica e della calibrazione OEM esatta.

La baseline è il segnale del sensore libero. Sottraendola si cerca di isolare la variazione dovuta al dito. Il clipping tronca valori fuori intervallo: dopo la saturazione non si può recuperare ciò che è stato perso. Il nuovo percorso live conserva valori numerici non saturati, sottrae la baseline misurata e passa al filtro. È una correzione numerica motivata, non la ricostruzione completa della calibrazione di fabbrica.

Misurare il sensore libero durante ogni apertura è fragile se l’interfaccia invita l’utente a toccarlo troppo presto. La prova KDE fallita ha mostrato proprio l’assenza di uno stato libero accettato, ma non ne ha ancora isolato la causa. Una soluzione stabile dovrà distinguere contatto, baseline affidabile, variazioni ambientali e stato della configurazione; non limitarsi ad aumentare una soglia RMS.

## 7. Più frame e più appoggi risolvono problemi diversi

Venticinque frame durante una singola pressione possono ridurre rumore casuale se il dito resta fermo. Se si muove, una media non registrata può sfocare le creste. Non aumenta automaticamente la porzione del dito osservata.

Cinque appoggi con piccole variazioni forniscono invece viste diverse. Il driver ora conserva cinque campioni filtrati, separati da un rilascio osservato. Questo è supportato dal modello di enrollment libfprint/fprintd: il driver dichiara il numero di passaggi e segnala i progressi. Il frontend può visualizzarli. L’[API fprintd](https://fprint.freedesktop.org/fprintd-dev/Device.html) prevede numero di passaggi, risultati intermedi e proprietà di contatto.

Il comportamento sofisticato ricordato da alcune interfacce non è garantito dal solo fprintd: qualità, riconoscimento di appoggi duplicati, stima di copertura e fusione dei campioni dipendono dall’implementazione. Qui non si costruisce ancora una mappa anatomica del dito né si chiede automaticamente la zona mancante. Un miglioramento successivo può misurare qualità e novità delle viste, evitando di accettare cinque pressioni quasi identiche o immagini scarse.

## 8. Come decide il matcher

Il filtro passa-banda enfatizza le strutture con frequenze compatibili con le creste. Il matcher attuale cerca traslazione e rotazione e calcola una correlazione normalizzata nella regione sovrapposta. Il risultato esprime somiglianza secondo questo algoritmo, non una probabilità che l’utente sia il proprietario.

Un’area piccola contiene poche biforcazioni e terminazioni: un estrattore di minutiae può non produrre abbastanza elementi per Bozorth. Questo spiega le difficoltà osservate, ma non prova che NBIS sia universalmente inutilizzabile su sensori piccoli o che la correlazione sia il matcher definitivo.

Con cinque campioni si prende il miglior punteggio. Può migliorare la tolleranza al posizionamento, ma offre anche cinque opportunità a un dito diverso di superare la soglia. Non si può mantenere una vecchia stima di sicurezza senza ripetere la valutazione. Abbassare la soglia per ottenere più successi può aumentare le false accettazioni.

Servono appoggi esclusi dall’enrollment e dalla selezione della soglia, altri giorni e dita, tentativi falliti conteggiati e tempi separati. Un controllo con l’indice sinistro della stessa persona è utile ma non rappresenta una popolazione di impostori. Zero errori su pochi tentativi non significa tasso d’errore zero. Le prove attuali dimostrano alcuni percorsi funzionanti, non resistenza a falsificazioni, attacchi di replay o una certificazione biometrica.

## 9. Dal modulo allo sblocco

La catena è: applicazione o schermata di blocco → PAM quando usato → fprintd → libfprint TOD → modulo del sensore → USB. Per enrollment e verifica si possono usare anche direttamente le API fprintd, senza passare da PAM. Il modulo converte comandi generici in operazioni specifiche e restituisce stati, template ed esiti.

fprintd conserva il template dell’utente; i file OEM in `/var/lib/fprint/syna_00dc` sono materiale di comunicazione e inizializzazione, non il posto in cui questo driver salva da solo tutti i template fprintd. La serializzazione versionata consente di rileggere i cinque campioni; il driver rifiuta tipi e lunghezze inattesi prima di confrontarli.

Un `verify-match` nel terminale prova una verifica via fprintd. KDE può aggiungere tempi di apertura, prompt, cancellazione PAM e comportamenti del frontend. Il driver invia proprietà di contatto, ma non disegna i messaggi di KDE. Per questo la prova desktop si valuta separatamente e si conserva sempre il percorso con password.

Il lettore autentica uno sblocco; il gesto di **bloccare** il PC resta una funzione del desktop (ad esempio Meta+L). Usare il tocco del sensore come comando di blocco richiederebbe una funzione aggiuntiva e non è stato implementato.

## 10. Che cosa si può pubblicare e dove

Il progetto è correttamente descrivibile come reverse engineering per l’interoperabilità e sviluppo di un driver sperimentale. È anche documentazione e tooling: non ogni sua parte fa reverse engineering e non ogni dispositivo arriverà a un driver.

Il [documento HACKING upstream](https://gitlab.freedesktop.org/libfprint/libfprint/-/blob/master/HACKING.md), letto nel clone locale indicato nell’audit, rifiuta l’integrazione di driver proprietari. Questo non equivale a una sentenza secondo cui qualunque payload cifrato impedisca per sempre un driver libero. Distinguere un motore proprietario eseguito sull’host da firmware/configurazione e chiarirne provenienza e licenza. Per una MR servono sia questa analisi sia codice e test adeguati; decide la review upstream.

Il modulo attuale è scritto per TOD. synaTudor usa lo stesso tipo di integrazione ma un motore Windows rilinkato; il nostro modulo implementa parte della catena autonomamente. Non si incastra automaticamente nel loader synaTudor e non entra automaticamente nella libfprint ordinaria. Può avere una distribuzione propria, purché dichiari dipendenze, modello provato, bootstrap mancante e limiti. Un pacchetto AUR può distribuirne la ricetta di build, non trasformare credenziali personali in file distribuibili.

## 11. Reverse engineering e diritto

Il reverse engineering non è automaticamente illecito. Nell’UE la [direttiva 2009/24/CE](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32009L0024) distingue studio del funzionamento durante l’uso legittimo (art. 5.3) e decompilazione indispensabile per l’interoperabilità (art. 6). Quest’ultima richiede diritto d’uso della copia, informazioni non già facilmente disponibili e attività limitate alle parti necessarie. Le informazioni ottenute hanno limiti d’uso e comunicazione; non autorizzano a riprodurre un programma sostanzialmente simile nella sua espressione o altri atti lesivi del copyright.

Sono indicazioni generali, non un giudizio legale sul pacchetto Acer o su ogni operazione eseguita. La valutazione concreta dipende anche dalla legge applicabile e dalla provenienza delle copie. Studiare un algoritmo e implementare autonomamente un’interfaccia è distinto dal distribuire una DLL, un blob, una chiave o codice copiato. La LGPL del nostro repository non concede diritti sui materiali del produttore.

L’entropia di un blocco non prova cifratura, firma, impossibilità di ricostruzione o licenza. Dati compressi possono avere entropia alta. Una firma autentica un contenuto ma non è una licenza; ricostruire una chiave di firma privata del produttore non è un percorso necessario da promettere. Se una firma o una cifratura impedisce la sostituzione di un payload, è un possibile blocco tecnico da dimostrare e documentare.

La prassi di questo progetto è conservare fonti e hash, scrivere implementazioni originali del protocollo, rispettare le attribuzioni e tenere dati biometrici e credenziali fuori dai contributi. Prima di distribuire materiale OEM, bisogna chiarirne i diritti: «si scarica gratis» e «è in un repository GitHub» non bastano.
