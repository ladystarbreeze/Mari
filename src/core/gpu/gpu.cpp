/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "gpu.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <queue>
#include <vector>

#include "../intc.hpp"
#include "../Mari.hpp"
#include "../scheduler.hpp"

namespace ps::gpu {

using Interrupt = intc::Interrupt;

/* --- GPU constants --- */

constexpr i64 CYCLES_PER_SCANLINE = 3413; // NTSC
constexpr i64 SCANLINES_PER_VDRAW = 240;
constexpr i64 SCANLINES_PER_FRAME = 262;

constexpr size_t VRAM_WIDTH  = 1024;
constexpr size_t VRAM_HEIGHT = 512;

/* 2D vertex */
struct Vertex {
    Vertex() : x(0), y(0), c(0), tex(0) {}
    Vertex(u32 v, u32 c) : x((i32)((v & 0x7FF) << 21) >> 21), y((i32)(((v >> 16) & 0x7FF) << 21) >> 21), c(c & 0xFFFFFF), tex(0) {}

    i32 x, y; // Coordinates

    u32 c; // Color
    u32 tex; // Tex coord
};

/* Drawing area */
struct XYArea {
    i32 x0, x1, y0, y1;
};

/* Drawing offset */
struct XYOffset {
    i32 xofs, yofs;
};

enum GPUState {
    ReceiveCommand,
    ReceiveArguments,
    CopyRectangle,
};

GPUState state = GPUState::ReceiveCommand;
int argCount;

u8 cmd; // Current command
std::queue<u32> cmdParam;

std::vector<u16> vram;

/* GPU drawing parameters */
XYArea xyarea;
XYOffset xyoffset;

i64 lineCounter = 0;

u64 idScanline; // Scheduler

/* Handles scanline events */
void scanlineEvent(i64 c) {
    ++lineCounter;

    if (lineCounter == SCANLINES_PER_VDRAW) {
        intc::sendInterrupt(Interrupt::VBLANK);

        update((u8 *)vram.data());
    } else if (lineCounter == SCANLINES_PER_FRAME) {
        lineCounter = 0;
    }
    
    scheduler::addEvent(idScanline, 0, CYCLES_PER_SCANLINE + c, false);
}

void setArgCount(int c) {
    argCount = c;

    state = GPUState::ReceiveArguments;
}

/* Converts BGR24 to BGR555 */
inline u16 toBGR555(u32 c) {
    const u16 b = (c >> 19) & 0x1F;
    const u16 g = (c >> 11) & 0x1F;
    const u16 r = (c >>  3) & 0x1F;

    return (b << 10) | (g << 5) | r;
}

void drawPixel24(i32 x, i32 y, u32 c) {
    x += xyoffset.xofs;
    y += xyoffset.yofs;

    if ((x < xyarea.x0) || (x >= xyarea.x1) || (y < xyarea.y0) || (y >= xyarea.y1)) return;

    //std::printf("[GPU       ] X = %d (%d), Y = %d (%d), COL = 0x%08X\n", x, xyoffset.xofs, y, xyoffset.yofs, c);

    vram[x + 1024 * y] = toBGR555(c);
}

i32 edgeFunction(const Vertex &a, const Vertex &b, const Vertex &c) {
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/* Draws a Gouraud shaded triangle */
void drawShadedTri(const Vertex &v0, const Vertex &v1, const Vertex &v2) {
    Vertex p, b, c;

    /* Ensure the winding order is correct */
    if (edgeFunction(v0, v1, v2) < 0) {
        b = v2;
        c = v1;
    } else {
        b = v1;
        c = v2;
    }

    const auto area = edgeFunction(v0, b, c);

    /* TODO: calculate bounding box */
    for (p.y = 0; p.y < 480; p.y++) {
        for (p.x = 0; p.x < 640; p.x++) {
            /* Calculate weights */
            const auto w0 = edgeFunction( b,  c, p);
            const auto w1 = edgeFunction( c, v0, p);
            const auto w2 = edgeFunction(v0,  b, p);

            /* Is point inside of triangle ? */
            if ((w0 >= 0) && (w1 >= 0) && (w2 >= 0)) {
                /* Interpolate color */
                const u32 cr = (w0 * ((v0.c >>  0) & 0xFF) + w1 * ((b.c >>  0) & 0xFF) + w2 * ((c.c >>  0) & 0xFF)) / area;
                const u32 cg = (w0 * ((v0.c >>  8) & 0xFF) + w1 * ((b.c >>  8) & 0xFF) + w2 * ((c.c >>  8) & 0xFF)) / area;
                const u32 cb = (w0 * ((v0.c >> 16) & 0xFF) + w1 * ((b.c >> 16) & 0xFF) + w2 * ((c.c >> 16) & 0xFF)) / area;

                const auto color = (cb << 16) | (cg << 8) | cr;

                drawPixel24(p.x, p.y, color);
            }
        }
    }
}

/* GP0(0x02) Fill Rectangle */
void fillRect() {
    const auto c = cmdParam.front() & 0xFFFFFF; cmdParam.pop();

    const auto coords = cmdParam.front(); cmdParam.pop();
    const auto dims = cmdParam.front(); cmdParam.pop();

    const auto xMin = 16 * ((coords >>  0) & 0xFFFF); // In 16px units
    auto y = 16 * ((coords >> 16) & 0xFFFF); // In 16px units

    const auto width  = 16 * ((dims >>  0) & 0xFFFF) + xMin; // In 16px units
    const auto height = 16 * ((dims >> 16) & 0xFFFF) + y;    // In 16px units

    for (; y < height; y++)
	{
		for (auto x = xMin; x < width; x++)
		{
			drawPixel24(x, y, c);
		}
	}

	state = GPUState::ReceiveCommand;
}

/* GP0(0x30) Draw Shaded Triangle (opaque) */
void drawTri30() {
    const auto c0 = cmdParam.front(); cmdParam.pop();
    const auto v0 = cmdParam.front(); cmdParam.pop();
    const auto c1 = cmdParam.front(); cmdParam.pop();
    const auto v1 = cmdParam.front(); cmdParam.pop();
    const auto c2 = cmdParam.front(); cmdParam.pop();
    const auto v2 = cmdParam.front(); cmdParam.pop();

    drawShadedTri(Vertex(v0, c0), Vertex(v1, c1), Vertex(v2, c2));

    state = GPUState::ReceiveCommand;
}

/* GP0(0x38) Draw Shaded Quadrilateral (opaque) */
void drawQuad38() {
    const auto c0 = cmdParam.front(); cmdParam.pop();
    const auto v0 = cmdParam.front(); cmdParam.pop();
    const auto c1 = cmdParam.front(); cmdParam.pop();
    const auto v1 = cmdParam.front(); cmdParam.pop();
    const auto c2 = cmdParam.front(); cmdParam.pop();
    const auto v2 = cmdParam.front(); cmdParam.pop();
    const auto c3 = cmdParam.front(); cmdParam.pop();
    const auto v3 = cmdParam.front(); cmdParam.pop();

    drawShadedTri(Vertex(v0, c0), Vertex(v1, c1), Vertex(v2, c2));
    drawShadedTri(Vertex(v1, c1), Vertex(v2, c2), Vertex(v3, c3));

    state = GPUState::ReceiveCommand;
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

    vram.resize(VRAM_WIDTH * VRAM_HEIGHT);

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

                        cmdParam.push(data); // Also first argument

                        setArgCount(2);
                        break;
                    case 0x2C:
                        std::printf("[GPU:GP0   ] Draw Textured Quad (semi-transparent, blended)\n");

                        setArgCount(8);
                        break;
                    case 0x30:
                        std::printf("[GPU:GP0   ] Draw Shaded Tri (opaque)\n");

                        cmdParam.push(data); // Also first argument

                        setArgCount(5);
                        break;
                    case 0x38:
                        std::printf("[GPU:GP0   ] Draw Shaded Quad (opaque)\n");

                        cmdParam.push(data); // Also first argument

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

                        xyarea.x0 = (data >>  0) & 0x3FF;
                        xyarea.y0 = (data >> 10) & 0x1FF;
                        break;
                    case 0xE4:
                        std::printf("[GPU:GP0   ] Set Drawing Area (BR)\n");

                        xyarea.x1 = (data >>  0) & 0x3FF;
                        xyarea.y1 = (data >> 10) & 0x1FF;
                        break;
                    case 0xE5:
                        std::printf("[GPU:GP0   ] Set Drawing Offset\n");

                        xyoffset.xofs = ((i32)(((data >>  0) & 0x7FF) << 21) >> 21);
                        xyoffset.yofs = ((i32)(((data >> 11) & 0x7FF) << 21) >> 21);
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
                    case 0x02: fillRect(); break;
                    case 0x30: drawTri30(); break;
                    case 0x38: drawQuad38(); break;
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
