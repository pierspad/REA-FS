# <img src="docs/rea-fs-icon.svg" alt="REA-FS" height="42" align="absmiddle"> REA-FS

[![GitHub Sponsors](https://img.shields.io/badge/Sponsor-%E2%9D%A4-ea4aaa?logo=github&style=flat)](https://github.com/sponsors/pierspad) [![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-Donate-yellow?logo=buymeacoffee)](https://buymeacoffee.com/pierspad) [![Ko-fi](https://img.shields.io/badge/Ko--fi-Support-ff5e5b?logo=ko-fi)](https://ko-fi.com/pierspad)

**Contents:** [What it does](#what-it-does) · [Get started](#get-started) · [Install the Synaptics 06cb:00dc driver](#install-the-synaptics-06cb00dc-driver) · [Project layout](#project-layout) · [Documentation](#documentation) · [Contributing](#contributing) · [License](#license)

**Make unsupported fingerprint readers work on Linux.**

REA-FS is a toolkit and GUI workbench to reverse-engineer unsupported USB fingerprint sensors with an LLM coding agent and build fast, native C drivers for `libfprint` / `fprintd`. It includes a complete, ready-to-use native C driver (`drivers/syna_tod`) for the **Synaptics `06cb:00dc`** sensor (~240 ms end-to-end verification).

---

## What it does

- **Native GUI for hardware testing (`scripts/open-panel.py`):** Instead of running fragile terminal commands, the agent writes an experiment plan and you test enrollment, verification, and finger management from a native Rust/`egui` window with live touch/lift prompts and one-click notes.
- **Structured workflow for any sensor:** Predefined private directories (`sessions/`, `.rea-fs.local/`) and a step-by-step checklist ([`docs/re-checklist-template.md`](docs/re-checklist-template.md)) keep biometric scans and keys out of Git while guiding the agent from USB discovery to a working C driver.
- **Working Synaptics `06cb:00dc` driver (`drivers/syna_tod`):** Full TLS 1.2 session establishment, Mode-2 frame reconstruction ($144 \times 56\text{ px}$), 8-stage enrollment, 1:N multi-finger `identify`, and a sub-40 ms 2D FFT + Summed-Area Table matcher.

---

## Get started

### 1. Clone the repository

```sh
git clone https://github.com/pierspad/REA-FS.git
cd REA-FS
```

### 2. Open the GUI panel

```sh
python3 scripts/open-panel.py
```

The launcher builds the Rust GUI on first run and opens the fingerprint workbench (*Verifica*, *Registra*, *Impronte*, *Strumenti*).

### 3. Reverse-engineer a new sensor with an AI agent

If you are adapting REA-FS to your own unsupported fingerprint reader, open this repository in your coding agent and send:

> Read `AGENTS.md`, `README.md`, `docs/re-checklist-template.md`, `docs/preferences.md`, `docs/agent-workflow.md`, and `docs/experiment-panel.md`. Run `python3 scripts/rea-fs.py preflight` to identify my sensor (`VID:PID`), follow the state machine in `AGENTS.md`, and use the GUI panel (`python3 scripts/open-panel.py`) whenever a physical touch or hardware test is needed.

---

## Install the Synaptics `06cb:00dc` driver

Build and run the offline test suite first:

```sh
python3 scripts/rea-fs.py check
```

Install the native `libfprint-tod` module, `udev` rule, and local Polkit rule:

```sh
# Dry run:
bash scripts/install-tod-driver.sh --credentials drivers/syna_tod/credentials

# Apply (requires root):
sudo bash scripts/install-tod-driver.sh --credentials drivers/syna_tod/credentials --apply
```

> **Note:** Built-in ROM `6.20` capture tables (`syna_capture_tables.h`) and first-boot host pairing (`0x4f`/`0x50`) are compiled directly into the C driver.

---

## Project layout

| Path | Description |
|---|---|
| `panel/` | Native Rust (`eframe`/`egui`) GUI for enrollment, verification, and agent telemetry |
| `drivers/syna_tod/` | Native C driver and `FpiSsm` state machine for Synaptics `06cb:00dc` |
| `scripts/` | `open-panel.py` (GUI launcher), `rea-fs.py` (preflight & checks), `install-tod-driver.sh` |
| `tools/` | Experiment runner and offline frame analysis utilities |
| `docs/` | Checklists, architecture notes, and `libfprint` upstreaming guide |
| `sessions/` · `.rea-fs.local/` | Git-ignored private directories (`0700`) for local logs and checkpoints |

---

## Documentation

- [Generic Sensor RE Checklist](docs/re-checklist-template.md) — step-by-step template for reverse-engineering a new sensor.
- [Synaptics `06cb:00dc` Checklist](docs/synaptics-06cb-00dc-checklist.md) — completed milestones for the `06cb:00dc` driver.
- [Upstreaming to `libfprint`](docs/libfprint-upstreaming.md) — how the non-blocking `FpiSsm` driver is structured and how to submit a Merge Request to `gitlab.freedesktop.org`.
- [Engineering Notes & Optimizations](docs/engineering-notes.md) — 2D FFT + SAT matching, persistent baseline cache, and 6-frame burst capture.
- [GUI Experiment Panel](docs/experiment-panel.md) — plan schema and event log format.
- [Driver Installation & PAM/Polkit](docs/driver-installation.md) — system setup and troubleshooting.

---

## Contributing

Bug reports and pull requests are welcome. For major changes or new sensor drivers, open an issue first. Never attach biometric captures or private keys. See [CONTRIBUTING.md](CONTRIBUTING.md).

## LLM Disclosure

This project was developed with the assistance of Large Language Models, used to support reverse engineering, code writing, and documentation.

## License

REA-FS is licensed under the [GNU Lesser General Public License v2.1](LICENSE).
