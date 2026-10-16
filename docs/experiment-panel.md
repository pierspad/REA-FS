# Local Experiment Panel

The `egui` experiment panel (`panel/`) provides a safe, structured interface for user-guided hardware tests while the coding agent prepares plans and reads outcomes from disk. It never auto-runs commands on startup or reload.

## Launching

From the repository root:

```sh
python3 scripts/open-panel.py
```

- If `.rea-fs.local/panel/plan.json` is absent, the launcher initializes a safe starter plan (`examples/panel-plan.json`) containing only simulation and offline checks.
- If `panel/target/release/rea-fs-panel` is already up to date, `open-panel.py` launches it directly without invoking Cargo. Building from source requires Rust/Cargo, a C toolchain, and Linux graphics headers (`egui`/`eframe` with `clipboard` support).
- The launcher installs a per-user desktop entry (`~/.local/share/applications/rea-fs-panel.desktop`) so Wayland compositors associate `rea-fs-panel` with `docs/rea-fs-icon.svg`.

## What the User Sees

- Dedicated tabs derived from the active plan: **Verify** (`fprintd-verify`), **Enroll** (`fprintd-enroll` with `{finger}`), **Fingerprints** (`fprintd-list` / `fprintd-delete`), and **Tools** (all other diagnostic, simulation, or offline commands).
- Real-time physical action banners (`DO NOT TOUCH`, `PLACE YOUR FINGER`, `HOLD STILL`, `LIFT YOUR FINGER`) driven strictly by observed device events (`operation_started`, D-Bus `finger-needed`/`finger-present`, `progress`, `retry_hint`).
- Stylized scanner indicator for `fprintd` runs (color-coded by phase; makes no claim of anatomical finger coverage) and a 3-step progress visual for `simulation` runs.
- Latency breakdown after verification (sensor open time, touch-to-result time, total duration), quick feedback chips / custom notes for the agent (`Note for agent`), and relative session log paths (`Session logs`).

## Plan Format (`plan.json`)

Start from `examples/panel-plan.json` or `examples/fprintd-panel-plan.json`. Schema version `1` requires:
- `plan_id` (non-empty string), `revision` (positive integer), `title` (non-empty string), and `experiments` (1..32 entries, file size $\le 256\text{ KiB}$).
- Each experiment requires: unique `id`, `title`, `instructions`, `argv` (string array, no shell evaluation; `sudo`/`su`/`pkexec` are rejected), `cwd` (relative path inside the repository), `timeout_seconds` (`1..3600`), `category`, and `adapter` (`plain`, `simulation`, or `fprintd`).
- Optional fields: `raw_log` (boolean, writes `output.log` up to $4\text{ MiB}$), `parameters` (`["finger"]` when `{finger}` is used in `argv`), and `next_steps` (up to 2000 chars).
- Placeholders substituted in `argv` without shell expansion: `{run_dir}`, `{user}`, and `{finger}`.

## Agent Operational Checklist (`STATE 3` $\to$ `STATE 4`)

### 1. Atomic Write & Validation
Write `.rea-fs.local/panel/plan.json` (`0600`) and run:

```sh
python3 scripts/experiment-panel.py validate --session-dir .rea-fs.local/panel
```

This validates the plan, writes `.rea-fs.local/panel/panel-status.json` (`0600`), and prints the JSON envelope with its SHA-256 `fingerprint`. The GUI polls `plan.json` every second (or when the user clicks **Reload plan.json**) and binds each run to the previewed fingerprint.

### 2. Example Valid `plan.json`

```json
{
  "schema_version": 1,
  "plan_id": "test-fprintd-verify",
  "revision": 1,
  "title": "Hardware Biometric Verification",
  "experiments": [
    {
      "id": "verify-any",
      "title": "Verify fingerprint",
      "instructions": "Keep the sensor clear while opening. When you see PLACE YOUR FINGER, touch the center of the reader and hold still until you see the result.",
      "argv": ["fprintd-verify"],
      "cwd": ".",
      "timeout_seconds": 60,
      "category": "biometric_capture",
      "adapter": "fprintd",
      "raw_log": false,
      "next_steps": "Return to chat and let the agent know you finished: it will read the summary and logs automatically."
    }
  ]
}
```

### 3. Inspecting Logs on Return (`STATE 4`)
When the user replies in chat, read the private session files directly (`0700` dir, `0600` files):
1. `.rea-fs.local/panel/panel-status.json`: confirms `valid: true` and active `revision`.
2. `.rea-fs.local/panel/runs-index.jsonl`: append-only chronological log of every completed, stopped, or failed run (`run_id`, `experiment_id`, `terminal_type`, `exit_code`, `result`, `parameters`, `run_dir`).
3. `.rea-fs.local/panel/latest-run.json`: full summary of the most recent run.
4. Inside `<run_dir>/`:
   - `summary.json`: `started_event`, `last_event`, `user_notes` (notes submitted during or after the run), and `driver_telemetry` (sanitized `journalctl -u fprintd.service` lines for `fprintd` runs).
   - `events.jsonl`: timestamped event stream (`started`, `operation_started`, `device_state`, `progress`, `retry_hint`, `verify_result`, `user_note`, terminal event).
   - `output.log`: raw stdout/stderr if `raw_log: true`.

## Cancellation & `fprintd` Bridge

- Runs execute in a dedicated process group (`start_new_session=True`). On cancellation, timeout, or window exit, the supervisor sends `SIGINT` $\to$ `SIGTERM` $\to$ `SIGKILL` to the process group and marks `cleanup_not_confirmed: true` when forced.
- For `adapter: "fprintd"`, `gdbus` polling of `finger-needed`, `finger-present`, and `num-enroll-stages` starts only *after* `fprintd` prints `Verify started!` or `Enrolling ...` (`operation_started` event), preventing stale D-Bus properties from triggering premature touch prompts during sensor open/calibration.
