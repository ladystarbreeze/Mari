/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#pragma once

#include <functional>

#include "../common/types.hpp"

namespace ps::scheduler {

void init();

void flush();

u64 registerEvent(std::function<void(int, i64)> func);

void addEvent(u64 id, int param, i64 cyclesUntilEvent);
void removeEvent(u64 id);
void processEvents(i64 elapsedCycles);

i64 getRunCycles();

}
