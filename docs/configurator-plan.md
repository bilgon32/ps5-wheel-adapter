# G27 Studio implementation plan

The feature branch is `codex/g27-configurator`. Keep the G27-to-G29 bridge and
licensed PS4 specialty-controller authentication. Other wheels and handbrakes
are outside this milestone. Master remains untouched.

## Deliverables

- [x] Separate configuration, HID-axis discovery, device roles, and PC protocol
      from the console bridge. Preserve working G27 controls, chords and FFB.
- [x] Versioned settings with calibration and an imported LUT in reserved flash.
- [x] PC setup USB identity, device discovery and live measurements through Pico.
- [x] Downloadable Windows desktop application with guided setup, live graphs,
      configuration backup, custom UF2 export and Pico flashing.
- [x] Firmware and desktop tests, actual firmware build and UI rendering checks.
- [x] Release automation for firmware and packaged Windows application.
- [x] Simple README, new connection/wiring graphics and real GUI screenshots.
- [ ] Logical commits pushed to origin; no merge into master.

## Architecture

G27-specific initialization and Logitech force transport stay intact. Separate
modules own settings encoding/validation, flash storage, HID descriptor parsing,
device registry and the PC request/reply protocol. PC setup uses a distinct HID
identity; console mode retains the existing G29 descriptors. The existing G29
F3 feature report accepts a narrowly defined maintenance-mode request from the
desktop application. No new interface is appended to the console identity.

The app runs locally in a desktop webview and talks to Pico through HIDAPI.
Peripherals remain on Pico's downstream hub. Live charts use Pico measurements,
with a separately labelled demo for exploration without hardware.

Personalized UF2 files combine bundled firmware with a validated settings block
in a reserved flash sector. The desktop app never invokes a compiler. Normal
settings changes can be saved directly from setup mode. Firmware updates retain
settings unless a personalized image explicitly replaces them.

## Verification gates

Host tests must cover signed and unaligned HID axes, malformed descriptors,
calibration inversion/clamping, role isolation, unplugged pedals, settings
corruption, LUT validation, protocol bounds and preservation of console behavior.
Desktop tests must exercise binary compatibility with firmware, UF2 block/address
validation, Pico-drive detection, HID request/reply correlation and failure paths.
UI checks must exercise setup, calibration, LUT import and responsive live charts
and verify no demo measurements appear as real connected devices.

Hardware-only validation remains explicit: Pico enumeration, downstream devices,
configuration save/reboot, PC-to-console transition, and GT7 authentication/FFB.
Do not claim those checks passed without observed hardware evidence.

## Software validation

Local checks on 2026-09-30 passed: the RP2040 firmware build, all seven native
test suites, 12 desktop tests including the real C protocol fixture, automated
Edge UI interactions, and startup of the packaged Windows executable with its
bundled firmware. Settings tests include interrupted flash writes and recovery
from the previous bank. Screenshots show the actual application in demo mode.

Hardware testing is deferred at the user's request. This remains a preview until
the new setup workflow and console behavior have been checked on the Pico.
