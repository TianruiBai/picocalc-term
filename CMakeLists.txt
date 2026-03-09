############################################################################
# PicoCalc-Term: Top-level CMakeLists.txt
#
# Build orchestration for NuttX + board definition + PicoCalc-Term OS
#
# Configurations:
#   - nsh:  Minimal NuttShell (board bring-up & validation)
#   - lvgl: Display + keyboard + LVGL (GUI bring-up)
#   - full: Complete OS (all apps, networking, etc.)
#
############################################################################

cmake_minimum_required(VERSION 3.20)

project(picocalc-term
    VERSION 0.1.0
    DESCRIPTION "PicoCalc Handheld Terminal OS"
    LANGUAGES C ASM
)

# --- Configuration ---
set(PICOCALC_CONFIG "full" CACHE STRING "Build configuration (nsh, lvgl, full)")
set_property(CACHE PICOCALC_CONFIG PROPERTY STRINGS nsh lvgl full)

set(NUTTX_DIR "${CMAKE_SOURCE_DIR}/nuttx" CACHE PATH "Path to NuttX source tree")
set(NUTTX_APPS_DIR "${CMAKE_SOURCE_DIR}/nuttx-apps" CACHE PATH "Path to NuttX apps")

# --- Toolchain ---
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

# --- Board definition ---
set(BOARD_DIR "${CMAKE_SOURCE_DIR}/boards/arm/rp23xx/picocalc-rp2350b")
set(DEFCONFIG "${BOARD_DIR}/configs/${PICOCALC_CONFIG}/defconfig")

message(STATUS "PicoCalc-Term v${PROJECT_VERSION}")
message(STATUS "Configuration: ${PICOCALC_CONFIG}")
message(STATUS "Board config:  ${DEFCONFIG}")
message(STATUS "NuttX:         ${NUTTX_DIR}")

# --- NuttX build integration ---
# NuttX uses its own Kconfig-based build system.
# This CMake file provides convenience targets that invoke make.

add_custom_target(configure
    COMMAND ${CMAKE_COMMAND} -E echo "Configuring NuttX with ${PICOCALC_CONFIG} config..."
    COMMAND ${CMAKE_COMMAND} -E copy ${DEFCONFIG} ${NUTTX_DIR}/.config
    COMMAND ${CMAKE_COMMAND} -E chdir ${NUTTX_DIR} make olddefconfig
    WORKING_DIRECTORY ${NUTTX_DIR}
    COMMENT "Applying ${PICOCALC_CONFIG} configuration"
)

add_custom_target(build
    COMMAND make -C ${NUTTX_DIR} -j$ENV{NUMBER_OF_PROCESSORS}
    DEPENDS configure
    COMMENT "Building NuttX + PicoCalc-Term"
)

add_custom_target(flash
    COMMAND ${CMAKE_COMMAND} -E echo "Flashing via UF2..."
    COMMAND ${CMAKE_OBJCOPY} -O binary ${NUTTX_DIR}/nuttx ${NUTTX_DIR}/nuttx.bin
    # TODO: Convert to UF2 and copy to RP2350 boot drive
    DEPENDS build
    COMMENT "Flashing firmware"
)

add_custom_target(clean_nuttx
    COMMAND make -C ${NUTTX_DIR} distclean
    COMMENT "Cleaning NuttX build"
)
