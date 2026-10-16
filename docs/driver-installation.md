# Installazione del driver Synaptics 06cb:00dc

Il driver è mantenuto in [synaptics-00dc](https://github.com/pierspad/synaptics-00dc).
REA-FS ne fissa una revisione nel submodule `drivers/syna_tod`.

```sh
git submodule update --init --recursive
bash drivers/syna_tod/install.sh
bash drivers/syna_tod/install.sh --apply
```

Il primo comando installer mostra soltanto il piano. Il secondo compila, verifica
ed installa il modulo TOD con backup. Le credenziali esistenti sono conservate;
`--credentials PRIVATE_DIR` importa soltanto quelle del proprio dispositivo.
Nessuna modifica PAM e nessuna nuova regola Polkit permissiva.

Per un dispositivo nuovo, leggere `drivers/syna_tod/docs/installation.md` e lo
stato sperimentale del pairing. L'agente prepara l'esperimento nel pannello;
`setup.sh` è una procedura interattiva destinata all'utente, non un comando da
eseguire automaticamente durante un'indagine.

Il wrapper storico `scripts/install-tod-driver.sh` inoltra al nuovo installer.
Il modulo richiede libfprint TOD: non è caricabile dalla sola libfprint upstream.
Le vecchie istruzioni di esportazione rimangono utili soltanto per sessioni già
investigate; non distribuire mai chiavi o baseline personali come prerequisiti.

## Aggiornamento dalla vecchia installazione

Il vecchio installer distribuiva `/etc/polkit-1/rules.d/50-syna-00dc-fprintd.rules`,
concedendo anche `setusername`. Il nuovo installer non la installa e non elimina
configurazioni amministrative già presenti. Occorre rivederla e rimuoverla con
backup per ripristinare i permessi della distribuzione. Nessuna modifica al sistema
è implicita nell'aggiornamento dei sorgenti.

## Evidenze

Gli esiti hardware precedenti restano in `docs/audit-2026-10-15.md` e nel checkpoint
privato. Le nuove build/test offline non dimostrano enrollment, KDE, pairing iniziale
o affidabilità biometrica. I test hardware vanno eseguiti dal pannello con timeout,
criterio di successo e lettura dei log, come descritto in `docs/experiment-panel.md`.
