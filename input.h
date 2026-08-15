#pragma once
#include <cstdint>

struct Input {
    bool up = false, down = false, left = false, right = false;
    bool a = false, b = false, select = false, start = false;

    void key_event(int keycode, bool pressed) {
        switch (keycode) {
            case 1073741906: up = pressed; break;     // SDLK_UP
            case 1073741905: down = pressed; break;   // SDLK_DOWN
            case 1073741904: left = pressed; break;   // SDLK_LEFT
            case 1073741903: right = pressed; break;  // SDLK_RIGHT
            case 122: a = pressed; break;             // Z
            case 120: b = pressed; break;             // X
            case 1073742054: select = pressed; break; // RSHIFT
            case 1073742049: select = pressed; break; // LSHIFT
            case 13: start = pressed; break;          // ENTER
            case 32: start = pressed; break;          // SPACE
            default: break;
        }
    }
};

extern Input input;
