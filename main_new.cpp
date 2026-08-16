#include <stdio.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#include "memory.h"
#include "cpu_new.h"
#include "PPU.h"
#include "video.h"
#include "input.h"
#include "timer.h"
#include "mbc.h"
#include "apu.h"

Memory memory;
PPU ppu;
Input input;
Timer timer;
Cartridge cartridge;
APU apu;
uint8_t framebuffer[144][160];
bool g_headless = false;

// Advance the timer and PPU together by n T-cycles. Called by the CPU's
// per-access sync (see cpu_new.h) and by the main loop.
void tick_components(int n) {
    if (n <= 0) return;
    timer.step(n);
    ppu.step(n);
    if (!g_headless) apu.step(n);
}

static void init_fake_bios() {
    cpu.A = 0x01; cpu.F = 0xB0;
    cpu.B = 0x00; cpu.C = 0x13;
    cpu.D = 0x00; cpu.E = 0xD8;
    cpu.H = 0x01; cpu.L = 0x4D;
    cpu.PC = 0x0100;
    cpu.SP = 0xFFFE;

    // Timer
    memory.write(0xFF05, 0x00);
    memory.write(0xFF06, 0x00);
    memory.write(0xFF07, 0x00);

    // Sound (basic init)
    memory.write(0xFF10, 0x80); memory.write(0xFF11, 0xBF);
    memory.write(0xFF12, 0xF3); memory.write(0xFF14, 0xBF);
    memory.write(0xFF16, 0x3F); memory.write(0xFF17, 0x00);
    memory.write(0xFF19, 0xBF); memory.write(0xFF1A, 0x7F);
    memory.write(0xFF1B, 0xFF); memory.write(0xFF1C, 0x9F);
    memory.write(0xFF1E, 0xBF); memory.write(0xFF20, 0xFF);
    memory.write(0xFF21, 0x00); memory.write(0xFF22, 0x00);
    memory.write(0xFF23, 0xBF); memory.write(0xFF24, 0x77);
    memory.write(0xFF25, 0xF3); memory.write(0xFF26, 0xF1);

    // PPU
    memory.write(0xFF40, 0x91); // LCDC: LCD on, BG on
    memory.write(0xFF42, 0x00); memory.write(0xFF43, 0x00);
    memory.write(0xFF45, 0x00); memory.write(0xFF47, 0xFC);
    memory.write(0xFF48, 0xFF); memory.write(0xFF49, 0xFF);
    memory.write(0xFF4A, 0x00); memory.write(0xFF4B, 0x00);

    // Interrupts (IF = 0xE1 matches DMG post-boot state: VBlank pending)
    memory.write(0xFFFF, 0x00);
    memory.write(0xFF0F, 0xE1);
}

static void load_nintendo_logo() {
    const uint8_t logo[] = {
        0xF0,0x00,0xF0,0x00,0xFC,0x00,0xFC,0x00,0xFC,0x00,0xFC,0x00,0xF3,0x00,0xF3,0x00,
        0x3C,0x00,0x3C,0x00,0x3C,0x00,0x3C,0x00,0x3C,0x00,0x3C,0x00,0x3C,0x00,0x3C,0x00,
        0xF0,0x00,0xF0,0x00,0xF0,0x00,0xF0,0x00,0x00,0x00,0x00,0x00,0xF3,0x00,0xF3,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xCF,0x00,0xCF,0x00,
        0x00,0x00,0x00,0x00,0x0F,0x00,0x0F,0x00,0x3F,0x00,0x3F,0x00,0x0F,0x00,0x0F,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0x00,0xC0,0x00,0x0F,0x00,0x0F,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0x00,0xF0,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF3,0x00,0xF3,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0x00,0xC0,0x00,
        0x03,0x00,0x03,0x00,0x03,0x00,0x03,0x00,0x03,0x00,0x03,0x00,0xFF,0x00,0xFF,0x00,
        0xC0,0x00,0xC0,0x00,0xC0,0x00,0xC0,0x00,0xC0,0x00,0xC0,0x00,0xC3,0x00,0xC3,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0x00,0xFC,0x00,
        0xF3,0x00,0xF3,0x00,0xF0,0x00,0xF0,0x00,0xF0,0x00,0xF0,0x00,0xF0,0x00,0xF0,0x00,
        0x3C,0x00,0x3C,0x00,0xFC,0x00,0xFC,0x00,0xFC,0x00,0xFC,0x00,0x3C,0x00,0x3C,0x00,
        0xF3,0x00,0xF3,0x00,0xF3,0x00,0xF3,0x00,0xF3,0x00,0xF3,0x00,0xF3,0x00,0xF3,0x00,
        0xF3,0x00,0xF3,0x00,0xC3,0x00,0xC3,0x00,0xC3,0x00,0xC3,0x00,0xC3,0x00,0xC3,0x00,
        0xCF,0x00,0xCF,0x00,0xCF,0x00,0xCF,0x00,0xCF,0x00,0xCF,0x00,0xCF,0x00,0xCF,0x00,
        0x3C,0x00,0x3C,0x00,0x3F,0x00,0x3F,0x00,0x3C,0x00,0x3C,0x00,0x0F,0x00,0x0F,0x00,
        0x3C,0x00,0x3C,0x00,0xFC,0x00,0xFC,0x00,0x00,0x00,0x00,0x00,0xFC,0x00,0xFC,0x00,
        0xFC,0x00,0xFC,0x00,0xF0,0x00,0xF0,0x00,0xF0,0x00,0xF0,0x00,0xF0,0x00,0xF0,0x00,
        0xF3,0x00,0xF3,0x00,0xF3,0x00,0xF3,0x00,0xF3,0x00,0xF3,0x00,0xF0,0x00,0xF0,0x00,
        0xC3,0x00,0xC3,0x00,0xC3,0x00,0xC3,0x00,0xC3,0x00,0xC3,0x00,0xFF,0x00,0xFF,0x00,
        0xCF,0x00,0xCF,0x00,0xCF,0x00,0xCF,0x00,0xCF,0x00,0xCF,0x00,0xC3,0x00,0xC3,0x00,
        0x0F,0x00,0x0F,0x00,0x0F,0x00,0x0F,0x00,0x0F,0x00,0x0F,0x00,0xFC,0x00,0xFC,0x00,
        0x3C,0x00,0x42,0x00,0xB9,0x00,0xA5,0x00,0xB9,0x00,0xA5,0x00,0x42,0x00,0x3C,0x00,
    };
    for (int i = 0; i < 400; i++)
        memory.write(0x8010 + i, logo[i]);
}

static void load_logo_tilemap() {
    const uint8_t map[] = {
        0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,
        0x19,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
    };
    for (int i = 0; i < 48; i++)
        memory.write(0x9904 + i, map[i]);
}

static bool load_rom(const char* filename) {
    std::ifstream f(filename, std::ios::binary | std::ios::ate);
    if (!f) { fprintf(stderr, "No ROM: %s\n", filename); return false; }

    size_t size = f.tellg(); f.seekg(0);
    std::vector<uint8_t> rom(size);
    f.read((char*)rom.data(), size);

    if (size < 0x150) {
        fprintf(stderr, "ROM too small: %s (%zu bytes)\n", filename, size);
        return false;
    }

    uint8_t cart_type = rom[0x147];
    cartridge.load(rom, cart_type);

    // Patch any 0x00 interrupt vectors with RETI (safety net for ROMs that
    // don't initialize them; the real boot ROM leaves RETI in these slots).
    // Only the five interrupt vectors (0x40,0x48,0x50,0x58,0x60) - never the
    // RST vectors (0x00..0x38), which games legitimately use.
    for (int addr = 0x40; addr <= 0x60; addr += 8) {
        if (cartridge.rom[addr] == 0x00)
            cartridge.rom[addr] = 0xD9;  // RETI
    }

    fprintf(stderr, "Loaded ROM: %s (%zu bytes, MBC type 0x%02X)\n", filename, size, cart_type);
    return true;
}

// Write the 160x144 framebuffer to a 24-bit BMP file (bottom-up, BGR).
// Used in headless mode to visually verify PPU output.
static void write_bmp(const char* path, const uint8_t fb[144][160]) {
    const int w = 160, h = 144;
    const int row_size = (w * 3 + 3) & ~3;   // rows are 4-byte aligned
    const int data_size = row_size * h;
    const int file_size = 54 + data_size;

    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot write screenshot: %s\n", path); return; }

    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = file_size & 0xFF; hdr[3] = (file_size >> 8) & 0xFF;
    hdr[4] = (file_size >> 16) & 0xFF; hdr[5] = (file_size >> 24) & 0xFF;
    hdr[10] = 54;                       // pixel data offset
    hdr[14] = 40;                       // BITMAPINFOHEADER size
    hdr[18] = w & 0xFF; hdr[19] = (w >> 8) & 0xFF;
    hdr[22] = h & 0xFF; hdr[23] = (h >> 8) & 0xFF;
    hdr[26] = 1;                        // planes
    hdr[28] = 24;                       // bits per pixel
    hdr[34] = data_size & 0xFF; hdr[35] = (data_size >> 8) & 0xFF;
    hdr[36] = (data_size >> 16) & 0xFF; hdr[37] = (data_size >> 24) & 0xFF;
    fwrite(hdr, 1, 54, f);

    // DMG grayscale: 0 = lightest (white), 3 = darkest (black)
    const uint8_t shade[4] = { 255, 170, 85, 0 };
    for (int y = h - 1; y >= 0; y--) {  // BMP rows are bottom-up
        for (int x = 0; x < w; x++) {
            uint8_t v = shade[fb[y][x] & 0x03];
            uint8_t px[3] = { v, v, v };   // B, G, R
            fwrite(px, 1, 3, f);
        }
        uint8_t pad[3] = {0, 0, 0};
        fwrite(pad, 1, row_size - w * 3, f);
    }
    fclose(f);
    fprintf(stderr, "Wrote screenshot: %s\n", path);
}

int main(int argc, char** argv) {
    const char* rom_path = "bgbtest.gb";
    uint64_t max_cycles = 0; // 0 = run forever (non-headless)
    const char* screenshot_path = nullptr;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0 || strcmp(argv[i], "-h") == 0) {
            g_headless = true;
        } else if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            max_cycles = strtoull(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            // Convenience: one frame = 456 clocks * 154 scanlines
            max_cycles = strtoull(argv[++i], nullptr, 10) * 70224ull;
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else {
            rom_path = argv[i];
        }
    }

    // Wire up timer register callbacks before any register writes happen.
    memory.div_write_cb  = []() { timer.write_div(); };
    memory.tima_read_cb  = []() -> uint8_t { return timer.read_tima(); };
    memory.tima_write_cb = [](uint8_t v) { timer.write_tima(v); };
    memory.tma_write_cb  = [](uint8_t v) { timer.write_tma(v); };
    memory.tac_write_cb  = [](uint8_t v) { timer.write_tac(v); };
    memory.lyc_write_cb  = [](uint8_t v) { ppu.write_lyc(v); };
    memory.stat_write_cb = [](uint8_t v) { ppu.write_stat(v); };
    memory.apu_read_cb   = [](uint16_t a) { return apu.read(a); };
    memory.apu_write_cb  = [](uint16_t a, uint8_t v) { apu.write(a, v); };
    // Bus blocking follows the STAT-delayed mode (the same signal the STAT
    // register's mode bits report), so OAM/VRAM accessibility matches STAT
    // mode timing exactly (mooneye intr_2_oam_ok_timing).
    memory.ppu_mode_cb   = []() {
        int internal = ppu.mode;
        int stat = memory.data[0xFF41] & 0x03;
        if (stat == 3) return 3;                    // OAM+VRAM blocked (delayed)
        if (internal == 2) return 2;                // OAM blocked (mode 2)
        if (internal == 3 && !ppu.line0) return 3;  // normal-line mode 3 immediate
        return 0;
    };
    memory.ppu_write_mode_cb = []() {
        int internal = ppu.mode;
        int stat = memory.data[0xFF41] & 0x03;
        if (stat == 3) return 3;                    // OAM+VRAM blocked (mode 3)
        if (stat == 2 && internal == 2) return 2;   // OAM blocked (mode 2)
        return 0;
    };

    // Cartridge (MBC) callbacks.
    memory.cart_read_cb     = [](uint16_t a) { return cartridge.read(a); };
    memory.cart_write_cb    = [](uint16_t a, uint8_t v) { cartridge.write(a, v); };
    memory.cart_ram_read_cb = [](uint16_t a) { return cartridge.read_ram(a); };
    memory.cart_ram_write_cb = [](uint16_t a, uint8_t v) { cartridge.write_ram(a, v); };

    init_fake_bios();

    memset(framebuffer, 0, sizeof(framebuffer));
    load_nintendo_logo();
    load_logo_tilemap();

    if (!load_rom(rom_path)) {
        if (g_headless) return 1;
    }

    SDL_AudioDeviceID audio_dev = 0;
    if (!g_headless) {
        init_video();

        // Audio (APU): queue-generated mono 16-bit samples.
        SDL_InitSubSystem(SDL_INIT_AUDIO);
        SDL_AudioSpec want = {};
        want.freq = APU::SAMPLE_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 2048;
        audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
        if (audio_dev) {
            // Prime a little silence to avoid an initial underrun.
            int16_t silence[2048] = {0};
            SDL_QueueAudio(audio_dev, silence, sizeof(silence));
            SDL_PauseAudioDevice(audio_dev, 0);
        }
    }

    // Frame pacing: the Game Boy renders at ~59.73 fps.
    using steady_clock = std::chrono::steady_clock;
    auto frame_interval = std::chrono::duration<double>(70224.0 / 4194304.0);
    auto next_frame = steady_clock::now() + frame_interval;

    // cpu_instrs (DMG) needs ~55 emulated seconds = ~230M cycles.
    if (g_headless && max_cycles == 0) max_cycles = 250000000;

    bool running = true;
    uint64_t total_cycles = 0;
    SDL_Event e;

    while (running) {
        if (!g_headless) {
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)
                    input.key_event(e.key.keysym.sym, e.type == SDL_KEYDOWN);
            }
        }

        // HALT handling
        if (cpu.halted) {
            uint8_t ie = memory.read(0xFFFF);
            uint8_t iff = memory.read(0xFF0F);
            if (ie & iff & 0x1F) {
                cpu.halted = false;
                if (cpu.IME) {
                    cpu.handleInterrupts();  // service interrupt immediately
                } else {
                    cpu.halt_bug = true;     // HALT bug: re-execute next instruction
                }
            } else {
                tick_components(4);
                total_cycles += 4;
                continue;
            }
        }

        // Normal interrupt dispatch (no-op unless IME is set)
        cpu.handleInterrupts();

        int cyc = cpu.step();
        // Advance any instruction cycles not already covered by the CPU's
        // per-access bus sync (internal ALU cycles, etc.).
        tick_components(cyc - cpu.synced_cycle);
        total_cycles += cyc;
        cpu.cycles = 0;

        // A frame just finished: drain audio and pace to real time.
        if (!g_headless && ppu.frame_ready) {
            ppu.frame_ready = false;
            if (audio_dev && apu.sample_count > 0) {
                SDL_QueueAudio(audio_dev, apu.samples, apu.sample_count * sizeof(int16_t));
                apu.sample_count = 0;
            }
            std::this_thread::sleep_until(next_frame);
            next_frame += frame_interval;
        }

        if (g_headless && total_cycles >= max_cycles) {
            running = false;
        }
    }

    if (audio_dev) SDL_CloseAudioDevice(audio_dev);

    if (screenshot_path) write_bmp(screenshot_path, framebuffer);
    if (!g_headless) cleanup_video();
    return 0;
}
