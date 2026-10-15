# Driver sperimentale Synaptics 06cb:00dc

Modulo C singolo `libsyna_00dc_tod.so` per **libfprint TOD**. Non funziona caricandolo nella libfprint upstream ordinaria. Non è una DLL Windows né un plugin per synaTudor. Ambito provato: Acer Aspire A514-54, USB 06cb:00dc revisione 0154, ROM 6.20 build 154, modulo 0x2449. L’attuale binding controlla VID/PID: il controllo delle varianti interne deve ancora essere implementato prima di estendere il supporto.

## Prerequisiti e build

L’agente verifica i pacchetti disponibili sulla distribuzione. Servono Meson, Ninja, compilatore C, pkg-config, header pubblici libfprint e header interni TOD, OpenSSL >=3, GUsb, libusb, GLib e GIO. Il pacchetto upstream che offre soltanto l’API pubblica non basta. Non usare automaticamente comandi di installazione di un’altra distribuzione.

```sh
meson setup drivers/syna_tod/build drivers/syna_tod
ninja -C drivers/syna_tod/build
meson test -C drivers/syna_tod/build --print-errorlogs
```

Se la build esiste, iniziare da Ninja. Sei suite C: decoder, matcher, record crittografici/PRF, caricatore OEM con fixture sintetiche template e calcolo dei timeout. Non accedono al sensore e non richiedono chiavi reali. Un test PRF deterministico e un roundtrip non sono validazione indipendente del protocollo; la suite non costituisce un audit crittografico.

## Materiale privato necessario

Il modulo richiede cinque file: `host_key.pem`, `host_cert.bin` (184 byte), `server_ecdh_pub.bin` (65), `capture_init.bin` (4676), `capture_packet.bin` (1917). Devono appartenere al dispositivo e alla configurazione pertinenti. Non copiarli da un’altra persona. Il caricatore controlla dimensioni e formato di base, non prova da solo l’identità o l’autenticità di tutto il materiale.

Per una sessione già investigata con i layout documentati:

```sh
python scripts/setup-tod-credentials.py \
  --session-dir sessions/LA_SESSIONE_ESISTENTE \
  --out-dir .rea-fs.local/credentials-export
```

Questo script **esporta materiale già presente**; non scarica driver, non estrae automaticamente un pacchetto Acer nuovo e non effettua pairing. La destinazione deve essere nuova e il suo genitore deve esistere. Valida tutti gli input prima di scrivere; crea directory 0700 e file 0600, rifiutando sovrascritture. La validazione della chiave PEM effettiva è distinta dal controllo delle dimensioni.

Il caricatore legge una directory esplicita, oppure `SYNA_TOD_DATA_DIR`, oppure `/var/lib/fprint/syna_00dc`. Non contiene più percorsi personali o fallback automatici alle sessioni. Un clone pulito non può ancora preparare autonomamente il materiale di un sensore nuovo: questo è un requisito aperto per la distribuzione.

## Prova locale senza installazione

Con fprintd libero e nessun altro programma che usi il sensore:

```sh
SYNA_TOD_DATA_DIR="$PWD/.rea-fs.local/credentials-export" \
FP_TOD_DRIVERS_DIR="$PWD/drivers/syna_tod/build" \
  ./drivers/syna_tod/build/test_fprint_enroll_verify
```

Lasciare il sensore libero durante l’apertura: il prototipo misura lì la baseline. Seguire gli eventi del programma/agente. Sono richiesti **cinque appoggi distinti**, con rilascio fra uno e l’altro. Poi viene verificato un ulteriore appoggio. Ogni cattura ha una scadenza reale di 60 s, oltre ai timeout USB; la calibrazione ha una scadenza di 15 s. Le attese di I/O e cleanup possono aggiungere tempo. Il test serializza e deserializza il template in memoria: non scrive autonomamente un nuovo file di impronte.

Il matcher confronta il campione con i cinque template e prende il massimo; soglia sperimentale 0,50, rotazioni −30…+30°, sovrapposizione minima 65%. Più template possono aumentare anche le false accettazioni: la soglia deve essere valutata sul sistema completo. I template vecchi di un solo appoggio sono leggibili, ma la nuova pipeline richiede una nuova registrazione per una valutazione significativa.

## Installazione controllata

Preparare e verificare prima la build e le credenziali. L’installer non esegue build come root.

```sh
# Piano leggibile: nessuna modifica di sistema.
./scripts/install-tod-driver.sh --credentials "$PWD/.rea-fs.local/credentials-export"

# Solo dopo autorizzazione per installazione di sistema:
sudo ./scripts/install-tod-driver.sh \
  --credentials "$PWD/.rea-fs.local/credentials-export" --apply
```

La directory dei plugin è ricavata da pkg-config. Il comando verifica la chiave, crea un backup privato dei file preesistenti sotto `/var/lib/fprint/syna_00dc-backup.XXXXXX`, installa modulo, credenziali e regola udev, ricarica le regole e riavvia fprintd se attivo. Non modifica PAM. Non è una transazione con rollback automatico; conservare il percorso stampato e controllare ogni errore prima di riprendere.

Per ripristinare, un amministratore ferma fprintd e rimette nei rispettivi percorsi il modulo, le credenziali e la regola salvati nel backup, poi ricarica udev. Se un file non esisteva prima, la sua rimozione va decisa in base al piano del backup. Non cancellare template o directory fprintd estranee al modulo. Non disabilitare l’accesso tramite password.

```sh
fprintd-enroll -f right-index-finger
fprintd-verify -f right-index-finger
fprintd-list "$USER"
```

Il dito da appoggiare è deciso dall’esperimento: `-f right-index-finger` seleziona il template da verificare, non conferma quale dito sia stato fisicamente usato. La prova con un dito diverso deve fallire.

## Stato hardware verificato il 15 ottobre 2026

- Build locale: cinque appoggi, template serializzato/riletto e verifica genuina 0,873, overlap 83,3%; confronto circa 3 s, chiusura segnalata riuscita.
- Driver installato: hash uguale alla build; fprintd espone `num-enroll-stages=5`, enrollment completato, verifica genuina 0,838 accettata e indice sinistro 0,320 rifiutato.
- KDE: primo tentativo fallito mentre il driver attendeva il sensore libero (`clear=0`, nessun confronto); successivo riuscito riferito dall’utente in circa 4 s, corroborato da score 0,874 e overlap 75,1% nel log.

Non è ancora dimostrata affidabilità quotidiana. Il primo fallimento resta da isolare; il feedback di contatto non è risultato visibile all’utente nella schermata KDE. Verificare inoltre riavvio, sospensione, cancellazione e più tentativi indipendenti. Queste prove non stimano FAR/FRR di popolazione e non dimostrano resistenza a impronte contraffatte.
