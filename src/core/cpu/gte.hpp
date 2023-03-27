/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#pragma once

#include "../../common/types.hpp"

namespace ps::cpu::gte {

u32 get(u32 idx);
u32 getControl(u32 idx);

void set(u32 idx, u32 data);
void setControl(u32 idx, u32 data);

void doCmd(u32 cmd);

}
