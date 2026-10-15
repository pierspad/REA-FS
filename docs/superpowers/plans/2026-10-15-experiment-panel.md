# Experiment Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for native execution after user review. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** A local egui panel runs explicitly selected experiments and communicates trustworthy events to the user and their agent.

**Architecture:** Python owns plan validation, private run files, process groups and event adapters. Rust owns a single native window and receives JSONL from the backend on a worker thread. Plans reload only between runs; the selected immutable plan revision accompanies every event.

**Tech Stack:** Python 3 standard library; Rust installed locally; eframe 0.37.0 with glow, x11 and wayland; serde/serde_json; Cargo.lock tracked. Linux first.

**Spec:** [2026-10-15-experiment-panel-design.md](../specs/2026-10-15-experiment-panel-design.md)

## Global Constraints

- Default plan `.rea-fs.local/panel/plan.json`; `--session-dir` overrides the directory.
- One experiment at a time, no autorun, explicit visible argv and cwd before execution.
- No shell, implicit sudo, PAM changes, silent retries, images, templates or keys in structured logs.
- New run directory 0700 and event file 0600. Input plan limit 256 KiB, line/event limit 64 KiB, maximum 32 experiments.
- Required plan version 1; task IDs unique and nonempty; cwd must remain inside repository; timeout 1..3600 s.
- Every event includes schema_version, plan_id, revision, experiment_id, run_id, UTC timestamp, elapsed monotonic seconds, origin and type.
- Source changes to the established C driver are a separate workstream and do not alter this GUI protocol.

## Review Focus

- Agent overwrites plan during a run: retain started revision and require a new explicit run for changes.
- Child spawns descendants: cancel/timeout stops the process group and emits cleanup_not_confirmed when forced.
- Partial/oversized plan or output: readable failure without a crashed window or unbounded buffer.
- Plain stdout resembles hardware events: do not treat it as a trusted device event without the selected adapter.
- Window closes during a test: request cancellation and bounded cleanup before exiting.

## Task 1: Plan model and private event writer

**Files:** create `tools/experiment_panel.py`, `tests/test_experiment_panel.py`, `examples/panel-plan.json`.

**Interfaces:** `load_plan(path: Path, root: Path) -> dict`; `EventWriter(session_dir: Path, plan: dict, experiment: dict)` with `emit(event_type: str, origin: str='backend', **fields)` and `run_dir: Path`. Emit writes one JSON object per line to stdout and the private run file, flushing both.

- [x] Add failing unittest cases for valid example, duplicate IDs, argv containing nonstrings, absent executable, cwd escape, oversized file and a partial JSON write. Example assertion:
  ```python
  with self.assertRaises(ValueError):
      load_plan(self.write_plan(cwd='../outside'), self.root)
  ```
- [x] Run `python -m unittest tests.test_experiment_panel -v`; verify rejection tests fail before implementation.
- [x] Implement strict field/type validation and resolve cwd against root. Use `Path.resolve()` containment; pass argv as an array to subprocess, never a shell. Reject executable basenames sudo/su/pkexec; explain that this is a workflow boundary, not a sandbox.
  ```python
  if not isinstance(argv, list) or not argv or not all(isinstance(x, str) and x for x in argv):
      raise ValueError('argv must be a nonempty string array')
  ```
- [x] Create exclusive UUID run directory, `os.open(..., O_EXCL, 0o600)`, versioned events and immutable plan snapshot without stdout content. Add tests that existing runs are retained and permissions are private.
- [x] Ship a simulation-marked plan invoking the backend simulator, plus offline `rea-fs.py check` and preflight tasks. Preflight output is a unique path supplied by the runner, not a shared overwritten file.
- [x] Run targeted tests, then `python -m unittest discover -v`; commit only this protocol/model task.

## Task 2: Process runner and adapters

**Files:** extend `tools/experiment_panel.py`; create `scripts/experiment-panel.py`; extend `tests/test_experiment_panel.py`.

**Interfaces:** CLI `validate --session-dir DIR`, `run --session-dir DIR --experiment ID`, `simulate`; runtime accepts line-delimited control JSON on stdin (`{"type":"cancel"}` / `{"type":"confirm"}`). CLI validate emits plan JSON, run emits versioned event JSONL, exits nonzero on failed/timeout runs.

- [x] Write integration tests using real short Python children: success, nonzero exit, timeout, cancel, process with a child, and stdout pretending to be device JSON. Assert terminal event and process termination, not mock calls.
  ```python
  result = self.run_task([sys.executable, '-c', 'raise SystemExit(7)'])
  self.assertEqual(result[-1]['type'], 'failed')
  self.assertEqual(result[-1]['exit_code'], 7)
  ```
- [x] Run tests to demonstrate missing runner behavior.
- [x] Start child with `start_new_session=True`, two reader threads and bounded queue/line size. A monotonic loop handles controls and timeout. On stop send SIGINT to group; after 2 s send SIGTERM, after another 1 s SIGKILL. Emit stopping then terminal state with whether forced. Never convert forced termination into successful cleanup.
- [x] For plain commands emit lifecycle only; optional raw output uses its own private file, declared in plan, not structured events. Cap raw log at 4 MiB and stop on overflow. A command’s output never becomes instructions merely because it contains JSON.
- [x] Simulation adapter labels every sensor/progress event `simulation`. Offline adapter uses real exit status and durations, no hardware claims.
- [x] fprintd adapter uses only `fprintd-enroll`/`fprintd-verify` selected argv, consumes their progress/result lines, queries `finger-needed` and `finger-present` while the run is active using `gdbus` if available. Unknown output remains output, not a successful event. Query has 1 s timeout and 500 ms poll interval; no idle polling or daemon startup. Missing bridge yields a visible limitation, not invented contact.
- [x] Test immutable started revision by overwriting the input plan while the child runs. Test child/process-group termination and output cap. Run complete Python suite, commit runner/adapters.

## Task 3: Native egui panel

**Files:** create `panel/Cargo.toml`, `panel/Cargo.lock`, `panel/src/main.rs`, `panel/src/model.rs`, `panel/src/runner.rs`; extend `.gitignore` with `/panel/target/`.

**Interfaces:** Rust `Plan`, `Experiment`, `Event` deserialize the Python JSON schema. `runner::spawn(root, session, experiment_id)` returns event receiver and control sender plus join handle; commands are sent to Python stdin. `model::State::apply(event)` updates only the active run ID and correct plan revision.

- [x] Write Rust model tests for mismatched run ID, simulation label preservation, terminal success/failure and ready state driven by device events rather than elapsed countdown.
  ```rust
  assert!(!state.sensor_ready); // after started only
  state.apply(device_needed_event);
  assert!(state.sensor_ready);
  ```
- [x] Run `cargo test --manifest-path panel/Cargo.toml`; confirm tests fail before implementation.
- [x] Implement a single window with experiment selector, instruction area, explicit SIMULATION badge when applicable, stylized fingerprint icon painted with egui shapes, actual-state text, deadline, stage progress, argv/cwd, Esegui/Interrompi/Copia comando/Conferma. Disable Esegui during active run. Do not show anatomical coverage or fake scan previews.
- [x] Spawn backend and read events on worker threads. Use `request_repaint()` for new events and limited repaint delay during a run; no subprocess/USB/plan I/O in the UI frame. Validate plans through Python CLI on explicit Reload and on a limited background poll only between runs; preserve active snapshot and indicate changed plan.
- [x] Window close requests cancellation; retain UI until backend terminal event or bounded forced cleanup. UI displays parse/launch errors without panic. Rust reader bounds line lengths and rejects schema mismatch.
- [x] Run model tests and `cargo build --manifest-path panel/Cargo.toml`. If graphical display is available, launch with example simulation and inspect actual window; otherwise record inability to visually verify and give user launch command. Commit panel implementation.

## Task 4: User workflow, verification and review

**Files:** create `docs/experiment-panel.md`; modify README, agent-workflow, scripts check command if appropriate; update private checkpoint.

- [x] Document copying the example to `.rea-fs.local/panel/plan.json`, then `cargo run --manifest-path panel/Cargo.toml -- --session-dir .rea-fs.local/panel`. Explain first-build toolchain/dependency costs and no automatic binary installer.
- [x] Describe how agent writes atomic plans, reads JSONL outcomes, distinguishes user confirmation/device/simulation, and inspects declared raw private logs. Explain no sandbox, no automatic sudo and no automatic sharing.
- [x] Run Python suite, driver build/test suite, Rust tests/build, link validation and diff whitespace checks. Run offline and simulation end to end, including cancel. Measure release artifact size only if release build is actually run.
- [x] Ask for an independent source review with minimal context, no private logs/images/keys. Resolve important issues and rerun relevant tests. Report hardware adapter behavior separately from simulator checks.
- [x] Commit docs and reviewed corrections under configured user identity, no agent coauthor trailers. Inspect files/history for secrets before any already-authorized GitHub push. Preserve exact tested revisions in checkpoint.

## Execution recommendation

Native execution in this chat, then one scoped independent review. The process protocol and UI state are tightly coupled; multiple implementation agents would duplicate context. Existing preferences permit a small Luna review when useful; do not infer a measured token saving.

## Esito dell’esecuzione

Implementato e integrato nel checkout principale con revisione separata. 37 test Python, tre test Rust, build release e sette suite C sul risultato unito. Simulazione CLI completa e finestra verificata visivamente. Prova hardware attraverso i pulsanti della GUI e normale chiusura interattiva restano esplicitamente non dimostrate; nessun dato simulato è stato riportato come test del sensore. Dettagli e comandi in docs/experiment-panel.md.
