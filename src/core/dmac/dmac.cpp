/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "dmac.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>

#include "../intc.hpp"
#include "../scheduler.hpp"
#include "../bus/bus.hpp"
#include "../cdrom/cdrom.hpp"
#include "../gpu/gpu.hpp"

namespace ps::dmac {

using Interrupt = intc::Interrupt;

const char *chnNames[7] = {
    "MDEC_IN", "MDEC_OUT", "GPU", "CDROM", "SPU", "PIO", "OTC",
};

enum Mode {
    Burst,
    Slice,
    LinkedList,
};

/* --- DMA registers --- */

/* DMA channel registers */
enum class ChannelReg {
    MADR = 0x1F801000, // Memory address
    BCR  = 0x1F801004, // Block count
    CHCR = 0x1F801008, // Channel control
};

/* DMA control registers */
enum class ControlReg {
    DPCR      = 0x1F8010F0,
    DICR      = 0x1F8010F4,
};

/* DMA Interrupt Control */
struct DICR {
    bool fi;  // Force interrupt
    u8   im;  // Interrupt mask
    bool mie; // Master interrupt enable
    u8   ip;  // Interrupt pending
    bool mif; // Master interrupt flag
};

/* D_CHCR */
struct ChannelControl {
    bool dir; // Direction
    bool dec; // Decrementing address
    bool cpe; // Chopping enable
    u8   mod; // Mode
    u8   cpd; // Chopping window (DMA)
    u8   cpc; // Chopping window (CPU)
    bool str; // Start
    bool fst; // Forced start (don't wait for DRQ)
};

/* DMA channel */
struct DMAChannel {
    ChannelControl chcr;

    u16 size, count; // Block count
    u32 madr;  // Memory address

    u32 len;

    bool drq;
};

DMAChannel channels[7]; // DMA channels

/* DMA interrupt control */
DICR dicr;

u32 dpcr; // Priority control

u64 idTransferEnd; // Scheduler

void checkInterrupt();

void transferEndEvent(int chnID) {
    auto &chcr = channels[chnID].chcr;

    std::printf("[DMAC      ] %s transfer end\n", chnNames[chnID]);

    chcr.str = false;

    /* Set interrupt pending flag, check for interrupts */
    if (dicr.im & (1 << chnID)) dicr.ip |= 1 << chnID;

    checkInterrupt();
}

/* Returns DMA channel from address */
Channel getChannel(u32 addr) {
    switch ((addr >> 4) & 0xFF) {
        case 0x08: return Channel::MDECIN;
        case 0x09: return Channel::MDECOUT;
        case 0x0A: return Channel::GPU;
        case 0x0B: return Channel::CDROM;
        case 0x0C: return Channel::SPU;
        case 0x0D: return Channel::PIO;
        case 0x0E: return Channel::OTC;
        default:
            std::printf("[DMAC      ] Unknown channel\n");

            exit(0);
    }
}

/* Handles CDROM DMA */
void doCDROM() {
    const auto chnID = Channel::CDROM;

    auto &chn  = channels[static_cast<int>(chnID)];
    auto &chcr = chn.chcr;

    std::printf("[DMAC      ] CDROM transfer\n");

    assert(!chcr.dir); // Always to RAM
    assert(chcr.mod == Mode::Burst); // Always burst?
    assert(!chcr.dec); // Always incrementing?
    assert(chn.size);

    for (int i = 0; i < chn.size; i++) {
        bus::write32(chn.madr, cdrom::getData32());

        chn.madr += 4;
    }

    scheduler::addEvent(idTransferEnd, static_cast<int>(chnID), 24 * chn.size, true);

    /* Clear BCR */
    chn.count = 0;
    chn.size  = 0;
}

/* Handles GPU DMA */
void doGPU() {
    const auto chnID = Channel::GPU;

    auto &chn  = channels[static_cast<int>(chnID)];
    auto &chcr = chn.chcr;

    std::printf("[DMAC      ] GPU transfer\n");

    assert((chcr.mod == Mode::Slice) || (chcr.mod == Mode::LinkedList));
    assert(!chcr.dec); // Always incrementing?

    i64 len = 0;

    if (chcr.mod == Mode::Slice) {
        assert(chn.len);

        len += chn.len;

        if (chcr.dir) { // To GPU
            for (int i = 0; i < len; i++) {
                gpu::writeGP0(bus::read32(chn.madr));

                chn.madr += 4;
            }
        } else { // To RAM
            for (int i = 0; i < len; i++) {
                bus::write32(chn.madr, gpu::readGPUREAD());

                chn.madr += 4;
            }
        }
    } else {
        assert(chcr.dir);

        /* Linked list DMA */
        while (true) {
            /* Get header */
            const auto header = bus::read32(chn.madr);

            chn.madr += 4;

            const auto size = (int)(header >> 24);

            len += size;

            /* Transfer size words */
            for (int i = 0; i < size; i++) {
                gpu::writeGP0(bus::read32(chn.madr));
            
                chn.madr += 4;
            }

            if (header & (1 << 23)) break; // We're done

            chn.madr = header & 0x1FFFFC;
        }
    }

    scheduler::addEvent(idTransferEnd, static_cast<int>(chnID), len, true);

    /* Clear DMA request */
    //chn.drq = false;

    /* Clear BCR */
    chn.count = 0;
    chn.size  = 0;
}

/* Handles OTC DMA */
void doOTC() {
    const auto chnID = Channel::OTC;

    auto &chn  = channels[static_cast<int>(chnID)];
    auto &chcr = chn.chcr;

    std::printf("[DMAC      ] OTC transfer\n");

    assert(!chcr.dir); // Always to RAM
    assert(chcr.mod == Mode::Burst); // Always burst?
    assert(chcr.dec); // Always decrementing?
    assert(chn.size);

    for (int i = chn.size; i > 0; i--) {
        u32 data;
        if (i != 1) { data = chn.madr - 4; } else { data = 0xFFFFFF; }

        bus::write32(chn.madr, data);

        chn.madr -= 4;
    }

    scheduler::addEvent(idTransferEnd, static_cast<int>(chnID), chn.size, true);

    /* Clear BCR */
    chn.count = 0;
    chn.size  = 0;
}

/* Handles SPU DMA */
void doSPU() {
    const auto chnID = Channel::SPU;

    auto &chn  = channels[static_cast<int>(chnID)];
    auto &chcr = chn.chcr;

    std::printf("[DMAC      ] SPU transfer\n");

    //assert(!chcr.dir); // Always to RAM?
    assert(chcr.mod == Mode::Slice); // Always slice?
    assert(!chcr.dec); // Always incrementing?
    assert(chn.len);

    /* TODO: SPU DMA */

    scheduler::addEvent(idTransferEnd, static_cast<int>(chnID), 4 * chn.len, true);

    /* Clear BCR */
    chn.count = 0;
    chn.size  = 0;
    chn.len   = 0;
}

void startDMA(Channel chn) {
    switch (chn) {
        case Channel::GPU  : doGPU(); break;
        case Channel::CDROM: doCDROM(); break;
        case Channel::SPU  : doSPU(); break;
        case Channel::OTC  : doOTC(); break;
        default:
            std::printf("[DMAC      ] Unhandled channel %d (%s) transfer\n", chn, chnNames[static_cast<int>(chn)]);

            exit(0);
    }
}

/* Sets master interrupt flag, sends interrupt */
void checkInterrupt() {
    const auto oldMIF = dicr.mif;

    dicr.mif = dicr.fi || (dicr.mie && (dicr.im & dicr.ip));
    
    std::printf("[DMAC      ] MIF = %d\n", dicr.mif);

    if (!oldMIF && dicr.mif) intc::sendInterrupt(Interrupt::DMA);
}

void checkRunning(Channel chn) {
    const auto chnID = static_cast<int>(chn);

    std::printf("[DMAC      ] Channel %d check\n", chnID);

    const bool cde = dpcr & (1 << (4 * chnID + 3));

    std::printf("[DMAC      ] D%d.DRQ = %d, DPCR.CDE%d = %d, D%d_CHCR.STR = %d, D%d_CHCR.FST = %d\n", chnID, channels[chnID].drq, chnID, cde, chnID, channels[chnID].chcr.str, chnID, channels[chnID].chcr.fst);

    if ((channels[chnID].drq || channels[chnID].chcr.fst) && cde && channels[chnID].chcr.str) startDMA(static_cast<Channel>(chnID));
}

void checkRunningAll() {
    for (int i = 0; i < 7; i++) {
        const bool cde = dpcr & (1 << (4 * i + 3));

        std::printf("[DMAC      ] D%d.DRQ = %d, DPCR.CDE%d = %d, D%d_CHCR.STR = %d, D%d_CHCR.FST = %d\n", i, channels[i].drq, i, cde, i, channels[i].chcr.str, i, channels[i].chcr.fst);

        if ((channels[i].drq || channels[i].chcr.fst) && cde && channels[i].chcr.str) return startDMA(static_cast<Channel>(i));
    }
}

void init() {
    std::memset(&channels, 0, 7 * sizeof(DMAChannel));

    /* Set initial DRQs */
    channels[static_cast<int>(Channel::MDECIN)].drq = true;
    channels[static_cast<int>(Channel::GPU   )].drq = true; // Hack
    channels[static_cast<int>(Channel::SPU   )].drq = true;
    channels[static_cast<int>(Channel::OTC   )].drq = true;

    /* TODO: register scheduler events */
    idTransferEnd = scheduler::registerEvent([](int chnID, i64) { transferEndEvent(chnID); });
}

u32 read(u32 addr) {
    u32 data;

    if (addr < static_cast<u32>(ControlReg::DPCR)) {
        const auto chnID = static_cast<int>(getChannel(addr));

        auto &chn = channels[chnID];

        switch (addr & ~(0xFF0)) {
            case static_cast<u32>(ChannelReg::CHCR):
                {
                    auto &chcr = chn.chcr;

                    //std::printf("[DMAC      ] 32-bit read @ D%d_CHCR\n", chnID);

                    data  = chcr.dir;
                    data |= chcr.dec << 1;
                    data |= chcr.mod << 9;
                    data |= chcr.cpd << 16;
                    data |= chcr.cpc << 20;
                    data |= chcr.str << 24;
                    data |= chcr.fst << 28;
                }
                break;
            default:
                std::printf("[DMAC      ] Unhandled 32-bit channel read @ 0x%08X\n", addr);

                exit(0);
        }
    } else {
        switch (addr) {
            case static_cast<u32>(ControlReg::DPCR):
                std::printf("[DMAC      ] 32-bit read @ DPCR\n");
                return dpcr;
            case static_cast<u32>(ControlReg::DICR):
                std::printf("[DMAC      ] 32-bit read @ DICR\n");

                data  = dicr.fi  << 15;
                data |= dicr.im  << 16;
                data |= dicr.mie << 23;
                data |= dicr.ip  << 24;
                data |= dicr.mif << 31;
                break;
            default:
                std::printf("[DMAC      ] Unhandled 32-bit control read @ 0x%08X\n", addr);

                exit(0);
        }
    }

    return data;
}

void write(u32 addr, u32 data) {
    if (addr < static_cast<u32>(ControlReg::DPCR)) {
        const auto chnID = static_cast<int>(getChannel(addr));

        auto &chn = channels[chnID];

        switch (addr & ~(0xFF0)) {
            case static_cast<u32>(ChannelReg::MADR):
                std::printf("[DMAC      ] 32-bit write @ D%u_MADR = 0x%08X\n", chnID, data);

                chn.madr = data & 0xFFFFFC;
                break;
            case static_cast<u32>(ChannelReg::BCR):
                std::printf("[DMAC      ] 32-bit write @ D%u_BCR = 0x%08X\n", chnID, data);

                chn.size  = data;
                chn.count = data >> 16;
                
                chn.len = (u32)chn.count * (u32)chn.size;
                break;
            case static_cast<u32>(ChannelReg::CHCR):
                {
                    auto &chcr = chn.chcr;

                    std::printf("[DMAC      ] 32-bit write @ D%u_CHCR = 0x%08X\n", chnID, data);

                    chcr.dir = data & (1 << 0);
                    chcr.dec = data & (1 << 1);
                    chcr.mod = (data >>  9) & 3;
                    chcr.cpd = (data >> 16) & 7;
                    chcr.cpc = (data >> 20) & 7;
                    chcr.str = data & (1 << 24);
                    chcr.fst = data & (1 << 28);
                }

                checkRunning(static_cast<Channel>(chnID));
                break;
            default:
                std::printf("[DMAC      ] Unhandled 32-bit channel write @ 0x%08X = 0x%08X\n", addr, data);

                exit(0);
        }
    } else {
        switch (addr) {
            case static_cast<u32>(ControlReg::DPCR):
                std::printf("[DMAC      ] 32-bit write @ DPCR = 0x%08X\n", data);

                dpcr = data;

                checkRunningAll();
                break;
            case static_cast<u32>(ControlReg::DICR):
                std::printf("[DMAC      ] 32-bit write @ DICR = 0x%08X\n", data);

                dicr.fi  = data & (1 << 15);
                dicr.im  = (data >> 16) & 0x7F;
                dicr.mie = data & (1 << 23);
                dicr.ip  = (dicr.ip & ~(data >> 24)) & 0x7F;

                checkInterrupt();
                break;
            default:
                std::printf("[DMAC      ] Unhandled 32-bit control write @ 0x%08X = 0x%08X\n", addr, data);

                exit(0);
        }
    }
}

/* Sets DRQ, runs channel if enabled */
void setDRQ(Channel chn, bool drq) {
    channels[static_cast<int>(chn)].drq = drq;

    checkRunning(chn);
}

}
