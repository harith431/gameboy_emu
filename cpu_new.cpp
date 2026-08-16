#include "cpu_new.h"
#include <cstdio>

CPU cpu;

// Forward declarations for CB opcodes
static int execCB();

int CPU::step() {
    if (halted) {
        cycles += 4;
        return 4;
    }

    // Reset the per-instruction T-cycle tracking. Each memory access inside
    // this instruction will advance the timer/PPU to its exact cycle.
    instr_cycle = 0;
    synced_cycle = 0;

    // HALT bug: the instruction following HALT is fetched twice (PC is not
    // incremented for the first fetch).
    bool no_increment = halt_bug;
    halt_bug = false;
    
    uint8_t op = read8(PC);
    if (!no_increment) PC++;
    int baseCycles = 0;
    
    // Save EI state
    bool wasEI = ei_pending;
    ei_pending = false;
    
    switch (op) {
    case 0x00: /* NOP */ baseCycles = 4; break;
    
    // === 8-bit LD ===
    case 0x7F: A = A; baseCycles = 4; break; // LD A,A
    case 0x78: A = B; baseCycles = 4; break;
    case 0x79: A = C; baseCycles = 4; break;
    case 0x7A: A = D; baseCycles = 4; break;
    case 0x7B: A = E; baseCycles = 4; break;
    case 0x7C: A = H; baseCycles = 4; break;
    case 0x7D: A = L; baseCycles = 4; break;
    case 0x7E: A = read8(HL()); baseCycles = 8; break;
    case 0x40: /* LD B,B */ baseCycles = 4; break;
    case 0x41: B = C; baseCycles = 4; break;
    case 0x42: B = D; baseCycles = 4; break;
    case 0x43: B = E; baseCycles = 4; break;
    case 0x44: B = H; baseCycles = 4; break;
    case 0x45: B = L; baseCycles = 4; break;
    case 0x46: B = read8(HL()); baseCycles = 8; break;
    case 0x47: B = A; baseCycles = 4; break;  // LD B,A
    case 0x48: C = B; baseCycles = 4; break;
    case 0x49: /* LD C,C */ baseCycles = 4; break;
    case 0x4A: C = D; baseCycles = 4; break;
    case 0x4B: C = E; baseCycles = 4; break;
    case 0x4C: C = H; baseCycles = 4; break;
    case 0x4D: C = L; baseCycles = 4; break;
    case 0x4E: C = read8(HL()); baseCycles = 8; break;
    case 0x4F: C = A; baseCycles = 4; break;  // LD C,A
    case 0x50: D = B; baseCycles = 4; break;
    case 0x51: D = C; baseCycles = 4; break;
    case 0x52: /* LD D,D */ baseCycles = 4; break;
    case 0x53: D = E; baseCycles = 4; break;
    case 0x54: D = H; baseCycles = 4; break;
    case 0x55: D = L; baseCycles = 4; break;
    case 0x56: D = read8(HL()); baseCycles = 8; break;
    case 0x57: D = A; baseCycles = 4; break;  // LD D,A
    case 0x58: E = B; baseCycles = 4; break;
    case 0x59: E = C; baseCycles = 4; break;
    case 0x5A: E = D; baseCycles = 4; break;
    case 0x5B: /* LD E,E */ baseCycles = 4; break;
    case 0x5C: E = H; baseCycles = 4; break;
    case 0x5D: E = L; baseCycles = 4; break;
    case 0x5E: E = read8(HL()); baseCycles = 8; break;
    case 0x5F: E = A; baseCycles = 4; break;  // LD E,A
    case 0x60: H = B; baseCycles = 4; break;
    case 0x61: H = C; baseCycles = 4; break;
    case 0x62: H = D; baseCycles = 4; break;
    case 0x63: H = E; baseCycles = 4; break;
    case 0x64: /* LD H,H */ baseCycles = 4; break;
    case 0x65: H = L; baseCycles = 4; break;
    case 0x66: H = read8(HL()); baseCycles = 8; break;
    case 0x67: H = A; baseCycles = 4; break;  // LD H,A
    case 0x68: L = B; baseCycles = 4; break;
    case 0x69: L = C; baseCycles = 4; break;
    case 0x6A: L = D; baseCycles = 4; break;
    case 0x6B: L = E; baseCycles = 4; break;
    case 0x6C: L = H; baseCycles = 4; break;
    case 0x6D: /* LD L,L */ baseCycles = 4; break;
    case 0x6E: L = read8(HL()); baseCycles = 8; break;
    case 0x6F: L = A; baseCycles = 4; break;  // LD L,A
    
    // LD (HL),r
    case 0x70: write8(HL(), B); baseCycles = 8; break;
    case 0x71: write8(HL(), C); baseCycles = 8; break;
    case 0x72: write8(HL(), D); baseCycles = 8; break;
    case 0x73: write8(HL(), E); baseCycles = 8; break;
    case 0x74: write8(HL(), H); baseCycles = 8; break;
    case 0x75: write8(HL(), L); baseCycles = 8; break;
    case 0x36: write8(HL(), fetch8()); baseCycles = 12; break; // LD (HL),d8
    case 0x77: write8(HL(), A); baseCycles = 8; break;  // LD (HL),A
    
    // LD r, d8
    case 0x06: B = fetch8(); baseCycles = 8; break;
    case 0x0E: C = fetch8(); baseCycles = 8; break;
    case 0x16: D = fetch8(); baseCycles = 8; break;
    case 0x1E: E = fetch8(); baseCycles = 8; break;
    case 0x26: H = fetch8(); baseCycles = 8; break;
    case 0x2E: L = fetch8(); baseCycles = 8; break;
    case 0x3E: A = fetch8(); baseCycles = 8; break;
    
    // LD (BC),A / LD (DE),A
    case 0x02: write8(BC(), A); baseCycles = 8; break;
    case 0x12: write8(DE(), A); baseCycles = 8; break;
    
    // LD A,(BC) / LD A,(DE)
    case 0x0A: A = read8(BC()); baseCycles = 8; break;
    case 0x1A: A = read8(DE()); baseCycles = 8; break;
    
    // LD (HL+),A / LD (HL-),A / LD A,(HL+) / LD A,(HL-)
    case 0x22: write8(HL(), A); setHL(HL()+1); baseCycles = 8; break;
    case 0x32: write8(HL(), A); setHL(HL()-1); baseCycles = 8; break;
    case 0x2A: A = read8(HL()); setHL(HL()+1); baseCycles = 8; break;
    case 0x3A: A = read8(HL()); setHL(HL()-1); baseCycles = 8; break;
    
    // LDH (a8),A / LDH A,(a8) / LD (C),A / LD A,(C)
    case 0xE0: write8(0xFF00 + fetch8(), A); baseCycles = 12; break;
    case 0xF0: A = read8(0xFF00 + fetch8()); baseCycles = 12; break;
    case 0xE2: write8(0xFF00 + C, A); baseCycles = 8; break;
    case 0xF2: A = read8(0xFF00 + C); baseCycles = 8; break;
    
    // LD (a16),A / LD A,(a16)
    case 0xEA: write8(fetch16(), A); baseCycles = 16; break;
    case 0xFA: A = read8(fetch16()); baseCycles = 16; break;
    
    // LD (a16),SP
    case 0x08: { uint16_t a = fetch16(); write8(a, SP & 0xFF); write8(a+1, SP >> 8); baseCycles = 20; } break;
    
    // === 16-bit LD ===
    case 0x01: setBC(fetch16()); baseCycles = 12; break;
    case 0x11: setDE(fetch16()); baseCycles = 12; break;
    case 0x21: setHL(fetch16()); baseCycles = 12; break;
    case 0x31: SP = fetch16(); baseCycles = 12; break;
    case 0xF9: SP = HL(); baseCycles = 8; break; // LD SP,HL
    case 0xF8: { // LD HL, SP+r8
        int8_t off = fetchS8();
        uint16_t res = SP + off;
        setZ(false); setN(false);
        setH(((SP & 0xF) + (off & 0xF)) > 0xF);
        setC(((SP & 0xFF) + (off & 0xFF)) > 0xFF);
        setHL(res);
        baseCycles = 12;
    } break;
    
    // === PUSH / POP ===
    case 0xF5: oam_bug_hook(SP); push16(AF()); baseCycles = 16; break;
    case 0xC5: oam_bug_hook(SP); push16(BC()); baseCycles = 16; break;
    case 0xD5: oam_bug_hook(SP); push16(DE()); baseCycles = 16; break;
    case 0xE5: oam_bug_hook(SP); push16(HL()); baseCycles = 16; break;
    case 0xF1: setAF(pop16()); baseCycles = 12; break;
    case 0xC1: setBC(pop16()); baseCycles = 12; break;
    case 0xD1: setDE(pop16()); baseCycles = 12; break;
    case 0xE1: setHL(pop16()); baseCycles = 12; break;
    
    // === ADD A,r ===
    case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87: {
        uint8_t v = (op == 0x86) ? read8(HL()) :
                    (op == 0x80) ? B : (op == 0x81) ? C : (op == 0x82) ? D :
                    (op == 0x83) ? E : (op == 0x84) ? H : (op == 0x85) ? L : A;
        uint16_t r = A + v;
        setZ((r & 0xFF) == 0); setN(false);
        setH(((A & 0xF) + (v & 0xF)) > 0xF);
        setC(r > 0xFF);
        A = r & 0xFF;
        baseCycles = (op == 0x86) ? 8 : 4;
    } break;
    
    case 0xC6: { // ADD A,d8
        uint8_t v = fetch8();
        uint16_t r = A + v;
        setZ((r & 0xFF) == 0); setN(false);
        setH(((A & 0xF) + (v & 0xF)) > 0xF);
        setC(r > 0xFF);
        A = r & 0xFF;
        baseCycles = 8;
    } break;
    
    // === ADC A,r ===
    case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
        uint8_t v = (op == 0x8E) ? read8(HL()) :
                    (op == 0x88) ? B : (op == 0x89) ? C : (op == 0x8A) ? D :
                    (op == 0x8B) ? E : (op == 0x8C) ? H : (op == 0x8D) ? L : A;
        uint8_t c = flagC() ? 1 : 0;
        uint16_t r = A + v + c;
        setZ((r & 0xFF) == 0); setN(false);
        setH(((A & 0xF) + (v & 0xF) + c) > 0xF);
        setC(r > 0xFF);
        A = r & 0xFF;
        baseCycles = (op == 0x8E) ? 8 : 4;
    } break;
    
    case 0xCE: { // ADC A,d8
        uint8_t v = fetch8(); uint8_t c = flagC() ? 1 : 0;
        uint16_t r = A + v + c;
        setZ((r & 0xFF) == 0); setN(false);
        setH(((A & 0xF) + (v & 0xF) + c) > 0xF);
        setC(r > 0xFF);
        A = r & 0xFF;
        baseCycles = 8;
    } break;
    
    // === SUB r ===
    case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        uint8_t v = (op == 0x96) ? read8(HL()) :
                    (op == 0x90) ? B : (op == 0x91) ? C : (op == 0x92) ? D :
                    (op == 0x93) ? E : (op == 0x94) ? H : (op == 0x95) ? L : A;
        setZ(A == v); setN(true);
        setH((A & 0xF) < (v & 0xF));
        setC(A < v);
        A -= v;
        baseCycles = (op == 0x96) ? 8 : 4;
    } break;
    
    case 0xD6: { uint8_t v = fetch8(); setZ(A == v); setN(true); setH((A & 0xF) < (v & 0xF)); setC(A < v); A -= v; baseCycles = 8; } break;
    case 0xDE: { // SBC A,d8
        uint8_t v = fetch8(); uint8_t c = flagC() ? 1 : 0;
        uint8_t oldA = A;
        A = A - v - c;
        setZ(A == 0); setN(true);
        setH((oldA & 0xF) < (v & 0xF) + c);
        setC(oldA < v + c);
        baseCycles = 8;
    } break;
    
    // === SBC A,r ===
    case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F: {
        uint8_t v = (op == 0x9E) ? read8(HL()) :
                    (op == 0x98) ? B : (op == 0x99) ? C : (op == 0x9A) ? D :
                    (op == 0x9B) ? E : (op == 0x9C) ? H : (op == 0x9D) ? L : A;
        uint8_t c = flagC() ? 1 : 0;
        uint8_t oldA = A;
        A = A - v - c;
        setZ(A == 0); setN(true);
        setH((oldA & 0xF) < (v & 0xF) + c);
        setC(oldA < v + c);
        baseCycles = (op == 0x9E) ? 8 : 4;
    } break;
    
    // === AND r ===
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: case 0xA6: case 0xA7: {
        uint8_t v = (op == 0xA6) ? read8(HL()) :
                    (op == 0xA0) ? B : (op == 0xA1) ? C : (op == 0xA2) ? D :
                    (op == 0xA3) ? E : (op == 0xA4) ? H : (op == 0xA5) ? L : A;
        A &= v;
        setZ(A == 0); setN(false); setH(true); setC(false);
        baseCycles = (op == 0xA6) ? 8 : 4;
    } break;
    
    case 0xE6: { A &= fetch8(); setZ(A == 0); setN(false); setH(true); setC(false); baseCycles = 8; } break;
    
    // === XOR r ===
    case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF: {
        uint8_t v = (op == 0xAE) ? read8(HL()) :
                    (op == 0xA8) ? B : (op == 0xA9) ? C : (op == 0xAA) ? D :
                    (op == 0xAB) ? E : (op == 0xAC) ? H : (op == 0xAD) ? L : A;
        A ^= v;
        setZ(A == 0); setN(false); setH(false); setC(false);
        baseCycles = (op == 0xAE) ? 8 : 4;
    } break;
    
    case 0xEE: { A ^= fetch8(); setZ(A == 0); setN(false); setH(false); setC(false); baseCycles = 8; } break;
    
    // === OR r ===
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7: {
        uint8_t v = (op == 0xB6) ? read8(HL()) :
                    (op == 0xB0) ? B : (op == 0xB1) ? C : (op == 0xB2) ? D :
                    (op == 0xB3) ? E : (op == 0xB4) ? H : (op == 0xB5) ? L : A;
        A |= v;
        setZ(A == 0); setN(false); setH(false); setC(false);
        baseCycles = (op == 0xB6) ? 8 : 4;
    } break;
    
    case 0xF6: { A |= fetch8(); setZ(A == 0); setN(false); setH(false); setC(false); baseCycles = 8; } break;
    
    // === CP r ===
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
        uint8_t v = (op == 0xBE) ? read8(HL()) :
                    (op == 0xB8) ? B : (op == 0xB9) ? C : (op == 0xBA) ? D :
                    (op == 0xBB) ? E : (op == 0xBC) ? H : (op == 0xBD) ? L : A;
        setZ(A == v); setN(true);
        setH((A & 0xF) < (v & 0xF));
        setC(A < v);
        baseCycles = (op == 0xBE) ? 8 : 4;
    } break;
    
    case 0xFE: { uint8_t v = fetch8(); setZ(A == v); setN(true); setH((A & 0xF) < (v & 0xF)); setC(A < v); baseCycles = 8; } break;
    
    // === INC r ===
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x3C: {
        uint8_t* r = (op == 0x04) ? &B : (op == 0x0C) ? &C : (op == 0x14) ? &D :
                     (op == 0x1C) ? &E : (op == 0x24) ? &H : (op == 0x2C) ? &L : &A;
        setH((*r & 0xF) == 0xF);
        (*r)++;
        setZ(*r == 0); setN(false);
        baseCycles = 4;
    } break;
    
    case 0x34: { // INC (HL)
        uint8_t v = read8(HL());
        setH((v & 0xF) == 0xF);
        v++; write8(HL(), v);
        setZ(v == 0); setN(false);
        baseCycles = 12;
    } break;
    
    // === DEC r ===
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x3D: {
        uint8_t* r = (op == 0x05) ? &B : (op == 0x0D) ? &C : (op == 0x15) ? &D :
                     (op == 0x1D) ? &E : (op == 0x25) ? &H : (op == 0x2D) ? &L : &A;
        setH((*r & 0xF) == 0);
        (*r)--;
        setZ(*r == 0); setN(true);
        baseCycles = 4;
    } break;
    
    case 0x35: { uint8_t v = read8(HL()); setH((v & 0xF) == 0); v--; write8(HL(), v); setZ(v == 0); setN(true); baseCycles = 12; } break;
    
    // === 16-bit INC/DEC ===
    case 0x03: oam_bug_hook(BC()); setBC(BC()+1); baseCycles = 8; break;
    case 0x13: oam_bug_hook(DE()); setDE(DE()+1); baseCycles = 8; break;
    case 0x23: oam_bug_hook(HL()); setHL(HL()+1); baseCycles = 8; break;
    case 0x33: oam_bug_hook(SP); SP++; baseCycles = 8; break;
    case 0x0B: oam_bug_hook(BC()); setBC(BC()-1); baseCycles = 8; break;
    case 0x1B: oam_bug_hook(DE()); setDE(DE()-1); baseCycles = 8; break;
    case 0x2B: oam_bug_hook(HL()); setHL(HL()-1); baseCycles = 8; break;
    case 0x3B: oam_bug_hook(SP); SP--; baseCycles = 8; break;
    
    // === ADD HL,rr ===
    case 0x09: { uint16_t v = BC(); uint32_t r = HL() + v; setN(false); setH((HL()&0xFFF)+(v&0xFFF)>0xFFF); setC(r>0xFFFF); setHL(r); baseCycles = 8; } break;
    case 0x19: { uint16_t v = DE(); uint32_t r = HL() + v; setN(false); setH((HL()&0xFFF)+(v&0xFFF)>0xFFF); setC(r>0xFFFF); setHL(r); baseCycles = 8; } break;
    case 0x29: { uint16_t v = HL(); uint32_t r = HL() + v; setN(false); setH((HL()&0xFFF)+(v&0xFFF)>0xFFF); setC(r>0xFFFF); setHL(r); baseCycles = 8; } break;
    case 0x39: { uint16_t v = SP;  uint32_t r = HL() + v; setN(false); setH((HL()&0xFFF)+(v&0xFFF)>0xFFF); setC(r>0xFFFF); setHL(r); baseCycles = 8; } break;
    
    // ADD SP,dd
    case 0xE8: {
        int8_t off = fetchS8();
        setZ(false); setN(false);
        setH(((SP & 0xF) + (off & 0xF)) > 0xF);
        setC(((SP & 0xFF) + (off & 0xFF)) > 0xFF);
        SP = SP + off;
        baseCycles = 16;
    } break;
    
    // === Rotates & Shifts ===
    case 0x07: { // RLCA
        bool c = (A & 0x80) != 0;
        A = (A << 1) | (c ? 1 : 0);
        setZ(false); setN(false); setH(false); setC(c);
        baseCycles = 4;
    } break;
    
    case 0x0F: { // RRCA
        bool c = (A & 0x01) != 0;
        A = (A >> 1) | (c ? 0x80 : 0);
        setZ(false); setN(false); setH(false); setC(c);
        baseCycles = 4;
    } break;
    
    case 0x17: { // RLA
        bool c = (A & 0x80) != 0;
        A = (A << 1) | (flagC() ? 1 : 0);
        setZ(false); setN(false); setH(false); setC(c);
        baseCycles = 4;
    } break;
    
    case 0x1F: { // RRA
        bool c = (A & 0x01) != 0;
        A = (A >> 1) | (flagC() ? 0x80 : 0);
        setZ(false); setN(false); setH(false); setC(c);
        baseCycles = 4;
    } break;
    
    // === DAA ===
    case 0x27: { // DAA
        int a = A;
        if (!flagN()) {
            if (flagH() || (a & 0x0F) > 9) a += 0x06;
            if (flagC() || a > 0x9F) { a += 0x60; setC(true); }
        } else {
            if (flagH()) a = (a - 0x06) & 0xFF;
            if (flagC()) a = (a - 0x60) & 0xFF;
        }
        A = a & 0xFF;
        setZ(A == 0);
        setH(false);
        baseCycles = 4;
    } break;
    
    // === CPL, CCF, SCF ===
    case 0x2F: A = ~A; setN(true); setH(true); baseCycles = 4; break;
    case 0x3F: setN(false); setH(false); setC(!flagC()); baseCycles = 4; break;
    case 0x37: setN(false); setH(false); setC(true); baseCycles = 4; break;
    
    // === JP ===
    case 0xC3: PC = fetch16(); baseCycles = 16; break;
    case 0xE9: PC = HL(); baseCycles = 4; break; // JP HL
    
    case 0xC2: { uint16_t a = fetch16(); if (!flagZ()) { PC = a; baseCycles = 16; } else baseCycles = 12; } break;
    case 0xCA: { uint16_t a = fetch16(); if (flagZ())  { PC = a; baseCycles = 16; } else baseCycles = 12; } break;
    case 0xD2: { uint16_t a = fetch16(); if (!flagC()) { PC = a; baseCycles = 16; } else baseCycles = 12; } break;
    case 0xDA: { uint16_t a = fetch16(); if (flagC())  { PC = a; baseCycles = 16; } else baseCycles = 12; } break;
    
    // === JR ===
    case 0x18: { int8_t o = fetchS8(); PC += o; baseCycles = 12; } break;
    case 0x20: { int8_t o = fetchS8(); if (!flagZ()) { PC += o; baseCycles = 12; } else baseCycles = 8; } break;
    case 0x28: { int8_t o = fetchS8(); if (flagZ())  { PC += o; baseCycles = 12; } else baseCycles = 8; } break;
    case 0x30: { int8_t o = fetchS8(); if (!flagC()) { PC += o; baseCycles = 12; } else baseCycles = 8; } break;
    case 0x38: { int8_t o = fetchS8(); if (flagC())  { PC += o; baseCycles = 12; } else baseCycles = 8; } break;
    
    // === CALL ===
    case 0xCD: { uint16_t a = fetch16(); push16(PC); PC = a; baseCycles = 24; } break;
    case 0xC4: { uint16_t a = fetch16(); if (!flagZ()) { push16(PC); PC = a; baseCycles = 24; } else baseCycles = 12; } break;
    case 0xCC: { uint16_t a = fetch16(); if (flagZ())  { push16(PC); PC = a; baseCycles = 24; } else baseCycles = 12; } break;
    case 0xD4: { uint16_t a = fetch16(); if (!flagC()) { push16(PC); PC = a; baseCycles = 24; } else baseCycles = 12; } break;
    case 0xDC: { uint16_t a = fetch16(); if (flagC())  { push16(PC); PC = a; baseCycles = 24; } else baseCycles = 12; } break;
    
    // === RET ===
    case 0xC9: PC = pop16(); baseCycles = 16; break;
    case 0xC0: if (!flagZ()) { PC = pop16(); baseCycles = 20; } else baseCycles = 8; break;
    case 0xC8: if (flagZ())  { PC = pop16(); baseCycles = 20; } else baseCycles = 8; break;
    case 0xD0: if (!flagC()) { PC = pop16(); baseCycles = 20; } else baseCycles = 8; break;
    case 0xD8: if (flagC())  { PC = pop16(); baseCycles = 20; } else baseCycles = 8; break;
    
    // === RETI ===
    case 0xD9: PC = pop16(); IME = true; baseCycles = 16; break;
    
    // === RST ===
    case 0xC7: push16(PC); PC = 0x00; baseCycles = 16; break;
    case 0xCF: push16(PC); PC = 0x08; baseCycles = 16; break;
    case 0xD7: push16(PC); PC = 0x10; baseCycles = 16; break;
    case 0xDF: push16(PC); PC = 0x18; baseCycles = 16; break;
    case 0xE7: push16(PC); PC = 0x20; baseCycles = 16; break;
    case 0xEF: push16(PC); PC = 0x28; baseCycles = 16; break;
    case 0xF7: push16(PC); PC = 0x30; baseCycles = 16; break;
    case 0xFF: push16(PC); PC = 0x38; baseCycles = 16; break;
    
    // === EI / DI / HALT / STOP ===
    case 0xFB: ei_pending = true; baseCycles = 4; break;
    case 0xF3: IME = false; baseCycles = 4; break;
    case 0x76: { // HALT
        if (IME) halted = true;
        else {
            uint8_t ie = read8(0xFFFF), iff = read8(0xFF0F);
            if (ie & iff & 0x1F) halt_bug = true;
            else halted = true;
        }
        baseCycles = 4;
    } break;
    case 0x10: /* STOP - not implemented */ PC++; baseCycles = 4; break;
    
    // === CB prefix ===
    case 0xCB: baseCycles = execCB(); break;
    
    // === Invalid opcodes ===
    case 0xD3: case 0xDB: case 0xDD: case 0xE3: case 0xE4: case 0xEB: case 0xEC: case 0xED:
    case 0xF4: case 0xFC: case 0xFD:
        // These are invalid on Game Boy - they halt the CPU
        // We treat as NOP for now
        baseCycles = 4;
        break;
    
    default:
        // Invalid Game Boy opcode - halt or NOP
        fprintf(stderr, "Unknown opcode: 0x%02X at PC=0x%04X\n", op, PC-1);
        baseCycles = 4;
        break;
    }
    
    // Apply EI after this instruction completes
    if (wasEI) IME = true;
    
    cycles += baseCycles;
    return baseCycles;
}

// CB-prefixed instructions
static int execCB() {
    uint8_t op = cpu.read8(cpu.PC++);
    uint8_t* r8 = nullptr;
    uint16_t addr = 0;
    bool isHL = false;
    int bit = (op >> 3) & 0x7;
    
    // Determine operand
    switch (op & 0x07) {
        case 0: r8 = &cpu.B; break;
        case 1: r8 = &cpu.C; break;
        case 2: r8 = &cpu.D; break;
        case 3: r8 = &cpu.E; break;
        case 4: r8 = &cpu.H; break;
        case 5: r8 = &cpu.L; break;
        case 6: isHL = true; addr = cpu.HL(); break;
        case 7: r8 = &cpu.A; break;
    }
    
    auto readVal = [&]() -> uint8_t { return isHL ? cpu.read8(addr) : *r8; };
    auto writeVal = [&](uint8_t v) { if (isHL) cpu.write8(addr, v); else *r8 = v; };
    
    int cyc = isHL ? 16 : 8;
    
    switch (op >> 3) {
    // RLC (0x00-0x07)
    case 0: {
        uint8_t v = readVal(); bool c = (v & 0x80) != 0;
        v = (v << 1) | (c ? 1 : 0);
        writeVal(v);
        cpu.setZ(v == 0); cpu.setN(false); cpu.setH(false); cpu.setC(c);
    } break;
    
    // RRC (0x08-0x0F)
    case 1: {
        uint8_t v = readVal(); bool c = (v & 0x01) != 0;
        v = (v >> 1) | (c ? 0x80 : 0);
        writeVal(v);
        cpu.setZ(v == 0); cpu.setN(false); cpu.setH(false); cpu.setC(c);
    } break;
    
    // RL (0x10-0x17)
    case 2: {
        uint8_t v = readVal(); bool c = (v & 0x80) != 0;
        v = (v << 1) | (cpu.flagC() ? 1 : 0);
        writeVal(v);
        cpu.setZ(v == 0); cpu.setN(false); cpu.setH(false); cpu.setC(c);
    } break;
    
    // RR (0x18-0x1F)
    case 3: {
        uint8_t v = readVal(); bool c = (v & 0x01) != 0;
        v = (v >> 1) | (cpu.flagC() ? 0x80 : 0);
        writeVal(v);
        cpu.setZ(v == 0); cpu.setN(false); cpu.setH(false); cpu.setC(c);
    } break;
    
    // SLA (0x20-0x27)
    case 4: {
        uint8_t v = readVal(); bool c = (v & 0x80) != 0;
        v <<= 1;
        writeVal(v);
        cpu.setZ(v == 0); cpu.setN(false); cpu.setH(false); cpu.setC(c);
    } break;
    
    // SRA (0x28-0x2F)
    case 5: {
        uint8_t v = readVal(); bool c = (v & 0x01) != 0;
        v = (v >> 1) | (v & 0x80);
        writeVal(v);
        cpu.setZ(v == 0); cpu.setN(false); cpu.setH(false); cpu.setC(c);
    } break;
    
    // SWAP (0x30-0x37)
    case 6: {
        uint8_t v = readVal();
        v = ((v & 0xF0) >> 4) | ((v & 0x0F) << 4);
        writeVal(v);
        cpu.setZ(v == 0); cpu.setN(false); cpu.setH(false); cpu.setC(false);
    } break;
    
    // SRL (0x38-0x3F)
    case 7: {
        uint8_t v = readVal(); bool c = (v & 0x01) != 0;
        v >>= 1;
        writeVal(v);
        cpu.setZ(v == 0); cpu.setN(false); cpu.setH(false); cpu.setC(c);
    } break;
    
    // BIT (0x40-0x7F)
    case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15: {
        uint8_t v = readVal();
        cpu.setZ(!(v & (1 << bit)));
        cpu.setN(false); cpu.setH(true);
        cyc = isHL ? 12 : 8;
    } break;
    
    // RES (0x80-0xBF)
    case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
        writeVal(readVal() & ~(1 << bit));
        break;
    
    // SET (0xC0-0xFF)
    case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31:
        writeVal(readVal() | (1 << bit));
        break;
    }
    
    return cyc;
}

void CPU::handleInterrupts() {
    if (!IME) return;

    // Interrupt check: reading IE/IF is internal to the CPU, so it does not
    // advance the bus. Use direct memory access (not read8/write8).
    uint8_t ie = memory.read(0xFFFF);
    uint8_t iff = memory.read(0xFF0F);
    uint8_t pending = ie & iff & 0x1F;
    if (!pending) return;

    IME = false;
    halted = false;

    // Find highest priority interrupt
    for (int i = 0; i < 5; i++) {
        if (pending & (1 << i)) {
            memory.write(0xFF0F, iff & ~(1 << i));
            memory.write(--SP, PC >> 8);
            memory.write(--SP, PC & 0xFF);
            const uint16_t vectors[] = { 0x40, 0x48, 0x50, 0x58, 0x60 };
            PC = vectors[i];
            // Interrupt dispatch takes 5 M-cycles (20 T-cycles).
            tick_components(20);
            cycles += 20;
            return;
        }
    }
}
