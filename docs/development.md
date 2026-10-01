# Development

End users should download the packaged Windows app. These instructions are for
building firmware, extending G27 Studio, and running its checks.

## Firmware

Initialize pinned submodules:

```sh
git submodule update --init --recursive
```

Install CMake, Ninja, Python, an Arm GNU toolchain, and a host C++ compiler. The
validated local Arm toolchain is 13.2.Rel1. For CMake 4, set
`CMAKE_POLICY_VERSION_MINIMUM=3.5` in the environment before configuring, so the
Pico SDK’s older host-tool projects inherit it.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Output: `build/adapter.uf2`. Standard firmware preserves existing configuration
flash. A Pico with no valid configuration starts in PC setup mode. Exported
personalized images replace both configuration banks and start in console mode.

This milestone supports the standard RP2040 Pico with 2 MB flash. The linker
checks that firmware does not overlap the final three sectors: two configuration
banks and the force-profile journal. The console retains its G29 USB identity;
PC setup uses a separate vendor-defined HID personality.

| Dependency | Pinned revision |
| --- | --- |
| Pico SDK | `6a7db34ff63345a7badec79ebea3aaef1712f374` (1.5.1) |
| jfedor2/TinyUSB | `8ef98ab274065e29ba138cc6f2efa0dc68028db3` |
| Pico-PIO-USB | `d00a10a8c425d0d40f81b87169102944b01f3bb3` |

## Desktop app

Use Python 3.12 on Windows in an isolated environment:

```powershell
python -m venv .venv
.venv/Scripts/python -m pip install -r desktop/requirements.txt
.venv/Scripts/python desktop/main.py
tools/build-desktop.ps1 -Python .venv/Scripts/python.exe
```

The package in `desktop/dist/G27-Studio` bundles the runtime, UI, and firmware.
Ship the complete folder in a ZIP. The default folder package avoids repeated
extraction and includes dependency notices. `-OneFile` is optional for local
experiments. Microsoft Edge WebView2 is required by the window.

## Checks

In Developer PowerShell with MSVC:

```powershell
cmake -S tests -B build-host-tests -G Ninja
cmake --build build-host-tests --parallel
ctest --test-dir build-host-tests --output-on-failure
```

Desktop tests communicate with the actual C protocol fixture built above:

```powershell
cd desktop
../.venv/Scripts/python -m unittest discover -s tests -v
cd ..
.venv/Scripts/python -m pip install -r desktop/requirements-dev.txt
.venv/Scripts/python desktop/tests/check_ui.py --channel msedge
```

For multi-configuration CMake generators, set `G27_PROTOCOL_FIXTURE` to the
correct executable, such as `../build-host-tests/Debug/protocol_fixture.exe`.
The UI script regenerates the README screenshots from the labelled demo and
tests live interactions through a separate mocked backend.

`G27-Studio.exe --smoke-test --smoke-output <path>` checks that the packaged UI,
Python bridge, HID module, and bundled firmware load together. It does not
change a connected adapter.

## Builds and releases

GitHub Actions builds the Pico firmware and Windows ZIP on feature-branch
pushes. Tests, UI checks, and packaged-app startup must pass before the archive
is uploaded. Artifacts are available on the workflow run.

A tag named `studio-v*` creates a **prerelease** containing the ZIP, UF2, and
SHA256 checksums. Tags can point to the feature branch; no merge into master is
required. Stable releases should follow actual Pico and GT7 validation.

## Hardware validation

Check first installation, console-to-setup switching, device enumeration, live
inputs, axis discovery and calibration, LUT upload, save/reboot, unplug recovery,
and BOOTSEL flashing. Then check the PS shortcut, profile chord, clutch/shifter,
and uninterrupted GT7 force feedback through an authentication renewal.

Desktop pulse tests prove the PC-to-wheel transport only. They do not prove GT7
force feedback or actual mechanical torque. Record observed results separately
from host tests and demo screenshots.
