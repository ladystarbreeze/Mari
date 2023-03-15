/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#pragma once

#include "../common/types.hpp"

namespace ps::intc {

/* Interrupt sources */
enum class Interrupt {
    VBLANK,
    GPU,
    CDROM,
    DMA,
    Timer0, Timer1, Timer2,
    SIORecieve,
    SIO,
    SPU,
    PIO,
};

u16 readMask();
u16 readStat();

void writeMask(u16 data);
void writeStat(u16 data);

void sendInterrupt(Interrupt i);

}
