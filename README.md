# REA-FS — Agent-Assisted Fingerprint Sensor Reverse Engineering

**REA-FS** is a reusable, privacy-first workbench and blueprint for reverse-engineering unsupported fingerprint sensors on Linux with an AI coding agent, turning raw hardware captures into fast, native C drivers (`libfprint-tod` and upstream `libfprint`).

This repository serves **two core purposes**:

1. **A Reusable Reverse-Engineering Blueprint & Toolkit:** Anyone with an unsupported fingerprint reader can clone this repository, point an LLM agent at it, and follow a structured state machine with predefined private paths (`sessions/<sensor-id>/`, `.rea-fs.local/`), reusable signal-processing/matching patterns, and a step-by-step checklist ([`docs/re-checklist-template.md`](docs/re-checklist-template.md)).
2. **A GUI-First Human–Agent Hardware Loop:** Instead of asking the user to run fragile terminal commands or paste long logs into chat, the LLM prepares a validated JSON experiment plan (`.rea-fs.local/panel/plan.json`) and invites the user to click a button in the lightweight native Rust/egui GUI (`python3 scripts/open-panel.py`). The user gets real-time visual feedback (*Place Finger* $\to$ *Hold Still* $\to$ *Lift Finger*) and can write observations directly in the GUI, while the agent reads the structured telemetry (`latest-run.json`, `summary.json`, `events.jsonl`) automatically.

It also includes our complete reference implementation for the **Synaptics WBDI `06cb:00dc`** ($144 \times 56$ pixels, TLS 1.2 session, sub-40 ms 2D FFT + Summed-Area Table matcher in native C). See [`docs/synaptics-06cb-00dc-checklist.md`](docs/synaptics-06cb-00dc-checklist.md) for current driver status and the `libfprint` upstreaming roadmap.

---

## Quick Start for New Users (Cloning the Repo for Your Own Sensor)

### 1. Clone the repository

```sh
git clone https://github.com/pierspad/REA-FS.git
cd REA-FS
```

### 2. Paste this bootstrap prompt into your AI coding agent

Open the folder with your agent (Claude Code, Cursor, Antigravity, Codex, etc.) and send:

> Read in order: `AGENTS.md`, `README.md`, `docs/re-checklist-template.md`, `docs/preferences.md`, `docs/agent-workflow.md`, `docs/experiment-panel.md`, and `docs/repositories.md`. Read `.rea-fs.local/preferences.json` if present and resume the relevant checkpoint in `sessions/` (or create a new one with `python3 scripts/rea-fs.py init-session` and copy `docs/re-checklist-template.md` into the session folder).
> My goal is to reverse-engineer the fingerprint sensor on this computer and make it work natively on Linux.
> Strictly follow the State Machine in `AGENTS.md` one step at a time:
> 1. Identify my actual hardware, bus, VID:PID, and existing upstream support first (`python3 scripts/rea-fs.py preflight`) — do not assume my sensor is the Synaptics `06cb:00dc` reference driver.
> 2. If `.rea-fs.local/preferences.json` is missing, ask me the 3 preference questions once, save them locally, and proceed without re-prompting inside authorized categories.
> 3. Use the direct terminal ONLY for read-only discovery, builds (`meson`, `ninja`, `cargo`), and offline unit tests (`python3 scripts/rea-fs.py check`).
> 4. For ANY hardware test, finger touch, enrollment, verification, or interactive observation, ALWAYS use the GUI Panel: write and validate `.rea-fs.local/panel/plan.json`, tell me which experiment to click in `python3 scripts/open-panel.py`, and wait for my reply.
> Never commit or upload biometric scans, private pairing keys, or OEM binaries.

### 3. Launch the GUI Workbench when invited by the agent

```sh
python3 scripts/open-panel.py
```

- **Minimal, portable dependencies:** The GUI is a single lightweight native Rust binary (`panel/`, built with `eframe`/`egui`) orchestrated by zero-dependency Python 3 standard library scripts (`scripts/`). It compiles once and runs smoothly on any Linux desktop (X11/Wayland).
- **Zero copy-pasting:** Select the experiment requested by the agent, follow the on-screen sensor prompts, optionally type notes into the **Osservazioni** box, and reply to the agent in chat. The agent automatically reads `.rea-fs.local/panel/latest-run.json`.

---

## Predefined Directory Layout & Privacy Boundaries

To keep reverse-engineering sessions organized and prevent accidental leaks of sensitive biometric data or cryptographic keys, REA-FS enforces a strict separation between tracked code and private local directories:

```text
REA-FS/
├── AGENTS.md                       # Mandatory agent state machine & GUI-first rules
├── README.md                       # This guide
├── docs/
│   ├── re-checklist-template.md    # Generic step-by-step checklist for ANY new sensor
│   ├── synaptics-06cb-00dc-checklist.md # Status & libfprint upstream checklist for 06cb:00dc
│   ├── agent-workflow.md           # Detailed agent operating procedures
│   ├── experiment-panel.md         # GUI panel JSON schema & telemetry specification
│   └── preferences.md              # User autonomy & privacy configuration spec
├── panel/                          # Lightweight native Rust/egui GUI workbench
├── scripts/
│   ├── open-panel.py               # Compiles (if needed) and opens the GUI panel
│   ├── experiment-panel.py         # Atomic plan validation & CLI supervisor
│   ├── rea-fs.py                   # Session init, passive preflight, and offline test runner
│   └── install-tod-driver.sh       # Installs TOD plugin, udev rule, and Polkit rule
├── drivers/
│   └── syna_tod/                   # Native C driver for Synaptics 06cb:00dc (TLS 1.2 + FFT/SAT matcher)
├── tools/                          # Offline Python frame decoders & analysis utilities
├── tests/                          # Offline unit tests for scripts, panel, and decoders
│
├── .rea-fs.local/                  # [GIT-IGNORED] Local agent state & GUI telemetry
│   ├── preferences.json            # User autonomy & privacy settings
│   └── panel/
│       ├── plan.json               # Active GUI experiment plan written by the agent
│       ├── latest-run.json         # Pointer to the latest completed GUI experiment
│       └── runs-index.jsonl        # Append-only log of GUI runs
├── sessions/                       # [GIT-IGNORED] Private per-sensor RE sessions (0700)
│   └── <YYYY-MM-DD-vendor-vid-pid>/
│       ├── checkpoint.md           # Live state machine checkpoint
│       ├── checklist.md            # Copy of docs/re-checklist-template.md
│       └── preflight.json          # Local hardware/system discovery output
└── workspace/                      # [GIT-IGNORED] Independent reference clones
    ├── libfprint/                  # Upstream libfprint / libfprint-tod target tree
    └── rea/                        # Optional reverse-engineering static analysis toolkit
```

> **Privacy Guarantee:** `.gitignore` explicitly blocks `/sessions/`, `/.rea-fs.local/`, `/workspace/`, `/drivers/syna_tod/credentials/`, and all raw biometric/key extensions (`*.pem`, `*.bin`, `*.raw`, `*.pgm`, `*.ppm`, `*.npy`). Temporary raw finger dumps captured during signal-processing research should be deleted as soon as offline metrics are verified.

---

## Practical Reverse-Engineering Insights & Best Practices

When adapting REA-FS to a new fingerprint sensor, keep these hard-won lessons in mind:

### 1. Small-Area Sensors ($144 \times 56$ px) Need Spectral Correlation, Not Minutiae
- Standard `libfprint` uses NBIS (`mindtct` + `bozorth3`) to extract ridge endings and bifurcations. This works well on large square sensors ($192 \times 192$ or $256 \times 256$), but **fails on narrow rectangular sensors** (such as $144 \times 56$ pixels, $7.2 \times 2.8\text{ mm}$ at 508 DPI) because a $2.8\text{ mm}$ vertical strip contains too few minutiae (often $< 5$) and high boundary truncation.
- **Solution:** Use **band-pass spatial filtering** (Difference-of-Gaussians tuned to the 6–9 px ridge wavelength) followed by **rotated 2D FFT cross-correlation normalized via Summed-Area Tables (SAT)**.

### 2. How to Match in $< 40\text{ ms}$ on CPU (Without GPU Overhead)
- **Why not GPU (`wgpu` / OpenGL / Vulkan)?** `fprintd` runs as a tightly sandboxed `root` systemd service (`ProtectSystem=strict`, no X11/Wayland display socket). Initializing a GPU compute context inside `fprintd` adds **80–250 ms** of driver init latency and breaks systemd sandboxing.
- **Pruned CPU 2D FFT + SAT is faster and zero-dependency:**
  - Precompute the probe's 2D FFT and SAT once per touch (**0.50 ms**).
  - Prune dead zero-padded rows (`56..127`) during forward horizontal 1D FFTs and prune unreachable shift rows during inverse 1D IFFTs.
  - Fuse the vertical 1D FFT, complex conjugate multiplication, and vertical 1D IFFT column-by-column inside L1 cache.
  - Use a `2°` coarse rotation grid (`-30°..+30°`) with full `(1, 1)` pixel shift resolution (avoiding `2x` shift decimation, which can miss odd-pixel ridge peaks of wavelength $\sim 7\text{ px}$), then refine $\pm 1^\circ$ around the top candidates.
  - **Result:** **37.4 ms** per template on a single CPU thread, with early-exit (`score >= 0.72`) completing verification in **~38 ms**.

### 3. Eliminate Open-Time Calibration Latency (`baseline_cache.bin`)
- Acquiring 10–20 empty-sensor frames every time `fprintd` opens the device adds ~0.5–1.0 s of lag and causes `clear=0` failures if the user places their finger on the sensor immediately (e.g. KDE lock screen).
- **Solution:** Persist the validated empty-sensor baseline to `/var/lib/fprint/<driver>/baseline_cache.bin` (`0600`). On `verify`/`identify`, load the cached baseline in **0 ms** and only refresh it if the first 3 frames are genuinely empty (`spatial_stddev < 45.0`).

### 4. Short Burst Capture (6 Frames $\approx 100\text{ ms}$) & Instant UI Feedback
- Waiting for 20–25 contact frames (~0.5 s) introduces motion blur and makes enrollment feel sluggish. Averaging **6 aligned frames** ($\pm 2\text{ px}$ translation search) preserves **$>96.8\%$ correlation** with a 20-frame window while cutting capture time to ~100 ms.
- **Enrollment UX:** Return the captured sample from `syna_capture_finger_sample` **immediately** after the 6 contact frames are collected — *before* waiting for the user to lift their finger. That way `fprintd` emits `enroll-stage-passed` while the finger is still down, and the GUI immediately switches from **"TIENI FERMO"** (*Hold Still*) to **"SOLLEVA IL DITO"** (*Lift Finger*). Wait for physical finger release (`rms <= 28.0`) at the beginning of the *next* stage.

### 5. Multi-Finger Recognition (`identify` 1:N) & Passwordless Polkit
- **`dev_class->identify` is mandatory for multi-finger support:** When multiple fingers are enrolled, `fprintd-verify` and PAM call `identify` (1:N). If a TOD driver only implements `dev_class->verify` (1:1), `fprintd` only checks the first enrolled finger (`#0`) and rejects all other enrolled fingers! Always implement both `verify` and `identify`.
- **Why Polkit asks for `sudo`/password on every finger registration:** `fprintd` defaults to `auth_self_keep`, which caches authorization per D-Bus client connection (`PolkitSystemBusName`). Because `fprintd-enroll` is a short-lived CLI process that exits after enrolling one finger, its D-Bus connection closes and invalidates `auth_self_keep`. Installing `/etc/polkit-1/rules.d/50-syna-00dc-fprintd.rules` (automated by `scripts/install-tod-driver.sh`) grants passwordless local enrollment/verification/deletion to active desktop sessions (`subject.local && subject.active`).

---

## Synaptics `06cb:00dc` Native C Driver Usage

### Run offline tests & matcher benchmark (no hardware required)

```sh
python3 scripts/rea-fs.py check
```

This runs:
1. All Python unit tests (`tests/` — workflow CLI, GUI panel supervisor, Mode 2 decoder, credential export).
2. Rust `cargo test` for the native GUI (`panel/`).
3. Meson/Ninja build and C test suites (`drivers/syna_tod/build/`):
   - `test_pipeline`: Frame validation, Mode 2 decoding, band-pass filter, template serialization, and persistent baseline cache (`empty sensor` vs `finger already present`).
   - `test_matcher`: Exact timing benchmark (**0.50 ms** probe setup, **37.39 ms** single template, **37.91 ms** early-exit verify).
   - `test_burst_frames`: 6-frame vs 20-frame burst SNR correlation check ($>96.8\%$).
   - `test_real_scores`: Genuine vs. impostor score separation check.

### Install the `06cb:00dc` TOD driver, udev rule, and Polkit rule

Once you have generated or placed your sensor's pairing credentials in `drivers/syna_tod/credentials/`:

```sh
# Dry-run inspection first:
bash scripts/install-tod-driver.sh --credentials drivers/syna_tod/credentials

# Apply installation (backs up previous files under /var/lib/fprint/syna_00dc-backup.XXXXXX):
sudo bash scripts/install-tod-driver.sh --credentials drivers/syna_tod/credentials --apply
```

---

## Documentation Index

- **Checklists & Roadmaps**
  - [Generic Sensor Reverse-Engineering Checklist](docs/re-checklist-template.md)
  - [Synaptics `06cb:00dc` Checklist & `libfprint` Upstream Roadmap](docs/synaptics-06cb-00dc-checklist.md)
- **Workflow & GUI Panel**
  - [Agent Workflow & State Machine](docs/agent-workflow.md)
  - [GUI Experiment Panel Specification](docs/experiment-panel.md)
  - [Preferences & Autonomy Policy](docs/preferences.md)
  - [Repositories & Git Identity Hygiene](docs/repositories.md)
- **Technical Deep Dives (`06cb:00dc`)**
  - [How the Full Stack Works (USB, TLS, Mode 2, Matcher)](docs/how-it-works.md)
  - [Image Quality, Calibration & Multi-Stage Enrollment](docs/image-quality-and-enrollment.md)
  - [Driver Installation & Operations Guide](docs/driver-installation.md)
  - [Relinking vs. Native `libfprint` Upstreaming Analysis](docs/relinking-and-upstream.md)
  - [Synaptics `06cb:00dc` Protocol Research Log](docs/research-06cb-00dc.md)
