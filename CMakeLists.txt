cmake_minimum_required(VERSION 3.13)

add_compile_definitions(PICO_DEFAULT_UART_BAUD_RATE=921600)
add_compile_definitions(PICO_DEFAULT_UART_TX_PIN=16)
add_compile_definitions(PICO_DEFAULT_UART_RX_PIN=17)
add_compile_definitions(PICO_DEFAULT_PIO_USB_DP_PIN=0)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/pico-sdk")
set(PICO_TINYUSB_PATH "${CMAKE_CURRENT_LIST_DIR}/tinyusb")
set(PICO_PIO_USB_PATH "${CMAKE_CURRENT_LIST_DIR}/Pico-PIO-USB")

include(pico_sdk_import.cmake)

project(adapter)

pico_sdk_init()

add_executable(adapter
    src/adapter.c
    src/brake.c
    src/g27.c
    src/g27_leds.c
    src/ffb.c
    src/profile_store.c
    src/descriptors.c
    src/settings.c
    src/settings_store.c
    src/hid_axes.c
    src/device_registry.c
    src/pc_mode.c
    src/pc_protocol.c
    src/auth_bridge.c
)
target_compile_features(adapter PRIVATE c_std_11)
target_compile_definitions(adapter PRIVATE PICO_FLASH_ASSUME_CORE1_SAFE=1)
target_include_directories(adapter PRIVATE src)
target_link_libraries(adapter
    pico_stdlib
    pico_flash
    hardware_exception
    hardware_watchdog
    tinyusb_host
    tinyusb_device
    tinyusb_board
    tinyusb_pico_pio_usb
)
# Configuration and the profile journal occupy the last three flash sectors.
# Linker overflow is a build failure, rather than a runtime loss of settings.
target_compile_definitions(adapter PRIVATE PICO_FLASH_SIZE_BYTES=2097152)
target_link_options(adapter PRIVATE "LINKER:--defsym=__flash_config_start=0x101fd000"
    "LINKER:-T,${CMAKE_CURRENT_LIST_DIR}/src/settings_guard.ld")
pico_add_extra_outputs(adapter)
