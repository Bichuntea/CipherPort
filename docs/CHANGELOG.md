<p align="right">
  <a href="CHANGELOG.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Changelog

## Unreleased

- Removed the date from the device home page, the clock from the device status
  bar, and the complete Web time-synchronization card and API. Password,
  language, Wi-Fi, display, and session behavior are unchanged.

- Added long-OK screen-off shortcuts exclusively to the home and saved-account
  list screens, with explicit bottom-of-screen hints on both pages. The account
  list uses a two-line hint panel so navigation, open, settings, and screen-off
  controls all remain visible. Wake-up now redraws and flushes the home screen
  while the LCD and backlight remain off, preventing the previously visible
  page from flashing before the home screen appears.

- Enlarged saved-account cards and their usable text width and moved notes
  clear of the lower border. User-supplied CJK account titles and notes now use
  one comprehensive static glyph source with no transforms or perpetual
  animations, avoiding mixed font metrics and post-save UI stalls on the
  no-PSRAM device. Removed the small caption below the one-time eight-digit
  recovery code. The Web lock page captures the logo as an inline image before
  disconnecting, so it remains visible after Wi-Fi and the server shut down.

- Regenerated the 19 px device heading font from every current UI string, so
  account-detail headings no longer mix 19 px glyphs with 14 px fallback
  glyphs. Authorized Web management now remains active independently of the
  shorter display auto-lock setting, with a 10-minute authenticated inactivity
  safety limit, allowing multiple records to be added in one session.

- Restored explicit `OK START` / `OK STOP` hints beside the Web-management
  back hint, added a long-OK return from Settings, and made the Web manager
  read the device language before approval so its first visible dialog follows
  the language selected during device onboarding.

- Added a persistent first-boot language chooser before device enrollment.
  Web management can now be closed from every Web state and returns to the
  originating Settings or empty-vault screen. Mixed English/Chinese controls
  use the CJK fallback font, About metadata uses supported ASCII separators,
  and the browser-confirmation action no longer renders stray square glyphs.

- Enabled LVGL compressed-font decoding for the device's 14 px body and 19 px
  title Chinese fonts. Their glyph data was already embedded, but the disabled
  decoder caused Chinese labels to render as blank boxes.

- Reworked the device About page around the `Cipherport` name and `Password
  Manager` function, mirroring the Web sidebar repository, author, and version
  metadata. Chinese UI typography now has distinct 12 px small, 14 px body,
  and 19 px title tiers so headings retain the same visual hierarchy as the
  20/22 px English headings.

- Made the device language row show both language choices in both modes, with
  the active language listed first. Web time synchronization now commits the epoch
  and time-zone offset together in NVS; a restart restores the last calibrated
  time instead of returning to the unsynchronized baseline.

- Replaced auto-lock deep sleep with a secure display-off state: the vault and
  Web session are closed, the LCD panel and backlight turn off, and one UP,
  DOWN, or OK action wakes the device. This avoids the former repeated hardware
  power-button sequence after automatic screen-off.

- Fixed boot-time clock initialization so it no longer overwrites the stored
  time zone or substitutes the firmware build time. An unsynchronized device
  now starts at local `2000-01-01 00:00`; the Web manager can synchronize the
  managing device's time or save a custom local time from 2000 through 2100.

- Added a segmented 4 MB factory layout for new devices and a separate 3 MB
  application-only upgrade path for existing devices. Both preserve
  `cardid@0x356000`; package validation rejects protected-range writes and
  enforces the 6.5 MB delivery limit. A partition-aware flasher rejects
  mismatched devices before writing.

- Added Web controls to read device time, synchronize from the managing phone
  or computer, or set a custom local time. The device stores the time-zone
  offset and refreshes its clock display after synchronization. Release
  verification now caps the distributable merged image at 6.5 MB within the
  unchanged 8 MB Flash and protected 3 MB application layout.

- Set the Web record generator to 12 mixed characters; the Wi-Fi management
  password remains a separate, freshly generated eight-digit number.

- Web welcome-message and language saves now wait for the device's NVS commit
  before reporting success; failed writes are reported to the browser.

- Added a persistent Web English/Simplified Chinese selector backed by the
  device language setting. Welcome text now updates the runtime view after
  storage confirmation, verifies the saved UTF-8 value, and uses LVGL's Source Han font
  first with the complete Noto CJK font as a fallback to prevent missing-glyph
  boxes.

- Fixed device-home saving for valid UTF-8 Chinese text and select the CJK font
  automatically for user-provided Chinese content. Web display settings now
  persist on the device, and the configured inactivity timeout securely locks
  the vault and turns off the panel/backlight while retaining function-key wake.

- Added a persistent English/Simplified Chinese device-language setting with
  complete CJK glyph coverage. Account details now show the title, account, and
  optional note, while empty notes no longer render a `NO NOTE` placeholder.

- Fixed firmware packaging to regenerate and verify the embedded compressed Web
  asset together with its HTML source, preventing repaired pages from compiling
  against a stale broken script. The connected-device screen now prints the
  complete `http://192.168.4.1/` management URL.

- Made the embedded Web bootstrap request device authorization independently,
  moved the original logo to a separate resource so the executable page is only
  about 16 KB compressed, and added compatibility fallbacks for mobile browsers.
  Fixed a bundling substitution that corrupted the JavaScript selector helper
  and disabled every Web control. The device connection window is 120 seconds;
  the authorized Web management session remains five minutes.

- Restored the original Web layout from the dedicated CipherPort Web design
  task, raised the mobile HTTP request-header limit, separated Wi-Fi association
  from browser authorization so new-record approval is not lost, and stop the
  device countdown immediately after the single client joins the SoftAP.

- Hid the PIN-attempt counter during normal entry so remaining tries appear
  only on the error screen, and aligned every Web countdown/connected `STOP`
  action with the same filled selected-button style used by other screens.

- Fixed device-hosted Web access after joining the SoftAP: the embedded bundle
  now always uses the real device API, unknown captive-portal paths redirect to
  the manager, and an authorized browser synchronizes the device clock. The
  status clock now starts from the deterministic 2000-01-01 baseline and refreshes each
  minute instead of remaining blank or stale.

- Restored the previously approved CipherPort Admin Web source and now bundles
  that exact responsive layout, behavior, and logo into the firmware. Added
  real device-backed account notes and Web CRUD, five-minute connection setup,
  Web requests for device-side PIN change/erase, simplified account screens,
  and page-local button-event handling.

- Fixed first-enrollment unlock after flashing a merged image: orphaned vault
  ciphertext is now cleared by the background storage task before a new vault
  key is created. PIN entry and rejection screens now show a short, dynamic
  remaining-attempt count that fits the device display.

- Fixed Web Management layout overlap by separating connection details from
  the countdown region. The SoftAP now keeps the fixed `CIPHERPORT` SSID while
  generating a new eight-digit numeric password on every start. Authorized Web
  sessions survive transient client reconnects and remain usable after account
  writes; explicit device/browser exit still closes Wi-Fi and invalidates the
  memory-only token. Restored the established sticky-header, two-column
  CipherPort Admin settings layout.

- Synchronized the production UI with Figma `02 Device Screens`: attached the
  battery cap to its outline, removed empty key-hint frames, wrapped first-account
  guidance, enlarged the one-time eight-digit recovery code, and made every
  single- and two-action screen respond only to its visible controls. Browser
  authorization now requires one device-side `OK` with no second PIN prompt.
  Rebuilt the embedded English-only Web Manager in the established CipherPort
  Admin layout while retaining live account, welcome-message, and physically
  approved mutation APIs.

- Refined the English-only device and Web Manager UI: removed language
  switching, duplicate and clipped hints, detached the battery fill from its
  outline, and replaced PIN entry boxes with unframed indicators. PIN entry
  now exposes digits, `CLEAR`, and `CONTINUE` in one focus loop; early submit
  shows an incomplete-entry error. Web Wi-Fi now uses an eight-digit random
  numeric password, starts only from Web Management, accepts one device, drops
  the waiting countdown after authorization, and stops when that UI is exited.
  The eight-digit super-access code is shown once and saved without re-entry.

- Fixed the shared PIN/recovery digit selector to use the proven built-in digit
  font, expose real CLEAR/CONTINUE focus states, support double-OK delete and
  long-OK back, and apply the same behavior to every secret-entry screen.
  Replaced the percentage battery display with icon-only four-state rendering,
  added an empty-vault first-account Web onboarding path, made the Web-managed
  bilingual welcome message adaptive and scrollable, and standardized password
  and Web session countdowns on an absolute deadline and the canonical bar.

- Rebuilt the device UI against the updated 67-state Figma flow, corrected the
  Chinese glyph set and title sizing, and completed first-run PIN/recovery,
  lockout, settings, battery/time, Web authorization, approval, and erase
  states. Added PIN- and recovery-wrapped random vault keys, authenticated
  two-generation atomic vault storage, real account browsing/reveal, and a
  bilingual device-hosted Web Manager with device-approved account CRUD.

- Integrated a compact bilingual CipherPort firmware shell and Web Manager: the
  shared English/Simplified Chinese preference is persisted through a bounded
  NVS worker, SoftAP and HTTP run only after device activation, the embedded Web
  UI is a 2.4 KiB gzip asset with no demo credentials, Bluetooth is removed from
  the production build, and a protected-layout-verified 2 MiB vault partition is
  reserved after `cardid`.

- Refined the CipherPort Web Manager preview with dismissible portrait navigation, no idle selection highlight in the portrait record list, click-to-extend sessions, a circular help control, record notes and device-order controls, random-by-default or fixed SoftAP passwords, timed auto-lock without an enable switch, all-key sounds, device-home content management, device-side PIN change, and physically confirmed data erasure.

- Defined the password-manager firmware requirements and aligned them with the new Figma device and basic Web Manager flows: fixed four-digit PIN enrollment and change, account/reveal/settings/web-management navigation, five-attempt PIN lockout, three-attempt recovery disablement, timed password reveal and scrolling, physically approved atomic web mutations, runtime ownership, security boundaries, and unresolved screen/hardware/storage/update decisions. The initial scope excludes an authenticator, activity log, categories, favorites, saved dates, and notes.

- Added the supplied 80-byte CW2017 profile for the specified 520 mAh cell, including content/update-flag checks, verified writes, the required restart sequence, and bounded SOC-readiness polling.

- Expanded the environment bootstrap document: added Espressif's Git service mirror (`git.espressif.com.cn`) as the preferred mainland-China route for ESP-IDF v5.5.3 and its submodules, documented submodule long-wait/timeout handling, in-place repair, and the pinned-commit shallow fetch for large submodules such as `esp32-wifi-lib`, warned about stale per-repository Jihulab `insteadOf` residue, and added the official offline release archive as a last-resort fallback (learned from `esp-mosaico/esp-mosaico-vibe`).

- Reorganized the documentation by function area with a dual entry point: the root `AGENTS.md` is now a thin router (hard constraints + task routing only) and the detailed AI workflow lives in `docs/development/ai-guide.md`; `agent-guide.md` was folded in. `docs/development/` gained a second level (`engineering/`, `ci/`, `release/`), and the `plays/` application archive and `experiences/` moved into a `docs/reference/` area with a dedicated README. Removed `docs/software-design/` (empty scaffold); folded the three `assets/{fonts,images,music}/README` leaves into the `assets/` README; flattened the six `project-completion` sub-documents into a single file; and unified each directory to a single README, eliminating every `INDEX` file and a duplicated experience index. All cross-references and bibliographic links were updated; no content was dropped.

- Removed the obsolete app/test partition at `0x700000` and its related
  bootloader, validation, and documentation requirements. The fixed protected
  `cardid` partition and its CI checks remain unchanged.
- Documented a release-title convention for multi-app releases: name tags as `v<version>-<app-name>` (e.g. `v0.1.0-voice-keychain`) so the release title carries the version and the app, and confirm the title after the release is published so a release list is scannable by app.
- Added a post-release follow-up workflow: an `issue-suggestions` skill for filing user feedback as issues against the upstream project, an `experience-pr` skill for submitting reusable development experience as a documentation PR, a `docs/experiences/` directory for per-entry experience files, and supporting `project-completion`, `file-issues`, and experience-index documents.
- Simplified the tracked repository root: moved GitHub-recognized community documents into `.github/`, moved the changelog into `docs/`, updated every reference, and added a root-document allowlist to repository checks.
- Repository-wide language policy: every maintained Markdown default `.md` file is English, Simplified Chinese uses a paired `.zh_CN.md`, and both provide language switches. Static checks reject missing peers, missing switches, and Chinese prose in English defaults.
- Phase one of the AI development workflow: streamlined task-based context routing, unified local/CI validation, added PR checks and a template, and committed the dependency lock for reproducible builds.
- PR review fixes: pinned GitHub Actions to full commit SHAs, split build/release jobs by least privilege, disabled persisted sync checkout credentials, added Feature Request and Usage Question forms, clarified private security-report fallback, and corrected stale README, CI-trigger, and branch descriptions.
- Changed commit titles, PR titles, and PR bodies from Chinese-default to English; updated the Chinese punctuation rule so it no longer applies to PR descriptions.
- Reworked `build-firmware.yml` to pass `SDKCONFIG_DEFAULTS=sdkconfig.defaults`, enable `partitions.csv`, preserve the 8 MB image header, merge a flashable `FoloToy-AI-Passport-full.bin`, publish only that artifact, and use Actions cache v5.
- Integrated upstream PR #6 to resolve PR #4 conflicts: Wi-Fi, Bluetooth LE, radio lifecycle, and low-power demos; a 3 MB factory partition; build/menu/configuration updates; hardware-guide coverage; and bilingual capability tables.
- Defined English imperative Conventional Commit formatting for both commits and PR titles.
- Removed stale sync-workflow template comments and generalized an irrelevant Redis TTL rule to cache components.
- Added Chinese punctuation, credential safety, and recoverable file-deletion conventions.
- Expanded source-comment requirements for functions, state, ownership, concurrency, timing, registers, and magic values.
- Removed AI execution instructions from product READMEs so they remain human-facing product and repository overviews.
- Added `docs/development/agent-guide.md` as the focused AI workflow guide.
- Updated `AGENTS.md`, `docs/INDEX.md`, and the development index for the agent guide.
- Documented why the root README path is reserved for fork owners and how GitHub README precedence supports it.
- Created `main-update` from the upstream-aligned baseline and combined the repository-structure, firmware-CI, and upstream-sync work.
- Corrected the merged documentation index, workflow path, project tree, and CI references.
- Moved CI documentation from software design to `docs/development/`.
- Moved fork-only documentation assets from `assets/docs/` to `docs/assets/`.
- Moved the upstream English/Chinese project READMEs under `docs/` and renamed the documentation catalog to `docs/INDEX.md`.
- Initialized `AGENTS.md`, `CLAUDE.md`, and `CHANGELOG.md`.
- Standardized the initial project README language filenames.
- Added the `docs/`, `assets/`, and `skills/` directory structure.
- Moved the upstream hardware guide into `docs/hardware-design/`.
- Standardized subdirectory README capitalization and introduced fork conventions.
- Allowed fork-owned root README and supplemental documentation content on fork `main`.
- Added and documented the fork-only supplemental-document directory.
- Moved the build CI document to its dedicated CI branch before consolidation.
- Documented clean-`main` reasons, the direct-development exception, and Actions enablement for forks.
- Split the original agent rules into contribution, development, and fork documents with a compact root index.
- Updated software-design and project README references for the new documentation structure.
- Added the documentation catalog and task-triggered routing based on the earlier repository model.
- Added bilingual contribution, code-of-conduct, security, and support documents tailored to this ESP-IDF and fork workflow.
