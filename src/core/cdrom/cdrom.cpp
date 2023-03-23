/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "cdrom.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <queue>

#include "../intc.hpp"
#include "../scheduler.hpp"

namespace ps::cdrom {

using Interrupt = intc::Interrupt;

/* --- CDROM constants --- */

constexpr int SECTOR_SIZE = 2352;
constexpr int READ_SIZE   = 0x818;

constexpr i64 CPU_SPEED = 44100 * 0x300;
constexpr i64 READ_TIME_SINGLE = CPU_SPEED / 75;
constexpr i64 READ_TIME_DOUBLE = CPU_SPEED / (2 * 75);

constexpr i64 INT3_TIME = 10000;

/* CDROM commands */
enum Command {
    GetStat = 0x01,
    SetLoc  = 0x02,
    ReadN   = 0x06,
    Pause   = 0x09,
    Init    = 0x0A,
    Unmute  = 0x0C,
    SetMode = 0x0E,
    GetTN   = 0x13,
    GetTD   = 0x14,
    SeekL   = 0x15,
    Test    = 0x19,
    GetID   = 0x1A,
    ReadTOC  = 0x1E,
};

/* Sub commands */
enum SubCommand {
    GetBIOSDate = 0x20,
};

/* Seek parameters */
struct SeekParam {
    int mins, secs, sector;
};

/* --- CDROM  registers --- */

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

std::ifstream file;

u8 mode, stat;
u8 iEnable, iFlags; // Interrupt registers

u8 index; // CDROM  register index

u8 cmd; // Current CDROM  command

std::queue<u8> paramFIFO, responseFIFO;

SeekParam seekParam;

u8 readBuf[SECTOR_SIZE];
int readIdx;

u64 idSendIRQ; // Scheduler

void readSector();

/* BCD to char conversion */
inline u8 toChar(u8 bcd)
{
	return (bcd / 16) * 10 + (bcd % 16);
}

void sendIRQEvent(int irq) {
    std::printf("[CDROM     ] INT%d\n", irq);

    iFlags |= (u8)irq;

    if (iEnable & iFlags) intc::sendInterrupt(Interrupt::CDROM);

    /* If this is an INT1, read sector and send new INT1 */
    if (irq == 1) {
        // Send status
        responseFIFO.push(stat);

        readSector();

        if (mode & static_cast<u8>(Mode::Speed)) {
            scheduler::addEvent(idSendIRQ, 1, READ_TIME_DOUBLE, false);
        } else {
            scheduler::addEvent(idSendIRQ, 1, READ_TIME_SINGLE, false);
        }
    }
}

void readSector() {
    auto &s = seekParam;

    /* Calculate seek target (in sectors) */
    const auto mm   = toChar(s.mins) * 60 * 75; // 1min = 60sec
    const auto ss   = toChar(s.secs) * 75; // 1min = 75 sectors
    const auto sect = toChar(s.sector);

    const auto seekTarget = mm + ss + sect - 150; // Starts at 2s, subtract 150 sectors to get start

    std::printf("[CDROM     ] Seeking to [%02X:%02X:%02X] = %d\n", s.mins, s.secs, s.sector, seekTarget);

    file.seekg(seekTarget * SECTOR_SIZE, std::ios_base::beg);

    file.read((char *)readBuf, SECTOR_SIZE);

    readIdx = (mode & static_cast<u8>(Mode::FullSector)) ? 0x0C : 0x18;

    s.sector++;

    /* Increment BCD values */
    if ((s.sector & 0xF) == 10) { s.sector += 0x10; s.sector &= 0xF0; }

    if (s.sector == 0x75) { s.secs++; s.sector = 0; }

    if ((s.secs & 0xF) == 10) { s.secs += 0x10; s.secs &= 0xF0; }

    if (s.secs == 0x60) { s.mins++; s.secs = 0; }

    if ((s.mins & 0xF) == 10) { s.mins += 0x10; s.mins &= 0xF0; }

    std::printf("[CDROM     ] Next seek to [%02X:%02X:%02X]\n", s.mins, s.secs, s.sector);
}

u8 readResponse() {
    assert(!responseFIFO.empty());

    const auto data = responseFIFO.front(); responseFIFO.pop();

    return data;
}

/* Get BIOS Date */
void cmdGetBIOSDate() {
    std::printf("[CDROM     ] Get BIOS Date\n");

    // Send date
    responseFIFO.push(0x96);
    responseFIFO.push(0x09);
    responseFIFO.push(0x12);
    responseFIFO.push(0xC2);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, INT3_TIME, true);
}

/* Get ID - Activate motor, set mode = 0x20, abort all commands */
void cmdGetID() {
    std::printf("[CDROM     ] Get ID\n");

    // Send status
    responseFIFO.push(stat);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, INT3_TIME, false);

    /* Licensed, Mode2 */
    responseFIFO.push(0x02);
    responseFIFO.push(0x00);
    responseFIFO.push(0x20);
    responseFIFO.push(0x00);

    responseFIFO.push('M');
    responseFIFO.push('A');
    responseFIFO.push('R');
    responseFIFO.push('I');

    // Send INT2
    scheduler::addEvent(idSendIRQ, 2, INT3_TIME + 30000, true);
}

/* Get Stat - Activate motor, set mode = 0x20, abort all commands */
void cmdGetStat() {
    std::printf("[CDROM     ] Get Stat\n");

    // Send status
    responseFIFO.push(stat);

    // Clear shell open flag
    stat &= ~static_cast<u8>(Status::ShellOpen);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, INT3_TIME, true);
}

/* Get TD - Returns track start */
void cmdGetTD() {
    std::printf("[CDROM     ] Get TD\n");

    paramFIFO.pop();

    // Send status
    responseFIFO.push(stat);
    responseFIFO.push(0);
    responseFIFO.push(0);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, INT3_TIME, true);
}

/* Get TN - Returns first and last track number */
void cmdGetTN() {
    std::printf("[CDROM     ] Get TN\n");

    // Send status
    responseFIFO.push(stat);
    responseFIFO.push(0x01);
    responseFIFO.push(0x01);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, INT3_TIME, true);
}

/* Read TOC - Reread TOC */
void cmdReadTOC() {
    std::printf("[CDROM     ] Read TOC\n");

    // Send status
    responseFIFO.push(stat);

    stat |= static_cast<u8>(Status::MotorOn);
    stat |= static_cast<u8>(Status::Read);

    // Send status
    responseFIFO.push(stat);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, INT3_TIME, false);

    // Send INT2
    scheduler::addEvent(idSendIRQ, 3, INT3_TIME + 20000, true);
}

/* Init - Activate motor, set mode = 0x20, abort all commands */
void cmdInit() {
    std::printf("[CDROM     ] Init\n");

    // Send old mode
    responseFIFO.push(mode);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, 80000, false);

    stat |= static_cast<u8>(Status::MotorOn);

    mode = static_cast<u8>(Mode::FullSector);

    // Send new mode
    responseFIFO.push(mode);

    // Send INT2
    scheduler::addEvent(idSendIRQ, 2, 80000 + 20000, true);
}

/* Pause */
void cmdPause() {
    std::printf("[CDROM     ] Pause\n");

    scheduler::removeEvent(idSendIRQ); // Kill all pending CDROM events

    /* Clear response buffer */
    while (!responseFIFO.empty()) responseFIFO.pop();

    // Send status
    responseFIFO.push(stat);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, INT3_TIME, false);

    /* Pause is faster if drive is already paused */
    if (!(stat & (static_cast<u8>(Status::Play) | static_cast<u8>(Status::Read)))) {
        scheduler::addEvent(idSendIRQ, 2, INT3_TIME + 10000, true);
    } else {
        scheduler::addEvent(idSendIRQ, 2, INT3_TIME + 5 * READ_TIME_SINGLE, true);
    }

    /* Pause drive */
    stat &= ~static_cast<u8>(Status::Play);
    stat &= ~static_cast<u8>(Status::Read);

    // Send status
    responseFIFO.push(stat);
}

/* ReadN - Read sector */
void cmdReadN() {
    std::printf("[CDROM     ] ReadN\n");

    // Send status
    responseFIFO.push(stat);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, INT3_TIME, false);

    stat &= ~static_cast<u8>(Status::Seek);
    stat |=  static_cast<u8>(Status::Read);

    // Send INT2
    if (stat & static_cast<u8>(Mode::Speed)) {
        scheduler::addEvent(idSendIRQ, 1, INT3_TIME + READ_TIME_DOUBLE, true);
    } else {
        scheduler::addEvent(idSendIRQ, 1, INT3_TIME + READ_TIME_SINGLE, true);
    }
}

/* SeekL - Data mode seek */
void cmdSeekL() {
    std::printf("[CDROM     ] SeekL\n");

    // Send status
    responseFIFO.push(stat);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, INT3_TIME, false);

    stat |= static_cast<u8>(Status::Seek);

    // Send status
    responseFIFO.push(stat);

    // Send INT2
    scheduler::addEvent(idSendIRQ, 2, INT3_TIME + 80000, true);
}

/* Set Loc - Sets seek parameters */
void cmdSetLoc() {
    std::printf("[CDROM     ] Set Loc\n");

    // Send status
    responseFIFO.push(stat);

    /* Set minutes, seconds, sector */
    seekParam.mins   = paramFIFO.front(); paramFIFO.pop();
    seekParam.secs   = paramFIFO.front(); paramFIFO.pop();
    seekParam.sector = paramFIFO.front(); paramFIFO.pop();

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, INT3_TIME, true);
}

/* Set Mode - Sets CDROM  mode */
void cmdSetMode() {
    std::printf("[CDROM     ] Set Mode\n");

    // Send status
    responseFIFO.push(stat);

    mode = paramFIFO.front(); paramFIFO.pop();

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, INT3_TIME, true);
}

/* Unmute */
void cmdUnmute() {
    std::printf("[CDROM     ] Unmute\n");

    // Send status
    responseFIFO.push(stat);

    // Send INT3
    scheduler::addEvent(idSendIRQ, 3, INT3_TIME, true);
}

/* Handles sub commands */
void doSubCmd() {
    const auto cmd = paramFIFO.front(); paramFIFO.pop();

    switch (cmd) {
        case SubCommand::GetBIOSDate: cmdGetBIOSDate(); break;
        default:
            std::printf("[CDROM     ] Unhandled sub command 0x%02X\n", cmd);

            exit(0);
    }
}

/* Handles CDROM  commands */
void doCmd(u8 data) {
    cmd = data;

    switch (cmd) {
        case Command::GetStat: cmdGetStat(); break;
        case Command::SetLoc : cmdSetLoc(); break;
        case Command::ReadN  : cmdReadN(); break;
        case Command::Pause  : cmdPause(); break;
        case Command::Init   : cmdInit(); break;
        case Command::Unmute : cmdUnmute(); break;
        case Command::SetMode: cmdSetMode(); break;
        case Command::GetTN  : cmdGetTN(); break;
        case Command::GetTD  : cmdGetTD(); break;
        case Command::SeekL  : cmdSeekL(); break;
        case Command::Test   : doSubCmd(); break;
        case Command::GetID  : cmdGetID(); break;
        case Command::ReadTOC: cmdReadTOC(); break;
        default:
            std::printf("[CDROM     ] Unhandled command 0x%02X\n", cmd);

            exit(0);
    }
}

void init(const char *isoPath) {
    // Open file
    file.open(isoPath, std::ios::in | std::ios::binary);

    if (!file.is_open()) {
        std::printf("[CDROM     ] Unable to open file \"%s\"\n", isoPath);

        exit(0);
    }

    file.unsetf(std::ios::skipws);

    /* Register scheduler events */
    idSendIRQ = scheduler::registerEvent([](int irq, i64) { sendIRQEvent(irq); });
}

u8 read(u32 addr) {
    switch (addr) {
        case 0x1F801800:
            {
                //std::printf("[CDROM     ] 8-bit read @ STATUS\n");

                u8 data = index;

                data |= paramFIFO.empty() << 3;        // Parameter FIFO empty
                data |= (paramFIFO.size() != 16) << 4; // Parameter FIFO not full
                data |= !responseFIFO.empty() << 5;    // Response FIFO not empty
                data |= (readIdx < READ_SIZE) << 6;    // Data FIFO not empty

                return data;
            }
        case 0x1F801801:
            {
                std::printf("[CDROM     ] 8-bit read @ RESPONSE\n");

                return readResponse();
            }
        case 0x1F801803:
            switch (index) {
                case 0:
                    std::printf("[CDROM     ] 8-bit read @ IE\n");
                    return iEnable;
                case 1:
                    //std::printf("[CDROM     ] 8-bit read @ IF\n");
                    return iFlags;
                default:
                    std::printf("[CDROM     ] Unhandled 8-bit read @ 0x%08X.%u\n", addr, index);

                    exit(0);
            }
            break;
        default:
            std::printf("[CDROM     ] Unhandled 8-bit read @ 0x%08X\n", addr);

            exit(0);
    }
}

void write(u32 addr, u8 data) {
    switch (addr) {
        case 0x1F801800:
            //std::printf("[CDROM     ] 8-bit write @ INDEX = 0x%02X\n", data);

            index = data & 3;
            break;
        case 0x1F801801:
            switch (index) {
                case 0:
                    std::printf("[CDROM     ] 8-bit write @ CMD = 0x%02X\n", data);

                    doCmd(data);
                    break;
                case 3:
                    std::printf("[CDROM     ] 8-bit write @ VOLR->L = 0x%02X\n", data);
                    break;
                default:
                    std::printf("[CDROM     ] Unhandled 8-bit write @ 0x%08X.%u = 0x%02X\n", addr, index, data);

                    exit(0);
            }
            break;
        case 0x1F801802:
            switch (index) {
                case 0:
                    std::printf("[CDROM     ] 8-bit write @ PARAM = 0x%02X\n", data);

                    assert(paramFIFO.size() < 16);

                    paramFIFO.push(data);
                    break;
                case 1:
                    std::printf("[CDROM     ] 8-bit write @ IE = 0x%02X\n", data);

                    iEnable = data & 0x1F;
                    break;
                case 2:
                    std::printf("[CDROM     ] 8-bit write @ VOLL->L = 0x%02X\n", data);
                    break;
                case 3:
                    std::printf("[CDROM     ] 8-bit write @ VOLR->R = 0x%02X\n", data);
                    break;
                default:
                    std::printf("[CDROM     ] Unhandled 8-bit write @ 0x%08X.%u = 0x%02X\n", addr, index, data);

                    exit(0);
            }
            break;
        case 0x1F801803:
            switch (index) {
                case 0:
                    std::printf("[CDROM     ] 8-bit write @ REQUEST = 0x%02X\n", data);

                    assert(!(data & (1 << 5)));
                    break;
                case 1:
                    std::printf("[CDROM     ] 8-bit write @ IF = 0x%02X\n", data);

                    iFlags &= (~data & 0x1F);
                    break;
                case 2:
                    std::printf("[CDROM     ] 8-bit write @ VOLL->R = 0x%02X\n", data);
                    break;
                case 3:
                    std::printf("[CDROM     ] 8-bit write @ APPLYVOL = 0x%02X\n", data);
                    break;
                default:
                    std::printf("[CDROM     ] Unhandled 8-bit write @ 0x%08X.%u = 0x%02X\n", addr, index, data);

                    exit(0);
            }
            break;
        default:
            std::printf("[CDROM     ] Unhandled 8-bit write @ 0x%08X = 0x%02X\n", addr, data);

            exit(0);
    }
}

u32 getData32() {
    assert(readIdx < READ_SIZE);

    u32 data;

    std::memcpy(&data, &readBuf[readIdx], 4);

    readIdx += 4;

    return data;
}

}
