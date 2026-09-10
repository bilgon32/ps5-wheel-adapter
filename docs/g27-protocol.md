# G27 native support and validation

The firmware requests `046d:c29b` native mode when the target G27 appears as the
Logitech `046d:c294` compatibility device. A wheel already in native mode is
accepted directly. If the switch is rejected or times out, the firmware resumes
the original compatibility decoder. The console-facing identity remains G29.

## Inputs

Native reports are 11 bytes without a report ID. Steering is 14 bits at bit 26;
accelerator, original brake and clutch are bytes 5, 6 and 7. These values are
scaled across the full G29 range. Scaling preserves all source values; it does
not turn an 8-bit pedal sensor into a 16-bit sensor. Arduino braking retains its
existing independent source selection, calibration and stale-input handling.

Forward gears use byte 2 bits 0–5; neutral clears all gear bits. Reverse requires
the push-down flag and the right/down shifter gate, avoiding reverse when the
stick is only pushed down in neutral. G29 reverse is bit 7 at payload offset 50.
The D-pad direction numbering and neutral value remain unchanged.

| Native G27 control | G29 control |
| --- | --- |
| Left / right paddles | L1 / R1 |
| Left / right upper wheel buttons | L2 / R2 |
| L4 / R4 extra wheel buttons | Minus / plus |
| L5 / R5 extra wheel buttons | Dial down / dial up |
| Black diamond: left / bottom / right / top | Square / cross / circle / triangle |
| Red buttons 1–4, left to right | L3 / select / start / R3 |
| Red buttons 2+3 together | PS |
| H-shifter | Gears 1–6, neutral, reverse |

Each mapped control is independent. Game assignments are configured in the G29
controls screen. Select and Start are delayed by 75 ms when pressed alone. If
the other button joins during that window, the firmware reports only PS and
suppresses both source buttons until both are released. This prevents a
staggered chord release from opening Share or Options after the PS menu.

## Force feedback

The original adapter held only the latest command, suppressed identical commands,
and gated wheel output behind console input readiness. This could lose effect
setup, updates or stops before they reached the wheel. The new path uses an
ordered 128-command FIFO, retries rejected submissions, and removes a command
only after a successful output completion. It accepts report 5 through
interrupt OUT and control SET_REPORT regardless of the host's report-type
label, matching the original adapter's permissive callback. Repeated commands
are retained. If the transfer metadata does not identify report 5, the parser
falls back to the original adapter's proven layout and reads the seven-byte
wheel command starting at byte 1. The PS5 LED diagnostic confirmed that this
fallback is required on the target console.

Every seven-byte force command is forwarded unchanged, preserving constant and
variable force values and the high-resolution spring/damper parameters. There
is no gain boost or invented increase in motor resolution. The protocol's
constant-force magnitude is 8-bit; high-resolution spring deadband parameters
have 11 bits. Steering resolution is separate and increases from compatibility
mode's 10 bits to native mode's 14 bits.

Mode-switch commands from the console are blocked so they cannot change the
physical wheel's identity. Rotation-range and RPM LED commands pass through.
Initialization stops old effects, disables wheel autocenter, sets a default
900-degree range, and clears LEDs before draining queued game commands. Native
mode switching disables autocenter, requests revert-on-reset, and selects G27
identity using interrupt OUT, with 20 ms between commands and no interrupt IN
transfer armed during the sequence. The wheel then disconnects and re-enumerates
as `046d:c29b`. This ordering was verified on the target hardware. Once ready,
input and force-feedback transfers run concurrently on their separate interrupt
endpoints, so an unchanged input report cannot stall force output. Subsequent
game commands can change the range and effects.

The queue is bounded: overflow drops the newest command and increments a counter.
UART output at 921600 baud on Pico GPIO16 reports received and sent commands,
queue depth, submission retries, overflow and blocked mode changes.

## Testing and flashing

Five host test executables cover brake calibration and inversion, native axes
and buttons, gears and neutral, packed G29 offsets, queue ordering and overflow,
failed sends, mode-switch re-enumeration, and authentication arbitration. The
ARM Release build succeeds against the repository's pinned dependencies.

Clutch, gears, independent buttons and force feedback have been validated on a
physical G27 connected to GT7 on PS5. In GT7, select MT and use a car with a
compatible manual transmission; some transmission types do not support a
clutch, as described in the [official GT7 manual](https://eu.gran-turismo.com/br/gt7/manual/controller/02).

## Protocol references and credit

Original adapter and authentication: [jfedor2/wheel-adapter](https://github.com/jfedor2/wheel-adapter).
Protocol facts were cross-checked against the following primary implementations;
their copyright and license notices remain with those projects. Reference
implementations were not imported as dependencies or copied into this fork.

- [Linux Logitech wheel driver](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-lg4ff.c): revision identification and native-mode commands.
- [GIMX native G27 report](https://github.com/matlo/GIMX/blob/e44a1cc9823939165d3d92418e9d679c66496ee1/shared/gimxcontroller/include/g27_ps3.h) and [control masks](https://github.com/matlo/GIMX/blob/e44a1cc9823939165d3d92418e9d679c66496ee1/shared/gimxcontroller/src/g27_ps3.c), by Mathieu Laurendeau.
- [GIMX G29 report](https://github.com/matlo/GIMX/blob/e44a1cc9823939165d3d92418e9d679c66496ee1/shared/gimxcontroller/include/g29_ps4.h) and [G29 controls](https://github.com/matlo/GIMX/blob/e44a1cc9823939165d3d92418e9d679c66496ee1/shared/gimxcontroller/src/g29_ps4.c).
- [GIMX Logitech FFB definitions](https://github.com/matlo/GIMX/blob/e44a1cc9823939165d3d92418e9d679c66496ee1/core/haptic/common/ff_lg.h) and [G29 PS4 startup analysis](https://gist.github.com/matlo/8b343449a95ef9672be2).
