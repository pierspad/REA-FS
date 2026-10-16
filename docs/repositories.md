# Project Repositories

| Repository | Remote | Local Path | Role |
| --- | --- | --- | --- |
| REA-FS | `git@github.com:pierspad/REA-FS.git` | root (`.`) | Experiment panel, supervisor, workflow, and research notes |
| synaptics-00dc | `https://github.com/pierspad/synaptics-00dc.git` | `drivers/syna_tod` (submodule) | Standalone C driver, build, installer, and unit tests |
| REA | `git@github.com:morluto/rea.git` | `workspace/rea` | Optional static reverse-engineering tools (Ghidra/CLI/MCP) |
| libfprint upstream | `git@ssh.gitlab.freedesktop.org:libfprint/libfprint.git` | `workspace/libfprint` | Official upstream source and native driver prototype |
| synaTudor upstream | `git@github.com:Popax21/synaTudor.git` | `workspace/synaTudor` | Reference Windows driver loader for Synaptics sensors |

## Submodule vs. Ignored Clones

Clones under `workspace/` are independent and git-ignored. `drivers/syna_tod` is a pinned Git submodule pointing to a verified commit of `synaptics-00dc`. A sibling checkout (`../synaptics-00dc`) may be used for driver development; it is the same repository, not a second implementation.

Initialize the submodule with:

```sh
git submodule update --init --recursive
```

Always commit, test, and push driver changes in `synaptics-00dc` before updating the submodule pin in REA-FS. When configuring a personal `libfprint` fork on freedesktop.org, verify existing remotes and Git author identity (`git config --show-origin`) before pushing.

## Where Contributions Belong

- **Standalone TOD C driver, installer, and C unit tests:** `synaptics-00dc`.
- **Native upstream driver (async `FpiSsm`, `FpDevice`/`FpImageDevice`):** `libfprint` upstream once hardware and provenance gates are met.
- **Windows DLL loader / WinBio wrapper fixes:** `synaTudor`.

## Optional Research References

- `git@github.com:uunicorn/python-validity.git` (`workspace/validity-reference`): USB Validity protocol, `RomInfo` layout, and TLS-PRF reference. Do not run its automatic setup or reset routines on unknown hardware.
- `git@github.com:sandbranch/x1-tablet-gen3-fingerprint.git` (`workspace/synaspi-reference`): SPI reference for internal product `0x33` (`SYNA8002`). Shared internal product ID does not imply identical transport, flash layout, or pairing keys.
