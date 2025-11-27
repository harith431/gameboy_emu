#pragma once
#include <vector>




class Memory {
public:
    std::vector<uint8_t> data;
    bool allow_rom_write = false;


    Memory() {
        data.resize(0x10000); // 64 KB
    }
    void set_allow_rom_write(bool value) {
        allow_rom_write = value;
    }

    uint8_t read(uint16_t addr) const {
        if (addr == 0xFF0F) {
            return data[addr] | 0xE0;
        }
        else {
            return data[addr];
        }
    }

    void write(uint16_t addr, uint8_t value) {

        if (addr < 0x8000 && !allow_rom_write) {

            printf(" ROM Write Attempt: Addr = 0x%04X, Value = 0x%02X\n", addr, value);
           
            return;
        }
        else if (addr == 0xFF0F) {
                 data[addr] = (value & 0x1F) | 0xE0;  // Only lower 5 bits are writable, upper bits always 1
                  return;
        }
        else {
            data[addr] = value;
        }
    }


};
extern Memory memory;

