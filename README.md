# PS5 adapter for old Logitech wheels

This is a personal fork of [jfedor2/wheel-adapter](https://github.com/jfedor2/wheel-adapter).
The original adapter and authentication implementation are by jfedor2. This fork
adds Arduino Micro load-cell brake support, Windows HID diagnostics, and tests.
The original commit history and dependency submodules are preserved.

No project-wide license was found upstream. This fork does not grant additional
rights to upstream code; existing file-level and dependency license notices still
apply.

This is an adapter that lets you use older Logitech wheels like the Driving Force Pro, G25 or G27 with PS5 games. It requires an additional controller belonging to the ["specialty peripheral"](https://blog.playstation.com/2020/08/03/playstation-5-answering-your-questions-on-compatible-ps4-peripherals-accessories/) class to satisfy the console's authentication requirements.

You will need:

* Raspberry Pi Pico.
* USB Type A female connector. Either an extension cable cut in half or a breakout board.
* USB hub. Unfortunately not all hubs work so you will have to try and see.
* Officially licensed PS4 controller that works in PS5 games by itself (not necessarily driving games). Arcade sticks and pad-shaped controllers specifically meant for fighting games are a good bet. I used a Razer Raion. A DualShock 4 will not work.

To make the device, either cut a USB extension cable in half and solder the wires to the right pins on the Pico, or if you don't want to solder, get a Pico with headers, a breadboard, a female USB Type A breakout board and some jumper wires. Make these connections:

| Pico | USB port or cable |
| ---: | -------- |
| GPIO0 (pin 1) | D+ (typically green) |
| GPIO1 (pin 2) | D- (typically white) |
| VBUS (pin 40) | VBUS/5V |
| GND (pin 38) | GND |

You're looking to make one of the things below:

![Two variants of the adapter](hardware.jpg)

Then flash the Pico with the firmware by doing the following. Press and hold the BOOTSEL button and while holding it, connect the Pico to your computer. A drive named "RPI-RP2" should appear. Copy the [adapter.uf2](adapter.uf2) file to that drive. That's it.

Then plug a USB hub into the port you added to the Pico and plug both your wheel and the controller you want to use for authentication into the hub. Then connect the Pico to your PS5.

![Diagram of the connections](diagram.png)

The wheel will operate in Driving Force compatibility mode so even if your wheel has a PlayStation button, it probably won't work. Pressing the "select" and "start" buttons at the same time works as the PlayStation button. If your wheel has more buttons than the basic 12, you probably won't be able to use them as unique buttons (they will work as duplicates of the standard ones).

## External Arduino Micro brake

The source now supports the inspected Arduino Micro load-cell brake as a third
device on the USB hub, alongside the wheel and authentication controller. Build
this source and flash the resulting `build/adapter.uf2`; the original root-level
`adapter.uf2` is unchanged and does **not** include external brake support.

This is a specific device profile, not support for arbitrary Arduino sketches:

| Property | Observed value |
| --- | --- |
| USB vendor/product | `2341:8037` |
| HID input report | ID `3`, 28 bytes including the ID |
| Brake axis | Generic Desktop Rz (`0x35`) |
| Rz position | Bytes 16–17, zero-based, including the ID |
| Encoding | Signed 16-bit little-endian, logical range -32767 to 32767 |
| Released value | -32767, observed in live idle reports |

The existing Arduino calibration is retained: -32767 means released and 32767
means full braking. The adapter maps this to G29's 65535-to-0 pedal range without
an additional curve, deadzone, or smoothing. The full-force endpoint is taken
from the HID logical range; a physical full-force sweep has not yet been tested.
Raw endpoints can be changed with `BRAKE_RELEASED_RAW` and `BRAKE_PRESSED_RAW` in
`src/brake.h` (or compiler definitions). Reversing them inverts the pedal.

Before the Arduino connects, the wheel's original brake operates normally. Once
the Arduino connects, its brake takes priority and wheel reports cannot overwrite
it. Disconnecting the Arduino, or receiving no valid report for 500 ms, releases
the brake. The adapter keeps external-brake selection until reboot; reconnecting
resumes operation on the next valid sample. Reboot without the Arduino to return
to the original pedal. This timeout is appropriate for the inspected sketch,
which sends reports continuously even at rest (233 reports in a three-second
capture); sketches that only send changes need a different policy.

Only the matching device/interface can update the brake, and packets with the
wrong report ID or length are ignored. Arduino HID interfaces are excluded from
authentication selection. Other devices still use the original convention that
a non-wheel device is the authentication controller; avoid additional unrelated
HID peripherals on the hub. The licensed authentication controller is still
required. Wheel native mode, clutch, shifter, and pedal curves are unchanged.

On Windows, inspect the device without additional packages or driver changes:

```powershell
./tools/Inspect-Brake.ps1          # Axis capabilities and Rz offset
./tools/Inspect-Brake.ps1 -Seconds 10  # Read-only live report capture
```

`tools/arduino-micro-hid.json` records the inspected capabilities and idle sample.
The tool never opens the serial port or writes reports to the Arduino.

Host tests (use a Developer PowerShell on Windows with MSVC and Ninja):

```sh
cmake -S tests -B build-host-tests -G Ninja
cmake --build build-host-tests
ctest --test-dir build-host-tests --output-on-failure
```

Tests cover signed decoding, calibration and inversion, source priority, invalid
reports, stale input, timer rollover, disconnect/reconnect, device routing, and USB
queue retries. Initial hardware testing by the fork maintainer confirmed working
load-cell braking with the new firmware. Extended periodic authentication testing
is still pending.

For firmware builds with CMake 4, the pinned SDK's older host tools need
`CMAKE_POLICY_VERSION_MINIMUM=3.5` in the environment and
`"-DCMAKE_POLICY_VERSION_MINIMUM=3.5"` on the CMake configure command. On Windows,
use Developer PowerShell with Ninja, Python on PATH, and `PICO_TOOLCHAIN_PATH`
pointing to the Arm GNU toolchain directory. This firmware was built with Arm GNU
13.2.Rel1 and the repository's pinned submodule revisions.

## Q&A

**Will a DualShock 4 work?**

No.

**Does it suffer from the 8 minute timeout?**

Not if you connect the right controller for authentication.

**Does force feedback work?**

Yes.

**What wheels did you actually test it with?**

A Driving Force Pro, but I'm optimistic about the others because they all have a Driving Force compatibility mode.

**What does the LED mean?**

The LED turns on after we successfully send the authentication data to the console and turns off again the next time the console initiates the authentication process. So if everything is working correctly, the LED should be on most of the time.

**Can I use a RP2040-based board other than the Pico?**

Yes.

**Can I use Adafruit's Feather RP2040 USB Host board?**

In theory it should work, but when I tried it some weird things happened, not sure why.

**Does it use Remote Play?**

No.

**Could it be modified to support other wheels?**

Yes.

**Could it be modified to support standalone pedals and shifters?**

Yes.

**Could it be modified to support DIY peripherals connected to GPIO pins?**

Yes.

**Could it be modified to support non-linear curves on the pedals?**

Yes.

**Would it work with a PS4?**

Probably.

**How do I compile the firmware?**

```
git clone https://github.com/jfedor2/wheel-adapter
cd wheel-adapter
git submodule update --init
mkdir build
cd build
cmake ..
make
```
