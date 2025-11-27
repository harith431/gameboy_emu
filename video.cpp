#include "video.h"
#include <vector>

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
SDL_Texture* texture = nullptr;

void init_video() {
    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow("Game Boy Emulator",
        SCREEN_WIDTH * SCALE, SCREEN_HEIGHT * SCALE,
        SDL_WINDOW_RESIZABLE );

    renderer = SDL_CreateRenderer(window, NULL);  // SDL3 uses NULL not -1
    texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);
}

void render_frame(const uint8_t framebuffer[144][160]) {
    std::vector<uint32_t> pixels(144 * 160);

    for (int y = 0; y < 144; y++) {
        for (int x = 0; x < 160; x++) {
            uint8_t color = framebuffer[y][x];
            uint8_t brightness = 255 - color * 85;
            pixels[y * 160 + x] = (brightness << 16) | (brightness << 8) | brightness;
        }
    }

    SDL_UpdateTexture(texture, NULL, pixels.data(), 160 * sizeof(uint32_t));

    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, NULL);  // ✅ SDL3 correct
    SDL_RenderPresent(renderer);
}

void cleanup_video() {
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}
