/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "spu.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>

#include "gauss.hpp"

#include "../intc.hpp"
#include "../scheduler.hpp"

namespace ps::spu {

/* --- SPU constants --- */

constexpr i64 SPU_RATE = 0x300;

constexpr u32 SPU_BASE = 0x1F801C00;
constexpr u32 RAM_SIZE = 0x80000;

/* --- SPU registers --- */

enum class SPUReg {
    VOLL    = 0x1F801C00,
    VOLR    = 0x1F801C02,
    PITCH   = 0x1F801C04,
    ADDR    = 0x1F801C06,
    ADSR    = 0x1F801C08,
    ADSRVOL = 0x1F801C0C,
    MVOLL   = 0x1F801D80,
    MVOLR   = 0x1F801D82,
    VLOUT   = 0x1F801D84,
    VROUT   = 0x1F801D86,
    KON     = 0x1F801D88,
    KOFF    = 0x1F801D8C,
    PMON    = 0x1F801D90,
    NON     = 0x1F801D94,
    REVON   = 0x1F801D98,
    REVADDR = 0x1F801DA2,
    SPUADDR = 0x1F801DA6,
    SPUDATA = 0x1F801DA8,
    SPUCNT  = 0x1F801DAA,
    FIFOCNT = 0x1F801DAC,
    SPUSTAT = 0x1F801DAE,
    CDVOLL  = 0x1F801DB0,
    CDVOLR  = 0x1F801DB2,
    EVOLL   = 0x1F801DB4,
    EVOLR   = 0x1F801DB6,
};

/* SPU control */
struct SPUCNT {
    bool cden;   // CD audio enable
    bool exten;  // External audio enable
    bool cdrev;  // CD reverb enable
    bool extrev; // External reverb enable
    u8   trxmod; // Transfer mode
    bool irqen;  // IRQ enable
    bool reven;  // Reverb master enable
    u8   nstep;  // Noise step
    u8   nshift; // Noise shift
    bool unmute; // (un)mute SPU
    bool spuen;  // SPU enable
};

/* SPU status */
struct SPUSTAT {
    u8   spumod; // SPU mode
    bool irq9;   // IRQ flag
    bool dmard;  // DMA read (0 = write)
    bool wrreq;  // DMA write request
    bool rdreq;  // DMA read request
    bool busy;   // SPU busy
    bool cbuf;   // Current capture buffer
};

struct Voice {
    bool on; // Set by KON

    u16 pitch;
    u32 pitchCounter;

    u32 addr, loopaddr, caddr;

    u8   adpcmBlock[16];
    bool hasBlock;

    i16 s[4]; // Most recent samples
};

std::vector<u8> ram;

i16 sound[2048];
int soundIdx = 0;

SPUCNT  spucnt;
SPUSTAT spustat;

u32 kon;

u32 spuaddr, caddr; // SPU address, current address

Voice voices[24];

u64 idStep;

/* Returns true if address is in range [base;size] */
bool inRange(u64 addr, u64 base, u64 size) {
    return (addr >= base) && (addr < (base + size));
}

/* Steps the SPU, calculates current sample */
void step() {
    if (spucnt.spuen && spucnt.unmute) {
        for (int i = 0; i < 1; i++) {
            auto &v = voices[i];

            if (!v.on) { sound[soundIdx++] = 0; continue; }

            if (!v.hasBlock) {
                /* Load new ADPCM block */

                std::memcpy(v.adpcmBlock, &ram[v.caddr], 16);

                v.caddr += 16;

                const auto flags = v.adpcmBlock[1];

                if (flags & (1 << 0)) {
                    v.caddr = 8 * v.loopaddr;

                    if (flags & (1 << 1)) {
                        /* TODO: force release */
                    }

                    /* TODO: set ENDX */

                    v.on = false;
                }

                if (flags & (1 << 2)) {
                    v.caddr = 8 * v.loopaddr;
                }

                v.hasBlock = true;
            }

            /* Increment pitch counter */

            u32 step = v.pitch;

            /* TODO: handle PMON */

            if (step > 0x3FFF) step = 0x4000;

            v.pitchCounter += step;

            /* Fetch new ADPCM sample */

            const auto adpcmIdx = v.pitchCounter >> 12;

            if (adpcmIdx < 28) {
                /* Move old samples */

                for (int i = 0; i < 3; i++) {
                    v.s[i] = v.s[i + 1];
                }

                v.s[3] = 128 * (i8)((v.adpcmBlock[2 + (adpcmIdx >> 1)] >> (4 * (adpcmIdx & 1))) << 4);

                sound[soundIdx++] = gauss::interpolate(v.pitchCounter >> 3, v.s[0], v.s[1], v.s[2], v.s[3]);
            } else {
                v.pitchCounter = 0;

                v.hasBlock = false;
            }
        }
    } else {
        sound[soundIdx++] = 0;
    }

    scheduler::addEvent(idStep, 0, SPU_RATE);
}

/* Handle Key On event */
void doKON() {
    for (int i = 0; i < 24; i++) {
        auto &v = voices[i];

        if (kon & (1 << i)) {
            /* TODO: set initial volume etc */

            v.caddr = 8 * v.addr;

            v.on = true;
        }
    }
}

void init() {
    const i16 out = 0;

    std::memset(&voices, 0, 24 * sizeof(Voice));

    /* Clear sound out file */
    std::ofstream file;

    file.open("snd.bin", std::ios::out | std::ios::binary | std::ios::trunc);

    file.write((char *)&out, 2);

    file.close();

    ram.resize(RAM_SIZE);

    idStep = scheduler::registerEvent([](int, i64) { step(); });

    scheduler::addEvent(idStep, 0, SPU_RATE);
}

/* Write audio to file */
void save() {
    std::ofstream file;

    file.open("snd.bin", std::ios::out | std::ios::binary | std::ios::app);

    file.write((char *)sound, 2 * soundIdx);

    file.close();

    soundIdx = 0;
}

u16 readRAM(u32 addr) {
    assert(addr < RAM_SIZE);

    u16 data;

    std::memcpy(&data, &ram[addr], 2);

    return data;
}

/* Writes a halfword to SPU RAM */
void writeRAM(u16 data) {
    assert(caddr < RAM_SIZE);

    std::printf("[SPU       ] [0x%05X] = 0x%04X\n", caddr, data);

    std::memcpy(&ram[caddr], &data, 2);

    ram[caddr] = data;

    caddr += 2;
}

u16 read(u32 addr) {
    u16 data;

    if (addr < static_cast<u32>(SPUReg::MVOLL)) { // SPU voices
        const auto vID = (addr >> 4) & 0x1F;

        switch (addr & ~(0x1F << 4)) {
            case static_cast<u32>(SPUReg::VOLL):
                std::printf("[SPU       ] 16-bit read @ V%u_VOLL\n", vID);
                break;
            case static_cast<u32>(SPUReg::VOLR):
                std::printf("[SPU       ] 16-bit read @ V%u_VOLR\n", vID);
                break;
            case static_cast<u32>(SPUReg::PITCH):
                std::printf("[SPU       ] 16-bit read @ V%u_PITCH\n", vID);
                break;
            case static_cast<u32>(SPUReg::ADDR):
                std::printf("[SPU       ] 16-bit read @ V%u_ADDR\n", vID);
                break;
            case static_cast<u32>(SPUReg::ADSR):
                std::printf("[SPU       ] 16-bit read @ V%u_ADSR_HI\n", vID);
                break;
            case static_cast<u32>(SPUReg::ADSR) + 2:
                std::printf("[SPU       ] 16-bit read @ V%u_ADSR_HI\n", vID);
                break;
            case static_cast<u32>(SPUReg::ADSRVOL):
                std::printf("[SPU       ] 16-bit read @ V%u_ADSRVOL\n", vID);
                break;
            default:
                std::printf("[SPU       ] Unhandled 16-bit voice %u read @ 0x%08X\n", vID, addr);

                exit(0);
        }

        return 0;
    } else if (inRange(addr, SPU_BASE + 0x188, 0x18)) { // Voice control
        switch (addr) {
            case static_cast<u32>(SPUReg::KON):
                std::printf("[SPU       ] 16-bit read @ KON_LO\n");
                return 0;
            case static_cast<u32>(SPUReg::KON) + 2:
                std::printf("[SPU       ] 16-bit read @ KON_HI\n");
                return 0;
            case static_cast<u32>(SPUReg::KOFF):
                std::printf("[SPU       ] 16-bit read @ KOFF_LO\n");
                return 0;
            case static_cast<u32>(SPUReg::KOFF) + 2:
                std::printf("[SPU       ] 16-bit read @ KOFF_HI\n");
                return 0;
            default:
                std::printf("[SPU       ] Unhandled 16-bit voice control read @ 0x%08X\n", addr);

                exit(0);
        }
    } else if (inRange(addr, SPU_BASE + 0x1A2, 0x1E)) { // SPU control
        switch (addr) {
            case static_cast<u32>(SPUReg::SPUCNT):
                std::printf("[SPU       ] 16-bit read @ SPUCNT\n");

                data  = spucnt.cden   << 0;
                data |= spucnt.exten  << 1;
                data |= spucnt.cdrev  << 2;
                data |= spucnt.extrev << 3;
                data |= spucnt.trxmod << 4;
                data |= spucnt.irqen  << 6;
                data |= spucnt.reven  << 7;
                data |= spucnt.nstep  << 8;
                data |= spucnt.nshift << 10;
                data |= spucnt.unmute << 14;
                data |= spucnt.spuen  << 15;
                break;
            case static_cast<u32>(SPUReg::FIFOCNT):
                std::printf("[SPU       ] 16-bit read @ FIFOCNT\n");
                return 4;
            case static_cast<u32>(SPUReg::SPUSTAT):
                std::printf("[SPU       ] 16-bit read @ SPUSTAT\n");

                data  = spustat.spumod;
                data |= spustat.irq9  << 6;
                data |= spustat.dmard << 7;
                data |= spustat.wrreq << 8;
                data |= spustat.rdreq << 9;
                data |= spustat.busy  << 10;
                data |= spustat.cbuf  << 11;
                break;
            default:
                std::printf("[SPU       ] Unhandled control 16-bit read @ 0x%08X\n", addr);

                exit(0);
        }
    } else {
        std::printf("[SPU       ] Unhandled 16-bit read @ 0x%08X\n", addr);

        exit(0);
    }

    return data;
}

void write(u32 addr, u16 data) {
    if (addr < static_cast<u32>(SPUReg::MVOLL)) { // SPU voices
        const auto vID = (addr >> 4) & 0x1F;

        auto &v = voices[vID];

        switch (addr & ~(0x1F << 4)) {
            case static_cast<u32>(SPUReg::VOLL):
                std::printf("[SPU       ] 16-bit write @ V%u_VOLL = 0x%04X\n", vID, data);
                break;
            case static_cast<u32>(SPUReg::VOLR):
                std::printf("[SPU       ] 16-bit write @ V%u_VOLR = 0x%04X\n", vID, data);
                break;
            case static_cast<u32>(SPUReg::PITCH):
                std::printf("[SPU       ] 16-bit write @ V%u_PITCH = 0x%04X\n", vID, data);

                v.pitch = data;
                break;
            case static_cast<u32>(SPUReg::ADDR):
                std::printf("[SPU       ] 16-bit write @ V%u_ADDR = 0x%04X\n", vID, data);

                v.addr = data;
                break;
            case static_cast<u32>(SPUReg::ADSR):
                std::printf("[SPU       ] 16-bit write @ V%u_ADSR_LO = 0x%04X\n", vID, data);
                break;
            case static_cast<u32>(SPUReg::ADSR) + 2:
                std::printf("[SPU       ] 16-bit write @ V%u_ADSR_HI = 0x%04X\n", vID, data);
                break;
            default:
                std::printf("[SPU       ] Unhandled 16-bit voice %u write @ 0x%08X = 0x%04X\n", vID, addr, data);

                exit(0);
        }
    } else if (inRange(addr, SPU_BASE + 0x180, 8)) { // SPU volume control
        switch (addr) {
            case static_cast<u32>(SPUReg::MVOLL):
                std::printf("[SPU       ] 16-bit write @ MVOLL = 0x%04X\n", data);
                break;
            case static_cast<u32>(SPUReg::MVOLR):
                std::printf("[SPU       ] 16-bit write @ MVOLR = 0x%04X\n", data);
                break;
            case static_cast<u32>(SPUReg::VLOUT):
                std::printf("[SPU       ] 16-bit write @ VLOUT = 0x%04X\n", data);
                break;
            case static_cast<u32>(SPUReg::VROUT):
                std::printf("[SPU       ] 16-bit write @ VROUT = 0x%04X\n", data);
                break;
            default:
                std::printf("[SPU       ] Unhandled 16-bit control write @ 0x%08X = 0x%04X\n", addr, data);

                exit(0);
        }
    } else if (inRange(addr, SPU_BASE + 0x188, 0x18)) { // Voice control
        switch (addr) {
            case static_cast<u32>(SPUReg::KON):
                std::printf("[SPU       ] 16-bit write @ KON_LO = 0x%04X\n", data);

                /* TODO: handle key on events */
                break;
            case static_cast<u32>(SPUReg::KON) + 2:
                std::printf("[SPU       ] 16-bit write @ KON_HI = 0x%04X\n", data);

                /* TODO: handle key on events */
                break;
            case static_cast<u32>(SPUReg::KOFF):
                std::printf("[SPU       ] 16-bit write @ KOFF_LO = 0x%04X\n", data);

                kon = (kon & 0xFFFF0000) | data;
                break;
            case static_cast<u32>(SPUReg::KOFF) + 2:
                std::printf("[SPU       ] 16-bit write @ KOFF_HI = 0x%04X\n", data);

                kon = (kon & 0xFFFF) | (data << 16);

                doKON();
                break;
            case static_cast<u32>(SPUReg::PMON):
                std::printf("[SPU       ] 16-bit write @ PMON_LO = 0x%04X\n", data);
                break;
            case static_cast<u32>(SPUReg::PMON) + 2:
                std::printf("[SPU       ] 16-bit write @ PMON_HI = 0x%04X\n", data);
                break;
            case static_cast<u32>(SPUReg::NON):
                std::printf("[SPU       ] 16-bit write @ NON_LO = 0x%04X\n", data);
                break;
            case static_cast<u32>(SPUReg::NON) + 2:
                std::printf("[SPU       ] 16-bit write @ NON_HI = 0x%04X\n", data);
                break;
            case static_cast<u32>(SPUReg::REVON):
                std::printf("[SPU       ] 16-bit write @ REVON_LO = 0x%04X\n", data);
                break;
            case static_cast<u32>(SPUReg::REVON) + 2:
                std::printf("[SPU       ] 16-bit write @ REVON_HI = 0x%04X\n", data);
                break;
            default:
                std::printf("[SPU       ] Unhandled 16-bit voice control write @ 0x%08X = 0x%04X\n", addr, data);

                exit(0);
        }
    } else if (inRange(addr, SPU_BASE + 0x1A2, 0x1E)) { // SPU control
        switch (addr) {
            case static_cast<u32>(SPUReg::REVADDR):
                std::printf("[SPU       ] 16-bit write @ REVADDR = 0x%04X\n", data);
                break;
            case static_cast<u32>(SPUReg::SPUADDR):
                std::printf("[SPU       ] 16-bit write @ SPUADDR = 0x%04X\n", data);

                spuaddr = data & 0xFFFF;
                
                caddr = 8 * spuaddr;
                break;
            case static_cast<u32>(SPUReg::SPUDATA):
                //std::printf("[SPU       ] 16-bit write @ SPUDATA = 0x%04X\n", data);

                writeRAM(data);
                break;
            case static_cast<u32>(SPUReg::SPUCNT):
                std::printf("[SPU       ] 16-bit write @ SPUCNT = 0x%04X\n", data);

                spucnt.cden   = data & (1 << 0);
                spucnt.exten  = data & (1 << 1);
                spucnt.cdrev  = data & (1 << 2);
                spucnt.extrev = data & (1 << 3);
                spucnt.trxmod = (data >> 4) & 3;
                spucnt.irqen  = data & (1 << 6);
                spucnt.reven  = data & (1 << 7);
                spucnt.nstep  = (data >>  8) & 3;
                spucnt.nshift = (data >> 10) & 0xF;
                spucnt.unmute = data & (1 << 14);
                spucnt.spuen  = data & (1 << 15);

                spustat.spumod = data & 0x3F;
                spustat.dmard  = data & (1 << 5);

                if (!spucnt.irqen) spustat.irq9 = false;
                break;
            case static_cast<u32>(SPUReg::FIFOCNT):
                std::printf("[SPU       ] 16-bit write @ FIFOCNT = 0x%04X\n", data);

                assert(data == 0x0004); // ??
                break;
            case static_cast<u32>(SPUReg::CDVOLL):
                std::printf("[SPU       ] 16-bit write @ CDVOLL = 0x%04X\n", data);
                break;
            case static_cast<u32>(SPUReg::CDVOLR):
                std::printf("[SPU       ] 16-bit write @ CDVOLR = 0x%04X\n", data);
                break;
            case static_cast<u32>(SPUReg::EVOLL):
                std::printf("[SPU       ] 16-bit write @ EVOLL = 0x%04X\n", data);
                break;
            case static_cast<u32>(SPUReg::EVOLR):
                std::printf("[SPU       ] 16-bit write @ EVOLR = 0x%04X\n", data);
                break;
            default:
                std::printf("[SPU       ] Unhandled 16-bit control write @ 0x%08X = 0x%04X\n", addr, data);

                exit(0);
        }
    } else if (inRange(addr, SPU_BASE + 0x1C0, 0x40)) {
        std::printf("[SPU       ] Unhandled 16-bit reverb write @ 0x%08X = 0x%04X\n", addr, data);
    } else {
        std::printf("[SPU       ] Unhandled 16-bit write @ 0x%08X = 0x%04X\n", addr, data);

        exit(0);
    }
}

}
