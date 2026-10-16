# How the Investigation Works: From Sensor to Desktop

REA-FS is a reverse-engineering workbench and driver prototype for unsupported Linux fingerprint readers. It separates passive discovery, static binary analysis, bounded USB/TLS probing, signal reconstruction, and system integration so coding agents can work methodically without guessing protocols or inventing results.

## 1. Passive Discovery & Upstream Check

Before writing code or touching the sensor, identify:
- Bus (`USB`, `SPI`, `I2C`), `VID:PID` (`lsusb`), `bcdDevice` revision, kernel, and installed `libfprint` / `fprintd` versions (`python3 scripts/rea-fs.py preflight`).
- Whether the `VID:PID` is already supported in `workspace/libfprint`, listed in the unsupported allowlist (`fprint-list-udev-hwdb.c`), or handled by an external project (`synaTudor`, `python-validity`).
- Whether the sensor performs **Match-on-Chip (MoC)** or **Match-on-Host (MoH)** raw image streaming.

## 2. Static Analysis of the OEM Driver

When no open protocol specification exists, inspect the legitimate OEM driver package matching the exact hardware (`VID:PID` in the `.inf` file; record download URL and SHA-256):
- Use static analysis (`REA` / `Ghidra`) to locate `WinBio` sensor/engine adapter tables, USB command builders, ROM/module lookup tables, and image reconstruction routines without executing untrusted binaries.
- Reconstruct command prerequisites: many sensors reject pairing (`0x4f`) or capture (`0x02`) with vendor status codes (e.g., `0x0401`) unless preceded by a ROM-specific initialization overlay (`0x06`) in the same USB session.

## 3. Host-Sensor Authentication (Pairing vs. Enrollment)

- **Pairing (Channel Authentication):** Binds the host and sensor cryptographically (`host_key.pem`, `host_cert.bin`, `server_ecdh_pub.bin`) so they can establish a mutually authenticated TLS 1.2 session (`0x44` framing, ECDH `prime256v1`, AES-256-CBC + HMAC-SHA256). Pairing is performed once per installation, not per finger.
- **Enrollment (Biometric Template):** Captures multiple views of a user's finger over the authenticated channel and stores the resulting template via `fprintd`.

## 4. Stream Framing, Mode-2 Decoding & Baseline Subtraction

On `06cb:00dc` (module `0x0190:0x00a2`, type `0x2449`):
- Each raw USB frame is $17,024\text{ bytes}$: $112\text{ rows} \times 152\text{ bytes}$ ($8\text{ header bytes} + 144\text{ sample bytes}$). Verifying row indices (`1..112`) and monotonic frame counters prevents interpreting misaligned bulk transfers as images.
- Even/odd row pairs `(0,1), (2,3), ...` are repeated readouts ($2\times$ repeat) and are averaged into $56$ rows.
- Capture mode `2` encodes each 16-sample horizontal group via the Kronecker matrix $M = (2I_4 - J_4) \otimes (2I_4 - J_4)$.
- The empty-sensor baseline must be subtracted **before** clipping/saturation (`tools/encoder_mode2.py` and `syna_image.c`); subtracting after `uint8` clipping loses ~16.6% of dynamic range to saturation.
- Caching the validated empty-sensor baseline in `/var/lib/fprint/syna_00dc/baseline_cache.bin` avoids open-time calibration lag and handles early finger contact (`clear=0` fallback).

## 5. Burst Averaging, Multi-Touch Enrollment & Spectral Matching

- **Temporal burst (6 frames, ~100 ms):** Averaging 6 translation-aligned frames ($\pm 2\text{ px}$) reduces shot noise while avoiding motion blur.
- **Multi-stage enrollment (8 touches):** Captures distinct placements/angles separated by physical finger lift (`rms <= 28.0`). Immediate completion after the 6-frame burst transitions the UI from `HOLD STILL` to `LIFT YOUR FINGER` while the finger is still down.
- **Spectral correlation matcher:** Because a $144 \times 56\text{ px}$ ($7.2 \times 2.8\text{ mm}$) strip yields too few minutiae ($3\text{–}9$) for NBIS `bozorth3` ($\ge 10$ required), `syna_matcher.c` uses Difference-of-Gaussians band-pass filtering and pruned 2D FFT cross-correlation normalized via Summed-Area Tables (SAT) across `-30°..+30°`.

## 6. Desktop Stack & Legal/Upstreaming Boundaries

- **Runtime stack:** Desktop lockscreen / PAM $\to$ `fprintd` (D-Bus) $\to$ `libfprint-2-tod` $\to$ `libsyna_00dc_tod.so` $\to$ `libusb` / `/dev/bus/usb/...`.
- **Interoperability & Clean-Room Code:** Under EU Directive 2009/24/EC (Articles 5(3) and 6), studying a legitimately obtained binary to achieve interoperability is distinct from redistributing proprietary executables. Keep all driver code clean-room, document the provenance of any configuration tables (`docs/provenance.md`), and never publish private pairing keys or biometric captures.
