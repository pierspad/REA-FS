# <img src="docs/rea-fs-icon.svg" alt="REA-FS" height="42" align="absmiddle"> REA-FS

[![GitHub Sponsors](https://img.shields.io/badge/Sponsor-%E2%9D%A4-ea4aaa?logo=github&style=flat)](https://github.com/sponsors/pierspad) [![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-Donate-yellow?logo=buymeacoffee)](https://buymeacoffee.com/pierspad) [![Ko-fi](https://img.shields.io/badge/Ko--fi-Support-ff5e5b?logo=ko-fi)](https://ko-fi.com/pierspad)

**A workbench for reverse-engineering unsupported fingerprint sensors on Linux.**

## Just want your Synaptics reader to work?

For **USB `06cb:00dc`**, use [synaptics-00dc](https://github.com/pierspad/synaptics-00dc):
the standalone C driver, installation command, prerequisites and known limitations.
You do not need this GUI, Rust or the reverse-engineering tools to use that driver.

## Investigate another sensor

REA-FS provides a Rust/egui experiment panel, a Python supervisor, private session
logs and a documented agent workflow. The Synaptics driver is a worked example,
not a promise of compatibility with other sensors.

```sh
git clone --recurse-submodules https://github.com/pierspad/REA-FS.git
cd REA-FS
python3 scripts/open-panel.py
```

The launcher builds the panel when necessary. A current built panel can run without
Cargo; building it requires Rust/Cargo and Linux graphics development libraries.
Python supervises experiments. No sensor experiment starts automatically.

Give your coding agent this instruction:

> Read AGENTS.md, docs/preferences.md, docs/agent-workflow.md and
> docs/experiment-panel.md. Resume the local checkpoint, identify my sensor
> passively, and prepare any physical experiment in the existing GUI panel.

## Layout

| Path | Responsibility |
|---|---|
| `panel/` | Rust/egui user interface |
| `tools/` | Python experiment supervision and offline analysis |
| `scripts/` | Small launchers and workflow commands |
| `tests/` | Workbench offline tests |
| `drivers/syna_tod/` | Pinned submodule of the standalone Synaptics driver |
| `docs/`, `examples/` | Workflow, research evidence and sample plans |
| `sessions/`, `.rea-fs.local/` | Ignored private checkpoints, logs and captures |
| `workspace/` | Ignored independent upstream/reference clones |

The driver has one maintained source repository. A submodule stores its URL and
an exact commit; it does not automatically follow the driver's latest changes.
For an existing checkout: `git submodule update --init --recursive`.
See [repository organization](docs/repositories.md) before updating the pin.

## Checks and documentation

```sh
python3 scripts/rea-fs.py check
```

Runs Python, Rust and C checks without hardware. Synthetic tests do not establish
biometric reliability; hardware tests use the panel and explicit user actions.
The complete test suite additionally needs NumPy (`python3-numpy` on Ubuntu,
or `requirements-dev.txt` in a virtual environment). The panel supervisor itself
uses only Python's standard library.

- [Experiment panel](docs/experiment-panel.md)
- [Generic sensor checklist](docs/re-checklist-template.md)
- [Synaptics investigation](docs/synaptics-06cb-00dc-checklist.md)
- [libfprint upstream work and remaining gaps](docs/libfprint-upstreaming.md)
- [Architecture decisions](docs/project-organization.md)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Never publish pairing keys, fingerprint
images or enrolled templates.

## LLM disclosure

Developed with assistance from Large Language Models for reverse engineering,
code, tests and documentation. Hardware evidence is recorded separately from
implementation and offline test results.

## License

[GNU LGPL v2.1](LICENSE). The driver submodule carries its own license and provenance.
