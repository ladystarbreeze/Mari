/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "mdec.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../dmac/dmac.hpp"

namespace ps::mdec {

using Channel = dmac::Channel;

/* --- MDEC constants --- */

constexpr int LUM_TABLE = 0;
constexpr int COL_TABLE = 64;

enum Command {
    NOP,
    DecodeMacroblock,
    SetQuantTables,
    SetScaleTable,
};

enum MDECState {
    Idle,
    ReceiveMacroblock,
    ReceiveQuantTables,
    ReceiveScaleTable,
};

/* --- MDEC registers --- */

struct MDECStatus {
    u16  rem;   // Words remaining
    u8   blk;   // Current block
    bool b15;   // Bit 15 set/clear (15-bit depth only)
    bool sign;  // Signed
    u8   dep;   // Output depth
    bool oreq;  // Output request
    bool ireq;  // Input request
    bool busy;
    bool empty; // Out FIFO empty
    bool full;  // In FIFO full/last word received
};

MDECStatus stat;

/* Quant tables (0-63 = lum, 64-127 = col) */
u8  quantTable[128];
int quantIdx = 0;

/* Scale table */
i16 scaleTable[64];
int scaleIdx = 0;

int cmdLen;

MDECState state = MDECState::Idle;

u32 readStat() {
    //std::printf("[MDEC      ] 32-bit read @ MDEC0\n");

    u32 data;

    data  = stat.rem;
    data |= stat.blk   << 16;
    data |= stat.b15   << 23;
    data |= stat.sign  << 24;
    data |= stat.dep   << 25;
    data |= stat.oreq  << 27;
    data |= stat.ireq  << 28;
    data |= stat.busy  << 29;
    data |= stat.full  << 30;
    data |= stat.empty << 31;

    return data;
}

void writeCmd(u32 data) {
    std::printf("[MDEC      ] 32-bit write @ MDEC0 = 0x%08X\n", data);

    switch (state) {
        case MDECState::Idle:
            {
                const auto cmd = data >> 29;

                /* Always copied */
                stat.b15  = data & (1 << 25);
                stat.sign = data & (1 << 26);
                stat.dep  = (data >> 27) & 3;

                switch (cmd) {
                    case Command::NOP:
                        std::printf("[MDEC      ] NOP\n");

                        stat.rem = data;
                        return;
                    case Command::DecodeMacroblock: // TODO: handle this
                        std::printf("[MDEC      ] Decode Macroblock\n");

                        cmdLen = data & 0xFFFF;

                        state = MDECState::ReceiveMacroblock;
                        break;
                    case Command::SetQuantTables:
                        std::printf("[MDEC      ] Set Quant Tables\n");

                        quantIdx = 0;

                        cmdLen = (data & 1) ? 32 : 16; // cmd[0] == 1 sets lum and col

                        state = MDECState::ReceiveQuantTables;
                        break;
                    case Command::SetScaleTable:
                        std::printf("[MDEC      ] Set Scale Table\n");

                        scaleIdx = 0;

                        cmdLen = 32;

                        state = MDECState::ReceiveScaleTable;
                        break;
                    default:
                        std::printf("[MDEC      ] Unhandled command %u\n", cmd);

                        exit(0);
                }

                stat.busy = true;
            }
            break;
        case MDECState::ReceiveMacroblock:
            if (!--cmdLen) {
                stat.rem  = 0xFFFF;
                stat.busy = false;

                /* Clear MDEC_IN request, set MDEC_OUT request */

                stat.full = true;
                stat.ireq = true;

                stat.empty = false;
                stat.oreq  = true;

                dmac::setDRQ(Channel::MDECOUT, true);

                state = MDECState::Idle;
            }
            break;
        case MDECState::ReceiveQuantTables:
            assert(quantIdx < 128);

            std::memcpy(&quantTable[quantIdx], &data, 4);

            quantIdx += 4;

            if (!--cmdLen) {
                stat.rem  = 0;
                stat.busy = false;



                state = MDECState::Idle;
            }
            break;
        case MDECState::ReceiveScaleTable:
            assert(scaleIdx < 64);

            std::memcpy(&scaleTable[scaleIdx], &data, 4);

            scaleIdx += 2;

            if (!--cmdLen) {
                stat.busy = false;

                state = MDECState::Idle;
            }
            break;
    }
}

void writeCtrl(u32 data) {
    std::printf("[MDEC      ] 32-bit write @ MDEC1 = 0x%08X\n", data);

    if (data & (1 << 31)) {
        std::printf("[MDEC      ] MDEC reset\n");

        stat.rem  = 0;
        stat.blk  = 0;
        stat.b15  = false;
        stat.sign = false;
        stat.dep  = 0;
        stat.oreq = false;
        stat.ireq = true;
        stat.busy = false;

        state = MDECState::Idle;
    }
}

}
