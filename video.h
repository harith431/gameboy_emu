#pragma once
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

constexpr auto SCREEN_WIDTH = 160;
constexpr auto SCREEN_HEIGHT = 144;
constexpr auto SCALE = 4;

extern SDL_Window* window;
extern SDL_Renderer* renderer;
extern SDL_Texture* texture;

// When true, no SDL window is created and no frames are rendered
// (useful for running test ROMs that report over the serial port).
extern bool g_headless;

void init_video();
void render_frame(const uint8_t framebuffer[144][160]);
void cleanup_video();
