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
#include "../timer/timer.hpp"

namespace ps::gpu {

using Interrupt = intc::Interrupt;

/* --- GPU constants --- */

constexpr i64 CYCLES_PER_HDRAW    = 2560;
constexpr i64 CYCLES_PER_SCANLINE = 3413; // NTSC
constexpr i64 SCANLINES_PER_VDRAW = 240;
constexpr i64 SCANLINES_PER_FRAME = 262;

constexpr size_t VRAM_WIDTH  = 1024;
constexpr size_t VRAM_HEIGHT = 512;

/* 2D vertex */
struct Vertex {
    Vertex() : x(0), y(0), c(0), tex(0) {}
    Vertex(u32 v) : x((i32)((v & 0x7FF) << 21) >> 21), y((i32)(((v >> 16) & 0x7FF) << 21) >> 21), c(0), tex(0) {}
    Vertex(u32 v, u32 c) : x((i32)((v & 0x7FF) << 21) >> 21), y((i32)(((v >> 16) & 0x7FF) << 21) >> 21), c(c & 0xFFFFFF), tex(0) {}
    Vertex(u32 v, u32 c, u32 tex) : x((i32)((v & 0x7FF) << 21) >> 21), y((i32)(((v >> 16) & 0x7FF) << 21) >> 21), c(c & 0xFFFFFF), tex(tex) {}

    i32 x, y; // Coordinates

    u32 c; // Color
    u32 tex; // Tex coord
};

/* Texture window */
struct TexWindow {
    u32 maskX, maskY;
    u32 ofsX, ofsY;
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
XYArea    xyarea;
XYOffset  xyoffset;
TexWindow texWindow;

CopyInfo dstCopyInfo, srcCopyInfo;

i64 lineCounter = 0;

u32 drawMode;

u32 gpuread = 0;
u32 gpustat = 7 << 26;

u64 idHBLANK, idScanline; // Scheduler

/* Handles HBLANK events */
void hblankEvent(i64 c) {
    timer::stepHBLANK();

    scheduler::addEvent(idHBLANK, 0, CYCLES_PER_SCANLINE + c, false);
}

/* Handles scanline events */
void scanlineEvent(i64 c) {
    ++lineCounter;

    if (lineCounter == SCANLINES_PER_VDRAW) {
        intc::sendInterrupt(Interrupt::VBLANK);

        timer::gateVBLANKStart();

        update((u8 *)vram.data());
    } else if (lineCounter == SCANLINES_PER_FRAME) {
        timer::gateVBLANKEnd();

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

    /* Apply tex window */
    texX = (texX & ~texWindow.maskX) | (texWindow.ofsX & texWindow.maskX);
    texY = (texY & ~texWindow.maskY) | (texWindow.ofsY & texWindow.maskY);

    /* Get tex page coordinates */
    const auto texPageX = texPage & 0xF;
    const auto texPageY = 256 * ((texPage >> 4) & 1);

    const auto depth = texDepth[(texPage >> 7) & 3];

    u32 x = 0;

    switch (depth) {
        case 4:
            x = 64 * texPageX + (texX / 4);
            break;
        case 8:
            x = 64 * texPageX + (texX / 2);
            break;
        case 16:
            x = 64 * texPageX + texX;
            break;
        default:
            break;
    }

    const auto y = texPageY + texY;

    const auto texel = vram[x + 1024 * y];

    if (depth == 16) return texel;

    const auto clutX = 16 * (clut & 0x3F);
    const auto clutY = (clut >> 6) & 0x1FF;

    const auto clutOfs = (depth == 4) ? (texel >> (4 * (texX & 3))) & 0xF : (texel >> (8 * (texX & 1))) & 0xFF;

    return vram[(clutX + clutOfs) + 1024 * clutY];
}

/* Draws a flat shaded triangle */
void drawFlatTri(const Vertex &v0, const Vertex &v1, const Vertex &v2, u32 color) {
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

    /* Calculate bounding box */
    auto xMin = std::min(a.x, std::min(b.x, c.x));
    auto yMin = std::min(a.y, std::min(b.y, c.y));
    auto xMax = std::max(a.x, std::max(b.x, c.x));
    auto yMax = std::max(a.y, std::max(b.y, c.y));

    xMin = std::max(xMin, xyarea.x0);
    yMin = std::max(yMin, xyarea.y0);
    xMax = std::min(xMax, xyarea.x1);
    yMax = std::min(yMax, xyarea.y1);

    color = toBGR555(color);

    for (p.y = yMin; p.y < yMax; p.y++) {
        for (p.x = xMin; p.x < xMax; p.x++) {
            /* Calculate weights */
            const auto w0 = edgeFunction(b, c, p);
            const auto w1 = edgeFunction(c, a, p);
            const auto w2 = edgeFunction(a, b, p);

            /* Is point inside of triangle ? */
            if ((w0 >= 0) && (w1 >= 0) && (w2 >= 0)) drawPixel<false>(p.x, p.y, color);
        }
    }
}

/* Draws a flat rectangle */
void drawFlatRect(const Vertex &v, i32 w, i32 h, u32 color) {
    auto a = v;

    /* Offset coordinates */
    a.x += xyoffset.xofs;
    a.y += xyoffset.yofs;

    /* Calculate bounding box */
    auto xMin = std::max(a.x, xyarea.x0);
    auto yMin = std::max(a.y, xyarea.y0);
    auto xMax = std::min(xMin + w, xyarea.x1);
    auto yMax = std::min(yMin + h, xyarea.y1);

    for (auto y = yMin; y < yMax; y++) {
        for (auto x = xMin; x < xMax; x++) {
            drawPixel<false>(x, y, color);
        }
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

/* Draws a textured rectangle */
void drawTexturedRect(const Vertex &v, i32 w, i32 h, u32 clut) {
    auto a = v;

    /* Offset coordinates */
    a.x += xyoffset.xofs;
    a.y += xyoffset.yofs;

    /* Calculate bounding box */
    auto xMin = std::max(a.x, xyarea.x0);
    auto yMin = std::max(a.y, xyarea.y0);
    auto xMax = std::min(xMin + w, xyarea.x1);
    auto yMax = std::min(yMin + h, xyarea.y1);

    const u32 texX = (a.tex >> 0) & 0xFF;
    const u32 texY = (a.tex >> 8) & 0xFF;

    u32 xc = 0, yc = 0;

    for (auto y = yMin; y < yMax; y++) {
        for (auto x = xMin; x < xMax; x++) {
            const auto color = fetchTex(texX + xc, texY + yc, drawMode, clut);

            ++xc;

            /* TODO: handle semi-transparency/blending */
            if (!color) continue;

            drawPixel<false>(x, y, color);
        }

        xc = 0;

        ++yc;
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

/* GP0(0x20) Draw Flat Tri (opaque) */
void drawTri20() {
    const auto color = cmdParam.front(); cmdParam.pop();

    const auto v0 = cmdParam.front(); cmdParam.pop();
    const auto v1 = cmdParam.front(); cmdParam.pop();
    const auto v2 = cmdParam.front(); cmdParam.pop();

    drawFlatTri(Vertex(v0), Vertex(v1), Vertex(v2), color);

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

/* GP0(0x28) Draw Flat Quadrilateral (opaque) */
void drawQuad28() {
    const auto color = cmdParam.front(); cmdParam.pop();

    const auto v0 = cmdParam.front(); cmdParam.pop();
    const auto v1 = cmdParam.front(); cmdParam.pop();
    const auto v2 = cmdParam.front(); cmdParam.pop();
    const auto v3 = cmdParam.front(); cmdParam.pop();

    drawFlatTri(Vertex(v0), Vertex(v1), Vertex(v2), color);
    drawFlatTri(Vertex(v1), Vertex(v2), Vertex(v3), color);

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

/* GP0(0x3E) Draw Shaded Textured Quadrilateral */
void drawQuad3E() {
    Vertex v[4];

    for (int i = 0; i < 4; i++) {
        const auto c0 = cmdParam.front(); cmdParam.pop();
        const auto v0 = cmdParam.front(); cmdParam.pop();
        const auto t0 = cmdParam.front(); cmdParam.pop();

        v[i] = Vertex(v0, c0, t0);
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

/* GP0(0x60) Draw Flat Rectangle (variable) */
void drawRect60() {
    const auto c = cmdParam.front(); cmdParam.pop();
    const auto v = cmdParam.front(); cmdParam.pop();

    const auto dims = cmdParam.front(); cmdParam.pop();

    Vertex v0 = Vertex(v, c);

    drawFlatRect(v0, dims & 0xFFFF, dims >> 16, c);

    state = GPUState::ReceiveCommand;
}

/* GP0(0x65) Draw Textured Rectangle (variable, opaque) */
void drawRect65() {
    const auto c = cmdParam.front(); cmdParam.pop();
    const auto v = cmdParam.front(); cmdParam.pop();
    const auto t = cmdParam.front(); cmdParam.pop();

    const auto dims = cmdParam.front(); cmdParam.pop();

    Vertex v0 = Vertex(v, c, t);

    const auto clut = v0.tex >> 16;

    drawTexturedRect(v0, dims & 0xFFFF, dims >> 16, clut);

    state = GPUState::ReceiveCommand;
}

/* GP0(0x74) Draw Textured Rectangle (8x8, opaque) */
void drawRect74() {
    const auto c = cmdParam.front(); cmdParam.pop();
    const auto v = cmdParam.front(); cmdParam.pop();
    const auto t = cmdParam.front(); cmdParam.pop();

    Vertex v0 = Vertex(v, c, t);

    const auto clut = v0.tex >> 16;

    drawTexturedRect(v0, 8, 8, clut);

    state = GPUState::ReceiveCommand;
}

/* GP0(0x78) Draw Flat Rectangle (8x8) */
void drawRect78() {
    const auto c = cmdParam.front(); cmdParam.pop();
    const auto v = cmdParam.front(); cmdParam.pop();

    Vertex v0 = Vertex(v, c);

    drawFlatRect(v0, 8, 8, c);

    state = GPUState::ReceiveCommand;
}

/* GP0(0x7C) Draw Textured Rectangle (16x16, opaque) */
void drawRect7C() {
    const auto c = cmdParam.front(); cmdParam.pop();
    const auto v = cmdParam.front(); cmdParam.pop();
    const auto t = cmdParam.front(); cmdParam.pop();

    Vertex v0 = Vertex(v, c, t);

    const auto clut = v0.tex >> 16;

    drawTexturedRect(v0, 16, 16, clut);

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

    state = GPUState::CopyRectangle;
}

/* GP0(0x80) Copy Rectangle (VRAM->VRAM) */
void copyVRAMToVRAM() {
    const auto srcCoord = cmdParam.front(); cmdParam.pop();
    const auto dstCoord = cmdParam.front(); cmdParam.pop();
    const auto dims     = cmdParam.front(); cmdParam.pop();

    /* Set up transfer */

    dstCopyInfo.xMin = (dstCoord >>  0) & 0xFFFF;
    dstCopyInfo.yMin = (dstCoord >> 16) & 0xFFFF;

    dstCopyInfo.cx = dstCopyInfo.xMin;
    dstCopyInfo.cy = dstCopyInfo.yMin;

    srcCopyInfo.xMin = (srcCoord >>  0) & 0xFFFF;
    srcCopyInfo.yMin = (srcCoord >> 16) & 0xFFFF;

    srcCopyInfo.xMax = ((dims >>  0) & 0xFFFF) + srcCopyInfo.xMin;
    srcCopyInfo.yMax = (dims >> 16) & 0xFFFF;

    srcCopyInfo.cx = srcCopyInfo.xMin;
    srcCopyInfo.cy = srcCopyInfo.yMin;

    /* Copy data */
    
    while (true) {
        vram[dstCopyInfo.cx + 1024 * dstCopyInfo.cy] = vram[srcCopyInfo.cx + 1024 * srcCopyInfo.cy];

        srcCopyInfo.cx++;
        dstCopyInfo.cx++;

        if (srcCopyInfo.cx >= srcCopyInfo.xMax) {
            srcCopyInfo.cy++;
            dstCopyInfo.cy++;

            if (srcCopyInfo.cy >= srcCopyInfo.yMax) break;

            srcCopyInfo.cx = srcCopyInfo.xMin;
            dstCopyInfo.cx = dstCopyInfo.xMin;
        }

        vram[dstCopyInfo.cx + 1024 * dstCopyInfo.cy] = vram[srcCopyInfo.cx + 1024 * srcCopyInfo.cy];

        srcCopyInfo.cx++;
        dstCopyInfo.cx++;

        if (srcCopyInfo.cx >= srcCopyInfo.xMax) {
            srcCopyInfo.cy++;
            dstCopyInfo.cy++;

            if (srcCopyInfo.cy >= srcCopyInfo.yMax) break;

            srcCopyInfo.cx = srcCopyInfo.xMin;
            dstCopyInfo.cx = dstCopyInfo.xMin;
        }
    }

    state = GPUState::ReceiveCommand;
}

void init() {
    idHBLANK   = scheduler::registerEvent([](int, i64 c) { hblankEvent(c); });
    idScanline = scheduler::registerEvent([](int, i64 c) { scanlineEvent(c); });

    vram.resize(VRAM_WIDTH * VRAM_HEIGHT);

    scheduler::addEvent(idHBLANK, 0, CYCLES_PER_HDRAW, false);
    scheduler::addEvent(idScanline, 0, CYCLES_PER_SCANLINE, true);
}

u32 readGPUREAD() {
    u32 data;

    if (state != GPUState::CopyRectangle) {
        data = gpuread;

        gpuread = 0;

        return data;
    }

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
                    case 0x00:
                        std::printf("[GPU:GP0   ] NOP\n");
                        break;
                    case 0x01:
                        std::printf("[GPU:GP0   ] Clear Cache\n");
                        break;
                    case 0x02:
                        std::printf("[GPU:GP0   ] Fill VRAM\n");

                        cmdParam.push(data); // Also first argument

                        setArgCount(2);
                        break;
                    case 0x1F:
                        std::printf("[GPU:GP0   ] Request Interrupt (0x%08X)\n", data);

                        gpustat |= 1 << 24;

                        intc::sendInterrupt(Interrupt::GPU);
                        break;
                    case 0x20:
                    case 0x22:
                        std::printf("[GPU:GP0   ] Draw Flat Tri (opaque)\n");

                        cmdParam.push(data); // Also first argument

                        setArgCount(3);
                        break;
                    case 0x28:
                    case 0x2A:
                    case 0x2B:
                        std::printf("[GPU:GP0   ] Draw Flat Quad (opaque)\n");

                        cmdParam.push(data); // Also first argument

                        setArgCount(4);
                        break;
                    case 0x2C:
                    case 0x2D:
                    case 0x2E:
                    case 0x2F:
                        std::printf("[GPU:GP0   ] Draw Textured Quad (semi-transparent, blended)\n");

                        cmdParam.push(data); // Also first argument

                        setArgCount(8);
                        break;
                    case 0x30:
                    case 0x32:
                        std::printf("[GPU:GP0   ] Draw Shaded Tri (opaque)\n");

                        cmdParam.push(data); // Also first argument

                        setArgCount(5);
                        break;
                    case 0x38:
                    case 0x3A:
                        std::printf("[GPU:GP0   ] Draw Shaded Quad (opaque)\n");

                        cmdParam.push(data); // Also first argument

                        setArgCount(7);
                        break;
                    case 0x3C:
                    case 0x3E:
                        std::printf("[GPU:GP0   ] Draw Shaded Textured Quad (opaque)\n");

                        cmdParam.push(data); // Also first argument

                        setArgCount(11);
                        break;
                    case 0x40:
                    case 0x42:
                        setArgCount(2);
                        break;
                    case 0x60:
                    case 0x62:
                        std::printf("[GPU:GP0   ] Draw Flat Rectangle (variable)\n");

                        cmdParam.push(data); // Also first argument

                        setArgCount(2);
                        break;
                    case 0x64:
                    case 0x65:
                    case 0x66:
                        std::printf("[GPU:GP0   ] Draw Textured Rectangle (variable, opaque)\n");

                        cmdParam.push(data); // Also first argument

                        setArgCount(3);
                        break;
                    case 0x78:
                        std::printf("[GPU:GP0   ] Draw Flat Rectangle (8x8)\n");

                        cmdParam.push(data); // Also first argument

                        setArgCount(1);
                        break;
                    case 0x74:
                        std::printf("[GPU:GP0   ] Draw Textured Rectangle (8x8, opaque)\n");

                        cmdParam.push(data); // Also first argument

                        setArgCount(2);
                        break;
                    case 0x7C:
                    case 0x7D:
                        std::printf("[GPU:GP0   ] Draw Textured Rectangle (16x16)\n");

                        cmdParam.push(data); // Also first argument

                        setArgCount(2);
                        break;
                    case 0x80:
                        std::printf("[GPU:GP0   ] Copy Rectangle (VRAM->VRAM)\n");

                        setArgCount(3);
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

                        drawMode = data & 0xFFFFFF;
                        break;
                    case 0xE2:
                        std::printf("[GPU:GP0   ] Set Texture Window\n");

                        texWindow.maskX = 8 * ((data >>  0) & 0x1F);
                        texWindow.maskY = 8 * ((data >>  5) & 0x1F);
                        texWindow.ofsX  = 8 * ((data >> 10) & 0x1F);
                        texWindow.ofsY  = 8 * ((data >> 15) & 0x1F);
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
                    case 0xE6:
                        std::printf("[GPU:GP0   ] Set Mask Bit\n");
                        break;
                    case 0xFF: // ???
                        std::printf("[GPU:GP0   ] Invalid command 0x%02X (0x%08X)\n", cmd, data);
                        break;
                    default:
                        std::printf("[GPU       ] Unhandled GP0 command 0x%02X (0x%08X)\n", cmd, data);

                        exit(0);
                }
            }
            break;
        case GPUState::ReceiveArguments:
            //std::printf("[GPU:GP0   ] 0x%08X\n", data);

            cmdParam.push(data);

            if (!--argCount) {
                switch (cmd) {
                    case 0x02: fillRect(); break;
                    case 0x20:
                    case 0x22:
                        drawTri20();
                        break;
                    case 0x28:
                    case 0x2A:
                    case 0x2B:
                        drawQuad28();
                        break;
                    case 0x2C:
                    case 0x2D:
                    case 0x2E:
                    case 0x2F:
                        drawQuad2C();
                        break;
                    case 0x30:
                    case 0x32:
                        drawTri30();
                        break;
                    case 0x38:
                    case 0x3A:
                        drawQuad38();
                        break;
                    case 0x3C:
                    case 0x3E:
                        drawQuad3E();
                        break;
                    case 0x60:
                    case 0x62:
                        drawRect60();
                        break;
                    case 0x64:
                    case 0x65:
                    case 0x66:
                        drawRect65();
                        break;
                    case 0x74: drawRect74(); break;
                    case 0x78: drawRect78(); break;
                    case 0x7C:
                    case 0x7D:
                        drawRect7C();
                        break;
                    case 0x80: copyVRAMToVRAM(); break;
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
                auto &c = dstCopyInfo;

                //std::printf("[GPU:GP0   ] [0x%08X] = 0x%04X\n", c.cx + 1024 * c.cy, data & 0xFFFF);

                vram[c.cx + 1024 * c.cy] = data;

                c.cx++;

                if (c.cx >= c.xMax) {
                    c.cy++;

                    c.cx = c.xMin;
                }

                //std::printf("[GPU:GP0   ] [0x%08X] = 0x%04X\n", c.cx + 1024 * c.cy, data >> 16);

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

u32 readStatus() {
    gpustat ^= 1 << 31;

    return gpustat;
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
        case 0x10:
            std::printf("[GPU:GP1   ] Get GPU Info\n");

            switch (data & 7) {
                case 2: // Texture Window
                    gpuread = ((texWindow.ofsY / 8) << 15) | ((texWindow.ofsX / 8) << 10) | ((texWindow.maskY / 8) << 5) | (texWindow.ofsX / 8);
                    break;
                case 3:
                    gpuread = (xyarea.y0 << 10) | xyarea.x0;
                    break;
                case 4:
                    gpuread = (xyarea.y1 << 10) | xyarea.x1;
                    break;
                case 5: // Drawing Offset
                    break;
                default:
                    break;
            }
            break;
        default:
            std::printf("[GPU       ] Unhandled GP1 command 0x%02X (0x%08X)\n", cmd, data);
            
            exit(0);
    }
}

}
