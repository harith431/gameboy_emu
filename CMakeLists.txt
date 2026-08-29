cmake_minimum_required(VERSION 3.16)
project(gameboy_emu VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SDL2
find_package(SDL2 REQUIRED)

# Source files
set(SOURCES
    src/main.cpp
    src/core/cpu.cpp
    src/ppu/ppu.cpp
    src/ppu/video.cpp
    src/ui/library.cpp
)

# Headers
set(HEADERS
    src/core/cpu.h
    src/core/memory.h
    src/ppu/ppu.h
    src/ppu/video.h
    src/input/input.h
    src/timer/timer.h
    src/apu/apu.h
    src/cart/mbc.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Resolve `#include "core/..."`, `#include "ppu/..."` etc. relative to src/.
target_include_directories(${PROJECT_NAME} PRIVATE src)

# Link SDL2 shared library. Do NOT link SDL2main: we use our own main()
# (video.h defines SDL_MAIN_HANDLED) and keep a console subsystem so that
# blargg test ROM serial output is visible on stdout.
target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)

# Install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
