
# .-------------------------------------------------------------------------.
# |     MIT Licensed : Copyright 2022, "Hippy"                              |
# `-------------------------------------------------------------------------'

# ***************************************************************************
# *     This project - Build a combined booter and some apps                *
# ***************************************************************************

set(PROJECT pico_multi_booter)
set(BOOT    boot)
set(OUTPUT  combined.uf2)

# .-------------------------------------------------------------------------.
# |     The usual CMake prerequisites                                       |
# `-------------------------------------------------------------------------'

cmake_minimum_required(VERSION 3.12)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(${PROJECT} C CXX ASM)

pico_sdk_init()


# .-------------------------------------------------------------------------.
# |     The other things we need to complete the build                      |
# `-------------------------------------------------------------------------'

find_package(Python3 REQUIRED COMPONENTS Interpreter)


add_subdirectory(picomite)
add_subdirectory(sd_boot)

### build boot.uf2
add_executable(${BOOT} boot.c)
target_link_libraries(${BOOT} pico_stdlib
							  hardware_pio 
)

target_compile_options(${BOOT} PRIVATE
    -Os
    -Wall
    -Werror
    -Wno-unused-variable
    -Wno-unused-function
)   
pico_enable_stdio_usb(${BOOT}  0)  
pico_enable_stdio_uart(${BOOT} 1)
pico_add_uf2_output(${BOOT})

target_link_libraries(${BOOT}
    hardware_resets
    cmsis_core
)
pico_set_binary_type(${BOOT} copy_to_ram)

add_custom_target(BUILT_${BOOT}
    COMMENT "Record Build Details for '${BOOT}'"
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${BOOT}.uf2
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/applink.py BUILT
            ${CMAKE_CURRENT_BINARY_DIR}
            ${BOOT}
)


# ***************************************************************************
# *     Build the BOOT plus all the APPS                                    *
# ***************************************************************************


add_dependencies(BUILT_boot boot)

add_dependencies(PREPARE_sd_boot BUILT_boot)
add_dependencies(sd_boot PREPARE_sd_boot)
add_dependencies(BUILT_sd_boot sd_boot)

add_dependencies(PREPARE_PicoMite BUILT_sd_boot)
add_dependencies(PicoMite PREPARE_PicoMite)
add_dependencies(BUILT_PicoMite PicoMite)


# ***************************************************************************
# *     Join the BOOT and all APP '.uf2' files together                     *
# ***************************************************************************
set(UF2S boot.uf2 sd_boot.uf2 PicoMite.uf2)

add_custom_target(JOIN
    COMMENT "Combine the '.uf2' files"
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/applink.py JOIN
            ${CMAKE_CURRENT_BINARY_DIR}
            ${OUTPUT}
            ${UF2S}
)

add_dependencies(JOIN BUILT_PicoMite)

add_custom_target(${PROJECT} ALL DEPENDS JOIN)

# ***************************************************************************
# *     Finished                                                            *
# ***************************************************************************
