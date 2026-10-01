# Origin and credits

G27 Studio grows from [Jan Fedor's wheel-adapter](https://github.com/jfedor2/wheel-adapter).
His original Pico adapter design, USB console bridge, PS5 authentication, and
Logitech compatibility implementation provided the starting point for this
project. The upstream history remains in this repository.

This fork expands that foundation toward a configurable, approachable G27
adapter: native controls and force feedback, independent USB pedals, measured
force curves, a desktop configuration application, and firmware installation
without a development toolchain.

The upstream repository has no project-wide license. This fork does not
relicense the upstream code or grant additional rights to it. Existing
file-level notices remain applicable. Dependencies retain their own licenses.

Protocol work references the Linux Logitech wheel driver and GIMX, documented
in [g27-protocol.md](g27-protocol.md). The bundled measured LUT comes from this
project's G27 wheel measurements.

Firmware dependencies: Raspberry Pi Pico SDK, TinyUSB, and Pico-PIO-USB.
Desktop dependencies: Python, pywebview, pythonnet, HIDAPI/cython-hidapi, and
PyInstaller. Their authors and notices are retained in the distributed package
and dependency sources. Microsoft Edge WebView2 is an external runtime.
