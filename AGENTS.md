# Game Boy Emulator - Project Context

> **GOAL: see `GOALS.md`** for the project objective, milestones, and
> definition of "working emulator". Work through its milestones in order and
> run `./run_tests.sh` after every change (no regressions).

## Thinking level & model guidance

> Pi's thinking level is a **harness setting** (Shift+Tab to cycle, `/thinking`
> to set); the agent cannot switch it itself. This project has two tiers —
> pick the right one per task.

| Task type | Thinking level |
|-----------|----------------|
| Routine edits, docs, refactors, running `./run_tests.sh`, simple one-line fixes | `low`–`medium` |
| Cycle-accurate timing (PPU/APU/timer), MBC/banking edge cases, blargg or mooneye failures, subtle glitches, save/load correctness | `high` |

If a task falls in the `high` tier, say **"this needs high thinking"** up front
so the human can bump it (Shift+Tab) before the expensive debugging begins.

## Project Overview
A C++ Game Boy (DMG-01) emulator targeting the desktop with **SDL2**. The goal
is cycle-accurate emulation of the original hardware, verified against the
blargg + mooneye test suites.

## Architecture

Code lives under `src/`, organized by subsystem:

- **core/cpu.cpp/.h** - SM83 (LR35902) CPU: registers, flags, interrupts, HALT bug
- **core/memory.h** - 64KB address space + MMIO routing + bus blocking
- **ppu/ppu.h/.cpp** - Pixel Processing Unit: T-cycle mode timing, sprite FIFO, OAM bug
- **ppu/video.h/.cpp** - SDL2 window/texture (160×144)
- **apu/apu.h** - APU: square 1/2, wave, noise + frame sequencer
- **cart/mbc.h** - Memory Bank Controllers: MBC1, MBC2, MBC3 (with RTC), MBC5
- **timer/timer.h** - DIV/TIMA/TMA/TAC timer (cycle-accurate: shared divider,
  TIMA reload delay, DIV/TAC write glitches)
- **input/input.h** - Joypad register and SDL keyboard mapping
- **ui/font.h** - embedded 5x7 bitmap font (no SDL_ttf dependency)
- **ui/library.h/.cpp** - game library launcher (import / select / remove ROMs)
- **main.cpp** - ROM loading, wiring, main loop, frame pacing, save states

`roms/` is the game library folder. `legacy/` holds the first-iteration
scaffolding (`main.cpp`, `CPU.h`, `opcodes.json`) — not part of the build.
`tools/gen_font.py` regenerates `src/ui/font.h`.

### Memory Map
| Range | Description |
|-------|-------------|
| 0x0000-0x3FFF | ROM Bank 0 (fixed) |
| 0x4000-0x7FFF | ROM Bank 1 (switchable via MBC) |
| 0x8000-0x9FFF | VRAM (8KB) |
| 0xA000-0xBFFF | External RAM (MBC banking) |
| 0xC000-0xDFFF | WRAM |
| 0xE000-0xFDFF | Echo RAM (mirror of C000-DDFF) |
| 0xFE00-0xFE9F | OAM (Sprite Attribute Table) |
| 0xFEA0-0xFEFF | Unusable |
| 0xFF00-0xFF7F | I/O Registers |
| 0xFF80-0xFFFE | HRAM |
| 0xFFFF | IE (Interrupt Enable) |

### Key I/O Registers
- 0xFF00 - Joypad
- 0xFF01-02 - Serial (output-only; test ROMs print to stdout)
- 0xFF04 - DIV (Divider)
- 0xFF05 - TIMA (Timer counter)
- 0xFF06 - TMA (Timer modulo)
- 0xFF07 - TAC (Timer control)
- 0xFF0F - IF (Interrupt Flag)
- 0xFF40 - LCDC (LCD Control)
- 0xFF41 - STAT (LCD Status)
- 0xFF42-43 - SCY, SCX (Scroll)
- 0xFF44 - LY (Scanline)
- 0xFF45 - LYC (LY Compare)
- 0xFF46 - DMA (OAM DMA, implemented)
- 0xFF47 - BGP (Background Palette)
- 0xFF48-49 - OBP0, OBP1 (Object Palettes)
- 0xFF4A-4B - WY, WX (Window position)
- 0xFFFF - IE (Interrupt Enable)

### Interrupts (priority order)
1. VBlank  (bit 0, vector 0x40)
2. STAT    (bit 1, vector 0x48)
3. Timer   (bit 2, vector 0x50)
4. Serial  (bit 3, vector 0x58)
5. Joypad  (bit 4, vector 0x60)

## Build Instructions
```bash
# Prerequisites: SDL2, CMake 3.16+
mkdir build && cd build
cmake ..
cmake --build .
```

## Run
```bash
./play.sh                 # library launcher
./play.sh mygame.gb       # play a specific ROM
./build/gameboy_emu.exe --import path/to/rom.gb   # add a ROM to roms/
./build/gameboy_emu.exe ROM --headless --cycles N  # run a test ROM headless
```

## Known Limitations
- Serial link is output-only (no 2-player transfer) — enough for test ROMs
- No MBC1 multicart mode (not detectable from the ROM header)
- No CGB double-speed mode / GBC-only registers (DMG only)
- `dmg_sound` / `dmg-acid2` not yet verified; `oam_bug` exact-pattern CRC
  sub-tests (2 of 8) still pending

## Testing
`./run_tests.sh` runs the blargg + mooneye suites in `test_roms/` and prints a
PASS/FAIL summary (currently **50/50**). Screenshot tests (`halt_bug`,
`oam_bug`, `mem_timing-2`, `dmg-acid2`) report via the screen, not serial:

```bash
./build/gameboy_emu.exe ROM --headless --frames 120 --screenshot out.bmp
```

## Invariants
- CPU instructions always take a multiple of 4 T-cycles; the CPU's per-access
  bus sync (`sync_bus`) interleaves timer/PPU/APU exactly.
- All MMIO routing goes through `Memory::read/write`; never touch
  `memory.data[...]` directly except for the PPU-owned STAT/LY bits (see PPU.h).
- Keep `--headless` working: test ROMs must print results to stdout.
- After every change, run `./run_tests.sh` — no regressions allowed.
- Keep `GOALS.md` "Current State" and checkboxes up to date.
