#!/usr/bin/env python3
"""Host tests for segmented package bounds and device-layout selection."""

from __future__ import annotations

import hashlib
import json
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from flash_package import matching_layout, read_package  # noqa: E402
from package_firmware import checked_part, make_archive  # noqa: E402
from verify_firmware import ENTRY, LAYOUTS, PARTITION_TABLE_SIZE  # noqa: E402


def sample_table(profile: str) -> bytes:
    layout = LAYOUTS[profile]
    entries = (
        (1, 2, 0x9000, 0x6000, "nvs"),
        (1, 1, 0xF000, 0x1000, "phy_init"),
        (1, 0x40, layout.vault_offset, 0x200000, "vault"),
        (1, 2, 0x356000, 0x4000, "cardid"),
        (0, 0, layout.app_offset, layout.app_size, "factory"),
    )
    table = bytearray(b"\xff" * PARTITION_TABLE_SIZE)
    for index, (kind, subtype, offset, size, label) in enumerate(entries):
        ENTRY.pack_into(table, index * ENTRY.size, 0x50AA, kind, subtype,
                        offset, size, label.encode().ljust(16, b"\0"), 0)
    marker = len(entries) * ENTRY.size
    struct.pack_into("<H", table, marker, 0xEBEB)
    table[marker + 16:marker + 32] = hashlib.md5(table[:marker]).digest()
    return bytes(table)


class FlashPackageTest(unittest.TestCase):
    def test_detects_both_profiles_without_mixing_them(self) -> None:
        for profile in LAYOUTS:
            table = sample_table(profile)
            self.assertTrue(matching_layout(table, profile))
            for other in LAYOUTS:
                if other != profile:
                    self.assertFalse(matching_layout(table, other))
        self.assertFalse(matching_layout(b"\xff" * PARTITION_TABLE_SIZE,
                                         "factory-4m"))

    def test_protected_ranges_cannot_be_written(self) -> None:
        checked_part(0x360000, b"\xe9", "factory-4m")
        checked_part(0x10000, b"\xe9", "legacy-3m")
        for profile, offset in (("factory-4m", 0x10000),
                                ("factory-4m", 0x356000),
                                ("legacy-3m", 0x35A000)):
            with self.subTest(profile=profile, offset=offset):
                with self.assertRaisesRegex(ValueError, "protected"):
                    checked_part(offset, b"\xe9", profile)

    def test_factory_package_contains_only_safe_segments(self) -> None:
        table = sample_table("factory-4m")
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / "factory.zip"
            make_archive(package, "factory-4m", "factory", table, [
                (0, "bootloader.bin", b"\xe9"),
                (0x8000, "partition-table.bin", table),
                (0x360000, "application.bin", b"\xe9"),
            ])
            manifest, parts = read_package(package)
            self.assertEqual(manifest["profile"], "factory-4m")
            self.assertEqual({offset for offset, _, _ in parts},
                             {0, 0x8000, 0x360000})

    def test_tampered_application_is_rejected(self) -> None:
        table = sample_table("legacy-3m")
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / "legacy.zip"
            make_archive(package, "legacy-3m", "upgrade", table,
                         [(0x10000, "application.bin", b"\xe9")])
            with zipfile.ZipFile(package) as archive:
                manifest = archive.read("flash-manifest.json")
            with zipfile.ZipFile(package, "w") as archive:
                archive.writestr("flash-manifest.json", manifest)
                archive.writestr("application.bin", b"x")
            with self.assertRaisesRegex(ValueError, "integrity"):
                read_package(package)


if __name__ == "__main__":
    unittest.main()
