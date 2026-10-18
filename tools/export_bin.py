#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "firmware/build"
OUT = ROOT / "bin"
OUT.mkdir(exist_ok=True)
items = []
for offset, source, name in [(0, "bootloader/bootloader.bin", "bootloader.bin"),
                             (0x8000, "partition_table/partition-table.bin", "partition-table.bin"),
                             (0x69000, "ota_data_initial.bin", "ota_data_initial.bin"),
                             (0x70000, "gluco_waveshare.bin", "gluco_waveshare.bin")]:
    shutil.copyfile(BUILD / source, OUT / name)
    data = (OUT / name).read_bytes()
    items.append({"file": name, "offset": offset, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
shutil.copyfile(BUILD / "gluco_waveshare.elf", OUT / "gluco_waveshare.elf")

app_bin = (OUT / "gluco_waveshare.bin").read_bytes()
app_desc = app_bin[32:32 + 256]
version = app_desc[16:48].split(b"\x00")[0].decode("utf-8", errors="replace") or "2.0.0"
compile_time = app_desc[80:96].split(b"\x00")[0].decode("utf-8", errors="replace")
compile_date = app_desc[96:112].split(b"\x00")[0].decode("utf-8", errors="replace")
elf_sha256 = app_desc[144:176].hex()

changes_file = ROOT / "cambios_ota.txt"
changes = []
if changes_file.exists():
    for line in changes_file.read_text(encoding="utf-8").splitlines():
        line = line.strip().lstrip("•-*").strip()
        if line:
            changes.append(line)
if not changes:
    changes = [
        "Cálculo de TIR 24 h desde Diabetes:M en RAM cada 15 min (sin desgaste de Flash)",
        "Actualización OTA desde Ajustes con confirmación previa y resumen de cambios",
        "Conexiones HTTPS reforzadas con reintentos automáticos y control de tiempos TLS",
    ]

manifest = {
    "version": version,
    "elf_sha256": elf_sha256,
    "build_date": f"{compile_date} {compile_time}".strip(),
    "target": "ESP32-S3-Touch-LCD-4.3B/BOX",
    "idf": "5.5.5",
    "changes": changes,
    "images": items,
}
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(f"Binarios y SHA-256 actualizados (v{version}, elf_sha256={elf_sha256[:12]}...)")
