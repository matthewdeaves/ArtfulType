# HOW TO BUILD USING CMAKE as a standalone project:
# mkdir build
# cd build
# cmake .. -DCMAKE_TOOLCHAIN_FILE=$HOME/dev/retro68/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
# make

cmake_minimum_required(VERSION 3.10)
project(ArtfulType C)

add_application(ArtfulType
    main.c
    scrolling.c
    markdown.c
    mdcore.c
    undo.c
    zoom.c
    file.c
    find.c
    print.c
    splash.c
    main.r
    CREATOR "ArtT"
    )

# Apple's Universal Interfaces gate the classic Toolbox names ArtfulType uses
# (inThumb/inUpButton, and other pre-Carbon spellings) behind OLDROUTINENAMES,
# which defaults to 0. Turn it on for the whole target.
target_compile_definitions(ArtfulType PRIVATE OLDROUTINENAMES=1)

set_target_properties(ArtfulType PROPERTIES COMPILE_OPTIONS -ffunction-sections)
if(CMAKE_SYSTEM_NAME MATCHES Retro68)
    set_target_properties(ArtfulType PROPERTIES LINK_FLAGS "-Wl,-gc-sections -Wl,--mac-strip-macsbug")
else()
    set_target_properties(ArtfulType PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
endif()
