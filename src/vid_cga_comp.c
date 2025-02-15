/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *      running old operating systems and software designed for IBM
 *      PC systems and compatibles from 1981 through fairly recent
 *      system designs based on the PCI bus.
 *
 *      This file is part of the 86Box distribution.
 *
 *      IBM CGA composite filter, borrowed from reenigne's DOSBox
 *      patch and ported to C.
 *
 *  modified for RGBtoHDMI
 *
 * Authors: reenigne,
 *      Miran Grca, <mgrca8@gmail.com>
 *
 *      Copyright 2015-2019 reenigne.
 *      Copyright 2015-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include <stdbool.h>
#include "defs.h"
#include "osd.h"
#include "rgb_to_fb.h"
#include "vid_cga_comp.h"
#include "rgb_to_hdmi.h"
#include "logging.h"


static double brightness = 0;
static double contrast = 100;
static double saturation = 100;
static double sharpness = 0;
static double hue_offset = 0;
static int ntsc_pixel_phase = 0;
int video_sharpness;

/* New algorithm by reenigne
   Works in all CGA modes/color settings and can simulate older and newer CGA revisions */

static const double tau = 6.28318531; /* == 2*pi */

static unsigned char chroma_multiplexer[256] = {
      2,  2,  2,  2, 114,174,  4,  3,   2,  1,133,135,   2,113,150,  4,
    133,  2,  1, 99, 151,152,  2,  1,   3,  2, 96,136, 151,152,151,152,
      2, 56, 62,  4, 111,250,118,  4,   0, 51,207,137,   1,171,209,  5,
    140, 50, 54,100, 133,202, 57,  4,   2, 50,153,149, 128,198,198,135,
     32,  1, 36, 81, 147,158,  1, 42,  33,  1,210,254,  34,109,169, 77,
    177,  2,  0,165, 189,154,  3, 44,  33,  0, 91,197, 178,142,144,192,
      4,  2, 61, 67, 117,151,112, 83,   4,  0,249,255,   3,107,249,117,
    147,  1, 50,162, 143,141, 52, 54,   3,  0,145,206, 124,123,192,193,
     72, 78,  2,  0, 159,208,  4,  0,  53, 58,164,159,  37,159,171,  1,
    248,117,  4, 98, 212,218,  5,  2,  54, 59, 93,121, 176,181,134,130,
      1, 61, 31,  0, 160,255, 34,  1,   1, 58,197,166,   0,177,194,  2,
    162,111, 34, 96, 205,253, 32,  1,   1, 57,123,125, 119,188,150,112,
     78,  4,  0, 75, 166,180, 20, 38,  78,  1,143,246,  42,113,156, 37,
    252,  4,  1,188, 175,129,  1, 37, 118,  4, 88,249, 202,150,145,200,
     61, 59, 60, 60, 228,252,117, 77,  60, 58,248,251,  81,212,254,107,
    198, 59, 58,169, 250,251, 81, 80, 100, 58,154,250, 251,252,252,252};

static double intensity[4] = {
    77.175381, 88.654656, 166.564623, 174.228438};

#define NEW_CGA(c,i,r,g,b) (((c)/0.72)*0.29 + ((i)/0.28)*0.32 + ((r)/0.28)*0.1 + ((g)/0.28)*0.22 + ((b)/0.28)*0.07)

double mode_brightness;
double mode_contrast;
double mode_hue;
double min_v;
double max_v;

static int ntsc_type;

unsigned int swap_R_B(int IRGB) {
    //r & b are swapped in RGBtoHDMI so rearrange to match
    int r_new = (IRGB & 0x04) >> 2;
    int ig_new = (IRGB & 0x0a);
    int b_new = (IRGB & 0x01) << 2;
    return b_new | ig_new | r_new;
}

void update_cga16_color() {
    int x;
    double c, i, v;
    double q, a, s, r;
    double iq_adjust_i, iq_adjust_q;
    double i0, i3, mode_saturation;

        static const double ri = 0.9563;
        static const double rq = 0.6210;
        static const double gi = -0.2721;
        static const double gq = -0.6474;
        static const double bi = -1.1069;
        static const double bq = 1.7046;

        brightness = get_brightness() - 100;
        contrast = get_contrast();
        saturation = get_saturation();
        hue_offset = get_tint();
        ntsc_pixel_phase = ((get_ntscphase() ^ 3)) & 3; //reorder to match existing artifact code

        switch(get_ntscfringe()) {
            default:
            case FRINGE_MEDIUM:
                    sharpness = 0;
                break;
            case FRINGE_SHARP:
                    sharpness = 50;
                break;
            case FRINGE_SOFT:
                    sharpness = 0;
                break;
        }

        ntsc_type = get_ntsctype();

        bool new_cga;

        //maybe add some hue or brt/cont offsets etc here
        switch(get_ntsctype()) {
            default:
            case NTSCTYPE_NEW_CGA:
                    new_cga = 1;
                break;
            case NTSCTYPE_OLD_CGA:
                    new_cga = 0;
                break;
            case NTSCTYPE_APPLE:
                    new_cga = 1;
                break;
            case NTSCTYPE_SIMPLE:
                    new_cga = 0;
                break;
        }

        if (!new_cga) {
                min_v = chroma_multiplexer[0] + intensity[0];
                max_v = chroma_multiplexer[255] + intensity[3];
        }
        else {
                i0 = intensity[0];
                i3 = intensity[3];
                min_v = NEW_CGA(chroma_multiplexer[0], i0, i0, i0, i0);
                max_v = NEW_CGA(chroma_multiplexer[255], i3, i3, i3, i3);
        }
        mode_contrast = 256/(max_v - min_v);
        mode_brightness = -min_v*mode_contrast;

        //if ((cgamode & 3) == 1)
        //        mode_hue = 14;
        //else
                mode_hue = 4;

        mode_contrast *= contrast * (new_cga ? 1.2 : 1)/100;             /* new CGA: 120% */
        mode_brightness += (new_cga ? brightness-10 : brightness)*5;     /* new CGA: -10 */
        mode_saturation = (new_cga ? 4.35 : 2.9)*saturation/100;  /* new CGA: 150% */

        for (x = 0; x < 1024; ++x) {
                int phase = (x + ntsc_pixel_phase) & 3;
                int right = swap_R_B((x >> 2) & 0x0f);
                int left = swap_R_B((x >> 6) & 0x0f);
                int rc = right;
                int lc = left;
                //if ((cgamode & 4) != 0) {
                //        rc = (right & 8) | ((right & 7) != 0 ? 7 : 0);
                //        lc = (left & 8) | ((left & 7) != 0 ? 7 : 0);
                //}
                c = chroma_multiplexer[((lc & 7) << 5) | ((rc & 7) << 2) | phase];
                i = intensity[(left >> 3) | ((right >> 2) & 2)];
                if (!new_cga)
                        v = c + i;
                else {
                        double r = intensity[((left >> 2) & 1) | ((right >> 1) & 2)];
                        double g = intensity[((left >> 1) & 1) | (right & 2)];
                        double b = intensity[(left & 1) | ((right << 1) & 2)];
                        v = NEW_CGA(c, i, r, g, b);
                }
                CGA_Composite_Table[x] =  (int) (v*mode_contrast + mode_brightness);
        }

        i = CGA_Composite_Table[3*68] - CGA_Composite_Table[3*68 + 2];
        q = CGA_Composite_Table[3*68 + 1] - CGA_Composite_Table[3*68 + 3];

        a = tau*(33 + 90 + hue_offset + mode_hue)/360.0;
        c = cos(a);
        s = sin(a);
        r = 256*mode_saturation/sqrt(i*i+q*q);

        iq_adjust_i = -(i*c + q*s)*r;
        iq_adjust_q = (q*c - i*s)*r;

        video_ri = (int) (ri*iq_adjust_i + rq*iq_adjust_q);
        video_rq = (int) (-ri*iq_adjust_q + rq*iq_adjust_i);
        video_gi = (int) (gi*iq_adjust_i + gq*iq_adjust_q);
        video_gq = (int) (-gi*iq_adjust_q + gq*iq_adjust_i);
        video_bi = (int) (bi*iq_adjust_i + bq*iq_adjust_q);
        video_bq = (int) (-bi*iq_adjust_q + bq*iq_adjust_i);
        video_sharpness = (int) (sharpness*256/100);
}

static Bit8u byte_clamp(int v) {
        v >>= (13 + 4);
        return v < 0 ? 0 : (v > 15 ? 15 : v);
}

/* 2048x1536 is the maximum we can possibly support. */
#define SCALER_MAXWIDTH 2048

static int temp[SCALER_MAXWIDTH + 10]={0};
static int atemp[SCALER_MAXWIDTH + 2]={0};
static int btemp[SCALER_MAXWIDTH + 2]={0};

void Composite_Process(Bit32u blocks, Bit8u *rgbi, int render)
{
    int x;
    Bit32u x2;
    int w = blocks*8;
    int *o;
    int *i;
    int *ap, *bp;

    uint32_t temp_srgb;
    uint32_t srgb0;
    uint32_t srgb1;
    uint32_t srgb2;
    uint32_t srgb3;

#define COMPOSITE_CONVERT(I, Q) do { \
        i[1] = (i[1]<<3) - ap[1]; \
        a = ap[0]; \
        b = bp[0]; \
        c = i[0]+i[0]; \
        d = i[-1]+i[1]; \
        y = ((c+d)<<8) + video_sharpness*(c-d); \
        rr = y + video_ri*(I) + video_rq*(Q); \
        gg = y + video_gi*(I) + video_gq*(Q); \
        bb = y + video_bi*(I) + video_bq*(Q); \
        ++i; \
        ++ap; \
        ++bp; \
} while (0)

#define OUT(v) do { *o = (v); ++o; } while (0)

        /* Simulate CGA composite output */
        o = temp;
        OUT(CGA_Composite_Table[(0<<6) | ((*rgbi & 0x0f)<<2) | 3]);
        for (x = 0; x < w; ++x) {
                OUT(CGA_Composite_Table[(((Bit32u)rgbi[0] & 0x0f)<<6) | (((Bit32u)rgbi[1] & 0x0f)<<2) | (x & 3)]);
                ++rgbi;
        }

        /* Store chroma */
        i = temp + 4;
        ap = atemp + 1;
        bp = btemp + 1;
        for (x = -1; x < w + 1; ++x) {
                ap[x] = i[-4]-((i[-2]-i[0]+i[2])<<1)+i[4];
                bp[x] = (i[-3]-i[-1]+i[1]-i[3])<<1;

                //   ap[x] = (-i[-2]+((i[0])<<1)-i[2])<<1;
                //   bp[x] = (-i[-1]+i[1])<<2;
                ++i;
        }

        /* Decode */
        i = temp + 5;
        i[-1] = (i[-1]<<3) - ap[-1];
        i[0] = (i[0]<<3) - ap[0];
        for (x2 = 0; x2 < blocks - 1; ++x2) {
            int y,a,b,c,d,rr,gg,bb;
                COMPOSITE_CONVERT(a, b);
                temp_srgb = (byte_clamp(rr)<<8) | (byte_clamp(gg)<<4) | byte_clamp(bb);
                COMPOSITE_CONVERT(-b, a);
                srgb0 = temp_srgb | (byte_clamp(rr)<<24) | (byte_clamp(gg)<<20) | byte_clamp(bb)<<16;
                COMPOSITE_CONVERT(-a, -b);
                temp_srgb = (byte_clamp(rr)<<8) | (byte_clamp(gg)<<4) | byte_clamp(bb);
                COMPOSITE_CONVERT(b, -a);
                srgb1 = temp_srgb | (byte_clamp(rr)<<24) | (byte_clamp(gg)<<20) | byte_clamp(bb)<<16;
                COMPOSITE_CONVERT(a, b);
                temp_srgb = (byte_clamp(rr)<<8) | (byte_clamp(gg)<<4) | byte_clamp(bb);
                COMPOSITE_CONVERT(-b, a);
                srgb2 = temp_srgb | (byte_clamp(rr)<<24) | (byte_clamp(gg)<<20) | byte_clamp(bb)<<16;
                COMPOSITE_CONVERT(-a, -b);
                temp_srgb = (byte_clamp(rr)<<8) | (byte_clamp(gg)<<4) | byte_clamp(bb);
                COMPOSITE_CONVERT(b, -a);
                srgb3 = temp_srgb | (byte_clamp(rr)<<24) | (byte_clamp(gg)<<20) | byte_clamp(bb)<<16;
            if (render)
                cga_render_words(srgb0, srgb1, srgb2, srgb3);  //renders 8 pixels at a time to the uncached screen using stmia for speed
            //if (!render && x2<8) log_info("%x %x %x %x",srgb0, srgb1, srgb2, srgb3);
        }

#undef COMPOSITE_CONVERT
#undef OUT
}

void Test_Composite_Process(Bit32u blocks, Bit8u *rgbi, int render) {
    Composite_Process(blocks, rgbi, render);
}

