# REA-FS Agent Instructions

Before starting an investigation, read `README.md`, `docs/repositories.md`, `docs/preferences.md`, `docs/agent-workflow.md`, and `docs/experiment-panel.md`. Also read `.rea-fs.local/preferences.json` if present; explicit user instructions in chat take precedence.

## Core Principles

- **Repository layout:** REA-FS owns the GUI panel, Python supervisor, and workflow docs. `workspace/rea` and `workspace/libfprint` are independent git-ignored clones. `drivers/syna_tod` is a pinned submodule of the standalone `synaptics-00dc` driver: initialize with `git submodule update --init --recursive` and publish driver commits before updating the submodule pin.
- **Git hygiene:** Verify repository, status, remotes, and effective Git identity before committing or pushing. Never mix the freedesktop identity into GitHub commits.
- **Evidence discipline:** Resume the local checkpoint before repeating any experiment. Separate verified facts, hypotheses, and untested paths. Never invent hardware observations, successful builds, or test outcomes.
- **Privacy & security:** Never commit or publish private keys, pairing credentials, biometric templates, raw scans, or unredacted session logs. Never push or open PRs/MRs without explicit authorization.
- **Preferences & autonomy:** Follow `docs/preferences.md`. Default mode is `ask`; in `autonomous` mode proceed without re-confirming within authorized categories. If `.rea-fs.local/preferences.json` is unconfigured, ask the 3 setup questions once in chat and save the result locally. Do not treat silence or refusal as consent to full autonomy. Errors require root-cause analysis, not blind retries.
- **Delegation:** Delegate to subagents only when allowed by session preferences and when net savings (including context transfer and verification) are plausible. Cheap model pricing does not automatically equal fewer tokens.

## New User, Sensor, or Community Feedback

Read `docs/getting-started.md`. Recommend cloning with `--recurse-submodules`; if the submodule is missing, check for local changes before initializing it (`python3 scripts/sync-driver.py --check` verifies submodule cleanliness, version alignment, and remote reachability). Do not apply Synaptics-specific opcodes, thresholds, pairing blobs, or Polkit rules to another sensor. When assisting an external driver tester, follow `drivers/syna_tod/docs/testing.md` and record the exact tested revision.

**Share findings upstream (with user permission):** If you and the user confirm that `synaptics-00dc` works on a specific laptop/ROM revision, discover a bug, or implement an improvement in `REA-FS` or `synaptics-00dc`, proactively offer to open a detailed GitHub Issue or Pull Request (including hardware compatibility confirmations containing laptop model, distro/kernel, and ROM/module ID, without personal serials or private credentials/biometrics).

## Execution Rule: Direct Terminal vs. GUI Panel

- **Direct terminal (run by the agent):** Permitted ONLY for passive read-only discovery (`lsusb`, `udevadm`, `python3 scripts/rea-fs.py preflight`), builds (`meson`, `ninja`, `cargo`), offline test suites (`python3 scripts/rea-fs.py check`, `python3 -m unittest`), and static binary analysis (`rea`, `ghidra`).
- **GUI Panel (`.rea-fs.local/panel/plan.json`):** MANDATORY for any test requiring physical contact with the sensor, user waiting, biometric capture, `fprintd` enroll/verify, or hardware observation. Do not write ad-hoc GUIs or daemons. Write and validate `.rea-fs.local/panel/plan.json` (`python3 scripts/experiment-panel.py validate --session-dir .rea-fs.local/panel`), tell the user which experiment to run in `python3 scripts/open-panel.py`, and wait for their reply. On return, inspect `.rea-fs.local/panel/runs-index.jsonl`, `latest-run.json`, `<run_dir>/summary.json`, and `<run_dir>/events.jsonl`.
- **Headless fallback:** Only when both `DISPLAY` and `WAYLAND_DISPLAY` are absent (pure SSH without forwarding), use the bounded time-window terminal protocol in `docs/agent-workflow.md`.

## Mandatory State Machine

Execute one state at a time and record the current state in `sessions/<id>/checkpoint.md` before advancing.

- **`STATE 0 — BOOTSTRAP & PREFERENCES`:** Read required docs and `.rea-fs.local/preferences.json`. Resume `sessions/<id>/checkpoint.md` or create one via `python3 scripts/rea-fs.py init-session`. If preferences are unconfigured, ask the 3 setup questions in `docs/preferences.md` and stop.
- **`STATE 1 — PASSIVE DISCOVERY`:** Run `python3 scripts/rea-fs.py preflight --output <session>/preflight.json`; identify bus, VID:PID, kernel, and installed `fprintd`/`libfprint` packages. Update `checkpoint.md`. No physical sensor contact.
- **`STATE 2 — RESEARCH & HYPOTHESIS`:** Check upstream support in `workspace/libfprint`, issues/patches, and OEM driver packages. Record existing support and gaps in `checkpoint.md`.
- **`STATE 3 — EXPERIMENT PREPARATION`:** Formulate a single experimental question (`docs/templates/investigation.md`). Run offline checks directly in the terminal. For hardware or interactive tests, write and validate `.rea-fs.local/panel/plan.json`, set `checkpoint.md` to `ACTION REQUIRED`, tell the user which experiment to run in the GUI panel, and stop.
- **`STATE 4 — LOG INSPECTION & EVALUATION`:** When the user replies, read `.rea-fs.local/panel/runs-index.jsonl`, `latest-run.json`, `<run_dir>/summary.json` (including `user_notes` and `driver_telemetry`), `<run_dir>/events.jsonl`, and optional `output.log`. Update `checkpoint.md`, separating observed facts from interpretations.
- **`STATE 5 — IMPLEMENTATION & VERIFICATION`:** Implement protocol/decoder/driver changes, run `python3 scripts/rea-fs.py check` offline, then prepare a GUI plan for hardware validation and repeat `STATE 3 -> STATE 4`.
