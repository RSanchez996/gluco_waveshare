#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [[ $# -lt 1 ]]; then
  echo 'Uso: bash instalar.sh /dev/ttyACM0 [--baud 115200] [--instalacion-limpia]'
  exit 2
fi
port="$1"
shift
if [[ ! -x .tools/flash-venv/bin/python ]]; then
  python3 -m venv .tools/flash-venv
fi
.tools/flash-venv/bin/python -m pip install --disable-pip-version-check 'esptool==4.12.0'
exec .tools/flash-venv/bin/python tools/install.py --port "$port" "$@"
