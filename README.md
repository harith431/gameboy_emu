# 🕹️ Game Boy Emulator

A C++ Game Boy (DMG-01) emulator using SDL3 for cross-platform rendering.

---

## 🚧 Current Status

- ✅ CPU core: all blargg CPU + instruction-timing tests pass
- ✅ PPU: background, window, and sprite rendering with scanline timing
- ✅ SDL2 video output (160×144 scaled)
- ✅ Input/joypad via SDL keyboard events
- ✅ Timer (DIV/TIMA/TMA/TAC) — cycle-accurate; all mooneye timer tests pass
- ✅ Interrupt handling (VBlank, STAT, Timer, Joypad)
- ✅ MBC1 / MBC2 / MBC5 / MBC3 (banking, no RTC) — mooneye MBC tests pass
- ✅ OAM DMA
- ⚠️ PPU timing is scanline-based (mem_timing / oam_bug tests fail)
- ✅ Audio (APU): square 1/2, wave, noise + frame sequencer, SDL output
- ❌ Serial link (output-only for test ROMs)

---

## 🛠️ Build & Run

### Prerequisites
- CMake 3.16+
- SDL3
- nlohmann-json

### Build
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Run
```bash
./gameboy_emu rom.gb                    # windowed (interactive)
./gameboy_emu rom.gb --headless         # no window; run 250M cycles
./gameboy_emu rom.gb --headless --frames 300
./gameboy_emu rom.gb --headless --cycles 500000000
./gameboy_emu rom.gb --headless --frames 60 --screenshot shot.bmp
```
Test ROM serial output (blargg/mooneye) is printed to stdout.

### Controls
| Key | Game Boy Button |
|-----|----------------|
| Arrow keys (or WASD) | D-Pad |
| Z (or K) | A |
| X (or J) | B |
| Enter / Space | Start |
| Left/Right Shift | Select |
| F5 / F7 | Save / Load state |

---

## 📁 Project Structure
```
├── CPU.h          # CPU registers, flags, interrupt control
├── memory.h       # 64KB address space with ROM write protection
├── PPU.h          # Pixel Processing Unit (scanline renderer)
├── PPU.cpp        # PPU instantiation
├── video.h/.cpp   # SDL3 video output
├── input.h        # Joypad input handling
├── timer.h        # DIV/TIMA/TMA/TAC timer
├── main.cpp       # ROM loading, CPU loop, emulator main
├── opcodes.json   # Complete opcode metadata (512 + 256 CB)
├── CMakeLists.txt # Build system
└── AGENTS.md      # Developer documentation
```

---

## 🎯 Roadmap
- [x] MBC1/MBC2/MBC5 support (and MBC3 banking, no RTC)
- [ ] MBC3 RTC (real-time clock)
- [ ] Audio (APU) - Square waves, wave, noise
- [ ] Cycle-accurate PPU timing (STAT interrupts, mode timing)
- [x] Save states (F5 save, F7 load)
- [ ] Debugger with breakpoints
- [ ] GBC support (SM83 double-speed mode)

