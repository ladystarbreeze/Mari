/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#pragma once

#include "../common/types.hpp"

namespace ps {

void init(const char *biosPath, const char *isoPath);
void run();

void update(const u8 *fb);

}
