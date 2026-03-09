
include_directories(
  ${CMAKE_CURRENT_LIST_DIR}
)

set(APP_NAME sd_boot)

add_subdirectory(i2ckbd)
add_subdirectory(lcdspi)
add_subdirectory(lib/pico-vfs)

add_executable(${APP_NAME}
    main.c
    key_event.c
    text_directory_ui.c
)

target_link_libraries(${APP_NAME}
    pico_stdlib
    hardware_sync
    hardware_flash
    hardware_irq
    hardware_adc
    hardware_pwm
    hardware_i2c
    hardware_spi
    hardware_dma
    hardware_exception
    hardware_pio
    pico_multicore
    i2ckbd
    lcdspi
    blockdevice_sd
    filesystem_fat
    filesystem_vfs
)

target_compile_options(${APP_NAME} PRIVATE
        -Os
        -Wall
        -Wno-unused-variable
        -Wno-unused-function
        -DPICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
) 

pico_enable_stdio_usb(${APP_NAME} 1)
pico_enable_stdio_uart(${APP_NAME} 1)

pico_add_extra_outputs(${APP_NAME})
pico_add_uf2_output(${APP_NAME})


target_link_options(${APP_NAME} PRIVATE -Wl,--print-memory-usage)
target_link_options(${APP_NAME} PRIVATE -Wl,-z,max-page-size=4096)

pico_set_linker_script(${APP_NAME} ${CMAKE_BINARY_DIR}/${APP_NAME}.ld)


set(UF2_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/${APP_NAME}.uf2)
set(UF2_DEST   ${CMAKE_BINARY_DIR}/${APP_NAME}.uf2)

add_custom_command(
    OUTPUT ${UF2_DEST}
    DEPENDS ${UF2_SOURCE}
    COMMAND ${CMAKE_COMMAND} -E copy ${UF2_SOURCE} ${UF2_DEST}
    COMMENT "Copying ${APP_NAME}.uf2 to top-level build dir"
)


add_custom_target(PREPARE_${APP_NAME}
            COMMENT "Create Linker Script for '${APP_NAME}'"
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/applink.py PREPARE
                    ${CMAKE_BINARY_DIR}
                    ${APP_NAME})

add_custom_target(BUILT_${APP_NAME}
        COMMENT "Record Build Details for '${APP_NAME}'"
        DEPENDS ${UF2_DEST}
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/applink.py BUILT
                ${CMAKE_BINARY_DIR}
                ${APP_NAME})

