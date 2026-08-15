#pragma once
#include <stdio.h>
#include <cstdint>
#include "memory.h"
#include "cpu_new.h"
#include "video.h"

#ifndef DBG
#define DBG(...) ((void)0)
#endif

extern uint8_t framebuffer[144][160];
extern CPU cpu;
extern Memory memory;

struct PPU {
    int ppu_clock = 0;
    int scanline = 0;
    int mode = 0;
    int frames = 0;
    bool frame_ready = false;
    bool vblank_triggered = false;
    bool lcd_enabled = false;

    void update_registers_from_memory() {
        uint8_t stat = memory.read(0xFF41);
        stat = (stat & 0xFC) | (mode & 0x03);

        uint8_t ly = memory.read(0xFF44);
        uint8_t lyc = memory.read(0xFF45);

        if (ly == lyc) {
            stat |= 0x04;
            if (stat & 0x40) {
                memory.write(0xFF0F, memory.read(0xFF0F) | 0x02);
            }
        } else {
            stat &= ~0x04;
        }

        switch (mode) {
        case 0: if (stat & 0x08) memory.write(0xFF0F, memory.read(0xFF0F) | 0x02); break;
        case 1: if (stat & 0x10) memory.write(0xFF0F, memory.read(0xFF0F) | 0x02); break;
        case 2: if (stat & 0x20) memory.write(0xFF0F, memory.read(0xFF0F) | 0x02); break;
        }

        // Write STAT directly: game writes are masked in Memory::write(), but
        // the PPU owns the mode/coincidence bits.
        memory.data[0xFF41] = stat;
    }

    void step(int cycles) {
        lcd_enabled = (memory.read(0xFF40) & 0x80) != 0;

        if (!lcd_enabled) {
            memory.data[0xFF44] = 0x00;
            ppu_clock = 0;
            scanline = 0;
            return;
        }
        ppu_clock += cycles;

        if (scanline < 144) {
            if (ppu_clock < 80)
                mode = 2; // OAM Scan
            else if (ppu_clock < 252)
                mode = 3; // Transfer
            else
                mode = 0; // HBlank
        } else {
            mode = 1; // VBlank
        }

        update_registers_from_memory();

        if (ppu_clock >= 456) {
            ppu_clock -= 456;

            memory.data[0xFF44] = static_cast<uint8_t>(scanline);

            if (scanline < 144) {
                render_scanline();
                render_window();
                render_sprites();
            }

            if (!vblank_triggered && scanline == 144) {
                uint8_t iflag = memory.read(0xFF0F);
                iflag |= 0x01;
                memory.write(0xFF0F, iflag);
                vblank_triggered = true;
            }

            if (scanline == 144) {
                frame_ready = true;
                if (!g_headless) render_frame(framebuffer);
            }

            scanline++;

            if (scanline > 153) {
                scanline = 0;
                vblank_triggered = false;
                frames++;
            }
        }
    }

    void render_scanline() {
        uint8_t scx = memory.read(0xFF43);
        uint8_t scy = memory.read(0xFF42);
        uint8_t bgp = memory.read(0xFF47);
        uint8_t lcdc = memory.read(0xFF40);
        if (!(lcdc & 0x01)) return;

        uint16_t tile_map = (lcdc & 0x08) ? 0x9C00 : 0x9800;
        uint16_t tile_data = (lcdc & 0x10) ? 0x8000 : 0x8800;
        bool signed_index = !(lcdc & 0x10);

        for (int x = 0; x < 160; ++x) {
            uint8_t pixel_x = (x + scx) & 0xFF;
            uint8_t pixel_y = (scanline + scy) & 0xFF;

            uint8_t tile_col = pixel_x / 8;
            uint8_t tile_row = pixel_y / 8;
            uint16_t tile_index_addr = tile_map + tile_row * 32 + tile_col;
            int tile_index = memory.read(tile_index_addr);
            if (signed_index) tile_index = (int8_t)tile_index;

            uint16_t tile_addr = tile_data + tile_index * 16;
            uint8_t line = pixel_y % 8;
            uint8_t byte1 = memory.read(tile_addr + line * 2);
            uint8_t byte2 = memory.read(tile_addr + line * 2 + 1);
            int bit = 7 - (pixel_x % 8);
            uint8_t color_num = ((byte2 >> bit) & 1) << 1 | ((byte1 >> bit) & 1);
            uint8_t color = (bgp >> (color_num * 2)) & 0x03;

            framebuffer[scanline][x] = color;
        }
    }

    void render_window() {
        uint8_t lcdc = memory.read(0xFF40);
        if (!(lcdc & 0x20)) return;

        uint8_t wx = memory.read(0xFF4B) - 7;
        uint8_t wy = memory.read(0xFF4A);
        uint8_t bgp = memory.read(0xFF47);
        if (scanline < wy) return;

        uint16_t tile_map = (lcdc & 0x40) ? 0x9C00 : 0x9800;
        uint16_t tile_data = (lcdc & 0x10) ? 0x8000 : 0x8800;
        bool signed_index = !(lcdc & 0x10);
        uint8_t win_y = scanline - wy;

        for (int x = wx; x < 160; ++x) {
            uint8_t win_x = x - wx;
            uint8_t tile_col = win_x / 8;
            uint8_t tile_row = win_y / 8;

            uint16_t tile_index_addr = tile_map + tile_row * 32 + tile_col;
            int tile_index = memory.read(tile_index_addr);
            if (signed_index) tile_index = (int8_t)tile_index;

            uint16_t tile_addr = tile_data + tile_index * 16;
            uint8_t line = win_y % 8;
            uint8_t byte1 = memory.read(tile_addr + line * 2);
            uint8_t byte2 = memory.read(tile_addr + line * 2 + 1);

            int bit = 7 - (win_x % 8);
            uint8_t color_num = ((byte2 >> bit) & 1) << 1 | ((byte1 >> bit) & 1);
            uint8_t color = (bgp >> (color_num * 2)) & 0x03;

            framebuffer[scanline][x] = color;
        }
    }

    void render_sprites() {
        uint8_t lcdc = memory.read(0xFF40);
        bool use_8x16 = lcdc & 0x04;

        for (int i = 0; i < 40; ++i) {
            uint8_t y = memory.read(0xFE00 + i * 4) - 16;
            uint8_t x = memory.read(0xFE00 + i * 4 + 1) - 8;
            uint8_t tile_index = memory.read(0xFE00 + i * 4 + 2);
            uint8_t attr = memory.read(0xFE00 + i * 4 + 3);

            if (scanline < y || scanline >= y + (use_8x16 ? 16 : 8)) continue;

            int sprite_line = scanline - y;
            if (attr & 0x40) sprite_line = (use_8x16 ? 15 : 7) - sprite_line;
            uint16_t tile_addr = 0x8000 + tile_index * 16 + sprite_line * 2;

            uint8_t byte1 = memory.read(tile_addr);
            uint8_t byte2 = memory.read(tile_addr + 1);

            for (int j = 0; j < 8; ++j) {
                int pixel_x = x + ((attr & 0x20) ? j : (7 - j));
                if (pixel_x < 0 || pixel_x >= 160) continue;
                uint8_t bit0 = (byte1 >> j) & 1;
                uint8_t bit1 = (byte2 >> j) & 1;
                uint8_t color_id = (bit1 << 1) | bit0;
                if (color_id == 0) continue;
                framebuffer[scanline][pixel_x] = 4 + color_id;
            }
        }
    }
};

extern PPU ppu;
