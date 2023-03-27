/*
 * Mari is a PlayStation emulator.
 * Copyright (C) 2023  Lady Starbreeze (Michelle-Marie Schiller)
 */

#include "gte.hpp"

#include <bit>
#include <cassert>
#include <cstdio>

namespace ps::cpu::gte {

typedef i16 Matrix[3][3];
typedef i16 Vec16[3];
typedef u32 Vec32[3];

/* --- GTE constants --- */

constexpr auto X = 0, Y = 1, Z = 2;
constexpr auto R = 0, G = 1, B = 2;

/* --- GTE instructions --- */

enum Opcode {
    RTPT = 0x30,
};

/* --- GTE registers --- */

enum GTEReg {
    VX0 = 0x00, VY0 = 0x01, VZ0 = 0x02,
    VX1 = 0x03, VY1 = 0x04, VZ1 = 0x05,
    VX2 = 0x06, VY2 = 0x07, VZ2 = 0x08,
};

enum ControlReg {
    RT11RT12 = 0x00, RT13RT21 = 0x01, RT22RT23 = 0x02, RT31RT32 = 0x03, RT33 = 0x04,
    TRX      = 0x05, TRY      = 0x06, TRZ      = 0x07,
    L11L12   = 0x08, L13L21   = 0x09, L22L23   = 0x0A, L31L32   = 0x0B, L33  = 0x0C,
    RBK      = 0x0D, GBK      = 0x0E, BBK      = 0x0F,
    LR1LR2   = 0x10, LR3LG1   = 0x11, LG2LG3   = 0x12, LB1LB2   = 0x13, LB3  = 0x14,
    RFC      = 0x15, GFC      = 0x16, BFC      = 0x17,
    OFX      = 0x18, OFY      = 0x19,
    H        = 0x1A,
    DCA      = 0x1B, DCB      = 0x1C,
    ZSF3     = 0x1D, ZSF4     = 0x1E,
    FLAG     = 0x1F,
};

/* Light color matrix */
enum LCM {
    LR, LB, LG,
};

/* --- GTE registers --- */

Vec16 v[3];   // Vectors 0-2
i16   ir[4];  // 16-bit Accumulators
i32   mac[4]; // Accumulators

/* --- GTE FIFOs --- */

u32 sxy[3]; // Screen X/Y (three entries)
u16 sz[4];  // Screen Z (four entries)

/* --- GTE control registers --- */

Matrix rt;            // Rotation matrix
Vec32  tr;            // Translation vector X/Y/Z
Matrix ls;            // Light source matrix
Vec32  bk;            // Background color R/G/B
Matrix lc;            // Light color matrix
Vec32  fc;            // Far color R/G/B
u32    ofx, ofy;      // Screen offset X/Y
u16    h;             // Projection plane distance
i16    dca;           // Depth cueing parameter A
u32    dcb;           // Depth cueing parameter B
i16    zsf3, zsf4;    // Z scale factors

u32 get(u32 idx) {
    std::printf("[GTE       ] Unhandled read @ %u\n", idx);

    exit(0);
}

u32 getControl(u32 idx) {
    switch (idx) {
        case ControlReg::FLAG:
            //std::printf("[GTE       ] Control read @ FLAG\n");
            return 0;
        default:
            std::printf("[GTE       ] Unhandled control read @ %u\n", idx);

            exit(0);
    }
}

void set(u32 idx, u32 data) {
    switch (idx) {
        case GTEReg::VX0:
            //std::printf("[GTE       ] Write @ VX0 = 0x%08X\n", data);

            v[0][X] = data;
            break;
        case GTEReg::VY0:
            //std::printf("[GTE       ] Write @ VY0 = 0x%08X\n", data);

            v[0][Y] = data;
            break;
        case GTEReg::VZ0:
            //std::printf("[GTE       ] Write @ VZ0 = 0x%08X\n", data);

            v[0][Z] = data;
            break;
        case GTEReg::VX1:
            //std::printf("[GTE       ] Write @ VX1 = 0x%08X\n", data);

            v[1][X] = data;
            break;
        case GTEReg::VY1:
            //std::printf("[GTE       ] Write @ VY1 = 0x%08X\n", data);

            v[1][Y] = data;
            break;
        case GTEReg::VZ1:
            //std::printf("[GTE       ] Write @ VZ1 = 0x%08X\n", data);

            v[1][Z] = data;
            break;
        case GTEReg::VX2:
            //std::printf("[GTE       ] Write @ VX2 = 0x%08X\n", data);

            v[2][X] = data;
            break;
        case GTEReg::VY2:
            //std::printf("[GTE       ] Write @ VY2 = 0x%08X\n", data);

            v[2][Y] = data;
            break;
        case GTEReg::VZ2:
            //std::printf("[GTE       ] Write @ VZ2 = 0x%08X\n", data);

            v[2][Z] = data;
            break;
        default:
            std::printf("[GTE       ] Unhandled write @ %u = 0x%08X\n", idx, data);

            exit(0);
    }
}

void setControl(u32 idx, u32 data) {
    switch (idx) {
        case ControlReg::RT11RT12:
            //std::printf("[GTE       ] Control write @ RT11RT12 = 0x%08X\n", data);

            rt[0][0] = data;
            rt[0][1] = data >> 16;
            break;
        case ControlReg::RT13RT21:
            //std::printf("[GTE       ] Control write @ RT13RT21 = 0x%08X\n", data);

            rt[0][2] = data;
            rt[1][0] = data >> 16;
            break;
        case ControlReg::RT22RT23:
            //std::printf("[GTE       ] Control write @ RT22RT23 = 0x%08X\n", data);

            rt[1][1] = data;
            rt[1][2] = data >> 16;
            break;
        case ControlReg::RT31RT32:
            //std::printf("[GTE       ] Control write @ RT31RT32 = 0x%08X\n", data);

            rt[2][0] = data;
            rt[2][1] = data >> 16;
            break;
        case ControlReg::RT33:
            //std::printf("[GTE       ] Control write @ RT33 = 0x%08X\n", data);

            rt[2][2] = data;
            break;
        case ControlReg::TRX:
            //std::printf("[GTE       ] Control write @ TRX = 0x%08X\n", data);

            tr[X] = data;
            break;
        case ControlReg::TRY:
            //std::printf("[GTE       ] Control write @ TRY = 0x%08X\n", data);

            tr[Y] = data;
            break;
        case ControlReg::TRZ:
            //std::printf("[GTE       ] Control write @ TRZ = 0x%08X\n", data);

            tr[Z] = data;
            break;
        case ControlReg::L11L12:
            //std::printf("[GTE       ] Control write @ L11L12 = 0x%08X\n", data);

            ls[0][0] = data;
            ls[0][1] = data >> 16;
            break;
        case ControlReg::L13L21:
            //std::printf("[GTE       ] Control write @ L13L21 = 0x%08X\n", data);

            ls[0][2] = data;
            ls[1][0] = data >> 16;
            break;
        case ControlReg::L22L23:
            //std::printf("[GTE       ] Control write @ L22L23 = 0x%08X\n", data);

            ls[1][1] = data;
            ls[1][2] = data >> 16;
            break;
        case ControlReg::L31L32:
            //std::printf("[GTE       ] Control write @ L31L32 = 0x%08X\n", data);

            ls[2][0] = data;
            ls[2][1] = data >> 16;
            break;
        case ControlReg::L33:
            //std::printf("[GTE       ] Control write @ L33 = 0x%08X\n", data);

            ls[2][2] = data;
            break;
        case ControlReg::RBK:
            //std::printf("[GTE       ] Control write @ RBK = 0x%08X\n", data);

            bk[R] = data;
            break;
        case ControlReg::GBK:
            //std::printf("[GTE       ] Control write @ GBK = 0x%08X\n", data);

            bk[G] = data;
            break;
        case ControlReg::BBK:
            //std::printf("[GTE       ] Control write @ BBK = 0x%08X\n", data);

            bk[B] = data;
            break;
        case ControlReg::LR1LR2:
            //std::printf("[GTE       ] Control write @ LR1LR2 = 0x%08X\n", data);

            lc[LCM::LR][0] = data;
            lc[LCM::LR][1] = data >> 16;
            break;
        case ControlReg::LR3LG1:
            //std::printf("[GTE       ] Control write @ LR3LG1 = 0x%08X\n", data);

            lc[LCM::LR][2] = data;
            lc[LCM::LG][0] = data >> 16;
            break;
        case ControlReg::LG2LG3:
            //std::printf("[GTE       ] Control write @ LG2LG3 = 0x%08X\n", data);

            lc[LCM::LG][1] = data;
            lc[LCM::LG][2] = data >> 16;
            break;
        case ControlReg::LB1LB2:
            //std::printf("[GTE       ] Control write @ LB1LB2 = 0x%08X\n", data);

            lc[LCM::LB][0] = data;
            lc[LCM::LB][1] = data >> 16;
            break;
        case ControlReg::LB3:
            //std::printf("[GTE       ] Control write @ LB3 = 0x%08X\n", data);

            lc[LCM::LB][2] = data;
            break;
        case ControlReg::RFC:
            //std::printf("[GTE       ] Control write @ RFC = 0x%08X\n", data);

            fc[R] = data;
            break;
        case ControlReg::GFC:
            //std::printf("[GTE       ] Control write @ GFC = 0x%08X\n", data);

            fc[G] = data;
            break;
        case ControlReg::BFC:
            //std::printf("[GTE       ] Control write @ BFC = 0x%08X\n", data);

            fc[B] = data;
            break;
        case ControlReg::OFX:
            //std::printf("[GTE       ] Control write @ OFX = 0x%08X\n", data);

            ofx = data;
            break;
        case ControlReg::OFY:
            //std::printf("[GTE       ] Control write @ OFY = 0x%08X\n", data);

            ofy = data;
            break;
        case ControlReg::H:
            //std::printf("[GTE       ] Control write @ H = 0x%08X\n", data);

            h = data;
            break;
        case ControlReg::DCA:
            //std::printf("[GTE       ] Control write @ DCA = 0x%08X\n", data);

            dca = data;
            break;
        case ControlReg::DCB:
            //std::printf("[GTE       ] Control write @ DCB = 0x%08X\n", data);

            dcb = data;
            break;
        case ControlReg::ZSF3:
            //std::printf("[GTE       ] Control write @ ZSF3 = 0x%08X\n", data);

            zsf3 = data;
            break;
        case ControlReg::ZSF4:
            //std::printf("[GTE       ] Control write @ ZSF4 = 0x%08X\n", data);

            zsf4 = data;
            break;
        default:
            std::printf("[GTE       ] Unhandled control write @ %u = 0x%08X\n", idx, data);

            exit(0);
    }
}

int countLeadingBits(u16 a) {
    if (a & (1 << 31)) {
        return std::__countl_one(a);
    }

    return std::__countl_zero(a);
}

/* GTE division (unsigned Newton-Raphson) */
u32 div(u32 a, u32 b) {
    static const u8 unrTable[] = {
		0xFF, 0xFD, 0xFB, 0xF9, 0xF7, 0xF5, 0xF3, 0xF1, 0xEF, 0xEE, 0xEC, 0xEA, 0xE8, 0xE6, 0xE4, 0xE3,
		0xE1, 0xDF, 0xDD, 0xDC, 0xDA, 0xD8, 0xD6, 0xD5, 0xD3, 0xD1, 0xD0, 0xCE, 0xCD, 0xCB, 0xC9, 0xC8,
		0xC6, 0xC5, 0xC3, 0xC1, 0xC0, 0xBE, 0xBD, 0xBB, 0xBA, 0xB8, 0xB7, 0xB5, 0xB4, 0xB2, 0xB1, 0xB0,
		0xAE, 0xAD, 0xAB, 0xAA, 0xA9, 0xA7, 0xA6, 0xA4, 0xA3, 0xA2, 0xA0, 0x9F, 0x9E, 0x9C, 0x9B, 0x9A,
		0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90, 0x8F, 0x8D, 0x8C, 0x8B, 0x8A, 0x89, 0x87, 0x86,
		0x85, 0x84, 0x83, 0x82, 0x81, 0x7F, 0x7E, 0x7D, 0x7C, 0x7B, 0x7A, 0x79, 0x78, 0x77, 0x75, 0x74,
		0x73, 0x72, 0x71, 0x70, 0x6F, 0x6E, 0x6D, 0x6C, 0x6B, 0x6A, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64,
		0x63, 0x62, 0x61, 0x60, 0x5F, 0x5E, 0x5D, 0x5D, 0x5C, 0x5B, 0x5A, 0x59, 0x58, 0x57, 0x56, 0x55,
		0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4F, 0x4E, 0x4D, 0x4D, 0x4C, 0x4B, 0x4A, 0x49, 0x48, 0x48,
		0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3F, 0x3F, 0x3E, 0x3D, 0x3C, 0x3C, 0x3B,
		0x3A, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35, 0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2F,
		0x2E, 0x2E, 0x2D, 0x2C, 0x2C, 0x2B, 0x2A, 0x2A, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24,
		0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1F, 0x1E, 0x1E, 0x1D, 0x1D, 0x1C, 0x1B, 0x1B, 0x1A,
		0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11,
		0x10, 0x0F, 0x0F, 0x0E, 0x0E, 0x0D, 0x0D, 0x0C, 0x0C, 0x0B, 0x0A, 0x0A, 0x09, 0x09, 0x08, 0x08,
		0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00,
		0x00,
	};

    if ((2 * b) <= a) {
        /* TODO: set overflow flags */

        return 0x1FFFF;
    }

    const auto shift = std::__countl_zero(b);

    a <<= shift;
    b <<= shift;

    const auto u = 0x101 + unrTable[(b - 0x7FC0) >> 7];

    b |= 0x8000;

    auto d = ((b * -u) + 0x80) >> 8;

    d = ((u * (0x20000 + d)) + 0x80) >> 8;

    const auto n = (a * d + 0x8000) >> 16;

    if (n > 0x1FFFF) return 0x1FFFF;

    return n;
}

/* --- GTE FIFO handlers --- */

/* Pushes screen X and Y values, performs clipping checks */
void pushSXY(i64 x, i64 y) {
    if (x > 1023) {
        x = 1023;
    } else if (x < -1024) {
        x = -1024;
    }

    if (y > 1023) {
        y = 1023;
    } else if (y < -1024) {
        y = -1024;
    }

    /* Advance FIFO stages */
    for (int i = 0; i < 2; i++) sxy[i] = sxy[i + 1];

    sxy[2] = ((u32)(u16)y << 16) | (u32)(u16)x;
}

/* Pushes a screen Z value, performs clipping checks */
void pushSZ(i64 data) {
    if (data < 0) {
        data = 0;
    } else if (data > 0xFFFF) {
        data = 0xFFFF;
    }

    /* Advance FIFO stages */
    for (int i = 0; i < 3; i++) sz[i] = sz[i + 1];

    sz[3] = data;
}

/* --- MAC/IR handlers --- */

/* Sets IR, performs clipping checks */
void setIR(u32 idx, i64 data, bool lm) {
    static const i64 IR_MIN[] = {      0, -0x8000, -0x8000, -0x8000 };
    static const i64 IR_MAX[] = { 0x1000,  0x7FFF,  0x7FFF,  0x7FFF };

    const auto irMin = (lm) ? 0 : IR_MIN[idx];

    /* Check for clipping */
    if (data > IR_MAX[idx]) {
        data = IR_MAX[idx];

        /* TODO: set IR overflow flags */
    } else if (data < irMin) {
        data = irMin;

        /* TODO: set IR overflow flags */
    }

    ir[idx] = data;
}

/* Sets MAC, performs overflow checks */
void setMAC(u32 idx, i64 data, int shift) {
    /* TODO: check for MAC overflow */

    /* Shift value, store low 32 bits of the result in MAC */
    mac[idx] = data >> shift;
}

/* Sign-extends MAC values */
i64 extsMAC(u32 idx, i64 data) {
    static const int MAC_WIDTH[] = { 31, 44, 44, 44 };

    /* TODO: check for MAC overflows */

    const int shift = 64 - MAC_WIDTH[idx];

    return (data << shift) >> shift;
}

/* Perspective Transform Triple */
void iRTPT(u32 cmd) {
    //std::printf("[GTE       ] RTPT\n");

    const auto lm = cmd & (1 << 10);
    const auto sf = cmd & (1 << 19);
    
    const auto shift = 12 * sf;

    for (int i = 0; i < 3; i++) {
        /* Do perspective transformation on vector Vi */
        const auto x = extsMAC(1, extsMAC(1, 0x1000 * tr[X] + rt[0][X] * v[i][X]) + rt[0][Y] * v[i][Y] + rt[0][Z] * v[i][Z]);
        const auto y = extsMAC(2, extsMAC(2, 0x1000 * tr[Y] + rt[1][X] * v[i][X]) + rt[1][Y] * v[i][Y] + rt[1][Z] * v[i][Z]);
        const auto z = extsMAC(3, extsMAC(3, 0x1000 * tr[Z] + rt[2][X] * v[i][X]) + rt[2][Y] * v[i][Y] + rt[2][Z] * v[i][Z]);

        /* Truncate results to 32 bits */
        setMAC(1, x, shift);
        setMAC(2, y, shift);
        setMAC(3, z, shift);

        setIR(1, mac[1], lm);
        setIR(2, mac[2], lm);

        setIR(3, z >> shift, false);

        /* Push new screen Z */

        pushSZ(mac[3] >> (12 * !sf));

        /* Calculate and push new screen XY */

        const auto unr = (i64)div(h, sz[3]);

        const auto sx = unr * ir[1] + ofx;
        const auto sy = unr * ir[2] + ofy;

        pushSXY(sx, sy);

        /* TODO: check for SX/SY MAC overflow */

        /* Depth cue */

        const auto dc = unr * dca + dcb;

        setMAC(0, dc, 0);

        setIR(0, dc >> 12, true);
    }
}

void doCmd(u32 cmd) {
    const auto opcode = cmd & 0x3F;

    switch (opcode) {
        case Opcode::RTPT: iRTPT(cmd); break;
        default:
            std::printf("[GTE       ] Unhandled instruction 0x%02X (0x%07X)\n", opcode, cmd);

            exit(0);
    }
}

}
