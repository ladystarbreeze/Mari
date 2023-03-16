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
#include "../common/types.hpp"

namespace ps {

/* --- Mari constants --- */

constexpr i64 RUN_CYCLES = 64;

void init(const char *biosPath, const char *isoPath) {
    std::printf("BIOS path: \"%s\"\nISO path: \"%s\"\n", biosPath, isoPath);

    scheduler::init();

    bus::init(biosPath);
    cpu::init();
    dmac::init();
    gpu::init();
    timer::init();
}

void run() {
    while (true) {
        cpu::step(RUN_CYCLES >> 1); // 2 cycles per instruction

        timer::step(RUN_CYCLES);

        scheduler::processEvents(RUN_CYCLES);
    }
}

}
