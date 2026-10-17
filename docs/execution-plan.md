# Execution Plan: Semantic Release, Submodule Sync Guard, GUI Zone Guidance & Async SSM Orchestration

This plan is designed for deterministic, step-by-step execution by an agentic coding model. Follow all rules in `AGENTS.md`:
- **Never run hardware-opening commands** (`setup.sh`, `fprintd-enroll`, `fprintd-verify`, `test_fprint_open`) from the terminal.
- **Git identity:** Verify `user.name = pierspad` and `user.email = pierpaolospadafora23+github@gmail.com` on both GitHub repos (`synaptics-00dc` and `REA-FS`), and `pierpaolospadafora23+gitlab@gmail.com` in `workspace/libfprint`. Never add `Co-authored-by` trailers.
- **Submodule rule:** Always commit and push `/home/ribben/Desktop/pierspad/github/synaptics-00dc` to `origin/main` **before** updating `drivers/syna_tod` in `/home/ribben/Desktop/pierspad/github/REA-FS`.

---

## Task 1: Configure Vesta-style `semantic-release` in `synaptics-00dc` and `REA-FS`

### 1.1 `synaptics-00dc` (`/home/ribben/Desktop/pierspad/github/synaptics-00dc`)
1. Create `/home/ribben/Desktop/pierspad/github/synaptics-00dc/.releaserc` matching the Vesta convention:
   ```json
   {
     "branches": ["main"],
     "plugins": [
       [
         "@semantic-release/commit-analyzer",
         {
           "preset": "conventionalcommits",
           "releaseRules": [
             { "type": "fix", "release": "patch" },
             { "type": "feat", "release": "minor" },
             { "type": "bump", "release": "patch" },
             { "type": "perf", "release": "patch" },
             { "type": "build", "scope": "Needs bump", "release": "patch" }
           ]
         }
       ],
       [
         "@semantic-release/release-notes-generator",
         {
           "preset": "conventionalcommits",
           "presetConfig": {
             "types": [
               { "type": "fix", "section": "🐛 Bug Fixes", "hidden": false },
               { "type": "feat", "section": "✨ New Features", "hidden": false },
               { "type": "bump", "section": "🚀 Updated Hardware Support", "hidden": false },
               { "type": "perf", "section": "🔧 Improvements", "hidden": false },
               { "type": "build", "hidden": true }
             ]
           }
         }
       ],
       [
         "@semantic-release/exec",
         {
           "prepareCmd": "sed -i \"s/version: '[0-9.]*'/version: '${nextRelease.version}'/\" meson.build && sed -i \"s/^VERSION=v[0-9.]*/VERSION=v${nextRelease.version}/\" bootstrap.sh && sed -i \"s/installs v[0-9.]*)/installs v${nextRelease.version})/\" bootstrap.sh && sed -i \"s|/v[0-9.]*/bootstrap.sh|/v${nextRelease.version}/bootstrap.sh|g\" README.md"
         }
       ],
       [
         "@semantic-release/git",
         {
           "assets": ["meson.build", "bootstrap.sh", "README.md"],
           "message": "chore: Release v${nextRelease.version} [skip ci]\n\n${nextRelease.notes}"
         }
       ],
       [
         "@semantic-release/github",
         {
           "successComment": false
         }
       ]
     ]
   }
   ```
2. Create `/home/ribben/Desktop/pierspad/github/synaptics-00dc/.github/workflows/release.yml`:
   - Trigger on `push` to `main` (ignoring commit messages containing `[skip ci]`) and `workflow_dispatch`.
   - Permissions: `contents: write`, `actions: read`, `checks: read`.
   - Steps:
     - `actions/checkout@v4` with `fetch-depth: 0`
     - Run offline C & Python checks (`sudo apt-get update && sudo apt-get install -y meson ninja-build pkg-config libssl-dev libgusb-dev libusb-1.0-0-dev libfprint-2-tod-dev && meson setup build && ninja -C build && meson test -C build --print-errorlogs && python3 -m unittest discover -s tests -p 'test_*.py' && bash -n install.sh bootstrap.sh setup.sh`)
     - Run `cycjimmy/semantic-release-action@v5` with `extra_plugins`: `@semantic-release/exec`, `@semantic-release/git`, `conventional-changelog-conventionalcommits@9.3.1` and `GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}`.

### 1.2 `REA-FS` (`/home/ribben/Desktop/pierspad/github/REA-FS`)
1. Create `/home/ribben/Desktop/pierspad/github/REA-FS/.releaserc`:
   - Same Vesta `@semantic-release/commit-analyzer` and `@semantic-release/release-notes-generator` sections (`🐛 Bug Fixes`, `✨ New Features`, `🚀 Updated App Support`, `🔧 Improvements`).
   - `@semantic-release/exec` with:
     ```json
     "prepareCmd": "sed -i '0,/^version = \".*\"/s//version = \"${nextRelease.version}\"/' panel/Cargo.toml && cargo check --manifest-path panel/Cargo.toml"
     ```
   - `@semantic-release/git` committing `["panel/Cargo.toml", "panel/Cargo.lock"]` with `"message": "chore: Release v${nextRelease.version} [skip ci]\n\n${nextRelease.notes}"`.
   - `@semantic-release/github` with `"successComment": false`.
2. Create `/home/ribben/Desktop/pierspad/github/REA-FS/.github/workflows/release.yml`:
   - Trigger on `push` to `main` and `workflow_dispatch`.
   - Checkout with `fetch-depth: 0` and `submodules: recursive`.
   - Install Rust stable (`dtolnay/rust-toolchain@stable`) and apt dependencies (same as `.github/workflows/check.yml`), run `python3 scripts/rea-fs.py check`, then run `cycjimmy/semantic-release-action@v5`.
3. Add a GitHub release version badge (`![GitHub Release](https://img.shields.io/github/v/release/pierspad/REA-FS?include_prereleases&label=release)`) to `REA-FS/README.md` alongside the existing badges.

---

## Task 2: Submodule / Version Sync Script (`scripts/sync-driver.py`) & Git Hooks

1. Create `/home/ribben/Desktop/pierspad/github/REA-FS/tools/driver_sync.py` and CLI wrapper `/home/ribben/Desktop/pierspad/github/REA-FS/scripts/sync-driver.py` (plus subcommand `sync-driver` in `scripts/rea-fs.py`):
   - **Functionality:**
     1. `--check` mode (read-only verification):
        - Checks that `drivers/syna_tod` working tree is clean (`git status --porcelain`).
        - Verifies that the version in `drivers/syna_tod/meson.build` (`version: 'X.Y.Z'`) matches `VERSION=vX.Y.Z` in `drivers/syna_tod/bootstrap.sh`.
        - Verifies that the commit SHA currently checked out in `drivers/syna_tod` is reachable on the remote `origin/main` branch of `synaptics-00dc` (via `git -C drivers/syna_tod branch -r --contains HEAD` or `git ls-remote`), preventing unpushed submodule pins from ever being pushed in `REA-FS`.
     2. `--sync` mode:
        - If `/home/ribben/Desktop/pierspad/github/synaptics-00dc` exists and is clean, fetches/fast-forwards `drivers/syna_tod` from `origin/main` (after verifying `synaptics-00dc` has no unpushed commits ahead of `origin/main`).
        - If `workspace/libfprint/libfprint/drivers/syna_00dc` exists, copies the tracked `src/*.c` and `src/*.h` files from `drivers/syna_tod/src/` into `workspace/libfprint/libfprint/drivers/syna_00dc/` and **automatically rewrites** `dev_class->id = "syna_00dc_tod";` to `dev_class->id = "syna_00dc";` in `workspace/libfprint/libfprint/drivers/syna_00dc/syna_device.c`, ensuring the native prototype ID is never accidentally clobbered.
     3. `--install-hooks` mode:
        - Installs a lightweight executable `.git/hooks/pre-push` (and `.git/hooks/pre-commit`) in `REA-FS` that runs `python3 scripts/sync-driver.py --check` before allowing a commit/push that touches `drivers/syna_tod`.
2. Add unit tests in `/home/ribben/Desktop/pierspad/github/REA-FS/tests/test_driver_sync.py` testing:
   - Version mismatch detection between `meson.build` and `bootstrap.sh`.
   - Detection of an unpushed submodule commit not present on `origin/main`.
   - Native prototype `dev_class->id = "syna_00dc"` rewrite during sync.
3. Run `python3 scripts/sync-driver.py --install-hooks` so the hooks are active in the local repository.

---

## Task 3: Stage-Guided Anatomical Finger Zone Lighting in the Rust GUI (`panel/`)

Enhance the Enrollment user experience in `panel/src/model.rs` and `panel/src/main.rs` so the user is guided through specific parts of the fingertip across the 8 enrollment stages, and the corresponding ridges on the fingerprint scanner icon light up.

1. In `panel/src/model.rs`:
   - Define an enum `FingerZone`:
     ```rust
     #[derive(Clone, Copy, Debug, PartialEq, Eq)]
     pub enum FingerZone {
         All,
         Center,
         Tip,
         Base,
         Edges,
     }
     ```
   - Add a pure helper function `pub fn enroll_zone_for_stage(completed: u64, total: u64) -> (FingerZone, &'static str)`:
     - When `total == 0`: `(FingerZone::All, "Place your finger flat in the center of the reader.")`
     - Map `completed` (0-indexed stage before completion) across the enrollment progression:
       - Stages `0..=1` (Touches 1–2): `(FingerZone::Center, "Step 1/4 · Center: place the flat center of your fingertip.")`
       - Stages `2..=3` (Touches 3–4): `(FingerZone::Tip, "Step 2/4 · Tip: shift slightly forward toward the fingertip.")`
       - Stages `4..=5` (Touches 5–6): `(FingerZone::Base, "Step 3/4 · Base: shift slightly down toward the lower pad.")`
       - Stages `6..` (Touches 7+): `(FingerZone::Edges, "Step 4/4 · Edges: tilt slightly left/right to capture the sides.")`
   - Export `FingerZone` and `enroll_zone_for_stage` and add unit tests in `model::tests` verifying zone progression for `completed = 0..8, total = 8`.
2. In `panel/src/main.rs`:
   - Update `fingerprint_scanner(ui: &mut egui::Ui, phase: Phase, result: Option<bool>, zone: FingerZone)`:
     - For `Verify` and `Tools`, pass `FingerZone::All` (all 6 ridge arcs use `accent`, preserving existing behavior).
     - For `Enroll` when `active` is true, compute `(zone, zone_prompt) = model::enroll_zone_for_stage(completed, total)` and pass `zone` to `fingerprint_scanner`:
       - When `zone != FingerZone::All` and `phase == Phase::Touch || phase == Phase::Hold`:
         - Draw the non-target ridge segments in a dimmed color (`accent.gamma_multiply(0.28)`) and draw the target anatomical zone (`Center` = inner 3 arcs `i <= 2`; `Tip` = upper curved crowns of outer arcs `i >= 2`; `Base` = lower vertical legs of arcs; `Edges` = left and right outer legs `i >= 3`) in bright `accent` with slightly thicker stroke (`2.8`), plus a subtle zone badge label below the scanner showing the active zone (`zone_prompt`).
   - Run `cargo test --locked --manifest-path panel/Cargo.toml` and `cargo build --release --locked --manifest-path panel/Cargo.toml` to keep `panel/target/release/rea-fs-panel` updated.

---

## Task 4: Complete Async `FpiSsm` Orchestrators & Remove Redundant `verify` in `synaptics-00dc`

Bring `synaptics-00dc` (and `workspace/libfprint`) one step closer to upstream `libfprint` standards while keeping the default runtime rock-solid:

1. **Remove redundant `syna_verify` in `workspace/libfprint/libfprint/drivers/syna_00dc/syna_device.c` (and support `verify` via `identify`):**
   - In upstream `libfprint` (since commit `c5e49592`), `FpDevice` automatically routes `verify` through `identify` with a 1-element gallery if `dev_class->verify` is not set, whereas in `libfprint-2-tod-1` (based on older `libfprint` 1.94.x), check if `fpi_device_get_verify_data` is required when `dev_class->verify` is called.
   - In `synaptics-00dc/src/syna_device.c`, refactor `syna_verify_thread` and `syna_identify_thread` to share a single common helper `syna_match_probe_against_gallery(const float *live_sample, GPtrArray *entries, FpPrint **out_best_print, SynaMatchResult *out_best_res)` so zero matching loop code is duplicated between `verify` and `identify`.
2. **Opt-in `FpiSsm` Open/Close in `syna_device.c` (`SYNA_USE_SSM=1`):**
   - Include `"syna_ssm.h"` in `src/syna_device.c` and add `"src/syna_ssm.c"` to `tod_sources` in `meson.build`! (Currently `syna_ssm.c` is only compiled in `test_ssm`, not in `tod_driver`!).
   - Add a helper `static gboolean syna_use_async_ssm(void)` that checks `g_strcmp0(getenv("SYNA_USE_SSM"), "1") == 0`.
   - When `syna_use_async_ssm()` is `TRUE`:
     - In `syna_open(FpDevice *device)`: if persistent baseline cache is available (or loaded via `syna_load_baseline_cache`), run `FpiSsm *ssm = syna_ssm_open_new(self); fpi_ssm_start(ssm, syna_open_ssm_done_cb);` where `syna_open_ssm_done_cb` marks `self->is_open = TRUE` and calls `fpi_device_open_complete(device, error)`.
     - In `syna_close(FpDevice *device)`: run `FpiSsm *ssm = syna_ssm_close_new(device, NULL, NULL); fpi_ssm_start(ssm, syna_close_ssm_done_cb);` where `syna_close_ssm_done_cb` calls `fpi_device_close_complete(device, error)`.
   - When `SYNA_USE_SSM` is unset (default), preserve the exact `GTask` threaded path unchanged.
3. **Verification & Sync:**
   - Run `meson compile -C build` and `meson test -C build --print-errorlogs` in `/home/ribben/Desktop/pierspad/github/synaptics-00dc`.
   - Commit changes in `synaptics-00dc` using Vesta-style conventional commit messages (`feat: ...`, `ci: ...`), **push `synaptics-00dc` to `origin/main`**, then run `python3 scripts/sync-driver.py --sync` and `python3 scripts/sync-driver.py --check` in `REA-FS`.
   - Commit and push `REA-FS` to `origin/main`.
   - Run `python3 scripts/rea-fs.py check` and verify 0 failures across all Python, Rust, and C test suites.
