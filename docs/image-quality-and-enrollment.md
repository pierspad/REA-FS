# Image Quality, Calibration & Enrollment: Verification Guide

This guide separates the specific signal-processing choices on Synaptics `06cb:00dc` ($144 \times 56\text{ px}$) from the general methodology applicable to other fingerprint readers, and outlines what remains to be verified for upstream `libfprint`.

## 1. Upstream `libfprint` Imaging vs. Custom Small-Area Pipeline

In upstream `libfprint` (`6f9479c3d55f`):
- `libfprint/fp-image-device-private.h` defines `IMG_ENROLL_STAGES = 5`.
- `libfprint/fpi-image-device.c` (`fpi_image_device_minutiae_detected`) extracts minutiae via NBIS `mindtct`, appends each valid feature set to the multi-stage `FpPrint`, and matches via `bozorth3` (`libfprint/fpi-print.c`).
- Upstream `FpImageDevice` does **not** build an anatomical finger mosaic or detect which part of the fingertip was touched; desktop animations are UI representations of stage progress.

In our `synaptics-00dc` driver (`drivers/syna_tod/src/syna_device.c`):
- Each enrollment touch captures a 6-frame aligned burst (~100 ms), subtracts the empty-sensor baseline prior to clipping, and stores up to 16 diversity-selected filtered sub-templates across 8 stages (`SYNA_ENROLL_STAGES = 8`).
- `syna_matcher.c` searches rotations in `-30°..+30°` and translations via 2D FFT + SAT normalized cross-correlation (minimum overlap `0.45` with smooth $\sqrt{\text{ov}/0.65}$ weighting, acceptance threshold `0.50`).

## 2. Key Properties & Trade-Offs to Verify

1. **Temporal averaging vs. spatial coverage:** Averaging frames within a single touch reduces random noise (if aligned) but never increases spatial coverage. Only distinct touches separated by physical lift (`rms <= 28.0`) provide new views of the finger.
2. **Max-over-templates score shift:** Taking the maximum correlation across $N$ enrolled sub-templates improves genuine recall on a narrow $2.8\text{ mm}$ sensor, but also gives an impostor finger $N$ chances to exceed the threshold. Any change to $N$, `min_overlap`, or filtering must be checked against non-enrolled impostor fingers without lowering the `0.50` threshold to hide false rejects.
3. **Baseline validity:** `syna_calibrate_baseline` checks that the raw ADC mean lies within `[50..200]` before saving `baseline_cache.bin`. It does not adapt the baseline continuously during contact (which would absorb a stationary fingerprint into the background).
4. **D-Bus `finger-present` semantics:** `finger-present` and `finger-needed` are driver-reported D-Bus properties, not independent hardware lines. Completing the 6-frame burst immediately emits `enroll-stage-passed` so the GUI prompts `LIFT YOUR FINGER` while the finger is still down, and the next stage blocks until 3 empty frames (`rms <= 28.0`) are observed before asserting `finger-needed = true`.

## 3. Upstream `FpImageDevice` Comparison Checklist

Before proposing the driver to upstream `libfprint`, evaluate whether `FpImageDevice` + NBIS can work on decoded Mode-2 frames or whether the custom `FpDevice` spectral matcher is strictly required:

- [ ] Run an offline comparison between NBIS (`mindtct` + `bozorth3` at tested DPI scalings) and `syna_matcher.c` on held-out genuine and impostor captures, recording minutiae counts, score distributions, and false-accept / false-reject counts.
- [ ] Keep synthetic golden tests for Mode-2 decoding, pre-clipping baseline subtraction, and burst alignment free of real biometric data.
- [ ] Convert blocking `GTask` USB operations to non-blocking `FpiSsm` state machines (`syna_ssm.c`) and verify cancellation, reopen, unplug, and suspend/resume via the GUI panel.
