<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# CipherPort Web Manager

This directory contains the dependency-free management interface hosted by the
AI Passport firmware. `dist/` is the approved CipherPort Admin source and
`firmware/index.html` is its self-contained production bundle. It preserves the sidebar, sticky page headings, vault
detail view, and two-column device-settings layout.

Pack the page for firmware with:

```text
node tools/build_web_firmware.mjs
node tools/pack_web_assets.mjs web-manager/firmware/index.html main/web_assets/index.html.gz
```

The firmware exposes a fixed `CIPHERPORT` WPA2 SoftAP name and generates a new
eight-digit numeric password every time Web Management starts. One client may
connect. Browser authorization requires one physical `OK` confirmation; account
mutations remain in RAM until the user selects `APPROVE` or `CANCEL` and presses
`OK` on the device.

Security invariants:

- no localStorage, IndexedDB, cookies, analytics, or remote assets;
- no PIN or recovery-code entry in the browser;
- session tokens exist only in RAM and are invalidated on explicit exit;
- a transient Wi-Fi reconnect does not end an authorized management session;
- responses use `Cache-Control: no-store` and restrictive security headers.
