# Guida all'installazione e all'uso del driver libfprint-tod per Synaptics 06cb:00dc

Questo documento descrive come compilare, testare e installare il driver out-of-tree **libfprint-tod** per il sensore d'impronte Synaptics `06cb:00dc` (ROM 6.20 build 154, modulo 0x2449, Acer Aspire A514-54).

---

## 1. Architettura del Driver (`drivers/syna_tod/`)

A causa dei blob di inizializzazione proprietari e cifrati/firmati da Acer (`5.5.2.1124`) non ridistribuibili nell'albero principale di `libfprint`, il driver è implementato come modulo condiviso per **`libfprint-2-tod-1`** (`libsyna_00dc_tod.so`).

Il driver esporta l'entry point standard TOD `fpi_tod_shared_driver_get_type()` e include:
1. **`syna_mode2`**: Decodifica vettoriale matrice Kronecker M 16×16, normalizzazione baseline floating-point prima del clipping a 8 bit.
2. **`syna_matcher`**: Filtro passa-banda 2D e matcher rigido a correlazione incrociata normalizzata con compensazione rotazione (-20°..+20°). Discrimina l'indice genuino (score 0.64–0.94) da dita impostatrici (score 0.08–0.30) con soglia decisionale 0.50.
3. **`syna_tls`**: Client crittografico TLS 1.2 OpenSSL EVP/HMAC su USB bulk (handshake ClientHello/Flight 2, ECDH P-256 effimero, AES-256-CBC, HMAC-SHA256, alert close_notify).
4. **`syna_oem`**: Caricatore credenziali e blob Acer protetti (`host_key.pem`, `host_cert.bin`, `server_ecdh_pub.bin`, `capture_init.bin`, `capture_packet.bin`).
5. **`syna_device`**: Sottoclasse `FpDevice` con supporto completo delle azioni asincrone di apertura, chiusura, rilevamento contatto tramite RMS e segnalazione eventi `FpFingerStatusFlags` (`FP_FINGER_STATUS_NEEDED`, `FP_FINGER_STATUS_PRESENT`).

---

## 2. Prerequisiti di Sistema

Su distribuzioni Arch / CachyOS / Fedora / Ubuntu:
```bash
# Dipendenze di compilazione
sudo pacman -S meson ninja gcc pkgconf libfprint-tod openssl libgusb libusb glib2
# oppure su Ubuntu:
# sudo apt install meson ninja-build libfprint-2-tod-dev libssl-dev libgusb-dev libusb-1.0-0-dev libglib2.0-dev
```

---

## 3. Compilazione ed Esecuzione dei Test Unitari

Dalla radice del repository:
```bash
cd drivers/syna_tod
meson setup build
ninja -C build
meson test -C build -v
```

Tutti i 4 test unitari vengono eseguiti automaticamente:
- `mode2_tests`: Verifica decodifica Kronecker, offset di baseline e integrità frame.
- `matcher_tests`: Verifica correlazione identica (1.00), shift noti e rigetto di impronte incorrelate (0.01 vs soglia 0.50).
- `tls_tests`: Verifica derivazione PRF RFC 5246 e roundtrip crittografico AES-256-CBC + HMAC-SHA256 con rilevamento manomissioni.
- `oem_tests`: Verifica caricamento chiavi e blob protetti.

---

## 4. Test Hardware in Spazio Utente (senza permessi root)

Il driver può essere testato direttamente contro il sensore fisico senza installare nulla a livello di sistema grazie alla variabile `FP_TOD_DRIVERS_DIR`:

### 4.1 Verifica Enumerazione Sensore
```bash
FP_TOD_DRIVERS_DIR=$PWD/drivers/syna_tod/build ./drivers/syna_tod/build/test_fprint_context
```
Output atteso:
```text
Found 1 fingerprint device(s):
  [0] Driver: syna_00dc_tod
      Device ID: 0
      Name: Synaptics 06cb:00dc (Acer Aspire A514-54)
      Scan type: press
      Enroll stages: 1
[PASS] Successfully discovered and bound syna_00dc_tod driver!
```

### 4.2 Test Apertura e Chiusura Crittografica
```bash
FP_TOD_DRIVERS_DIR=$PWD/drivers/syna_tod/build ./drivers/syna_tod/build/test_fprint_open
```
Output atteso:
```text
Testing fp_device_open_sync on Synaptics 06cb:00dc (Acer Aspire A514-54) (syna_00dc_tod)...
[PASS] Device opened successfully and TLS handshake + OEM init completed!
Testing fp_device_close_sync...
[PASS] Device closed cleanly!
```

### 4.3 Test Interattivo di Registrazione e Riconoscimento
```bash
FP_TOD_DRIVERS_DIR=$PWD/drivers/syna_tod/build ./drivers/syna_tod/build/test_fprint_enroll_verify
```
Questo comando:
1. Apre il sensore ed esegue l'handshake TLS e l'init OEM.
2. Ti chiede di posizionare il dito sul sensore per la registrazione (Enrollment).
3. Rileva il contatto e calcola il template biometrico tramite FFT 2D.
4. Ti chiede di riappoggiare il dito per la verifica (Verification).
5. Mostra il punteggio di correlazione e l'esito di matching.

---

## 5. Installazione di Sistema per `fprintd` / PAM Login

Per integrare il driver nel desktop Linux (GDM, SDDM, blocco schermo, `sudo` via impronta):

### 5.1 Esecuzione dello Script di Installazione
```bash
sudo ./scripts/install-tod-driver.sh
```
Lo script esegue automaticamente:
1. Compilazione del modulo `libsyna_00dc_tod.so`.
2. Copia del modulo in `/usr/lib/libfprint-2/tod-1/libsyna_00dc_tod.so` (permessi 0755).
3. Estrazione sicura delle chiavi di pairing locali e dei blob Acer in `/var/lib/fprint/syna_00dc/` (permessi 0600).
4. Installazione della regola udev in `/etc/udev/rules.d/60-syna-00dc-tod.rules` e ricaricamento di `udevadm`.
5. Riavvio del servizio `fprintd.service`.

### 5.2 Uso con i Comandi Standard `fprintd`
Una volta installato, il sensore è gestito nativamente dal demone `fprintd`:

```bash
# Registrazione dell'impronta
fprintd-enroll

# Verifica dell'impronta registrata
fprintd-verify

# Lista delle dita registrate
fprintd-list $USER
```

---

## 6. Sicurezza e Riservatezza delle Credenziali

- **Chiavi host e template**: Risiedono esclusivamente nella directory protetta locale `/var/lib/fprint/syna_00dc/` con permessi `0600` (root-only).
- **Controllo di versione Git**: Tutte le chiavi private (`*.pem`), le catture grezze (`*.bin`), i dump di sessione e i binari di compilazione sono esclusi dal tracciamento Git tramite `.gitignore`. Nessun dato biometrico o crittografico privato viene pubblicato.
