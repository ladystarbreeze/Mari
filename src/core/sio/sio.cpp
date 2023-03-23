/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "sio.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <queue>

#include "../intc.hpp"

namespace ps::sio {

/* --- SIO registers --- */

enum class SIOReg {
    JOYFIFO = 0x1F801040,
    JOYSTAT = 0x1F801044,
    JOYMODE = 0x1F801048,
    JOYCTRL = 0x1F80104A,
    JOYBAUD = 0x1F80104E,
};

struct JOYCTRL {
    bool txen; // TX enable
    //bool joyn; // /JOYn
    bool rxen; // RX enable
    u8   irqm; // RX interrupt mode
    bool tirq; // TX interrupt enable
    bool rirq; // RX interrupt enable
    bool airq; // ACK interrupt enable
    bool slot; // /JOYn select
};

struct JOYSTAT {
    bool rdy0; // TX ready 0 (?)
    bool rdy1; // TX ready 1 (?)
    bool ack;  // /ACK
    bool irq;  // IRQ7 pending
};

JOYCTRL joyctrl;
JOYSTAT joystat;

//int cmdLen = 0;

std::queue<u8> rxFIFO;

u8 read8(u32 addr) {
    u8 data;

    switch (addr) {
        case static_cast<u32>(SIOReg::JOYFIFO):
            std::printf("[SIO       ] 8-bit read @ JOY_RX_FIFO\n");

            if (rxFIFO.empty()) {
                std::printf("RX FIFO is empty!\n");

                return 0;
            }

            data = rxFIFO.front(); rxFIFO.pop();
            break;
        default:
            std::printf("[SIO       ] Unhandled 8-bit read @ 0x%08X\n", addr);

            exit(0);
    }

    return data;
}

u16 read16(u32 addr) {
    u16 data;

    switch (addr) {
        case static_cast<u32>(SIOReg::JOYSTAT):
            std::printf("[SIO       ] 16-bit read @ JOY_STAT\n");

            data  = joystat.rdy0;
            data |= !rxFIFO.empty() << 1;
            data |= joystat.rdy1    << 2;
            data |= joystat.ack     << 7;
            data |= joystat.irq     << 9;
            break;
        case static_cast<u32>(SIOReg::JOYCTRL):
            std::printf("[SIO       ] 16-bit read @ JOY_CTRL\n");

            data  = joyctrl.txen;
            data |= joyctrl.rxen << 1;
            data |= joyctrl.irqm << 8;
            data |= joyctrl.tirq << 10;
            data |= joyctrl.rirq << 11;
            data |= joyctrl.airq << 12;
            data |= joyctrl.slot << 13;
            break;
        default:
            std::printf("[SIO       ] Unhandled 16-bit read @ 0x%08X\n", addr);

            exit(0);
    }

    return data;
}

void write8(u32 addr, u8 data) {
    switch (addr) {
        case static_cast<u32>(SIOReg::JOYFIFO):
            std::printf("[SIO       ] 8-bit write @ JOY_TX_FIFO = 0x%02X\n", data);

            rxFIFO.push(0xFF); // No pad/MC connected
            break;
        default:
            std::printf("[SIO       ] Unhandled 8-bit write @ 0x%08X = 0x%02X\n", addr, data);

            exit(0);
    }
}

void write16(u32 addr, u16 data) {
    switch (addr) {
        case static_cast<u32>(SIOReg::JOYMODE):
            std::printf("[SIO       ] 16-bit write @ JOY_MODE = 0x%04X\n", data);

            assert(data == 0x000D);
            break;
        case static_cast<u32>(SIOReg::JOYCTRL):
            std::printf("[SIO       ] 16-bit write @ JOY_CTRL = 0x%04X\n", data);

            joyctrl.txen = data & (1 << 0);
            joyctrl.irqm = (data >> 8) & 3;
            joyctrl.tirq = data & (1 << 10);
            joyctrl.rirq = data & (1 << 11);
            joyctrl.airq = data & (1 << 12);

            if (data & (1 << 1)) {
                joyctrl.slot = false; // if !(/JOYn), slot = false
                joyctrl.rxen = true;
            } else {
                joyctrl.rxen = false;
            }

            if (data & (1 << 2)) {
                joyctrl.rxen = true;
            }

            if (data & (1 << 4)) {
                /* Ack JOY_STAT bits */
                std::printf("[SIO       ] JOY ACK\n");

                joystat.irq = false;
            }

            if (data & (1 << 6)) {
                std::printf("[SIO       ] JOY reset\n");

                joystat.rdy0 = true;
                joystat.rdy1 = true;
            }

            joyctrl.slot = data & (1 << 13);
            break;
        case static_cast<u32>(SIOReg::JOYBAUD):
            std::printf("[SIO       ] 16-bit write @ JOY_BAUD = 0x%04X\n", data);

            assert(data == 0x0088);
            break;
        default:
            std::printf("[SIO       ] Unhandled 16-bit write @ 0x%08X = 0x%04X\n", addr, data);

            exit(0);
    }
}

}
