# Immagini, calibrazione ed enrollment: cosa verificare

Indagine del 15 ottobre 2026. Questa guida distingue il caso Synaptics osservato dal metodo riutilizzabile per altri lettori. Le proposte sotto sono esperimenti da eseguire, non miglioramenti di accuratezza già dimostrati.

## Da dove viene l’esperienza di Ubuntu

Il desktop presenta la procedura e i progressi; fprintd espone le operazioni; libfprint e il driver determinano acquisizione, numero di passaggi, retry e matching. Una buona animazione non prova che sia stata costruita una mappa anatomica. Il comportamento di un sensore con matching nel firmware può differire da quello di un sensore che restituisce immagini all’host.

Nel clone upstream letto, commit `6f9479c3d55f847c1b3769f28ceb99227f9858cf`:

- `libfprint/fp-image-device-private.h`: default imaging `IMG_ENROLL_STAGES = 5`.
- `libfprint/fpi-image-device.c`, funzione `fpi_image_device_minutiae_detected`: estrae caratteristiche, aggiunge ogni campione valido all’enrollment e incrementa lo stage; gli errori di estrazione possono produrre retry invece di contare come un appoggio valido.
- `libfprint/fpi-print.c`: i template NBIS contengono più insiemi di minutiae; il matching usa Bozorth3. Questo percorso non coincide con il nostro confronto delle immagini filtrate.

Questi file non dimostrano un rilevamento automatico di «punta/lato sinistro/lato destro» o una fusione anatomica completa. Prima di attribuire quella capacità a un’interfaccia occorre identificare sensore, driver e codice realmente usati.

Riferimenti: [API imaging upstream](https://fprint.freedesktop.org/libfprint-dev/libfprint-2-Internal-FpImageDevice.html), [FpImage](https://fprint.freedesktop.org/libfprint-dev/FpImage.html). Per nuove indagini leggere anche sorgenti e test della revisione effettivamente clonata.

## Stato concreto del nostro prototipo

`drivers/syna_tod/src/syna_device.c` acquisisce cinque appoggi separati. Ogni appoggio raccoglie fino a 25 frame, oppure almeno 10 se il dito viene rimosso prima; sottrae la baseline e media i frame, quindi applica un filtro. I cinque appoggi restano cinque template, senza mosaico. La verifica cerca il punteggio massimo fra loro. Il matcher cerca rotazioni da −30° a +30° a passi di 2° e traslazioni, richiedendo overlap geometrico superiore al 65% del rettangolo. La soglia di accettazione è 0,50.

Conseguenze:

- Più frame dello stesso contatto aiutano contro rumore casuale, ma non aggiungono aree del dito. Se il dito si muove, mediarli senza allinearli può sfocare le creste: è un rischio da misurare.
- Cinque appoggi identici non garantiscono cinque aree diverse. Non ci sono ancora un criterio di novità, un controllo completo di qualità o un controllo di coerenza del dito fra gli stage.
- L’overlap del matcher è quello dei rettangoli trasformati, non una maschera affidabile della pelle utile né la percentuale del dito conosciuta.
- Aumentare a dieci template aumenta anche lavoro di confronto e opportunità di falsa accettazione quando si usa il massimo. Misurare tempi e confronti con altre dita, senza abbassare la soglia per far riuscire una demo.

## La lacuna prioritaria della calibrazione

All’apertura il driver media fino a 20 frame (scadenza 15 s, almeno 10 richiesti) e li considera baseline del sensore vuoto. Il codice verifica formato e disponibilità dei frame, ma **non dimostra che il sensore fosse vuoto**. Una baseline stabile può contenere un dito fermo. In cattura si considera vuoto un frame con RMS rispetto alla baseline non superiore a 15; servono tre frame consecutivi prima di chiedere un nuovo contatto.

Il fallimento KDE osservato (`clear=0`, nessun frame di contatto, RMS minimo 35,8 e poi 21,1) è compatibile con una baseline non rappresentativa o una soglia inadatta, ma non identifica la causa. Il successo successivo non la risolve.

Primo esperimento: confrontare apertura a sensore vuoto, contatto già presente all’apertura e contatto solo dopo il pronto; registrare separatamente durata open/TLS/calibrazione/attesa/cattura/matching/close. Conservare privati i frame necessari e salvare nel report aggregato conteggi, rumore temporale, deriva rispetto alla baseline, RMS di vuoto e contatto, esito e motivo del rifiuto. Non pubblicare frame/template/chiavi.

Prima correzione da progettare sui risultati: stato esplicito di calibrazione, rimozione richiesta quando necessaria, controllo di stabilità e qualità della baseline, soglie con isteresi motivate dalla distribuzione vuoto/contatto. La stabilità da sola non basta a escludere il dito. Non adattare continuamente la baseline mentre potrebbe esserci un contatto: si rischia di assorbire l’impronta nello sfondo. Non cambiare parametri OEM non compresi per tentativi.

## Appoggi diversi e copertura: percorso incrementale

1. Stabilizzare acquisizione e baseline. Etichettare gli errori prima di considerarli mancati riconoscimenti.
2. Confrontare selezione del frame migliore, media attuale e media con allineamento. Usare gli stessi dati per il confronto offline e una verifica indipendente finale. Misurare rumore, saturazione, maschera dell’area utile, contrasto locale e coerenza delle creste; non usare il solo contrasto globale come qualità biometrica.
3. Guidare cinque appoggi con piccoli cambiamenti di posizione: centro, lieve inclinazione a sinistra, a destra, verso punta e verso base. Queste sono istruzioni dichiarate, non posizioni osservate. Mantenere una zona comune per poterli confrontare.
4. Stimare registrazione e novità fra campioni accettati, con maschere dell’area utile e controllo di confidenza. Rifiutare duplicati soltanto dopo aver validato il criterio; scarso overlap può significare area nuova, dito diverso o immagine inutilizzabile.
5. Visualizzare eventualmente una **copertura relativa stimata**, con zone non registrabili dichiarate sconosciute. Una posizione assoluta sul dito non deriva automaticamente dalla traslazione dell’immagine. Niente percentuali anatomiche inventate.
6. Confrontare cinque e più appoggi a parità di protocollo di verifica: successi, rifiuti, timeout, distribuzione punteggi e latenza mediana/p95. Separare dita/persona usate per taratura e verifica; pochi tentativi sul proprietario non stimano la FAR di popolazione.

## Lavoro utile per upstream

Il prossimo confronto tecnico sensato è una pipeline imaging `FpImageDevice` con immagini realmente decodificate e normalizzate, estrazione minutiae e matching NBIS del progetto. Potrebbe evitare un matcher dedicato e riusare enrollment/retry/stati; **non è provato** che immagini 144×56 del prototipo offrano minutiae sufficienti. Va verificato offline prima di sostituire il percorso funzionante. Non inventare DPI o aumentare l’immagine per far crescere artificialmente il numero di dettagli: stimare scala effettiva, orientamento, polarità, flag e densità dai dati disponibili.

Checklist per preparare una contribuzione:

- [ ] Dump sintetici e golden test del decoder, inclusi valori non saturati e ordine baseline/filtro; nessuna biometria reale nei test pubblici.
- [ ] Prova offline riproducibile NBIS vs matcher attuale, con parametri espliciti e limiti documentati.
- [ ] Separazione del protocollo da ABI TOD e credenziali locali; stato asincrono, cancellazione e USB transfer conformi all’albero upstream.
- [ ] Errori recuperabili di acquisizione esposti come retry; attesa rimozione e completamento corretti, senza conteggiare campioni non validi.
- [ ] Bootstrap su nuovo dispositivo, selezione ROM/modulo verificata e provenienza/licenza di payload e chiavi chiarite.
- [ ] Test di timeout, scollegamento, riapertura e cancellazione in ogni fase; log di protocollo minimizzati.
- [ ] Discussione anticipata coi manutentori su architettura e dati OEM, prima di considerare pronta una MR.

Il plugin TOD attuale non si carica in libfprint upstream. Il percorso imaging aiuta sull’architettura, ma non risolve da solo bootstrap, licenze, sicurezza del protocollo o qualità delle immagini. Vedi [confronto upstream e relinking](relinking-and-upstream.md).

## Metodo da riusare su un altro sensore

Non copiare valori 20/25 frame, RMS 15, geometria, cinque stage o soglia 0,50. Prima identificare il contratto del sensore: immagini o matching interno, dimensioni/encoder, eventi di contatto, calibrazione richiesta, stato persistente, formato template e operazioni di chiusura. Scrivere domanda sperimentale, criteri di arresto e successo; produrre un piano GUI o CLI limitato; leggere il run effettivo e aggiornare il checkpoint. Cambiare una variabile per confronto. L’utente interviene per i contatti fisici e le decisioni necessarie; l’agente prepara comandi, verifica prerequisiti e analizza i risultati.
