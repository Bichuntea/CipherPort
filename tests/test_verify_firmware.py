#!/usr/bin/env python3
"""Host tests for the protected firmware-layout parser."""

from __future__ import annotations

import hashlib
import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "verify_firmware", ROOT / "tools" / "verify_firmware.py"
)
assert SPEC and SPEC.loader
VERIFY = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VERIFY
SPEC.loader.exec_module(VERIFY)


def sample_table(profile: str = "factory-4m") -> bytes:
    layout = VERIFY.LAYOUTS[profile]
    entries = (
        (1, 2, 0x9000, 0x6000, "nvs"),
        (1, 1, 0xF000, 0x1000, "phy_init"),
        (0, 0, layout.app_offset, layout.app_size, "factory"),
        (1, 2, 0x356000, 0x4000, "cardid"),
        (1, 0x40, layout.vault_offset, 0x200000, "vault"),
    )
    raw = bytearray(b"\xff" * VERIFY.PARTITION_TABLE_SIZE)
    for index, (kind, subtype, offset, size, label) in enumerate(entries):
        VERIFY.ENTRY.pack_into(
            raw,
            index * VERIFY.ENTRY.size,
            0x50AA,
            kind,
            subtype,
            offset,
            size,
            label.encode().ljust(16, b"\0"),
            0,
        )
    marker = len(entries) * VERIFY.ENTRY.size
    struct.pack_into("<H", raw, marker, 0xEBEB)
    raw[marker + 16 : marker + 32] = hashlib.md5(raw[:marker]).digest()
    return bytes(raw)


class PartitionParserTest(unittest.TestCase):
    def test_parses_protected_layout_and_md5(self) -> None:
        partitions, found_md5 = VERIFY.parse_partition_table(sample_table())
        self.assertTrue(found_md5)
        cardid = next(item for item in partitions if item.label == "cardid")
        self.assertEqual(cardid.offset, VERIFY.CARDID_OFFSET)
        self.assertEqual(partitions[-1].label, "vault")

    def test_rejects_bad_md5(self) -> None:
        raw = bytearray(sample_table())
        raw[28] ^= 1
        with self.assertRaisesRegex(ValueError, "MD5"):
            VERIFY.parse_partition_table(bytes(raw))


class ProtectedLayoutTest(unittest.TestCase):
    def test_layout_verification_accepts_both_profiles(self) -> None:
        for profile, layout in VERIFY.LAYOUTS.items():
            with self.subTest(profile=profile):
                merged = bytearray(b"\xff" * (layout.app_offset + 1))
                merged[
                    VERIFY.PARTITION_TABLE_OFFSET :
                    VERIFY.PARTITION_TABLE_OFFSET + VERIFY.PARTITION_TABLE_SIZE
                ] = sample_table(profile)
                merged[layout.app_offset] = 0xE9

                with tempfile.TemporaryDirectory() as directory:
                    build_dir = Path(directory)
                    (build_dir / "FoloToy-AI-Passport.bin").write_bytes(b"\xe9")
                    VERIFY.verify_protected_layout(bytes(merged), build_dir, layout)

    def test_rejects_wrong_profile_and_identity_payload(self) -> None:
        layout = VERIFY.LAYOUTS["factory-4m"]
        merged = bytearray(b"\xff" * (layout.app_offset + 1))
        merged[
            VERIFY.PARTITION_TABLE_OFFSET :
            VERIFY.PARTITION_TABLE_OFFSET + VERIFY.PARTITION_TABLE_SIZE
        ] = sample_table()
        merged[layout.app_offset] = 0xE9
        with tempfile.TemporaryDirectory() as directory:
            build_dir = Path(directory)
            (build_dir / "FoloToy-AI-Passport.bin").write_bytes(b"\xe9")
            with self.assertRaisesRegex(ValueError, "partition 'factory'"):
                VERIFY.verify_protected_layout(
                    bytes(merged), build_dir, VERIFY.LAYOUTS["legacy-3m"]
                )
            merged[VERIFY.CARDID_OFFSET] = 0x01
            with self.assertRaisesRegex(ValueError, "cardid payload"):
                VERIFY.verify_protected_layout(bytes(merged), build_dir, layout)


if __name__ == "__main__":
    unittest.main()
