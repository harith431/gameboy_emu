# 🕹️ Game Boy Emulator

A lightweight Game Boy emulator project focused on replicating the original hardware architecture. The goal is to emulate the core functionality of the Game Boy across different platforms, with both software simulation and potential hardware integration (e.g., embedded systems, FPGA, or retro handhelds).

---

## 🚧 Current Status (June 2025)

- ✅ CPU core implemented (instruction fetch-decode-execute cycle)
- ✅ PC advancement fixed using instruction length
- ✅ Most base opcodes are working correctly
- ⚠️ CB-prefixed and interrupt-related opcodes in progress
- ❌ PPU (graphics), audio, and input systems not yet implemented

---

## 🎯 Project Goals

- Emulate Game Boy architecture (CPU, PPU, APU, MMU)
- Simulate hardware behavior on:
  - Desktop platforms (cross-platform)
  - Embedded systems (e.g., Arduino, STM32)
  - FPGA platforms (future goal)
- Explore hardware-software co-design and system-level integration

---

## 🛠️ Build & Run

> ⚠️ Basic emulator loop only; no display or sound yet

