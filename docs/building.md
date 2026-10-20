# Building and testing the firmware

For normal setup, download the ready-to-flash `adapter.uf2` from the
[latest release](https://github.com/bilgon32/ps5-wheel-adapter/releases/latest)
and follow the [installation instructions](../README.md#flash-and-connect-the-adapter).
Build from source when changing the firmware or its configuration.

## Build the firmware

Build this fork and use `build/adapter.uf2` to get all features described here.

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

## Dependencies

The firmware uses pinned Git submodules:

| Dependency | Revision used by this repository | Purpose |
| --- | --- | --- |
| [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | `1.5.1` / `6a7db34ff63345a7badec79ebea3aaef1712f374` | RP2040 runtime and build system |
| [jfedor2/tinyusb](https://github.com/jfedor2/tinyusb) | `8ef98ab274065e29ba138cc6f2efa0dc68028db3` | USB device and host stack |
| [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) | `d00a10a8c425d0d40f81b87169102944b01f3bb3` | Downstream USB host on GPIO0/GPIO1 |

The hardware-tested 2026.10.20 firmware was built with Arm GNU Toolchain
`15.2.Rel1` (GCC `15.2.1`) and CMake `4.2.1` in Release configuration for the
default `pico` board. Arm GNU Toolchain `13.2.Rel1` was also validated previously.
The project requires CMake 3.13 or newer, Python 3, and a host C++ compiler.
Dependency licenses and notices remain in their respective submodule directories.

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
overflow, force-profile transforms and selection, G27 status patterns, failed
sends, native-mode re-enumeration, and fair authentication arbitration.

On macOS or Linux, the current host-test stubs use the `_MSC_VER` compatibility
path to avoid Pico hardware calls. Configure them with:

```sh
cmake -S tests -B build-host-tests -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-D_MSC_VER=1930
cmake --build build-host-tests --parallel
ctest --test-dir build-host-tests --output-on-failure
```

This flag is only for host tests. The firmware build uses the normal Arm
compiler path and checks the packed USB report layouts.
