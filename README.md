# <img src="docs/rea-fs-icon.svg" alt="REA-FS" height="42" align="absmiddle"> REA-FS

#### Reverse Engineer Any - Fingerprint Sensor

[![GitHub Sponsors](https://img.shields.io/badge/Sponsor-%E2%9D%A4-ea4aaa?logo=github&style=flat)](https://github.com/sponsors/pierspad) [![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-Donate-yellow?logo=buymeacoffee)](https://buymeacoffee.com/pierspad) [![Ko-fi](https://img.shields.io/badge/Ko--fi-Support-ff5e5b?logo=ko-fi)](https://ko-fi.com/pierspad)

**A workbench for reverse-engineering unsupported fingerprint sensors on Linux.**

## Just want your Fingerprint sensor to work?

I used this workflow for my Synaptics-00dc and it works flawlessly, hope it can help you too :)

> For **USB synaptics `06cb:00dc`**, use [github.com/pierspad/synaptics-00dc](https://github.com/pierspad/synaptics-00dc)

## Investigate another sensor

Start with the [new-sensor guide](docs/getting-started.md). Clone with `--recurse-submodules` to include the example driver source:

```sh
git clone --recurse-submodules https://github.com/pierspad/REA-FS.git
cd REA-FS
python3 scripts/open-panel.py
```

Give your coding agent this prompt:

> Read AGENTS.md, docs/preferences.md, docs/agent-workflow.md, and
> docs/experiment-panel.md. Resume the local checkpoint, identify my sensor
> passively, and prepare any physical experiment in the existing GUI panel.

## Development

```sh
python3 scripts/rea-fs.py check
```

Runs the offline Python, Rust, and C test suites and builds. Install the driver build dependencies and NumPy (`python3-numpy` or `requirements-dev.txt`) first.

The GUI lives in `panel/`, the Python supervisor and analysis tools in `tools/`, and the example driver submodule in `drivers/syna_tod`. See the [repository map](docs/repositories.md) for details.

[Experiment panel](docs/experiment-panel.md) ·
[Sensor checklist](docs/re-checklist-template.md) ·
[Synaptics research](docs/research-06cb-00dc.md) ·
[Engineering notes](docs/engineering-notes.md) ·
[libfprint port](docs/libfprint-upstreaming.md)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Never publish pairing keys, fingerprint images, or enrolled templates.

## LLM disclosure

Developed with assistance from Large Language Models for reverse engineering, code, tests, and documentation. Hardware evidence is recorded separately from implementation and offline test results.

## License

[GNU LGPL v2.1](LICENSE). The driver submodule carries its own license and provenance.
