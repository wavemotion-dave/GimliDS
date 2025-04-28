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
 *  VIC.h - 6569R5 emulation (line based)
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

#ifndef _VIC_H
#define _VIC_H
#include <nds.h>

extern const unsigned FIRST_DISP_LINE;

// Define this if you want global variables instead of member variables
#if defined(__i386) || defined(mc68000) || defined(__MC68K__)
#define GLOBAL_VARS
#endif

// Define this if you have a processor that can do unaligned accesses quickly
#if defined(__i386) || defined(mc68000) || defined(__MC68K__)
#define CAN_ACCESS_UNALIGNED
#endif


// Total number of raster lines (PAL)
const unsigned TOTAL_RASTERS = 0x138;

// Screen refresh frequency (PAL)
const unsigned SCREEN_FREQ = 50;


class MOS6510;
class C64Display;
class C64;
struct MOS6569State;


class MOS6569 {
public:
    MOS6569(C64 *c64, C64Display *disp, MOS6510 *CPU, uint8 *RAM, uint8 *Char, uint8 *Color);

    uint8 ReadRegister(uint16 adr);
    void WriteRegister(uint16 adr, uint8 byte);
    int EmulateLine(void);
    void ChangedVA(uint16 new_va);  // CIA VA14/15 has changed
    void TriggerLightpen(void);     // Trigger lightpen interrupt
    void GetState(MOS6569State *vd);
    void SetState(MOS6569State *vd);
    void Reset(void);

private:
#ifndef GLOBAL_VARS
    void vblank(void);
    void raster_irq(void);

    uint16 mx[8];               // VIC registers
    uint8 my[8];
    uint8 mx8;
    uint8 ctrl1, ctrl2;
    uint8 lpx, lpy;
    uint8 me, mxe, mye, mdp, mmc;
    uint8 vbase;
    uint8 irq_flag, irq_mask;
    uint8 clx_spr, clx_bgr;
    uint8 ec, b0c, b1c, b2c, b3c, mm0, mm1;
    uint8 sc[8];

    uint8 *ram, *char_rom, *color_ram; // Pointers to RAM and ROM
    C64 *the_c64;               // Pointer to C64
    C64Display *the_display;    // Pointer to C64Display
    MOS6510 *the_cpu;           // Pointer to 6510

    uint8 colors[256];          // Indices of the 16 C64 colors (16 times mirrored to avoid "& 0x0f")

    uint8 ec_color, b0c_color, b1c_color,
          b2c_color, b3c_color; // Indices for exterior/background colors
    uint8 mm0_color, mm1_color; // Indices for MOB multicolors
    uint8 spr_color[8];         // Indices for MOB colors

    uint32 ec_color_long;       // ec_color expanded to 32 bits

    uint8 matrix_line[40];      // Buffer for video line, read in Bad Lines
    uint8 color_line[40];       // Buffer for color line, read in Bad Lines

    uint8 *chunky_line_start;   // Pointer to start of current line in bitmap buffer
    int xmod;                   // Number of bytes per row

    uint16 raster_y;                // Current raster line
    uint16 irq_raster;          // Interrupt raster line
    uint16 dy_start;                // Comparison values for border logic
    uint16 dy_stop;
    uint16 rc;                  // Row counter
    uint16 vc;                  // Video counter
    uint16 vc_base;             // Video counter base
    uint16 x_scroll;                // X scroll value
    uint16 y_scroll;                // Y scroll value
    uint16 cia_vabase;          // CIA VA14/15 video base

    uint16 mc[8];               // Sprite data counters

    int display_idx;            // Index of current display mode

    long pad0;  // Keep buffers long-aligned
    uint8 spr_coll_buf[DISPLAY_X];  // Buffer for sprite-sprite collisions and priorities
    uint8 fore_mask_buf[DISPLAY_X/8];   // Foreground mask for sprite-graphics collisions and priorities
#ifndef CAN_ACCESS_UNALIGNED
    uint8 text_chunky_buf[40*8];    // Line graphics buffer
#endif

    bool display_state;         // true: Display state, false: Idle state
    bool border_on;             // Flag: Upper/lower border on (Frodo SC: Main border flipflop)
    bool frame_skipped;         // Flag: Frame is being skipped
    uint8 bad_lines_enabled;    // Flag: Bad Lines enabled for this frame
    bool lp_triggered;          // Flag: Lightpen was triggered in this frame

    uint8 *get_physical(uint16 adr);
    void make_mc_table(void);
    void el_std_text(uint8 *p, uint8 *q, uint8 *r);
    void el_mc_text(uint8 *p, uint8 *q, uint8 *r);
    void el_std_bitmap(uint8 *p, uint8 *q, uint8 *r);
    void el_mc_bitmap(uint8 *p, uint8 *q, uint8 *r);
    void el_ecm_text(uint8 *p, uint8 *q, uint8 *r);
    void el_std_idle(uint8 *p, uint8 *r);
    void el_mc_idle(uint8 *p, uint8 *r);
    void el_sprites(uint8 *chunky_ptr);
    int el_update_mc(int raster);

    uint16 mc_color_lookup[4];

    bool border_40_col;         // Flag: 40 column border
    uint8 sprite_on;            // 8 flags: Sprite display/DMA active

    uint8 *matrix_base;         // Video matrix base
    uint8 *char_base;           // Character generator base
    uint8 *bitmap_base;         // Bitmap base
#endif
};


// VIC state
struct MOS6569State {
    uint8 m0x;              // Sprite coordinates
    uint8 m0y;
    uint8 m1x;
    uint8 m1y;
    uint8 m2x;
    uint8 m2y;
    uint8 m3x;
    uint8 m3y;
    uint8 m4x;
    uint8 m4y;
    uint8 m5x;
    uint8 m5y;
    uint8 m6x;
    uint8 m6y;
    uint8 m7x;
    uint8 m7y;
    uint8 mx8;

    uint8 ctrl1;            // Control registers
    uint8 raster;
    uint8 lpx;
    uint8 lpy;
    uint8 me;
    uint8 ctrl2;
    uint8 mye;
    uint8 vbase;
    uint8 irq_flag;
    uint8 irq_mask;
    uint8 mdp;
    uint8 mmc;
    uint8 mxe;
    uint8 mm;
    uint8 md;

    uint8 ec;               // Color registers
    uint8 b0c;
    uint8 b1c;
    uint8 b2c;
    uint8 b3c;
    uint8 mm0;
    uint8 mm1;
    uint8 m0c;
    uint8 m1c;
    uint8 m2c;
    uint8 m3c;
    uint8 m4c;
    uint8 m5c;
    uint8 m6c;
    uint8 m7c;
                            // Additional registers
    uint8 pad0;
    uint16 irq_raster;      // IRQ raster line
    uint16 vc;              // Video counter
    uint16 vc_base;         // Video counter base
    uint8 rc;               // Row counter
    uint8 spr_dma;          // 8 Flags: Sprite DMA active
    uint8 spr_disp;         // 8 Flags: Sprite display active
    uint8 mc[8];            // Sprite data counters
    uint8 mc_base[8];       // Sprite data counter bases
    bool display_state;     // true: Display state, false: Idle state
    bool bad_line;          // Flag: Bad Line state
    bool bad_line_enable;   // Flag: Bad Lines enabled for this frame
    bool lp_triggered;      // Flag: Lightpen was triggered in this frame
    bool border_on;         // Flag: Upper/lower border on (Frodo SC: Main border flipflop)
    u32  total_frames;  

    uint16 bank_base;       // VIC bank base address
    uint16 matrix_base;     // Video matrix base
    uint16 char_base;       // Character generator base
    uint16 bitmap_base;     // Bitmap base
    uint16 sprite_base[8];  // Sprite bases

                            // Frodo SC:
    int cycle;              // Current cycle in line (1..63)
    uint16 raster_x;        // Current raster x position
    int ml_index;           // Index in matrix/color_line[]
    uint8 ref_cnt;          // Refresh counter
    uint8 last_vic_byte;    // Last byte read by VIC
    bool ud_border_on;      // Flag: Upper/lower border on
};

#endif
