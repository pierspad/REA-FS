#!/usr/bin/env bash
# Compatibility wrapper: the driver repository owns installation.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ! -f "$ROOT/drivers/syna_tod/install.sh" ]]; then
    echo 'Driver submodule missing: git submodule update --init --recursive' >&2
    exit 1
fi
exec bash "$ROOT/drivers/syna_tod/install.sh" "$@"
