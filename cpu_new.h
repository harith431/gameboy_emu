#pragma once
#include <cstdint>
#include "memory.h"

// Advance the timer and PPU by `n` T-cycles. Defined in main_new.cpp.
void tick_components(int n);

struct CPU {
    // T-cycle position within the currently-executing instruction. Used to
    // interleave memory accesses with the PPU/timer (see read8/write8).
    int instr_cycle = 0;   // T-cycles elapsed so far in this instruction
    int synced_cycle = 0;  // T-cycles already pushed to the timer/PPU
    // 8-bit registers
    uint8_t A = 0x01, B = 0x00, C = 0x13, D = 0x00, E = 0xD8;
    uint8_t F = 0xB0, H = 0x01, L = 0x4D;
    uint16_t PC = 0x0100, SP = 0xFFFE;
    
    bool IME = false;
    bool halted = false;
    bool halt_bug = false;
    bool ei_pending = false;  // EI enables IME after next instruction
    
    int cycles = 0;
    
    // 16-bit register access
    uint16_t AF() const { return (A << 8) | F; }
    uint16_t BC() const { return (B << 8) | C; }
    uint16_t DE() const { return (D << 8) | E; }
    uint16_t HL() const { return (H << 8) | L; }
    
    void setAF(uint16_t v) { A = v >> 8; F = v & 0xF0; }
    void setBC(uint16_t v) { B = v >> 8; C = v & 0xFF; }
    void setDE(uint16_t v) { D = v >> 8; E = v & 0xFF; }
    void setHL(uint16_t v) { H = v >> 8; L = v & 0xFF; }
    
    // Flag helpers
    bool flagZ() const { return F & 0x80; }
    bool flagN() const { return F & 0x40; }
    bool flagH() const { return F & 0x20; }
    bool flagC() const { return F & 0x10; }
    void setZ(bool v)  { F = v ? (F | 0x80) : (F & ~0x80); }
    void setN(bool v)  { F = v ? (F | 0x40) : (F & ~0x40); }
    void setH(bool v)  { F = v ? (F | 0x20) : (F & ~0x20); }
    void setC(bool v)  { F = v ? (F | 0x10) : (F & ~0x10); }
    
    // Advance the timer/PPU up to the current point in the instruction so
    // that the upcoming access happens at the right T-cycle.
    void sync_bus() {
        if (instr_cycle > synced_cycle) {
            tick_components(instr_cycle - synced_cycle);
            synced_cycle = instr_cycle;
        }
    }

    uint8_t read8(uint16_t addr) {
        sync_bus();
        uint8_t v = memory.read(addr);
        instr_cycle += 4;
        return v;
    }
    void write8(uint16_t addr, uint8_t v) {
        sync_bus();
        memory.write(addr, v);
        instr_cycle += 4;
    }
    uint16_t read16(uint16_t addr) { return read8(addr) | (read8(addr+1) << 8); }
    void write16(uint16_t addr, uint16_t v) { write8(addr, v & 0xFF); write8(addr+1, v >> 8); }
    
    uint8_t fetch8() { return read8(PC++); }
    uint16_t fetch16() { uint16_t v = read16(PC); PC += 2; return v; }
    int8_t fetchS8() { return (int8_t)fetch8(); }
    
    void push8(uint8_t v) { write8(--SP, v); }
    void push16(uint16_t v) { push8(v >> 8); push8(v & 0xFF); }
    uint8_t pop8() { return read8(SP++); }
    uint16_t pop16() { uint16_t v = read16(SP); SP += 2; return v; }
    
    // Execute one instruction, returns cycles taken
    int step();
    
    // Handle pending interrupts
    void handleInterrupts();
};

extern CPU cpu;
