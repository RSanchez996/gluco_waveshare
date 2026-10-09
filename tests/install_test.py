import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("installer", ROOT / "tools/install.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)

def table(entries=None):
    data = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, sub, off, size, name.encode(), 0)
                    for name, kind, sub, off, size in (entries or installer.EXPECTED))
    data += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(data).digest()
    return data.ljust(4096, b"\xff")

class InstallerTests(unittest.TestCase):
    def test_real_shipped_images(self):
        self.assertEqual(len(installer.images()), 4)

    def test_checksum_corruption(self):
        raw = bytearray(table());raw[8] ^= 1
        with self.assertRaises(ValueError):
            installer.partitions(raw)

    def test_encrypted_partition_rejected(self):
        raw = bytearray(table());raw[28] = 1
        with self.assertRaises(ValueError):
            installer.partitions(raw)

    def simulate(self, raw, nvs=b"S" * 0x60000, clean=False, truncate=False, flash_size="16MB"):
        calls = []
        def run(port, baud, *args, capture=False):
            calls.append(args)
            if args[0] == "read_flash":
                data = raw if args[1] == "0x8000" else nvs if args[1] == "0x9000" else b"\xff" * 8192
                Path(args[-1]).write_bytes(data[:-1] if truncate else data)
            return subprocess.CompletedProcess([], 0, f"Detected flash size: {flash_size}\n", "")
        with tempfile.TemporaryDirectory() as folder, patch.object(installer, "ROOT", Path(folder)), \
             patch.object(installer, "run", run), patch.object(installer, "images", return_value=[(0, Path("bootloader.bin"))]), \
             contextlib.redirect_stdout(io.StringIO()):
            try:
                installer.flash("test", 115200, clean)
                error = None
            except ValueError as exc:
                error = str(exc)
            backups = list(Path(folder).glob("nvs_backups/*/nvs.bin"))
            if backups and not truncate:
                self.assertEqual(backups[0].read_bytes(), nvs)
        return calls, error

    def test_compatible_flash_preserves_nvs(self):
        calls, error = self.simulate(table())
        self.assertIsNone(error)
        self.assertEqual(sum(x[0] == "write_flash" for x in calls), 1)
        self.assertFalse(any(x[0] == "erase_flash" for x in calls))

    def test_incompatible_no_writes(self):
        entries = installer.EXPECTED.copy();entries[0] = ("nvs", 1, 2, 0x9000, 0x6000)
        calls, error = self.simulate(table(entries))
        self.assertIsNotNone(error)
        self.assertFalse(any(x[0] in ("write_flash", "erase_flash") for x in calls))

    def test_incomplete_backup_no_writes(self):
        calls, error = self.simulate(table(), truncate=True)
        self.assertIsNotNone(error)
        self.assertFalse(any(x[0] in ("write_flash", "erase_flash") for x in calls))

    def test_blank_board_accepted(self):
        calls, error = self.simulate(b"\xff" * 4096, b"\xff" * 0x60000)
        self.assertIsNone(error)
        self.assertTrue(any(x[0] == "write_flash" for x in calls))

    def test_blank_table_with_existing_data_no_writes(self):
        calls, error = self.simulate(b"\xff" * 4096)
        self.assertIsNotNone(error)
        self.assertFalse(any(x[0] in ("write_flash", "erase_flash") for x in calls))

    def test_explicit_clean_after_backup(self):
        calls, error = self.simulate(b"\0" * 4096, clean=True)
        self.assertIsNone(error)
        self.assertEqual([x[0] for x in calls], ["flash_id", "read_flash", "read_flash", "read_flash", "erase_flash", "write_flash"])

    def test_wrong_flash_capacity_no_writes(self):
        calls, error = self.simulate(table(), flash_size="8MB")
        self.assertIsNotNone(error)
        self.assertEqual(len(calls), 1)

if __name__ == "__main__":
    unittest.main()
