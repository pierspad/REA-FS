# Execution Plan: Native `libfprint` Readiness & Async `FpiSsm` Completion

> **Target Executor:** Cost-effective coding model (following step-by-step instructions).
> **Reviewer:** Larger model / Maintainer (post-execution audit).
> **Strict Rules:**
> - Do NOT run `setup.sh`, `fprintd-enroll`, `fprintd-verify`, or any hardware-opening executable from the terminal.
> - Do NOT lower matcher thresholds (`0.50` / `0.45`), weaken TLS/OEM validation, or edit PAM/Polkit.
> - Every C or Python change MUST compile cleanly and pass `python3 scripts/rea-fs.py check` before committing.
> - Make small, atomic commits in `/home/ribben/Desktop/pierspad/github/synaptics-00dc` first, run tests, and only update the `drivers/syna_tod` submodule in `/home/ribben/Desktop/pierspad/github/REA-FS` after verifying everything passes.

---

## Context & Current State

1. **What works in production today (`v0.1.2`):**
   - Full USB + OEM TLS 1.2 (`prime256v1` ECDH/ECDSA + AES-256-CBC + HMAC-SHA256) session establishment (`syna_tls.c`).
   - Built-in ROM `6.20` (`0x2449`) capture initializer (`0x06`) and frame request (`0x02`) (`syna_capture_tables.h`).
   - Pre-clip Mode-2 Kronecker $16 \times 16$ decoding + 6-frame translation-aligned burst averaging (`syna_image.c`).
   - Sub-30 ms CPU 2D FFT + Summed-Area Table (SAT) + precomputed rotation LUT matcher (`syna_matcher.c`).
   - 8-stage enrollment, instant `HOLD STILL` $\to$ `LIFT YOUR FINGER` feedback, 1:1 `verify`, and 1:N `identify` (`syna_device.c`).
   - Persistent `baseline_cache.bin` for zero-wait open (`syna_device.c`).

2. **What is missing for upstream `libfprint` (`docs/libfprint-upstreaming.md`):**
   - **Task 1 — OpenSSL 3.0 Clean Compilation (Zero Deprecated API Warnings):**
     In `workspace/libfprint`, building the native prototype emits compiler warnings because `syna_tls.c` uses low-level `EC_KEY` / `ECDSA_do_sign` / `HMAC_Init_ex` APIs deprecated in OpenSSL 3.0. Upstream `libfprint` uses `-Werror=deprecated-declarations` or rejects deprecated OpenSSL 1.1 calls in review.
   - **Task 2 — Per-Device Persistent State Path Isolation:**
     Currently `syna_oem.c` / `syna_device.c` store `baseline_cache.bin` and credentials in a single model-wide folder (`/var/lib/fprint/syna_00dc`) and keep a process-global static baseline array in `syna_device.c`. Moving the cached baseline into the `FpiDeviceSynaTod` instance struct (with disk load/save scoped per device or clean per-instance lifecycle) removes mutable global state.
   - **Task 3 — Complete `FpiSsm` Async State Machines (`syna_ssm.c` / `syna_ssm.h`):**
     Currently `syna_ssm.c` implements `syna_ssm_open_new()` and `syna_ssm_capture_new()`, while close (`0x1a` cleanup + encrypted TLS `close_notify` + USB interface release) is not yet encapsulated in an `FpiSsm` close machine (`syna_ssm_close_new()`). Adding `syna_ssm_close_new()` and unit-testing it in `tests/test_ssm.c` completes the non-blocking transport trio (`open`, `capture`, `close`).
   - **Task 4 — Reproducible Offline `FpImageDevice` / NBIS vs. Spectral Matcher Benchmark Tool:**
     Upstream `libfprint` maintainers will ask why `06cb:00dc` uses a custom spectral correlation matcher instead of `FpImageDevice` + NBIS (`mindtct`/`bozorth3`). We need a deterministic, self-contained C or Python test/script (`tools/benchmark_matcher_vs_nbis.py` + unit test) that evaluates synthetic ridge strips ($144 \times 56$ at realistic ridge period $6.2\text{ px}$ with partial vertical shift and noise) through both minutiae-count/Bozorth thresholds and our 2D FFT+SAT matcher, outputting a structured JSON/Markdown table of recall, impostor separation, and runtime.
   - **Task 5 — Sync the Local Native `libfprint` Prototype (`workspace/libfprint`):**
     Copy the updated C sources into `workspace/libfprint/libfprint/drivers/syna_00dc/`, build with Meson/Ninja, and verify all 3 upstream unit test suites pass with zero compiler warnings.

---

## Step-by-Step Tasks for the Executor Model

### Task 1: Replace Deprecated OpenSSL 1.1 Calls in `syna_tls.c` with OpenSSL 3 `EVP` / `EVP_MAC` APIs

**Files to inspect & edit:**
- `/home/ribben/Desktop/pierspad/github/synaptics-00dc/src/syna_tls.c`
- `/home/ribben/Desktop/pierspad/github/synaptics-00dc/tests/test_tls.c`

**Instructions:**
1. Run `meson compile -C /home/ribben/Desktop/pierspad/github/synaptics-00dc/build` and check any `-Wdeprecated-declarations` warnings in `src/syna_tls.c` (specifically `HMAC_CTX_new`, `HMAC_Init_ex`, `EC_KEY_*`, `EC_POINT_point2oct`, `ECDSA_do_verify`, `ECDSA_do_sign`, `ECDH_compute_key`).
2. Replace deprecated OpenSSL 1.1 calls in `src/syna_tls.c` with their OpenSSL 3.0 `EVP_PKEY`, `EVP_MD_CTX`, `EVP_MAC` (`OSSL_MAC_PARAM_DIGEST`), and `EVP_PKEY_CTX` equivalents (`<openssl/evp.h>`, `<openssl/core_names.h>`, `<openssl/param_build.h>`, `<openssl/ecdsa.h>`).
   - *Constraint:* Preserve exact byte-for-byte wire compatibility! Especially for deterministic `HS_KEY_PAIR_GEN` P-256 key derivation (`BIGNUM` privkey + `EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1)` + `EC_POINT_mul` + `EVP_PKEY_fromdata` with `OSSL_PARAM_BLD`), raw X/Y coordinate extraction (`EVP_PKEY_get_bn_param` with `OSSL_PKEY_PARAM_EC_PUB_X` and `OSSL_PKEY_PARAM_EC_PUB_Y`), ECDH shared secret derivation (`EVP_PKEY_derive_init` / `EVP_PKEY_derive`), and ECDSA raw `(r, s)` / DER signature verification.
3. Run:
   ```sh
   meson test -C /home/ribben/Desktop/pierspad/github/synaptics-00dc/build --print-errorlogs
   ```
   All 12 C test suites (especially `tls_tests` and `pairing_tests`) MUST pass with zero warnings under `-Wdeprecated-declarations`.

---

### Task 2: Move Process-Global Baseline Cache into Device Instance Struct

**Files to inspect & edit:**
- `/home/ribben/Desktop/pierspad/github/synaptics-00dc/src/syna_device.c`
- `/home/ribben/Desktop/pierspad/github/synaptics-00dc/src/syna_ssm.h`

**Instructions:**
1. Inspect `src/syna_device.c` where `g_cached_baseline` and `g_has_cached_baseline` are declared as file-scope `static` variables.
2. Add `float cached_baseline[SYNA_IMG_WIDTH * SYNA_IMG_HEIGHT];` and `gboolean has_cached_baseline;` directly to `struct _FpiDeviceSynaTod` in `src/syna_ssm.h`.
3. Update `syna_load_baseline_cache()`, `syna_save_baseline_cache()`, `syna_calibrate_baseline()`, `syna_open_thread()`, and `syna_capture_finger_sample()` in `src/syna_device.c` to read and write `self->cached_baseline` and `self->has_cached_baseline` instead of a process-global static array.
4. Run `meson test -C /home/ribben/Desktop/pierspad/github/synaptics-00dc/build --print-errorlogs` and verify all tests pass.

---

### Task 3: Add `syna_ssm_close_new()` to `syna_ssm.c` and Unit Test in `test_ssm.c`

**Files to inspect & edit:**
- `/home/ribben/Desktop/pierspad/github/synaptics-00dc/src/syna_ssm.h`
- `/home/ribben/Desktop/pierspad/github/synaptics-00dc/src/syna_ssm.c`
- `/home/ribben/Desktop/pierspad/github/synaptics-00dc/tests/test_ssm.c`

**Instructions:**
1. In `src/syna_ssm.h`, declare:
   ```c
   typedef enum {
       SYNA_CLOSE_STEP_CLEANUP_TX = 0,
       SYNA_CLOSE_STEP_CLEANUP_RX,
       SYNA_CLOSE_STEP_TLS_ALERT_TX,
       SYNA_CLOSE_STEP_TLS_ALERT_RX,
       SYNA_CLOSE_STEP_RELEASE_USB,
       SYNA_CLOSE_NUM_STATES,
   } SynaCloseStep;

   FpiSsm *syna_ssm_close_new(FpDevice *dev, SynaSsmUsbRunner runner, gpointer user_data);
   ```
2. Implement `syna_ssm_close_new()` in `src/syna_ssm.c`:
   - `SYNA_CLOSE_STEP_CLEANUP_TX` / `RX`: if `self->tls.established`, build and send encrypted application command `0x1a` (`{0x1a}`), receive response, and validate status is `0x0000` or `0x0491`.
   - `SYNA_CLOSE_STEP_TLS_ALERT_TX` / `RX`: build encrypted TLS `close_notify` alert (`syna_tls_build_close_notify`), send with uncancellable transfer (`NULL` cancellable, `1000 ms` timeout), and read optional alert response (ignoring benign EOF/timeout on close).
   - `SYNA_CLOSE_STEP_RELEASE_USB`: zero/free `self->tls` and `self->oem`, and release USB interface `0` if claimed.
3. Add deterministic unit tests in `tests/test_ssm.c` verifying:
   - Clean 5-state progression of `syna_ssm_close_new()`.
   - Proper state enum count (`SYNA_CLOSE_NUM_STATES == 5`).
   - Uncancellable transfers during close (`cancellable == NULL` on cleanup/alert).
4. Run `meson test -C /home/ribben/Desktop/pierspad/github/synaptics-00dc/build --print-errorlogs` and confirm `ssm_tests` passes.

---

### Task 4: Add Deterministic Offline Matcher vs. Minutiae Benchmark (`tools/benchmark_small_sensor.py`)

**Files to create/edit in REA-FS:**
- `/home/ribben/Desktop/pierspad/github/REA-FS/tools/benchmark_small_sensor.py`
- `/home/ribben/Desktop/pierspad/github/REA-FS/tests/test_benchmark_small_sensor.py`

**Instructions:**
1. Create `tools/benchmark_small_sensor.py` (using only Python standard library + `numpy`) that generates deterministic synthetic $144 \times 56$ curved/branching ridge fields at the measured $6.2\text{ px}$ wavelength with realistic vertical offsets ($\pm 14\text{ px}$ on a $56\text{ px}$ height), small rotations ($\pm 10^\circ$), and sensor noise.
2. Compute and report:
   - Expected visible minutiae count in a $144 \times 56$ ($7.2 \times 2.8\text{ mm}$) aperture vs. `MIN_COMPUTABLE_BOZORTH_MINUTIAE = 10`.
   - Genuine vs. impostor score separation using `tools/placement_overlap.py` (spectral cross-correlation).
3. Add a fast unit test in `tests/test_benchmark_small_sensor.py` so `python3 scripts/rea-fs.py check` exercises it automatically.

---

### Task 5: Refresh `workspace/libfprint` Native Prototype & Run Full Check

**Instructions:**
1. Copy the updated `src/*.c` and `src/*.h` from `/home/ribben/Desktop/pierspad/github/synaptics-00dc/src/` into `/home/ribben/Desktop/pierspad/github/REA-FS/workspace/libfprint/libfprint/drivers/syna_00dc/` (keeping the `syna_00dc` native driver ID).
2. Compile and run the upstream unit tests:
   ```sh
   ninja -C /home/ribben/Desktop/pierspad/github/REA-FS/workspace/libfprint/build-syna00dc-prototype
   meson test -C /home/ribben/Desktop/pierspad/github/REA-FS/workspace/libfprint/build-syna00dc-prototype --suite unit-tests --print-errorlogs
   ```
3. Run the complete workbench and driver test suite from `/home/ribben/Desktop/pierspad/github/REA-FS`:
   ```sh
   python3 scripts/rea-fs.py check
   ```
4. Update `docs/libfprint-upstreaming.md` and `sessions/2026-10-15-synaptics-06cb-00dc/checkpoint.md` with the exact test counts, compiler warning status, and commit hashes. Do NOT claim hardware validation for offline changes.
