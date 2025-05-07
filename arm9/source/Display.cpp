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
 *  Display.cpp - C64 graphics display, emulator window handling
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

#include "sysdeps.h"

#include "Display.h"
#include "main.h"
#include "IEC.h"
#include "C64.h"
#include "Prefs.h"
#include "Cartridge.h"
#include "mainmenu.h"
#include <maxmod9.h>
#include "soundbank.h"


u8 floppy_sound_counter __attribute__((section(".dtcm"))) = 0;
u8 bDebugDisplay  __attribute__((section(".dtcm"))) = 0;

// "Colodore" palette
uint8_t palette_red[16] = {
    0x00, 0xff, 0x81, 0x75, 0x8e, 0x56, 0x2e, 0xed, 0x8e, 0x55, 0xc4, 0x4a, 0x7b, 0xa9, 0x70, 0xb2
};

uint8_t palette_green[16] = {
    0x00, 0xff, 0x33, 0xce, 0x3c, 0xac, 0x2c, 0xf1, 0x50, 0x38, 0x6c, 0x4a, 0x7b, 0xff, 0x6d, 0xb2
};

uint8_t palette_blue[16] = {
    0x00, 0xff, 0x38, 0xc8, 0x97, 0x4d, 0x9b, 0x71, 0x29, 0x00, 0x71, 0x4a, 0x7b, 0x9f, 0xeb, 0xb2
};


u8 last_drive_access_type = 0;
void floppy_soundfx(u8 type)
{
    last_drive_access_type = type;
    if (myConfig.diskSFX)
    {
        if (floppy_sound_counter == 0) floppy_sound_counter = 250;
    }
}


/*
 *  Update drive LED display (deferred until Update())
 */
u16 LastFloppySound = 0;
u8 last_led_states = 0x00;
void C64Display::UpdateLEDs(int l0, int l1)
{
    led_state[0] = l0;
    led_state[1] = l1;

    last_led_states = l0;

    if (led_state[0] == DRVLED_ERROR)
    {
        DSPrint(24, 21, 2, (char*)"CDE");   // Red Error Label
    }
    else
    {
        if (led_state[0] || led_state[1])
        {
            if (last_drive_access_type)
            {
                DSPrint(24, 21, 2, (char*)"#$%"); // Blue Activity Label (write)
            }
            else
            {
                DSPrint(24, 21, 2, (char*)"@AB"); // Green Activity Label (read or other access)
            }
        }
        else
        {
            DSPrint(24, 21, 2, (char*)" !\""); // White Idle Drive Label
            last_led_states = 0;
        }
    }
}

/*
 *  Display_NDS.i by Troy Davis(GPF), adapted from:
 *  Display_GP32.i by Mike Dawson - C64 graphics display, emulator window handling,
 *
 *  Frodo (C) 1994-1997,2002 Christian Bauer
 *  X11 stuff by Bernd Schmidt/Lutz Vieweg
 */

#include "C64.h"
#include "VIC.h"

#include <nds.h>
#include <fat.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <unistd.h>
#include "diskmenu.h"
#include "keyboard.h"
#include "soundbank.h"
#include <maxmod9.h>

#define ABS(a) (((a) < 0) ? -(a) : (a))
#define ROUND(f) ((u32) ((f) < 0.0 ? (f) - 0.5 : (f) + 0.5))

#define KB_NORMAL 0
#define KB_CAPS   1
#define KB_SHIFT  2

#define F_1 0x1
#define F_2 0x2
#define F_3 0x3
#define F_4 0x4
#define F_5 0x5
#define F_6 0x6
#define F_7 0x7
#define F_8 0x18

#define INSERT_CART 0xFD
#define MOUNT_DISK  0xFE
#define MAIN_MENU   0xFF

#define LFA 0x095 // Left arrow
#define CLR 0x147 // Home/clear
#define PND 0x92  // Pound 
#define RST 0x13  // Restore
#define RET '\n'  // Enter
#define BSP 0x08  // Backspace
#define CTL 0x21  // Ctrl
#define SPC 0x20  // Space
#define ATT 0x22  // At@
#define UPA 0x23  // UP arrow symbol
#define RUN 0x00  // RunStop
#define SLK 0x25  // Shift Lock
#define CMD 0x26  // Commodore key
#define SHF 0x27  // Shift Key
#define CUP 0x14  // Cursor up
#define CDL 0x15  // Cursor left

static int m_Mode=KB_SHIFT;

extern u8 col, row; // console cursor position

static int keystate[256];

extern u8 MainMenu(C64 *the_c64);
void show_joysticks(void);
void show_cartstatus(void);
void show_shift_key(void);

char str[300];
int bg0b, bg1b;

void ShowKeyboard(void)
{
    videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE);

    videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE); // BG0 = debug console; BG1 = keyboard
    bg0b = bgInitSub(0, BgType_Text8bpp, BgSize_T_256x256, 31,0);
    bg1b = bgInitSub(1, BgType_Text8bpp, BgSize_T_256x256, 29,0);
    bgSetPriority(bg0b,1); bgSetPriority(bg1b,0);

    decompress(keyboardTiles, bgGetGfxPtr(bg0b),  LZ77Vram);
    decompress(keyboardMap, (void*) bgGetMapPtr(bg0b),  LZ77Vram);
    dmaCopy((void*) bgGetMapPtr(bg0b)+32*30*2,(void*) bgGetMapPtr(bg1b),32*24*2);
    dmaCopy((void*) keyboardPal,(void*) BG_PALETTE_SUB,256*2);
    unsigned  short dmaVal = *(bgGetMapPtr(bg1b)+24*32);
    dmaFillWords(dmaVal | (dmaVal<<16),(void*)  bgGetMapPtr(bg1b),32*24*2);

    show_joysticks();
    show_shift_key();
    show_cartstatus();
}


/*
  C64 keyboard matrix:

    Bit 7   6   5   4   3   2   1   0
  0    CUD  F5  F3  F1  F7 CLR RET DEL
  1    SHL  E   S   Z   4   A   W   3
  2     X   T   F   C   6   D   R   5
  3     V   U   H   B   8   G   Y   7
  4     N   O   K   M   0   J   I   9
  5     ,   @   :   .   -   L   P   +
  6     /   ^   =  SHR HOM  ;   *   �
  7    R/S  Q   C= SPC  2  CTL  <-  1
*/

#define MATRIX(a,b) (((a) << 3) | (b))


/*
 *  Display constructor: Draw Speedometer/LEDs in window
 */

C64Display::C64Display(C64 *the_c64) : TheC64(the_c64)
{

}


/*
 *  Display destructor
 */

C64Display::~C64Display()
{
}


/*
 *  Prefs may have changed
 */

void C64Display::NewPrefs(Prefs *prefs)
{
    floppy_sound_counter = 50; // One seconds of no floppy sound...
}

u8 JITTER[]  __attribute__((section(".dtcm"))) = {0, 64, 128};
s16 temp_offset __attribute__((section(".dtcm"))) = 0;
u16 slide_dampen __attribute__((section(".dtcm"))) =0;
u16 vBlanks __attribute__((section(".dtcm"))) = 0;
ITCM_CODE void vblankIntr(void)
{
    vBlanks++;
    int cxBG = ((s16)myConfig.offsetX << 8);
    int cyBG = ((s16)myConfig.offsetY+temp_offset) << 8;
    int xdxBG = ((320 / myConfig.scaleX) << 8) | (320 % myConfig.scaleX) ;
    int ydyBG = ((200 / myConfig.scaleY) << 8) | (200 % myConfig.scaleY);

    REG_BG2X = cxBG;
    REG_BG2Y = cyBG;
    REG_BG3X = cxBG+JITTER[myConfig.jitter];
    REG_BG3Y = cyBG;

    REG_BG2PA = xdxBG;
    REG_BG2PD = ydyBG;
    REG_BG3PA = xdxBG;
    REG_BG3PD = ydyBG;

    if (temp_offset)
    {
        if (slide_dampen == 0)
        {
            if (temp_offset > 0) temp_offset--;
            else temp_offset++;
        }
        else
        {
            slide_dampen--;
        }
    }

    if (floppy_sound_counter)
    {
        if (floppy_sound_counter == 250)
        {
            if (myConfig.diskSFX) mmEffect(SFX_FLOPPY);
        }
        floppy_sound_counter--;
    }
}

// Toggle full 320x256
static s16 last_xScale = 0;
static s16 last_yScale = 0;
static s16 last_xOffset = 0;
static s16 last_yOffset = 0;
__attribute__ ((noinline)) void toggle_zoom(void)
{
  if (last_xScale == 0)
  {
      last_xScale  = myConfig.scaleX;
      last_yScale  = myConfig.scaleY;
      last_xOffset = myConfig.offsetX;
      last_yOffset = myConfig.offsetY;
      myConfig.scaleX  = 320;
      myConfig.scaleY  = 200;
      myConfig.offsetX = 60;
      myConfig.offsetY = 24;
  }
  else
  {
      myConfig.scaleX = last_xScale;
      myConfig.scaleY = last_yScale;
      myConfig.offsetX = last_xOffset;
      myConfig.offsetY = last_yOffset;
      last_xScale = last_yScale = 0;
      last_xOffset = last_yOffset = 0;
  }
}

extern void InterruptHandler(void);
int init_graphics(void)
{
    //set the mode for 2 text layers and two extended background layers
    powerOn(POWER_ALL_2D);

    videoSetMode(MODE_5_2D | DISPLAY_BG2_ACTIVE | DISPLAY_BG3_ACTIVE);

    bgInit(3, BgType_Bmp8, BgSize_B8_512x512, 0,0);
    bgInit(2, BgType_Bmp8, BgSize_B8_512x512, 0,0);

    REG_BLDCNT = BLEND_ALPHA | BLEND_SRC_BG2 | BLEND_DST_BG3;
    REG_BLDALPHA = (8 << 8) | 8; // 50% / 50%

    //set the first two banks as background memory and the third as sub background memory
    //D is not used..if you need a bigger background then you will need to map
    //more vram banks consecutivly (VRAM A-D are all 0x20000 bytes in size)
    vramSetPrimaryBanks(VRAM_A_MAIN_BG_0x06000000, VRAM_B_MAIN_BG_0x06020000, VRAM_C_SUB_BG , VRAM_D_LCD);

    vramSetBankD(VRAM_D_LCD);        // Not using this for video but 128K of faster RAM always useful!  Mapped at 0x06860000 -   256K Used for tape patch look-up
    vramSetBankE(VRAM_E_LCD);        // Not using this for video but 64K of faster RAM always useful!   Mapped at 0x06880000 -   ..
    vramSetBankF(VRAM_F_LCD);        // Not using this for video but 16K of faster RAM always useful!   Mapped at 0x06890000 -   ..
    vramSetBankG(VRAM_G_LCD);        // Not using this for video but 16K of faster RAM always useful!   Mapped at 0x06894000 -   ..
    vramSetBankH(VRAM_H_LCD);        // Not using this for video but 32K of faster RAM always useful!   Mapped at 0x06898000 -   ..
    vramSetBankI(VRAM_I_LCD);        // Not using this for video but 16K of faster RAM always useful!   Mapped at 0x068A0000 -   Unused - reserved for future use


    videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE); //sub bg 0 will be used to print text
    REG_BG0CNT_SUB = BG_MAP_BASE(31);
    BG_PALETTE_SUB[255] = RGB15(31,31,31);
    //consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);

    if (!fatInitDefault())
    {
        iprintf("Unable to initialize media device!");
        return -1;
    }

    chdir("/roms");
    chdir("c64");

    ShowKeyboard();

    REG_BG3CNT = BG_BMP8_512x512;

    int cxBG = (myConfig.offsetX << 8);
    int cyBG = (myConfig.offsetY) << 8;
    int xdxBG = ((320 / myConfig.scaleX) << 8) | (320 % myConfig.scaleX) ;
    int ydyBG = ((200 / myConfig.scaleY) << 8) | (200 % myConfig.scaleY);

    REG_BG2X = cxBG;
    REG_BG2Y = cyBG;
    REG_BG3X = cxBG;
    REG_BG3Y = cyBG;
    REG_BG3PA = xdxBG;
    REG_BG3PD = ydyBG;

    SetYtrigger(190); //trigger 2 lines before vsync
    irqSet(IRQ_VBLANK, vblankIntr);
    irqEnable(IRQ_VBLANK);

  return TRUE;

}

/*
 *  Redraw one raster line of the bitmap to the LCD
 */
__attribute__ ((noinline)) ITCM_CODE void C64Display::UpdateRasterLine(int raster, u8 *src)
{
    // Output the raster line to the LCD...
    u32 *dest = (uint32*)((u32)0x06000000 + (512*(raster-FIRST_DISP_LINE)));
    u32 *source = (u32*) src;
    for (int i=0; i<(DISPLAY_X-0x14)/4; i++)
    {
        *dest++ = *source++;
    }
}


/*
 *  Draw speedometer
 */

//*****************************************************************************
// Displays a message on the screen
//*****************************************************************************
void DSPrint(int iX,int iY,int iScr,char *szMessage)
{
  u16 *pusScreen,*pusMap;
  u16 usCharac;
  char *pTrTxt=szMessage;

  pusScreen=(u16*) bgGetMapPtr(bg1b) + iX + (iY<<5);
  pusMap=(u16*) (iScr == 6 ? bgGetMapPtr(bg0b)+24*32 : (iScr == 0 ? bgGetMapPtr(bg0b)+24*32 : bgGetMapPtr(bg0b)+26*32 ));

  while((*pTrTxt)!='\0' )
  {
    char ch = *pTrTxt++;
    if (ch >= 'a' && ch <= 'z') ch -= 32;   // Faster than strcpy/strtoupper

    if (((ch)<' ') || ((ch)>'_'))
      usCharac=*(pusMap);                   // Will render as a vertical bar
    else if((ch)<'@')
      usCharac=*(pusMap+(ch)-' ');          // Number from 0-9 or punctuation
    else
      usCharac=*(pusMap+32+(ch)-'@');       // Character from A-Z
    *pusScreen++=usCharac;
  }
}

void show_joysticks(void)
{
    if (myConfig.joyPort)
    {
        DSPrint(1, 3, 2, (char*)"()");
        DSPrint(1, 4, 2, (char*)"HI");
        DSPrint(3, 3, 2, (char*)"*+");
        DSPrint(3, 4, 2, (char*)"JK");
    }
    else
    {
        DSPrint(3, 3, 2, (char*)"()");
        DSPrint(3, 4, 2, (char*)"HI");
        DSPrint(1, 3, 2, (char*)"*+");
        DSPrint(1, 4, 2, (char*)"JK");
    }
}

void show_cartstatus(void)
{
    if (cart_in)
    {
        DSPrint(21, 23, 2, (char*)"PQR");
    }
    else
    {
        DSPrint(21, 23, 2, (char*)"012");
    }

    extern u8 cart_led;
    if (cart_led)
    {
        DSPrint(22, 21, 2, (char*)"3");
        cart_led--;
    }
    else
    {
        DSPrint(22, 21, 6, (char*)" ");
    }
}

void show_shift_key(void)
{
    if (m_Mode == KB_SHIFT)
    {
        DSPrint(1, 17, 2, (char*)",-");
        DSPrint(1, 18, 2, (char*)"LM");
    }
    else
    {
        DSPrint(1, 17, 2, (char*)"./");
        DSPrint(1, 18, 2, (char*)"NO");
    }
}

extern u8 *fake_heap_end;     // current heap start
extern u8 *fake_heap_start;   // current heap end

u8* getHeapStart() {return fake_heap_start;}
u8* getHeapEnd()   {return (u8*)sbrk(0);}
u8* getHeapLimit() {return fake_heap_end;}

int getMemUsed() { // returns the amount of used memory in bytes
   struct mallinfo mi = mallinfo();
   return mi.uordblks;
}

int getMemFree() { // returns the amount of free memory in bytes
   struct mallinfo mi = mallinfo();
   return mi.fordblks + (getHeapLimit() - getHeapEnd());
}


int i = 0;
int debug[8]={0,0,0,0,0,0,0,0};
void C64Display::Speedometer(int speed)
{
    char tmp[34];

    if (bDebugDisplay)
    {
        sprintf(tmp, "%-8d", speed);
        DSPrint(19, 1, 6, tmp);

        sprintf(tmp, "%-8d %-8d %-6d %-6d", debug[0],debug[1],debug[2],debug[3]);
        DSPrint(0, 0, 6, tmp);
    }

    show_joysticks();
    show_shift_key();
    show_cartstatus();
}



void C64Display::KeyPress(int key, uint8 *key_matrix, uint8 *rev_matrix) {
    int c64_byte, c64_bit, shifted;
    if(!keystate[key]) {
        keystate[key]=1;
        c64_byte=key>>3;
        c64_bit=key&7;
        shifted=key&128;
        c64_byte&=7;
        if(shifted) {
            key_matrix[6] &= 0xef;
            rev_matrix[4] &= 0xbf;
        }
        key_matrix[c64_byte]&=~(1<<c64_bit);
        rev_matrix[c64_bit]&=~(1<<c64_byte);
    }
}

void C64Display::KeyRelease(int key, uint8 *key_matrix, uint8 *rev_matrix) {
    int c64_byte, c64_bit, shifted;
    if(keystate[key]) {
        keystate[key]=0;
        c64_byte=key>>3;
        c64_bit=key&7;
        shifted=key&128;
        c64_byte&=7;
        if(shifted) {
            key_matrix[6] |= 0x10;
            rev_matrix[4] |= 0x40;
        }
        key_matrix[c64_byte]|=(1<<c64_bit);
        rev_matrix[c64_bit]|=(1<<c64_byte);
    }
}

/*
 *  Poll the keyboard
 */
int c64_key=-1;
int lastc64key=-1;
bool m_tpActive=false;
touchPosition m_tp;
u8 issue_commodore_key = 0;

void C64Display::IssueKeypress(uint8 row, uint8 col, uint8 *key_matrix, uint8 *rev_matrix)
{
    c64_key = MATRIX(row,col);
    KeyPress(c64_key, key_matrix, rev_matrix);
    lastc64key=c64_key;
}

int bDelayLoadPRG = 0;
int bDelayLoadCRT = 0;
void C64Display::PollKeyboard(uint8 *key_matrix, uint8 *rev_matrix, uint8 *joystick)
{
        // For PRG files, we wait about half-a-sec before loading in the program...
        if (bDelayLoadPRG)
        {
            if (--bDelayLoadPRG)
            {
                TheC64->LoadPRG(CartFilename);
            }
        }

        // For PRG files, we wait about half-a-sec before loading in the program...
        if (bDelayLoadCRT)
        {
            if (--bDelayLoadCRT)
            {
                TheC64->InsertCart(CartFilename);
                
                // Magic Desk requires TRUE DRIVE emulation
                Prefs *prefs = new Prefs(ThePrefs);
                strcpy(prefs->DrivePath[0], Drive8File);
                strcpy(prefs->DrivePath[1], Drive9File);
                myConfig.trueDrive = TheC64->TheCart->isTrueDriveRequired();
                prefs->TrueDrive = myConfig.trueDrive;
                TheC64->NewPrefs(prefs);
                ThePrefs = *prefs;
                delete prefs;
                
                TheC64->Reset();
            }
        }

        scanKeys();

        if (issue_commodore_key)
        {
            issue_commodore_key = 0;
            c64_key = MATRIX(7,5);
            KeyPress(c64_key, key_matrix, rev_matrix);
            lastc64key=c64_key;
        }
        else
        {
            if ((lastc64key >-1) && ((keysCurrent() & KEY_TOUCH) == 0))
            {
                KeyRelease(lastc64key, key_matrix, rev_matrix);
            }

            if ((keysCurrent() & KEY_TOUCH) == 0)   // No touch screen... reset the flag
            {
                m_tpActive = false;
            }
            else
            if ((m_tpActive == false) && (keysCurrent() & KEY_TOUCH))
            {
                touchRead(&m_tp);
                m_tpActive = true;

                unsigned short c = 0;
                int tilex, tiley;

                tilex = m_tp.px;
                tiley = m_tp.py;
                
                if (tiley > 20) // We're in the keyboard area...
                {
                    if (tiley < 44) // Big Key Row
                    {
                         if (tilex < 42)
                         {
                            myConfig.joyPort ^= 1;
                            extern void show_joysticks();
                            show_joysticks();
                         }
                        else if (tilex < 80)   c = CTL;
                        else if (tilex < 118)  c = BSP;
                        else if (tilex < 156)  c = RST;
                        else if (tilex < 194)  c = CLR;
                        else if (tilex < 255)  c = RUN;
                    }
                    else if (tiley < 74) // Number Row
                    {
                             if (tilex < 23)  c = '1';
                        else if (tilex < 42)  c = '2';
                        else if (tilex < 61)  c = '3';
                        else if (tilex < 80)  c = '4';
                        else if (tilex < 99)  c = '5';
                        else if (tilex < 118) c = '6';
                        else if (tilex < 137) c = '7';
                        else if (tilex < 156) c = '8';
                        else if (tilex < 175) c = '9';
                        else if (tilex < 194) c = '0';
                        else if (tilex < 213) c = '+';
                        else if (tilex < 233) c = '-';
                        else if (tilex < 256) c = PND;
                    }
                    else if (tiley < 104) // QWERTY Row
                    {
                             if (tilex < 23)  c = CUP;
                        else if (tilex < 42)  c = 'Q';
                        else if (tilex < 61)  c = 'W';
                        else if (tilex < 80)  c = 'E';
                        else if (tilex < 99)  c = 'R';
                        else if (tilex < 118) c = 'T';
                        else if (tilex < 137) c = 'Y';
                        else if (tilex < 156) c = 'U';
                        else if (tilex < 175) c = 'I';
                        else if (tilex < 194) c = 'O';
                        else if (tilex < 213) c = 'P';
                        else if (tilex < 233) c = '@';
                        else if (tilex < 256) c = '*';
                    }
                    else if (tiley < 134) // ASDF Row
                    {
                             if (tilex < 23)  c = CDL;
                        else if (tilex < 42)  c = 'A';
                        else if (tilex < 61)  c = 'S';
                        else if (tilex < 80)  c = 'D';
                        else if (tilex < 99)  c = 'F';
                        else if (tilex < 118) c = 'G';
                        else if (tilex < 137) c = 'H';
                        else if (tilex < 156) c = 'J';
                        else if (tilex < 175) c = 'K';
                        else if (tilex < 194) c = 'L';
                        else if (tilex < 213) c = ':';
                        else if (tilex < 233) c = ';';
                        else if (tilex < 256) c = '=';
                    }
                    else if (tiley < 164) // ZXCV Row
                    {
                             if (tilex < 23)  c = SHF;
                        else if (tilex < 42)  c = 'Z';
                        else if (tilex < 61)  c = 'X';
                        else if (tilex < 80)  c = 'C';
                        else if (tilex < 99)  c = 'V';
                        else if (tilex < 118) c = 'B';
                        else if (tilex < 137) c = 'N';
                        else if (tilex < 156) c = 'M';
                        else if (tilex < 175) c = ',';
                        else if (tilex < 194) c = '.';
                        else if (tilex < 213) c = '/';
                        else if (tilex < 256) c = RET;
                    }
                    else if (tiley < 192) // Bottom Row
                    {
                             if (tilex < 23)  c = F_1;
                        else if (tilex < 42)  c = F_3;
                        else if (tilex < 61)  c = F_5;
                        else if (tilex < 80)  c = F_7;
                        else if (tilex < 164) c = ' ';
                        else if (tilex < 193) c = INSERT_CART;
                        else if (tilex < 223) c = MOUNT_DISK;
                        else if (tilex < 256) c = MAIN_MENU;
                    }

                    if (c==MAIN_MENU)
                    {
                        TheC64->Pause();
                        MainMenu(TheC64);
                        ShowKeyboard();
                        TheC64->Resume();
                    }
                    else if (c==INSERT_CART)
                    {
                        TheC64->Pause();
                        u8 reload = mount_cart(TheC64);
                        ShowKeyboard();
                        if ((reload == 1) || (reload == 2))
                        {
                            // Remove any disks...
                            Prefs *prefs = new Prefs(ThePrefs);
                            strcpy(prefs->DrivePath[0], "");
                            strcpy(prefs->DrivePath[1], "");
                            prefs->TrueDrive = myConfig.trueDrive;
                            TheC64->NewPrefs(prefs);
                            ThePrefs = *prefs;
                            delete prefs;

                            if (reload == 1) // load cart THEN reset
                            {
                                TheC64->PatchKernal(ThePrefs.FastReset, ThePrefs.TrueDrive);
                                TheC64->Reset();
                                bDelayLoadCRT = 5; // 5 frames and load the CRT file
                            }
                            else // reload is 2 - PRG file reset FIRST
                            {
                                TheC64->PatchKernal(ThePrefs.FastReset, ThePrefs.TrueDrive);
                                TheC64->Reset();
                                bDelayLoadPRG = 10; // 10 frames and load the PRG file
                            }
                            cart_in = 1;
                        }
                        else if (reload == 3)
                        {
                            TheC64->RemoveCart();
                            TheC64->PatchKernal(ThePrefs.FastReset, ThePrefs.TrueDrive);
                            TheC64->Reset();
                            cart_in = 0;
                        }
                        TheC64->Resume();
                    }
                    else if (c==MOUNT_DISK)
                    {
                        TheC64->Pause();
                        u8 reload = mount_disk(TheC64);
                        ShowKeyboard();

                        if (reload & 0x7F)
                        {
                            kbd_buf_reset();

                            // Insert the new disk into the drive...
                            Prefs *prefs = new Prefs(ThePrefs);
                            strcpy(prefs->DrivePath[0], Drive8File);
                            strcpy(prefs->DrivePath[1], Drive9File);
                            prefs->TrueDrive = myConfig.trueDrive;
                            TheC64->NewPrefs(prefs);
                            ThePrefs = *prefs;
                            delete prefs;

                            // See if we should issue a system-wide RESET
                            if (reload == 2)
                            {
                                TheC64->RemoveCart();
                                TheC64->PatchKernal(ThePrefs.FastReset, ThePrefs.TrueDrive);
                                TheC64->Reset();
                            }
                        }
                        TheC64->Resume();

                        if (reload & 0x80)
                        {
                            extern void kbd_buf_feed(const char *s);
                            kbd_buf_feed("\rLOAD\"*\",8,1\rRUN\r");
                        }
                    }
                    else if (c != 0)
                    {
                       mmEffect(SFX_KEYCLICK);  // Play short key click for feedback...
                    }

                    if(c==RET) // Return
                    {
                        c64_key = MATRIX(0,1);
                        KeyPress(c64_key, key_matrix, rev_matrix);
                        lastc64key=c64_key;
                    } else
                    if(c==BSP) // Backspace
                    {
                        c64_key = MATRIX(0,0);
                        KeyPress(c64_key, key_matrix, rev_matrix);
                        lastc64key=c64_key;

                    } else
                    if(c==RUN)
                    {
                        mmEffect(SFX_KEYCLICK);  // Play short key click for feedback...
                        c64_key = MATRIX(7,7);
                        KeyPress(c64_key, key_matrix, rev_matrix);
                        lastc64key=c64_key;

                    } else
                    if(c==SLK || c==SHF)
                    {
                        if(m_Mode==KB_NORMAL) {
                            m_Mode = KB_SHIFT;
                        } else {
                            m_Mode = KB_NORMAL;
                        }
                        show_shift_key();
                    }
                    else
                    {
                        u8 left_arrow = 0;
                        if(c!=0x0)
                        {
                            switch (c)
                            {
                                case 'A': c64_key = MATRIX(1,2); break;
                                case 'B': c64_key = MATRIX(3,4); break;
                                case 'C': c64_key = MATRIX(2,4); break;
                                case 'D': c64_key = MATRIX(2,2); break;
                                case 'E': c64_key = MATRIX(1,6); break;
                                case 'F': c64_key = MATRIX(2,5); break;
                                case 'G': c64_key = MATRIX(3,2); break;
                                case 'H': c64_key = MATRIX(3,5); break;
                                case 'I': c64_key = MATRIX(4,1); break;
                                case 'J': c64_key = MATRIX(4,2); break;
                                case 'K': c64_key = MATRIX(4,5); break;
                                case 'L': c64_key = MATRIX(5,2); break;
                                case 'M': c64_key = MATRIX(4,4); break;
                                case 'N': c64_key = MATRIX(4,7); break;
                                case 'O': c64_key = MATRIX(4,6); break;
                                case 'P': c64_key = MATRIX(5,1); break;
                                case 'Q': c64_key = MATRIX(7,6); break;
                                case 'R': c64_key = MATRIX(2,1); break;
                                case 'S': c64_key = MATRIX(1,5); break;
                                case 'T': c64_key = MATRIX(2,6); break;
                                case 'U': c64_key = MATRIX(3,6); break;
                                case 'V': c64_key = MATRIX(3,7); break;
                                case 'W': c64_key = MATRIX(1,1); break;
                                case 'X': c64_key = MATRIX(2,7); break;
                                case 'Y': c64_key = MATRIX(3,1); break;
                                case 'Z': c64_key = MATRIX(1,4); break;

                                case ' ': c64_key = MATRIX(7,4); break;

                                case '0': c64_key = MATRIX(4,3); break;
                                case '1': c64_key = MATRIX(7,0); break;
                                case '2': c64_key = MATRIX(7,3); break;
                                case '3': c64_key = MATRIX(1,0); break;
                                case '4': c64_key = MATRIX(1,3); break;
                                case '5': c64_key = MATRIX(2,0); break;
                                case '6': c64_key = MATRIX(2,3); break;
                                case '7': c64_key = MATRIX(3,0); break;
                                case '8': c64_key = MATRIX(3,3); break;
                                case '9': c64_key = MATRIX(4,0); break;
                                case '*': c64_key = MATRIX(6,1); break;
                                case ':': c64_key = MATRIX(5,5); break;
                                case ';': c64_key = MATRIX(6,2); break;
                                case '=': c64_key = MATRIX(6,5); break;
                                case '/': c64_key = MATRIX(6,7); break;

                                case ATT: c64_key = MATRIX(5,6); break;

                                case ',': c64_key = MATRIX(5,7); break;
                                case '.': c64_key = MATRIX(5,4); break;
                                case '+': c64_key = MATRIX(5,0); break;
                                case '-': c64_key = MATRIX(5,3); break;

                                case CTL: c64_key = MATRIX(7,2); break;
                                case RST: c64_key = MATRIX(7,7); TheC64->NMI(); break;

                                case CLR: c64_key = MATRIX(6,3); break;
                                case LFA: c64_key = MATRIX(7,1); break;
                                case UPA: c64_key = MATRIX(6,6); break;
                                case PND:
                                    if (myConfig.poundKey == 0) c64_key = MATRIX(6,0);  // Pound
                                    if (myConfig.poundKey == 1) c64_key = MATRIX(7,1);  // Right Arrow
                                    if (myConfig.poundKey == 2) c64_key = MATRIX(0,7);  // Up Arrow
                                    if (myConfig.poundKey == 3) c64_key = MATRIX(7,5);  // Commodore Command
                                    break;
                                case CMD: c64_key = MATRIX(7,5); break;

                                case CUP: c64_key = MATRIX(0,7); break;
                                case CDL: c64_key = MATRIX(0,2); break;

                                case F_1: c64_key = MATRIX(0,4); break;
                                case F_3: c64_key = MATRIX(0,5); break;
                                case F_5: c64_key = MATRIX(0,6); break;
                                case F_7: c64_key = MATRIX(0,3); break;

                                default :  c64_key = -1; break;

                            }
                            if (c64_key < 0)
                                return;
                            if(m_Mode==KB_NORMAL)
                            {
                                c64_key = c64_key | 0x80;
                            }
                            KeyPress(c64_key, key_matrix, rev_matrix);
                            lastc64key=c64_key;
                            
                            if (left_arrow)
                            {
                                KeyPress(MATRIX(7,1) | 0x80, key_matrix, rev_matrix);
                            }
                        }
                    }
                }
            }
        }
}


/*
 *  Allocate C64 colors
 */

typedef struct {
    int r;
    int g;
    int b;
} plt;

static plt palette[256];

void C64Display::InitColors(uint8 *colors)
{
    int i;

    for (i = 0; i < 16; i++)
    {
        palette[i].r = palette_red[i]>>3;
        palette[i].g = palette_green[i]>>3;
        palette[i].b = palette_blue[i]>>3;
        BG_PALETTE[i]=RGB15(palette_red[i]>>3,palette_green[i]>>3,palette_blue[i]>>3);
    }

    // frodo internal 8 bit palette
    for(i=0; i<256; i++) {
        colors[i] = i & 0x0f;
    }
}


/*
 *  Show a requester (error message)
 */

char tmp[256];
long int ShowRequester(const char *a, const char *b, const char *)
{
    sprintf(tmp, "%s: %s\n", a, b);
    DSPrint(0, 0, 6, tmp);
    return 1;
}



