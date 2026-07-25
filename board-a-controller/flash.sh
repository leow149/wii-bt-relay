#!/usr/bin/env bash
# Compiles and uploads Board A (controller-facing) via arduino-cli, then
# opens the serial monitor. One command, run from anywhere:
#
#   ./flash.sh [PORT] [FQBN]
#
# PORT defaults to /dev/ttyUSB0.
# FQBN defaults to esp32-bluepad32:esp32:esp32dev (confirmed against a real
# arduino-cli run -- the package uses a HYPHEN, not the underscore this
# script originally guessed. If arduino-cli still errors saying it doesn't
# recognize the FQBN, run `arduino-cli core search bluepad` and pass the
# exact ID it prints as this script's second argument.
#
# One-time setup (see docs/HARDWARE_SETUP.md for the full walkthrough):
#   arduino-cli config init
#   arduino-cli config add board_manager.additional_urls \
#     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
#   arduino-cli config add board_manager.additional_urls \
#     https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json
#   arduino-cli core update-index
#   arduino-cli core install esp32-bluepad32:esp32
set -euo pipefail

PORT="${1:-/dev/ttyUSB0}"
FQBN="${2:-esp32-bluepad32:esp32:esp32dev}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v arduino-cli &> /dev/null; then
    echo "ERROR: arduino-cli not found on PATH." >&2
    echo "See docs/HARDWARE_SETUP.md for install instructions." >&2
    exit 1
fi

echo "==> Compiling Board A ($FQBN)"
arduino-cli compile --fqbn "$FQBN" "$DIR"

echo "==> Uploading to $PORT"
arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$DIR"

echo "==> Done. Opening serial monitor (Ctrl+C to exit)"
arduino-cli monitor -p "$PORT" -c baudrate=115200
