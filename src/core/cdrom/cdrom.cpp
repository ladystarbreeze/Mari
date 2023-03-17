/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "cdrom.hpp"

#include <cassert>
#include <cstdio>
#include <queue>

#include "../intc.hpp"
#include "../scheduler.hpp"

namespace ps::cdrom {

using Interrupt = intc::Interrupt;

/* CD-ROM commands */
enum Command {
    GetStat = 0x01,
    Init    = 0x0A,
    GetID   = 0x1A,
};

/* Seek parameters */
struct SeekParam {
    int mins, secs, sector;
};

/* --- CD-ROM registers --- */

enum class Mode {
    CDDAOn     = 1 << 0,
    AutoPause  = 1 << 1,
    Report     = 1 << 2,
    XAFilter   = 1 << 3,
    Ignore     = 1 << 4,
    FullSector = 1 << 5,
    XAADPCMOn  = 1 << 6,
    Speed      = 1 << 7,
};

enum class Status {
    Error     = 1 << 0,
    MotorOn   = 1 << 1,
    SeekError = 1 << 2,
    IDError   = 1 << 3,
    ShellOpen = 1 << 4,
    Read      = 1 << 5,
    Seek      = 1 << 6,
    Play      = 1 << 7,
};

u8 mode, stat;
u8 iEnable, iFlags; // Interrupt registers

u8 index; // CD-ROM register index

u8 cmd; // Current CD-ROM command

std::queue<u8> paramFIFO, responseFIFO;

SeekParam seekParam;

u64 idSendIRQ; // Scheduler

void sendIRQEvent(int irq) {
    std::printf("[CD-ROM    ] INT%d\n", irq);

    iFlags |= (u8)irq;

    if (iEnable & iFlags) intc::sendInterrupt(Interrupt::CDROM);
}

/* Get ID - Activate motor, set mode = 0x20, abort all commands */
void cmdGetID() {
    std::printf("[CD-ROM    ] Get ID\n");

    // Send status
    responseFIFO.push(stat);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, 30000, false);

    /* Licensed, Mode2 */
    responseFIFO.push(stat & ~(1 << 3));
    responseFIFO.push(0x00);
    responseFIFO.push(0x20);
    responseFIFO.push(0x00);

    responseFIFO.push('M');
    responseFIFO.push('A');
    responseFIFO.push('R');
    responseFIFO.push('I');

    // Send INT2
    scheduler::addEvent(idSendIRQ, 2, 50000, true);
}

/* Get Stat - Activate motor, set mode = 0x20, abort all commands */
void cmdGetStat() {
    std::printf("[CD-ROM    ] Get Stat\n");

    // Send status
    responseFIFO.push(stat);

    // Clear shell open flag
    stat &= ~static_cast<u8>(Status::ShellOpen);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, 20000, true);
}

/* Init - Activate motor, set mode = 0x20, abort all commands */
void cmdInit() {
    std::printf("[CD-ROM    ] Init\n");

    // Send old mode
    responseFIFO.push(mode);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, 80000, false);

    stat |= static_cast<u8>(Status::MotorOn);

    mode = static_cast<u8>(Mode::FullSector);

    // Send new mode
    responseFIFO.push(mode);

    // Send INT2
    scheduler::addEvent(idSendIRQ, 2, 110000, true);
}

/* Handles CD-ROM commands */
void doCmd(u8 data) {
    cmd = data;

    switch (cmd) {
        case Command::GetStat: cmdGetStat(); break;
        case Command::Init   : cmdInit(); break;
        case Command::GetID  : cmdGetID(); break;
        default:
            std::printf("[CD-ROM    ] Unhandled command 0x%02X\n", cmd);

            exit(0);
    }
}

void init() {
    /* Register scheduler events */
    idSendIRQ = scheduler::registerEvent([](int irq, i64) { sendIRQEvent(irq); });
}

u8 read(u32 addr) {
    switch (addr) {
        case 0x1F801800:
            {
                std::printf("[CD-ROM    ] 8-bit read @ STATUS\n");

                u8 data = 0;

                data |= paramFIFO.empty() << 3;        // Parameter FIFO empty
                data |= (paramFIFO.size() != 16) << 4; // Parameter FIFO not full
                data |= !responseFIFO.empty() << 5;    // Response FIFO not empty

                return data;
            }
        case 0x1F801801:
            {
                std::printf("[CD-ROM    ] 8-bit read @ RESPONSE\n");

                const auto data = responseFIFO.front(); responseFIFO.pop();

                return data;
            }
        case 0x1F801803:
            switch (index) {
                case 1:
                    std::printf("[CD-ROM    ] 8-bit read @ IF\n");
                    return iFlags;
                default:
                    std::printf("[CD-ROM    ] Unhandled 8-bit read @ 0x%08X.%u\n", addr, index);

                    exit(0);
            }
            break;
        default:
            std::printf("[CD-ROM    ] Unhandled 8-bit read @ 0x%08X\n", addr);

            exit(0);
    }
}

void write(u32 addr, u8 data) {
    switch (addr) {
        case 0x1F801800:
            std::printf("[CD-ROM    ] 8-bit write @ INDEX = 0x%02X\n", data);

            index = data & 3;
            break;
        case 0x1F801801:
            switch (index) {
                case 0:
                    std::printf("[CD-ROM    ] 8-bit write @ CMD = 0x%02X\n", data);

                    doCmd(data);
                    break;
                default:
                    std::printf("[CD-ROM    ] Unhandled 8-bit write @ 0x%08X.%u = 0x%02X\n", addr, index, data);

                    exit(0);
            }
            break;
        case 0x1F801802:
            switch (index) {
                case 0:
                    std::printf("[CD-ROM    ] 8-bit write @ PARAM = 0x%02X\n", data);

                    assert(paramFIFO.size() < 16);

                    paramFIFO.push(data);
                    break;
                case 1:
                    std::printf("[CD-ROM    ] 8-bit write @ IE = 0x%02X\n", data);

                    iEnable = data & 0x1F;
                    break;
                default:
                    std::printf("[CD-ROM    ] Unhandled 8-bit write @ 0x%08X.%u = 0x%02X\n", addr, index, data);

                    exit(0);
            }
            break;
        case 0x1F801803:
            switch (index) {
                case 1:
                    std::printf("[CD-ROM    ] 8-bit write @ IF = 0x%02X\n", data);

                    iFlags &= (~data & 0x1F);
                    break;
                default:
                    std::printf("[CD-ROM    ] Unhandled 8-bit write @ 0x%08X.%u = 0x%02X\n", addr, index, data);

                    exit(0);
            }
            break;
        default:
            std::printf("[CD-ROM    ] Unhandled 8-bit write @ 0x%08X = 0x%02X\n", addr, data);

            exit(0);
    }
}

}
