/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "Mari.hpp"

#include <cstdio>
#include <cstring>

#include <ctype.h>

#include "bus/bus.hpp"
#include "cpu/cpu.hpp"
#include "../common/types.hpp"

namespace ps {

/* --- Mari constants --- */

constexpr i64 RUN_CYCLES = 32;

void init(const char *biosPath, const char *isoPath) {
    std::printf("BIOS path: \"%s\"\nISO path: \"%s\"\n", biosPath, isoPath);

    bus::init(biosPath);
    cpu::init();
}

void run() {
    while (true) {
        cpu::step(RUN_CYCLES >> 1); // 2 cycles per instruction
    }
}

}
