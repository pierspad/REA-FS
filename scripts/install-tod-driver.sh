#!/usr/bin/env bash
#
# install-tod-driver.sh
# Automated installer for the Synaptics 06cb:00dc libfprint-tod driver
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DRIVER_DIR="${REPO_DIR}/drivers/syna_tod"
BUILD_DIR="${DRIVER_DIR}/build"
TOD_DEST_DIR="/usr/lib/libfprint-2/tod-1"
CRED_DEST_DIR="/var/lib/fprint/syna_00dc"
UDEV_DEST="/etc/udev/rules.d/60-syna-00dc-tod.rules"

echo "============================================================"
echo " Synaptics 06cb:00dc libfprint-tod Driver Installer"
echo "============================================================"

if [ "$EUID" -ne 0 ]; then
    echo "[-] This installation script requires root privileges."
    echo "    Please run with sudo: sudo ./scripts/install-tod-driver.sh"
    exit 1
fi

# 1. Build driver if needed
echo "[1/5] Building driver..."
if [ ! -f "${BUILD_DIR}/libsyna_00dc_tod.so" ]; then
    echo "  [*] Configuring meson build in ${BUILD_DIR}..."
    sudo -u "${SUDO_USER:-$USER}" meson setup "${BUILD_DIR}" "${DRIVER_DIR}"
fi
sudo -u "${SUDO_USER:-$USER}" ninja -C "${BUILD_DIR}"

# 2. Install shared module to TOD plugin directory
echo "[2/5] Installing libsyna_00dc_tod.so to ${TOD_DEST_DIR}..."
mkdir -p "${TOD_DEST_DIR}"
cp -f "${BUILD_DIR}/libsyna_00dc_tod.so" "${TOD_DEST_DIR}/libsyna_00dc_tod.so"
chmod 0755 "${TOD_DEST_DIR}/libsyna_00dc_tod.so"
echo "  [+] Driver module installed."

# 3. Install OEM credentials and blobs
echo "[3/5] Installing sensor credentials to ${CRED_DEST_DIR}..."
python3 "${SCRIPT_DIR}/setup-tod-credentials.py" --out-dir "${CRED_DEST_DIR}"
echo "  [+] Credentials and OEM blobs installed with restricted permissions (0600)."

# 4. Install udev rule
echo "[4/5] Installing udev rules to ${UDEV_DEST}..."
cp -f "${DRIVER_DIR}/60-syna-00dc-tod.rules" "${UDEV_DEST}"
chmod 0644 "${UDEV_DEST}"
udevadm control --reload-rules || true
udevadm trigger --subsystem-match=usb || true
echo "  [+] Udev rules applied."

# 5. Restart fprintd
echo "[5/5] Restarting fprintd service..."
if systemctl is-active --quiet fprintd; then
    systemctl restart fprintd
    echo "  [+] fprintd restarted."
else
    echo "  [*] fprintd is not currently running (will start on D-Bus demand)."
fi

echo ""
echo "============================================================"
echo " Installation Complete!"
echo "============================================================"
echo "You can now test your sensor using standard fprintd commands:"
echo "  1. Enroll a finger:"
echo "     fprintd-enroll"
echo "  2. Verify your enrolled finger:"
echo "     fprintd-verify"
echo "  3. List enrolled prints:"
echo "     fprintd-list \$USER"
echo ""
echo "Or run the direct interactive test tool:"
echo "  ${BUILD_DIR}/test_fprint_enroll_verify"
echo "============================================================"
