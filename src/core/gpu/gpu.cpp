/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "gpu.hpp"

#include <cassert>
#include <cstdio>
#include <queue>

#include "../intc.hpp"
#include "../scheduler.hpp"

namespace ps::gpu {

using Interrupt = intc::Interrupt;

/* --- GPU constants --- */

constexpr i64 CYCLES_PER_SCANLINE = 3413; // NTSC
constexpr i64 SCANLINES_PER_VDRAW = 240;
constexpr i64 SCANLINES_PER_FRAME = 262;

enum GPUState {
    ReceiveCommand,
    ReceiveArguments,
    CopyRectangle,
};

GPUState state = GPUState::ReceiveCommand;
int argCount;

u8 cmd; // Current command
std::queue<u32> cmdParam;

i64 lineCounter = 0;

u64 idScanline; // Scheduler

/* Handles scanline events */
void scanlineEvent(i64 c) {
    ++lineCounter;

    if (lineCounter == SCANLINES_PER_VDRAW) {
        intc::sendInterrupt(Interrupt::VBLANK);
    } else if (lineCounter == SCANLINES_PER_FRAME) {
        lineCounter = 0;
    }
    
    scheduler::addEvent(idScanline, 0, CYCLES_PER_SCANLINE + c, false);
}

void setArgCount(int c) {
    argCount = c;

    state = GPUState::ReceiveArguments;
}

/* GP0(0xA0) Copy Rectangle (CPU->VRAM) */
void copyCPUToVRAM() {
    cmdParam.pop();

    const auto wh = cmdParam.front();
    cmdParam.pop();

    argCount = ((int)(wh >> 16) * (int)(wh & 0xFFFF)) / 2;

    state = GPUState::CopyRectangle;
}

void init() {
    idScanline = scheduler::registerEvent([](int, i64 c) { scanlineEvent(c); });

    scheduler::addEvent(idScanline, 0, CYCLES_PER_SCANLINE, true);
}

void writeGP0(u32 data) {
    switch (state) {
        case GPUState::ReceiveCommand:
            {
                cmd = data >> 24;

                switch (cmd) {
                    case 0x01:
                        std::printf("[GPU:GP0   ] Clear Cache\n");
                        break;
                    case 0x02:
                        std::printf("[GPU:GP0   ] Fill VRAM\n");

                        setArgCount(2);
                        break;
                    case 0x2C:
                        std::printf("[GPU:GP0   ] Draw Textured Quad (semi-transparent, blended)\n");

                        setArgCount(8);
                        break;
                    case 0x30:
                        std::printf("[GPU:GP0   ] Draw Shaded Tri (opaque)\n");

                        setArgCount(5);
                        break;
                    case 0x38:
                        std::printf("[GPU:GP0   ] Draw Shaded Quad (opaque)\n");

                        setArgCount(7);
                        break;
                    case 0xA0:
                        std::printf("[GPU:GP0   ] Copy Rectangle (CPU->VRAM)\n");

                        setArgCount(2);
                        break;
                    case 0xC0:
                        std::printf("[GPU:GP0   ] Copy Rectangle (VRAM->CPU)\n");

                        setArgCount(2);
                        break;
                    case 0xE1:
                        std::printf("[GPU:GP0   ] Set Draw Mode\n");
                        break;
                    case 0xE2:
                        std::printf("[GPU:GP0   ] Set Texture Window\n");
                        break;
                    case 0xE3:
                        std::printf("[GPU:GP0   ] Set Drawing Area (TL)\n");
                        break;
                    case 0xE4:
                        std::printf("[GPU:GP0   ] Set Drawing Area (BR)\n");
                        break;
                    case 0xE5:
                        std::printf("[GPU:GP0   ] Set Drawing Offset\n");
                        break;
                    default:
                        std::printf("[GPU       ] Unhandled GP0 command 0x%02X (0x%08X)\n", cmd, data);

                        exit(0);
                }
            }
            break;
        case GPUState::ReceiveArguments:
            std::printf("[GPU:GP0   ] 0x%08X\n", data);

            cmdParam.push(data);

            if (!--argCount) {
                switch (cmd) {
                    case 0xA0: copyCPUToVRAM(); break;
                    default:
                        while (cmdParam.size()) cmdParam.pop();

                        state = GPUState::ReceiveCommand;
                }
            }
            break;
        case GPUState::CopyRectangle:
            std::printf("[GPU:GP0   ] CPU->VRAM write = 0x%08X\n", data);

            if (!--argCount) state = GPUState::ReceiveCommand;
            break;
        default:
            exit(0);
    }
}

void writeGP1(u32 data) {
    const auto cmd = data >> 24;

    switch (cmd) {
        case 0x00:
            std::printf("[GPU:GP1   ] Reset GPU\n");
            break;
        case 0x01:
            std::printf("[GPU:GP1   ] Reset Command Buffer\n");
            break;
        case 0x02:
            std::printf("[GPU:GP1   ] Ack GPU Interrupt\n");
            break;
        case 0x03:
            std::printf("[GPU:GP1   ] Enable Display\n");
            break;
        case 0x04:
            std::printf("[GPU:GP1   ] Set DMA Direction\n");
            break;
        case 0x05:
            std::printf("[GPU:GP1   ] Set Display Area\n");
            break;
        case 0x06:
            std::printf("[GPU:GP1   ] Set Horizontal Range\n");
            break;
        case 0x07:
            std::printf("[GPU:GP1   ] Set Vertical Range\n");
            break;
        case 0x08:
            std::printf("[GPU:GP1   ] Set Display Mode\n");
            break;
        default:
            std::printf("[GPU       ] Unhandled GP1 command 0x%02X (0x%08X)\n", cmd, data);
            
            exit(0);
    }
}

}
