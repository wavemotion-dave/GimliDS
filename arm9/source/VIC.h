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

// Total number of raster lines (PAL)
#define TOTAL_RASTERS   312

// Screen refresh frequency (PAL)
#define SCREEN_FREQ     50

// First and last displayed line
#define FIRST_DISP_LINE 28
#define LAST_DISP_LINE  (FIRST_DISP_LINE+256)

// First and last possible line for Bad Lines
#define FIRST_DMA_LINE  0x30
#define LAST_DMA_LINE   0xf7

// Display window coordinates
#define ROW25_YSTART    0x33
#define ROW25_YSTOP     0xfb
#define ROW24_YSTART    0x37
#define ROW24_YSTOP     0xf7

#define COL40_XSTART    0x20
#define COL40_XSTOP     0x160
#define COL38_XSTART    0x27
#define COL38_XSTOP     0x157

class MOS6510;
class C64Display;
class C64;
struct MOS6569State;

extern uint8 vic_ultimax_mode;

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
    void vblank(void);
    void raster_irq(void);
    uint8 *get_physical(uint16 adr);
    void el_std_text(uint8 *p, uint8 *q, uint8 *r);
    void el_mc_text(uint8 *p, uint8 *q, uint8 *r);
    void el_std_bitmap(uint8 *p, uint8 *q, uint8 *r);
    void el_mc_bitmap(uint8 *p, uint8 *q, uint8 *r);
    void el_ecm_text(uint8 *p, uint8 *q, uint8 *r);
    void el_std_idle(uint8 *p, uint8 *r);
    void el_mc_idle(uint8 *p, uint8 *r);
    void el_sprites(uint8 *chunky_ptr);
    int el_update_mc(int raster);
    void init_text_color_table(uint8 *colors);
    void make_mc_table(void);
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
    
    uint8 spare1;
    uint8 spare2;
    uint16 spare3;
    uint32 spare4;    
};

#endif
