#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [[ -z "${IDF_PATH:-}" ]]; then
  echo 'Primero activa ESP-IDF 5.5.5: source /ruta/esp-idf/export.sh'
  exit 2
fi
if [[ "$(git -C "$IDF_PATH" describe --tags --exact-match 2>/dev/null)" != v5.5.5 ]]; then
  echo 'Se requiere ESP-IDF v5.5.5 para reproducir este firmware.'
  exit 2
fi
export IDF_COMPONENT_MANAGER=0
python3 tools/embed_web.py
python3 tools/test_all.py
python3 "$IDF_PATH/tools/idf.py" -C firmware build
python3 tools/export_bin.py
