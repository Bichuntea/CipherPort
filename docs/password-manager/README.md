<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# CipherPort Password Manager Requirements

## Product scope

This fork turns AI Passport into a local password viewer. The encrypted vault
lives on the device. A temporary Wi-Fi page edits records, but the browser is
not persistent storage.

The first release follows the Figma `02 Device Screens` flow: welcome, fixed
four-digit PIN entry, account browsing, explicit reveal confirmation, timed
plaintext password display, settings, web-management Off/Active states,
three-step PIN change, lock confirmation, destructive erase hold, and About.
The updated Figma file also covers recovery, first-run enrollment, lockout,
Web authorization/mutation approval, long-content, and erase result states.

The ESP32-C3 release does not provide USB HID typing, USB networking, Bluetooth
typing, fingerprint unlock, or cloud sync. Native USB remains the USB
Serial/JTAG console and flashing path.

## Hardware invariants

- Target ESP-IDF 5.5.3 on ESP32-C3 with 8 MB Flash and no PSRAM.
- New devices use a 4 MB application partition at `0x360000`; existing devices
  retain the 3 MB partition at `0x10000`. Preserve `cardid` at `0x356000`,
  size `0x4000`, and never migrate provisioned data by raw flashing.
- Use BSP display, button, audio, battery, and shared-I2C APIs. Never create a
  second ADC1 unit or I2C0 bus.
- Button callbacks only enqueue events. Storage, audio, and networking run in
  workers. Non-LVGL contexts hold the BSP LVGL lock for UI access.
- Wi-Fi and Bluetooth start disabled after every boot or wake.

## Controls

The application replaces the demo menu and launches into the password manager.

| Input | Behavior |
| --- | --- |
| `UP` / `DOWN` | Previous or next item/value |
| `OK` click | Confirm, enter, or execute |
| `OK` long press | Back or cancel |
| power click | Lock and blank, only after an MCU-visible event is confirmed |
| power long press | Enter deep sleep, only after an MCU wake path is confirmed |

Selection, confirmation, return, success, warning, and commit-result tones are
distinct. `UP` and `DOWN` sound identical during secret entry. Automatic
password scrolling is silent.

The Figma shortcut `HOLD UP+DOWN SETTINGS` and erase gesture `HOLD UP + DOWN`
cannot be treated as implementable on the current single-channel ADC resistor
ladder: simultaneous presses collapse to one analog voltage. Until a measured
dual-press window is approved, account-list Settings uses `OK` double-click and
the erase screen uses a context-specific five-second `OK` hold. The displayed
key hints must match the implemented gestures before release.

## Figma-derived screen contract

All pages use the 240 x 320 secure-terminal shell: black background, status at
`y=10`, title at `y=34`, primary content beginning at `y=72`, actions at
`y=246`, and a conditional key hint at `y=288`. Empty hint frames are never
drawn; single-action screens expose only their full-width action. The status bar always reserves
time and battery; Wi-Fi and Bluetooth labels appear only while active. Battery
colors are Good at 50% or above, Medium at 20-49%, and Low at 19% or below. An
invalid clock renders `--:--` rather than a plausible time.

```text
WELCOME -> ENTER PIN -> SAVED ACCOUNTS -> ACCOUNT
ACCOUNT -> REVEAL CONFIRM -> PASSWORD REVEALED -> ACCOUNT
SAVED ACCOUNTS -> SETTINGS
SETTINGS -> WEB MANAGEMENT OFF / ACTIVE
SETTINGS -> CURRENT PIN -> NEW PIN -> CONFIRM PIN
SETTINGS -> LOCK CONFIRM -> WELCOME
SETTINGS -> CLEAR ALL DATA HOLD
SETTINGS -> ABOUT
```

`WELCOME` accepts only an `OK` click and opens PIN entry. `UP/DOWN` scroll a
Web-configured welcome message only when it exceeds the adaptive panel. Account rows use
`UP/DOWN`; `OK` opens the focused account. An empty vault instead shows a
first-account guide whose `OK` action opens Web Management; Wi-Fi starts only
after another `OK` action on that management page. Account details show full-word labels
above bounded values and keep the password masked. Reveal always passes through
the two-choice warning dialog. The revealed page owns the absolute 15-second
deadline, range indicator, canonical progress bar, manual scrolling, and
immediate-hide action.

## Enrollment and secret entry

1. Enter a fixed four-digit PIN on the device. No PIN-length setting exists.
2. Enter the PIN twice.
3. Generate an eight-digit decimal super-access code, preserving leading zeroes.
4. Display it once and require only an acknowledgement that it was saved;
   setup does not require re-entry.
5. Create a random vault master key. Store wrapped key material and verifiers,
   never plaintext PIN or recovery code.

For every PIN digit, generate a new secure Fisher-Yates permutation containing
`0..9` exactly once and render it in the two-row selector. `UP` and `DOWN` move
through all digits plus `CLEAR` and `CONTINUE`; `OK` activates the focused item.
Reshuffle after every digit. Confirmed digits use unframed masked indicators.
`CLEAR` removes the entire draft at any time. Selecting `CONTINUE` before all
digits exist displays an explicit incomplete-entry error. Double-clicking `OK`
removes the previous digit, and long-pressing `OK` returns safely.

Changing a PIN is always three steps: verify the current four digits, enter the
new four digits, then re-enter them. Only the final `SAVE` may replace the PIN
wrapper. A mismatch returns to the new-PIN step with both drafts wiped.

## Lock, recovery, and erase

- Five incorrect PIN attempts lock the vault; the counter survives reset and
  power loss.
- A locked vault exposes recovery entry, erase, and sleep only.
- The recovery code allows three incorrect attempts total, persisted across
  reset and power loss.
- Successful recovery requires a new PIN and recovery code and invalidates the
  old wrapper.
- Three recovery failures permanently disable recovery for the current vault.
- Flash-backed prototype counters are not rollback-proof. Production protection
  requires an approved eFuse policy or additional secure hardware.

Erase is initiated only on the device from the dedicated warning page. With
the current button hardware, hold `OK` for five seconds on that page and show
continuous progress before enabling the final commit. Destroy vault-key
wrappers before erasing password-manager data, and never touch `cardid`.

## Accounts and password display

Records contain only bounded UTF-8 platform, note, URL, username, and password
fields in the initial release. There are no categories, favorites, or saved
dates. The Web Manager can keep the device order or sort records by platform
name in ascending or descending order. Passwords are masked by default. Selecting `Reveal password` opens a
warning dialog with `CANCEL` and `REVEAL`; no plaintext is produced before the
second confirmation. Explicit reveal lasts 15 seconds by default; the
configurable range is 5–30 seconds, never unlimited.
Lock, timeout, navigation away, or `OK` long press wipes the display buffer.

For passwords wider than one line, pause at the start for 1.5 seconds, scroll at
about five characters per second, pause one second at the end, and repeat. Show
the visible range and total length. `UP`/`DOWN` move manually and `OK` pauses or
resumes without extending the deadline. Use a bounded, device-tested monospace
font that distinguishes `0/O` and `1/I/l`.

## Wi-Fi management

After unlock and explicit user action:

1. Start a password-protected SoftAP with at most one client.
2. Show SSID, private IPv4 address, and remaining time on the device.
3. Require an explicit device-side `OK` after the browser connects.
4. Issue a random in-memory session token only after that confirmation.
5. Stage each bounded mutation in RAM and require a device-side
   `APPROVE`/`CANCEL` selection followed by `OK`.
6. Stop on lock, explicit browser close, device-side exit, or two minutes
   without an authorized client. A transient station reconnect does not destroy
   an already authorized session.

The Off page exposes only the Web start action and an explanation. Starting it
enters a two-minute connection window. The SSID is always `CIPHERPORT`; a new
random eight-digit numeric Wi-Fi password is generated on every entry. Only one
device may connect. After browser connection and the single
device-side `OK` confirmation, the page shows `CONNECTED` and removes the countdown. Leaving
any device-side Web Management state stops Wi-Fi and removes the status icon.
Runtime network values never appear before successful startup.

The default is `http://192.168.4.1` on port 80. Custom addresses must be private
unicast addresses and are displayed on-device. The server binds only to the
SoftAP. Responses use `Cache-Control: no-store`; the page does not persist
secrets in browser storage. Requests use strict size bounds, per-request nonces,
session binding, timeouts, and rate limits. Secrets and request bodies never
enter logs.

Plain HTTP relies on WPA link protection and lacks independent application-layer
confidentiality. This is accepted only for the prototype and needs a production
threat review.

## Storage and atomic commits

The existing 24 KiB NVS is for bounded configuration and security metadata, not
the vault. The 2 MiB `vault` partition starts at `0x10000` on new devices and
at `0x35a000` on legacy devices. The partition is marked encrypted, but
that flag provides confidentiality only on devices provisioned with Flash
Encryption. The firmware additionally uses a PIN/recovery-wrapped random master
key and AES-GCM authenticated two-generation commits for up to 16 bounded
records. Interrupted-write behavior still requires physical device validation.

Each generation contains a format version, logical generation, unique nonce,
encrypted payload, and authentication tag. Write and verify an inactive
generation, atomically switch the active marker, then invalidate the old one.
Boot selects the newest authenticated committed generation. Any cancellation,
disconnect, write error, or reset before the marker switch keeps the old one.

## Security architecture

- Generate a random master key; never use a short PIN directly as the vault key.
- Wrap it separately for PIN and recovery paths with random salts and a
  device-bound secret.
- Use a reviewed authenticated encryption mode and never repeat a nonce for a
  key. Compare verifiers in constant time and wipe temporary secrets.
- Decrypt only the selected record when practical.
- Flash encryption, Secure Boot, NVS encryption, signed firmware, debug limits,
  and anti-rollback require separate development and production provisioning.
  Ordinary development builds never burn irreversible eFuses.
- Backups use an independent high-entropy secret, not the eight-digit code.

## Runtime ownership

| Owner | Responsibility |
| --- | --- |
| LVGL task | Screens and non-secret presentation state |
| button callback | Non-blocking event enqueue |
| app reducer | Pure state transitions and authorization |
| security worker | Verification, key unwrap, crypto, and wiping |
| storage worker | Atomic generations, counters, and backup streaming |
| network worker | SoftAP/STA, HTTP, sessions, and request bounds |
| audio worker | Short queued tones and blocking PCM writes |

Workers use bounded queues. Automatic screen-off wipes secrets, closes the Web
session, turns off the LCD panel and backlight, and keeps the button path alive;
one function-key action wakes the display. Hardware power-off remains separate.

## Top-level state model

```text
UNENROLLED -> PIN_SETUP(4) -> PIN_CONFIRM(4) -> RECOVERY_CONFIRM -> WELCOME
WELCOME -> PIN_ENTRY(4) -> ACCOUNT_LIST
PIN_ENTRY -- five failures --> PIN_LOCKED
PIN_LOCKED -> RECOVERY_ENTRY -> NEW_PIN_SETUP(4) -> LOCKED
RECOVERY_ENTRY -- three failures --> RECOVERY_DISABLED
ACCOUNT_LIST -> ACCOUNT_DETAIL -> REVEAL_CONFIRM -> PASSWORD_REVEAL
ACCOUNT_LIST -> SETTINGS -> CHANGE_PIN_CURRENT -> CHANGE_PIN_NEW -> CHANGE_PIN_CONFIRM
SETTINGS -> LOCK_CONFIRM -> WELCOME
SETTINGS -> ERASE_HOLD / ABOUT / WEB_MANAGEMENT_OFF
WEB_MANAGEMENT_OFF -> WIFI_STARTING -> WEB_AUTH_PENDING -> WEB_ACTIVE
WEB_ACTIVE -> DEVICE_APPROVAL -> ATOMIC_COMMIT -> WEB_ACTIVE
any sensitive state -> LOCKING -> LOCKED / SLEEP_PREP
```

Every transition leaving sensitive content explicitly wipes buffers. All
communication is unavailable in locked states.

## Delivery phases

1. Pure reducer, randomized secret entry, bounds, and host tests.
2. Enrollment/lock UI, account shell, audio queue, and battery state.
3. Approved vault partition, crypto storage, recovery, erase, and atomic tests.
4. Bounded SoftAP/HTTP service and embedded page.
5. Backup/restore and an approved signed-update strategy.
6. Production provisioning and physical-device validation.

## Decisions requiring evidence

1. The BSP exposes no MCU-readable left power-button signal. Confirm its
   schematic and wake capability before implementing its gestures.
2. Approve vault capacity, field limits, backup maximum, and partition layout.
3. The single factory-app layout has no inactive OTA slot. Define a protected,
   power-fail-safe signed update path before enabling web updates.
4. Approve prototype HTTP risk or select application-layer encryption/HTTPS.
5. Define development, pilot, and production eFuse provisioning separately.
6. Replace the two impossible `UP+DOWN` Figma hints or approve a measured ADC
   interpretation. The documented fallback is `OK` double-click for Settings
   and a five-second `OK` hold for erase.
7. Add final Figma screens for enrollment/recovery, web mutation approval,
   PIN lockout/recovery-disabled outcomes, and write success/failure.

## Acceptance baseline

Host tests cover state transitions, permutation invariants, attempt boundaries,
timeouts, password scrolling, parser bounds, interrupted atomic commits, and
wipe hooks. Device tests cover input, display readability, optional
battery failure, audio latency, heap budgets, repeated network lifecycle,
interrupted Flash writes, malformed HTTP input, one-client enforcement,
and absence of sensitive logs.
