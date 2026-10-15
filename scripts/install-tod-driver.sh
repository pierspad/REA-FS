#!/usr/bin/env bash
# Review first; apply only with explicit system-installation consent.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_DIR}/drivers/syna_tod/build"
CRED_DEST_DIR="/var/lib/fprint/syna_00dc"
UDEV_DEST="/etc/udev/rules.d/60-syna-00dc-tod.rules"
POLKIT_DEST="/etc/polkit-1/rules.d/50-syna-00dc-fprintd.rules"
APPLY=0
CREDENTIALS=""
while (($#)); do
    case "$1" in
        --credentials) CREDENTIALS="${2:?missing credentials directory}"; shift 2 ;;
        --apply) APPLY=1; shift ;;
        *) echo 'Usage: install-tod-driver.sh --credentials PRIVATE_DIR [--apply]' >&2; exit 2 ;;
    esac
done
[[ -n "$CREDENTIALS" ]] || { echo 'An explicit --credentials directory is required.' >&2; exit 2; }
TOD_DEST_DIR="$(pkg-config --variable=tod_driversdir libfprint-2-tod-1)"
[[ "$TOD_DEST_DIR" == /* && "$TOD_DEST_DIR" != / ]] || { echo 'Invalid TOD plugin directory.' >&2; exit 1; }
[[ -f "${BUILD_DIR}/libsyna_00dc_tod.so" ]] || { echo 'Build locally with ninja before installing.' >&2; exit 1; }
for name in host_key.pem host_cert.bin server_ecdh_pub.bin capture_init.bin capture_packet.bin; do
    [[ -s "$CREDENTIALS/$name" && ! -L "$CREDENTIALS/$name" ]] || { echo "Missing or symlinked credential: $name" >&2; exit 1; }
    [[ ! -L "$CRED_DEST_DIR/$name" ]] || { echo "Refusing symlinked destination: $name" >&2; exit 1; }
done
if [[ -f "$CREDENTIALS/baseline_cache.bin" && ! -L "$CREDENTIALS/baseline_cache.bin" ]]; then
    [[ ! -L "$CRED_DEST_DIR/baseline_cache.bin" ]] || { echo "Refusing symlinked destination: baseline_cache.bin" >&2; exit 1; }
fi
[[ ! -L "$CRED_DEST_DIR" && ! -L "$UDEV_DEST" && ! -L "$POLKIT_DEST" ]] || { echo 'Refusing symlinked destination.' >&2; exit 1; }
openssl pkey -in "$CREDENTIALS/host_key.pem" -check -noout >/dev/null 2>&1 || { echo 'Invalid private key.' >&2; exit 1; }
echo "Module: ${BUILD_DIR}/libsyna_00dc_tod.so -> ${TOD_DEST_DIR}/libsyna_00dc_tod.so"
echo "Private credentials: $CREDENTIALS -> $CRED_DEST_DIR (0700 directory, 0600 files)"
echo "Udev rule: $UDEV_DEST"
echo "Polkit rule: $POLKIT_DEST"
echo 'Existing files will be backed up privately under /var/lib/fprint/syna_00dc-backup.XXXXXX.'
echo 'Will reload udev rules and restart fprintd if active. PAM configuration is not edited.'
if [[ "$APPLY" != 1 ]]; then
    echo 'DRY RUN: no system changes. Add --apply only after reviewing this plan.'
    exit 0
fi
[[ "$EUID" == 0 ]] || { echo '--apply requires root.' >&2; exit 1; }
umask 077
BACKUP_DIR="$(mktemp -d /var/lib/fprint/syna_00dc-backup.XXXXXX)"
if [[ -f "$TOD_DEST_DIR/libsyna_00dc_tod.so" ]]; then cp -p "$TOD_DEST_DIR/libsyna_00dc_tod.so" "$BACKUP_DIR/driver.so"; fi
if [[ -f "$UDEV_DEST" ]]; then cp -p "$UDEV_DEST" "$BACKUP_DIR/udev.rules"; fi
if [[ -f "$POLKIT_DEST" ]]; then cp -p "$POLKIT_DEST" "$BACKUP_DIR/polkit.rules"; fi
if [[ -d "$CRED_DEST_DIR" ]]; then cp -a "$CRED_DEST_DIR" "$BACKUP_DIR/credentials"; fi
install -d -m 0755 "$TOD_DEST_DIR"
install -d -m 0700 "$CRED_DEST_DIR"
for name in host_key.pem host_cert.bin server_ecdh_pub.bin capture_init.bin capture_packet.bin; do
    install -m 0600 "$CREDENTIALS/$name" "$CRED_DEST_DIR/$name"
done
if [[ -f "$CREDENTIALS/baseline_cache.bin" ]]; then
    install -m 0600 "$CREDENTIALS/baseline_cache.bin" "$CRED_DEST_DIR/baseline_cache.bin"
fi
install -m 0755 "${BUILD_DIR}/libsyna_00dc_tod.so" "$TOD_DEST_DIR/libsyna_00dc_tod.so"
install -m 0644 "${REPO_DIR}/drivers/syna_tod/60-syna-00dc-tod.rules" "$UDEV_DEST"
if [[ -d "/etc/polkit-1/rules.d" ]]; then
    install -m 0644 "${REPO_DIR}/drivers/syna_tod/50-syna-00dc-fprintd.rules" "$POLKIT_DEST"
fi
udevadm control --reload-rules
if systemctl is-active --quiet fprintd; then systemctl restart fprintd; fi
echo "Installed. Private backup: $BACKUP_DIR"
echo 'Keep password access available. Re-enroll before evaluating the changed image pipeline.'
