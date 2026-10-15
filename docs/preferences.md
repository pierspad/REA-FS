# Preferenze dell’indagine

Prima di iniziare, l’agente legge `.rea-fs.local/preferences.json`, se esiste, e le preferenze già espresse in chat. Il file è locale e ignorato da Git: le scelte del manutentore non diventano quelle di chi clona il progetto. Il modello di partenza è [preferences.example.json](preferences.example.json).

Queste sono istruzioni per l’agente, non un sistema che applica permessi: un file JSON non concede accesso al computer, non modifica sudo e non installa modelli o strumenti. Le istruzioni esplicite dell’utente prevalgono sul file; aggiornarlo quando cambiano.

## Prima configurazione in chat

Se manca una scelta necessaria, l’agente presenta insieme queste domande, una sola volta:

1. **Autorizzazioni:** chiedere prima delle operazioni che modificano dispositivo/sistema (predefinito), oppure autorizzare l’indagine autonoma sul sensore e nel progetto?
2. **Collaborazione:** usare solo l’agente principale, oppure permettere subagent per attività delimitate quando il risparmio complessivo è plausibile?
3. **Obiettivo di risparmio:** meno token totali, minor costo economico oppure meno tempo? Quali modelli sono ammessi, se hai preferenze?

L’agente traduce le risposte nel file locale, senza obbligare l’utente a editarlo. Registra anche ambito, esclusioni e origine della scelta. Non ripetere domande già risolte in chat. Una risposta “decidi tu” o il rifiuto di configurare consentono all’agente di scegliere i default e registrarli come scelta dell’agente: non equivalgono al consenso per l’autonomia completa. Il silenzio non è una risposta; mentre attende, l’agente può svolgere il lavoro già autorizzato.

## Autorizzazioni

| Modalità | Comportamento |
| --- | --- |
| `ask` (default) | Procedere con letture, analisi e preparazione già richieste. Chiedere solo le autorizzazioni mancanti prima di modifiche al sensore/sistema, raccolte biometriche e pubblicazione. |
| `autonomous` | Procedere senza nuove conferme nelle categorie e nell’ambito autorizzati, preparando prima ogni esperimento, verificandone i prerequisiti e salvando gli esiti. |

L’ambito deve nominare il progetto e il sensore. Le categorie in `allowed_actions` sono il consenso operativo; le esclusioni restano in `excluded_actions`. Una categoria autorizzata non va sottoposta a conferma a ogni prova. L’autonomia non significa tentativi alla cieca: errori, timeout e cambiamenti imprevisti richiedono analisi e un nuovo esperimento motivato, non retry automatici.

Categorie utili: `diagnostics`, `pairing`, `oem_initialization`, `device_reset`, `factory_reset`, `persistent_firmware`, `system_packages`, `system_driver_installation`, `biometric_capture`, `publish`. Definire separatamente modifiche a PAM/login e modifiche globali a SSH/sudo. Non aggiungere categorie solo perché è stato selezionato un modello economico.

Quando l’utente sceglie l’autonomia senza precisare l’ambito, descrivere e salvare quello applicato: progetto corrente e sensore identificato; analisi, build locali, diagnostica, pairing e inizializzazione OEM pertinente. Pubblicazione, modifica dell’accesso/login, cancellazione di dati estranei, factory reset e firmware persistente restano fuori finché non sono inclusi esplicitamente. Una preferenza non può superare i vincoli degli strumenti o le istruzioni di priorità superiore.

Per acquisire impronte, definire prima conservazione e destinazione: dati privati di sessione, minimizzati, senza pubblicazione o invio a servizi esterni. La richiesta fisica “appoggia il dito” resta necessaria quando il test è pronto; non è una nuova domanda di autorizzazione tecnica.

Se servono privilegi, preferire un accesso circoscritto al dispositivo o un comando delimitato. Non configurare sudo senza password generale come effetto collaterale. Registrare se il permesso è temporaneo e ricontrollarlo dopo riconnessione o riavvio.

## Modelli e subagent

`delegation.mode` può essere `off` o `when_economical`. Il default è `off`; chi preferisce il risparmio può scegliere `when_economical` e specificare `objective` (`total_tokens`, `monetary_cost` o `latency`) e i modelli ammessi.

Un modello meno caro non garantisce meno token o meno tempo. Valutare contesto da trasferire, duplicazione dell’analisi, coordinamento, verifica dei risultati e possibili correzioni. Per confronti economici usare prezzi effettivamente disponibili e aggiornati; non dedurli dal nome del modello. Se non si conoscono le tariffe o l’uso effettivo, dichiararlo e non promettere risparmi misurati.

Un subagent ha senso per una ricerca indipendente con input breve, una funzione isolata o una verifica delimitata. Evitarlo per piccole modifiche documentali, indagini con molto stato condiviso o per ripetere una decompilazione già disponibile. Il principale conserva responsabilità per ipotesi, esperimenti hardware e verifica.

Prima di delegare:

- Verificare che il modello richiesto sia disponibile e che la delega sia consentita.
- Definire una domanda, i soli file necessari, un risultato breve e il criterio di verifica.
- Stimare il beneficio complessivo rispetto al lavoro diretto. Se `strict_condition` è true e non c’è evidenza sufficiente del risparmio richiesto, non delegare.
- Registrare motivo e risultato nel checkpoint; distinguere stime da consumi osservati. Non inventare conteggi di token o prezzi.

Non inviare chiavi private, immagini o template biometrici a un subagent per comodità. Usare una descrizione minimizzata del protocollo quando basta. Non avviare chat separate o pubblicare risultati come sostituto della delega interna.

## Ripresa

A ogni ripresa: leggere preferenze e checkpoint, applicare le autorizzazioni ancora pertinenti, aggiornare solo ciò che è cambiato. Un file mancante o malformato non significa autonomia autorizzata. Se un tool o un vincolo superiore impedisce un’azione, spiegare il blocco concreto; cambiare preferenze non lo aggira.
