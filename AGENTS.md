# Istruzioni per agenti REA-FS

Leggi README.md, docs/repositories.md, docs/preferences.md e docs/agent-workflow.md prima dell’indagine. Leggi anche `.rea-fs.local/preferences.json` se presente; le istruzioni esplicite dell’utente prevalgono.

- REA-FS contiene workflow e documentazione; `workspace/rea` e `workspace/libfprint` sono clone indipendenti ignorati. Leggi le istruzioni proprie di ogni clone prima di modificarlo.
- Verifica repository, stato Git, remote e identità effettiva prima di commit o accessi remoti. Non portare l’identità freedesktop nel repository GitHub.
- Riprendi il checkpoint locale prima di ripetere un esperimento. Distingui fatti, ipotesi e risultati ancora da verificare.
- Non inventare osservazioni hardware, supporto, build riuscite o test eseguiti.
- Comunica all’utente solo azioni fisiche utili e tempestive. Definisci timeout e criterio di successo; non interpretare il silenzio come conferma.
- Applica docs/preferences.md: default autorizzazioni `ask`; in modalità `autonomous` procedi senza riconferme nelle categorie autorizzate. Registra il consenso già dato e rispettane l’ambito; chiedi solo per categorie mancanti. Un errore richiede indagine, non retry ciechi.
- Se mancano preferenze, chiedile una volta in chat e salvale localmente. Se l’utente rifiuta o delega la scelta, registra i default scelti dall’agente; non inventare consenso all’autonomia.
- Delega solo secondo le preferenze e i vincoli della sessione: includi nel confronto il costo di contesto, coordinamento e verifica. Modello economico e minor consumo di token non sono sinonimi.
- Non eseguire push, aprire merge request o pubblicare dati senza autorizzazione. Non includere chiavi, template biometrici, scansioni o binari riservati nei file tracciati.
- Usa REA solo se disponibile e utile; clonare il progetto non significa aver installato o connesso il suo MCP.
- Non implementare una GUI o un orchestratore come effetto collaterale dell’indagine sul sensore. Il workflow attuale è guidato dall’agente.
