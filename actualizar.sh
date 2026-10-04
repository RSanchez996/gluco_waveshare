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

if ./.build-venv/bin/python -c 'import esptool' >/dev/null 2>&1; then
  esptool_cmd=(./.build-venv/bin/python -m esptool)
elif [[ -f "${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/tool-esptoolpy/esptool.py" ]]; then
  esptool_cmd=(./.build-venv/bin/python "${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/tool-esptoolpy/esptool.py")
else
  echo 'No encuentro esptool para copiar NVS. Instálalo en firmware/.build-venv antes de actualizar.' >&2
  exit 1
fi

read_action=read_flash
esptool_help="$("${esptool_cmd[@]}" --help 2>&1 || true)"
if [[ "$esptool_help" == *read-flash* ]]; then
  read_action=read-flash
fi
backup_dir="$project_dir/nvs_backups"
umask 077
mkdir -p "$backup_dir"
backup_path="$backup_dir/nvs_$(date +%Y%m%d_%H%M%S).bin"
echo "Copiando NVS a $backup_path antes de grabar..."
if ! "${esptool_cmd[@]}" --chip esp32s3 --port "$upload_port" \
  "$read_action" 0x9000 0x60000 "$backup_path"; then
  rm -f "$backup_path"
  echo 'No se pudo copiar NVS. Se cancela la carga para proteger los ajustes.' >&2
  exit 1
fi
if [[ $(stat -c '%s' "$backup_path") -ne 393216 ]]; then
  echo 'La copia de NVS está incompleta. Se cancela la carga.' >&2
  exit 1
fi
echo 'Copia completa. Actualizando solo el programa, sin erase...'
./.build-venv/bin/pio run -t upload --upload-port "$upload_port"
echo "Actualización completada. Conserva la copia privada: $backup_path"
