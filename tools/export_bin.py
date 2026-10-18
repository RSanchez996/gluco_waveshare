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
shutil.copyfile(BUILD / "gluco_waveshare.elf",OUT / "gluco_waveshare.elf")
(OUT / "manifest.json").write_text(json.dumps({"version": "2.0.0", "target": "ESP32-S3-Touch-LCD-4.3B/BOX", "idf": "5.5.5", "images": items}, indent=2) + "\n")
print("Binarios y SHA-256 actualizados")
