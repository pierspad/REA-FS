# Installing the Synaptics `06cb:00dc` Driver

The standalone C driver is maintained in [synaptics-00dc](https://github.com/pierspad/synaptics-00dc) and pinned in REA-FS at `drivers/syna_tod`.

```sh
git submodule update --init --recursive
bash drivers/syna_tod/install.sh
bash drivers/syna_tod/install.sh --apply
```

- `bash drivers/syna_tod/install.sh` prints a dry-run installation plan without modifying the system.
- `bash drivers/syna_tod/install.sh --apply` builds, runs offline C tests as the normal user, backs up existing files under `/var/lib/fprint/syna_00dc-backup.XXXXXXXX`, and installs `libsyna_00dc_tod.so` plus the udev rule.
- Existing credentials in `/var/lib/fprint/syna_00dc` (`0700`) are preserved. Pass `--credentials PRIVATE_DIR` only when importing credentials for the same physical device.
- For first-time pairing and calibration on a fresh installation, see `drivers/syna_tod/docs/installation.md` (`bash setup.sh`). Never run `setup.sh` unattended from an agent terminal.

## Legacy Polkit Rule Cleanup

Older versions of `scripts/install-tod-driver.sh` installed `/etc/polkit-1/rules.d/50-syna-00dc-fprintd.rules`, which granted `net.reactivated.fprint.device.setusername` alongside `enroll`/`verify` to active local sessions. The current installer no longer installs this file, nor does it silently delete existing administrator files. If present from an older install, back it up and remove it manually to restore default distribution Polkit policy.

## Hardware Validation

Offline builds and unit tests do not prove hardware operation. After installing or updating the module, validate cancellation, reopening, enrollment, and verification through the GUI experiment panel (`docs/experiment-panel.md`).
