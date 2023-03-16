/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "../../common/types.hpp"

namespace ps::gpu {

void init();

void writeGP0(u32 data);
void writeGP1(u32 data);

}
