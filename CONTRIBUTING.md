# Contributing

Search existing issues before opening a new one. For major changes or new sensor drivers, open an issue first to discuss the device USB ID (`VID:PID`) and protocol scope.

## Issues

Include your exact sensor `VID:PID` (`lsusb`), Linux distribution, kernel version, and `libfprint` / `fprintd` versions.

**Privacy & Security:** Never attach biometric scans (`*.bin`, `*.raw`, `*.pgm`, `*.png` of real fingerprints), enrolled `fprintd` templates, or private pairing keys (`*.pem`, `host_cert.bin`, `server_ecdh_pub.bin`) to issues or pull requests.

## Pull Requests

Keep changes focused. Explain the problem, the hardware tested (if applicable), and the offline checks you ran. Add unit tests for protocol parsers, frame decoders, or matcher changes, and update affected documentation.

## Development & Verification

Run the full offline test suite (Python, Rust GUI panel, and C driver unit tests) from the repository root:

```sh
python3 scripts/rea-fs.py check
cargo test --manifest-path panel/Cargo.toml
```

To validate the GUI experiment plan without touching hardware:

```sh
python3 scripts/experiment-panel.py validate --session-dir .rea-fs.local/panel
```

For matcher or signal-processing changes, record before/after execution times (`meson test -C drivers/syna_tod/build -v matcher_tests`) and verify that genuine vs. impostor score separation does not regress.
