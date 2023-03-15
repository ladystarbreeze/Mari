/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "timer.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../intc.hpp"

namespace ps::timer {

using Interrupt = intc::Interrupt;

/* --- Timer registers --- */

enum TimerReg {
    COUNT = 0x1F801100,
    MODE  = 0x1F801104,
    COMP  = 0x1F801108,
};

/* Timer mode register */
struct Mode {
    bool gate; // GATE enable
    u8   gats; // GATe Select
    bool zret; // Zero RETurn
    bool cmpe; // CoMPare Enable
    bool ovfe; // OVerFlow Enable
    bool rept; // REPeaT interrupt
    bool levl; // LEVL
    u8   clks; // CLocK Select
    bool intf; // INTerrupt Flag
    bool equf; // EQUal Flag
    bool ovff; // OVerFlow Flag
};

/* Timer */
struct Timer {
    Mode mode; // T_MODE

    u32 count; // T_COUNT
    u16 comp;  // T_COMP

    // Prescaler
    u16 subcount;
    u16 prescaler;
};

Timer timers[3];

/* Returns timer ID from address */
int getTimer(u32 addr) { 
    switch ((addr >> 4) & 0xFF) {
        case 0x10: return 0;
        case 0x11: return 1;
        case 0x12: return 2;
        default:
            std::printf("[Timer     ] Invalid timer\n");

            exit(0);
    }
}

void sendInterrupt(int tmID) {
    auto &mode = timers[tmID].mode;

    if (mode.intf) intc::sendInterrupt(static_cast<Interrupt>(tmID + 4));

    if (mode.rept && mode.levl) {
        mode.intf = !mode.intf; // Toggle interrupt flag
    } else {
        mode.intf = false;
    }
}

void init() {
    memset(&timers, 0, 3 * sizeof(Timer));

    for (auto &i : timers) i.prescaler = 1;

    std::printf("[Timer     ] Init OK\n");
}

u16 read(u32 addr) {
    u16 data;

    // Get channel ID
    const auto chn = getTimer(addr);

    auto &timer = timers[chn];

    switch ((addr & ~0xFF0) | (1 << 8)) {
        case TimerReg::COUNT:
            std::printf("[Timer     ] 16-bit read @ T%d_COUNT\n", chn);
            return timer.count;
        case TimerReg::MODE:
            {
                std::printf("[Timer     ] 16-bit read @ T%d_MODE\n", chn);
                
                auto &mode = timer.mode;

                data  = mode.gate;
                data |= mode.gats << 1;
                data |= mode.zret << 3;
                data |= mode.cmpe << 4;
                data |= mode.ovfe << 5;
                data |= mode.rept << 6;
                data |= mode.levl << 7;
                data |= mode.clks << 8;
                data |= mode.intf << 10;
                data |= mode.equf << 11;
                data |= mode.ovff << 12;

                /* Clear interrupt flags */

                mode.equf = false;
                mode.ovff = false;
            }
            break;
        default:
            std::printf("[Timer     ] Unhandled 16-bit read @ 0x%08X\n", addr);

            exit(0);
    }

    return data;
}

void write(u32 addr, u16 data) {
    // Get channel ID
    const auto chn = getTimer(addr);

    auto &timer = timers[chn];

    switch ((addr & ~0xFF0) | (1 << 8)) {
        case TimerReg::COUNT:
            std::printf("[Timer     ] 16-bit write @ T%d_COUNT = 0x%04X\n", chn, data);

            timer.count = data;
            break;
        case TimerReg::MODE:
            {
                auto &mode = timer.mode;

                std::printf("[Timer     ] 16-bit write @ T%d_MODE = 0x%04X\n", chn, data);

                mode.gate = data & 1;
                mode.gats = (data >> 1) & 3;
                mode.zret = data & (1 << 3);
                mode.cmpe = data & (1 << 4);
                mode.ovfe = data & (1 << 5);
                mode.rept = data & (1 << 6);
                mode.levl = data & (1 << 7);
                mode.clks = (data >> 8) & 3;

                mode.intf = true; // Always reset to 1

                if (mode.gate) {
                    std::printf("[Timer     ] Unhandled timer gate\n");

                    exit(0);
                }

                if (mode.clks) {
                    if (chn == 2) {
                        timer.prescaler = 1 + 7 * (u16)(mode.clks > 1);
                    } else {
                        std::printf("[Timer     ] Unhandled clock source\n");

                        exit(0);
                    }
                }

                timer.subcount = 0;
                timer.count = 0;    // Always cleared
            }
            break;
        case TimerReg::COMP:
            std::printf("[Timer     ] 16-bit write @ T%d_COMP = 0x%04X\n", chn, data);

            timer.comp = data;

            if (!timer.mode.levl) timer.mode.intf = true; // Set INTF if in toggle mode
            break;
        default:
            std::printf("[Timer     ] Unhandled 16-bit write @ 0x%08X = 0x%04X\n", addr, data);

            exit(0);
    }
}

/* Steps timers */
void step(i64 c) {
    for (int i = 0; i < 3; i++) {
        auto &timer = timers[i];

        /* Timers 0 and 1 have a different clock source if CLKS is odd */
        if ((timer.mode.clks & 1) && ((i == 0) || (i == 1))) continue;

        timer.subcount += c;

        while (timer.subcount > timer.prescaler) {
            timer.count++;

            if (timer.count & (1 << 16)) {
                if (timer.mode.ovfe && !timer.mode.ovff) {
                    // Checking OVFF is necessary because timer IRQs are edge-triggered
                    timer.mode.ovff = true;

                    sendInterrupt(i);
                }
            }

            if (timer.count == timer.comp) {
                if (timer.mode.cmpe && !timer.mode.equf) {
                    // Checking EQUF is necessary because timer IRQs are edge-triggered
                    timer.mode.equf = true;

                    sendInterrupt(i);
                }

                if (timer.mode.zret) timer.count = 0;
            }

            timer.count &= 0xFFFF;

            timer.subcount -= timer.prescaler;
        }
    }
}

}
