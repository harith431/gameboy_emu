#include <stdio.h>
#include <cstdint>
#include "memory.h"
#include "CPU.h"



PPU ppu;
void render_scanline() {
    uint8_t scy = memory.read(0xFF42);   // Scroll Y
    uint8_t scx = memory.read(0xFF43);   // Scroll X
    uint8_t ly = memory.read(0xFF44);   // Current scanline
    uint8_t lcdc = memory.read(0xFF40);  // LCD control register
    uint8_t bgp = memory.read(0xFF47);   // Background palette

    bool bg_enable = lcdc & 0x01;
    uint16_t bg_tile_map = (lcdc & 0x08) ? 0x9C00 : 0x9800;
    uint16_t tile_data_addr = (lcdc & 0x10) ? 0x8000 : 0x8800;
    bool signed_index = !(lcdc & 0x10);

    if (!bg_enable) return;

    for (int x = 0; x < 160; ++x) {
        uint8_t pixel_x = (x + scx) & 0xFF;
        uint8_t pixel_y = (ly + scy) & 0xFF;

        uint8_t tile_col = pixel_x / 8;
        uint8_t tile_row = pixel_y / 8;

        uint16_t tile_index_addr = bg_tile_map + tile_row * 32 + tile_col;
        int tile_index = memory.read(tile_index_addr);

        // Handle signed tile index if using 0x8800 region
        if (signed_index) tile_index = (int8_t)tile_index;

        uint16_t tile_addr = tile_data_addr + (tile_index * 16);
        uint8_t line = pixel_y % 8;
        uint8_t byte1 = memory.read(tile_addr + line * 2);
        uint8_t byte2 = memory.read(tile_addr + line * 2 + 1);

        int bit = 7 - (pixel_x % 8);
        uint8_t color_num = ((byte2 >> bit) & 1) << 1 | ((byte1 >> bit) & 1);

        // Decode color using background palette (BGP)
        uint8_t color = (bgp >> (color_num * 2)) & 0x03;

        // Write pixel to framebuffer
        framebuffer[ly][x] = color;
    }
}

uint16_t oam_start = 0xFE00;
for (int i = 0; i < 40; i++) {
    uint8_t y = memory.read(oam_start + i * 4);
    uint8_t x = memory.read(oam_start + i * 4 + 1);
    uint8_t tile_index = memory.read(oam_start + i * 4 + 2);
    uint8_t attr = memory.read(oam_start + i * 4 + 3);
}

void render_sprites() {
    for (int i = 0; i < 40; ++i) {
        uint8_t y = memory.read(0xFE00 + i * 4) - 16;
        uint8_t x = memory.read(0xFE00 + i * 4 + 1) - 8;
        uint8_t tile_index = memory.read(0xFE00 + i * 4 + 2);
        uint8_t attr = memory.read(0xFE00 + i * 4 + 3);

        bool y_flip = attr & 0x40;
        bool x_flip = attr & 0x20;
        bool use_obp1 = attr & 0x10;
        bool priority = attr & 0x80;

        uint8_t line = memory.read(0xFF44);
        if (line < y || line >= y + 8) continue; // Sprite not on current scanline

        int sprite_line = line - y;
        if (y_flip) sprite_line = 7 - sprite_line;

        uint16_t tile_addr = 0x8000 + tile_index * 16;
        uint8_t byte1 = memory.read(tile_addr + sprite_line * 2);
        uint8_t byte2 = memory.read(tile_addr + sprite_line * 2 + 1);

        for (int pixel = 0; pixel < 8; ++pixel) {
            int x_pos = x + (x_flip ? (7 - pixel) : pixel);
            if (x_pos < 0 || x_pos >= 160) continue;

            uint8_t bit0 = (byte1 >> (7 - pixel)) & 0x1;
            uint8_t bit1 = (byte2 >> (7 - pixel)) & 0x1;
            uint8_t color_id = (bit1 << 1) | bit0;

            if (color_id == 0) continue; // Transparent

            // Priority: skip if background has higher priority
            if (priority && framebuffer[line][x_pos] != 0) continue;

            framebuffer[line][x_pos] = 4 + color_id; // Distinguish from background
        }
    }
}


int main() {
    while (true) {
        int cpu_cycles = cpu.clock_cycles;
        ppu.step(cpu_cycles);
        cpu.clock_cycles = 0;
        if (memory.read(0xFF44) < 144) {
            render_scanline();
            render_sprites(); 
        }

    } 
}
