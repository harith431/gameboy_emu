#pragma once
#include <cstdint>
#include <vector>
#include <cstdio>
#include <ctime>

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
    bool has_battery = false; // battery-backed external RAM (persist to .sav)
    uint8_t rom_bank = 1;   // MBC1: low 5 bits; MBC3/MBC5: full bank
    uint8_t bank_hi = 0;    // MBC1: upper 2 bits
    uint8_t ram_bank = 0;   // MBC3/MBC5: RAM bank (0-15)
    uint8_t mode = 0;       // MBC1 banking mode (0 = ROM, 1 = RAM)
    int ram_banks = 0;

    // MBC3 real-time clock
    bool rtc_enabled = false;
    bool rtc_mapped = false;     // RTC (vs RAM) mapped to 0xA000-0xBFFF
    uint8_t rtc_select = 0;      // 0-4: sec, min, hour, day_lo, day_hi
    uint8_t rtc_regs[5] = {0};   // latched/current RTC register values
    uint64_t rtc_seconds = 0;    // total seconds (running clock)
    uint64_t rtc_last_sync = 0;  // wall clock (time()) at last sync

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

        // Battery-backed cartridges (RAM written to disk as a .sav file).
        switch (cart_type) {
            case 0x03: case 0x06: case 0x09: case 0x0D:
            case 0x0F: case 0x10: case 0x13:
            case 0x1B: case 0x1E:
                has_battery = true; break;
            default: has_battery = false; break;
        }

        // MBC3 real-time clock (types 0x0F and 0x10).
        rtc_enabled = (cart_type == 0x0F || cart_type == 0x10);
        rtc_mapped = false;
        rtc_select = 0;
        for (int i = 0; i < 5; i++) rtc_regs[i] = 0;
        rtc_seconds = 0;
        rtc_last_sync = (uint64_t)time(nullptr);

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
                    ram_bank = value & 0x0F;
                    rtc_mapped = rtc_enabled && (value & 0x08) != 0;
                    rtc_select = value & 0x07;
                } else {
                    // RTC latch: copy the running time into the readable regs.
                    if (rtc_enabled) rtc_latch();
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
        if (type == MBC3 && rtc_mapped && rtc_enabled) return read_rtc();
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
        if (type == MBC3 && rtc_mapped && rtc_enabled) { write_rtc(value); return; }
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

    // ---- MBC3 real-time clock ----

    void rtc_latch() {
        uint64_t total = rtc_seconds;
        if (!(rtc_regs[4] & 0x40)) {  // not halted: add elapsed wall time
            total += (uint64_t)time(nullptr) - rtc_last_sync;
        }
        uint64_t days = total / 86400;
        uint8_t hi = rtc_regs[4] & 0xC0;  // preserve halt + carry flags
        if (days > 511) hi |= 0x80;       // day counter carry
        hi |= (days >> 8) & 1;            // day bit 8
        rtc_regs[0] = total % 60;
        rtc_regs[1] = (total / 60) % 60;
        rtc_regs[2] = (total / 3600) % 24;
        rtc_regs[3] = days & 0xFF;
        rtc_regs[4] = hi;
    }

    void rtc_sync_from_regs() {
        uint64_t days = ((uint64_t)(rtc_regs[4] & 1) << 8) | rtc_regs[3];
        rtc_seconds = days * 86400 + (uint64_t)rtc_regs[2] * 3600
                    + (uint64_t)rtc_regs[1] * 60 + rtc_regs[0];
        rtc_last_sync = (uint64_t)time(nullptr);
    }

    uint8_t read_rtc() const {
        if (rtc_select >= 5) return 0xFF;
        uint8_t v = rtc_regs[rtc_select];
        if (rtc_select == 0 || rtc_select == 1) v &= 0x3F; // sec/min 0-59
        else if (rtc_select == 2) v &= 0x1F;               // hour 0-23
        else if (rtc_select == 4) v &= 0xC1;               // carry|halt|day8
        return v;
    }

    void write_rtc(uint8_t value) {
        if (rtc_select >= 5) return;
        switch (rtc_select) {
            case 0: rtc_regs[0] = value & 0x3F; break;
            case 1: rtc_regs[1] = value & 0x3F; break;
            case 2: rtc_regs[2] = value & 0x1F; break;
            case 3: rtc_regs[3] = value; break;
            case 4: rtc_regs[4] = value & 0xC1; break; // carry, halt, day bit 8
        }
        rtc_sync_from_regs();
    }

    // Load/save battery-backed RAM (.sav file). Returns true on successful load.
    bool load_ram_file(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        if (type == MBC2) {
            if (mbc2_ram.size() != 512) mbc2_ram.assign(512, 0);
            fread(mbc2_ram.data(), 1, 512, f);
        } else if (!ram.empty()) {
            fread(ram.data(), 1, ram.size(), f);
        }
        if (rtc_enabled && fread(rtc_regs, 1, 5, f) == 5) {
            fread(&rtc_seconds, 8, 1, f);
            fread(&rtc_last_sync, 8, 1, f);
            if (rtc_last_sync == 0) rtc_last_sync = (uint64_t)time(nullptr);
        }
        fclose(f);
        return true;
    }

    void save_ram_file(const char* path) const {
        FILE* f = fopen(path, "wb");
        if (!f) { fprintf(stderr, "cannot write save file: %s\n", path); return; }
        if (type == MBC2) {
            fwrite(mbc2_ram.data(), 1, mbc2_ram.size(), f);
        } else if (!ram.empty()) {
            fwrite(ram.data(), 1, ram.size(), f);
        }
        if (rtc_enabled) {
            fwrite(rtc_regs, 1, 5, f);
            fwrite(&rtc_seconds, 8, 1, f);
            fwrite(&rtc_last_sync, 8, 1, f);
        }
        fclose(f);
    }
};

extern Cartridge cartridge;
