#pragma once
#include <SDL3/SDL.h>

constexpr auto SCREEN_WIDTH = 160;
constexpr auto SCREEN_HEIGHT = 144;
constexpr auto SCALE = 4;

extern SDL_Window* window;
extern SDL_Renderer* renderer;
extern SDL_Texture* texture;

void init_video();
void render_frame(const uint8_t framebuffer[144][160]);
void cleanup_video();
