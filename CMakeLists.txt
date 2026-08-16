cmake_minimum_required(VERSION 3.16)
project(gameboy_emu VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SDL2
find_package(SDL2 REQUIRED)
# nlohmann-json is included as json.hpp header-only

# Source files
set(SOURCES
    main_new.cpp
    cpu_new.cpp
    PPU.cpp
    video.cpp
)

# Headers
set(HEADERS
    cpu_new.h
    memory.h
    PPU.h
    video.h
    input.h
    timer.h
    apu.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Link SDL2 shared library. Do NOT link SDL2main: we use our own main()
# (video.h defines SDL_MAIN_HANDLED) and keep a console subsystem so that
# blargg test ROM serial output is visible on stdout.
target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)

# Install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
