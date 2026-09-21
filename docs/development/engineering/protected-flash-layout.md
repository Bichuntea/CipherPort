<p align="right">
  <a href="protected-flash-layout.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Protected Flash Layout

The ESP32-C3 has 8 MB Flash. The per-device `cardid` region remains fixed at
`0x356000`; no distributable firmware package contains user vault contents or
device identity data.

## Supported layouts

| Partition | New devices (`factory-4m`) | Existing devices (`legacy-3m`) |
| --- | --- | --- |
| NVS | `0x9000`, `0x6000` bytes | same |
| PHY | `0xF000`, `0x1000` bytes | same |
| Vault | `0x10000`, `0x200000` bytes | `0x35A000`, `0x200000` bytes |
| `cardid` | `0x356000`, `0x4000` bytes | same |
| Application | `0x360000`, `0x400000` bytes | `0x10000`, `0x300000` bytes |

Both partition tables require a valid MD5 marker and non-overlapping entries.
The final packaged ZIP must be below 6,500,000 bytes; 8 MB is the physical
Flash capacity, not permission to overwrite protected partitions.

The new application lies after `cardid`. Consequently, a contiguous merged BIN
from `0x0` would erase `cardid` and the vault region, even if its contents are
`0xFF`. Merged images are generated only for build verification and are **never
flashed or distributed** for the new layout.

## Delivery and upgrades

`./tools/validate.sh --firmware` builds and checks both layouts, then creates:

- `AI-Passport-factory-4m.zip`: segmented bootloader (`0x0`), partition table
  (`0x8000`), and application (`0x360000`) for a blank-table new device.
- `AI-Passport-upgrade-legacy-3m.zip`: application only at `0x10000`. It keeps
  the existing partition table, NVS, `cardid`, and vault untouched.
- `AI-Passport-upgrade-factory-4m.zip`: application only at `0x360000` for later
  updates of devices already provisioned with the new table.

Use `python tools/flash_package.py <zip> --port <serial-port>` to inspect the
device partition table before writing. Factory flashing refuses an existing
table; upgrades refuse a mismatched layout. Never use `erase-flash` on a
provisioned device. There is no automatic migration from legacy to 4 MB:
moving the old vault across the running application requires a separate,
hardware-tested backup and migration workflow.
