<p align="right">
  <a href="build-and-test.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Build and Test

Use ESP-IDF 5.5.3. On a clean machine or when the toolchain is missing, follow
the [environment bootstrap](environment-setup.md) first.

> Prefer `./tools/validate.sh --firmware` for both firmware profiles. Use the
> segmented factory ZIP only on a blank-table new device, or the matching
> application-only upgrade ZIP on an existing device. Never flash a merged BIN
> from `0x0` across protected `cardid`. Treat
> `idf.py build` and `idf.py flash` as incremental development commands, not the
> default delivery path.

```bash
source <path-to-esp-idf-v5.5.3>/export.sh
idf.py --version             # must report ESP-IDF v5.5.3
./tools/validate.sh --firmware # build, verify, and package both layouts
idf.py set-target esp32c3     # fresh checkout or changed target
idf.py build                  # optional incremental application build
idf.py flash monitor          # optional incremental application flash
idf.py fullclean              # remove stale generated build state only
```

`idf.py fullclean` does not fully synchronize an existing `sdkconfig` with
changed defaults. Preserve intentional local settings, then run
`idf.py set-target esp32c3` when the target or tracked defaults must be
regenerated.

The tracked `dependencies.lock` pins Managed Component resolution. After changing an `idf_component.yml`, regenerate the lock with ESP-IDF 5.5.3, review version changes, and commit it with the manifest. An ordinary build must not leave an unexplained lock-file diff.

Firmware validation uses separate fresh build directories and isolated `sdkconfig` files for the 4 MB factory and 3 MB legacy profiles. It does not overwrite a developer's root `sdkconfig`. Merged images are verified but not distributed; only segmented ZIP packages are copied to `build/`. The gate enforces the [protected Flash layout](protected-flash-layout.md): `cardid`, profile-specific application and vault offsets, partition-table MD5, the 6.5 MB package limit, and absence of device-specific data.

The baseline also has a hardware-independent logic test:

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_ui_pixel_math.c main/ui_pixel_math.c \
  -o /tmp/test_ui_pixel_math
/tmp/test_ui_pixel_math
```

Use the unified validation entry point:

```bash
./tools/validate.sh --static    # repository checks, workflows, links, secrets, host tests
./tools/validate.sh --firmware  # build, merge-bin, offsets, and protected layout
./tools/validate.sh             # complete gate; requires an activated ESP-IDF environment
```

CI calls the same script. Fix the shared script or environment if local and CI behavior differs; do not duplicate command sequences in workflows.

Hardware-affecting changes must also run the applicable on-device checklist in the hardware guide. Report compilation separately from physical-device validation.

Distribute only the three named, verified ZIP packages. Flash them with
`python tools/flash_package.py build/<package>.zip --port <serial-port>`; the
tool rejects a partition-layout mismatch before writing. Raw merged BIN files
are verification artifacts, not safe flashing instructions.
