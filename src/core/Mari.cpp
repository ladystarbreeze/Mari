/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "Mari.hpp"

#include <cstdio>
#include <cstring>

#include <ctype.h>

#include "scheduler.hpp"
#include "bus/bus.hpp"
#include "cdrom/cdrom.hpp"
#include "cpu/cpu.hpp"
#include "dmac/dmac.hpp"
#include "gpu/gpu.hpp"
#include "sio/sio.hpp"
#include "timer/timer.hpp"

#include <SDL2/SDL.h>

#undef main

namespace ps {

/* --- Mari constants --- */

constexpr i64 RUN_CYCLES = 64;

/* SDL2 */
SDL_Renderer *renderer;
SDL_Window *window;
SDL_Texture *texture;

SDL_Event e;

bool isRunning = true;

/* Initializes SDL */
void initSDL() {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

    SDL_CreateWindowAndRenderer(1024, 512, 0, &window, &renderer);
    SDL_SetWindowSize(window, 1024, 512);
    SDL_RenderSetLogicalSize(renderer, 1024, 512);
    SDL_SetWindowResizable(window, SDL_FALSE);
    SDL_SetWindowTitle(window, "Mari");

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XBGR1555, SDL_TEXTUREACCESS_STREAMING, 1024, 512);
}

void init(const char *biosPath, const char *isoPath) {
    std::printf("BIOS path: \"%s\"\nISO path: \"%s\"\n", biosPath, isoPath);

    scheduler::init();

    bus::init(biosPath);
    cdrom::init(isoPath);
    cpu::init();
    dmac::init();
    gpu::init();
    sio::init();
    timer::init();

    scheduler::flush();

    initSDL();
}

void run() {
    while (isRunning) {
        cpu::step(RUN_CYCLES >> 1); // 2 cycles per instruction

        timer::step(RUN_CYCLES);

        scheduler::processEvents(RUN_CYCLES);
    }

    SDL_Quit();
}

void update(const u8 *fb) {
    const u8 *keyState = SDL_GetKeyboardState(NULL);

    u16 input = 0;

    SDL_PollEvent(&e);

    switch (e.type) {
        case SDL_QUIT   : isRunning = false; break;
        case SDL_KEYDOWN:
            if (keyState[SDL_GetScancodeFromKey(SDLK_c)]) input |= 1 <<  0; // SELECT
            if (keyState[SDL_GetScancodeFromKey(SDLK_v)]) input |= 1 <<  3; // START
            if (keyState[SDL_GetScancodeFromKey(SDLK_w)]) input |= 1 <<  4; // UP
            if (keyState[SDL_GetScancodeFromKey(SDLK_d)]) input |= 1 <<  5; // RIGHT
            if (keyState[SDL_GetScancodeFromKey(SDLK_s)]) input |= 1 <<  6; // DOWN
            if (keyState[SDL_GetScancodeFromKey(SDLK_a)]) input |= 1 <<  7; // LEFT
            if (keyState[SDL_GetScancodeFromKey(SDLK_1)]) input |= 1 <<  8; // L2
            if (keyState[SDL_GetScancodeFromKey(SDLK_3)]) input |= 1 <<  9; // R2
            if (keyState[SDL_GetScancodeFromKey(SDLK_q)]) input |= 1 << 10; // L2
            if (keyState[SDL_GetScancodeFromKey(SDLK_e)]) input |= 1 << 11; // R2
            if (keyState[SDL_GetScancodeFromKey(SDLK_t)]) input |= 1 << 12; // TRIANGLE
            if (keyState[SDL_GetScancodeFromKey(SDLK_h)]) input |= 1 << 13; // CIRCLE
            if (keyState[SDL_GetScancodeFromKey(SDLK_g)]) input |= 1 << 14; // CROSS
            if (keyState[SDL_GetScancodeFromKey(SDLK_f)]) input |= 1 << 15; // SQUARE
            break;
        default: break;
    }

    sio::setInput(~input);

    SDL_UpdateTexture(texture, nullptr, fb, 2 * 1024);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

}
