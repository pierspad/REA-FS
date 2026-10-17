# Engineering Notes & Optimizations

This document collects the technical design decisions, signal-processing choices, and latency optimizations implemented in **REA-FS** and in the `drivers/syna_tod` C driver.

---

## 1. Small-Area Sensors ($144 \times 56$ px) Need Spectral Correlation, Not Minutiae

Standard `libfprint` uses NBIS (`mindtct` + `bozorth3`) to extract ridge endings and bifurcations. That works well on large square sensors ($192 \times 192$ or $256 \times 256$), but fails on narrow rectangular sensors (such as $144 \times 56$ pixels, $7.2 \times 2.8\text{ mm} = 20.16\text{ mm}^2$ at 508 DPI):
- Under a normal $\pm 14\text{ px}$ ($0.7\text{ mm}$) vertical touch displacement, the overlapping area drops to $15.12\text{ mm}^2$. At human finger minutiae density ($\approx 0.28\text{ minutiae/mm}^2$), the expected count in the overlap is $\lambda \approx 4.23$ minutiae.
- NBIS `bozorth3` hardcodes `MIN_COMPUTABLE_BOZORTH_MINUTIAE = 10`; by the Poisson cumulative distribution $P(N < 10 \mid \lambda = 4.23) = 98.8\%$, NBIS rejects almost all genuine touches before scoring.
- Reproducible offline proof: `python3 tools/benchmark_small_sensor.py`.

**Solution:** Use band-pass spatial filtering (Difference-of-Gaussians tuned to the 6–9 px ridge wavelength) followed by rotated 2D FFT cross-correlation normalized via Summed-Area Tables (SAT), which correlates ~6,000 continuous ridge pixels and maintains $>0.92$ genuine vs. $<0.39$ impostor separation.

---

## 2. Sub-40 ms CPU Matching (Why CPU FFT+SAT Beats GPU inside `fprintd`)

- **Why not GPU (`wgpu` / OpenGL / Vulkan)?** `fprintd` runs as a tightly sandboxed `root` systemd service (`ProtectSystem=strict`, no X11/Wayland display socket). Initializing a GPU compute context inside `fprintd` adds 80–250 ms of driver init latency and breaks systemd sandboxing.
- **Pruned CPU 2D FFT + SAT + Precomputed Rotation LUTs:**
  - Precompute the probe's 2D FFT and SAT once per touch (**0.39 ms**).
  - Precompute nearest-neighbor pixel source indices (`rot_idx_lut[61][144*56]`) and rotated mask integral images (`rot_sat_m[61][57*145]`) for all 61 angles (`-30°..+30°`), eliminating `cosf`/`sinf`/`roundf` and mask SAT computation during matching.
  - Prune dead zero-padded rows (`56..127`) during forward horizontal 1D FFTs and prune unreachable shift rows during inverse 1D IFFTs.
  - Fuse the vertical 1D FFT, complex conjugate multiplication, and vertical 1D IFFT column-by-column inside L1 cache.
  - Search coarse angles outward from `0°` (`0, -2, +2, -4, +4, ...`) so upright/slightly tilted touches hit a high-confidence peak (`score >= 0.88`) within the first few angles, refine $\pm 1^\circ$, and return immediately.
  - **Result:** **2.6 ms** for an upright template (`0°`), **11.4 ms** for a `12°`-rotated template with early-exit, and **23.2 ms** worst-case across all 31 coarse + 6 fine angles on a single CPU thread.

---

## 3. Zero-Wait Open via Persistent Baseline Cache (`baseline_cache.bin`)

Acquiring 10–20 empty-sensor frames every time `fprintd` opens the device adds 0.5–1.0 s of lag and causes `clear=0` failures if the user places their finger on the sensor immediately.

**Solution:** Persist the validated empty-sensor baseline to `/var/lib/fprint/syna_00dc/baseline_cache.bin` (`0600`) and cache it per `FpiDeviceSyna00dc` instance (`self->cached_baseline`). On `verify`/`identify`, load the cached baseline in **0 ms** and allow capture to proceed even when the finger is already resting on the sensor at open time (`RMS > 35.0`).

---

## 4. Short Burst Capture (6 Frames $\approx 100\text{ ms}$) & Instant Lift Transition

- Averaging **6 aligned frames** ($\pm 2\text{ px}$ translation search) preserves $>96.8\%$ correlation with a 20-frame window while cutting capture time to ~100 ms and avoiding motion blur.
- **Instant Enrollment Feedback:** `syna_capture_finger_sample` returns immediately after collecting the 6 contact frames — *before* waiting for finger release. This lets `fprintd` emit `enroll-stage-passed` while the finger is still down so the GUI switches from **"HOLD STILL"** to **"LIFT YOUR FINGER"** right away. Physical finger lift (`rms <= 28.0`) is enforced at the start of the next stage.

---

## 5. Multi-Finger `identify` (1:N), `fprintd` Duplicate-Detection Gotcha & Passwordless Polkit

- **`dev_class->identify`:** When multiple fingers are enrolled, `fprintd-verify` and PAM call `identify` (1:N). Implementing `dev_class->identify` allows matching against all enrolled fingers in a single touch, with progressive sub-template learning (up to 16 diversity-selected views per finger).
- **The `fprintd` Duplicate-Detection Pre-Enroll Scan Gotcha:**
  - In `fprintd` (`src/device.c`: `fprint_device_enroll_start` and `on_nr_enroll_stages_changed`), advertising `FP_DEVICE_FEATURE_IDENTIFY` causes `fprintd` to check `FP_DEVICE_FEATURE_DUPLICATES_CHECK` (`1 << 7`).
  - If `FP_DEVICE_FEATURE_DUPLICATES_CHECK` is **not** set, `fprintd` inflates `num-enroll-stages` on D-Bus by `+1` (e.g., exposing 9 stages when `nr_enroll_stages = 8`) and silently invokes `fp_device_identify(device, all_prints, ..., enroll_identify_cb)` *before* starting the actual `enroll()` operation.
  - If `all_prints` is empty (`prints->len == 0`), returning a `GError` from `identify` causes `fprintd` to abort `EnrollStart` immediately with D-Bus error `net.reactivated.Fprint.Error.Internal: Device reported an error during identify for enroll`.
  - **Fix 1 (Empty-gallery compliance):** Per upstream `libfprint` unit test `test_driver_identify_empty_gallery`, empty gallery identify must return `NULL` without error (`fpi_device_identify_report(device, NULL, NULL, NULL)` + `fpi_device_identify_complete(device, NULL)`). In the threaded worker, `syna_identify_thread` checks `!entries || entries->len == 0` and calls `g_task_return_pointer(task, NULL, NULL)`.
  - **Fix 2 (`FP_DEVICE_FEATURE_DUPLICATES_CHECK`):** Explicitly setting `dev_class->features |= FP_DEVICE_FEATURE_DUPLICATES_CHECK;` after `fpi_device_class_auto_initialize_features(dev_class)` tells `fprintd` that duplicate checking is handled or unneeded, preventing the extra pre-enroll scan and keeping D-Bus `num-enroll-stages` equal to `nr_enroll_stages`.
- **Polkit Rule (`50-syna-00dc-fprintd.rules`):** `fprintd` defaults to `auth_self_keep`, which expires as soon as a short-lived CLI process (`fprintd-enroll`) exits. Installing `/etc/polkit-1/rules.d/50-syna-00dc-fprintd.rules` allows active local desktop sessions to enroll, verify, and delete prints without repeated `sudo` prompts.

---

## 6. Upstream `libfprint` Readiness: OpenSSL 3.0 EVP & Async `FpiSsm`

- **OpenSSL 3.0 EVP Compliance (`syna_tls.c`):** Custom TLS 1.2 PRF, HMAC-SHA256, P-256 ECDH key exchange, and ECDSA signing use pure OpenSSL 3.0 `EVP_MAC`, `EVP_Q_mac`, `EVP_PKEY_fromdata`, `EVP_PKEY_sign`, and `EVP_PKEY_Q_keygen` APIs with zero `-Wdeprecated-declarations` warnings.
- **Complete `FpiSsm` Engine (`syna_ssm.c`):** Alongside the production `GTask` backend, non-blocking asynchronous state machines cover the full device lifecycle:
  - **`OPEN_SSM` (`syna_ssm_open_new`):** 19 states handling USB interface claim, ROM version probe (`0x01`), module ID query (`0x75`), deterministic host pairing (`0x4f`/`0x50`), TLS 1.2 handshake flights, cleanup (`0x1a`), and capture initialization (`0x06`), with a dedicated error recovery state releasing claimed interfaces on failure.
  - **`CAPTURE_SSM` (`syna_ssm_capture_new`):** 12 states executing capture start (`0x02`), non-blocking 17,024-byte bulk stream reads on endpoint `0x82`, real-time RMS contact/lift detection, capture stop (`0x04`), and 6-frame alignment/filtering.
  - **`CLOSE_SSM` (`syna_ssm_close_new`):** 5 states executing cleanup (`0x1a`), TLS `close_notify`, and USB interface release. Marked uncancellable (`cancellable == NULL`) to guarantee hardware and interface cleanup even when callers cancel teardown.
- **Full Operation Wiring & Opt-In Gate (`SYNA_USE_SSM=1`):**
  - All device entry points (`syna_open`, `syna_enroll`, `syna_verify`, `syna_identify`, `syna_close`) are wired to both the threaded and `FpiSsm` backends.
  - In the standalone TOD driver, `syna_use_async_ssm()` checks the environment variable `SYNA_USE_SSM=1`, allowing zero-risk offline and hardware verification of the non-blocking engine while maintaining 100% stable `GTask` execution as the default.
  - In the native `libfprint` prototype (`workspace/libfprint`), the asynchronous engine is invoked directly for native upstream integration.

