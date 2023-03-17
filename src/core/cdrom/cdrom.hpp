/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#pragma once

#include "../../common/types.hpp"

namespace ps::cdrom {

void init();

u8 read(u32 addr);

void write(u32 addr, u8 data);

}
