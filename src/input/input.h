#pragma once
#include <cstdint>
#include <SDL2/SDL_keycode.h>

struct Input {
    bool up = false, down = false, left = false, right = false;
    bool a = false, b = false, select = false, start = false;

    void key_event(int keycode, bool pressed) {
        switch (keycode) {
            // D-pad: arrow keys (primary) and WASD (alternative)
            case SDLK_UP:    case SDLK_w: up = pressed;    break;
            case SDLK_DOWN:  case SDLK_s: down = pressed;  break;
            case SDLK_LEFT:  case SDLK_a: left = pressed;  break;
            case SDLK_RIGHT: case SDLK_d: right = pressed; break;
            // Buttons: Z = A, X = B (Nintendo-style)
            case SDLK_z: case SDLK_k: a = pressed; break;
            case SDLK_x: case SDLK_j: b = pressed; break;
            // Start / Select
            case SDLK_RETURN: case SDLK_SPACE: start = pressed;  break;
            case SDLK_LSHIFT: case SDLK_RSHIFT: select = pressed; break;
            default: break;
        }
    }
};

extern Input input;
