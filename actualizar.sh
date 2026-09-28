#!/usr/bin/env bash
set -euo pipefail

if [[ $# -gt 1 ]]; then
  echo 'Uso: ./actualizar.sh [puerto, por ejemplo /dev/ttyACM0]' >&2
  exit 2
fi

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
upload_port="${1:-/dev/ttyACM0}"
cd "$project_dir/firmware"
if [[ ! -x ./.build-venv/bin/pio ]]; then
  echo 'Falta firmware/.build-venv/bin/pio. Ejecuta antes ./compilar.sh.' >&2
  exit 1
fi

echo 'Actualizando solo el programa. No se ejecutará erase ni se borrará NVS.'
exec ./.build-venv/bin/pio run -t upload --upload-port "$upload_port"
