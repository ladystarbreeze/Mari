/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#pragma once

#include "../../common/types.hpp"

namespace ps::spu {

void init();
void save();

void writeRAM(u16 data);

u16 read(u32 addr);

void write(u32 addr, u16 data);

}
