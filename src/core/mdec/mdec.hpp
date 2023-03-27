/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#pragma once

#include "../../common/types.hpp"

namespace ps::mdec {

void init();

u32 readData();
u32 readStat();

void writeCmd(u32 data);
void writeCtrl(u32 data);

}
