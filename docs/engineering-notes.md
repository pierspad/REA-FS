# Engineering Notes & Optimizations

This document collects the technical design decisions, signal-processing choices, and latency optimizations implemented in **REA-FS** and in the `drivers/syna_tod` C driver.

---

## 1. Small-Area Sensors ($144 \times 56$ px) Need Spectral Correlation, Not Minutiae

Standard `libfprint` uses NBIS (`mindtct` + `bozorth3`) to extract ridge endings and bifurcations. That works well on large square sensors ($192 \times 192$ or $256 \times 256$), but fails on narrow rectangular sensors (such as $144 \times 56$ pixels, $7.2 \times 2.8\text{ mm}$ at 508 DPI) because a $2.8\text{ mm}$ vertical strip contains too few minutiae (often $< 5$) and high boundary truncation.

**Solution:** Use band-pass spatial filtering (Difference-of-Gaussians tuned to the 6–9 px ridge wavelength) followed by rotated 2D FFT cross-correlation normalized via Summed-Area Tables (SAT).

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

**Solution:** Persist the validated empty-sensor baseline to `/var/lib/fprint/syna_00dc/baseline_cache.bin` (`0600`). On `verify`/`identify`, load the cached baseline in **0 ms** and allow capture to proceed even when the finger is already resting on the sensor at open time (`RMS > 35.0`).

---

## 4. Short Burst Capture (6 Frames $\approx 100\text{ ms}$) & Instant Lift Transition

- Averaging **6 aligned frames** ($\pm 2\text{ px}$ translation search) preserves $>96.8\%$ correlation with a 20-frame window while cutting capture time to ~100 ms and avoiding motion blur.
- **Instant Enrollment Feedback:** `syna_capture_finger_sample` returns immediately after collecting the 6 contact frames — *before* waiting for finger release. This lets `fprintd` emit `enroll-stage-passed` while the finger is still down so the GUI switches from **"HOLD STILL"** to **"LIFT YOUR FINGER"** right away. Physical finger lift (`rms <= 28.0`) is enforced at the start of the next stage.

---

## 5. Multi-Finger `identify` (1:N) & Passwordless Polkit

- **`dev_class->identify`:** When multiple fingers are enrolled, `fprintd-verify` and PAM call `identify` (1:N). Implementing `dev_class->identify` allows matching against all enrolled fingers in a single touch, with progressive sub-template learning (up to 16 diversity-selected views per finger).
- **Polkit Rule (`50-syna-00dc-fprintd.rules`):** `fprintd` defaults to `auth_self_keep`, which expires as soon as a short-lived CLI process (`fprintd-enroll`) exits. Installing `/etc/polkit-1/rules.d/50-syna-00dc-fprintd.rules` allows active local desktop sessions to enroll, verify, and delete prints without repeated `sudo` prompts.
