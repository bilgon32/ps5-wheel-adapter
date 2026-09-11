# Compensating Logitech gear-drive force-feedback deadzones

## Decision

The adapter can compensate the G27's weak response around zero torque without changing the PS5-facing HID device or the wheel's native mode. The safest first version is a small set of selectable output profiles that transform only the level bytes in Logitech **constant-force** download and refresh commands. Range, RPM LEDs, spring, damper, friction, authentication, and wheel-mode commands should remain byte-for-byte unchanged.

The recommended initial profiles are:

1. **Linear** — unchanged output, for comparison and troubleshooting.
2. **Minimum force 12%** — the default; every nonzero torque starts just above 12% and the remaining range is rescaled to retain 100% output.
3. **Minimum force 18%** — a stronger G27 setting for units with more friction or gear lash.
4. **Progressive** — a continuous low-force boost that is gentler around zero than a fixed minimum-force jump.
5. **G27 measured LUT** — the inverse response supplied from this project's physical G27, sampled into the protocol's 127 magnitude steps.

Cycle profiles by holding the two outer red shifter buttons (G27 L3 + R3) for one second. Consume both buttons once the chord triggers, blink the Pico's onboard LED one to four times, and save the chosen profile in the last flash sector. On boot, restore the latest valid record; if none exists, select Minimum force 12%.

This design is viable on the present RP2040 firmware. A measured, per-wheel inverse LUT is the best final solution. The supplied `g27.lut` is valid, strictly monotonic, and has 101 points from `0|0` to `1|1`; it is included as the fifth profile. A LUT copied from another G27 is only a preset: measurements reported by owners vary substantially even within the same model.

## What the deadzone is

The symptom is often called a *center deadzone*, but there are two different phenomena:

- A **torque deadzone** is a band of small force commands around zero that cannot overcome motor, bearing, and drivetrain friction. It can be felt most often while driving straight because the simulator is requesting small alternating torques there, but it is tied to force magnitude rather than steering angle.
- A **position deadzone or gear-lash region** is physical free movement around a direction reversal. Software can make the motors take up that slack more decisively, but cannot recover motion or torque that the mechanism cannot transmit.

The G27 uses two motors and helical gearing.[^1] That construction is durable and produces useful peak torque, but gears, preload, and static friction make very small commands ineffective. iRacing introduced a minimum-force offset specifically for wheels with a neutral force deadzone and documented typical values of 5–15%.[^2] Its current calibration guidance also groups the G25, G27, G29, G920, G923, and DFGT as roughly 2.2 Nm devices and explains that its nonlinear mode boosts small forces for these lower-end gear-driven wheels.[^3]

Community WheelCheck measurements are less controlled but useful for model-specific scale. One long-running G27 guide reports 12–22% across four G27 samples and recommends roughly 10–20%.[^4] This variance is the main reason to ship multiple conservative profiles and later support a measured LUT instead of declaring one universal G27 value.

## What the adapter can safely change

The G27 and the PC/PS4 forms of the G29 use Logitech's seven-byte classic force-feedback protocol. GIMX identifies four effect slots, separates the command nibble from the slot mask, and defines download, download-and-play, play, stop, and refresh operations.[^5] It also identifies force types for constant force, spring, damper, autocenter, periodic effects, high-resolution conditions, and friction.[^6] The Linux `hid-lg4ff` driver uses the protocol's variable-force type to produce steady torque: `0x80` is neutral and its command is `11 08 <level> 80 00 00 00`.[^7] This is also the command shape that successfully moved the G27 in this adapter's earlier bench test.

This matters because the five parameter bytes do not always mean torque. For a spring they include dead-band limits, coefficient selectors, slope flags, and a clip level; high-resolution spring and damper packets pack still more fields into individual bits.[^6] Applying a generic curve to every byte would alter effect geometry and could create dangerous or nonsensical output.

The shaper must recognize both steady-torque representations:

- For nominal constant-force type `0x00`, the header's upper nibble selects one or more of four slots and each selected slot uses its corresponding level byte.
- For variable-force type `0x08`, two initial levels are safe to reshape only when both packed step/time bytes are zero. Nonzero step fields describe a waveform that the wheel continues generating internally; changing only its initial value would distort it unpredictably.

The adapter can therefore:

1. Validate a seven-byte report as it does now.
2. Recognize command values 0x0 (download), 0x1 (download-and-play), and 0xC (refresh), while preserving the upper slot bits.
3. Require type `0x00`, or type `0x08` with zero step/time fields.
4. Transform only the identified level bytes.
5. Queue and transmit the resulting seven bytes normally.

`PLAY` and `STOP` commands do not replace force parameters and should pass through. Extended range, identity, and RPM LED commands must also pass through, subject to the adapter's existing protection against console-requested identity changes.

The protocol has two neutral encodings, 127 and 128, in GIMX's conversion routine.[^8] Both should remain neutral. Values 0–126 represent one direction and 129–255 the other, with 127 usable magnitude steps on each side. Integer transforms are sufficient; a 128-entry magnitude LUT exactly matches the effective command resolution.

## Candidate compensation methods

### Fixed minimum-force offset

For normalized input magnitude `x` in `(0, 1]` and minimum force `m`:

```text
y = m + (1 - m) * x
```

Zero stays zero. The rescaling preserves full-scale output, unlike simply adding `m`, which clips the upper part of the signal. This is the closest match to iRacing's documented base offset and is cheap, deterministic, and easy to compare.

Its weakness is unavoidable: output jumps from zero to slightly above `m`. If the game alternates tiny positive and negative values, the wheel can chatter or oscillate. A lower profile (12%) and a higher profile (18%) let the driver find the smallest value that reliably moves the wheel. The correct setting is the first value that overcomes the physical threshold, rather than the strongest-feeling value.

### Progressive or gamma boost

A power curve such as `y = x^0.65` is continuous at zero and boosts the low and middle range while preserving zero and full scale. Assetto Corsa's controller tool describes gamma below 1.0 in the same way: smaller forces are boosted.[^9]

This feels smoother around sign changes than a hard offset, but it is not a complete deadzone inverse. The smallest outputs can still fall below the wheel's static-friction threshold. It is valuable as a comparison profile and may feel more natural in GT7 when a hard 18% floor produces rattle.

### Measured inverse LUT

WheelCheck's Step Log 2 test sends increasing forces and records wheel movement. Open tools then normalize, smooth, and invert the measured relation so the requested physical response becomes the LUT input and the original test force becomes its output. Assetto Corsa's open Controller Tweaks implementation averages multiple captures, normalizes both axes, smooths the response, swaps the measured input/output relationship, and writes `x|y` LUT points.[^10] Other open integrations use the same Assetto Corsa format and require endpoints `0|0` and `1|1`.[^11]

For this firmware, the generated inverse can be sampled at 128 magnitudes and compiled as bytes. That has several advantages:

- no floating-point work in the USB path;
- exact coverage of the classic protocol's available constant-force levels;
- a tiny table;
- easy review and deterministic tests;
- optional separate left/right tables if repeated tests show meaningful asymmetry.

The supplied wheel table maps a requested 1% response to a 16.2% motor command, establishing the approximate breakaway region. It then maps 25% to 26.9%, 50% to 36.0%, 75% to 74.4%, and 100% to 100%. This differs sharply from a fixed offset: the measured inverse reduces much of the middle range to compensate for the wheel's nonlinear physical response instead of simply making every force heavier. Linear interpolation at `magnitude / 127`, followed by rounding to an integer command magnitude, produces 77 distinct outputs across the available 128 entries. The source table is preserved in `profiles/g27-measured.lut` so this conversion remains reviewable.

The test must be made with the G27 connected directly to the PC, with normal power and no hand on the rim. Multiple runs should be averaged. The raw response should be made monotonic before inversion so measurement noise cannot create a LUT that reduces motor command while requested force increases.

### Dither and dynamic compensation

Alternating above-threshold pulses can produce an average torque below the static threshold, and vibration can reduce effective static friction in mechanical contacts.[^12] In this application it also introduces audible gear chatter, timing sensitivity, extra heat, and force content that GT7 did not request. It is a later experiment, not a default.

An adaptive controller could use steering speed and direction to apply extra breakaway torque only while the mechanism appears stuck. That requires reliable velocity estimation, filtering, and stability limits. It also mixes a local control loop with the game's force loop. The current adapter should establish the benefit of static profiles and a measured LUT before attempting it.

Adding general damping or friction is useful only as a stabilizer. It cannot restore missing road detail and can hide the detail the boost recovers. Logitech setup recommendations commonly disable driver spring and damper contributions so the simulator remains the source of effects.[^13]

## Profile behavior and clipping

A weak wheel has limited dynamic range. Raising low forces makes them perceptible, but every mapping must decide how to use the remaining range:

- **Rescaled minimum force** keeps maximum output and compresses all nonzero input into the remaining range. At 12%, the original 0–100% range becomes approximately 12–100%.
- **Additive minimum force** preserves the original low/mid slope but reaches 100% early, losing strong-force differences. It should not be used here.
- **Gamma** boosts a broad region and approaches 100% smoothly. It alters more of the game's intended response than a minimum-force offset.
- **Measured LUT** is the only option intended to linearize the physical wheel, rather than merely making it livelier.

Game-side gain still matters. iRacing defines clipping as loss of requested-force differences once the wheel is already at maximum.[^3] The adapter cannot reconstruct detail that GT7 has already clipped before transmission. Profile comparisons should therefore use fixed GT7 Max Torque and Sensitivity settings, and the higher minimum-force profile should be rejected if it produces persistent straight-line oscillation or masks cornering-force changes.

## On-the-fly selection

The two outer red shifter buttons are decoded as L3 and R3 and currently have no adapter chord. Requiring both for one second makes accidental activation unlikely. On activation:

- advance to the next profile exactly once;
- suppress L3 and R3 until both are released, preventing an in-game side effect;
- apply the new profile to subsequent constant-force packets;
- blink the onboard LED `profile index + 1` times;
- print the profile name over UART;
- defer persistence briefly so rapid comparisons cause one flash write rather than one write per press.

The LED already reports authentication state. A small LED manager should treat profile indication as a temporary overlay, then restore the authentication state. It should never blink from a USB callback or block the main loop; USB host/device work must continue between timed transitions.

## Persistence on RP2040

The Pico has 2 MiB of flash in this board configuration. The SDK requires programming in 256-byte pages and erasing in 4096-byte sectors.[^14] Flash cannot be read through XIP while it is being changed; the SDK's `flash_safe_execute` helper enters a safe state with interrupts disabled for a single-core application.[^15]

Reserve the final 4096-byte sector and assert at runtime that `__flash_binary_end` remains below it. Use a 256-byte append-only record containing a magic number, format version, sequence, profile, and checksum. A sector holds 16 selections before an erase. With saves delayed until the profile has remained stable, wear is negligible for this use. On boot, scan records and select the newest valid sequence. An interrupted page write leaves the prior record valid; an interrupted sector erase falls back to the default profile.

Flash erasure can pause USB service longer than a page program. The implementation should program unused pages during normal changes and erase only when the journal is full. Hardware testing must verify that the occasional erase does not make the PS5 or downstream hub disconnect. If it does, move the erase to early boot and keep one pre-erased sector available, or use two sectors as an atomic rotating journal.

## Validation plan

### Host tests

- Verify both neutral values remain unchanged.
- Verify both directions, endpoints, monotonicity, and exact expected values for every profile.
- Verify only selected constant-force slot bytes change.
- Verify spring, damper, play, stop, RPM LED, range, and unknown packets remain unchanged.
- Verify the chord triggers once, consumes L3/R3 through release, and does not interfere with the Select+Start PlayStation shortcut.
- Verify persistence record validation and newest-sequence selection independently from flash hardware.

### Bench tests

Use the existing constant-force test path to sweep each direction from neutral upward. Record the first command that produces repeatable movement and check left/right symmetry. Do this after the wheel has warmed up because friction and motor behavior can change with temperature.

Then repeat a fixed GT7 scenario with the five profiles. A useful comparison is the same car, tires, circuit, assists, Max Torque, and FFB Sensitivity, driving the same straight and the same medium-speed corner. Evaluate:

- detail just off center while holding the rim;
- straight-line oscillation with normal hands-on driving;
- gear rattle and heat;
- transition through zero torque while catching a slide;
- whether strong cornering forces retain distinguishable steps;
- profile recall after a full power cycle;
- authentication LED restoration after profile indication.

The 12% profile is the correct default candidate until direct measurements say otherwise. If 12% still has a gap, use 18% temporarily and generate a measured LUT. If 12% chatters, lower the floor rather than adding broad damping first.

## Applicability beyond the G27

The G25 and G27 share the classic Logitech protocol and similar geared architecture, so the same constant-force profiles are technically applicable. The G29 PC and PS4 identities also use the classic protocol, although GIMX notes that the PS4 form has a one-byte command offset.[^5] This repository currently drives a native G27 and should gate compensation on the known G27 path until other devices are captured and tested.

The G920 is different: GIMX identifies it as HID++ only.[^5] It cannot be added by assuming the seven-byte format. G923/TrueForce devices add another force-feedback path and higher-frequency effects; they need their own parser and are outside this change.

## Sources

[^1]: Logitech Support, [Logitech G27 Racing Wheel Technical Specifications](https://support.logi.com/hc/en-us/articles/360023461893-Logitech-G27-Racing-Wheel-Technical-Specifications).
[^2]: iRacing, [Release Notes for 2012 Season 3](https://www.iracing.com/release-notes-for-2012-season-3/), section describing `steeringFFBBaseOffset`.
[^3]: iRacing Support, [Controller Setup and Calibration](https://support.iracing.com/support/solutions/articles/31000133335).
[^4]: EdRacing, [Logitech G27](https://www.edracing.com/edr/Logitech_G27.php). This is community measurement, not manufacturer data.
[^5]: GIMX, [`ff_lg.h`: device identities, report size, slots, and commands](https://github.com/matlo/GIMX/blob/master/core/haptic/common/ff_lg.h#L27-L80).
[^6]: GIMX, [`ff_lg.h`: effect types and parameter layouts](https://github.com/matlo/GIMX/blob/master/core/haptic/common/ff_lg.h#L97-L181).
[^7]: Linux kernel, [`hid-lg4ff.c`: constant-force encoding](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-lg4ff.c#L386-L430).
[^8]: GIMX, [`ff_lg.h`: unsigned 8-bit to signed force conversion](https://github.com/matlo/GIMX/blob/master/core/haptic/common/ff_lg.h#L204-L222).
[^9]: Assetto Corsa Custom Shaders Patch, [Controller Tweaks gamma and LUT modes](https://github.com/ac-custom-shaders-patch/app-csp-defaults/blob/main/ControllerTweaks/ControllerTweaks.lua#L1784-L1831).
[^10]: Assetto Corsa Custom Shaders Patch, [WheelCheck CSV conversion and smoothing](https://github.com/ac-custom-shaders-patch/app-csp-defaults/blob/main/ControllerTweaks/ControllerTweaks.lua#L1349-L1415).
[^11]: ikt32, [GTAVManualTransmission wheel FFB LUT documentation](https://github.com/ikt32/GTAVManualTransmission/blob/master/doc/README.md#wheel-ffb-lut).
[^12]: Popov et al., [The Influence of Vibration on Friction: A Contact-Mechanical Perspective](https://doi.org/10.3389/fmech.2020.00069).
[^13]: iRacing Support, [Recommended Settings for Logitech Wheel and Pedals](https://support.iracing.com/support/solutions/articles/31000133487-recommended-settings-for-logitech-wheel-and-pedals-).
[^14]: Raspberry Pi, [Pico SDK `hardware_flash` API](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_flash).
[^15]: Raspberry Pi, [Pico SDK `pico_flash` API](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#pico_flash).
