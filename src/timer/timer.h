#pragma once
#include <cstdint>
#include "core/memory.h"

// Game Boy Timer (cycle-accurate model).
//
// DIV (0xFF04), TIMA (0xFF05), TMA (0xFF06) and TAC (0xFF07) all share one
// 16-bit internal counter that is incremented once per M-cycle (4 T-cycles).
// DIV is the upper 8 bits of that counter. TIMA is clocked by the *falling
// edge* of a selected bit of the same counter:
//
//   TAC 00 -> bit 9  (period 1024 T-cycles = 4096 Hz)
//   TAC 01 -> bit 3  (period   16 T-cycles = 262144 Hz)
//   TAC 10 -> bit 5  (period   64 T-cycles = 65536 Hz)
//   TAC 11 -> bit 7  (period  256 T-cycles = 16384 Hz)
//
// TIMA overflow is followed by a 3-state reload machine (RUNNING ->
// RELOADING -> RELOADED -> RUNNING). While RELOADING, TIMA reads back 0 for
// one M-cycle; the Timer interrupt is requested when transitioning
// RELOADING -> RELOADED. Writes to TIMA/TMA/DIV/TAC during this window have
// special semantics (see below). This matches SameBoy and passes the
// mooneye timer tests.
struct Timer {
    uint16_t div_counter = 0;
    enum State { RUNNING, RELOADING, RELOADED } tima_state = RUNNING;

    static uint16_t trigger_bit(uint8_t tac) {
        switch (tac & 0x03) {
            case 0: return 512; // bit 9
            case 1: return 8;   // bit 3
            case 2: return 32;  // bit 5
            case 3: return 128; // bit 7
            default: return 512;
        }
    }

    void increase_tima() {
        uint8_t tima = memory.data[0xFF05] + 1;
        if (tima == 0) {
            // Overflow: TIMA is loaded with TMA immediately, but reads back 0
            // for one M-cycle and the interrupt is delayed one M-cycle.
            memory.data[0xFF05] = memory.data[0xFF06];
            tima_state = RELOADING;
        } else {
            memory.data[0xFF05] = tima;
        }
    }

    void set_internal_div_counter(uint16_t value) {
        // Bits that transitioned 1 -> 0 form the falling edges.
        uint16_t triggers = div_counter & ~value;
        uint8_t tac = memory.data[0xFF07];
        if ((tac & 0x04) && (triggers & trigger_bit(tac))) {
            increase_tima();
        }
        div_counter = value;
        memory.data[0xFF04] = value >> 8;
    }

    void advance_state_machine() {
        if (tima_state == RELOADED) {
            tima_state = RUNNING;
        } else if (tima_state == RELOADING) {
            // Request the Timer interrupt one M-cycle after the overflow.
            memory.data[0xFF0F] = (memory.data[0xFF0F] & 0x1F) | 0x04;
            tima_state = RELOADED;
        }
    }

    void step(int cycles) {
        // Advance in whole M-cycles (4 T-cycles each). Every CPU instruction
        // takes a multiple of 4 cycles, so there is no remainder.
        while (cycles >= 4) {
            advance_state_machine();
            set_internal_div_counter(div_counter + 4);
            cycles -= 4;
        }
    }

    // --- Register access (called from Memory) ---

    uint8_t read_tima() {
        if (tima_state == RELOADING) return 0;
        return memory.data[0xFF05];
    }

    void write_tima(uint8_t value) {
        // A TIMA write is ignored only in the RELOADED state (the M-cycle
        // right after the interrupt fires).
        if (tima_state != RELOADED) {
            memory.data[0xFF05] = value;
        }
    }

    void write_tma(uint8_t value) {
        memory.data[0xFF06] = value;
        // While not running normally, TMA writes also update TIMA.
        if (tima_state != RUNNING) {
            memory.data[0xFF05] = value;
        }
    }

    void write_tac(uint8_t new_tac) {
        uint8_t old_tac = memory.data[0xFF07];
        // Writing TAC can glitch TIMA (this is what makes the rapid_toggle
        // test observe "unexpected" timer increases).
        if (old_tac & 0x04) {
            uint16_t old_bit = trigger_bit(old_tac);
            uint16_t new_bit = trigger_bit(new_tac);
            if (div_counter & old_bit) {
                if (!(new_tac & 0x04) || !(div_counter & new_bit)) {
                    increase_tima();
                }
            }
        }
        memory.data[0xFF07] = new_tac;
    }

    void write_div() {
        // Any DIV write resets the shared internal counter to 0. If the
        // selected TIMA clock bit was high, this creates a falling edge and
        // increments TIMA.
        set_internal_div_counter(0);
    }
};

extern Timer timer;
