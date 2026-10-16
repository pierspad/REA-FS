# Design Note: Linux Driver for Synaptics `06cb:00dc`

Status: Architectural design implemented in the standalone [synaptics-00dc](https://github.com/pierspad/synaptics-00dc) C driver (`drivers/syna_tod` submodule). See [audit-2026-10-15.md](audit-2026-10-15.md) and [libfprint-upstreaming.md](libfprint-upstreaming.md) for current upstream gates.

## Hardware & Protocol Constraints

- **Host-side raw image sensor:** Unlike `06cb:00df` (match-on-chip supported by upstream `synaptics.c`), `06cb:00dc` (`product == 0x33`, module `0x0190:0x00a2` / type `0x2449`) streams encoded $152 \times 112$ raw frames over an OEM TLS 1.2 session (`0x44` bulk framing) and requires host-side decoding and matching.
- **Small active area ($144 \times 56\text{ px}$):** After pair-averaging adjacent rows and decoding Mode-2 16-sample Kronecker blocks, the reconstructed image is $144 \times 56$ pixels ($\approx 7.2 \times 2.8\text{ mm}$). Standard NBIS `mindtct`/`bozorth3` struggles to extract $\ge 10$ minutiae from a $2.8\text{ mm}$ strip.
- **Host-stored credentials & templates:** Flash partition query (`0x3e`) reports `0` partitions (`blocksize = 0`). Both pairing credentials (`host_key.pem`, `host_cert.bin`, `server_ecdh_pub.bin`) and enrolled biometric templates are stored on the host.

## Architecture Choice

| Option | Pros | Cons |
| --- | --- | --- |
| **C `libfprint-tod` module (`synaptics-00dc`)** | Native C, integrates with standard `fprintd`/PAM/desktop, direct path to upstream `libfprint` | Requires `libfprint-tod` package until upstreamed into native `libfprint` |
| **`python-validity` + `open-fprintd`** | Reuses Python TLS/Validity helpers | Built for match-on-chip sensors; requires replacing system `fprintd` |
| **Standalone daemon** | No `libfprint` constraints | Reinvents `fprintd` D-Bus/PAM integration |

## Modular C Driver Components (`drivers/syna_tod/src/`)

1. **`syna_usb.c` / `syna_proto.h`:** Pre-init hardware verification (`0x01` ROM version, `0x75` module ID), bulk framing, monotonic deadlines, and clean interface release.
2. **`syna_tls.c`:** ECDH (`prime256v1`) / ECDSA handshake, `TLS-PRF` key derivation, AES-256-CBC + HMAC-SHA256 record encryption/authentication, `close_notify`, and deterministic `HS_KEY_PAIR_GEN` host pairing (`0x4f` / `0x50`).
3. **`syna_oem.c` / `syna_capture_tables.h`:** Built-in ROM `6.20` capture initializer (`0x06`, 4,676 B) and capture request (`0x02`, 1,917 B), plus strict validation of private credentials and baseline cache in `/var/lib/fprint/syna_00dc`.
4. **`syna_image.c`:** $152 \times 112$ frame header/counter validation, `(0,1),(2,3)` row pair-averaging, pre-clipping baseline subtraction, Mode-2 Kronecker $16 \times 16$ decoding, and 6-frame translation-aligned burst averaging.
5. **`syna_matcher.c`:** Difference-of-Gaussians band-pass filtering + pruned 2D radix-2 FFT cross-correlation normalized via Summed-Area Tables (SAT) and precomputed rotation LUTs (`-30°..+30°`).
6. **`syna_device.c` / `syna_ssm.c`:** `FpDevice` lifecycle, 8-stage enrollment, 1:1 `verify` and 1:N `identify`, persistent baseline cache, and experimental `FpiSsm` state machines.
