# SPDX-License-Identifier: GPL-3.0-or-later
# Q2C-LCARS sound firmware: USB serial tone generator -> I2S (MAX98357A).
# Build: cmake -B build -DPICO_BOARD=pico2 (or pico) && cmake --build build
cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(q2c_lcars_sound C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(q2c_lcars_sound main.c)
pico_generate_pio_header(q2c_lcars_sound ${CMAKE_CURRENT_LIST_DIR}/i2s.pio)

# The USB product name is how HelixScreen finds this board in /dev/serial/by-id
# (SerialToneSoundBackend::kDeviceMatch = "Q2C-LCARS_Sound").
target_compile_definitions(q2c_lcars_sound PRIVATE
    USBD_MANUFACTURER="Q2C-LCARS"
    USBD_PRODUCT="Q2C-LCARS Sound")

target_link_libraries(q2c_lcars_sound pico_stdlib hardware_pio hardware_clocks)
pico_enable_stdio_usb(q2c_lcars_sound 1)
pico_enable_stdio_uart(q2c_lcars_sound 0)
pico_add_extra_outputs(q2c_lcars_sound)
