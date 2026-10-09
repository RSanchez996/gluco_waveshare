#!/usr/bin/env python3
"""Flash only the shipped ESP32-S3 images, keeping and backing up NVS by default."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = [("nvs", 1, 2, 0x9000, 0x60000), ("otadata", 1, 0, 0x69000, 0x2000),
            ("app0", 0, 0x10, 0x70000, 0x680000), ("app1", 0, 0x11, 0x6F0000, 0x680000),
            ("spiffs", 1, 0x82, 0xD70000, 0x290000)]

def partitions(raw):
    entries = []
    for pos in range(0, min(len(raw), 0xC00), 32):
        chunk = raw[pos:pos + 32]
        if len(chunk) != 32:
            raise ValueError("Tabla de particiones incompleta")
        magic = struct.unpack_from("<H", chunk)[0]
        if magic == 0xEBEB:
            if chunk[16:] != hashlib.md5(raw[:pos]).digest():
                raise ValueError("Checksum de la tabla de particiones incorrecto")
            break
        if magic == 0xFFFF:
            break
        if magic != 0x50AA:
            raise ValueError("Tabla de particiones desconocida o dañada")
        _, kind, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", chunk)
        if flags != 0:
            raise ValueError("No se admite flash/NVS cifrada en este instalador")
        entries.append((label.split(b"\0", 1)[0].decode("ascii"), kind, subtype, offset, size))
    return entries

def images():
    manifest = json.loads((ROOT / "bin/manifest.json").read_text(encoding="utf-8"))
    result = []
    for item in manifest["images"]:
        name = item["file"]
        if Path(name).name != name:
            raise ValueError("Nombre de imagen no válido")
        path = ROOT / "bin" / name
        data = path.read_bytes()
        if len(data) != item["size"] or hashlib.sha256(data).hexdigest() != item["sha256"]:
            raise ValueError(f"Imagen alterada o incompleta: {name}; descarga de nuevo el ZIP")
        result.append((item["offset"], path))
    if [x[0] for x in result] != [0, 0x8000, 0x69000, 0x70000]:
        raise ValueError("Offsets de grabación inesperados")
    if partitions((ROOT / "bin/partition-table.bin").read_bytes()) != EXPECTED:
        raise ValueError("Las particiones del firmware no coinciden con el instalador")
    return result

def run(port, baud, *args, capture=False):
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
           "--baud", str(baud), *map(str, args)]
    return subprocess.run(cmd, check=True, text=True, capture_output=capture)

def flash(port, baud, clean=False):
    payload = images()  # verify everything before opening the device
    ident = run(port, baud, "flash_id", capture=True)
    print(ident.stdout)
    if not re.search(r"Detected flash size:\s*16MB", ident.stdout):
        raise ValueError("Se requiere la Waveshare 4.3B/BOX ESP32-S3 con flash de 16 MB")
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder = ROOT / "nvs_backups" / stamp
    folder.mkdir(parents=True, mode=0o700)
    table = folder / "partition-table.bin"
    nvs = folder / "nvs.bin"
    ota = folder / "otadata.bin"
    run(port, baud, "read_flash", "0x8000", "0x1000", table)
    run(port, baud, "read_flash", "0x9000", "0x60000", nvs)
    run(port, baud, "read_flash", "0x69000", "0x2000", ota)
    for path, length in [(table, 4096), (nvs, 0x60000), (ota, 8192)]:
        if path.stat().st_size != length:
            raise ValueError("Respaldo incompleto; no se ha escrito el firmware")
        os.chmod(path, 0o600)
    print(f"Respaldo local: {folder}\nContiene claves Wi-Fi y de LibreLinkUp; guárdalo en privado.")
    current = partitions(table.read_bytes()) if not clean else []
    blank = table.read_bytes() == b"\xff" * 4096 and nvs.read_bytes() == b"\xff" * 0x60000
    if not clean and current != EXPECTED and not blank:
        raise ValueError("Particiones incompatibles. Se ha guardado un respaldo y no se ha escrito nada. "
                         "Consulta README.md; --instalacion-limpia borra la configuración")
    if clean:
        run(port, baud, "erase_flash")
    args = ["write_flash", "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "16MB"]
    for offset, path in payload:
        args.extend([hex(offset), path])
    run(port, baud, *args)
    print("Instalación terminada. El primer arranque puede tardar unos segundos.")

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", required=True, help="COM5, /dev/ttyACM0 o /dev/cu.usbmodem...")
    p.add_argument("--baud", type=int, default=460800)
    p.add_argument("--instalacion-limpia", action="store_true", help="BORRA toda la flash tras el respaldo")
    args = p.parse_args()
    try:
        flash(args.port, args.baud, args.instalacion_limpia)
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
