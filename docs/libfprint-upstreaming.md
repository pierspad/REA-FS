# Submitting `syna_wbdi` (`06cb:00dc`) Upstream to `libfprint`

This guide explains **what** can be submitted to upstream [`libfprint`](https://gitlab.freedesktop.org/libfprint/libfprint), **how** to prepare the Merge Request, and **what upstream maintainers require**.

---

## 1. Why `06cb:00dc` Needs a Dedicated Driver (`synaptics_wbdi`)

In upstream `libfprint`, `libfprint/drivers/synaptics/` only supports **Match-in-Sensor (BMKT)** devices (`06cb:00df`, `06cb:00bd`, etc.), where image capture and minutiae matching happen inside the sensor's processor.

By contrast, **Synaptics `06cb:00dc`** (ROM `6.20` build `154`, sensor module `0x0190:0x00a2` / `86C TM-P3568-001`, product ID `'3'` / `0x33`) is a **Host-Side Raw-Image WBDI** sensor:
1. It establishes a custom **TLS 1.2 (`ECDHE-ECDSA-AES256-CBC-SHA256`)** session framed with `0x44 0x00 0x00 0x00` headers over USB bulk endpoints `0x01`/`0x81`.
2. It streams raw $112 \times 152$-byte **Mode-2 capacitive frames** (`17,024` bytes) over endpoint `0x82`.
3. Reconstruction ($144 \times 56$ pixels @ 508 DPI) and rotational cross-correlation matching (`-30°..+30°`) run on the host CPU.

---

## 2. What Has Already Been Implemented in `drivers/syna_tod/src/`

All four technical blockers for upstream `libfprint` inclusion are now implemented and unit-tested in `drivers/syna_tod/`:

| Upstream Requirement | Implementation in `drivers/syna_tod/src/` | Status |
|---|---|---|
| **1. Hardware Identity Verification (`0x01` + `0x75`)** | [`syna_proto.h`](../drivers/syna_tod/src/syna_proto.h): `syna_parse_version_response()` (asserts ROM `6.20` build `154`, product `0x33`) and `syna_parse_sensor_id_response()` (asserts module `0x0190:0x00a2`). | Ready & tested (`test_status.c`) |
| **2. Self-Contained Capture Tables (`0x06` + `0x02`)** | [`syna_capture_tables.h`](../drivers/syna_tod/src/syna_capture_tables.h): compiled-in `SYNA_BUILTIN_CAPTURE_INIT` (`4,676 B`) and `SYNA_BUILTIN_CAPTURE_PACKET` (`1,917 B`). No external `.bin` files required. | Ready & tested (`test_oem.c`) |
| **3. First-Boot Host Pairing (`0x4f` + `0x50`)** | [`syna_tls.c`](../drivers/syna_tod/src/syna_tls.c): `syna_pairing_generate_request()` derives the deterministic ROM `6.20` `HS_KEY_PAIR_GEN` EC P-256 vendor signer via `syna_tls_prf`, signs a fresh host EC P-256 keypair, and `syna_pairing_extract_credentials()` extracts `host_cert.bin` (`184 B`) + `server_ecdh_pub.bin` (`65 B`). | Ready & tested (`test_tls.c`) |
| **4. Non-Blocking `FpiSsm` + `FpiUsbTransfer` Engine** | [`syna_ssm.c`](../drivers/syna_tod/src/syna_ssm.c): `syna_ssm_open_new()` (16 states: reset, claim, `0x01`, `0x75`, optional `0x4f`/`0x50` pairing, TLS handshake, `0x1a`, `0x06`) and `syna_ssm_capture_new()` (11 states: `0x02` start, chunked `17,024 B` bulk reads on `EP 0x82`, contact/lift detection, `0x04` stop, 6-frame $\pm 2\text{ px}$ alignment + bandpass filter). | Ready & compiled |

---

## 3. How to Open an Upstream Merge Request on `gitlab.freedesktop.org`

### Step 1: Verify Git Identity
Before touching `workspace/libfprint`, ensure your Git identity inside `workspace/libfprint` matches your `freedesktop.org` account (see [`docs/repositories.md`](repositories.md)).

### Step 2: Copy the Driver into `workspace/libfprint`
Create `workspace/libfprint/libfprint/drivers/synaptics_wbdi/` containing:
- `syna_ssm.c` / `syna_ssm.h` (as the primary `FpDevice` class `FpiDeviceSynapticsWbdi`)
- `syna_tls.c` / `syna_tls.h`
- `syna_mode2.c` / `syna_mode2.h`
- `syna_matcher.c` / `syna_matcher.h`
- `syna_template.c` / `syna_template.h`
- `syna_oem.c` / `syna_oem.h`
- `syna_proto.h` & `syna_capture_tables.h`

Register `synaptics_wbdi` in `workspace/libfprint/libfprint/meson.build` under `default_drivers` (linking `openssl` / `libcrypto` and `-lm`).

### Step 3: Upstream Maintainer Checklist before Opening the MR
1. **Formatting:** Run `ninja -C build clang-format` (or `./scripts/uncrustify.sh`) inside `workspace/libfprint` so all C files match GNU/GLib indentation.
2. **State Storage Discussion:** Upstream `libfprint` prefers passing pairing data via `FpPrint` or a standard `/var/lib/fprint/` state path rather than environment variables. Mention in the MR description that `06cb:00dc` pairs once via commands `0x4f`/`0x50` and caches the resulting 184-byte certificate and 65-byte ECDH public key.
3. **`umockdev` CI Trace:** Upstream requires a sanitized USB trace in `tests/synaptics_wbdi/` (using synthetic/non-biometric test frames) so GitLab CI can run `meson test` without physical hardware.
