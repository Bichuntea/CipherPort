#!/usr/bin/env python3
"""Flash a verified segmented package without erasing cardid, NVS, or vault."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from package_firmware import FLASH_SIZE, checked_part, sha256
from verify_firmware import CARDID_OFFSET, CARDID_SIZE, LAYOUTS, Partition, parse_partition_table


def matching_layout(table: bytes, profile: str) -> bool:
    try:
        entries, found_md5 = parse_partition_table(table)
    except (UnicodeDecodeError, ValueError):
        return False
    if not found_md5 or len(entries) != 5:
        return False
    layout = LAYOUTS[profile]
    expected = {
        Partition(1, 2, 0x9000, 0x6000, "nvs"),
        Partition(1, 1, 0xF000, 0x1000, "phy_init"),
        Partition(0, 0, layout.app_offset, layout.app_size, "factory"),
        Partition(1, 2, CARDID_OFFSET, CARDID_SIZE, "cardid"),
        Partition(1, 0x40, layout.vault_offset, 0x200000, "vault"),
    }
    return set(entries) == expected


def read_package(package: Path) -> tuple[dict, list[tuple[int, str, bytes]]]:
    with zipfile.ZipFile(package) as archive:
        manifest = json.loads(archive.read("flash-manifest.json"))
        profile = manifest.get("profile")
        kind = manifest.get("kind")
        if manifest.get("schema") != 1 or manifest.get("chip") != "ESP32-C3" or \
           manifest.get("flashBytes") != FLASH_SIZE or profile not in LAYOUTS or \
           kind not in {"factory", "upgrade"}:
            raise ValueError("unsupported firmware manifest")
        parts = []
        for item in manifest["parts"]:
            filename = item["file"]
            if Path(filename).name != filename or filename == "flash-manifest.json":
                raise ValueError("invalid part filename")
            data = archive.read(filename)
            offset = item["offset"]
            if not isinstance(offset, int) or len(data) != item["size"] or \
               sha256(data) != item["sha256"]:
                raise ValueError(f"part {filename} failed integrity verification")
            checked_part(offset, data, profile)
            parts.append((offset, filename, data))
        expected_offsets = ({0, 0x8000, LAYOUTS[profile].app_offset}
                            if kind == "factory" else {LAYOUTS[profile].app_offset})
        if {item[0] for item in parts} != expected_offsets or len(parts) != len(expected_offsets):
            raise ValueError("firmware package has incomplete or unexpected Flash segments")
        if kind == "factory" and profile != "factory-4m":
            raise ValueError("legacy factory flashing is not supported")
        if manifest.get("partitionTableSha256") is None:
            raise ValueError("missing partition-table identity")
        if kind == "factory":
            table = next(data for offset, _, data in parts if offset == 0x8000)
            if sha256(table) != manifest["partitionTableSha256"]:
                raise ValueError("factory partition table does not match manifest")
        return manifest, parts


def esptool(port: str, baud: int, command: list[str]) -> None:
    subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32c3",
                    "--port", port, "--baud", str(baud), *command], check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--port", required=True, help="exact serial port, for example COM5")
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--dry-run", action="store_true", help="check package only; do not access a device")
    args = parser.parse_args()
    manifest, parts = read_package(args.package)
    if args.dry_run:
        print(f"Package verified: {manifest['profile']} {manifest['kind']}; no device accessed")
        return 0
    with tempfile.TemporaryDirectory(prefix="ai-passport-flash-") as temp:
        table_path = Path(temp) / "device-partition-table.bin"
        esptool(args.port, args.baud,
                ["read_flash", "0x8000", "0x1000", str(table_path)])
        table = table_path.read_bytes()[:0xC00]
        if manifest["kind"] == "factory":
            if any(byte != 0xFF for byte in table):
                raise ValueError("factory package requires a blank partition table; use a matching upgrade package")
        elif not matching_layout(table, manifest["profile"]):
            raise ValueError("device partition layout does not match this upgrade package; no Flash was written")
        command = ["write_flash", "--flash_mode", "dio", "--flash_freq", "80m",
                   "--flash_size", "8MB"]
        for offset, filename, data in sorted(parts):
            path = Path(temp) / filename
            path.write_bytes(data)
            command.extend([hex(offset), str(path)])
        esptool(args.port, args.baud, command)
    print(f"Flashed {manifest['profile']} {manifest['kind']} without erase-flash")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
