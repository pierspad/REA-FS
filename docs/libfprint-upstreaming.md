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
