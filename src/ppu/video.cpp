#include "ppu/video.h"
#include <vector>

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
SDL_Texture* texture = nullptr;

void init_video() {
    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow("Game Boy Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH * SCALE, SCREEN_HEIGHT * SCALE,
        SDL_WINDOW_RESIZABLE);

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);
}

void render_frame(const uint8_t framebuffer[144][160]) {
    std::vector<uint32_t> pixels(144 * 160);

    // Game Boy DMG green-ish palette
    const uint32_t palette[4] = {
        0xFFE0F8D0,  // lightest
        0xFF88C070,  // light
        0xFF346856,  // dark
        0xFF081820,  // darkest
    };

    for (int y = 0; y < 144; y++) {
        for (int x = 0; x < 160; x++) {
            uint8_t color = framebuffer[y][x] & 0x03;
            pixels[y * 160 + x] = palette[color];
        }
    }

    SDL_UpdateTexture(texture, NULL, pixels.data(), 160 * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

void cleanup_video() {
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}
