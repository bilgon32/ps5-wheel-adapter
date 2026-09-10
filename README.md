# PS5 wheel adapter with native Logitech G27 support

This firmware lets a Raspberry Pi Pico present an older Logitech wheel to a
PlayStation 5 as a Logitech G29. It uses a licensed PS4 specialty controller for
authentication and supports an optional standalone load-cell brake connected
through an Arduino Micro.

The project runs entirely over USB. It does not use Remote Play.

## Origin, credit, and license

This repository is a personal fork of
[jfedor2/wheel-adapter](https://github.com/jfedor2/wheel-adapter). The original
adapter design, PS5 authentication implementation, wiring instructions, photos,
and initial Logitech compatibility support were created by
[Jan Fedor](https://github.com/jfedor2). The original commit history and
dependency submodules are preserved.

This fork adds native G27 operation, standalone load-cell braking, a more robust
force-feedback bridge, and automated tests. Protocol details were cross-checked
against the Linux Logitech wheel driver and GIMX; the specific references and
control mapping are documented in
[docs/g27-protocol.md](docs/g27-protocol.md).

The upstream repository does not contain a project-wide license. This fork does
not relicense the upstream code or grant additional rights to it. File-level
copyright notices and the separate licenses of each dependency continue to
apply.

## Additions in this fork

- Native Logitech G27 mode with 14-bit steering input.
- Working clutch pedal and H-pattern gears 1–6, neutral, and reverse.
- Independent G27 wheel and shifter buttons instead of G25-style duplicates.
- Select + Start PlayStation-button shortcut that consumes both source buttons
  until release, preventing unwanted Share or Options menus.
- Optional Arduino Micro load-cell brake on the Rz axis.
- Ordered, retryable Logitech force-feedback forwarding, including the legacy
  PS5 output-report layout.
- Full-range pedal mapping without added curves, deadzones, or smoothing.
- Host tests for report decoding, pedal calibration, device routing,
  authentication arbitration, native-mode switching, and force feedback.

The G27 controls, clutch, shifter, external brake, PlayStation shortcut, and
force feedback have been tested with Gran Turismo 7 on PS5.

## Compatibility

| Device | Support in this fork |
| --- | --- |
| Logitech G27 | Fully tested in native mode, including inputs and force feedback |
| Logitech Driving Force Pro | Tested by the upstream author; retained through compatibility mode, not retested here |
| Logitech G25 and other older wheels with Driving Force compatibility mode | Expected to retain upstream basic-wheel support, not hardware-tested here |
| Logitech G29 as the physical wheel | Not supported; G29 is the identity emulated toward the console |
| Arduino Micro load-cell brake | Tested with the exact `2341:8037` HID profile described below |
| DualShock 4 for authentication | Not supported |
| Licensed PS4 specialty controllers | Required; fight pads and arcade sticks that work in PS5 games are typical candidates |

Game behavior still matters. In GT7, clutch operation requires manual
transmission mode and a car whose transmission supports a clutch. USB hub
compatibility also varies, so another hub is worth trying when downstream
devices fail to enumerate.

## What you need

- Raspberry Pi Pico or a compatible RP2040 board.
- USB Type-A female connector, from either a breakout board or a cut extension
  cable.
- USB hub for the wheel, authentication controller, and optional Arduino.
- Supported Logitech wheel with its normal power supply.
- Officially licensed PS4 specialty controller that works in PS5 games.
- Optional Arduino Micro exposing the supported load-cell HID report.
- Soldering equipment, or a Pico with headers, breadboard, breakout board, and
  jumper wires.

This fork is successfully tested with a **Hori Wired Controller Light for PS4**
for authentication. The Razer Raion was used by the upstream author. A standard
DualShock 4 does not satisfy this adapter's authentication requirement.

## Hardware assembly

Add a downstream USB port to the Pico using these connections:

| Raspberry Pi Pico | USB port or cable |
| --- | --- |
| GPIO0, physical pin 1 | D+; commonly green |
| GPIO1, physical pin 2 | D-; commonly white |
| VBUS, physical pin 40 | VBUS / 5 V |
| GND, physical pin 38 | Ground |

Cable colors are common conventions rather than guarantees; verify the cable or
breakout board labels.

![Two hardware assembly options](hardware.jpg)

The Pico's built-in USB connector is the upstream connection to the computer or
PS5. The Type-A port added above is the downstream host connection for the USB
hub.

![Adapter connection diagram](diagram.png)

## Build the firmware

The root-level `adapter.uf2` is the original upstream binary. It does not include
the native G27 or external-brake changes. Build this fork and use
`build/adapter.uf2` to get all features described here.

Clone the fork and initialize its pinned dependencies:

```sh
git clone --recurse-submodules https://github.com/bilgon32/ps5-wheel-adapter.git
cd ps5-wheel-adapter
```

For an existing clone:

```sh
git submodule update --init --recursive
```

Install CMake, Python 3, an Arm GNU toolchain, and a native C++ compiler for the
Pico build tools. Ninja is recommended. If `arm-none-eabi-gcc` is not on `PATH`,
set `PICO_TOOLCHAIN_PATH` to the Arm toolchain directory.

The pinned Pico SDK contains older host-tool projects. With CMake 4, set
`CMAKE_POLICY_VERSION_MINIMUM` before the first configure so the setting is
inherited by those projects:

```powershell
$env:CMAKE_POLICY_VERSION_MINIMUM = '3.5' # Windows PowerShell
```

```sh
export CMAKE_POLICY_VERSION_MINIMUM=3.5   # Linux or macOS
```

On Windows, open a Developer PowerShell so a Visual Studio C++ compiler is
available, then run:

```powershell
$env:PICO_TOOLCHAIN_PATH = 'C:\path\to\arm-none-eabi'
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

On Linux or macOS:

```sh
export PICO_TOOLCHAIN_PATH=/path/to/arm-none-eabi
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The resulting firmware is `build/adapter.uf2`.

## Flash and connect the adapter

1. Disconnect the Pico.
2. Hold its **BOOTSEL** button while connecting its built-in USB port to your
   computer.
3. Open the `RPI-RP2` drive that appears.
4. Copy `build/adapter.uf2` to that drive. The Pico automatically reboots.
5. Disconnect the Pico from the computer.
6. Connect the wheel and authentication controller to the downstream USB hub.
   Connect the Arduino Micro too if using the external brake.
7. Connect the hub to the Type-A host port added to the Pico.
8. Power the wheel, then connect the Pico's built-in USB port to the PS5.

The wheel should calibrate and center during startup. On a G27, the firmware
then switches from the `046d:c294` compatibility identity to the native
`046d:c29b` identity.

The Pico LED turns off when the console begins authentication and turns on after
the signed response is returned. During normal operation it should remain on
most of the time.

## G27 controls

Native mode enables the clutch, H-shifter, and independent wheel buttons. The
full mapping is in [docs/g27-protocol.md](docs/g27-protocol.md).

Press the second and third red shifter buttons, Select + Start, within 75 ms of
each other to send the PlayStation button. The firmware suppresses Select and
Start until both are released, so releasing one before the other cannot open
Share or Options. Pressing either button alone still works after the 75 ms chord
detection delay.

## Optional Arduino load-cell brake

External braking is enabled automatically only for this measured Arduino Micro
profile:

| HID property | Required value |
| --- | --- |
| USB vendor and product ID | `2341:8037` |
| Input report | Report ID `3`, 28 bytes including the ID |
| Brake axis | Generic Desktop Rz, usage `0x35` |
| Rz location | Bytes 16–17, zero-based and including the report ID |
| Encoding | Signed 16-bit little-endian |
| Default released endpoint | `-32767` |
| Default fully pressed endpoint | `32767` |

The adapter maps that input linearly onto the G29 brake range: `65535` released
and `0` fully pressed. It applies no response curve, deadzone, or smoothing.

To inspect the Arduino on Windows, connect it directly to the computer and run:

```powershell
.\tools\Inspect-Brake.ps1
.\tools\Inspect-Brake.ps1 -Seconds 10
```

The second command performs a read-only live capture. Start released, press the
pedal gradually to the intended maximum force, and release it. Compare the
reported minimum and maximum with the configured endpoints. The captured device
description is stored in
[tools/arduino-micro-hid.json](tools/arduino-micro-hid.json).

If the same HID layout uses different raw endpoints, edit these definitions in
[`src/brake.h`](src/brake.h):

```c
#define BRAKE_RELEASED_RAW (-32767)
#define BRAKE_PRESSED_RAW 32767
```

Swap the two values to invert the pedal direction. Rebuild and reflash after any
change. A device with a different VID, PID, report ID, report size, byte offset,
or encoding requires a corresponding source change; this is not a generic HID
pedal mapper.

When the Arduino is absent at boot, the wheel's original brake works normally.
Once the Arduino connects, it owns the brake input until the adapter reboots.
Disconnecting it or receiving no valid report for 500 ms safely releases the
brake. Reconnect it to resume, or reboot without it to return to the wheel pedal.
The Arduino firmware must send reports continuously, including while the pedal
is stationary; sketches that report only changes need a different timeout
policy.

## Force feedback

The adapter forwards the seven-byte Logitech force-feedback protocol without
scaling its values. It preserves command order, retries failed downstream
transfers, and blocks console commands that could switch the physical wheel out
of native mode. PS5 control transfers use a legacy packet layout, which is
handled as a fallback after normal G29 report parsing.

Native G27 mode provides 14-bit steering input. That is separate from motor
resolution: constant force remains 8-bit and the protocol's high-resolution
spring and damper parameters retain their original precision.

## Dependencies

The firmware uses pinned Git submodules:

| Dependency | Revision used by this repository | Purpose |
| --- | --- | --- |
| [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | `1.5.1` / `6a7db34ff63345a7badec79ebea3aaef1712f374` | RP2040 runtime and build system |
| [jfedor2/tinyusb](https://github.com/jfedor2/tinyusb) | `8ef98ab274065e29ba138cc6f2efa0dc68028db3` | USB device and host stack |
| [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) | `d00a10a8c425d0d40f81b87169102944b01f3bb3` | Downstream USB host on GPIO0/GPIO1 |

The validated local toolchain was Arm GNU Toolchain `13.2.Rel1`. The project
requires CMake 3.13 or newer, Python 3, and a host C++ compiler. Dependency
licenses and notices remain in their respective submodule directories.

## Run the host tests

The host tests do not require a connected wheel or Pico. On Windows, use a
Developer PowerShell with MSVC and Ninja:

```sh
cmake -S tests -B build-host-tests -G Ninja
cmake --build build-host-tests --parallel
ctest --test-dir build-host-tests --output-on-failure
```

The suite covers normal and inverted load-cell calibration, invalid and stale
reports, disconnect recovery, native G27 axes and buttons, gears and neutral,
packed G29 offsets, the PlayStation-button chord, force-feedback ordering and
overflow, failed sends, native-mode re-enumeration, and authentication
arbitration.
