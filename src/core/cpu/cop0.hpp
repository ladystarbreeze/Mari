/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#pragma once

#include "../../common/types.hpp"

namespace ps::cpu::cop0 {

/* Exception codes */
enum Exception {
    Interrupt  = 0x0,
    SystemCall = 0x8,
};

/* Exception names */
static const char *eNames[32] = { 
    "INT", "MOD", "TLBL", "TLBS", "AdEL", "AdES", "IBE", "DBE", "Syscall", "BP", "RI", "CpU", "Ov",
};

void init();

u32 get(u32 idx);

void set(u32 idx, u32 data);

void enterException(Exception e);
void leaveException();

void setInterruptPending(bool irq);

bool isBEV();
bool isCacheIsolated();

void setBD(bool bd);
void setEPC(u32 pc);

}
