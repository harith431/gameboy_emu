# Goal: A Working Game Boy Emulator

> This file is the single source of truth for the project's objective. Work
> through the milestones in order until every box in "Definition of Done" is
> checked. Update "Current State" whenever a milestone completes.

## Objective

Build a **cycle-accurate Game Boy (DMG-01) emulator** in C++ that passes the
standard blargg + mooneye test suites and runs commercial games with correct
graphics, input, and audio.

---

## Definition of Done

All of the following must be true:

- [ ] **blargg** — all DMG tests pass:
  - [x] `cpu_instrs` (and the 11 individual `cpu_XX` tests)
  - [x] `instr_timing`
  - [ ] `mem_timing` (and `mem_timing-2` via screenshot)
  - [ ] `halt_bug` (screenshot)
  - [ ] `oam_bug` (screenshot)
  - [ ] `dmg_sound`
- [ ] **mooneye** — all `acceptance/` tests that apply to DMG pass
  (timer done; PPU, interrupts, OAM DMA, serial, boot, misc pending)
- [ ] **dmg-acid2** renders the reference image correctly
- [ ] **APU** — square 1/2, wave, noise channels with length/envelope/sweep,
  audible through SDL at correct pitch
- [ ] **Real games** — at least these boot and are playable (correct graphics,
  input, audio, no crashes): Tetris, Super Mario Land, The Legend of Zelda:
  Link's Awakening, Pokémon Red/Blue, Kirby's Dream Land
- [ ] **Battery save** — `.sav` persistence for MBC RAM+battery carts
- [ ] **60 FPS** — sustained in windowed mode on a typical desktop

---

## Current State (baseline)

Passing (50/50 in `run_tests.sh`):

| Subsystem | Status |
|-----------|--------|
| CPU (all opcodes incl. CB, flags, interrupts) | ✅ blargg `cpu_instrs` + `instr_timing` pass |
| Timer (cycle-accurate, reload delay, DIV/TAC glitches) | ✅ 10/10 mooneye timer tests |
| MBC1 / MBC2 / MBC5 / MBC3 (banking, no RTC) | ✅ mooneye MBC tests |
| PPU (background / window / sprites) | ⚠️ per-T-cycle mode timing + CPU interleave; 6/12 mooneye PPU tests |
| APU | ❌ not implemented |
| Serial | ⚠️ output-only (enough for test ROMs) |

PPU tests passing: `intr_1_2_timing`, `intr_2_0_timing`, `intr_2_mode0_timing`,
`intr_2_mode3_timing`, `stat_lyc_onoff`, `vblank_stat_intr`.

PPU tests still failing (need variable mode-3 length, STAT IRQ blocking, and the
LCD-on 2-cycle offset): `intr_2_mode0_timing_sprites`, `intr_2_oam_ok_timing`,
`hblank_ly_scx_timing`, `lcdon_timing`, `lcdon_write_timing`, `stat_irq_blocking`.
Also still failing/unverified: `mem_timing`, `halt_bug`, `oam_bug`, `dmg_sound`,
`dmg-acid2`, and the mooneye OAM-DMA / serial / boot acceptance tests.

---

## Milestones (do these in order)

### M1 — Core correctness (DONE ✅)
CPU + timer + MBC. Deliverable: `run_tests.sh` shows 50/50 pass.

### M2 — Cycle-accurate PPU (in progress)
The PPU now advances per T-cycle, interleaved with the CPU via per-access bus
sync. Remaining work:

- [x] Per-T-cycle mode timing: mode 2 (80) → 3 → 0 → 1, 456 dots/scanline
- [x] STAT interrupts on mode transitions + LYC=LY coincidence (edge-triggered)
- [x] LYC write semantics, LCD on/off transitions, VBlank + line-144 mode-2 int
- [ ] Variable mode-3 length (sprite count + SCX alignment)
- [ ] STAT IRQ blocking (level-sensitive internal interrupt line)
- [ ] LCD-on 2-cycle offset quirk
- [ ] OAM DMA timing (blocks CPU/bus for 160 cycles)
- [ ] Result: blargg `mem_timing`, `halt_bug`, `oam_bug` pass; mooneye
      `acceptance/ppu/*`, `acceptance/oam_dma/*` pass
- [ ] `dmg-acid2` renders correctly (compare screenshot to reference)

### M3 — APU (audio)
Implement the sound hardware and SDL audio output.

Deliverables / acceptance:
- [ ] Frame sequencer (512 Hz), 4 channels: square 1/2, wave, noise
- [ ] Length counter, volume envelope, frequency sweep, DAC
- [ ] NR10–NR52 register semantics
- [ ] blargg `dmg_sound` passes (register + behavior tests)
- [ ] Audible, pitch-accurate sound in windowed mode

### M4 — Full mooneye acceptance + peripherals
Close the remaining accuracy gaps.

Deliverables / acceptance:
- [ ] `acceptance/interrupts/*`, `acceptance/serial/*`,
      `acceptance/boot_*`, `acceptance/timer/*` (already green), `misc/*`
- [ ] Full joypad behavior (P1 select bits, button matrix)
- [ ] Serial link (transfer + interrupts), not just print-to-stdout
- [ ] OAM DMA + general DMA edge cases
- [ ] `interrupt_time` (CGB-only, may legitimately fail on DMG — document it)

### M5 — Real-game compatibility
Deliverables / acceptance:
- [ ] The 5 games in "Definition of Done" boot and play correctly
- [ ] Battery save/load (`*.sav`) for carts with MBC RAM+battery
- [ ] 60 FPS sustained; no audio crackle
- [ ] Fix any game-specific bugs found (e.g. sprite priority, window, timer,
      halt, MBC edge cases)

---

## Test infrastructure

ROMs live in `test_roms/`. The full compiled collection is available as a
release zip:
`https://github.com/c-sp/game-boy-test-roms/releases/download/v7.0/game-boy-test-roms-v7.0.zip`
(extract `mooneye-test-suite/` and `blargg/` as needed).

Run one test headless:

```bash
PATH="/c/msys64/ucrt64/bin:$PATH" ./build/gameboy_emu.exe ROM --headless --cycles N 2>/dev/null
```

### Pass/fail detection
- **mooneye**: PASS = serial sends bytes `03 05 08 0D 15 22`;
  FAIL = `42 42 42 42 42 42` ("BBBBBB").
- **blargg**: prints ASCII — `Passed` / `Passed all tests` / `Failed`.

`./run_tests.sh` automates this and prints a PASS/FAIL summary.

### Screenshot tests
Tests like `halt_bug`, `oam_bug`, `mem_timing-2`, and `dmg-acid2` report via
the **screen**, not serial. Use:

```bash
./build/gameboy_emu.exe ROM --headless --frames 120 --screenshot out.bmp
```

and compare `out.bmp` against the reference PNGs in
`test_roms/src/*-expected/` (or the reference images shipped with the test).

---

## Architecture notes

Key files (the `*_new.*` files are the live ones; `main.cpp`/`CPU.h` are legacy):

| File | Role |
|------|------|
| `cpu_new.cpp/.h` | SM83 CPU, one instruction per `step()` |
| `memory.h` | 64 KB address space + MMIO; routes to timer/MBC/PPU via callbacks |
| `timer.h` | cycle-accurate timer (shared divider, reload state machine) |
| `mbc.h` | MBC1/2/3/5 banking + external RAM |
| `PPU.h` | rendering + mode state (scanline-based — needs the M2 rewrite) |
| `video.cpp/.h` | SDL2 window/texture |
| `input.h` | joypad + keyboard |
| `main_new.cpp` | wiring, ROM load, main loop, CLI flags, BMP dump |

Invariants to preserve:
- CPU instructions always take a multiple of 4 cycles (T-cycles); the timer and
  PPU currently advance by the whole instruction's cycle count *after* the CPU
  step. M2 changes this to T-cycle interleaving — do it without regressing
  `instr_timing` (CPU) or the mooneye timer tests.
- All MMIO routing goes through `Memory::read/write`; never touch
  `memory.data[...]` directly except for the PPU-owned STAT/LY bits (see PPU.h).
- Keep `--headless` working: test ROMs must print results to stdout.

### Work rules
1. After every change, run `./run_tests.sh` — no regressions allowed.
2. Keep `GOALS.md` "Current State" and the checkboxes up to date.
3. Prefer small, testable commits; one milestone (or sub-milestone) per change.
4. If a test's expected behavior is unclear, read the test's `.s` source in
   `https://github.com/Gekkio/mooneye-test-suite` (it documents the hardware
   behavior it checks) or Gekkio's gb-ctr reference.
