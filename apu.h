#pragma once
#include <cstdint>

// Game Boy APU (sound). Functional implementation of all four channels
// (square 1/2, wave, noise) with length counter, volume envelope, frequency
// sweep (square 1), and the 512 Hz frame sequencer. Samples are generated at
// 44100 Hz into a ring buffer that the SDL audio code drains each frame.

struct SquareChannel {
    bool enabled = false;
    bool dac_on = false;
    uint8_t duty = 0;          // 0-3
    uint8_t env_volume = 0;    // current envelope volume (0-15)
    uint8_t initial_volume = 0;
    uint8_t env_period = 0;
    bool env_add = false;
    uint16_t freq = 0;         // 11-bit
    int length = 0;            // 0-64
    bool length_enable = false;
    // sweep (square 1 only)
    uint8_t sweep_period = 0;
    bool sweep_negate = false;
    uint8_t sweep_shift = 0;
    bool sweep_on = false;
    uint16_t shadow_freq = 0;
    int sweep_timer = 0;
    // internal
    uint16_t timer = 0;
    uint8_t duty_pos = 0;
    int env_timer = 0;

    static constexpr uint8_t DUTY[4] = { 0x01, 0x81, 0x87, 0x7E };

    void clock() {
        if (!enabled) return;
        if (--timer <= 0) {
            timer = (2048 - freq) * 4;
            duty_pos = (duty_pos + 1) & 7;
        }
    }
    void step_envelope() {
        if (env_period == 0) return;
        if (--env_timer <= 0) {
            env_timer = env_period;
            int v = env_volume;
            if (env_add) { if (v < 15) v++; }
            else { if (v > 0) v--; }
            env_volume = v;
        }
    }
    void step_length() {
        if (length_enable && length > 0) {
            if (--length == 0) enabled = false;
        }
    }
    void step_sweep() {
        if (!sweep_on || sweep_period == 0) return;
        if (--sweep_timer <= 0) {
            sweep_timer = sweep_period;
            uint16_t delta = shadow_freq >> sweep_shift;
            if (sweep_negate) {
                shadow_freq -= delta;
            } else {
                shadow_freq += delta;
                if (shadow_freq > 2047) { enabled = false; return; }
            }
            freq = shadow_freq;
            timer = (2048 - freq) * 4;
        }
    }
    void trigger() {
        enabled = dac_on;
        timer = (2048 - freq) * 4;
        duty_pos = 0;
        env_timer = env_period;
        env_volume = initial_volume;
        if (length == 0) length = 64;
    }
    int output() const {
        if (!enabled) return 0;
        return ((DUTY[duty] >> duty_pos) & 1) ? env_volume : 0;
    }
};

struct WaveChannel {
    bool enabled = false;
    bool dac_on = false;
    uint8_t volume_code = 0;  // 0=mute,1=100%,2=50%,3=25%
    uint16_t freq = 0;
    int length = 0;           // 0-256
    bool length_enable = false;
    uint16_t timer = 0;
    uint8_t sample_index = 0;

    void clock() {
        if (!enabled) return;
        if (--timer <= 0) {
            timer = (2048 - freq) * 2;
            sample_index = (sample_index + 1) & 31;
        }
    }
    void step_length() {
        if (length_enable && length > 0) {
            if (--length == 0) enabled = false;
        }
    }
    void trigger() {
        enabled = dac_on;
        timer = (2048 - freq) * 2;
        sample_index = 0;
        if (length == 0) length = 256;
    }
    int output(const uint8_t wave_ram[16]) const {
        if (!enabled || volume_code == 0) return 0;
        uint8_t byte = wave_ram[sample_index >> 1];
        uint8_t s = (sample_index & 1) ? (byte & 0x0F) : (byte >> 4);
        return s >> (volume_code - 1);
    }
};

struct NoiseChannel {
    bool enabled = false;
    bool dac_on = false;
    uint8_t env_volume = 0;
    uint8_t initial_volume = 0;
    uint8_t env_period = 0;
    bool env_add = false;
    int length = 0;
    bool length_enable = false;
    uint8_t divisor_code = 0;
    bool width_7 = false;
    uint8_t shift = 0;
    uint16_t timer = 0;
    uint16_t lfsr = 0x7FFF;
    int env_timer = 0;

    void clock() {
        if (!enabled) return;
        if (--timer <= 0) {
            int divisor = (divisor_code == 0) ? 8 : (16 * divisor_code);
            timer = divisor << shift;
            uint8_t bit = (lfsr ^ (lfsr >> 1)) & 1;
            lfsr = (lfsr >> 1) | (bit << 14);
            if (width_7) lfsr = (lfsr & ~0x40) | (bit << 6);
        }
    }
    void step_envelope() {
        if (env_period == 0) return;
        if (--env_timer <= 0) {
            env_timer = env_period;
            int v = env_volume;
            if (env_add) { if (v < 15) v++; }
            else { if (v > 0) v--; }
            env_volume = v;
        }
    }
    void step_length() {
        if (length_enable && length > 0) {
            if (--length == 0) enabled = false;
        }
    }
    void trigger() {
        enabled = dac_on;
        lfsr = 0x7FFF;
        int divisor = (divisor_code == 0) ? 8 : (16 * divisor_code);
        timer = divisor << shift;
        env_timer = env_period;
        env_volume = initial_volume;
        if (length == 0) length = 64;
    }
    int output() const {
        if (!enabled) return 0;
        return (lfsr & 1) ? 0 : env_volume;
    }
};

struct APU {
    SquareChannel sq1, sq2;
    WaveChannel wave;
    NoiseChannel noise;
    uint8_t wave_ram[16] = {0};
    uint8_t nr50 = 0x77, nr51 = 0xF3, nr52 = 0xF1;

    int frame_step = 0;
    int frame_timer = 0;
    float sample_acc = 0.0f;

    static constexpr int BUF_SIZE = 8192;
    int16_t samples[BUF_SIZE];
    int sample_count = 0;

    static constexpr int SAMPLE_RATE = 44100;
    static constexpr float T_PER_SAMPLE = 4194304.0f / SAMPLE_RATE;

    void step(int cycles) {
        for (int t = 0; t < cycles; t++) {
            // Frame sequencer (512 Hz)
            if (++frame_timer >= 8192) {
                frame_timer = 0;
                frame_step = (frame_step + 1) & 7;
                if (frame_step == 0 || frame_step == 2 || frame_step == 4 || frame_step == 6) {
                    sq1.step_length(); sq2.step_length(); wave.step_length(); noise.step_length();
                }
                if (frame_step == 2 || frame_step == 6) sq1.step_sweep();
                if (frame_step == 7) { sq1.step_envelope(); sq2.step_envelope(); noise.step_envelope(); }
            }

            // Clock the channel timers
            sq1.clock(); sq2.clock(); wave.clock(); noise.clock();

            // Sample generation
            sample_acc += 1.0f;
            if (sample_acc >= T_PER_SAMPLE) {
                sample_acc -= T_PER_SAMPLE;
                if (sample_count < BUF_SIZE) samples[sample_count++] = generate_sample();
            }
        }
    }

    int16_t generate_sample() const {
        int ch1 = sq1.output();
        int ch2 = sq2.output();
        int ch3 = wave.output(wave_ram);
        int ch4 = noise.output();

        int left = 0, right = 0;
        if (nr51 & 0x10) left += ch1;
        if (nr51 & 0x20) left += ch2;
        if (nr51 & 0x40) left += ch3;
        if (nr51 & 0x80) left += ch4;
        if (nr51 & 0x01) right += ch1;
        if (nr51 & 0x02) right += ch2;
        if (nr51 & 0x04) right += ch3;
        if (nr51 & 0x08) right += ch4;

        float lv = (nr50 >> 4) & 7;
        float rv = nr50 & 7;
        float l = (left / 60.0f) * (lv / 7.0f) * 2.0f - 1.0f;
        float r = (right / 60.0f) * (rv / 7.0f) * 2.0f - 1.0f;
        float mono = (l + r) * 0.5f * 0.75f;
        if (mono > 1.0f) mono = 1.0f;
        if (mono < -1.0f) mono = -1.0f;
        return (int16_t)(mono * 32767.0f);
    }

    void write(uint16_t addr, uint8_t v) {
        if (addr >= 0xFF30 && addr <= 0xFF3F) { wave_ram[addr - 0xFF30] = v; return; }
        switch (addr) {
        case 0xFF10:
            sq1.sweep_period = (v >> 4) & 7;
            sq1.sweep_negate = (v >> 3) & 1;
            sq1.sweep_shift = v & 7;
            sq1.sweep_timer = sq1.sweep_period ? sq1.sweep_period : 8;
            break;
        case 0xFF11:
            sq1.length = 64 - (v & 0x3F);
            sq1.duty = (v >> 6) & 3;
            break;
        case 0xFF12:
            sq1.env_period = v & 7;
            sq1.env_add = (v >> 3) & 1;
            sq1.initial_volume = (v >> 4) & 0xF;
            sq1.dac_on = (v & 0xF8) != 0;
            if (!sq1.dac_on) sq1.enabled = false;
            break;
        case 0xFF13:
            sq1.freq = (sq1.freq & 0x700) | v;
            break;
        case 0xFF14:
            sq1.freq = (sq1.freq & 0xFF) | ((v & 7) << 8);
            sq1.length_enable = (v >> 6) & 1;
            if (v & 0x80) {
                sq1.trigger();
                sq1.shadow_freq = sq1.freq;
                sq1.sweep_timer = sq1.sweep_period ? sq1.sweep_period : 8;
                sq1.sweep_on = (sq1.sweep_period || sq1.sweep_shift);
            }
            break;
        case 0xFF16:
            sq2.length = 64 - (v & 0x3F);
            sq2.duty = (v >> 6) & 3;
            break;
        case 0xFF17:
            sq2.env_period = v & 7;
            sq2.env_add = (v >> 3) & 1;
            sq2.initial_volume = (v >> 4) & 0xF;
            sq2.dac_on = (v & 0xF8) != 0;
            if (!sq2.dac_on) sq2.enabled = false;
            break;
        case 0xFF18:
            sq2.freq = (sq2.freq & 0x700) | v;
            break;
        case 0xFF19:
            sq2.freq = (sq2.freq & 0xFF) | ((v & 7) << 8);
            sq2.length_enable = (v >> 6) & 1;
            if (v & 0x80) sq2.trigger();
            break;
        case 0xFF1A:
            wave.dac_on = (v >> 7) & 1;
            if (!wave.dac_on) wave.enabled = false;
            break;
        case 0xFF1B:
            wave.length = 256 - v;
            break;
        case 0xFF1C:
            wave.volume_code = (v >> 5) & 3;
            break;
        case 0xFF1D:
            wave.freq = (wave.freq & 0x700) | v;
            break;
        case 0xFF1E:
            wave.freq = (wave.freq & 0xFF) | ((v & 7) << 8);
            wave.length_enable = (v >> 6) & 1;
            if (v & 0x80) wave.trigger();
            break;
        case 0xFF20:
            noise.length = 64 - (v & 0x3F);
            break;
        case 0xFF21:
            noise.env_period = v & 7;
            noise.env_add = (v >> 3) & 1;
            noise.initial_volume = (v >> 4) & 0xF;
            noise.dac_on = (v & 0xF8) != 0;
            if (!noise.dac_on) noise.enabled = false;
            break;
        case 0xFF22:
            noise.divisor_code = v & 7;
            noise.width_7 = (v >> 3) & 1;
            noise.shift = (v >> 4) & 0xF;
            break;
        case 0xFF23:
            noise.length_enable = (v >> 6) & 1;
            if (v & 0x80) noise.trigger();
            break;
        case 0xFF24:
            nr50 = v;
            break;
        case 0xFF25:
            nr51 = v;
            break;
        case 0xFF26:
            nr52 = (nr52 & 0x0F) | (v & 0x80);
            if (!(nr52 & 0x80)) {
                sq1.enabled = sq2.enabled = wave.enabled = noise.enabled = false;
            }
            break;
        default:
            break;
        }
    }

    uint8_t read(uint16_t addr) const {
        if (addr >= 0xFF30 && addr <= 0xFF3F) return wave_ram[addr - 0xFF30];
        switch (addr) {
        case 0xFF24: return nr50;
        case 0xFF25: return nr51;
        case 0xFF26: {
            uint8_t s = nr52 & 0x80;
            if (sq1.enabled) s |= 0x01;
            if (sq2.enabled) s |= 0x02;
            if (wave.enabled) s |= 0x04;
            if (noise.enabled) s |= 0x08;
            return s | 0x70;
        }
        default: return 0xFF;
        }
    }
};

extern APU apu;
