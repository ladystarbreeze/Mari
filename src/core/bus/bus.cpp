/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "bus.hpp"

#include <cstdio>

#include "../../common/file.hpp"

namespace ps::bus {

/* --- PS memory regions --- */

/* Base addresses */
enum class MemoryBase {
    RAM   = 0x00000000,
    SPRAM = 0x1F800000, // Scratchpad RAM
    SIO   = 0x1F801040, // Serial I/O
    DMA   = 0x1F801080, // DMA controller
    Timer = 0x1F801100,
    SPU   = 0x1F801D80, // Sound processing unit
    BIOS  = 0x1FC00000,
};

/* Memory sizes */
enum class MemorySize {
    RAM   = 0x200000,
    SPRAM = 0x000400,
    SIO   = 0x000020,
    DMA   = 0x000080,
    Timer = 0x000030,
    SPU   = 0x000300,
    BIOS  = 0x080000,
};

/* --- PlayStation memory --- */
std::vector<u8> ram;
std::vector<u8> bios;

/* Returns true if address is in range [base;size] */
bool inRange(u64 addr, u64 base, u64 size) {
    return (addr >= base) && (addr < (base + size));
}

void init(const char *biosPath) {
    ram.resize(static_cast<int>(MemorySize::RAM));

    bios = loadBinary(biosPath);

    assert(bios.size() == static_cast<u64>(MemorySize::BIOS));

    std::printf("[Bus       ] Init OK\n");
}

/* Reads a byte from the system bus */
u8 read8(u32 addr) {
    if (inRange(addr, static_cast<u32>(MemoryBase::BIOS), static_cast<u32>(MemorySize::BIOS))) {
        return bios[addr - static_cast<u16>(MemoryBase::BIOS)];
    } else {
        switch (addr) {
            default:
                std::printf("[Bus       ] Unhandled 8-bit read @ 0x%08X\n", addr);

                exit(0);
        }
    }
}

/* Reads a halfword from the system bus */
u16 read16(u32 addr) {
    u16 data;

    if (inRange(addr, static_cast<u32>(MemoryBase::BIOS), static_cast<u32>(MemorySize::BIOS))) {
        std::memcpy(&data, &bios[addr - static_cast<u16>(MemoryBase::BIOS)], sizeof(u16));
    } else {
        switch (addr) {
            default:
                std::printf("[Bus       ] Unhandled 16-bit read @ 0x%08X\n", addr);

                exit(0);
        }
    }

    return data;
}

/* Reads a word from the system bus */
u32 read32(u32 addr) {
    u32 data;

    if (inRange(addr, static_cast<u32>(MemoryBase::BIOS), static_cast<u32>(MemorySize::BIOS))) {
        std::memcpy(&data, &bios[addr - static_cast<u32>(MemoryBase::BIOS)], sizeof(u32));
    } else {
        switch (addr) {
            default:
                std::printf("[Bus       ] Unhandled 32-bit read @ 0x%08X\n", addr);

                exit(0);
        }
    }

    return data;
}

/* Writes a byte to the system bus */
void write8(u32 addr, u8 data) {
    if (inRange(addr, static_cast<u32>(MemoryBase::RAM), static_cast<u32>(MemorySize::RAM))) {
        ram[addr] = data;
    } else {
        switch (addr) {
            default:
                std::printf("[Bus       ] Unhandled 8-bit write @ 0x%08X = 0x%02X\n", addr, data);

                exit(0);
        }
    }
}

/* Writes a halfword to the system bus */
void write16(u32 addr, u16 data) {
    if (inRange(addr, static_cast<u32>(MemoryBase::RAM), static_cast<u32>(MemorySize::RAM))) {
        memcpy(&ram[addr], &data, sizeof(u16));
    } else {
        switch (addr) {
            default:
                std::printf("[Bus       ] Unhandled 16-bit write @ 0x%08X = 0x%04X\n", addr, data);

                exit(0);
        }
    }
}

/* Writes a word to the system bus */
void write32(u32 addr, u32 data) {
    if (inRange(addr, static_cast<u32>(MemoryBase::RAM), static_cast<u32>(MemorySize::RAM))) {
        memcpy(&ram[addr], &data, sizeof(u32));
    } else {
        switch (addr) {
            default:
                std::printf("[Bus       ] Unhandled 32-bit write @ 0x%08X = 0x%08X\n", addr, data);

                exit(0);
        }
    }
}

}
