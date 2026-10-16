# Community readiness action plan

Scope: user requested clearer onboarding, tester recruitment, naming/TOD advice,
and execution. Keep `synaptics-00dc`; no repository rename or external messages.
Documentation changes only in this iteration; preserve installed driver and panel plan.

## Execute now

- [x] Replace sensor-specific generic checklist with evidence-driven steps; use the
  actual preferences schema, no fixed matcher/latency promises or permissive Polkit rule.
- [x] Add new-sensor getting-started guide: recursive clone, reading order, passive
  discovery, private session, simulation, experimental question and hardware gate.
- [x] Add driver tester guide and issue template, LLM contribution boundaries,
  TOD explanation, recursive workbench clone and agent instructions.
- [x] Check relative links, shell syntax and CLI options; review the documentation;
  publish driver first and then update the REA-FS pin and documentation.

## Next milestones — require new evidence

1. Installed v0.1.1: cancellation/reopen and verification through the existing panel.
   Exit criterion: logs show reuse after cancellation and correct verification.
2. First external tester with matching ROM/module: installation, initial pairing,
   calibration, enrollment, verification and recovery report. No credential import.
3. Runtime hardening: replay/error tests, device-specific persistent state,
   custom build directory in setup, cancellation/unplug/suspend coverage. Each
   behavioral change needs an isolated regression and then hardware validation.
4. Biometric evaluation: separate enrollment, threshold-development and held-out
   touches; compare NBIS/image-device path and current matcher. Report denominators,
   retries and timeouts; do not extrapolate population reliability from one owner.
5. Upstream candidate: finish async orchestration, agree state ownership, review
   table provenance and TLS implementation, test native build on hardware, then
   request upstream review. A compiling prototype is not an upstream acceptance.
6. Broader distribution: versioned packages and tested uninstall/rollback after
   fresh-install reports; then distro/community announcements with exact scope.

Validation focus: fresh checkout; absent TOD; sensor not 00dc; missing preferences;
LLM-generated patch that changes matching/security to make a test pass.

## Execution record — 2026-10-17

Documentation implementation complete. Checked 14 shell blocks with bash -n and
31 relative links; verified init-session/preflight arguments against CLI help.
A bounded independent Luna review found no actionable issues. No hardware or
system installation performed; no external announcement sent. Driver changes
published before updating the workbench pin. Next milestones remain pending
and are not represented as completed by the documentation checks.
