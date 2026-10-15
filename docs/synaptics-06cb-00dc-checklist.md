# Synaptics `06cb:00dc` Project Checklist & `libfprint` Upstream Roadmap

This checklist instantiates [`docs/re-checklist-template.md`](re-checklist-template.md) for the **Synaptics WBDI `06cb:00dc`** sensor (ROM `6.20` build `154`, hardware module `0x2449`, $144 \times 56$ pixels @ 508 DPI).

---

## Phase 0 — Bootstrap, Preferences & Privacy Setup
- [x] **Initialize Private Session:** `sessions/2026-10-15-synaptics-06cb-00dc/checkpoint.md` created (`0700` dir, `0600` files).
- [x] **Configure Preferences:** `.rea-fs.local/preferences.json` configured with local autonomy preferences and `allow_upload: false`.
- [x] **Verify `.gitignore` & Git History Hygiene:** Confirmed zero biometric scans (`*.bin`, `*.raw`, `*.pgm`, `*.npy`) or private keys (`*.pem`) exist in Git history; all temporary raw finger captures removed from disk and blocked in `.gitignore`.

---

## Phase 1 — Passive Hardware Discovery
- [x] **Run Preflight:** Identified USB device `06cb:00dc` on bus `001`, vendor-specific interface (`0xff/0x00/0x00`), EP1 OUT (`0x01`) and EP1 IN (`0x81`, `0x83`).
- [x] **Firmware / ROM Version Probe:** Verified cleartext command `0x01` returns product `'B'` (`0x42`), firmware `6.20` build `154`, security flag `0x02`.
- [x] **Extended Metadata (`0x19` / `0x75`):** Identified module ID `0x2449` (9289), $112 \times 152$ raw stream geometry ($56 \times 144$ after pair-averaging and 8-byte trailer stripping).

---

## Phase 2 — Upstream Research & OEM Static Analysis
- [x] **Search `workspace/libfprint`:** Confirmed `synaptics.c` in upstream `libfprint` targets MisMatch-in-Sensor (`06cb:00df`, etc.) where matching runs on-chip, whereas `06cb:00dc` is a host-side raw-image sensor (`product_id == 'B'`) requiring host TLS 1.2 session establishment, Mode 2 frame decoding, and host-side image matching.
- [x] **Static Binary Analysis (`synaWudfBioUsb124.dll` via REA):**
  - [x] Decoded TLS 1.2 custom ECDHE-ECDSA-AES256-GCM handshake over USB bulk endpoints (`0x44` framing).
  - [x] Decoded `17,024`-byte raw frame layout (`112` rows $\times$ `152` bytes) and Mode 2 spatial reconstruction (`56` rows $\times$ `144` cols).

---

## Phase 3 — Transport, Session Bootstrap & Raw Frame Capture (GUI Panel)
- [x] **TLS 1.2 Session Establishment in Native C (`drivers/syna_tod/src/syna_tls.c`):** Implemented full OpenSSL custom BIO + ECDH (`prime256v1`) + AES-256-GCM record framing (`0x44 0x00 0x00 0x00` prefix).
- [x] **Raw Stream Capture (`drivers/syna_tod/src/syna_usb.c`):** Implemented command `0x80` capture initializer, `0x82` frame request loop, and `0x86` stream drain.
- [x] **GUI Panel Integration (`scripts/open-panel.py` + `panel/`):** All hardware enrollment, verification, and diagnostics run through the Rust `egui` panel with structured logs in `.rea-fs.local/panel/`.

---

## Phase 4 — Signal Processing & Low-Latency Matcher (< 50 ms)
- [x] **Persistent Empty-Sensor Baseline (`baseline_cache.bin`):**
  - [x] Saved atomically (`0600`) to `/var/lib/fprint/syna_00dc/baseline_cache.bin` whenever an empty sensor is observed.
  - [x] Eliminated the 10–20 frame open delay during `verify`/`identify`; works even if the finger is already resting on the sensor at device open (`clear=0` fallback to cached baseline).
- [x] **Short Burst Capture (6 Frames):**
  - [x] Reduced contact window from 20–25 frames (~0.5 s) to **6 frames** (~100 ms) with $\pm 2\text{ px}$ translation alignment, preserving $>96.8\%$ correlation while eliminating motion blur.
- [x] **Pruned 2D FFT + Summed-Area Table (SAT) Matcher (`drivers/syna_tod/src/syna_matcher.c`):**
  - [x] Replaced $O(N \cdot M)$ spatial loops (~3,000 ms) with pruned 1D/2D radix-2 FFT cross-correlation + $O(1)$ SAT local energy normalization across `-30°..+30°`.
  - [x] Precomputed probe FFT + SAT (`SynaPrecomputedProbe`) in **0.50 ms**.
  - [x] Single-template match runs in **37.39 ms**; early-exit verification (`score >= 0.72`) completes in **37.91 ms** total CPU time.
  - [x] Lowered minimum overlap tolerance to `0.45` with smooth $\sqrt{\text{ov}/0.65}$ weighting to tolerate vertical finger shifts on the narrow $2.8\text{ mm}$ ($56\text{ px}$) sensor height.
  - [x] Verified genuine (`0.83–0.92`) vs. impostor (`0.28–0.32`) separation around the `0.50` threshold.

---

## Phase 5 — System Integration (`fprintd` / TOD Module)
- [x] **8-Stage Enrollment (`SYNA_ENROLL_STAGES = 8`):** Captures 8 progressive touches (up to 16 diversity-selected sub-templates) to cover diverse angles and placements on the small $144 \times 56$ sensor.
- [x] **Instant "Hold Still" $\to$ "Lift Finger" Transition:** `syna_capture_finger_sample` returns immediately after the 6-frame burst so `fprintd` emits `enroll-stage-passed` while the finger is still down, switching the GUI to **"SOLLEVA IL DITO"** right away; the next stage waits for physical lift (`rms <= 28.0`) before prompting **"APPOGGIA IL DITO"**.
- [x] **Multi-Finger `identify` (1:N) Support (`syna_identify`):** Implemented `dev_class->identify` so `fprintd-verify` and desktop PAM login match against **all** enrolled fingers in a single touch.
- [x] **Passwordless Local Polkit Rule (`drivers/syna_tod/50-syna-00dc-fprintd.rules`):** Installed into `/etc/polkit-1/rules.d/50-syna-00dc-fprintd.rules` by `scripts/install-tod-driver.sh` so active local desktop sessions can enroll, verify, and delete prints without repeated `sudo`/Polkit password prompts.

---

## Phase 6 — Remaining Tasks for Native `libfprint` Upstreaming

To move `drivers/syna_tod` from an external `libfprint-tod` plugin into an upstream-mergeable driver inside `workspace/libfprint/libfprint/drivers/synaptics_wbdi/`:

- [ ] **1. Pre-Init Hardware Verification (`0x01` + `0x19`/`0x75`):**
  - Query and assert ROM version (`6.20` build `154`) and module ID (`0x2449`) before sending the `0x80` capture initializer sequence.
- [ ] **2. Programmatic `0x80` Capture Initializer & `0x82` Frame Request:**
  - Replace the external `capture_init.bin` (723 bytes) and `capture_packet.bin` (137 bytes) files with compiled-in const arrays / register serializer functions, gated by the hardware version check above.
- [ ] **3. Self-Contained Host Pairing (`0x47` Certificate Provisioning):**
  - Implement first-use host ECDSA P-256 keypair generation and sensor certificate exchange in C so a clean Linux install can pair with an unprovisioned `06cb:00dc` sensor without importing keys from a prior pairing session.
- [ ] **4. Port Synchronous `libusb` Calls to Asynchronous `FpiSsm` + `FpiUsbTransfer`:**
  - Convert `syna_usb.c` and the GTask worker threads in `syna_device.c` into `libfprint`'s native non-blocking state machine (`FpiSsm`) using `fpi_usb_transfer_new()` and `fpi_usb_transfer_submit()`.
- [ ] **5. Upstream `umockdev` Test Suite:**
  - Add synthetic/sanitized USB + TLS session traces under `workspace/libfprint/tests/` so upstream CI can test open, enroll, verify, and identify without physical hardware.
