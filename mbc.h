#pragma once
#include <cstdint>
#include <vector>
#include <cstdio>

// Memory Bank Controller emulation: MBC1, MBC2, MBC3 (no RTC) and MBC5.
//
// The cartridge exposes two 16 KiB windows into the ROM:
//   0x0000-0x3FFF  "fixed" bank
//   0x4000-0x7FFF  "switchable" bank
// and (except MBC2) an 8 KiB external-RAM window at 0xA000-0xBFFF.
// Writes to 0x0000-0x7FFF are MBC control-register writes.
struct Cartridge {
    enum Type { NONE, MBC1, MBC2, MBC3, MBC5 };

    Type type = NONE;
    std::vector<uint8_t> rom;
    std::vector<uint8_t> ram;      // external RAM (banks of 8 KiB)
    std::vector<uint8_t> mbc2_ram; // 512 nibbles for MBC2 (512 bytes, low nibble)

    bool ram_enabled = false;
    uint8_t rom_bank = 1;   // MBC1: low 5 bits; MBC3/MBC5: full bank
    uint8_t bank_hi = 0;    // MBC1: upper 2 bits
    uint8_t ram_bank = 0;   // MBC3/MBC5: RAM bank (0-15)
    uint8_t mode = 0;       // MBC1 banking mode (0 = ROM, 1 = RAM)
    int ram_banks = 0;

    void load(const std::vector<uint8_t>& data, uint8_t cart_type) {
        rom = data;
        ram.clear();
        mbc2_ram.clear();
        ram_enabled = false;
        rom_bank = 1; bank_hi = 0; ram_bank = 0; mode = 0;

        switch (cart_type) {
            case 0x00: type = NONE; break;
            case 0x01: case 0x02: case 0x03: type = MBC1; break;
            case 0x05: case 0x06: type = MBC2; break;
            case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: type = MBC3; break;
            case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: type = MBC5; break;
            default: type = NONE; break;
        }

        uint8_t ramsize = rom.size() > 0x149 ? rom[0x149] : 0;
        switch (ramsize) {
            case 0x00: ram_banks = 0; break;
            case 0x02: ram_banks = 1; break;  // 8 KiB
            case 0x03: ram_banks = 4; break;  // 32 KiB
            case 0x04: ram_banks = 16; break; // 128 KiB
            case 0x05: ram_banks = 8; break;  // 64 KiB
            default: ram_banks = 0; break;
        }

        if (type == MBC2) {
            ram_banks = 0;
            mbc2_ram.assign(512, 0);
        } else if (ram_banks > 0) {
            ram.assign((size_t)ram_banks * 0x2000, 0);
        }
    }

    int rom_bank_count() const {
        size_t n = rom.size() / 0x4000;
        if (n == 0) n = 1;
        return (int)n;
    }

    uint8_t read_rom_bank(int bank, uint16_t offset) const {
        size_t base = ((size_t)bank % rom_bank_count()) * 0x4000 + offset;
        if (base >= rom.size()) return 0xFF;
        return rom[base];
    }

    // The bank number used for the switchable 0x4000-0x7FFF window.
    uint32_t switchable_rom_bank() const {
        switch (type) {
            case MBC1: {
                uint32_t bank = (uint32_t)(bank_hi << 5) | rom_bank;
                if (rom_bank == 0) bank++;      // bank 0 is forced to 1
                return bank;
            }
            case MBC2: {
                uint32_t bank = rom_bank & 0x0F;
                if (bank == 0) bank = 1;   // bank 0 is forced to 1
                return bank;
            }
            case MBC3: return rom_bank & 0x7F;
            case MBC5: return rom_bank & 0x1FF;
            default: return 1;
        }
    }

    uint8_t read(uint16_t addr) {
        if (addr < 0x4000) {
            if (type == MBC1 && mode == 1) {
                // RAM mode: fixed window is switchable by the upper bits.
                return read_rom_bank(bank_hi << 5, addr);
            }
            return read_rom_bank(0, addr);
        }
        return read_rom_bank(switchable_rom_bank(), addr - 0x4000);
    }

    void write(uint16_t addr, uint8_t value) {
        switch (type) {
            case MBC1:
                if (addr < 0x2000) {
                    ram_enabled = (value & 0x0F) == 0x0A;
                } else if (addr < 0x4000) {
                    rom_bank = value & 0x1F;
                } else if (addr < 0x6000) {
                    bank_hi = value & 0x03;
                } else {
                    mode = value & 0x01;
                }
                break;

            case MBC2:
                if (addr < 0x4000) {
                    if (addr & 0x0100) rom_bank = value & 0x0F;
                    else ram_enabled = (value & 0x0F) == 0x0A;
                }
                break;

            case MBC3:
                if (addr < 0x2000) {
                    ram_enabled = (value & 0x0F) == 0x0A;
                } else if (addr < 0x4000) {
                    rom_bank = value & 0x7F;
                    if (rom_bank == 0) rom_bank = 1;
                } else if (addr < 0x6000) {
                    ram_bank = value & 0x0F; // RTC register select ignored
                } else {
                    // RTC latch ignored
                }
                break;

            case MBC5:
                if (addr < 0x2000) {
                    ram_enabled = (value & 0x0F) == 0x0A;
                } else if (addr < 0x3000) {
                    rom_bank = (uint8_t)((rom_bank & 0x100) | value);
                } else if (addr < 0x4000) {
                    rom_bank = (uint8_t)(((value & 0x01) << 8) | (rom_bank & 0xFF));
                } else if (addr < 0x6000) {
                    ram_bank = value & 0x0F;
                }
                break;

            default: break;
        }
    }

    // The RAM bank currently mapped to 0xA000-0xBFFF.
    int current_ram_bank() const {
        switch (type) {
            case MBC1: return (mode == 1) ? (bank_hi & 0x03) : 0;
            case MBC3: return ram_bank & 0x0F;
            case MBC5: return ram_bank & 0x0F;
            default: return 0;
        }
    }

    uint8_t read_ram(uint16_t addr) {
        if (type == MBC2) {
            if (!ram_enabled) return 0xFF;
            uint16_t off = (addr - 0xA000) & 0x1FF; // 512 nibbles, wraps
            return 0xF0 | (mbc2_ram[off] & 0x0F);
        }
        if (!ram_enabled || ram.empty()) return 0xFF;
        size_t off = (size_t)(current_ram_bank() % ram_banks) * 0x2000 + (addr - 0xA000);
        if (off >= ram.size()) return 0xFF;
        return ram[off];
    }

    void write_ram(uint16_t addr, uint8_t value) {
        if (type == MBC2) {
            if (!ram_enabled) return;
            uint16_t off = (addr - 0xA000) & 0x1FF; // 512 nibbles, wraps
            mbc2_ram[off] = value & 0x0F;
            return;
        }
        if (!ram_enabled || ram.empty()) return;
        size_t off = (size_t)(current_ram_bank() % ram_banks) * 0x2000 + (addr - 0xA000);
        if (off >= ram.size()) return;
        ram[off] = value;
    }
};

extern Cartridge cartridge;
