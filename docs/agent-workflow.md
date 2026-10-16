# Agent Investigation Workflow

## 1. Resume & Preflight (`STATE 0`)

- [ ] Read `docs/preferences.md` and `.rea-fs.local/preferences.json`. Configure missing choices once in chat.
- [ ] Read `AGENTS.md` and clone-specific instructions (`drivers/syna_tod/AGENTS.md`). Preserve any uncommitted local changes.
- [ ] Verify `workspace/rea` and `workspace/libfprint` if needed; check `git config --show-origin` for author identity and remotes before committing or pushing.
- [ ] Resume the relevant session in `sessions/<id>/checkpoint.md`, or initialize a new one with `python3 scripts/rea-fs.py init-session`.
- [ ] Record target sensor, phase, environment, repository revisions, granted permissions, completed runs, blockers, and next step.

## 2. Passive Discovery (`STATE 1`)

- [ ] Run `python3 scripts/rea-fs.py preflight --output <session>/preflight.json` and read-only tools (`lsusb`, `udevadm`, `fprintd-list`) to identify distro, kernel, USB/SPI/I2C bus, `VID:PID`, and installed packages.
- [ ] Request only necessary dependencies within the authorized scope. Redact serial numbers and personal identifiers.

## 3. Research & Hypothesis (`STATE 2`)

- [ ] Check upstream `libfprint` device tables, unsupported-device allowlists, issues, and pending MRs. Note exact revisions.
- [ ] Compare related sensors and legitimately obtained OEM driver packages (record URL and SHA256).
- [ ] Record evidence, confidence, and alternatives for each conclusion. Formulate a single testable experimental question (`docs/templates/investigation.md`) with expected outcome, timeout, and stop criteria.

### Conditional Checks: WBDI / TLS Sensors (e.g. Synaptics `06cb:00dc`)

> [!NOTE]
> Apply only when the target sensor uses a similar USB WBDI / OEM TLS architecture. Do not assume `RomInfo`, TLS handshakes, or $152 \times 112$ frames on unrelated vendors (Goodix, Elan, FocalTech).

- Distinguish upstream `libfprint`, `libfprint-tod`, vendor TOD modules, and `fprintd`.
- Verify `VID:PID` in OEM `.inf` files and inspect `WinBio` engine/sensor exports statically before running any wrapper.
- Reuse a single disassembly/Ghidra session for related queries rather than re-importing the binary on every CLI call.
- Reconstruct prerequisite OEM initialization blobs (`0x06`) by ROM version and module type before testing pairing (`0x4f`) or public material queries (`0x50`).
- Distinguish USB `VID:PID` from internal protocol product IDs (e.g., `0x33`) and sensor module IDs (`0x0190:0x00a2`).
- Treat metadata queries (`0x01`, `0x3e`, `0x75`), initial TLS `ClientHello`, full ECDSA/ECDH authentication, and pairing ownership writes (`0x4f`) as separate stages.
- Audit third-party diagnostic loggers before execution (some reference scripts log private keys at `INFO` level). Store credentials with `0600` permissions in `0700` directories outside tracked paths.

### Conditional Checks: Host-Side Raw Image Sensors

- Synchronize physical touches with an explicit ready state; capture an empty-sensor baseline first.
- Derive row stride, headers, and frame counters from the byte stream (nominal program width can differ from transmitted row bytes).
- Verify pair-averaging parity and sample encoding (`encoder_mode2`) before interpreting apparent ridges or NBIS minutiae counts.
- Subtract the empty-sensor baseline *before* clipping/saturation (`y = clip(...)`).
- Ensure TLS `close_notify` and stream stop (`0x04`) run on early returns and errors; handle residual device state (`0x1a` cleanup or USB reset) before re-initializing.

## 4. Experiment Execution: GUI Panel vs. Terminal (`STATE 3` & `STATE 4`)

- **GUI Panel (`scripts/open-panel.py` — Mandatory for hardware/user interaction):** Whenever `DISPLAY` or `WAYLAND_DISPLAY` is present, any operation requiring physical touch, user waiting, biometric capture, or `fprintd` enroll/verify **must** run through the GUI panel:
  1. Write `.rea-fs.local/panel/plan.json` atomically (`0600`) and validate with `python3 scripts/experiment-panel.py validate --session-dir .rea-fs.local/panel`.
  2. Update `checkpoint.md` to `ACTION REQUIRED`, instruct the user which experiment to run in `python3 scripts/open-panel.py`, and stop.
  3. When the user replies in chat, do **not** ask them to paste output; read directly from disk:
     - `.rea-fs.local/panel/panel-status.json`
     - `.rea-fs.local/panel/runs-index.jsonl`
     - `.rea-fs.local/panel/latest-run.json`
     - `<run_dir>/summary.json` (including `user_notes` and `driver_telemetry`), `<run_dir>/events.jsonl`, and optional `output.log`.
- **Direct Terminal:** Restricted to passive read-only commands, builds (`cargo`, `meson`, `ninja`), offline test suites (`rea-fs.py check`), and static analysis. Never run hardware probes concurrently with the GUI panel.
- **Headless Fallback:** Used only when both `DISPLAY` and `WAYLAND_DISPLAY` are absent. Present the full bounded time-window sequence upfront and analyze the log afterward.

## 5. Implementation, Matcher Evaluation & Upstreaming (`STATE 5`)

- Implement the minimal evidence-backed change. Run `python3 scripts/rea-fs.py check` to verify Python, Rust, and C offline suites.
- Separate enrollment touches, threshold-tuning data, and held-out verification attempts. Multiple frame windows from a single touch are not independent touches.
- Taking the maximum score across $N$ templates shifts the impostor distribution upward: re-verify false-accept margins with non-enrolled fingers whenever $N$ or alignment tolerances change.
- Update `checkpoint.md` at every state transition using this structure:

```text
Target / sensor:
Phase & status (WAIT, ACTION REQUIRED, BLOCKED, COMPLETED):
Environment & repository revisions:
Verified facts & evidence paths:
Hypotheses & confidence:
Completed runs (command/id, timestamp, outcome):
Permissions & exclusions:
Physical action requested (if any):
Next reproducible step:
```
