# Investigation Preferences

Before starting work, the agent reads `.rea-fs.local/preferences.json` (git-ignored) and any preferences stated in chat. The template is [preferences.example.json](preferences.example.json). Explicit user instructions in chat override the file; update the file when preferences change.

## Initial Setup in Chat

If `.rea-fs.local/preferences.json` is missing or unconfigured, ask these three questions once in a single message:

1. **Authorization:** Ask before operations that modify device or system state (`ask`, default), or authorize autonomous investigation within the project and identified sensor (`autonomous`)?
2. **Delegation:** Use only the main agent (`off`, default), or allow subagents for bounded tasks when net savings are plausible (`when_economical`)?
3. **Optimization target:** Minimize `total_tokens`, `monetary_cost`, or `latency`? Which subagent models are permitted?

Record the answers, scope, and source in `.rea-fs.local/preferences.json`. If the user declines to configure or says "you decide", record conservative defaults as `agent_default`; never infer `autonomous` mode from silence or refusal.

## Authorization Modes

| Mode | Behavior |
| --- | --- |
| `ask` (default) | Run passive discovery, static analysis, and offline checks directly. Ask before device/system writes, biometric capture, or publication. |
| `autonomous` | Proceed without repeated confirmation within `allowed_actions` and `scope` (project + sensor). Prepare each experiment, verify prerequisites, and log outcomes. |

- **Scope & categories:** `allowed_actions` may include `read_only_analysis`, `local_preparation`, `diagnostics`, `pairing`, `oem_initialization`, `device_reset`, `biometric_capture`, `local_commits`, `system_driver_installation`, `publish`. Keep `login_configuration` (PAM), `factory_reset`, `persistent_firmware`, and `global_ssh_or_sudo_changes` in `excluded_actions` unless explicitly authorized.
- **No blind retries:** Autonomy does not permit blind retries on hardware errors or timeouts. Analyze the failure and formulate a new hypothesis first.
- **Biometric data:** Always keep captures and templates in private git-ignored session directories (`data.external_transfer: false`). Physical touch prompts still require user participation via the GUI panel.
- **Privileges:** Prefer scoped device ACLs (`setfacl` on the specific `/dev/bus/usb/...` node) or explicit user-run installer commands over broad passwordless `sudo`.

## Subagent Delegation

When `delegation.mode` is `when_economical`:

- Delegate only isolated lookups or self-contained tasks with short inputs and clear verification criteria.
- Account for context transfer, coordination, and review overhead; a cheaper model does not guarantee fewer total tokens.
- Keep hardware experiments, stateful protocol debugging, and small doc edits in the main agent.
- Never send private keys, pairing material, or biometric captures/templates to subagents.
