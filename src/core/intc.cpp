/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "intc.hpp"

#include <cassert>
#include <cstdio>

#include "cpu/cop0.hpp"

namespace ps::intc {

/* Interrupt sources */
const char *intNames[] = {
    "VBLANK",
    "GPU",
    "CDROM",
    "DMA",
    "Timer 0", "Timer 1", "Timer 2",
    "SIO Receive",
    "SIO",
    "SPU",
    "PIO",
};

/* --- INTC registers --- */

u16 iMASK = 0, iSTAT = 0;

void checkInterrupt();

/* Returns I_MASK */
u16 readMask() {
    return iMASK;
}

/* Returns I_STAT */
u16 readStat() {
    return iSTAT;
}

/* Writes I_MASK */
void writeMask(u16 data) {
    iMASK = (data & 0x7FF);

    assert(!(iMASK & 0x7F2));

    checkInterrupt();
}

/* Writes I_STAT */
void writeStat(u16 data) {
    iSTAT &= (data & 0x7FF);

    checkInterrupt();
}

void sendInterrupt(Interrupt i) {
    std::printf("[INTC      ] %s interrupt request\n", intNames[static_cast<int>(i)]);

    iSTAT |= 1 << static_cast<int>(i);

    checkInterrupt();
}

void checkInterrupt() {
    //std::printf("[INTC      ] I_STAT = 0x%04X, I_MASK = 0x%04X\n", iSTAT, iMASK);

    cpu::cop0::setInterruptPending(iSTAT & iMASK);
}

}
