# 🕹️ Game Boy Emulator

A cycle-accurate **Game Boy (DMG-01)** emulator written from scratch in C++.

This project exists to learn **how a computer system actually works** — the
CPU microarchitecture, the memory map and bus timing, the pixel pipeline, the
sound hardware, and the memory-bank controllers. Every subsystem is modeled
close to the real silicon and verified against the hardware test suites
(blargg + mooneye).

---

## ✅ What works

| Subsystem | Status |
|-----------|--------|
| **CPU** (SM83/LR35902, all opcodes + CB, interrupts, HALT bug) | ✅ blargg `cpu_instrs` + `instr_timing` |
| **PPU** (background/window/sprites, per-T-cycle mode timing, sprite-FIFO, LCD-on quirk, OAM corruption bug) | ✅ 11/12 mooneye PPU tests |
| **Timer** (cycle-accurate, reload delay, glitches) | ✅ 10/10 mooneye timer |
| **MBC1 / MBC2 / MBC3 / MBC5** (banking + MBC3 RTC) | ✅ mooneye MBC |
| **APU** (square 1/2, wave, noise, envelope, sweep, frame sequencer) | ✅ audible via SDL |
| **Joypad** | ✅ keyboard |
| **Battery save** (`.sav`), **save states** (F5/F7) | ✅ |
| **Frame pacing** (~59.73 fps) | ✅ |
| **Game library** (import & pick ROMs from a launcher screen) | ✅ |

### Test scoreboard
- `run_tests.sh` — **50/50** (CPU + timer + MBC)
- mooneye PPU — **11/12** (one sprite-fetch edge case is 1 dot off)
- blargg `halt_bug`, `mem_timing`, `mem_timing-2` — **pass**
- blargg `oam_bug` — **6/8** (exact corruption-pattern CRC checks remain)

Still pending: `dmg_sound` / `dmg-acid2` (ROMs not bundled), serial link, and the
last 2 `oam_bug` sub-tests. Full detail lives in [`GOALS.md`](GOALS.md).

---

## 🧠 Why this project

The goal is to understand the machine bottom-up, the way an electrical &
computer engineer would:

- **CPU** — how instructions are fetched, decoded, and how each memory access
  is interleaved with the rest of the system on a per-T-cycle basis.
- **Memory & bus** — the 64 KB address space, echo RAM, and why VRAM/OAM are
  *blocked* during certain PPU modes.
- **PPU** — the 456-dot scanline, mode 2/3/0 timing, the pixel FIFO, sprite
  fetch penalties, and the STAT interrupt quirks.
- **Timer & interrupts** — the shared DIV counter, falling-edge clocking, and
  the interrupt priority chain.
- **APU** — square/wave/noise synthesis, envelopes, sweep, and the 512 Hz frame
  sequencer.
- **Cartridges** — how MBCs page ROM/RAM into the address space, and the RTC.

Each of these maps to a real hardware behavior you can read about and then
*see pass* a hardware test ROM.

---

## 🛠️ Build & Run

### Prerequisites
- CMake 3.16+
- SDL2
- (header-only `json.hpp` is bundled)

### Build
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Run
```bash
./play.sh                 # opens the game library launcher
./play.sh mygame.gb       # plays a specific ROM directly
./build/gameboy_emu.exe rom.gb --headless --cycles 500000000   # test ROMs
./build/gameboy_emu.exe --import path/to/rom.gb                 # add a ROM to the library
```

Running with no ROM argument opens the **library screen**: a launcher that
lists every game in `roms/`, lets you **import** new `.gb`/`.gbc` files from
disk, **remove** them, and **pick** one to play. Press **Esc** in-game to
return to the library.

### Library controls
| Key | Action |
|-----|--------|
| Up / Down | Move selection |
| Enter | Play selected game |
| I | Import a ROM (opens a file browser) |
| Del / Backspace | Remove selected game |
| Esc | Quit / cancel |

### Controls
### In-game controls
| Key | Game Boy button |
|-----|-----------------|
| Arrow keys (or WASD) | D-Pad |
| Z (or K) | A |
| X (or J) | B |
| Enter / Space | Start |
| Left/Right Shift | Select |
| F5 / F7 | Save / Load state |
| Esc | Return to library |

---

## 📁 Project structure

```
src/
├── core/
│   ├── cpu.cpp/.h   # SM83 CPU — one instruction per step(), per-access bus sync
│   └── memory.h     # 64 KB address space + MMIO routing + bus blocking
├── ppu/
│   ├── ppu.h/.cpp   # pixel pipeline, mode timing, sprite FIFO, OAM bug
│   └── video.h/.cpp # SDL2 window/texture
├── apu/
│   └── apu.h        # 4-channel audio + frame sequencer
├── cart/
│   └── mbc.h        # MBC1/2/3/5 banking + external RAM + RTC
├── timer/
│   └── timer.h      # cycle-accurate timer (shared divider, reload state machine)
├── input/
│   └── input.h      # joypad + keyboard
├── ui/
│   ├── font.h       # embedded 5x7 bitmap font (no SDL_ttf dependency)
│   ├── library.h/.cpp  # library launcher + ROM import/file browser
└── main.cpp         # wiring, main loop, frame pacing, save states

roms/                # game library (drop .gb/.gbc files here)
tools/gen_font.py    # regenerates src/ui/font.h
legacy/              # first-iteration scaffolding (not built)
├── main.cpp
├── CPU.h
└── opcodes.json
```

Plus `GOALS.md` (milestones + scoreboard) and `AGENTS.md` (architecture notes).

---

## 🎯 Roadmap

- [x] CPU core + instruction timing
- [x] Timer, MBC1/2/3/5 + RTC
- [x] Cycle-accurate PPU (mode timing, sprite FIFO, LCD-on quirk)
- [x] APU (4 channels + SDL output)
- [x] Battery save + save states
- [x] Frame pacing
- [x] Game library (import / select / remove ROMs)
- [ ] `dmg_sound` + `dmg-acid2` verification
- [ ] `oam_bug` exact-pattern sub-tests
- [ ] Serial link (2-player)
- [ ] Debugger with breakpoints
- [ ] GBC support
