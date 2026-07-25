#!/usr/bin/env bash
# Builds and flashes Board B (Wii-facing) via ESP-IDF, then opens the
# serial monitor. One command, run from anywhere:
#
#   ./flash.sh [PORT]
#
# PORT defaults to /dev/ttyUSB0.
#
# If idf.py isn't already on your PATH (i.e. you haven't sourced
# export.sh in this shell), set IDF_PATH to your ESP-IDF checkout and
# this script will source export.sh for you automatically, e.g.:
#
#   IDF_PATH=$HOME/esp/esp-idf ./flash.sh /dev/ttyUSB0
#
# See docs/HARDWARE_SETUP.md for the one-time ESP-IDF install steps.
set -euo pipefail

PORT="${1:-/dev/ttyUSB0}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v idf.py &> /dev/null; then
    if [ -n "${IDF_PATH:-}" ] && [ -f "$IDF_PATH/export.sh" ]; then
        echo "==> idf.py not on PATH, sourcing $IDF_PATH/export.sh"
        # shellcheck disable=SC1091
        . "$IDF_PATH/export.sh"
    else
        echo "ERROR: idf.py not found and IDF_PATH not set." >&2
        echo "Either run '. \$HOME/esp/esp-idf/export.sh' first in this shell," >&2
        echo "or re-run this script as: IDF_PATH=\$HOME/esp/esp-idf ./flash.sh" >&2
        exit 1
    fi
fi

cd "$DIR"
echo "==> Setting target to esp32"
idf.py set-target esp32

echo "==> Building, flashing $PORT, and opening monitor (Ctrl+] to exit)"
idf.py -p "$PORT" build flash monitor
