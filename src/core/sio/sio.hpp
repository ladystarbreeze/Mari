/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#pragma once

#include "../../common/types.hpp"

namespace ps::sio {

void init();

u8  read8(u32 addr);
u16 read16(u32 addr);
u32 read32(u32 addr);

void write8(u32 addr, u8 data);
void write16(u32 addr, u16 data);
void write32(u32 addr, u32 data);

void setInput(u16 input);

}
