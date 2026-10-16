# Driver distribution and workbench consolidation

User delegated architecture and execution on 2026-10-16, including creation of a
dedicated GitHub repository. Preserve existing local matcher changes. No PAM edits,
sensor operations from agent terminal, or claims of fresh hardware validation.

## Design

Keep the existing egui UI and Python experiment supervisor. A standalone C/Meson
repository, `pierspad/synaptics-00dc`, owns the sensor driver, tests and installer.
REA-FS consumes that repository at `drivers/syna_tod` as a pinned Git submodule.
The sibling checkout is the maintainer's development copy; the submodule is the
reproducible example. No private credentials or captures are migrated or published.
Installer must fail explicitly when TOD headers are unavailable; no silent system
libfprint replacement. First pairing/enrollment remain explicit physical operations.

## Execution

- [x] Enlarge viewport and add scrolling; run Rust tests/build. Embed existing icon
      and register matching per-user Wayland desktop entry; tests validate escaping.
- [x] Harden dormant SSM responses, deadline and cancellation; keep known working
      backend default until hardware parity is demonstrated. Test actual code.
- [x] Remove permissive Polkit rule from default install; implement standalone
      build/install entry points, offline install tests and rollback metadata.
- [x] Make synthetic vs private test inputs explicit; add unified check and CI.
- [x] Extract tracked driver files only, preserving source provenance; prepare
      concise README, badges, contributing, disclosure and installer command.
- [x] Review publication contents, create/push dedicated repo, replace parent
      tracked driver with pinned submodule while preserving ignored private data.
- [ ] Update parent documentation and wrappers; verify clean recursive clone.
- [x] Prepare GUI hardware plan for cancellation/reopen; baseline/bootstrap
      validation; report measurements still requiring hardware/user participation.

## Evidence

- Driver source `2c55a59`, experimental tag v0.1.0; standalone CI passed on Ubuntu.
- Shared production alignment replaces test/device/SSM copies; 11 C suites pass.
- Installer/setup tests stage files and mock services, not actual root/hardware use.
- Native libfprint compile prototype `412d6286` (local only) builds and its three
  upstream unit suites pass; FpImageDevice/NBIS conversion remains open.
- Existing system driver/PAM/Polkit not modified. Source installer no longer ships
  permissive rule; existing installed rule still needs deliberate remediation.
- User explicitly asked to reuse README icon after initial design; no new artwork.

## Verification and limits

Check Python, Rust, C, installer dry-run/negative cases, clean source/submodule
checkout and no secrets in tracked exports. No timing/security generalization from
synthetic tests. Upstream FpImageDevice integration requires a separate compile and
matching evaluation; never replace working production backend on compilation alone.

## Coordination

Main agent owns extraction, installer, documentation and integration. Luna owns
bounded GUI edit and SSM hardening in disjoint files; no measured token savings
claimed. Work in existing checkout to preserve uncommitted optimization and private
experiment paths; snapshot local diff privately before structural migration.
