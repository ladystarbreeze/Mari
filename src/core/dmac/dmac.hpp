/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#pragma once

#include "../../common/types.hpp"

namespace ps::dmac {

/* DMA channels */
enum class Channel {
    MDECIN,
    MDECOUT,
    GPU,
    CDROM,
    SPU,
    PIO,
    OTC,
};

void init();

u32 read(u32 addr);

void write8(u32 addr, u8 data);
void write32(u32 addr, u32 data);

void setDRQ(Channel chn, bool drq);

}
