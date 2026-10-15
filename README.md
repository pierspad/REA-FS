# REA-FS

Un percorso assistito da agenti per indagare sensori di impronte non supportati e sviluppare driver per libfprint. L’utente prepara gli accessi, avvia il proprio agente e interviene quando serve un’azione fisica sul sensore.

Questo repository contiene documentazione, checklist operative e un decoder offline sperimentale. Non contiene ancora un orchestratore, una GUI o un driver nuovo; il funzionamento su un sensore va dimostrato con test hardware.

## 1. Prepara gli accessi

- [ ] Installa Git e un client SSH; scegli un coding agent con accesso al terminale.
- [ ] Configura l’accesso GitHub. Per contribuire, usa il tuo fork di REA-FS.
- [ ] Crea un account su **gitlab.freedesktop.org** e registra la tua chiave pubblica SSH. È un servizio distinto da gitlab.com.
- [ ] Crea lì un fork personale di [libfprint](https://gitlab.freedesktop.org/libfprint/libfprint), se vuoi pubblicare modifiche. Per studiare il codice basta clonare l’upstream.

Integra questi blocchi nel tuo `~/.ssh/config`, adattando le chiavi e senza duplicare configurazioni esistenti:

```sshconfig
Host github.com
    HostName github.com
    User git
    IdentityFile ~/.ssh/LA_TUA_CHIAVE_GITHUB
    IdentitiesOnly yes

Host gitlab.freedesktop.org ssh.gitlab.freedesktop.org
    HostName ssh.gitlab.freedesktop.org
    User git
    IdentityFile ~/.ssh/id_ed25519_freedesktop
    IdentitiesOnly yes
```

Se usi già alias o regole Git `insteadOf`, mantienili e verifica l’host risultante. Al primo collegamento verifica la chiave del server; non disabilitare il controllo delle host key.

```sh
ssh -T git@github.com
ssh -T git@ssh.gitlab.freedesktop.org
```

Controlla il messaggio di benvenuto: alcuni servizi restituiscono un codice diverso da zero anche con autenticazione riuscita.

## 2. Prepara le repository

```sh
git clone git@github.com:pierspad/REA-FS.git
cd REA-FS
mkdir -p workspace
git clone git@github.com:morluto/rea.git workspace/rea
```

Per libfprint scegli **una sola** alternativa. Sostituisci `TUO_NAMESPACE` con il namespace reale del fork:

```sh
# Per contribuire dal tuo fork:
git clone git@ssh.gitlab.freedesktop.org:TUO_NAMESPACE/libfprint.git workspace/libfprint
git -C workspace/libfprint remote add upstream git@ssh.gitlab.freedesktop.org:libfprint/libfprint.git
```

```sh
# Per iniziare dall’upstream ufficiale:
git clone git@ssh.gitlab.freedesktop.org:libfprint/libfprint.git workspace/libfprint
```

`workspace/` è ignorata: i clone restano repository indipendenti, senza submodule e senza imporre il fork del manutentore. L’elenco delle sorgenti è in [docs/repositories.md](docs/repositories.md).

## 3. Verifica le identità

L’account autenticato dipende dalla chiave SSH; nome ed email dei commit dipendono da Git. Sono configurazioni distinte. Per il solo clone di libfprint puoi impostare:

```sh
git -C workspace/libfprint config --local core.sshCommand 'ssh -i ~/.ssh/id_ed25519_freedesktop -o IdentitiesOnly=yes'
git -C workspace/libfprint config --local user.name 'IL_TUO_NOME'
git -C workspace/libfprint config --local user.email 'LA_TUA_EMAIL_FREEDESKTOP'
```

Se hai profili per directory con `includeIf`, usa un profilo specifico per il clone invece di duplicare nome ed email locali. Non modificare alla cieca il profilo globale.

```sh
git -C workspace/libfprint config --show-origin --get user.name
git -C workspace/libfprint config --show-origin --get user.email
git -C workspace/libfprint config --show-origin --get core.sshCommand
git -C workspace/libfprint remote -v
git config --show-origin --get user.email
```

- [ ] L’accesso freedesktop riconosce il tuo account.
- [ ] Nome ed email in libfprint sono quelli desiderati.
- [ ] In REA-FS rimane il tuo profilo GitHub.
- [ ] `origin` punta al tuo fork, oppure sai che stai lavorando sul clone upstream. L’accesso SSH non dà automaticamente permessi di push sull’upstream.

## 4. Avvia il tuo agente

Leggi [le preferenze dell’indagine](docs/preferences.md): puoi scegliere autorizzazioni richieste oppure autonomia nell’ambito concordato, e decidere quando usare subagent/modelli economici. Se non hai già scelto in chat o compilato il file locale, l’agente ti presenta le domande una volta e salva le risposte in `.rea-fs.local/preferences.json`. Il [modello di partenza](docs/preferences.example.json) usa autorizzazioni richieste e nessuna delega. Le tue scelte locali non vengono pubblicate con la repository.

Apri REA-FS nel tuo agente e incolla:

> Leggi AGENTS.md, applica docs/preferences.md e segui docs/agent-workflow.md in ordine. Verifica prima le repository e le identità Git, poi identifica il sensore senza modificarlo. Riprendi una sessione esistente se presente. Indaga supporto esistente, fonti e protocolli; prepara modifiche a libfprint solo quando sostenute dalle evidenze. Procedi autonomamente nelle attività già autorizzate e chiamami quando serve un’azione sul sensore, indicando esattamente cosa fare. Salva stato, risultati e prossimo passo a ogni fase. Non dichiarare riuscito un test hardware non eseguito.

L’agente controlla gli strumenti disponibili. Per attivare REA, consulta il README del clone e rivedi la procedura prima di eseguirla: clonarlo non installa né configura il server MCP. REA è un’integrazione opzionale; l’indagine può usare altri strumenti disponibili.

## 5. Durante le prove

L’agente deve dare istruzioni brevi e contestuali:

- **ATTENDI** — analisi o preparazione in corso; non toccare il sensore.
- **APPOGGIA IL DITO** — il test è pronto: quale dito, per quanto tempo e quale segnale attendere.
- **RIMUOVI IL DITO** — rimuovilo e attendi il prossimo messaggio.
- **AZIONE RICHIESTA** — una sola operazione precisa, con il motivo.
- **BLOCCATO / COMPLETATO** — risultato verificato e prossimo passo.

Le istruzioni fisiche vanno inviate quando il test è davvero in ascolto. Il silenzio non conferma un’azione: serve un evento hardware o una risposta dell’utente. Ogni attesa deve avere una scadenza e una procedura per riprendere o interrompere.

Una GUI persistente potrà mostrare questi stessi stati, il test corrente e i comandi «eseguito» e «interrompi». È un’estensione futura; oggi l’interazione avviene tramite agente e terminale.

Le tracce possono contenere dati biometrici. Concorda con l’agente cosa raccogliere prima delle acquisizioni. Sessioni e dati locali non vanno pubblicati automaticamente.

## Analisi offline del flusso osservato

`tools/raw_frames.py` legge il formato RAW verificato sul sensore USB 06cb:00dc di questa indagine: righe di 152 byte, 112 righe per frame, coppie adiacenti → 144×56 campioni. Valida intestazioni e contatori; non acquisisce dati dal dispositivo, non applica calibrazione o sottrazione della baseline e non è un driver. Vedi [risultati e limiti](docs/research-06cb-00dc.md).

```sh
python tools/raw_frames.py --input sessions/LA_TUA_SESSIONE/capture.bin \
    --output sessions/LA_TUA_SESSIONE/frame.pgm --frame 0 --encoder-mode 2 --allow-partial-tail
python -m unittest discover -v
```

La modalità encoder va verificata nel programma di acquisizione. Per il programma OEM di questa sessione è `2`: espande gruppi di 16 campioni dopo la media delle righe ripetute. Il default `raw` conserva il piano dei campioni codificati, che non equivale a un’immagine dell’impronta. Anche la modalità `2` senza calibrazione è solo diagnostica: la CLI non stima né applica la baseline del sensore.

L’output deve essere un file nuovo: viene creato con permessi 0600. Input massimo 64 MiB. Ometti `--allow-partial-tail` per rifiutare anche una coda incompleta; l’opzione non corregge frame corrotti e non cerca automaticamente l’allineamento. Conserva eventuali immagini biometriche nelle sessioni locali ignorate.
