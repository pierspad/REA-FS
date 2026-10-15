# Progetto: supporto Linux per Synaptics 06cb:00dc

Stato: proposta. Nessun driver ancora implementato. Base: `docs/research-06cb-00dc.md`.

## Vincoli verificati

- Inizializzazioni di pairing e cattura cifrate/firmate dal vendor → **non distribuibili**, vanno estratte a runtime dal pacchetto Acer ufficiale (`5.5.2.1124`, SHA256 in checkpoint) scaricato o fornito dall'utente.
- Nessuna flash utente sul sensore (0 partizioni) → template e chiave host vanno salvati sul PC, permessi 0600.
- Matching sull'host; area 144×56 (~11×4 mm ipotizzati): NBIS/Bozorth non è sufficiente da solo.

## Architettura scelta: driver fuori albero

Opzioni valutate:

| Opzione | Pro | Contro |
|---|---|---|
| Modulo **libfprint-TOD** (C) | stessa API di libfprint, funziona con fprintd standard, eventuale upstream futuro | richiede libfprint-tod di distribuzione; matcher NBIS inadeguato senza estensioni |
| Estensione **python-validity** + open-fprintd | protocollo della stessa famiglia (TLS, `4f`, `06`, estrazione blob a runtime già presente) | progetto poco mantenuto, oggi pensato per match-on-sensor; richiede sostituire fprintd |
| Servizio autonomo | libertà totale | reinventa fprintd/PAM |

Proposta: **modulo TOD in C** con componenti separati e testabili:

1. `oem-extract`: verifica SHA256 del pacchetto Acer, estrae dalla DLL i blocchi per ROM/tipo modulo (logica già validata in `oem-pe-data.py`).
2. `tls`: handshake ECDH/ECDSA, record AES+HMAC (OpenSSL), test con server simulato.
3. `pairing`: generazione chiave host, comando `4f`, salvataggio credenziali; ri-pairing solo su richiesta esplicita.
4. `capture`: pulizia `1a`, init `06`, cattura `02`, stop `04`, close; chiusura TLS su ogni percorso di errore.
5. `image`: framing 152×112, media coppie, mode 2 con baseline prima del clipping, rilevamento contatto.
6. `match`: enrollment multi-appoggio; prima misura su NBIS ricampionato, poi matcher per area piccola se necessario.

Le componenti 1–5 sono già dimostrate in Python; vanno portate e coperte da test. La 6 è il problema aperto.

## Uso del repository per altri sensori

REA-FS resta il workflow generico (`docs/agent-workflow.md`); il codice specifico vive in una directory/progetto per sensore. Per chi ha lo stesso 06cb:00dc: il driver dovrà verificare VID/PID/revisione/ROM/tipo modulo e rifiutare combinazioni non provate.

## Prossimi passi

1. Verificare persistenza dell'abbinamento dopo riavvio.
2. Raccogliere un set di appoggi (stesso dito ×N, altro dito) per scegliere il matcher.
3. Scheletro modulo TOD + port di `image` con test.
