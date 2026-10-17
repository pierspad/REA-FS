# New-sensor investigation checklist

Copy into your private session. Mark a step complete only with an evidence reference;
mark inapplicable steps explicitly. See [getting started](getting-started.md).
This checklist does not prescribe Synaptics thresholds, commands or algorithms.

## Bootstrap and discovery

- [ ] Read AGENTS.md and the mandatory workflow; resume or initialize a private session.
- [ ] Configure preferences using the actual schema: `authorization.mode`, scoped
  `allowed_actions`/`excluded_actions`, and `data.external_transfer: false`.
  An unconfigured example is not authorization.
- [ ] Check Git state/identity and private-path ignores; initialize missing submodules
  without discarding local changes. Keep secrets and captures outside tracked paths.
- [ ] Run preflight and record bus/ID, kernel, distribution and library versions.
- [ ] Check upstream drivers, unsupported lists and related issues separately.
- [ ] Identify the matching OEM source, revision, hash and applicable hardware.

## Protocol research

- [ ] Determine transport, endpoints/framing, statuses, authentication and cleanup.
- [ ] Establish whether matching is on-device or on-host and whether images are exposed.
- [ ] Inspect diagnostic open/reset/pairing effects before executing a probe.
- [ ] Record facts, hypotheses and alternatives in the investigation template.
- [ ] Define one experiment, timeout, stop condition and observable result; physical
  tests use a validated GUI plan, then logs and user notes inform the checkpoint.
- [ ] Review device access permissions within authorization; do not broaden PAM,
  sudo or Polkit permissions as a shortcut.

## Acquisition and decoding — only if images are exposed

- [ ] Verify packet boundaries, lengths, encoding, checksums and geometry against evidence.
- [ ] Compare empty-sensor and contact samples if appropriate to this sensor.
- [ ] Validate reconstruction before interpreting apparent ridges or minutiae.
- [ ] Determine whether calibration, frame alignment or averaging is needed by measurement.
- [ ] Keep private data only for the agreed retention period; use synthetic/sanitized
  fixtures for published tests, not real fingerprint images or credentials.

## Recognition and integration

- [ ] Evaluate the upstream image-device/NBIS path where applicable; justify a custom
  matcher with independent results, not a fixed image-size rule.
- [ ] Separate enrollment, threshold-development and held-out verification touches.
- [ ] Record genuine/impostor attempts, retries, timeouts and sample sizes; synthetic
  tests or self-matches do not establish false-accept/false-reject rates.
- [ ] Measure open/capture/match/close separately; caching does not imply zero latency.
- [ ] Use actual device events for touch/lift feedback and define retry semantics.
- [ ] Implement the relevant libfprint operations and test verify/identify separately.
- [ ] Test cancellation/reopen, short transfers, malformed replies, unplug and resume.
- [ ] Validate clean installation/pairing without developer credentials, backup and recovery.
- [ ] Retain distribution authorization policy; treat login configuration separately.

## Upstream and distribution

- [ ] Read the current upstream contributor requirements and choose an appropriate API.
- [ ] Review asynchronous I/O, cancellation, storage ownership and multi-device state.
- [ ] Record exact table/firmware/source provenance; C arrays do not settle redistribution.
- [ ] Add replay/error tests without secrets, compile and test the native integration.
- [ ] Obtain hardware evidence on another device/revision before widening support claims.
- [ ] Publish only reviewed source and redacted reports with explicit authorization.

## Phase 5 — `fprintd` & `libfprint` Integration Gotchas (Lessons from Synaptics `06cb:00dc`)

When integrating a reverse-engineered sensor driver into `libfprint` and testing against `fprintd`, several non-obvious daemon interactions, architectural pitfalls, and matching constraints emerge. Every driver author must address the following verified gotchas:

### 1. The `fprintd` Duplicate-Detection Pre-Enroll Scan (`FP_DEVICE_FEATURE_DUPLICATES_CHECK`)
- [ ] **Understand `fprintd` stage injection:**
  In `fprintd` (`src/device.c`: `fprint_device_enroll_start` and `on_nr_enroll_stages_changed`), whenever a driver implements `dev_class->identify` (which causes `fpi_device_class_auto_initialize_features()` to set `FP_DEVICE_FEATURE_IDENTIFY`):
  - If `FP_DEVICE_FEATURE_DUPLICATES_CHECK` (`1 << 7`) is **NOT** set on `dev_class->features`, `fprintd` assumes the hardware lacks duplicate prevention.
  - `fprintd` silently adds `+1` to `num-enroll-stages` on the D-Bus interface (e.g., a driver with `nr_enroll_stages = 8` exposes `num-enroll-stages = 9` to desktop frontends like GNOME Settings).
  - Before starting `fp_device_enroll()`, `fprintd` runs `fp_device_identify(device, all_prints, ..., enroll_identify_cb)` as a pre-enroll scan to check if the presented finger is already enrolled under any user.
- [ ] **Handle empty gallery cleanly without `GError`:**
  - If no fingers are enrolled on the system (`all_prints->len == 0`), `fprintd` still calls `fp_device_identify()` with this empty gallery during pre-enroll.
  - If the driver's `identify` implementation returns a `GError` (e.g., `G_IO_ERROR_INVALID_DATA` "No valid enrolled templates available") instead of completing gracefully, `fprintd` immediately aborts `EnrollStart` with:
    `net.reactivated.Fprint.Error.Internal: Device reported an error during identify for enroll`
  - Upstream `libfprint` enforces this requirement via unit test `test_driver_identify_empty_gallery`: an `identify` call on an empty gallery (`prints->len == 0`) **MUST** complete without error:
    ```c
    if (!prints || prints->len == 0) {
        fpi_device_identify_report (device, NULL, NULL, NULL);
        fpi_device_identify_complete (device, NULL);
        return;
    }
    ```
    (In a threaded GTask driver, return a `NULL` pointer without setting a `GError` so `on_identify_completed` reports no match and completes with `error = NULL`).
- [ ] **Suppress extraneous pre-enroll touch via `FP_DEVICE_FEATURE_DUPLICATES_CHECK`:**
  - If duplicate detection is handled within the driver or is unnecessary for the enrollment workflow, set:
    ```c
    fpi_device_class_auto_initialize_features (dev_class);
    dev_class->features |= FP_DEVICE_FEATURE_DUPLICATES_CHECK;
    ```
  - This prevents `fprintd` from prepending an extra `identify` touch before `enroll`, keeps D-Bus `num-enroll-stages` strictly equal to `dev_class->nr_enroll_stages`, and avoids confusing desktop users with an unannounced extra scan.

### 2. Verification via `identify` (1:N vs. 1:1)
- [ ] **Understand `libfprint` 1:1 fallback:**
  In `libfprint`, `fp_device_verify()` checks whether `cls->verify` is defined. If `cls->verify == NULL` and `cls->identify != NULL`, `libfprint` automatically wraps `cls->identify` by passing a synthetic 1-element gallery `[target_print]`.
- [ ] **Understand `fprintd` multi-finger verification (`Verify("any")`):**
  When `FP_DEVICE_FEATURE_IDENTIFY` is advertised by the driver, `fprintd-verify` and the PAM module (`pam_fprintd`) use `fp_device_identify()` across all enrolled fingers belonging to the user.
  - This enables passwordless single-touch authentication where the user does not have to pre-select which finger they touch the sensor with.
  - Drivers supporting host-side matching should prioritize a unified gallery matcher (`match_probe_against_gallery`) that iterates through all candidate prints and sub-templates, tracking the highest normalized correlation score, optimal rotation angle, and overlap percentage.

### 3. Multi-Stage Zone Guidance for Tiny Capacitive Sensors
- [ ] **Recognize minutiae extraction limits on narrow apertures:**
  Standard `libfprint` NBIS pipeline (`mindtct` + `bozorth3`) fails on small rectangular sensors (e.g., $144 \times 56$ px @ 508 DPI, $7.2 \times 2.8\text{ mm} = 20.16\text{ mm}^2$).
  - With a typical vertical finger shift of $\pm 14\text{ px}$ ($0.7\text{ mm}$), the effective overlap region is only $15.12\text{ mm}^2$.
  - At human minutiae density ($\approx 0.28\text{ minutiae/mm}^2$), the expected count in the overlapping aperture is $\lambda \approx 4.23$ minutiae.
  - Because Bozorth3 requires `MIN_COMPUTABLE_BOZORTH_MINUTIAE = 10`, Poisson probability $P(N < 10 \mid \lambda = 4.23) = 98.8\%$ causes almost all genuine touches to be rejected before scoring.
- [ ] **Implement progressive multi-zone enrollment:**
  Small sensors require multi-stage enrollment guidance (e.g., 8 stages) directing the user to present different zones of the fingertip:
  - Stages 1–2: Center / flat pad.
  - Stage 3: Tip of the finger.
  - Stage 4: Base / lower joint.
  - Stages 5–6: Left edge / tilted.
  - Stages 7–8: Right edge / tilted.
  Storing diversity-selected sub-templates (up to 16 views per finger) ensures the composite gallery covers the whole fingertip.
- [ ] **Employ rotation-tolerant FFT cross-correlation with SAT energy normalization:**
  - Filter images with a spatial Difference-of-Gaussians (DoG) bandpass tuned to the 6–9 px ridge wavelength.
  - Search rotations across $\pm 30^\circ$ (or $\pm 45^\circ$) outward from $0^\circ$ (`0°, -2°, +2°, -4°, +4°, ...`) using 2D radix-2 FFT cross-correlation.
  - Normalize correlation energy locally in $O(1)$ using Summed-Area Tables (SAT / integral images).
  - Implement early-exit thresholds ($score \ge 0.88$) so upright or near-upright genuine touches verify in under 3–12 ms on a single CPU core without incurring full angular sweeps.

### 4. OpenSSL 3.0 `EVP_MAC` Migration & `FpiSsm` Non-Blocking State Machines
- [ ] **Modern OpenSSL 3.0 cryptography without deprecation warnings:**
  Sensors utilizing custom TLS-over-USB sessions (such as Synaptics WBDI pairing and AES-256-GCM record layers) must avoid deprecated OpenSSL 1.1 APIs:
  - Replace `HMAC_CTX` and `HMAC()` with `EVP_MAC_fetch(NULL, "HMAC", NULL)`, `EVP_MAC_init`, and `EVP_MAC_final` (or `EVP_Q_mac()`).
  - Replace `EC_KEY_new_by_curve_name()` and `EC_KEY_generate_key()` with `EVP_PKEY_Q_keygen(NULL, NULL, "EC", "prime256v1")`.
  - Replace `o2i_ECPublicKey()` with `EVP_PKEY_fromdata()` using `OSSL_PKEY_PARAM_PUB_KEY`.
  - Replace `ECDSA_sign()` with `EVP_PKEY_sign_init()` / `EVP_PKEY_sign()`.
  This ensures zero `-Wdeprecated-declarations` warnings under modern Linux compiler toolchains.
- [ ] **Structured asynchronous sequential state machines (`FpiSsm`):**
  Upstream `libfprint` requires non-blocking asynchronous drivers based on GLib event loops:
  - Structure driver lifecycle around sequential state machines using `FpiSsm` and `FpiUsbTransfer`.
  - **`OPEN_SSM`**: Interface claim, firmware/ROM identity checks (e.g., 0x01/0x75), first-boot host pairing (0x4f/0x50), TLS flight exchange, and capture pipeline configuration (0x1a/0x06). On failure, transition to a dedicated release state that frees claimed USB interfaces.
  - **`CAPTURE_SSM`**: Start capture command (0x02), asynchronous streaming bulk transfers on endpoint 0x82, frame assembly, finger contact detection (RMS calculation), stop capture (0x04), and frame averaging/filtering.
  - **`CLOSE_SSM`**: Teardown state machine executing cleanup commands (0x1a), TLS alert / `close_notify`, and USB interface release. **Crucially, `CLOSE_SSM` must be uncancellable** (`cancellable == NULL`), ensuring USB cleanup completes cleanly even if the caller or daemon attempts to cancel the closing operation.
