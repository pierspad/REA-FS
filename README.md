# REA-FS

Reverse engineering assistito da agenti per studiare lettori di impronte non supportati e sviluppare driver Linux. Il percorso parte dall’hardware dell’utente, conserva evidenze e checkpoint e richiede interventi fisici solo quando un esperimento è pronto.

## Parti da questo messaggio

Clona questa repository, aprila con un agente che possa leggere file ed eseguire comandi e incolla:

> Leggi AGENTS.md, README.md, docs/repositories.md, docs/preferences.md e docs/agent-workflow.md. Leggi le preferenze locali se presenti e riprendi il checkpoint pertinente. Il mio obiettivo è studiare il lettore di impronte di questo computer e, se tecnicamente possibile, renderlo utilizzabile su Linux. Identifica prima hardware, sistema e supporto esistente; non assumere che il mio sensore sia quello dell’esempio Synaptics. Chiedimi una sola volta le preferenze mancanti, salva le risposte localmente e procedi senza riconferme nelle categorie già autorizzate. Scegli la modalità interattiva o una finestra temporale annunciata secondo le capacità reali del tuo client. Guidami con un’azione fisica alla volta, solo quando il test è in ascolto, indicando scadenza e segnale di successo. Non interpretare il silenzio come conferma. Confronta fonti, verifica ipotesi e prepara strumenti ripetibili invece di accumulare comandi ad hoc. Salva fatti, ipotesi, errori, revisioni e prossimo passo. Proteggi chiavi e dati biometrici; niente pubblicazione automatica. Non promettere un driver né dichiarare riuscite prove non eseguite. Se il lavoro è bloccato, spiega l’evidenza e il requisito concreto per continuare.

```sh
git clone https://github.com/pierspad/REA-FS.git
cd REA-FS
```

Per leggere il progetto non servono account freedesktop, chiavi SSH o privilegi amministrativi. Gli accessi per contribuire si preparano quando servono: [repository, identità e destinazioni](docs/repositories.md).

## Cosa è disponibile

| Parte | Stato e limiti |
| --- | --- |
| Workflow generico per altri sensori | Documentazione e strumenti locali; l’agente adatta gli esperimenti al protocollo osservato |
| Decoder e confronto offline | `tools/`: formato osservato su **06cb:00dc**, non un decoder universale |
| Driver C sperimentale | `drivers/syna_tod/`: modulo per il **fork libfprint TOD**, non driver integrato nell’upstream |
| Prova hardware del 15 ottobre 2026 | Cinque appoggi, rilettura del template serializzato e una verifica genuina riuscita; nessuna stima affidabile di FAR/FRR |
| Altri modelli o revisioni | Supporto da investigare; lo stesso marchio o VID/PID non garantisce compatibilità |

Il prototipo Synaptics usa credenziali e dati OEM privati **assenti da un clone pulito**. Non è ancora un’installazione automatica per nuovi proprietari dello stesso sensore. Il successo riferito dall’utente nello sblocco KDE riguarda la versione precedentemente installata; la nuova build locale deve essere valutata separatamente.

## Orientati nel progetto

- [Procedura dell’agente](docs/agent-workflow.md): discovery, ricerca, esperimenti e checkpoint.
- [Preferenze e autorizzazioni](docs/preferences.md): autonomia concordata, dati privati e delega.
- [Guida completa al processo](docs/how-it-works.md): hardware, protocollo, immagini, matching, desktop e aspetti legali.
- [Audit e preparazione dei contributi](docs/audit-2026-10-15.md): cosa funziona, cosa manca e destinazione delle patch.
- [Driver Synaptics: uso e limiti](docs/driver-installation.md).
- [Ricerca Synaptics](docs/research-06cb-00dc.md), [progetto del driver](docs/design-06cb-00dc-driver.md) e [handover](docs/agent-handover.md): contesto storico, da leggere rispettando le correzioni successive.
- [Modello per una nuova indagine](docs/templates/investigation.md).

## Strumenti ripetibili

Python 3, libreria standard per gli script di workflow. Il decoder/confronto può richiedere NumPy; la build C richiede il pacchetto di sviluppo del **fork TOD** e le dipendenze elencate da Meson. L’agente verifica la distribuzione prima di proporre installazioni.

```sh
# Crea una sessione NUOVA, privata; sessions/ esiste? Creala solo se manca.
mkdir -p sessions
python scripts/rea-fs.py init-session --sensor 'BUS:ID-OSSERVATO' \
  --directory sessions/la-mia-indagine

# Legge metadati locali; non apre il sensore, non contatta remote.
python scripts/rea-fs.py preflight --output sessions/la-mia-indagine/preflight.json

# Test offline Python, build locale e test C; nessuna prova hardware.
python scripts/rea-fs.py check

# Riassume tentativi etichettati manualmente, senza immagini o chiavi.
python scripts/rea-fs.py summarize sessions/la-mia-indagine/attempts.csv --threshold 0.5
```

`preflight` contiene metadati privati, incluse identità Git: rivedilo prima di condividerlo. I file nuovi sono creati con permessi 0600, le directory di sessione con 0700; i comandi di creazione rifiutano le destinazioni già esistenti.

Il CSV dei tentativi ha intestazione `kind,score,seconds,status`, con `kind` = `genuine`/`impostor` e `status` = `ok`/`timeout`/`error`. Il riepilogo separa gli errori di acquisizione dai confronti conclusi. Le percentuali descrivono quei tentativi, non una certificazione del matcher.

Per il decoder specifico:

```sh
python tools/raw_frames.py --input sessions/SESSIONE/capture.bin \
  --output sessions/SESSIONE/frame.pgm --frame 0 --encoder-mode 2 --allow-partial-tail
```

Il formato osservato ha righe da 152 byte ×112 = **17.024 byte per frame**; dopo media delle coppie e decodifica produce 144×56 campioni. La CLI offline non applica la calibrazione OEM. L’output deve essere nuovo, input massimo 64 MiB; `--allow-partial-tail` tollera solo una coda incompleta.

## Interazione e riservatezza

Con un client interattivo l’agente avvia un processo e osserva gli eventi prima di chiedere «APPOGGIA IL DITO». Con un client che non può interagire durante il comando prepara una finestra temporale con istruzioni e segnali locali già visibili all’utente; analizza poi l’intera registrazione, senza inventare conferme. [Dettagli operativi](docs/agent-workflow.md#modalità-del-client-interattiva-o-a-finestra).

`workspace/`, `sessions/`, `.rea-fs.local/` e le credenziali locali sono ignorati. `.gitignore` non è una garanzia contro la pubblicazione accidentale: controllare sempre file tracciati e diff. Niente chiavi, template, scansioni o binari OEM nei contributi. Nessun orchestratore o GUI è necessario per seguire questo workflow. REA è un’integrazione opzionale: clonarlo non configura il suo MCP.
