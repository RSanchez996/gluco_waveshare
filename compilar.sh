#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/firmware"
python3 -m venv .build-venv
. .build-venv/bin/activate
python -m pip install --quiet "platformio>=6.2.0" "esptool>=5,<6"
pio run
