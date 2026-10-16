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

Completed Tasks 1 through 5 of `docs/superpowers/plans/2026-10-17-upstream-readiness-plan.md`:
1. **OpenSSL 3.0 EVP migration (`syna_tls.c`):** Migrated deprecated OpenSSL 1.1 calls (`HMAC`, `EC_KEY`, `o2i_ECPublicKey`, `ECDSA_sign`) to `EVP_MAC`, `EVP_Q_mac`, `EVP_PKEY_fromdata`, `EVP_PKEY_sign`, and `EVP_PKEY_Q_keygen`. Recompiled with zero `-Wdeprecated-declarations` warnings. Driver commit `df8bb44`.
2. **Per-device baseline cache (`syna_device.c` / `syna_device.h` / `syna_proto.h`):** Moved `cached_baseline` array and `has_cached_baseline` flag from process-global static variables into `struct _FpiDeviceSyna00dc` (`FpiDeviceSynaTod`). Removed global baseline mutex. Driver commit `b7731db`.
3. **Non-blocking teardown SSM (`syna_ssm.c` / `syna_ssm.h` / `test_ssm.c`):** Implemented `syna_ssm_close_new()` completing the 5-state teardown sequence (`CLEANUP_TX` / `RX`, `TLS_ALERT_TX` / `RX`, `RELEASE_USB`) with uncancellable transfers (`cancellable == NULL`). Handled all enum states under `-Wswitch-enum`. Added unit test suite `ssm_tests` in `tests/test_ssm.c`. Standalone driver now passes 13 C test suites and 12 Python tests. Driver commits `20593b2` and `e88019c`.
4. **Small-sensor benchmark tool (`tools/benchmark_small_sensor.py` / `tests/test_benchmark_small_sensor.py`):** Added deterministic offline tool evaluating human minutiae density in 144x56 aperture (20.16 mm^2, dropping to 15.12 mm^2 under +/-14 px vertical shift: expected 4.2 minutiae vs NBIS `MIN_COMPUTABLE_BOZORTH_MINUTIAE = 10`, yielding 98.8% Bozorth3 failure rate) against 2D FFT spectral cross-correlation (>0.92 genuine, <0.39 impostor, separation margin >0.54). 4 automated tests added to workbench (65 passed).
5. **Native libfprint prototype refresh (`workspace/libfprint`):** Synced updated sources into `libfprint/drivers/syna_00dc/`, committed locally as `a29472d1`. Full native compile completed with zero compiler warnings under both `-Wdeprecated-declarations` and `-Wswitch-enum`. All 3 upstream test suites (`fpi-assembling`, `fpi-ssm`, `fpi-device`) passed.
