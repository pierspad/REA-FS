# Investigate a new fingerprint sensor

REA-FS supplies a workflow, a local experiment panel and a Synaptics case study.
It is not a universal decoder or automatic driver generator. A different reader
may use a different bus, authentication scheme, image format or on-device matcher.

## 1. Get the workbench

```sh
git clone --recurse-submodules https://github.com/pierspad/REA-FS.git
cd REA-FS
```

The recursive clone includes the pinned example driver. If you cloned without it:

```sh
git submodule update --init --recursive
```

This downloads source only; it does not install a driver, configure REA/Ghidra,
clone the optional `workspace/` references, or access your fingerprint sensor.
Never install the Synaptics example on a different sensor just to try it.

## 2. Read in this order

| Read | Purpose |
|---|---|
| [Agent instructions](../AGENTS.md) and [preferences](preferences.md) | Scope, consent, private data and execution rules |
| [Repository map](repositories.md) | Which code belongs where; optional references |
| [Agent workflow](agent-workflow.md) | Investigation states and evidence requirements |
| [Generic checklist](re-checklist-template.md) | Milestones for your sensor |
| [Experiment panel](experiment-panel.md) | Prepare, run, stop and inspect a bounded experiment |
| [Investigation template](templates/investigation.md) | One question, prediction and result per experiment |
| [Synaptics research](research-06cb-00dc.md) and [status](libfprint-upstreaming.md) | A case study and its current limitations |

Historical research notes describe hypotheses and superseded trials. Use the
current status document and your own checkpoint to decide the next action.

## 3. Start with passive discovery

Identify the actual USB VID:PID with `lsusb`; for SPI/I2C readers record the relevant
ACPI/HID identity instead. Configure local preferences through the three questions
in the preferences guide; the example JSON is unconfigured, not consent.
After identifying the reader, replace both placeholders below:

```sh
python3 scripts/rea-fs.py init-session --sensor 'BUS:OBSERVED-ID' \
  --directory sessions/my-sensor
python3 scripts/rea-fs.py preflight --output sessions/my-sensor/preflight.json
```

Resume an existing checkpoint rather than creating another for the same work.
Keep session output private. Check upstream support before assuming reverse
engineering is needed. An ID in an unsupported-device list is not a driver.

## 4. Try the panel without touching hardware

```sh
python3 scripts/open-panel.py
```

Python 3 is required; building the GUI also needs Rust/Cargo, a C toolchain and
Linux graphics development dependencies. A current compiled GUI needs no Cargo.
On a fresh checkout the launcher supplies the [starter plan](../examples/panel-plan.json).
Choose **Try the panel**: it is a simulation, not a fingerprint test. Existing
plans are preserved; do not overwrite a session's hardware plan with an example.
Full offline checks also require the driver build dependencies and NumPy.

## 5. Ask one question at a time

For example: “Does the documented response parser reject a truncated packet?” is
an offline question. “Does this command start a bounded stream and stop it cleanly?”
requires protocol evidence, reviewed side effects and a hardware plan.
First inspect the matching OEM package and record its source/hash privately.
Do not reuse Synaptics opcodes, firmware tables, pairing or reset sequences on
another reader. A diagnostic program may change device state when it opens it.

Prepare the plan using the experiment guide, then validate:

```sh
python3 scripts/experiment-panel.py validate --session-dir .rea-fs.local/panel
```

The user starts physical experiments in the GUI. Define a timeout, a stop action
and an observable success criterion. After the user returns, inspect `latest-run.json`,
`runs-index.jsonl`, the run's `summary.json` (including notes) and `events.jsonl`.
Do not treat a plausible image, successful build or silence as a hardware result.

## Prompt for your agent

> Read AGENTS.md and docs/getting-started.md, then the mandatory workflow and local
> preferences. Resume my checkpoint or establish my preferences before creating one.
> Identify my sensor passively and check existing upstream support. Treat Synaptics
> as a case study, not a compatible driver. Propose one evidence-backed experiment;
> physical tests must use the existing panel. Keep credentials and biometrics local.

The agent can initialize a missing submodule after checking for local changes.
It should not erase or replace an existing investigation, install the example
implicitly, or publish private logs. Missing tools and unsupported hardware are
valid findings; they are not permission to invent a successful result.

## 6. Share hardware reports, fixes, or new sensor findings

If you solve a problem, improve the workbench, or test the Synaptics driver on your machine,
please share your results by opening an **Issue** or **Pull Request** on GitHub
([`REA-FS`](https://github.com/pierspad/REA-FS) or [`synaptics-00dc`](https://github.com/pierspad/synaptics-00dc)):

- **Even if installation worked on the first try:** Open a quick Issue reporting your laptop
  model, Linux distribution/kernel, USB ID, and sensor ROM/module revision so we can track
  verified devices and hardware variants (never include personal serial numbers, pairing keys,
  or fingerprint data).
- **If your AI agent fixed a bug or added support:** Ask your agent to prepare a clean PR or
  Issue with a detailed technical explanation of the root cause and the offline test results.

