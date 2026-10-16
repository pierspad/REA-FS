# Windows DLL Relinking (`synaTudor`) vs. Native C Driver

Reviewed `synaTudor` revision: `31dfdb06107fd1c35c9f9ceae72617e98eccc43a`.

## 1. What Was Observed

- Upstream `synaTudor` targets `06cb:00be` using Lenovo's `r19fp02w.exe` package (`synaFpAdapter104.dll` + `synaWudfBioUsb104.dll`).
- Our hardware is `06cb:00dc` (Acer Aspire A514-54), whose official OEM package (`5.5.2.1124`) ships `synaBscAdapter124.dll` and `synaWudfBioUsb124.dll`.
- Static inspection (`acer-import-audit.json`) found 160 imports in the adapter DLL and 237 in the USB driver DLL, including symbols not currently implemented in `synaTudor`'s WinAPI/WDF wrapper. However, `synaTudor` replaces unresolved imports with aborting stubs at load time, so static import diffs alone do not prove whether `124` would fail at runtime. No dynamic execution of the Acer `124` DLL inside `synaTudor` has been attempted.

## 2. How `synaTudor` Works vs. `synaptics-00dc`

- **`synaTudor` (PE Relinking):** Resolves PE imports/relocations in a sandboxed Linux x86-64 host process, emulates WinAPI/WDF/WinBio (`FxDriverEntryUm`), and delegates image decoding, enrollment, and matching to the proprietary Windows DLL via IPC. Because upstream `libfprint` (`HACKING.md`) rejects proprietary binary drivers, a relinked Windows DLL wrapper can never be merged into upstream `libfprint`.
- **`synaptics-00dc` (Clean-Room Native C):** Implements the USB/TLS 1.2 transport, ROM `6.20` capture initialization, Mode-2 Kronecker frame reconstruction, and 2D FFT + SAT spectral matching directly in C without loading or executing any Windows DLL.

## 3. Strategic Decision

Because our native C driver already authenticates, captures, decodes, enrolls, and matches on `06cb:00dc` with sub-40 ms CPU latency, **all active development focuses on hardening the native C driver (`drivers/syna_tod`) and preparing its port to upstream `libfprint`** (see [libfprint-upstreaming.md](libfprint-upstreaming.md)).

If a bounded `synaTudor` relinking experiment is ever needed for comparative reference:
1. Run PE loading and import resolution in an isolated process without USB access first to identify which stubbed WinAPI/WDF calls are actually reached during `FxDriverEntryUm`.
2. Never grant a relinked OEM binary access to the physical USB sensor without an isolated credential store, as `OnPrepareHardware` can trigger `ProcessPairing` (`0x4f`) and overwrite existing host pairing keys.
