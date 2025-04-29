// =====================================================================================
// GimliDS Copyright (c) 2025 Dave Bernazzani (wavemotion-dave)
//
// As GimliDS is a port of the Frodo emulator for the DS/DSi/XL/LL handhelds,
// any copying or distribution of this emulator, its source code and associated
// readme files, with or without modification, are permitted per the original
// Frodo emulator license shown below.  Hugest thanks to Christian Bauer for his
// efforts to provide a clean open-source emulation base for the C64.
//
// Numerous hacks and 'unsafe' optimizations have been performed on the original
// Frodo emulator codebase to get it running on the small handheld system. You
// are strongly encouraged to seek out the official Frodo sources if you're at
// all interested in this emulator code.
//
// The GimliDS emulator is offered as-is, without any warranty. Please see readme.md
// =====================================================================================

/*
 *  VIC.cpp - 6569R5 emulation (line based)
 *
 *  Frodo Copyright (C) Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Notes:
 * ------
 *
 *  - The EmulateLine() function is called for every emulated
 *    raster line. It computes one pixel row of the graphics
 *    according to the current VIC register settings and returns
 *    the number of cycles available for the CPU in that line.
 *  - The graphics are output into an 8 bit chunky bitmap
 *  - The sprite-graphics priority handling and collision
 *    detection is done in a bit-oriented way with masks.
 *    The foreground/background pixel mask for the graphics
 *    is stored in the fore_mask_buf[] array. Multicolor
 *    sprites are converted from their original chunky format
 *    to a bitplane representation (two bit masks) for easier
 *    handling of priorities and collisions.
 *  - The sprite-sprite priority handling and collision
 *    detection is done in with the byte array spr_coll_buf[],
 *    that is used to keep track of which sprites are already
 *    visible at certain X positions.
 *
 * Incompatibilities:
 * ------------------
 *
 *  - Raster effects that are achieved by modifying VIC registers
 *    in the middle of a raster line cannot be emulated
 *  - Sprite collisions are only detected within the visible
 *    screen area (excluding borders)
 *  - Sprites are only drawn if they completely fit within the
 *    left/right limits of the chunky bitmap
 *  - The Char ROM is not visible in the bitmap displays at
 *    addresses $0000 and $8000
 *  - The IRQ is cleared on every write access to the flag
 *    register. This is a hack for the RMW instructions of the
 *    6510 that first write back the original value.
 */
#include <nds.h>
#include "sysdeps.h"

#include "VIC.h"
#include "C64.h"
#include "CPUC64.h"
#include "Display.h"
#include "mainmenu.h"
#include "Prefs.h"

// First and last displayed line
const unsigned FIRST_DISP_LINE = 0x20;
const unsigned LAST_DISP_LINE = 0x110;

// First and last possible line for Bad Lines
const unsigned FIRST_DMA_LINE = 0x30;
const unsigned LAST_DMA_LINE = 0xf7;

// Display window coordinates
const int ROW25_YSTART = 0x33;
const int ROW25_YSTOP = 0xfb;
const int ROW24_YSTART = 0x37;
const int ROW24_YSTOP = 0xf7;

const int COL40_XSTART = 0x20;
const int COL40_XSTOP = 0x160;
const int COL38_XSTART = 0x27;
const int COL38_XSTOP = 0x157;

uint8 fast_line_buffer[512] __attribute__((section(".dtcm"))) = {0};

// Tables for sprite X expansion
uint16 ExpTable[256] __attribute__((section(".dtcm"))) = {
    0x0000, 0x0003, 0x000C, 0x000F, 0x0030, 0x0033, 0x003C, 0x003F,
    0x00C0, 0x00C3, 0x00CC, 0x00CF, 0x00F0, 0x00F3, 0x00FC, 0x00FF,
    0x0300, 0x0303, 0x030C, 0x030F, 0x0330, 0x0333, 0x033C, 0x033F,
    0x03C0, 0x03C3, 0x03CC, 0x03CF, 0x03F0, 0x03F3, 0x03FC, 0x03FF,
    0x0C00, 0x0C03, 0x0C0C, 0x0C0F, 0x0C30, 0x0C33, 0x0C3C, 0x0C3F,
    0x0CC0, 0x0CC3, 0x0CCC, 0x0CCF, 0x0CF0, 0x0CF3, 0x0CFC, 0x0CFF,
    0x0F00, 0x0F03, 0x0F0C, 0x0F0F, 0x0F30, 0x0F33, 0x0F3C, 0x0F3F,
    0x0FC0, 0x0FC3, 0x0FCC, 0x0FCF, 0x0FF0, 0x0FF3, 0x0FFC, 0x0FFF,
    0x3000, 0x3003, 0x300C, 0x300F, 0x3030, 0x3033, 0x303C, 0x303F,
    0x30C0, 0x30C3, 0x30CC, 0x30CF, 0x30F0, 0x30F3, 0x30FC, 0x30FF,
    0x3300, 0x3303, 0x330C, 0x330F, 0x3330, 0x3333, 0x333C, 0x333F,
    0x33C0, 0x33C3, 0x33CC, 0x33CF, 0x33F0, 0x33F3, 0x33FC, 0x33FF,
    0x3C00, 0x3C03, 0x3C0C, 0x3C0F, 0x3C30, 0x3C33, 0x3C3C, 0x3C3F,
    0x3CC0, 0x3CC3, 0x3CCC, 0x3CCF, 0x3CF0, 0x3CF3, 0x3CFC, 0x3CFF,
    0x3F00, 0x3F03, 0x3F0C, 0x3F0F, 0x3F30, 0x3F33, 0x3F3C, 0x3F3F,
    0x3FC0, 0x3FC3, 0x3FCC, 0x3FCF, 0x3FF0, 0x3FF3, 0x3FFC, 0x3FFF,
    0xC000, 0xC003, 0xC00C, 0xC00F, 0xC030, 0xC033, 0xC03C, 0xC03F,
    0xC0C0, 0xC0C3, 0xC0CC, 0xC0CF, 0xC0F0, 0xC0F3, 0xC0FC, 0xC0FF,
    0xC300, 0xC303, 0xC30C, 0xC30F, 0xC330, 0xC333, 0xC33C, 0xC33F,
    0xC3C0, 0xC3C3, 0xC3CC, 0xC3CF, 0xC3F0, 0xC3F3, 0xC3FC, 0xC3FF,
    0xCC00, 0xCC03, 0xCC0C, 0xCC0F, 0xCC30, 0xCC33, 0xCC3C, 0xCC3F,
    0xCCC0, 0xCCC3, 0xCCCC, 0xCCCF, 0xCCF0, 0xCCF3, 0xCCFC, 0xCCFF,
    0xCF00, 0xCF03, 0xCF0C, 0xCF0F, 0xCF30, 0xCF33, 0xCF3C, 0xCF3F,
    0xCFC0, 0xCFC3, 0xCFCC, 0xCFCF, 0xCFF0, 0xCFF3, 0xCFFC, 0xCFFF,
    0xF000, 0xF003, 0xF00C, 0xF00F, 0xF030, 0xF033, 0xF03C, 0xF03F,
    0xF0C0, 0xF0C3, 0xF0CC, 0xF0CF, 0xF0F0, 0xF0F3, 0xF0FC, 0xF0FF,
    0xF300, 0xF303, 0xF30C, 0xF30F, 0xF330, 0xF333, 0xF33C, 0xF33F,
    0xF3C0, 0xF3C3, 0xF3CC, 0xF3CF, 0xF3F0, 0xF3F3, 0xF3FC, 0xF3FF,
    0xFC00, 0xFC03, 0xFC0C, 0xFC0F, 0xFC30, 0xFC33, 0xFC3C, 0xFC3F,
    0xFCC0, 0xFCC3, 0xFCCC, 0xFCCF, 0xFCF0, 0xFCF3, 0xFCFC, 0xFCFF,
    0xFF00, 0xFF03, 0xFF0C, 0xFF0F, 0xFF30, 0xFF33, 0xFF3C, 0xFF3F,
    0xFFC0, 0xFFC3, 0xFFCC, 0xFFCF, 0xFFF0, 0xFFF3, 0xFFFC, 0xFFFF
};

uint16 MultiExpTable[256] __attribute__((section(".dtcm"))) = {
    0x0000, 0x0005, 0x000A, 0x000F, 0x0050, 0x0055, 0x005A, 0x005F,
    0x00A0, 0x00A5, 0x00AA, 0x00AF, 0x00F0, 0x00F5, 0x00FA, 0x00FF,
    0x0500, 0x0505, 0x050A, 0x050F, 0x0550, 0x0555, 0x055A, 0x055F,
    0x05A0, 0x05A5, 0x05AA, 0x05AF, 0x05F0, 0x05F5, 0x05FA, 0x05FF,
    0x0A00, 0x0A05, 0x0A0A, 0x0A0F, 0x0A50, 0x0A55, 0x0A5A, 0x0A5F,
    0x0AA0, 0x0AA5, 0x0AAA, 0x0AAF, 0x0AF0, 0x0AF5, 0x0AFA, 0x0AFF,
    0x0F00, 0x0F05, 0x0F0A, 0x0F0F, 0x0F50, 0x0F55, 0x0F5A, 0x0F5F,
    0x0FA0, 0x0FA5, 0x0FAA, 0x0FAF, 0x0FF0, 0x0FF5, 0x0FFA, 0x0FFF,
    0x5000, 0x5005, 0x500A, 0x500F, 0x5050, 0x5055, 0x505A, 0x505F,
    0x50A0, 0x50A5, 0x50AA, 0x50AF, 0x50F0, 0x50F5, 0x50FA, 0x50FF,
    0x5500, 0x5505, 0x550A, 0x550F, 0x5550, 0x5555, 0x555A, 0x555F,
    0x55A0, 0x55A5, 0x55AA, 0x55AF, 0x55F0, 0x55F5, 0x55FA, 0x55FF,
    0x5A00, 0x5A05, 0x5A0A, 0x5A0F, 0x5A50, 0x5A55, 0x5A5A, 0x5A5F,
    0x5AA0, 0x5AA5, 0x5AAA, 0x5AAF, 0x5AF0, 0x5AF5, 0x5AFA, 0x5AFF,
    0x5F00, 0x5F05, 0x5F0A, 0x5F0F, 0x5F50, 0x5F55, 0x5F5A, 0x5F5F,
    0x5FA0, 0x5FA5, 0x5FAA, 0x5FAF, 0x5FF0, 0x5FF5, 0x5FFA, 0x5FFF,
    0xA000, 0xA005, 0xA00A, 0xA00F, 0xA050, 0xA055, 0xA05A, 0xA05F,
    0xA0A0, 0xA0A5, 0xA0AA, 0xA0AF, 0xA0F0, 0xA0F5, 0xA0FA, 0xA0FF,
    0xA500, 0xA505, 0xA50A, 0xA50F, 0xA550, 0xA555, 0xA55A, 0xA55F,
    0xA5A0, 0xA5A5, 0xA5AA, 0xA5AF, 0xA5F0, 0xA5F5, 0xA5FA, 0xA5FF,
    0xAA00, 0xAA05, 0xAA0A, 0xAA0F, 0xAA50, 0xAA55, 0xAA5A, 0xAA5F,
    0xAAA0, 0xAAA5, 0xAAAA, 0xAAAF, 0xAAF0, 0xAAF5, 0xAAFA, 0xAAFF,
    0xAF00, 0xAF05, 0xAF0A, 0xAF0F, 0xAF50, 0xAF55, 0xAF5A, 0xAF5F,
    0xAFA0, 0xAFA5, 0xAFAA, 0xAFAF, 0xAFF0, 0xAFF5, 0xAFFA, 0xAFFF,
    0xF000, 0xF005, 0xF00A, 0xF00F, 0xF050, 0xF055, 0xF05A, 0xF05F,
    0xF0A0, 0xF0A5, 0xF0AA, 0xF0AF, 0xF0F0, 0xF0F5, 0xF0FA, 0xF0FF,
    0xF500, 0xF505, 0xF50A, 0xF50F, 0xF550, 0xF555, 0xF55A, 0xF55F,
    0xF5A0, 0xF5A5, 0xF5AA, 0xF5AF, 0xF5F0, 0xF5F5, 0xF5FA, 0xF5FF,
    0xFA00, 0xFA05, 0xFA0A, 0xFA0F, 0xFA50, 0xFA55, 0xFA5A, 0xFA5F,
    0xFAA0, 0xFAA5, 0xFAAA, 0xFAAF, 0xFAF0, 0xFAF5, 0xFAFA, 0xFAFF,
    0xFF00, 0xFF05, 0xFF0A, 0xFF0F, 0xFF50, 0xFF55, 0xFF5A, 0xFF5F,
    0xFFA0, 0xFFA5, 0xFFAA, 0xFFAF, 0xFFF0, 0xFFF5, 0xFFFA, 0xFFFF
};

static union {
    struct {
        uint8 a,b,c,d;
    } a;
    uint32 b;
} TextColorTable[16][16][16];

#ifdef GLOBAL_VARS
static uint16 mc_color_lookup[4]        __attribute__((section(".dtcm")));
static uint8 text_chunky_buf[40*8]      __attribute__((section(".dtcm")));
static uint16 mx[8]                     __attribute__((section(".dtcm")));
static uint8 mx8                        __attribute__((section(".dtcm")));
static uint8 my[8]                      __attribute__((section(".dtcm")));
static uint8 ctrl1                      __attribute__((section(".dtcm")));
static uint8 ctrl2                      __attribute__((section(".dtcm")));
static uint8 lpx                        __attribute__((section(".dtcm")));
static uint8 lpy                        __attribute__((section(".dtcm")));
static uint8 me                         __attribute__((section(".dtcm")));
static uint8 mxe                        __attribute__((section(".dtcm")));
static uint8 mye                        __attribute__((section(".dtcm")));
static uint8 mdp                        __attribute__((section(".dtcm")));
static uint8 mmc                        __attribute__((section(".dtcm")));
static uint8 vbase                      __attribute__((section(".dtcm")));
static uint8 irq_flag                   __attribute__((section(".dtcm")));
static uint8 irq_mask                   __attribute__((section(".dtcm")));
static uint8 clx_spr                    __attribute__((section(".dtcm")));
static uint8 clx_bgr                    __attribute__((section(".dtcm")));
static uint8 ec                         __attribute__((section(".dtcm")));
static uint8 b0c                        __attribute__((section(".dtcm")));
static uint8 b1c                        __attribute__((section(".dtcm")));
static uint8 b2c                        __attribute__((section(".dtcm")));
static uint8 b3c                        __attribute__((section(".dtcm")));
static uint8 mm0                        __attribute__((section(".dtcm")));
static uint8 mm1                        __attribute__((section(".dtcm")));

static uint8 sc[8]                      __attribute__((section(".dtcm")));

static uint8 *ram                       __attribute__((section(".dtcm")));
static uint8 *char_rom                  __attribute__((section(".dtcm")));
static uint8 *color_ram                 __attribute__((section(".dtcm")));  // Pointers to RAM and ROM

static C64 *the_c64                     __attribute__((section(".dtcm")));  // Pointer to C64
static C64Display *the_display          __attribute__((section(".dtcm")));  // Pointer to C64Display
static MOS6510 *the_cpu                 __attribute__((section(".dtcm")));  // Pointer to 6510

static uint8 colors[256]                __attribute__((section(".dtcm")));  // Indices of the 16 C64 colors (16 times mirrored to avoid "& 0x0f")

static uint8 ec_color                   __attribute__((section(".dtcm")));
static uint8 b0c_color                  __attribute__((section(".dtcm")));
static uint8 b1c_color                  __attribute__((section(".dtcm")));
static uint8 b2c_color                  __attribute__((section(".dtcm")));
static uint8 b3c_color                  __attribute__((section(".dtcm")));
static uint32 b0c_color32               __attribute__((section(".dtcm")));

static uint8 mm0_color                  __attribute__((section(".dtcm")));
static uint8 mm1_color                  __attribute__((section(".dtcm")));    // Indices for MOB multicolors
static uint8 spr_color[8]               __attribute__((section(".dtcm")));    // Indices for MOB colors
static uint32 ec_color_long             __attribute__((section(".dtcm")));    // ec_color expanded to 32 bits

static uint8 matrix_line[40]            __attribute__((section(".dtcm")));    // Buffer for video line, read in Bad Lines
static uint8 color_line[40]             __attribute__((section(".dtcm")));    // Buffer for color line, read in Bad Lines

static uint8 *matrix_base               __attribute__((section(".dtcm")));    // Video matrix base
static uint8 *char_base                 __attribute__((section(".dtcm")));    // Character generator base
static uint8 *bitmap_base               __attribute__((section(".dtcm")));    // Bitmap base

static uint16 raster_y                  __attribute__((section(".dtcm")));     // Current raster line
static uint16 irq_raster                __attribute__((section(".dtcm")));     // Interrupt raster line
static uint16 dy_start                  __attribute__((section(".dtcm")));     // Comparison values for border logic
static uint16 dy_stop                   __attribute__((section(".dtcm")));
static uint16 rc                        __attribute__((section(".dtcm")));     // Row counter
static uint16 vc                        __attribute__((section(".dtcm")));     // Video counter
static uint16 vc_base                   __attribute__((section(".dtcm")));     // Video counter base
static uint16 x_scroll                  __attribute__((section(".dtcm")));     // X scroll value
static uint16 y_scroll                  __attribute__((section(".dtcm")));
static uint16 cia_vabase                __attribute__((section(".dtcm")));     // CIA VA14/15 video base

static int display_idx                  __attribute__((section(".dtcm")));     // Index of current display mode
static uint32 mc[8]                     __attribute__((section(".dtcm")));     // Sprite data counters
static uint8 sprite_on                  __attribute__((section(".dtcm")));     // 8 Flags: Sprite display/DMA active

static uint8 spr_coll_buf[DISPLAY_X]    __attribute__((section(".dtcm")));     // Buffer for sprite-sprite collisions and priorities
static uint8 fore_mask_buf[DISPLAY_X/8] __attribute__((section(".dtcm")));     // Foreground mask for sprite-graphics collisions and priorities

static u8   display_state               __attribute__((section(".dtcm")));     // true: Display state, false: Idle state
static u8   border_on                   __attribute__((section(".dtcm")));     // Flag: Upper/lower border on
static u8   border_40_col               __attribute__((section(".dtcm")));     // Flag: 40 column border
static u8   frame_skipped               __attribute__((section(".dtcm")));     // Flag: Frame is being skipped
static uint8 bad_lines_enabled          __attribute__((section(".dtcm")));     // Flag: Bad Lines enabled for this frame
static bool lp_triggered                __attribute__((section(".dtcm")));     // Flag: Lightpen was triggered in this frame

static u32  total_frames                __attribute__((section(".dtcm")));     // Total frames - used for consistent frame skip on DS-Lite

#endif


/*
 *  Constructor: Initialize variables
 */

static void init_text_color_table(uint8 *colors)
{
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            for (int k = 0; k < 16; k++) {
                TextColorTable[i][j][k].a.a = colors[k & 8 ? i : j];
                TextColorTable[i][j][k].a.b = colors[k & 4 ? i : j];
                TextColorTable[i][j][k].a.c = colors[k & 2 ? i : j];
                TextColorTable[i][j][k].a.d = colors[k & 1 ? i : j];
            }
}

MOS6569::MOS6569(C64 *c64, C64Display *disp, MOS6510 *CPU, uint8 *RAM, uint8 *Char, uint8 *Color)
#ifndef GLOBAL_VARS
    : char_rom(Char), color_ram(Color), the_c64(c64), the_display(disp), the_cpu(CPU)
#endif
{
    int i;

    // Set pointers
#ifdef GLOBAL_VARS
    the_c64 = c64;
    the_display = disp;
    the_cpu = CPU;
    ram = RAM;
    char_rom = Char;
    color_ram = Color;
#endif
    matrix_base = RAM;
    char_base = RAM;
    bitmap_base = RAM;

    // Initialize VIC registers
    mx8 = 0;
    ctrl1 = ctrl2 = 0;
    lpx = lpy = 0;
    me = mxe = mye = mdp = mmc = 0;
    vbase = irq_flag = irq_mask = 0;
    clx_spr = clx_bgr = 0;
    cia_vabase = 0;
    ec = b0c = b1c = b2c = b3c = mm0 = mm1 = 0;
    b0c_color32 = 0;
    for (i=0; i<8; i++) mx[i] = my[i] = sc[i] = 0;

    // Initialize other variables
    raster_y = 0xffff;
    rc = 7;
    irq_raster = vc = vc_base = x_scroll = y_scroll = 0;
    dy_start = ROW24_YSTART;
    dy_stop = ROW24_YSTOP;

    display_idx = 0;
    display_state = false;
    border_on = false;
    lp_triggered = false;

    sprite_on = 0;
    for (i=0; i<8; i++)
        mc[i] = 21;

    frame_skipped = false;
    total_frames = 0;

    // Clear foreground mask
    memset(fore_mask_buf, 0, DISPLAY_X/8);

    // Preset colors to black
    disp->InitColors(colors);
    init_text_color_table(colors);
    ec_color = b0c_color = b1c_color = b2c_color = b3c_color = mm0_color = mm1_color = colors[0];
    ec_color_long = (ec_color << 24) | (ec_color << 16) | (ec_color << 8) | ec_color;
    for (i=0; i<8; i++) spr_color[i] = colors[0];
}


void MOS6569::Reset(void)
{
    display_idx = 0;
    display_state = false;
    border_on = false;
    lp_triggered = false;

    total_frames = 0;
    frame_skipped = false;
    raster_y = 0xffff;

    // Clear foreground mask
    memset(fore_mask_buf, 0, DISPLAY_X/8);
}


#ifdef GLOBAL_VARS
static void make_mc_table(void)
#else
void MOS6569::make_mc_table(void)
#endif
{
    mc_color_lookup[0] = b0c_color | (b0c_color << 8);
    mc_color_lookup[1] = b1c_color | (b1c_color << 8);
    mc_color_lookup[2] = b2c_color | (b2c_color << 8);
}


/*
 *  Convert video address to pointer
 */

#ifdef GLOBAL_VARS
inline uint8 *get_physical(uint16 adr)
#else
inline uint8 *MOS6569::get_physical(uint16 adr)
#endif
{
    int va = adr | cia_vabase;
    if ((va & 0x7000) == 0x1000)
        return char_rom + (va & 0x0fff);
    else
        return ram + va;
}


/*
 *  Get VIC state
 */

void MOS6569::GetState(MOS6569State *vd)
{
    int i;

    vd->m0x = mx[0] & 0xff; vd->m0y = my[0];
    vd->m1x = mx[1] & 0xff; vd->m1y = my[1];
    vd->m2x = mx[2] & 0xff; vd->m2y = my[2];
    vd->m3x = mx[3] & 0xff; vd->m3y = my[3];
    vd->m4x = mx[4] & 0xff; vd->m4y = my[4];
    vd->m5x = mx[5] & 0xff; vd->m5y = my[5];
    vd->m6x = mx[6] & 0xff; vd->m6y = my[6];
    vd->m7x = mx[7] & 0xff; vd->m7y = my[7];
    vd->mx8 = mx8;

    vd->ctrl1 = (ctrl1 & 0x7f) | ((raster_y & 0x100) >> 1);
    vd->raster = raster_y & 0xff;
    vd->lpx = lpx; vd->lpy = lpy;
    vd->ctrl2 = ctrl2;
    vd->vbase = vbase;
    vd->irq_flag = irq_flag;
    vd->irq_mask = irq_mask;

    vd->me = me; vd->mxe = mxe; vd->mye = mye; vd->mdp = mdp; vd->mmc = mmc;
    vd->mm = clx_spr; vd->md = clx_bgr;

    vd->ec = ec;
    vd->b0c = b0c; vd->b1c = b1c; vd->b2c = b2c; vd->b3c = b3c;
    vd->mm0 = mm0; vd->mm1 = mm1;
    vd->m0c = sc[0]; vd->m1c = sc[1];
    vd->m2c = sc[2]; vd->m3c = sc[3];
    vd->m4c = sc[4]; vd->m5c = sc[5];
    vd->m6c = sc[6]; vd->m7c = sc[7];

    vd->pad0 = 0;
    vd->irq_raster = irq_raster;
    vd->vc = vc;
    vd->vc_base = vc_base;
    vd->rc = rc;
    vd->spr_dma = vd->spr_disp = sprite_on;
    for (i=0; i<8; i++)
        vd->mc[i] = vd->mc_base[i] = mc[i];
    vd->display_state = display_state;
    vd->bad_line = raster_y >= FIRST_DMA_LINE && raster_y <= LAST_DMA_LINE && ((raster_y & 7) == y_scroll) && bad_lines_enabled;
    vd->bad_line_enable = bad_lines_enabled;
    vd->lp_triggered = lp_triggered;
    vd->border_on = border_on;

    vd->bank_base = cia_vabase;
    vd->matrix_base = ((vbase & 0xf0) << 6) | cia_vabase;
    vd->char_base = ((vbase & 0x0e) << 10) | cia_vabase;
    vd->bitmap_base = ((vbase & 0x08) << 10) | cia_vabase;
    for (i=0; i<8; i++)
        vd->sprite_base[i] = (matrix_base[0x3f8 + i] << 6) | cia_vabase;

    vd->cycle = 1;
    vd->raster_x = 0;
    vd->ml_index = 0;
    vd->ref_cnt = 0xff;
    vd->last_vic_byte = 0;
    vd->ud_border_on = border_on;
    vd->total_frames = total_frames;
}


/*
 *  Set VIC state (only works if in VBlank)
 */

void MOS6569::SetState(MOS6569State *vd)
{
    int i, j;

    mx[0] = vd->m0x; my[0] = vd->m0y;
    mx[1] = vd->m1x; my[1] = vd->m1y;
    mx[2] = vd->m2x; my[2] = vd->m2y;
    mx[3] = vd->m3x; my[3] = vd->m3y;
    mx[4] = vd->m4x; my[4] = vd->m4y;
    mx[5] = vd->m5x; my[5] = vd->m5y;
    mx[6] = vd->m6x; my[6] = vd->m6y;
    mx[7] = vd->m7x; my[7] = vd->m7y;
    mx8 = vd->mx8;
    for (i=0, j=1; i<8; i++, j<<=1) {
        if (mx8 & j)
            mx[i] |= 0x100;
        else
            mx[i] &= 0xff;
    }

    ctrl1 = vd->ctrl1;
    ctrl2 = vd->ctrl2;
    x_scroll = ctrl2 & 7;
    y_scroll = ctrl1 & 7;
    if (ctrl1 & 8) {
        dy_start = ROW25_YSTART;
        dy_stop = ROW25_YSTOP;
    } else {
        dy_start = ROW24_YSTART;
        dy_stop = ROW24_YSTOP;
    }
    border_40_col = ctrl2 & 8;
    display_idx = ((ctrl1 & 0x60) | (ctrl2 & 0x10)) >> 4;

    raster_y = vd->raster | ((vd->ctrl1 & 0x80) << 1);
    lpx = vd->lpx; lpy = vd->lpy;

    vbase = vd->vbase;
    cia_vabase = vd->bank_base;
    matrix_base = get_physical((vbase & 0xf0) << 6);
    char_base = get_physical((vbase & 0x0e) << 10);
    bitmap_base = get_physical((vbase & 0x08) << 10);

    irq_flag = vd->irq_flag;
    irq_mask = vd->irq_mask;

    me = vd->me; mxe = vd->mxe; mye = vd->mye; mdp = vd->mdp; mmc = vd->mmc;
    clx_spr = vd->mm; clx_bgr = vd->md;

    ec = vd->ec;
    ec_color = colors[ec];
    ec_color_long = (ec_color << 24) | (ec_color << 16) | (ec_color << 8) | ec_color;

    b0c = vd->b0c; b1c = vd->b1c; b2c = vd->b2c; b3c = vd->b3c;
    b0c_color = colors[b0c];
    b1c_color = colors[b1c];
    b2c_color = colors[b2c];
    b3c_color = colors[b3c];
    b0c_color32 = (b0c_color << 24) | (b0c_color << 16) | (b0c_color << 8) | (b0c_color << 0);
    make_mc_table();

    mm0 = vd->mm0; mm1 = vd->mm1;
    mm0_color = colors[mm0];
    mm1_color = colors[mm1];

    sc[0] = vd->m0c; sc[1] = vd->m1c;
    sc[2] = vd->m2c; sc[3] = vd->m3c;
    sc[4] = vd->m4c; sc[5] = vd->m5c;
    sc[6] = vd->m6c; sc[7] = vd->m7c;
    for (i=0; i<8; i++)
        spr_color[i] = colors[sc[i]];

    irq_raster = vd->irq_raster;
    vc = vd->vc;
    vc_base = vd->vc_base;
    rc = vd->rc;
    sprite_on = vd->spr_dma;
    for (i=0; i<8; i++)
        mc[i] = vd->mc[i];
    display_state = vd->display_state;
    bad_lines_enabled = vd->bad_line_enable;
    lp_triggered = vd->lp_triggered;
    border_on = vd->border_on;
    total_frames = vd->total_frames;
}


/*
 *  Trigger raster IRQ
 */

#ifdef GLOBAL_VARS
static inline void raster_irq(void)
#else
inline void MOS6569::raster_irq(void)
#endif
{
    irq_flag |= 0x01;
    if (irq_mask & 0x01) {
        irq_flag |= 0x80;
        the_cpu->TriggerVICIRQ();
    }
}


/*
 *  Read from VIC register
 */

ITCM_CODE uint8 MOS6569::ReadRegister(uint16 adr)
{
    switch (adr) {
        case 0x00: case 0x02: case 0x04: case 0x06:
        case 0x08: case 0x0a: case 0x0c: case 0x0e:
            return mx[adr >> 1];

        case 0x01: case 0x03: case 0x05: case 0x07:
        case 0x09: case 0x0b: case 0x0d: case 0x0f:
            return my[adr >> 1];

        case 0x10:  // Sprite X position MSB
            return mx8;

        case 0x11:  // Control register 1
            return (ctrl1 & 0x7f) | ((raster_y & 0x100) >> 1);

        case 0x12:  // Raster counter
            return raster_y;

        case 0x13:  // Light pen X
            return lpx;

        case 0x14:  // Light pen Y
            return lpy;

        case 0x15:  // Sprite enable
            return me;

        case 0x16:  // Control register 2
            return ctrl2 | 0xc0;

        case 0x17:  // Sprite Y expansion
            return mye;

        case 0x18:  // Memory pointers
            return vbase | 0x01;

        case 0x19:  // IRQ flags
            return irq_flag | 0x70;

        case 0x1a:  // IRQ mask
            return irq_mask | 0xf0;

        case 0x1b:  // Sprite data priority
            return mdp;

        case 0x1c:  // Sprite multicolor
            return mmc;

        case 0x1d:  // Sprite X expansion
            return mxe;

        case 0x1e:{ // Sprite-sprite collision
            uint8 ret = clx_spr;
            clx_spr = 0;    // Read and clear
            return ret;
        }

        case 0x1f:{ // Sprite-background collision
            uint8 ret = clx_bgr;
            clx_bgr = 0;    // Read and clear
            return ret;
        }

        case 0x20: return ec | 0xf0;
        case 0x21: return b0c | 0xf0;
        case 0x22: return b1c | 0xf0;
        case 0x23: return b2c | 0xf0;
        case 0x24: return b3c | 0xf0;
        case 0x25: return mm0 | 0xf0;
        case 0x26: return mm1 | 0xf0;

        case 0x27: case 0x28: case 0x29: case 0x2a:
        case 0x2b: case 0x2c: case 0x2d: case 0x2e:
            return sc[adr - 0x27] | 0xf0;

        default:
            return 0xff;
    }
}


/*
 *  Write to VIC register
 */
ITCM_CODE void MOS6569::WriteRegister(uint16 adr, uint8 byte)
{
    switch (adr) {
        case 0x00: case 0x02: case 0x04: case 0x06:
        case 0x08: case 0x0a: case 0x0c: case 0x0e:
            mx[adr >> 1] = (mx[adr >> 1] & 0xff00) | byte;
            break;

        case 0x10:{
            int i, j;
            mx8 = byte;
            for (i=0, j=1; i<8; i++, j<<=1) {
                if (mx8 & j)
                    mx[i] |= 0x100;
                else
                    mx[i] &= 0xff;
            }
            break;
        }

        case 0x01: case 0x03: case 0x05: case 0x07:
        case 0x09: case 0x0b: case 0x0d: case 0x0f:
            my[adr >> 1] = byte;
            break;

        case 0x11:{ // Control register 1
            ctrl1 = byte;
            y_scroll = byte & 7;

            uint16 new_irq_raster = (irq_raster & 0xff) | ((byte & 0x80) << 1);
            if (irq_raster != new_irq_raster && raster_y == new_irq_raster)
                raster_irq();
            irq_raster = new_irq_raster;

            if (byte & 8) {
                dy_start = ROW25_YSTART;
                dy_stop = ROW25_YSTOP;
            } else {
                dy_start = ROW24_YSTART;
                dy_stop = ROW24_YSTOP;
            }

            display_idx = ((ctrl1 & 0x60) | (ctrl2 & 0x10)) >> 4;
            break;
        }

        case 0x12:{ // Raster counter
            uint16 new_irq_raster = (irq_raster & 0xff00) | byte;
            if (irq_raster != new_irq_raster && raster_y == new_irq_raster)
                raster_irq();
            irq_raster = new_irq_raster;
            break;
        }

        case 0x15:  // Sprite enable
            me = byte;
            break;

        case 0x16:  // Control register 2
            ctrl2 = byte;
            x_scroll = byte & 7;
            border_40_col = byte & 8;
            display_idx = ((ctrl1 & 0x60) | (ctrl2 & 0x10)) >> 4;
            break;

        case 0x17:  // Sprite Y expansion
            mye = byte;
            break;

        case 0x18:  // Memory pointers
            vbase = byte;
            matrix_base = get_physical((byte & 0xf0) << 6);
            char_base = get_physical((byte & 0x0e) << 10);
            bitmap_base = get_physical((byte & 0x08) << 10);
            break;

        case 0x19: // IRQ flags
            irq_flag = irq_flag & (~byte & 0x0f);
            the_cpu->ClearVICIRQ(); // Clear interrupt (hack!)
            if (irq_flag & irq_mask) // Set master bit if allowed interrupt still pending
                irq_flag |= 0x80;
            break;

        case 0x1a:  // IRQ mask
            irq_mask = byte & 0x0f;
            if (irq_flag & irq_mask) { // Trigger interrupt if pending and now allowed
                irq_flag |= 0x80;
                the_cpu->TriggerVICIRQ();
            } else {
                irq_flag &= 0x7f;
                the_cpu->ClearVICIRQ();
            }
            break;

        case 0x1b:  // Sprite data priority
            mdp = byte;
            break;

        case 0x1c:  // Sprite multicolor
            mmc = byte;
            break;

        case 0x1d:  // Sprite X expansion
            mxe = byte;
            break;

        case 0x20:
            extern uint8_t palette_red[16];
            extern uint8_t palette_green[16];
            extern uint8_t palette_blue[16];
            ec_color = colors[ec = byte];
            ec_color_long = (ec_color << 24) | (ec_color << 16) | (ec_color << 8) | ec_color;
            BG_PALETTE_SUB[1]=RGB15(palette_red[ec_color]>>3,palette_green[ec_color]>>3,palette_blue[ec_color]>>3);
            break;

        case 0x21:
            if (b0c != byte) {
                b0c_color = colors[b0c = byte & 0xF];
                b0c_color32 = (b0c_color << 24) | (b0c_color << 16) | (b0c_color << 8) | (b0c_color << 0);
                make_mc_table();
            }
            break;

        case 0x22:
            if (b1c != byte) {
                b1c_color = colors[b1c = byte & 0xF];
                make_mc_table();
            }
            break;

        case 0x23:
            if (b2c != byte) {
                b2c_color = colors[b2c = byte & 0xF];
                make_mc_table();
            }
            break;

        case 0x24: b3c_color = colors[b3c = byte & 0xF]; break;
        case 0x25: mm0_color = colors[mm0 = byte]; break;
        case 0x26: mm1_color = colors[mm1 = byte]; break;

        case 0x27: case 0x28: case 0x29: case 0x2a:
        case 0x2b: case 0x2c: case 0x2d: case 0x2e:
            spr_color[adr - 0x27] = colors[sc[adr - 0x27] = byte];
            break;
    }
}


/*
 *  CIA VA14/15 has changed
 */

void MOS6569::ChangedVA(uint16 new_va)
{
    cia_vabase = new_va << 14;
    WriteRegister(0x18, vbase); // Force update of memory pointers
}


/*
 *  Trigger lightpen interrupt, latch lightpen coordinates
 */

void MOS6569::TriggerLightpen(void)
{
    if (!lp_triggered) {    // Lightpen triggers only once per frame
        lp_triggered = true;

        lpx = 0;            // Latch current coordinates
        lpy = raster_y;

        irq_flag |= 0x08;   // Trigger IRQ
        if (irq_mask & 0x08) {
            irq_flag |= 0x80;
            the_cpu->TriggerVICIRQ();
        }
    }
}


/*
 *  VIC vertical blank: Reset counters and redraw screen
 */

#ifdef GLOBAL_VARS
static inline void vblank(void)
#else
inline void MOS6569::vblank(void)
#endif
{
    raster_y = vc_base = 0;
    lp_triggered = false;

    // Skip every other frame on DS-Lite/Phat
    total_frames++;
    if (isDSiMode())
    {
         extern u8 last_led_states;
         frame_skipped = false;
         if (myConfig.trueDrive && last_led_states) // If True Drive and we're accessing the drive...
         {
             frame_skipped = (total_frames & 3); // Skip 3 of 4 frames in true drive mode when accessing drive
         }
    }
    else
    {
        frame_skipped = (total_frames & 1); // Skip every other...
        if (frame_skipped)
        {
            if ((total_frames % 3) == 0) frame_skipped = 0; // But every so often toss in an odd frame
        }
    }

    the_c64->VBlank(!frame_skipped);
}


#ifdef GLOBAL_VARS
ITCM_CODE void el_std_text(uint8 *p, uint8 *q, uint8 *r)
#else
inline void MOS6569::el_std_text(uint8 *p, uint8 *q, uint8 *r)
#endif
{
    unsigned int b0cc = b0c;
    uint32 *lp = (uint32 *)p;
    uint8 *cp = color_line;
    uint8 *mp = matrix_line;

    // Loop for 40 characters
    for (int i=0; i<40; i++)
    {
        uint8 data = r[i] = q[mp[i] << 3];

        if (!data)
        {
            *lp++ = b0c_color32;
            *lp++ = b0c_color32;
        }
        else
        {
            uint8 color = cp[i];
            *lp++ = TextColorTable[color][b0cc][data>>4].b;
            *lp++ = TextColorTable[color][b0cc][data&0xf].b;
        }
    }
}


#ifdef GLOBAL_VARS
ITCM_CODE void el_mc_text(uint8 *p, uint8 *q, uint8 *r)
#else
inline void MOS6569::el_mc_text(uint8 *p, uint8 *q, uint8 *r)
#endif
{
    uint32 *wp = (uint32 *)p;
    uint8 *cp = color_line;
    uint8 *mp = matrix_line;

    // Loop for 40 characters
    for (int i=0; i<40; i++)
    {
        uint8 data = q[mp[i] << 3];

        if (cp[i] & 8) {
            r[i] = (data & 0xaa) | (data & 0xaa) >> 1;
            if (!data)
            {
                *wp++ = b0c_color32;
                *wp++ = b0c_color32;
            }
            else
            {
                uint8 color = colors[cp[i] & 7];
                mc_color_lookup[3] = color | (color << 8);
                *wp++ = mc_color_lookup[(data >> 6) & 3] | (mc_color_lookup[(data >> 4) & 3] << 16);
                *wp++ = mc_color_lookup[(data >> 2) & 3] | (mc_color_lookup[(data >> 0) & 3] << 16);
            }

        } else { // Standard mode in multicolor mode
            r[i] = data;
            if (!data)
            {
                *wp++ = b0c_color32;
                *wp++ = b0c_color32;
            }
            else
            {
                uint8 color = cp[i];
                *wp++ = TextColorTable[color][b0c][data>>4].b;
                *wp++ = TextColorTable[color][b0c][data&0xf].b;
            }
        }
    }
}


#ifdef GLOBAL_VARS
void el_std_bitmap(uint8 *p, uint8 *q, uint8 *r)
#else
inline void MOS6569::el_std_bitmap(uint8 *p, uint8 *q, uint8 *r)
#endif
{
    uint32 *lp = (uint32 *)p;
    uint8 *mp = matrix_line;

    // Loop for 40 characters
    for (int i=0; i<40; i++, q+=8)
    {
        uint8 data = r[i] = *q;
        uint8 color = mp[i] >> 4;
        uint8 bcolor = mp[i] & 15;

        *lp++ = TextColorTable[color][bcolor][data>>4].b;
        *lp++ = TextColorTable[color][bcolor][data&0xf].b;
    }
}


#ifdef GLOBAL_VARS
void el_mc_bitmap(uint8 *p, uint8 *q, uint8 *r)
#else
inline void MOS6569::el_mc_bitmap(uint8 *p, uint8 *q, uint8 *r)
#endif
{
    uint16 lookup[4];
    uint32 *wp = (uint32 *)p;
    uint8 *cp = color_line;
    uint8 *mp = matrix_line;

    lookup[0] = (b0c_color << 8) | b0c_color;
    uint32 bg32 = (b0c_color << 24) | (b0c_color << 16) | (b0c_color << 8) | b0c_color;

    // Loop for 40 characters
    for (int i=0; i<40; i++, q+=8)
    {
        uint8 color, acolor, bcolor;

        uint8 data = *q;
        r[i] = (data & 0xaa) | (data & 0xaa) >> 1;

        if (!data)
        {
            *wp++ = bg32;
            *wp++ = bg32;
        }
        else
        {
            color = mp[i] >> 4;
            lookup[1] = (color << 8) | color;
            bcolor = mp[i] & 0xf;
            lookup[2] = (bcolor << 8) | bcolor;
            acolor = cp[i] & 0xf;
            lookup[3] = (acolor << 8) | acolor;

            *wp++ = lookup[(data >> 6) & 3] | (lookup[(data >> 4) & 3] << 16);
            *wp++ = lookup[(data >> 2) & 3] | (lookup[(data >> 0) & 3] << 16);
        }
    }
}


#ifdef GLOBAL_VARS
static inline void el_ecm_text(uint8 *p, uint8 *q, uint8 *r)
#else
inline void MOS6569::el_ecm_text(uint8 *p, uint8 *q, uint8 *r)
#endif
{
    uint32 *lp = (uint32 *)p;
    uint8 *cp = color_line;
    uint8 *mp = matrix_line;
    uint8 *bcp = &b0c;

    // Loop for 40 characters
    for (int i=0; i<40; i++)
    {
        uint8 data = r[i] = mp[i];
        uint8 color = cp[i];
        uint8 bcolor = bcp[(data >> 6) & 3];

        data = q[(data & 0x3f) << 3];
        *lp++ = TextColorTable[color][bcolor][data>>4].b;
        *lp++ = TextColorTable[color][bcolor][data&0xf].b;
    }
}


#ifdef GLOBAL_VARS
ITCM_CODE void el_std_idle(uint8 *p, uint8 *r)
#else
inline void MOS6569::el_std_idle(uint8 *p, uint8 *r)
#endif
{
    uint8 data = *get_physical(ctrl1 & 0x40 ? 0x39ff : 0x3fff);
    uint32 *lp = (uint32 *)p;
    uint32 conv0 = TextColorTable[0][b0c][data>>4].b;
    uint32 conv1 = TextColorTable[0][b0c][data&0xf].b;

    for (int i=0; i<40; i++) {
        *lp++ = conv0;
        *lp++ = conv1;
        *r++ = data;
    }
}


#ifdef GLOBAL_VARS
ITCM_CODE void el_mc_idle(uint8 *p, uint8 *r)
#else
inline void MOS6569::el_mc_idle(uint8 *p, uint8 *r)
#endif
{
    uint8 data = *get_physical(0x3fff);
    uint32 *lp = (uint32 *)p - 1;
    r--;

    uint16 lookup[4];
    lookup[0] = (b0c_color << 8) | b0c_color;
    lookup[1] = lookup[2] = lookup[3] = colors[0];

    uint16 conv0 = (lookup[(data >> 6) & 3] << 16) | lookup[(data >> 4) & 3];
    uint16 conv1 = (lookup[(data >> 2) & 3] << 16) | lookup[(data >> 0) & 3];

    for (int i=0; i<40; i++) {
        *++lp = conv0;
        *++lp = conv1;
        *++r = data;
    }
}


ITCM_CODE void el_sprites(uint8 *chunky_ptr)
{
    unsigned spr_coll=0, gfx_coll=0;

    // Draw each active sprite
    for (unsigned snum = 0; snum < 8; ++snum)
    {
        uint8_t sbit = 1 << snum;

        // Is sprite visible?
        if ((sprite_on & sbit) && mx[snum] < DISPLAY_X-32)
        {
            uint8_t *p = chunky_ptr + mx[snum] + 8;
            uint8_t *q = spr_coll_buf + mx[snum] + 8;

            // Fetch sprite data and mask
            uint8_t *sdatap = get_physical(matrix_base[0x3f8 + snum] << 6 | (mc[snum]*3));
            uint32_t sdata = (*sdatap << 24) | (*(sdatap+1) << 16) | (*(sdatap+2) << 8);

            uint8_t color = spr_color[snum];

            unsigned spr_mask_pos = mx[snum] + 8 - x_scroll;    // Sprite bit position in fore_mask_buf
            unsigned sshift = spr_mask_pos & 7;

            uint8_t *fmbp = fore_mask_buf + (spr_mask_pos / 8);
            uint32_t fore_mask = (fmbp[0] << 24) | (fmbp[1] << 16) | (fmbp[2] << 8) | (fmbp[3] << 0);
            fore_mask = (fore_mask << sshift) | (fmbp[4] >> (8-sshift));

            if (mxe & sbit)        // X-expanded
            {
                if (mx[snum] >= DISPLAY_X-56)
                    continue;

                // Fetch extra sprite mask
                uint32_t sdata_l = 0, sdata_r = 0;
                uint32_t fore_mask_r = (fmbp[4] << 24) | (fmbp[5] << 16) | (fmbp[6] << 8);
                fore_mask_r <<= sshift;

                if (mmc & sbit)    // X-expanded multicolor mode
                {
                    uint32_t plane0_l, plane0_r, plane1_l, plane1_r;

                    // Expand sprite data
                    sdata_l = MultiExpTable[sdata >> 24 & 0xff] << 16 | MultiExpTable[sdata >> 16 & 0xff];
                    sdata_r = MultiExpTable[sdata >> 8 & 0xff] << 16;

                    // Convert sprite chunky pixels to bitplanes
                    plane0_l = (sdata_l & 0x55555555) | (sdata_l & 0x55555555) << 1;
                    plane1_l = (sdata_l & 0xaaaaaaaa) | (sdata_l & 0xaaaaaaaa) >> 1;
                    plane0_r = (sdata_r & 0x55555555) | (sdata_r & 0x55555555) << 1;
                    plane1_r = (sdata_r & 0xaaaaaaaa) | (sdata_r & 0xaaaaaaaa) >> 1;

                    // Collision with graphics?
                    if ((fore_mask & (plane0_l | plane1_l)) || (fore_mask_r & (plane0_r | plane1_r))) {
                        gfx_coll |= sbit;
                    }

                    // Mask sprite if in background
                    if ((mdp & sbit) == 0) {
                        fore_mask = 0;
                        fore_mask_r = 0;
                    }

                    // Paint sprite
                    for (unsigned i = 0; i < 32; ++i, plane0_l <<= 1, plane1_l <<= 1, fore_mask <<= 1)
                    {
                        uint8_t col;
                        if (plane1_l & 0x80000000)
                        {
                            if (plane0_l & 0x80000000)
                            {
                                col = mm1_color;
                            }
                            else
                            {
                                col = color;
                            }
                        }
                        else
                        {
                            if (plane0_l & 0x80000000)
                            {
                                col = mm0_color;
                            }
                            else
                            {
                                continue;
                            }
                        }

                        if (q[i]) { // Obscured by higher-priority data?
                            spr_coll |= q[i] | sbit;
                        } else if ((fore_mask & 0x80000000) == 0) {
                            p[i] = col;
                        }
                        q[i] |= sbit;
                    }
                    for (unsigned i = 32; i < 48; ++i, plane0_r <<= 1, plane1_r <<= 1, fore_mask_r <<= 1) {
                        uint8_t col;
                        if (plane1_r & 0x80000000) {
                            if (plane0_r & 0x80000000) {
                                col = mm1_color;
                            } else {
                                col = color;
                            }
                        } else {
                            if (plane0_r & 0x80000000) {
                                col = mm0_color;
                            } else {
                                continue;
                            }
                        }
                        if (q[i]) { // Obscured by higher-priority data?
                            spr_coll |= q[i] | sbit;
                        } else if ((fore_mask_r & 0x80000000) == 0) {
                            p[i] = col;
                        }
                        q[i] |= sbit;
                    }

                } else {            // X-expanded standard mode

                    // Expand sprite data
                    sdata_l = ExpTable[sdata >> 24 & 0xff] << 16 | ExpTable[sdata >> 16 & 0xff];
                    sdata_r = ExpTable[sdata >> 8 & 0xff] << 16;

                    // Collision with graphics?
                    if ((fore_mask & sdata_l) || (fore_mask_r & sdata_r)) {
                        gfx_coll |= sbit;
                    }

                    // Mask sprite if in background
                    if ((mdp & sbit) == 0) {
                        fore_mask = 0;
                        fore_mask_r = 0;
                    }

                    // Paint sprite
                    if (sdata_l)
                    for (unsigned i = 0; i < 32; ++i, sdata_l <<= 1, fore_mask <<= 1)
                    {
                        if (sdata_l & 0x80000000) {
                            if (q[i]) { // Obscured by higher-priority data?
                                spr_coll |= q[i] | sbit;
                            } else if ((fore_mask & 0x80000000) == 0) {
                                p[i] = color;
                            }
                            q[i] |= sbit;
                        }
                    }

                    if (sdata_r)
                    for (unsigned i = 32; i < 48; ++i, sdata_r <<= 1, fore_mask_r <<= 1)
                    {
                        if (sdata_r & 0x80000000)
                        {
                            if (q[i]) { // Obscured by higher-priority data?
                                spr_coll |= q[i] | sbit;
                            } else if ((fore_mask_r & 0x80000000) == 0) {
                                p[i] = color;
                            }
                            q[i] |= sbit;
                        }
                    }
                }

            }
            else   // Unexpanded
            {
                if (mmc & sbit) // Unexpanded multicolor mode
                {
                    uint32_t plane0, plane1;

                    // Convert sprite chunky pixels to bitplanes
                    plane0 = (sdata & 0x55555555) | (sdata & 0x55555555) << 1;
                    plane1 = (sdata & 0xaaaaaaaa) | (sdata & 0xaaaaaaaa) >> 1;

                    // Collision with graphics?
                    if (fore_mask & (plane0 | plane1)) {
                        gfx_coll |= sbit;
                    }

                    // Mask sprite if in background
                    if ((mdp & sbit) == 0) {
                        fore_mask = 0;
                    }

                    // Paint sprite
                    if (plane0 || plane1)
                    for (unsigned i = 0; i < 24; ++i, plane0 <<= 1, plane1 <<= 1, fore_mask <<= 1)
                    {
                        uint8_t col;
                        if (plane1 & 0x80000000)
                        {
                            if (plane0 & 0x80000000)
                            {
                                col = mm1_color;
                            }
                            else
                            {
                                col = color;
                            }
                        }
                        else
                        {
                            if (plane0 & 0x80000000)
                            {
                                col = mm0_color;
                            }
                            else
                            {
                                continue;
                            }
                        }
                        if (q[i]) { // Obscured by higher-priority data?
                            spr_coll |= q[i] | sbit;
                        } else if ((fore_mask & 0x80000000) == 0) {
                            p[i] = col;
                        }
                        q[i] |= sbit;
                    }
                }
                else    // Unexpanded standard mode
                {
                    // Collision with graphics?
                    if (fore_mask & sdata) {
                        gfx_coll |= sbit;
                    }

                    // Mask sprite if in background
                    if ((mdp & sbit) == 0) {
                        fore_mask = 0;
                    }

                    // Paint sprite
                    if (sdata)
                    for (unsigned i = 0; i < 24; ++i, sdata <<= 1, fore_mask <<= 1)
                    {
                        if (sdata & 0x80000000)
                        {
                            if (q[i]) {     // Obscured by higher-priority data?
                                spr_coll |= q[i] | sbit;
                            } else if ((fore_mask & 0x80000000) == 0) {
                                p[i] = color;
                            }
                            q[i] |= sbit;
                        }
                    }
                }
            }
        }
    }

    // Check sprite-sprite collisions
    if (spr_coll)
    {
        uint8_t old_clx_spr = clx_spr;
        clx_spr |= spr_coll;
        if (old_clx_spr == 0) { // Interrupt on first detected collision
            irq_flag |= 0x04;
            if (irq_mask & 0x04) {
                irq_flag |= 0x80;
                the_cpu->TriggerVICIRQ();
            }
        }
    }

    // Check sprite-background collisions
    if (gfx_coll)
    {
        uint8_t old_clx_bgr = clx_bgr;
        clx_bgr |= gfx_coll;
        if (old_clx_bgr == 0) { // Interrupt on first detected collision
            irq_flag |= 0x02;
            if (irq_mask & 0x02) {
                irq_flag |= 0x80;
                the_cpu->TriggerVICIRQ();
            }
        }
    }
}


#ifdef GLOBAL_VARS
ITCM_CODE int el_update_mc(int raster)
#else
inline int MOS6569::el_update_mc(int raster)
#endif
{
    int i, j;
    int cycles_used = 0;
    uint8 spron = sprite_on;
    uint8 spren = me;
    uint8 sprye = mye;
    uint8 raster8bit = raster & 0xff;

    // Increment sprite data counters
    for (i=0, j=1; i<8; i++, j<<=1)
    {
        // Sprite enabled?
        if (spren & j)
        {
            // Yes, activate if Y position matches raster counter
            if (my[i] == raster8bit)
            {
                mc[i] = 0;
                spron |= j;
            } else goto spr_off;
        }
        else
        {
spr_off:
            // No, turn sprite off when data counter exceeds 60 and increment counter
            if (mc[i] != 21) // Instead of increment by 3 and looking for 63, we inc++ and look for 21... compensate for added speed
            {
                if (sprye & j)     // Y expansion
                {
                    if (!((my[i] ^ raster8bit) & 1))
                    {
                        cycles_used++;
                        if (++mc[i] == 21) spron &= ~j;
                    }
                }
                else
                {
                    cycles_used++;
                    if (++mc[i] == 21) spron &= ~j;
                }
            }
        }
    }

    sprite_on = spron;
    return (cycles_used<<1); // Each cycles_used above is 2 actual cycles
}

/*
 *  Emulate one raster line
 */

int MOS6569::EmulateLine(void)
{
    int cycles_left = 63 + CycleDeltas[myConfig.cpuCycles];    // Cycles left for CPU
    bool is_bad_line = false;

    // Get raster counter into local variable for faster access and increment
    unsigned int raster = raster_y+1;

    // End of screen reached?
    if (raster != TOTAL_RASTERS)
    {
        raster_y = raster;
    }
    else  // Yes, enter vblank - new frame coming up
    {
        vblank();
        raster = 0;
    }

    // Trigger raster IRQ if IRQ line reached
    if (raster == irq_raster)
        raster_irq();

    // In line $30, the DEN bit controls if Bad Lines can occur
    if (raster == 0x30)
        bad_lines_enabled = ctrl1 & 0x10;

    // Skip frame? Only calculate Bad Lines then
    if (frame_skipped)
    {
        if (raster >= FIRST_DMA_LINE && raster <= LAST_DMA_LINE && ((raster & 7) == y_scroll) && bad_lines_enabled) {
            is_bad_line = true;
            cycles_left = 23 + CycleDeltas[myConfig.badCycles];
        }
        goto VIC_nop;
    }

    // Within the visible range?
    if (raster >= FIRST_DISP_LINE && raster <= LAST_DISP_LINE)
    {
        u8 bSkipDraw = 0;
        // Our output goes here
        uint8 *chunky_ptr = fast_line_buffer;
        uint32 *direct_scr_ptr = (uint32*)((u32)0x06000000 + (512*(raster-FIRST_DISP_LINE)));

        // Set video counter
        vc = vc_base;

        // Bad Line condition?
        if (raster >= FIRST_DMA_LINE && raster <= LAST_DMA_LINE && ((raster & 7) == y_scroll) && bad_lines_enabled)
        {
            // Turn on display
            display_state = is_bad_line = true;
            cycles_left = 23 + CycleDeltas[myConfig.badCycles];
            rc = 0;

            // Read and latch 40 bytes from video matrix and color RAM
            uint8 *mp = matrix_line - 1;
            uint8 *cp = color_line - 1;
            int vc1 = vc - 1;
            uint8 *mbp = matrix_base + vc1;
            uint8 *crp = color_ram + vc1;
            for (int i=0; i<40; i++)
            {
                *++mp = *++mbp;
                *++cp = *++crp;
            }
        }

        // Handler upper/lower border
        if (raster == dy_stop)
            border_on = true;
        if (raster == dy_start && (ctrl1 & 0x10)) // Don't turn off border if DEN bit cleared
            border_on = false;

        if (!border_on)
        {
            // Display window contents
            uint8 *p = chunky_ptr + COL40_XSTART;       // Pointer in chunky display buffer
            uint8 *r = fore_mask_buf + COL40_XSTART/8;  // Pointer in foreground mask buffer
#ifdef ALIGNMENT_CHECK
            uint8 *use_p = ((((int)p) & 3) == 0) ? p : text_chunky_buf;
#endif
            {
                p--;
                uint8 b0cc = b0c_color;
                int limit = x_scroll;
                for (int i=0; i<limit; i++) // Background on the left if XScroll>0
                    *++p = b0cc;
                p++;
            }

            if (display_state)
            {
                switch (display_idx)
                {
                    case 0: // Standard text
                        if (x_scroll & 3)
                        {
                            el_std_text(text_chunky_buf, char_base + rc, r);
                            // Experimentally, this is slightly faster than memcpy()
                            u32 *dest=(u32*)p;  u32 *src=(u32*)text_chunky_buf; for (int i=0; i<80; i++) *dest++ = *src++;
                        }
                        else
                        {
                            el_std_text(p, char_base + rc, r);
                        }
                        break;

                    case 1: // Multicolor text
                        if (x_scroll & 3)
                        {
                            el_mc_text(text_chunky_buf, char_base + rc, r);
                            // Experimentally, this is slightly faster than memcpy()
                            u32 *dest=(u32*)p;  u32 *src=(u32*)text_chunky_buf; for (int i=0; i<80; i++) *dest++ = *src++;
                        }
                        else
                        {
                            el_mc_text(p, char_base + rc, r);
                        }
                        break;

                    case 2: // Standard bitmap
                        if (x_scroll & 3)
                        {
                            el_std_bitmap(text_chunky_buf, bitmap_base + (vc << 3) + rc, r);
                            // Experimentally, this is slightly faster than memcpy()
                            u32 *dest=(u32*)p;  u32 *src=(u32*)text_chunky_buf; for (int i=0; i<80; i++) *dest++ = *src++;
                        }
                        else
                        {
                            el_std_bitmap(p, bitmap_base + (vc << 3) + rc, r);
                        }
                        break;

                    case 3: // Multicolor bitmap
                        if (x_scroll & 3)
                        {
                            el_mc_bitmap(text_chunky_buf, bitmap_base + (vc << 3) + rc, r);
                            // Experimentally, this is slightly faster than memcpy()
                            u32 *dest=(u32*)p;  u32 *src=(u32*)text_chunky_buf; for (int i=0; i<80; i++) *dest++ = *src++;
                        }
                        else
                        {
                            el_mc_bitmap(p, bitmap_base + (vc << 3) + rc, r);
                        }
                        break;

                    case 4: // ECM text
                        if (x_scroll & 3)
                        {
                            el_ecm_text(text_chunky_buf, char_base + rc, r);
                            // Experimentally, this is slightly faster than memcpy()
                            u32 *dest=(u32*)p;  u32 *src=(u32*)text_chunky_buf; for (int i=0; i<80; i++) *dest++ = *src++;
                        }
                        else
                        {
                            el_ecm_text(p, char_base + rc, r);
                        }
                        break;

                    default:    // Invalid mode (all black)
                        memset(p, colors[0], 320);
                        memset(r, 0, 40);
                        break;
                }
                vc += 40;

            }
            else
            {    // Idle state graphics
                switch (display_idx)
                {
                    case 0:     // Standard text
                    case 1:     // Multicolor text
                    case 4:     // ECM text
#ifndef CAN_ACCESS_UNALIGNED
#ifdef ALIGNMENT_CHECK
                        el_std_idle(use_p, r);
                        if (use_p != p) {memcpy(p, use_p, 8*40);}
#else
                        if (x_scroll & 3) {
                            el_std_idle(text_chunky_buf, r);
                            // Experimentally, this is slightly faster than memcpy()
                            u32 *dest=(u32*)p;  u32 *src=(u32*)text_chunky_buf; for (int i=0; i<80; i++) *dest++ = *src++;
                        } else
                            el_std_idle(p, r);
#endif
#else
                        el_std_idle(p, r);
#endif
                        break;

                    case 3:     // Multicolor bitmap
#ifndef CAN_ACCESS_UNALIGNED
#ifdef ALIGNMENT_CHECK
                        el_mc_idle(use_p, r);
                        if (use_p != p) {memcpy(p, use_p, 8*40);}
#else
                        if (x_scroll & 3) {
                            el_mc_idle(text_chunky_buf, r);
                            // Experimentally, this is slightly faster than memcpy()
                            u32 *dest=(u32*)p;  u32 *src=(u32*)text_chunky_buf; for (int i=0; i<80; i++) *dest++ = *src++;
                        } else
                            el_mc_idle(p, r);
#endif
#else
                        el_mc_idle(p, r);
#endif
                        break;

                    default:    // Invalid mode (all black)
                        memset(p, colors[0], 320);
                        memset(r, 0, 40);
                        break;
                }
            }

            // Draw sprites
            // Clear sprite collision buffer - but only if we have spites to draw on this line
            if (sprite_on)
            {
                memset((uint32 *)spr_coll_buf, 0x00, sizeof(spr_coll_buf));
                el_sprites(chunky_ptr);
            }

            // Handle left/right border
            uint32 *lp = (uint32 *)chunky_ptr + 4;
            uint32 c = ec_color_long;
            for (int i=5; i<COL40_XSTART/4; i++)
                *++lp = c;
            lp = (uint32 *)(chunky_ptr + COL40_XSTOP) - 1;
            for (int i=0; i<((DISPLAY_X-COL40_XSTOP)-16)/4; i++)
                *++lp = c;
            if (!border_40_col)
            {
                // Get us onto an even alignment and do 16-bits for added speed
                u16 c16 = ec_color_long & 0xFFFF;
                p = chunky_ptr + COL40_XSTART;
                if ((u32)p & 1)
                {
                    *p++ = ec_color;
                    u16 *p16 = (u16 *) p;
                    *p16++ = c16;
                    *p16++ = c16;
                    *p16   = c16;
                }
                else
                {
                    u16 *p16 = (u16 *) p;
                    *p16++ = c16;
                    *p16++ = c16;
                    *p16++ = c16;
                     p = (u8 *) p16;
                    *p = ec_color;
                }

                // Get us onto an even alignment and do 32-bits for added speed
                p = chunky_ptr + COL38_XSTOP;
                if ((u32)p & 1)
                {
                    *p++ = ec_color;
                    u32 *p32 = (u32 *) p;
                    *p32++ = c;
                    *p32 = c;
                }
                else
                {
                    u32 *p32 = (u32 *) p;
                    *p32++ = c;
                    *p32++ = c;
                     p = (u8 *) p32;
                    *p = ec_color;
                }
            }
        }
        else
        {
            // Display border - directly to screen!
            bSkipDraw = 1;
            direct_scr_ptr+=4;
            uint32 c = ec_color_long;
            for (int i=4; i<(DISPLAY_X/4)-5; i++)
                *++direct_scr_ptr = c;
        }

        // Increment row counter, go to idle state on overflow
        if (rc == 7) {
            display_state = false;
            vc_base = vc;
        } else
            rc++;

        if (raster >= FIRST_DMA_LINE-1 && raster <= LAST_DMA_LINE-1 && (((raster+1) & 7) == y_scroll) && bad_lines_enabled)
            rc = 0;

        // Not end of screen... output the next scanline as it will be 'stale' and not cached...
        // This also helps with tearing as we'll be outputting the 'stale' (last frame) line while the new frame is drawing.
        if (!frame_skipped)
        {
            if (!bSkipDraw)
            {
                the_display->UpdateRasterLine(raster, (u8*)(fast_line_buffer));
            }
        }
    }

VIC_nop:
    // Skip this if all sprites are off
    if (me | sprite_on)
        cycles_left -= el_update_mc(raster);

    return cycles_left;
}
