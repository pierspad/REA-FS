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
