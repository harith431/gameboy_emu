#pragma once
#include <vector>
#include <cstdint>
#include <cstdio>
#include "input.h"

// Game Boy memory map + memory-mapped I/O.
// The PPU writes LY/STAT directly through `memory.data` (see PPU.h) so that
// game writes to those read-only registers can be correctly ignored here.
class Memory {
public:
    std::vector<uint8_t> data;

    // Timer register hooks (wired up to the Timer in main). The PPU writes
    // LY/STAT directly through `memory.data`, but the timer owns DIV/TIMA/TMA/
    // TAC because those registers have write glitches and reload semantics.
    void (*div_write_cb)() = nullptr;
    uint8_t (*tima_read_cb)() = nullptr;
    void (*tima_write_cb)(uint8_t) = nullptr;
    void (*tma_write_cb)(uint8_t) = nullptr;
    void (*tac_write_cb)(uint8_t) = nullptr;
    void (*lyc_write_cb)(uint8_t) = nullptr;
    void (*stat_write_cb)(uint8_t) = nullptr;
    uint8_t (*apu_read_cb)(uint16_t) = nullptr;
    void (*apu_write_cb)(uint16_t, uint8_t) = nullptr;

    // Cartridge (MBC) hooks: ROM window and external RAM.
    uint8_t (*cart_read_cb)(uint16_t) = nullptr;
    void (*cart_write_cb)(uint16_t, uint8_t) = nullptr;
    uint8_t (*cart_ram_read_cb)(uint16_t) = nullptr;
    void (*cart_ram_write_cb)(uint16_t, uint8_t) = nullptr;

    // Returns the PPU's current mode for bus blocking:
    // READ: OAM blocked in mode 2 (immediate) + modes 2/3 (STAT-delayed);
    //       VRAM blocked in mode 3 (STAT-delayed, or immediate on normal lines).
    int (*ppu_mode_cb)() = nullptr;
    // WRITE: OAM/VRAM writes are blocked following the STAT-delayed mode only.
    int (*ppu_write_mode_cb)() = nullptr;

    Memory() { data.resize(0x10000); }

    uint8_t read(uint16_t addr) {
        if (addr < 0x8000 && cart_read_cb) return cart_read_cb(addr);       // ROM
        if (addr >= 0xA000 && addr <= 0xBFFF && cart_ram_read_cb) return cart_ram_read_cb(addr); // cartridge RAM
        if (ppu_mode_cb) {
            int m = ppu_mode_cb();
            if (addr >= 0x8000 && addr <= 0x9FFF && m == 3) return 0xFF;   // VRAM: mode 3
            if (addr >= 0xFE00 && addr <= 0xFE9F && (m == 2 || m == 3)) return 0xFF; // OAM: modes 2/3
        }
        if (addr == 0xFF00) return read_joypad();
        if (addr == 0xFF05 && tima_read_cb) return tima_read_cb();   // TIMA reads 0 while reloading
        if (addr == 0xFF0F) return data[addr] | 0xE0;                 // IF: bits 5-7 read 1
        if (addr == 0xFF41) return data[addr] | 0x80;                 // STAT: bit 7 reads 1
        if (addr >= 0xE000 && addr <= 0xFDFF) return data[addr - 0x2000]; // Echo RAM
        if (addr >= 0xFF10 && addr <= 0xFF3F && apu_read_cb) return apu_read_cb(addr);
        return data[addr];
    }

    void write(uint16_t addr, uint8_t value) {
        // Echo RAM mirrors WRAM (0xC000-0xDDFF)
        if (addr >= 0xE000 && addr <= 0xFDFF) {
            data[addr - 0x2000] = value;
            return;
        }

        // ROM region: MBC control-register writes
        if (addr < 0x8000) {
            if (cart_write_cb) cart_write_cb(addr, value);
            return;
        }

        // Cartridge external RAM
        if (addr >= 0xA000 && addr <= 0xBFFF) {
            if (cart_ram_write_cb) cart_ram_write_cb(addr, value);
            return;
        }

        // Bus blocking on writes: VRAM during mode 3, OAM during modes 2/3.
        if (ppu_write_mode_cb) {
            int m = ppu_write_mode_cb();
            if (addr >= 0x8000 && addr <= 0x9FFF && m == 3) return;
            if (addr >= 0xFE00 && addr <= 0xFE9F && (m == 2 || m == 3)) return;
        }

        // APU registers (NR10-NR52 + wave RAM)
        if (addr >= 0xFF10 && addr <= 0xFF3F) {
            if (apu_write_cb) apu_write_cb(addr, value);
            return;
        }

        switch (addr) {
        case 0xFF00: // Joypad: only select bits 4-5 are writable
            data[addr] = (data[addr] & 0xCF) | (value & 0x30);
            return;

        case 0xFF01: // Serial data: print to stdout (blargg test ROM output)
            putchar(value);
            fflush(stdout);
            return;

        case 0xFF02: // Serial control: flush on transfer start
            if (value == 0x81) fflush(stdout);
            return;

        case 0xFF04: // DIV: any write resets the shared timer counter
            if (div_write_cb) div_write_cb();
            return;

        case 0xFF05: // TIMA
            if (tima_write_cb) tima_write_cb(value);
            return;

        case 0xFF06: // TMA
            if (tma_write_cb) tma_write_cb(value);
            return;

        case 0xFF07: // TAC
            if (tac_write_cb) tac_write_cb(value);
            return;

        case 0xFF45: // LYC: may update the STAT coincidence bit + interrupt
            if (lyc_write_cb) lyc_write_cb(value);
            else data[addr] = value;
            return;

        case 0xFF0F: // IF: only bits 0-4 writable, bits 5-7 read 1
            data[addr] = (value & 0x1F) | 0xE0;
            return;

        case 0xFF41: // STAT: only bits 3-6 writable (mode/coincidence are PPU-owned)
            if (stat_write_cb) stat_write_cb(value);
            else data[addr] = (data[addr] & 0x07) | (value & 0x78);
            return;

        case 0xFF44: // LY: read-only
            return;

        case 0xFF46: { // OAM DMA: copy 160 bytes from (value << 8) to OAM
            uint16_t src = (uint16_t)value << 8;
            for (int i = 0; i < 160; i++)
                data[0xFE00 + i] = read(src + i);
            return;
        }

        default:
            data[addr] = value;
        }
    }

private:
    uint8_t read_joypad() {
        uint8_t select = data[0xFF00] & 0x30;
        uint8_t buttons = 0x0F;

        if (!(select & 0x10)) { // P14 low -> action buttons
            if (input.a)      buttons &= ~0x01;
            if (input.b)      buttons &= ~0x02;
            if (input.select) buttons &= ~0x04;
            if (input.start)  buttons &= ~0x08;
        }
        if (!(select & 0x20)) { // P15 low -> direction buttons
            if (input.right) buttons &= ~0x01;
            if (input.left)  buttons &= ~0x02;
            if (input.up)    buttons &= ~0x04;
            if (input.down)  buttons &= ~0x08;
        }
        return 0xC0 | select | buttons;
    }
};

extern Memory memory;
