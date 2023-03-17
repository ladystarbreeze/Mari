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
    Vertex(u32 v, u32 c, u32 tex) : x((i32)((v & 0x7FF) << 21) >> 21), y((i32)(((v >> 16) & 0x7FF) << 21) >> 21), c(c & 0xFFFFFF), tex(tex) {}

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

/* Copy info */
struct CopyInfo {
    u32 cx, cy; // Current X/Y

    u32 xMin, yMin;
    u32 xMax, yMax;
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

CopyInfo dstCopyInfo, srcCopyInfo;

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

template<bool conv>
void drawPixel(i32 x, i32 y, u32 c) {
    if constexpr (conv) {
        vram[x + 1024 * y] = toBGR555(c);
    } else {
        vram[x + 1024 * y] = c;
    }
}

i32 edgeFunction(const Vertex &a, const Vertex &b, const Vertex &c) {
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

u16 fetchTex(i32 texX, i32 texY, u32 texPage, u32 clut) {
    static const u32 texDepth[4] = { 4, 8, 16, 0 };

    /* Get tex page coordinates */
    const auto texPageX = texPage & 0xF;
    const auto texPageY = 256 * ((texPage >> 4) & 1);

    const auto depth = texDepth[(texPage >> 7) & 3];

    assert((depth == 4) || (depth == 16));

    u32 x = 0;

    switch (depth) {
        case 4: case 8:
            x = 64 * texPageX + (texX / depth);
            break;
        case 16:
            x = 64 * texPageX + texX;
            break;
        default:
            break;
    }

    const auto y = texPageY + texY;

    const auto texel = vram[x + 1024 * y];

    switch (depth) {
        case 4:
            {
                const auto clutX = 16 * (clut & 0x3F);
                const auto clutY = (clut >> 6) & 0x1FF;
                const auto clutOfs = (texel >> (4 * (texX & 3))) & 0xF;

                return vram[(clutX + clutOfs) + 1024 * clutY];
            }
            break;
        case 16: return texel;
        default:
            assert(false); // Shouldn't happen
    }
}

/* Draws a Gouraud shaded triangle */
void drawShadedTri(const Vertex &v0, const Vertex &v1, const Vertex &v2) {
    Vertex p, a, b, c;

    a = v0;

    /* Ensure the winding order is correct */
    if (edgeFunction(v0, v1, v2) < 0) {
        b = v2;
        c = v1;
    } else {
        b = v1;
        c = v2;
    }

    /* Offset coordinates */
    a.x += xyoffset.xofs;
    b.x += xyoffset.xofs;
    c.x += xyoffset.xofs;
    a.y += xyoffset.yofs;
    b.y += xyoffset.yofs;
    c.y += xyoffset.yofs;

    const auto area = edgeFunction(a, b, c);

    /* Calculate bounding box */
    auto xMin = std::min(a.x, std::min(b.x, c.x));
    auto yMin = std::min(a.y, std::min(b.y, c.y));
    auto xMax = std::max(a.x, std::max(b.x, c.x));
    auto yMax = std::max(a.y, std::max(b.y, c.y));

    xMin = std::max(xMin, xyarea.x0);
    yMin = std::max(yMin, xyarea.y0);
    xMax = std::min(xMax, xyarea.x1);
    yMax = std::min(yMax, xyarea.y1);

    for (p.y = yMin; p.y < yMax; p.y++) {
        for (p.x = xMin; p.x < xMax; p.x++) {
            /* Calculate weights */
            const auto w0 = edgeFunction(b, c, p);
            const auto w1 = edgeFunction(c, a, p);
            const auto w2 = edgeFunction(a, b, p);

            /* Is point inside of triangle ? */
            if ((w0 >= 0) && (w1 >= 0) && (w2 >= 0)) {
                /* Interpolate color */
                const u32 cr = (w0 * ((a.c >>  0) & 0xFF) + w1 * ((b.c >>  0) & 0xFF) + w2 * ((c.c >>  0) & 0xFF)) / area;
                const u32 cg = (w0 * ((a.c >>  8) & 0xFF) + w1 * ((b.c >>  8) & 0xFF) + w2 * ((c.c >>  8) & 0xFF)) / area;
                const u32 cb = (w0 * ((a.c >> 16) & 0xFF) + w1 * ((b.c >> 16) & 0xFF) + w2 * ((c.c >> 16) & 0xFF)) / area;

                const auto color = (cb << 16) | (cg << 8) | cr;

                drawPixel<true>(p.x, p.y, color);
            }
        }
    }
}

/* Draws a textured triangle */
void drawTexturedTri(const Vertex &v0, const Vertex &v1, const Vertex &v2, u32 clut, u32 texPage) {
    Vertex p, a, b, c;

    a = v0;

    /* Ensure the winding order is correct */
    if (edgeFunction(v0, v1, v2) < 0) {
        b = v2;
        c = v1;
    } else {
        b = v1;
        c = v2;
    }

    /* Offset coordinates */
    a.x += xyoffset.xofs;
    b.x += xyoffset.xofs;
    c.x += xyoffset.xofs;
    a.y += xyoffset.yofs;
    b.y += xyoffset.yofs;
    c.y += xyoffset.yofs;

    const auto area = edgeFunction(a, b, c);

    /* Calculate bounding box */
    auto xMin = std::min(a.x, std::min(b.x, c.x));
    auto yMin = std::min(a.y, std::min(b.y, c.y));
    auto xMax = std::max(a.x, std::max(b.x, c.x));
    auto yMax = std::max(a.y, std::max(b.y, c.y));

    xMin = std::max(xMin, xyarea.x0);
    yMin = std::max(yMin, xyarea.y0);
    xMax = std::min(xMax, xyarea.x1);
    yMax = std::min(yMax, xyarea.y1);

    for (p.y = yMin; p.y < yMax; p.y++) {
        for (p.x = xMin; p.x < xMax; p.x++) {
            /* Calculate weights */
            const auto w0 = edgeFunction(b, c, p);
            const auto w1 = edgeFunction(c, a, p);
            const auto w2 = edgeFunction(a, b, p);

            /* Is point inside of triangle ? */
            if ((w0 >= 0) && (w1 >= 0) && (w2 >= 0)) {
                /* Interpolate tex coords */
                const u32 texX = (w0 * ((a.tex >>  0) & 0xFF) + w1 * ((b.tex >>  0) & 0xFF) + w2 * ((c.tex >>  0) & 0xFF)) / area;
                const u32 texY = (w0 * ((a.tex >>  8) & 0xFF) + w1 * ((b.tex >>  8) & 0xFF) + w2 * ((c.tex >>  8) & 0xFF)) / area;

                const auto color = fetchTex(texX, texY, texPage, clut);

                /* TODO: handle semi-transparency/blending */
                if (!color) continue;

                drawPixel<false>(p.x, p.y, color);
            }
        }
    }
}

/* GP0(0x02) Fill Rectangle */
void fillRect() {
    /* Convert 24-bit to 15-bit here to speed up things */
    const auto c = toBGR555(cmdParam.front() & 0xFFFFFF); cmdParam.pop();

    const auto coords = cmdParam.front(); cmdParam.pop();
    const auto dims   = cmdParam.front(); cmdParam.pop();

    auto x0 = 16 * ((coords >>  0) & 0xFFFF); // In 16px units
    auto y0 = 16 * ((coords >> 16) & 0xFFFF); // In 16px units

    /* Offset coordinates */
    x0 += xyoffset.xofs;
    y0 += xyoffset.yofs;

    const auto width  = 16 * ((dims >>  0) & 0xFFFF); // In 16px units
    const auto height = 16 * ((dims >> 16) & 0xFFFF); // In 16px units

    /* Calculate rectangle */
    const auto xMin = std::max((i32)x0, xyarea.x0);
    const auto yMin = std::max((i32)y0, xyarea.y0);
    const auto xMax = std::min((i32)(width  + x0), xyarea.x1);
    const auto yMax = std::min((i32)(height + y0), xyarea.y1);

    for (auto y = yMin; y < yMax; y++)
	{
		for (auto x = xMin; x < xMax; x++)
		{
			drawPixel<false>(x, y, c);
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

/* GP0(0x2C) Draw Textured Quadrilateral (semi-transparent, blended) */
void drawQuad2C() {
    const auto c = cmdParam.front(); cmdParam.pop();

    Vertex v[4];

    for (int i = 0; i < 4; i++) {
        const auto v0 = cmdParam.front(); cmdParam.pop();
        const auto t0 = cmdParam.front(); cmdParam.pop();

        v[i] = Vertex(v0, c, t0);
    }

    const auto clut = v[0].tex >> 16;

    u32 texPage;
    if (edgeFunction(v[0], v[1], v[2]) < 0) {
        texPage = v[2].tex >> 16;
    } else {
        texPage = v[1].tex >> 16;
    }

    drawTexturedTri(v[0], v[1], v[2], clut, texPage);
    drawTexturedTri(v[1], v[2], v[3], clut, texPage);

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
    const auto coords = cmdParam.front(); cmdParam.pop();
    const auto dims   = cmdParam.front(); cmdParam.pop();

    dstCopyInfo.xMin = (coords >>  0) & 0xFFFF;
    dstCopyInfo.yMin = (coords >> 16) & 0xFFFF;

    dstCopyInfo.xMax = ((dims >>  0) & 0xFFFF) + dstCopyInfo.xMin;
    dstCopyInfo.yMax = (dims >> 16) & 0xFFFF;

    argCount = (((dims >> 16) * (dims & 0xFFFF) + 1) & ~1) / 2;

    dstCopyInfo.cx = dstCopyInfo.xMin;
    dstCopyInfo.cy = dstCopyInfo.yMin;

    state = GPUState::CopyRectangle;
}

/* GP0(0xC0) Copy Rectangle (VRAM->CPU) */
void copyVRAMToCPU() {
    const auto coords = cmdParam.front(); cmdParam.pop();
    const auto dims   = cmdParam.front(); cmdParam.pop();

    srcCopyInfo.xMin = (coords >>  0) & 0xFFFF;
    srcCopyInfo.yMin = (coords >> 16) & 0xFFFF;

    srcCopyInfo.xMax = ((dims >>  0) & 0xFFFF) + srcCopyInfo.xMin;
    srcCopyInfo.yMax = (dims >> 16) & 0xFFFF;

    argCount = (((dims >> 16) * (dims & 0xFFFF) + 1) & ~1) / 2;

    srcCopyInfo.cx = srcCopyInfo.xMin;
    srcCopyInfo.cy = srcCopyInfo.yMin;

    // state = GPUState::CopyRectangle;
}

void init() {
    idScanline = scheduler::registerEvent([](int, i64 c) { scanlineEvent(c); });

    vram.resize(VRAM_WIDTH * VRAM_HEIGHT);

    scheduler::addEvent(idScanline, 0, CYCLES_PER_SCANLINE, true);
}

u32 readGPUREAD() {
    u32 data;

    auto &c = srcCopyInfo;

    data = vram[c.cx + 1024 * c.cy];

    c.cx++;

    if (c.cx == c.xMax) {
        c.cy++;
        
        c.cx = c.xMin;
    }

    data |= (u32)vram[c.cx + 1024 * c.cy] << 16;

    c.cx++;

    if (c.cx == c.xMax) {
        c.cy++;
        
        c.cx = c.xMin;
    }

    if (!--argCount) state = GPUState::ReceiveCommand;

    return data;
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

                        cmdParam.push(data); // Also first argument

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
                    case 0x2C: drawQuad2C(); break;
                    case 0x30: drawTri30(); break;
                    case 0x38: drawQuad38(); break;
                    case 0xA0: copyCPUToVRAM(); break;
                    case 0xC0: copyVRAMToCPU(); break;
                    default:
                        while (cmdParam.size()) cmdParam.pop();

                        state = GPUState::ReceiveCommand;
                }
            }
            break;
        case GPUState::CopyRectangle:
            {
                std::printf("[GPU:GP0   ] CPU->VRAM write = 0x%08X\n", data);

                auto &c = dstCopyInfo;

                vram[c.cx + 1024 * c.cy] = data;

                c.cx++;

                if (c.cx >= c.xMax) {
                    c.cy++;

                    c.cx = c.xMin;
                }

                vram[c.cx + 1024 * c.cy] = data >> 16;

                c.cx++;

                if (c.cx >= c.xMax) {
                    c.cy++;

                    c.cx = c.xMin;
                }

                if (!--argCount) state = GPUState::ReceiveCommand;
            }
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