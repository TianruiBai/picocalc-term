cmake_minimum_required(VERSION 3.16)

# Set the target board
# real YAHAL supports "rp2350-launchpad"; the Plus‑W should be compatible
set(YAHAL_BOARD "rp2350-launchpad")

# Use the YAHAL repository we just cloned locally
set(YAHAL_DIR "${CMAKE_CURRENT_LIST_DIR}/YAHAL")
# (delete or override this if you move YAHAL elsewhere)


# Locate the YAHAL library folder
include(YAHAL_import.cmake)

project(pico-mp3-player)

add_executable(pico-mp3-player
    pico_mp3_player.cpp
    main_task.cpp
    sd_reader_task.cpp
    mp3_decoder_task.cpp
    i2ckbd.cpp
    # include stub implementations for standalone build
    stubs/fake_fonts.cpp
)

# include our local stub headers so project can compile without full YAHAL
target_include_directories(pico-mp3-player PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

# Add YAHAL, MAD and FatFs libraries
yahal_add_me(pico-mp3-player)
yahal_add_library(pico-mp3-player MAD)
yahal_add_library(pico-mp3-player FatFs)
yahal_add_library(pico-mp3-player uGUI)

yahal_add_custom_targets(pico-mp3-player)
yahal_add_extra_outputs(pico-mp3-player)
