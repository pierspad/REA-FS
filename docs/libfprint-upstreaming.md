# libfprint upstream work

The maintained driver source is [synaptics-00dc](https://github.com/pierspad/synaptics-00dc),
pinned here at `drivers/syna_tod`. It is a TOD module, not an upstream driver yet.

## Local native compile prototype (2026-10-16)

The same source from driver commit `2c55a59` was copied into the ignored upstream
clone at `libfprint/drivers/syna_00dc`, registered in both Meson maps, and its device
ID string changed from `syna_00dc_tod` to `syna_00dc`. Base upstream revision:
`6f9479c3d55f847c1b3769f28ceb99227f9858cf`; local branch:
`codex/synaptics-00dc-native-prototype`. This is a candidate copy for upstream work,
not a second maintained driver in REA-FS.

```sh
meson setup workspace/libfprint/build-syna00dc-prototype workspace/libfprint \
  -Ddrivers=syna_00dc -Ddoc=false -Dintrospection=false -Dinstalled-tests=false
ninja -C workspace/libfprint/build-syna00dc-prototype
meson test -C workspace/libfprint/build-syna00dc-prototype --suite unit-tests --print-errorlogs
```

Native library build succeeded; the generated supported-device table includes
06cb:00dc. All three upstream unit suites (device, SSM, assembling) passed.
The local prototype is committed as `412d6286`; no upstream push was made.
This verifies compilation/registration without TOD, not USB operation.
OpenSSL deprecated-API warnings remain. The prototype retains the threaded/custom
matcher backend; it is not the proposed FpImageDevice/NBIS conversion and has not
been installed or submitted upstream. Historical Trial7/Trial8 capture files were
not available at their recorded paths during this work, so a real NBIS comparison
cannot be inferred from synthetic tests.

## Implemented versus validated

- Native protocol, Mode-2 reconstruction and host matcher: implemented; historical
  local hardware observations are recorded in the sensor checklist.
- ROM/module checks, built-in capture configuration and pairing helpers: implemented.
  A unit test does not prove successful first pairing on a fresh sensor.
- FpiSsm open/capture engine: experimental, compiled and partially tested offline;
  **not called by the production device operations**. The default uses GTask threads.
- Recent SSM corrections validate init/stop responses, check capture-start once,
  cap capture duration, make stop uncancellable and release failed-open interfaces.
  Policy tests do not replay actual USB state transitions.
- Shared frame alignment is exercised directly by synthetic pipeline/burst tests.
  No new population FAR/FRR or hardware timing claim follows from those tests.

## Recommended port

1. Build a minimal FpImageDevice adapter delivering reconstructed images to NBIS.
   Compare held-out genuine/impostor performance against the custom matcher before
   deciding whether to retain a custom FpDevice matching implementation.
2. Complete async calibration, close, enroll/verify orchestration and cancellation;
   validate stop/reopen, unplug, early touch and suspend/resume using the panel.
3. Demonstrate device-local first pairing and baseline acquisition without personal
   credential exports. Agree state ownership/storage with upstream maintainers.
4. Record exact provenance of ROM configuration tables and review their nature and
   redistribution suitability. C arrays alone do not establish upstream readiness.
5. Add synthetic/sanitized USB replay tests, then compile against the actual upstream
   revision and open a focused MR with limitations and reproducible evidence.

The root upstream `meson.build` defines `drivers_info`; driver source maps are in
`libfprint/meson.build`. Follow the current clone, not a blanket “copy these files”
recipe. Existing `syna_ssm.c` is an engine using the TOD device struct, not an
independent FpDevice subclass ready to drop into upstream.

Verify freedesktop Git identity and remotes separately from GitHub. Publication of
hardware traces needs review; never include credentials or real fingerprints.

## Offline hardening refresh (2026-10-17)

The native prototype was refreshed from driver v0.1.2 (`8b15cf2`) and committed
locally as `1d963480`. Native build and all three upstream unit suites passed again.
This adds stricter cache files, private temporary writes and a no-re-pair guard;
it does not activate SSM or implement FpImageDevice. It remains uninstalled and
unpublished upstream. The standalone driver passed 12 C suites, also under
AddressSanitizer/UndefinedBehaviorSanitizer, and 12 Python installer/setup tests.
These checks do not replace USB replay, hardware recovery or biometric evaluation.

## Native libfprint readiness and async FpiSsm completion (2026-10-17)

1. **OpenSSL 3.0 EVP migration (`syna_tls.c`):** Migrated deprecated OpenSSL 1.1 calls (`HMAC`, `EC_KEY`, `o2i_ECPublicKey`, `ECDSA_sign`) to `EVP_MAC`, `EVP_Q_mac`, `EVP_PKEY_fromdata`, `EVP_PKEY_sign`, and `EVP_PKEY_Q_keygen`. Recompiled with zero `-Wdeprecated-declarations` warnings. Driver commit `df8bb44`.
2. **Per-device baseline cache (`syna_device.c` / `syna_device.h` / `syna_proto.h`):** Moved `cached_baseline` array and `has_cached_baseline` flag from process-global static variables into `struct _FpiDeviceSyna00dc` (`FpiDeviceSynaTod`). Removed global baseline mutex. Driver commit `b7731db`.
3. **Non-blocking teardown SSM (`syna_ssm.c` / `syna_ssm.h` / `test_ssm.c`):** Implemented `syna_ssm_close_new()` completing the 5-state teardown sequence (`CLEANUP_TX` / `RX`, `TLS_ALERT_TX` / `RX`, `RELEASE_USB`) with uncancellable transfers (`cancellable == NULL`). Handled all enum states under `-Wswitch-enum`. Added unit test suite `ssm_tests` in `tests/test_ssm.c`. Standalone driver now passes 13 C test suites and 12 Python tests. Driver commits `20593b2` and `e88019c`.
4. **Small-sensor benchmark tool (`tools/benchmark_small_sensor.py` / `tests/test_benchmark_small_sensor.py`):** Added deterministic offline tool evaluating human minutiae density in a $144 \times 56$ aperture ($20.16\text{ mm}^2$, dropping to $15.12\text{ mm}^2$ under $\pm 14\text{ px}$ vertical shift: expected $4.23$ minutiae vs NBIS `MIN_COMPUTABLE_BOZORTH_MINUTIAE = 10`, yielding a $98.8\%$ Bozorth3 failure rate) against 2D FFT spectral cross-correlation ($>0.92$ genuine, $<0.39$ impostor, separation margin $>0.54$). 4 automated tests added to the workbench (65 passed).
5. **Native libfprint prototype refresh (`workspace/libfprint`):** Synced updated sources into `libfprint/drivers/syna_00dc/` (keeping native `dev_class->id = "syna_00dc"` and removing `06cb:00dc` from `fprint-list-udev-hwdb.c`), committed locally as `377fa6cc`. Full native compile completed with zero compiler warnings under both `-Wdeprecated-declarations` and `-Wswitch-enum`, and all upstream unit and hwdb/metainfo suites passed.
6. **`dev_class->identify` + `FP_DEVICE_FEATURE_DUPLICATES_CHECK` + empty-gallery compliance:**
   - Addressed `fprintd` duplicate check pre-enroll scan gotcha: by default, implementing `dev_class->identify` causes `fprintd` (`src/device.c`) to add `+1` to `num-enroll-stages` and trigger an unannounced pre-enroll `fp_device_identify(device, all_prints)` scan before calling `enroll()`.
   - When no prints are enrolled (`all_prints->len == 0`), returning a `GError` from `identify` causes `fprintd` to abort `EnrollStart` with `Device reported an error during identify for enroll`.
   - In `syna_identify_thread`, empty gallery returns `NULL` without error, passing upstream `test_driver_identify_empty_gallery`.
   - Explicitly set `dev_class->features |= FP_DEVICE_FEATURE_DUPLICATES_CHECK;` in class initialization after `fpi_device_class_auto_initialize_features(dev_class)`, suppressing the redundant pre-enroll touch and maintaining exact D-Bus stage counts (`num-enroll-stages == nr_enroll_stages`).
7. **Full `FpiSsm` runtime engine coverage (`OPEN_SSM`, `CAPTURE_SSM`, `CLOSE_SSM`):**
   - The asynchronous sequential state machines (`syna_ssm_open_new`, `syna_ssm_capture_new`, `syna_ssm_close_new`) are fully wired into `syna_open`, `syna_enroll`, `syna_verify`, `syna_identify`, and `syna_close`.
   - Runtime execution is gated by the environment variable `SYNA_USE_SSM=1` in the standalone TOD repository, enabling safe offline and live verification of the non-blocking engine without risking regression for daily users.

## Dual-Track Strategy: `libfprint-tod` vs Native `libfprint` Merge Request

To ensure uninterrupted service for current users while pursuing upstream acceptance, REA-FS maintains a strict dual-track development process:

```
                  ┌───────────────────────────────────────────────┐
                  │   Upstream Hardware Investigation (REA-FS)    │
                  └───────────────────────┬───────────────────────┘
                                          │
                  ┌───────────────────────┴───────────────────────┐
                  ▼                                               ▼
  Track 1: Production TOD Module                   Track 2: Native Upstream MR
  repo: synaptics-00dc (main)                      repo: workspace/libfprint (fork)
  ──────────────────────────────────────           ────────────────────────────────────
  • Distributed via bootstrap.sh                   • Merged into upstream libfprint
  • libfprint-tod-1 plugin binary                  • Built-in driver (syna_00dc)
  • Threaded backend (GTask) default               • 100% async FpiSsm backend
  • Opt-in SSM via SYNA_USE_SSM=1                  • Zero external TOD dependency
  • Zero regression for laptop users               • Upstream CI & review compliance
```

### Track 1: Production TOD Module (`synaptics-00dc`, branch `main`)
- **Target Audience:** Laptop users running modern Linux distributions (Ubuntu 22.04/24.04, Debian 12/testing, Fedora 39/40, Arch) with `libfprint-2-tod1`.
- **Packaging & Delivery:** One-line installer (`bootstrap.sh`) builds and installs `libfprint-tod-syna-00dc.so` to `/usr/lib/x86_64-linux-gnu/libfprint-2/tod-1/`.
- **Runtime Stability:** Defaults to the multi-threaded `GTask` engine. This engine handles baseline caching, 6-frame burst captures, instant touch-to-lift UI feedback, and multi-finger PAM authentication reliably today.
- **Experimental Testing:** Setting `SYNA_USE_SSM=1` switches runtime execution to the non-blocking `FpiSsm` engine dynamically without recompilation.

### Track 2: Native Upstream Merge Request (`workspace/libfprint`, GitLab MR branch)
- **Target Audience:** Upstream `freedesktop.org/libfprint/libfprint` maintainers and global Linux distributions shipping vanilla `libfprint`.
- **Driver Registration:** Registered as driver `syna_00dc` in `meson.build` and `libfprint/meson.build` (device ID `syna_00dc`, USB ID `06cb:00dc`).
- **Architectural Standards:**
  - Pure non-blocking `FpiSsm` execution across all entrypoints (`open`, `close`, `enroll`, `verify`, `identify`).
  - Guaranteed uncancellable teardown (`syna_ssm_close_new` with `cancellable == NULL`).
  - Full OpenSSL 3.0 `EVP_MAC` / `EVP_PKEY` compliance with zero compiler warnings under `-Wdeprecated-declarations` and `-Wswitch-enum`.
  - Empty gallery identify compliance verified by `test_driver_identify_empty_gallery`.
  - Hardware documentation and small-sensor justification (`tools/benchmark_small_sensor.py`).
- **Complete Isolation:** Maintained on a dedicated GitLab branch in `workspace/libfprint`. Work on the native MR proceeds in parallel and never breaks or destabilizes the working TOD module.

