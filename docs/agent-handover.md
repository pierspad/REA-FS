# Registro dei passaggi fra agenti

Serve a modelli successivi per verificare cosa è stato fatto da ciascun agente.

| Dal commit | Tag | Agente / modello | Data |
|---|---|---|---|
| `24fd9fd`…`1eabf01` | — | agente precedente (vedi checkpoint privato) | 2026-10-15 |
| `1eabf01` | `handover-gemini-base` | Antigravity; marcatore registrato come “Gemini”. Il lavoro successivo del 15/10 18:06 è stato eseguito dal modello selezionato nell’interfaccia: Claude Opus 5.5 | 2026-10-15 |

Per rivedere il lavoro dopo l’handover: `git diff handover-gemini-base..HEAD` e la sezione “Sovrapposizione fra appoggi indipendenti” in `research-06cb-00dc.md`. Le evidenze private (script `check-placement-overlap.py`, `placement-overlap-results.json`) restano nella directory di sessione ignorata.
