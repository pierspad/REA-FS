# Repository del progetto

| Repository | Clone SSH | Posizione locale | Ruolo |
| --- | --- | --- | --- |
| REA-FS | `git@github.com:pierspad/REA-FS.git` | radice | Workflow e documentazione |
| REA | `git@github.com:morluto/rea.git` | `workspace/rea` | Strumenti di reverse engineering, integrazione opzionale |
| libfprint upstream | `git@ssh.gitlab.freedesktop.org:libfprint/libfprint.git` | `workspace/libfprint` | Sorgente ufficiale e sviluppo driver |
| synaTudor upstream | `git@github.com:Popax21/synaTudor.git` | `workspace/synaTudor` | Riferimento del caricatore dei driver Windows Synaptics |
| Fork synaTudor del manutentore | `git@github.com:pierspad/synaTudor.git` | `workspace/synaTudor-fork` | Sviluppo sperimentale; gli altri utenti scelgono il proprio fork |
| Fork libfprint dell’utente | URL personale su freedesktop | stesso clone libfprint | Destinazione delle proprie contribuzioni |

Non sono submodule. Ogni utente sceglie fork e branch; nessun clone annidato viene tracciato da REA-FS. Per registrare un esperimento, salva nella sessione URL dei remote e commit esatti (`git rev-parse HEAD`), senza credenziali.

Il clone iniziale del manutentore usa l’upstream libfprint come `origin`: non è un fork personale. Quando avrai creato il fork, dal clone potrai fare:

```sh
git remote rename origin upstream
git remote add origin git@ssh.gitlab.freedesktop.org:TUO_NAMESPACE/libfprint.git
```

Prima verifica i remote esistenti; se `upstream` esiste già, confronta gli URL e adatta i comandi. La creazione del fork sul servizio non è implicita nel clone.

## Dove proporre una modifica

Una correzione al caricatore Windows, al supporto WDF o al modulo TOD di synaTudor va proposta a synaTudor. Un driver nativo che implementa il protocollo seguendo le convenzioni upstream va proposto a libfprint. Le due contribuzioni hanno requisiti diversi: il funzionamento con una DLL proprietaria non dimostra che esista un driver nativo pronto per libfprint. Scegliere la destinazione dopo aver validato la modifica e discusso i risultati con l’utente.

## Riferimenti di ricerca opzionali

- `git@github.com:uunicorn/python-validity.git` → `workspace/validity-reference`: protocollo USB Validity, struttura RomInfo e flash metadata. Non eseguirne automaticamente setup, calibrazione o reset.
- `git@github.com:sandbranch/x1-tablet-gen3-fingerprint.git` → `workspace/synaspi-reference`: riferimento SPI per product interno 0x33. La corrispondenza del product non implica uguale trasporto, chip o pairing. Non è una dipendenza necessaria per gli altri utenti.
