#!/usr/bin/env python3
"""Create segmented factory and partition-matched upgrade packages."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import zipfile
from pathlib import Path

from verify_firmware import CARDID_OFFSET, CARDID_SIZE, LAYOUTS, VAULT_SIZE

PACKAGE_LIMIT = 6_500_000
FLASH_SIZE = 8 * 1024 * 1024
STAMP = (2026, 1, 1, 0, 0, 0)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def checked_part(offset: int, data: bytes, layout_name: str) -> None:
    layout = LAYOUTS[layout_name]
    end = offset + len(data)
    if offset < 0 or end > FLASH_SIZE or not data:
        raise ValueError("firmware part has invalid Flash bounds")
    protected = (
        (0x9000, 0xF000, "nvs"),
        (CARDID_OFFSET, CARDID_OFFSET + CARDID_SIZE, "cardid"),
        (layout.vault_offset, layout.vault_offset + VAULT_SIZE, "vault"),
    )
    for start, stop, label in protected:
        if offset < stop and start < end:
            raise ValueError(f"firmware part overlaps protected {label} partition")


def checked_build(build_dir: Path, profile: str) -> tuple[bytes, bytes, bytes]:
    subprocess.run(
        [sys.executable, "-S", str(Path(__file__).with_name("verify_firmware.py")),
         str(build_dir), "--profile", profile],
        check=True,
    )
    bootloader = (build_dir / "bootloader" / "bootloader.bin").read_bytes()
    table = (build_dir / "partition_table" / "partition-table.bin").read_bytes()
    application = (build_dir / "FoloToy-AI-Passport.bin").read_bytes()
    return bootloader, table, application


def make_archive(destination: Path, profile: str, kind: str,
                 table: bytes, parts: list[tuple[int, str, bytes]]) -> None:
    if kind == "factory" and profile != "factory-4m":
        raise ValueError("only the 4 MB layout may be factory packaged")
    previous_end = -1
    manifest_parts = []
    for offset, filename, data in sorted(parts):
        checked_part(offset, data, profile)
        if offset < previous_end:
            raise ValueError("firmware parts overlap each other")
        previous_end = offset + len(data)
        manifest_parts.append({
            "offset": offset, "file": filename, "size": len(data), "sha256": sha256(data),
        })
    manifest = {
        "schema": 1,
        "chip": "ESP32-C3",
        "flashBytes": FLASH_SIZE,
        "profile": profile,
        "kind": kind,
        "applicationOffset": LAYOUTS[profile].app_offset,
        "applicationPartitionBytes": LAYOUTS[profile].app_size,
        "partitionTableSha256": sha256(table),
        "protectedCardidOffset": CARDID_OFFSET,
        "parts": manifest_parts,
    }
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=9) as archive:
        manifest_info = zipfile.ZipInfo("flash-manifest.json", STAMP)
        manifest_info.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(manifest_info, json.dumps(manifest, indent=2) + "\n")
        for _, filename, data in parts:
            info = zipfile.ZipInfo(filename, STAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    if destination.stat().st_size > PACKAGE_LIMIT:
        destination.unlink()
        raise ValueError(f"{destination.name} exceeds the 6.5 MB delivery target")
    print(f"Package: {destination} ({destination.stat().st_size} bytes)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("factory_build", type=Path)
    parser.add_argument("legacy_build", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    factory_boot, factory_table, factory_app = checked_build(
        args.factory_build, "factory-4m"
    )
    _, legacy_table, legacy_app = checked_build(args.legacy_build, "legacy-3m")
    make_archive(args.output_dir / "AI-Passport-factory-4m.zip", "factory-4m",
                 "factory", factory_table, [
                     (0x0, "bootloader.bin", factory_boot),
                     (0x8000, "partition-table.bin", factory_table),
                     (LAYOUTS["factory-4m"].app_offset, "application.bin", factory_app),
                 ])
    make_archive(args.output_dir / "AI-Passport-upgrade-legacy-3m.zip",
                 "legacy-3m", "upgrade", legacy_table,
                 [(LAYOUTS["legacy-3m"].app_offset, "application.bin", legacy_app)])
    make_archive(args.output_dir / "AI-Passport-upgrade-factory-4m.zip",
                 "factory-4m", "upgrade", factory_table,
                 [(LAYOUTS["factory-4m"].app_offset, "application.bin", factory_app)])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
