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
    int ppu_clock = 0;   // dot within the current scanline (0-455)
    int scanline = 0;    // LY
    int mode = 2;        // 0=HBlank 1=VBlank 2=OAM scan 3=transfer
    int frames = 0;
    bool frame_ready = false;
    bool lcd_enabled = false;

    // Set IF's STAT interrupt bit (bit 1) if the given STAT enable bit is set.
    void fire_stat_interrupt(uint8_t enable_bit) {
        if (memory.data[0xFF41] & enable_bit) {
            memory.data[0xFF0F] = (memory.data[0xFF0F] & 0x1F) | 0x02;
        }
    }

    // Switch to a new mode, update the STAT mode bits, and fire the matching
    // STAT interrupt (bits 3/4/5) if enabled. Called on mode *transitions*.
    void set_mode(int new_mode) {
        if (new_mode == mode) return;
        mode = new_mode;
        uint8_t stat = memory.data[0xFF41];
        stat = (stat & 0xFC) | (mode & 0x03);
        memory.data[0xFF41] = stat;

        switch (mode) {
        case 0: fire_stat_interrupt(0x08); break; // mode-0 interrupt (bit 3)
        case 1: fire_stat_interrupt(0x10); break; // mode-1 interrupt (bit 4)
        case 2: fire_stat_interrupt(0x20); break; // mode-2 interrupt (bit 5)
        default: break;
        }
    }

    // Update the STAT LYC=LY coincidence bit and fire the LYC interrupt
    // (bit 6) only on the rising edge (coincidence 0 -> 1).
    void update_lyc() {
        uint8_t ly = memory.data[0xFF44];
        uint8_t lyc = memory.data[0xFF45];
        uint8_t stat = memory.data[0xFF41];
        bool was_coincident = (stat & 0x04) != 0;
        bool coincident = (ly == lyc);
        if (coincident) stat |= 0x04; else stat &= ~0x04;
        memory.data[0xFF41] = stat;
        if (coincident && !was_coincident) fire_stat_interrupt(0x40);
    }

    // The game wrote LYC: update the coincidence bit/interrupt (only while
    // the LCD is on; with LCD off the comparison clock is stopped).
    void write_lyc(uint8_t value) {
        memory.data[0xFF45] = value;
        if (lcd_enabled) update_lyc();
    }

    // Advance the PPU by `cycles` T-cycles (4.194304 MHz dots).
    void step(int cycles) {
        bool lcd_on = (memory.read(0xFF40) & 0x80) != 0;
        if (lcd_on != lcd_enabled) {
            lcd_enabled = lcd_on;
            if (!lcd_on) {
                // LCD off: mode 0, LY reset to 0. The LYC=LY coincidence bit
                // is frozen (the comparison clock stops).
                if (mode != 0) set_mode(0);
                ppu_clock = 0;
                scanline = 0;
                memory.data[0xFF44] = 0x00;
            } else {
                // LCD on: LY starts at 0 and line 0 begins in mode 0
                // (DMG quirk: no mode 2 on the first line).
                ppu_clock = 0;
                scanline = 0;
                memory.data[0xFF44] = 0x00;
                update_lyc();
                mode = 0;
                uint8_t stat = memory.data[0xFF41];
                stat = (stat & 0xFC) | 0x00;
                memory.data[0xFF41] = stat;
            }
        }

        if (!lcd_enabled) return;

        while (cycles-- > 0) {
            ppu_clock++;
            if (ppu_clock >= 456) {
                ppu_clock = 0;

                // Render the scanline that just finished.
                if (scanline < 144) {
                    render_scanline();
                    render_window();
                    render_sprites();
                }

                scanline++;
                if (scanline > 153) {
                    scanline = 0;
                    frames++;
                }
                memory.data[0xFF44] = static_cast<uint8_t>(scanline);
                update_lyc();

                if (scanline == 144) {
                    // Enter VBlank: VBlank interrupt + STAT mode-1 interrupt.
                    // On DMG the mode-2 interrupt also fires here (if enabled).
                    memory.data[0xFF0F] = (memory.data[0xFF0F] & 0x1F) | 0x01;
                    set_mode(1);
                    fire_stat_interrupt(0x20);
                    frame_ready = true;
                    if (!g_headless) render_frame(framebuffer);
                } else if (scanline < 144) {
                    set_mode(2); // visible scanline starts in OAM-scan mode
                }
                // scanlines 145-153 remain in mode 1
            } else if (scanline < 144) {
                if (ppu_clock == 80) set_mode(3);   // OAM scan -> transfer
                else if (ppu_clock == 252) set_mode(0); // transfer -> HBlank
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
