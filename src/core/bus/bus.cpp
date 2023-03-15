/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "bus.hpp"

#include <cstdio>

#include "../intc.hpp"

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

/* Expansion region bases/sizes */
u32 exp1Base = 0x1F000000, exp1Size;
u32 exp2Base = 0x1F000000, exp2Size;
u32 exp3Base = 0x1FA00000, exp3Size; // Base is fixed!!

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
    if (inRange(addr, exp1Base, exp1Size)) {
        std::printf("[Bus       ] 8-bit read @ 0x%08X (EXP1)\n", addr);

        return 0;
    }

    if (inRange(addr, static_cast<u32>(MemoryBase::RAM), static_cast<u32>(MemorySize::RAM))) {
        return ram[addr];
    } else if (inRange(addr, static_cast<u32>(MemoryBase::BIOS), static_cast<u32>(MemorySize::BIOS))) {
        return bios[addr - static_cast<u32>(MemoryBase::BIOS)];
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

    if (inRange(addr, static_cast<u32>(MemoryBase::RAM), static_cast<u32>(MemorySize::RAM))) {
        std::memcpy(&data, &ram[addr], sizeof(u16));
    } else if (inRange(addr, static_cast<u32>(MemoryBase::BIOS), static_cast<u32>(MemorySize::BIOS))) {
        std::memcpy(&data, &bios[addr - static_cast<u32>(MemoryBase::BIOS)], sizeof(u16));
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

    if (inRange(addr, static_cast<u32>(MemoryBase::RAM), static_cast<u32>(MemorySize::RAM))) {
        std::memcpy(&data, &ram[addr], sizeof(u32));
    } else if (inRange(addr, static_cast<u32>(MemoryBase::BIOS), static_cast<u32>(MemorySize::BIOS))) {
        std::memcpy(&data, &bios[addr - static_cast<u32>(MemoryBase::BIOS)], sizeof(u32));
    } else {
        switch (addr) {
            case 0x1F801070:
                std::printf("[Bus       ] 32-bit read @ I_STAT\n");
                return intc::readStat();
            case 0x1F801074:
                std::printf("[Bus       ] 32-bit read @ I_MASK\n");
                return intc::readMask();
            default:
                std::printf("[Bus       ] Unhandled 32-bit read @ 0x%08X\n", addr);

                exit(0);
        }
    }

    return data;
}

/* Writes a byte to the system bus */
void write8(u32 addr, u8 data) {
    if (inRange(addr, exp2Base, exp2Size)) {
        if (addr == (exp2Base + 0x41)) {
            std::printf("[PS        ] POST = 0x%02X\n", data);
        } else {
            std::printf("[Bus       ] 8-bit write @ 0x%08X (EXP2) = 0x%02X\n", addr, data);
        }

        return;
    }

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
    } else if (inRange(addr, static_cast<u32>(MemoryBase::SPU), static_cast<u32>(MemorySize::SPU))) {
        std::printf("[Bus       ] Unhandled 16-bit write @ 0x%08X (SPU) = 0x%04X\n", addr, data);
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
            case 0x1F801000:
                std::printf("[Bus       ] 32-bit write @ EXP1_BASE = 0x%08X\n", data);

                exp1Base = (exp1Base & 0xFF000000) | (data & 0xFFFFFF);
                break;
            case 0x1F801004:
                std::printf("[Bus       ] 32-bit write @ EXP2_BASE = 0x%08X\n", data);

                exp2Base = (exp2Base & 0xFF000000) | (data & 0xFFFFFF);
                break;
            case 0x1F801008:
                std::printf("[Bus       ] 32-bit write @ EXP1_SIZE = 0x%08X\n", data);

                exp1Size = 1 << ((data >> 16) & 0x1F);
                break;
            case 0x1F80100C:
                std::printf("[Bus       ] 32-bit write @ EXP3_SIZE = 0x%08X\n", data);

                exp3Size = 1 << ((data >> 16) & 0x1F);
                break;
            case 0x1F801010:
                std::printf("[Bus       ] 32-bit write @ BIOS_DELAY = 0x%08X\n", data);
                break;
            case 0x1F801014:
                std::printf("[Bus       ] 32-bit write @ SPU_DELAY = 0x%08X\n", data);
                break;
            case 0x1F801018:
                std::printf("[Bus       ] 32-bit write @ CDROM_DELAY = 0x%08X\n", data);
                break;
            case 0x1F80101C:
                std::printf("[Bus       ] 32-bit write @ EXP2_SIZE = 0x%08X\n", data);

                exp2Size = 1 << ((data >> 16) & 0x1F);
                break;
            case 0x1F801020:
                std::printf("[Bus       ] 32-bit write @ COM_DELAY = 0x%08X\n", data);
                break;
            case 0x1F801060:
                std::printf("[Bus       ] 32-bit write @ RAM_SIZE = 0x%08X\n", data);
                break;
            case 0x1F801070:
                std::printf("[Bus       ] 32-bit write @ I_STAT = 0x%08X\n", data);
                return intc::writeStat(data);
            case 0x1F801074:
                std::printf("[Bus       ] 32-bit write @ I_MASK = 0x%08X\n", data);
                return intc::writeMask(data);
            case 0x1FFE0130:
                std::printf("[Bus       ] 32-bit write @ CACHE_CONTROL = 0x%08X\n", data);
                break;
            default:
                std::printf("[Bus       ] Unhandled 32-bit write @ 0x%08X = 0x%08X\n", addr, data);

                exit(0);
        }
    }
}

}
