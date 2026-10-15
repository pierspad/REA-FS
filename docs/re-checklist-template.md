# Fingerprint Sensor Reverse Engineering Checklist (Template)

Copy this file into your private session folder (`sessions/<YYYY-MM-DD-vendor-vid-pid>/checklist.md`) when starting an investigation on a new fingerprint reader. Check off each box (`[x]`) only after verifying it with concrete offline or GUI-captured evidence.

---

## Phase 0 — Bootstrap, Preferences & Privacy Setup
- [ ] **Initialize Private Session:** Run `python3 scripts/rea-fs.py init-session --sensor '<BUS:VID:PID>' --directory sessions/<session-id>`.
- [ ] **Configure Preferences:** Create `.rea-fs.local/preferences.json` (from `docs/preferences.example.json`) defining `autonomy_mode` (`ask` or `autonomous`), allowed categories, and `privacy.allow_upload = false`.
- [ ] **Verify `.gitignore` & Git Identity:** Ensure `sessions/`, `.rea-fs.local/`, `workspace/`, `*.pem`, `*.bin`, `*.raw`, `*.pgm`, and `*.npy` are ignored and that `git config user.name` / `user.email` match the target repository policy.

---

## Phase 1 — Passive Hardware Discovery (No Sensor Touch)
- [ ] **Run Preflight:** Execute `python3 scripts/rea-fs.py preflight --output sessions/<session-id>/preflight.json`.
- [ ] **Record USB / SPI / I2C Descriptors:** Capture `lsusb -v -d <vid>:<pid>` (endpoints, transfer types, max packet sizes, interface classes).
- [ ] **Check Kernel & System Services:** Verify `uname -r`, `fprintd` version, `libfprint` version, and whether any kernel driver or `udev` rule binds to the device.
- [ ] **Firmware / ROM Version Probe (Read-Only):** If a known safe version command exists, query firmware version, build number, and hardware module ID without writing persistent state.

---

## Phase 2 — Upstream Research & OEM Static Analysis
- [ ] **Search `workspace/libfprint`:** Check `libfprint/drivers/` for existing drivers supporting the same vendor (`06cb` Synaptics, `27c6` Goodix, `04f3` Elan, `138a` Validity, `1c7a` LighTuning, etc.) or similar sensor dimensions.
- [ ] **Locate OEM Windows Driver (if needed):** Extract the Windows Update `.cab`/`.inf` package into a private subfolder under `sessions/<session-id>/oem/` (never commit OEM binaries).
- [ ] **Static Binary Analysis (REA / Ghidra):**
  - [ ] Identify USB bulk/control framing, command opcodes, and status codes.
  - [ ] Determine whether communication is cleartext, TLS-wrapped, or proprietary ECDH/AES session-encrypted.
  - [ ] Locate frame dimensions (`rows`, `cols`, header/trailer bytes per row or per frame) and pixel packing (`uint8`, `uint12`, `uint16`, or paired-row interleaving).

---

## Phase 3 — Transport, Session Bootstrap & Raw Frame Capture (GUI Panel)
- [ ] **Non-Root Device Access (`udev` / ACL):** Configure a local `udev` rule or temporary ACL so capture probes run without root wherever possible.
- [ ] **Session Handshake / Pairing:**
  - [ ] Verify whether existing host credentials can be loaded or if first-boot pairing is required.
  - [ ] Store any pairing keys or certificates exclusively in `sessions/<session-id>/` or `/var/lib/fprint/<device>/` with `0700`/`0600` permissions.
- [ ] **First Raw Frame Capture via GUI Panel (`scripts/open-panel.py`):**
  - [ ] Write and validate `.rea-fs.local/panel/plan.json` (`python3 scripts/experiment-panel.py validate --session-dir .rea-fs.local/panel`).
  - [ ] Capture an **empty-sensor baseline** burst (10–20 frames with no finger present).
  - [ ] Capture a **live finger contact** burst via the GUI panel and verify frame boundaries, checksums/trailers, and pixel value distributions.
  - [ ] Delete temporary raw biometric dumps once signal metrics and offline unit tests are recorded.

---

## Phase 4 — Signal Processing & Low-Latency Matcher (< 50 ms)
- [ ] **Empty-Sensor Baseline Calibration:**
  - [ ] Subtract the static fixed-pattern sensor baseline from raw frames.
  - [ ] Persist the validated baseline (`baseline_cache.bin`) so verification starts in **0 ms** even if the user's finger is already resting on the sensor when `verify`/`identify` opens the device.
- [ ] **Contact Detection & Short Burst Averaging:**
  - [ ] Measure RMS energy of `(frame - baseline)` to detect finger contact reliably.
  - [ ] Align ($\pm 2\text{ px}$) and average a short burst of **5–8 stable frames** (~100 ms) to boost SNR without motion blur.
- [ ] **Spatial Ridge Enhancement:**
  - [ ] Apply zero-mean normalization, spatial band-pass filtering (DoG / Gabor tuned to ridge wavelength ~6–9 px), and soft-clamping (`tanh`).
- [ ] **Fast Matcher Architecture (Target: < 40 ms on CPU):**
  - [ ] **Small-Area Sensors (e.g. $144 \times 56$):** Minutiae extractors (NBIS `mindtct`/`bozorth3`) often fail below $100 \times 100\text{ px}$ because too few minutiae fall inside the crop. Use **2D FFT phase/cross-correlation + Summed-Area Tables (SAT)** across rotations (`-30°..+30°`) with coarse-to-fine angle search.
  - [ ] Precompute probe FFT + SAT invariants once per touch (< 1 ms) and reuse across all enrolled templates.
  - [ ] Implement early-exit (`score >= 0.70`) for instant verification.
  - [ ] Add an offline C benchmark (`test_matcher`) verifying latency and genuine vs. impostor score separation.

---

## Phase 5 — System Integration (`fprintd` / TOD / Native `libfprint`)
- [ ] **Multi-Stage Enrollment UX:**
  - [ ] Use 6–8 enrollment stages to cover partial finger placements.
  - [ ] Report `enroll-stage-passed` **immediately** after capturing the 6-frame burst (before waiting for finger release) so the GUI immediately transitions from *Hold Still* to *Lift Finger*.
  - [ ] Wait for physical finger lift (`rms <= release_threshold`) at the start of the next stage before prompting *Place Finger*.
  - [ ] Map brief taps to `FP_DEVICE_RETRY_TOO_SHORT` instead of fatal timeouts.
- [ ] **Multi-Finger `identify` (1:N) + `verify` (1:1):**
  - [ ] Implement both `dev_class->verify` and `dev_class->identify` so `fprintd-verify` and PAM login work across **all** enrolled fingers, not just the first enrolled print.
- [ ] **Polkit Passwordless Local Enrollment/Verification:**
  - [ ] Install a Polkit rule (`/etc/polkit-1/rules.d/50-*-fprintd.rules`) granting `net.reactivated.fprint.device.{enroll,verify,setusername}` to `subject.local && subject.active` so users are not prompted for `sudo`/root passwords on every finger registration.

---

## Phase 6 — Upstreaming to `libfprint`
- [ ] **Zero External File Dependencies:** Replace any pre-recorded initializer blobs with self-contained protocol packet builders and hardware version checks.
- [ ] **Port to `FpiSsm` & `FpiUsbTransfer`:** Convert synchronous USB helper calls into asynchronous `FpiSsm` state machines inside `workspace/libfprint/libfprint/drivers/<driver_name>/`.
- [ ] **Add `umockdev` Regression Tests:** Record sanitized protocol traces for `meson test` in `libfprint`.
