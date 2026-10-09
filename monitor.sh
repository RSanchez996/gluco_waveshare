#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
exec .tools/flash-venv/bin/python -m serial.tools.miniterm "${1:?Indica el puerto}" 115200
