# ArtfulType Floppy Writer -- a throwaway tool app that writes the embedded
# 800K bootable image to an inserted floppy on real hardware.
#
# Build (from this directory), pointing at a Retro68 m68k toolchain:
#   mkdir build && cd build
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=<retro68>/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
#   make
#
# The 800K image to embed defaults to ../../dist/ArtfulType-800K.dsk (produced
# by build-boot-images.sh). Override with -DAT_DISK_IMAGE=/path/to/image.dsk.

cmake_minimum_required(VERSION 3.10)
project(ArtfulTypeFloppyWriter C)

if(NOT AT_DISK_IMAGE)
    set(AT_DISK_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/ArtfulType-800K.dsk")
endif()
get_filename_component(AT_DISK_IMAGE "${AT_DISK_IMAGE}" ABSOLUTE)
if(NOT EXISTS "${AT_DISK_IMAGE}")
    message(FATAL_ERROR
        "Disk image to embed not found:\n  ${AT_DISK_IMAGE}\n"
        "Build it first (./build-boot-images.sh from the repo root) or pass "
        "-DAT_DISK_IMAGE=/path/to/ArtfulType-800K.dsk")
endif()
message(STATUS "Embedding disk image: ${AT_DISK_IMAGE}")

# Bake the absolute image path into the .r so Rez's $$read always resolves.
# Rez only re-runs when writer.r changes, and the path alone wouldn't change if
# the image is regenerated in place -- so embed the image's content hash in the
# .r too, and mark the image a configure dependency. Now regenerating the image
# reconfigures (new hash -> writer.r changes -> Rez re-embeds), and a plain
# `make` can never ship a stale image.
file(SHA256 "${AT_DISK_IMAGE}" AT_DISK_IMAGE_HASH)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${AT_DISK_IMAGE}")
configure_file(writer.r.in "${CMAKE_CURRENT_BINARY_DIR}/writer.r" @ONLY)

add_application(ArtfulTypeFloppyWriter
    main.c
    "${CMAKE_CURRENT_BINARY_DIR}/writer.r"
    CREATOR "ATfw"
)

set_target_properties(ArtfulTypeFloppyWriter PROPERTIES COMPILE_OPTIONS -ffunction-sections)
if(CMAKE_SYSTEM_NAME MATCHES Retro68)
    set_target_properties(ArtfulTypeFloppyWriter PROPERTIES
        LINK_FLAGS "-Wl,-gc-sections -Wl,--mac-strip-macsbug")
else()
    set_target_properties(ArtfulTypeFloppyWriter PROPERTIES
        LINK_FLAGS "-Wl,-gc-sections")
endif()
