# Research Summary: Synaptics USB `06cb:00dc`

> [!NOTE]
> Chronological research record from 2026-10-15 on Acer Aspire A514-54 (`bcdDevice 0154`). For current driver architecture and upstream status, see [engineering-notes.md](engineering-notes.md), [synaptics-06cb-00dc-checklist.md](synaptics-06cb-00dc-checklist.md), and [libfprint-upstreaming.md](libfprint-upstreaming.md).

## 1. Hardware Identity & OEM Provenance

- **USB & ROM identity:** `06cb:00dc` (`bcdDevice 1.54`, interface `0xff/0x00/0x00`, bulk endpoints `0x01` OUT / `0x81` IN). Plaintext query `0x01` returns 38 bytes: ROM `6.20` build `154`, internal product `0x33` (`51`), target `1`.
- **Flash & sensor module (`0x3e`, `0x75`, `0x6f`):** Query `0x3e` reports `0` partitions (`blocksize = 0`, JEDEC `ff:ff`). Query `0x75` returns module `0x0190:0x00a2`, mapped in the OEM table (`0x1801394f0`, record 415) to sensor type `0x2449` (`86C TM-P3568-001`, confirmed on-device by factory subtag `8`).
- **OEM driver analyzed statically:** Official Acer package `5.5.2.1124` (`synaWudfBioUsb124.dll`, SHA-256 `33171a48999f2415617d54e9ba44dcb90f0f05599190e948f62fbc780fc2d916`). Analyzed via REA 6.5.0 / Ghidra 12.1.2 without executing the DLL.

## 2. Pairing & OEM TLS 1.2 Handshake

- **Prerequisite pairing overlay (`0x06`):** Ownership command `0x4f` and public-material query `0x50` return vendor error `0x0401` unless preceded in the **same USB claim** by command `0x06` + the ROM `6.20` group-2 pairing initializer blob (record 69 at `0x18015e9c0`, variant 5, 5,796 bytes).
- **Host pairing (`0x4f`) & ECDH verification (`0x50`):** Deterministic `HS_KEY_PAIR_GEN` `TLS-PRF` key generation produces the 453-byte `0x4f` ownership request. Command `0x4f` returns the 184-byte signed host certificate; `0x50` returns a 924-byte payload containing the 400-byte device ECDH public block at offset `524`, signed with ECDSA-SHA256 under the OEM ROM `6.20` root key (`0x18013f0f5`).
- **Session establishment & persistence:** TLS 1.2 handshake over `0x44 00 00 00` bulk records negotiates `0xc005` (`TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA` with OEM HMAC-SHA256 record MACs). Pairing persists across USB resets and full system reboots without re-sending `0x4f`.

## 3. Capture Stream & Mode-2 Frame Reconstruction

- **Capture initialization (`0x1a` / `0x06` / `0x02` / `0x04`):** Over the authenticated TLS channel, cleanup `0x1a` (status `0` or `0x0491`), capture overlay `0x06` (record 67, variant 15, 4,676 bytes, SHA-256 `5a0799c9c90738f3f06c9e9f03db6299bf3096e481c0f17c9735c4a5087cc669`), and capture command `0x02` (1,917 bytes built from the 1,688-byte type `0x2449` program at `0x1801dd2b0` plus Windsor line updates) start raw streaming on `0x82`. Command `0x04` stops streaming before encrypted `close_notify`.
- **Frame geometry ($17,024\text{ B}$):** Autocorrelation and header validation across 1,971 frames confirmed $112\text{ rows} \times 152\text{ bytes}$ ($8\text{ header bytes} + 144\text{ sample bytes}$), row index `1..112`, and monotonic frame counter.
- **Pair-averaging & Mode-2 Kronecker decoder:** Adjacent 0-based row pairs `(0,1), (2,3), ...` are $2\times$ repeated readouts (mean difference `2.1` vs `27.9` for shifted parity), yielding $144 \times 56$ samples. Capture chunk `42` (`02 08 90 ...`) selects encoder mode `2` (`0x1800b2db0` $\to$ `0x1800b50c0` $\to$ `0x1800cae50` $\to$ `0x1800cb580`), which transforms each 16-sample group via $M = (2I_4 - J_4) \otimes (2I_4 - J_4)$:
  $$y = \operatorname{clip}\!\left(\frac{Mx - 4(B - 128)}{2} - 128\right)$$
  Subtracting the empty-sensor baseline $B$ **before** clipping (`tools/encoder_mode2.py` and `syna_image.c`) prevents the 16.6% pixel saturation observed when baseline subtraction was applied post-clip.

## 4. Biometric Discrimination: Why Spectral Correlation Replaced NBIS

- **NBIS limitation on $144 \times 56\text{ px}$:** Reconstructed $144 \times 56$ frames have an isotropic ridge period of $\approx 6.1\text{–}6.5\text{ px}$ ($\approx 333\text{–}508\text{ DPI}$). Because the vertical aperture is only $56\text{ px}$ ($\approx 2.8\text{ mm}$), NBIS `mindtct` extracts only $3\text{–}9$ minutiae (occasionally $10\text{–}11$), falling below or barely at `MIN_COMPUTABLE_BOZORTH_MINUTIAE = 10` (`bozorth3` cross-touch scores were `0..23` vs threshold `40`).
- **Band-pass 2D cross-correlation (`tools/placement_overlap.py` & `syna_matcher.c`):** Using pre-clip Mode-2 reconstruction, empty-baseline subtraction, band-pass filtering (`0.08–0.30` cycles/px), and rotated cross-correlation (`-30°..+30°`):
  - **Genuine same-finger touches (Right Index across Trials 3, 5, 7 pre/post-reboot):** scores **`0.643 – 0.940`** (with exact transitive translation consistency).
  - **Genuine same-finger touches (Left Index, Trial 8):** score **`0.775`** (`0.813` with 6-frame C burst).
  - **Impostor cross-finger touches (Left Index vs. Right Index across Trials 3, 5, 7):** scores **`0.086 – 0.301`**.
  - **Decision threshold `0.50`:** cleanly separates all observed genuine touches (`>= 0.64`) from impostor touches (`<= 0.30`).

## 5. OEM Table Classification

- **Capture & pairing overlays (`0x06`, 4,676 B and 5,796 B):** High entropy (`7.63–7.84 bits/B` after 4-byte header `02 00 00 01`), representing compressed or encrypted sensor microcode/configuration.
- **Module capture program (`0x02`, 1,688 B, entropy `4.86 bits/B`) & calibration ramp (56 B):** Structured register/sequence tables embedded in `drivers/syna_tod/src/syna_capture_tables.h`. Provenance and upstream redistribution acceptability require explicit review with `libfprint` maintainers (`docs/provenance.md`).
