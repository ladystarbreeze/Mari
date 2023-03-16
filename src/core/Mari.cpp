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
#include "cpu/cpu.hpp"
#include "dmac/dmac.hpp"
#include "gpu/gpu.hpp"
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
SDL_Event *e;

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
    cpu::init();
    dmac::init();
    gpu::init();
    timer::init();

    initSDL();
}

void run() {
    while (true) {
        cpu::step(RUN_CYCLES >> 1); // 2 cycles per instruction

        timer::step(RUN_CYCLES);

        scheduler::processEvents(RUN_CYCLES);
    }
}

void update(const u8 *fb) {
    SDL_PollEvent(e); // Change this when we implement controllers

    SDL_UpdateTexture(texture, nullptr, fb, 2 * 1024);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

}
