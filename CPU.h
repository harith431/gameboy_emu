#pragma once
#include <cstdint>
#include "memory.h"
struct CPU {
    uint8_t A, B, C, D, E, F, H, L;
    uint16_t PC = 0x00, STACK_P = 0;
    int clock_cycles;
    bool IME = false;
    bool IME_Pending = false;
    bool halted = false;
    bool pc_modified = false;
    bool halt_bug = false;
    bool justExecutedEI = false;
    uint8_t last_opcode = 0x00;

    void setAF(uint16_t val) { A = val >> 8; F = val & 0xF0; }
    void setBC(uint16_t val) { B = val >> 8; C = val & 0xFF; }
    void setDE(uint16_t val) { D = val >> 8; E = val & 0xFF; }
    void setHL(uint16_t val) { H = val >> 8; L = val & 0xFF; }

    uint16_t getAF() { return (A << 8) | F; }
    uint16_t getBC() { return (B << 8) | C; }
    uint16_t getDE() { return (D << 8) | E; }
    uint16_t getHL() { return (H << 8) | L; }

    void setFlagZ(bool condition) { F = condition ? (F | 0x80) : (F & ~0x80); }
    void setFlagN(bool condition) { F = condition ? (F | 0x40) : (F & ~0x40); }
    void setFlagH(bool condition) { F = condition ? (F | 0x20) : (F & ~0x20); }
    void setFlagC(bool condition) { F = condition ? (F | 0x10) : (F & ~0x10); }

    bool getFlagC() { return (F & 0x10) != 0; }

    void request_interrupt(int id) {
        uint8_t iflag = memory.read(0xFF0F);
        iflag |= (1 << id);
        memory.write(0xFF0F, iflag);
    }
    
};
extern CPU cpu;