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
 *  main.cpp - Main program
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

#include "main.h"
#include "C64.h"
#include "Display.h"
#include "Prefs.h"

#include "mainmenu.h"
#include "intro.h"
#include <nds.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
extern int init_graphics(void);

extern void InterruptHandler(void);

// Global variables
C64 *TheC64 = NULL;     // Global C64 object
void intro_logo(void);
extern void init_maxmod(void);
extern void ShowKeyboard(void);
extern void BottomScreenMainMenu(void);

#define KERNAL_ROM_FILE "kernal.rom"
#define BASIC_ROM_FILE  "basic.rom"
#define CHAR_ROM_FILE   "char.rom"
#define DRIVE_ROM_FILE  "1541.rom"

/*
 *  Load C64 ROM files
 */

char big_path[300];
u8 Frodo::load_rom(const char *which, const char *path, uint8 *where, size_t size)
{
    sprintf(big_path, "/roms/bios/%s", path); // Try /roms/bios
    FILE *f = fopen(big_path, "rb");
    if (f)
    {
        size_t actual = fread(where, 1, size, f);
        fclose(f);
        if (actual == size) return 1;
    }
    else // Try /roms/c64
    {
        sprintf(big_path, "/roms/c64/%s", path);
        FILE *f = fopen(big_path, "rb");
        if (f)
        {
            size_t actual = fread(where, 1, size, f);
            fclose(f);
            if (actual == size) return 1;
        }
        else // Try current directory
        {
            FILE *f = fopen(path, "rb");
            if (f)
            {
                size_t actual = fread(where, 1, size, f);
                fclose(f);
                if (actual == size) return 1;
            }
        }
    }
    return 0;
}

void Frodo::load_rom_files()
{
    u8 roms_loaded = 0;
    roms_loaded += load_rom("Basic",   BASIC_ROM_FILE,  TheC64->Basic,   BASIC_ROM_SIZE);
    roms_loaded += load_rom("Kernal",  KERNAL_ROM_FILE, TheC64->Kernal,  KERNAL_ROM_SIZE);
    roms_loaded += load_rom("Char",    CHAR_ROM_FILE,   TheC64->Char,    CHAR_ROM_SIZE);
    roms_loaded += load_rom("1541",    DRIVE_ROM_FILE,  TheC64->ROM1541, DRIVE_ROM_SIZE);

    if (roms_loaded != 4)
    {
        BottomScreenMainMenu();

        DSPrint(0,5, 6, (char*) "ONE OR MORE BIOS ROMS NOT FOUND ");
        DSPrint(0,7, 6, (char*) "THIS EMULATOR REQUIRES ORIGINAL ");
        DSPrint(0,8, 6, (char*) "C64 BIOS ROMS AS FOLLOWS:       ");
        DSPrint(0,10,6, (char*) "KERNAL.ROM   8K  CRC32:dbe3e7c7 ");
        DSPrint(0,11,6, (char*) "BASIC.ROM    8K  CRC32:f833d117 ");
        DSPrint(0,12,6, (char*) "CHAR.ROM     4K  CRC32:ec4272ee ");
        DSPrint(0,13,6, (char*) "1541.ROM    16K  CRC32:899fa3c5 ");
        DSPrint(0,15,6, (char*) "PLACE THESE EXACTLY NAMED ROMS  ");
        DSPrint(0,16,6, (char*) "IN /ROMS/BIOS or /ROMS/C64 or   ");
        DSPrint(0,17,6, (char*) "IN THE SAME DIRECTORY AS THE EMU");
        while(1) asm("nop");
    }
}

/*
 *  Create application object and start it
 */
extern "C" {
int main(int argc, char **argv)
{
    Frodo *the_app;

    defaultExceptionHandler();

    intro_logo();

    if (!init_graphics())
        return 0;

    LoadConfig();

    the_app = new Frodo();

    the_app->ArgvReceived(argc, argv);

    keysSetRepeat(15, 6);

    the_app->ReadyToRun();
    
    delete the_app;

    return (1);
}


/*
 *  Constructor: Initialize member variables
 */

Frodo::Frodo()
{
    TheC64 = NULL;
}


/*
 *  Process command line arguments
 */
char cmd_line_file[256];
void Frodo::ArgvReceived(int argc, char **argv)
{
    if (argc > 1)
    {
        //  We want to start in the directory where the file is being launched...
        if  (strchr(argv[1], '/') != NULL)
        {
            static char  path[128];
            strcpy(path,  argv[1]);
            char  *ptr = &path[strlen(path)-1];
            while (*ptr !=  '/') ptr--;
            ptr++;
            strcpy(cmd_line_file,  ptr);
            *ptr=0;
            chdir(path);
        }
        else
        {
            strcpy(cmd_line_file,  argv[1]);
        }
        
        ///TODO: Unsure what do do here... we could auto-load and auto-launch the disk/CRT file I guess...
    }
}


/*
 *  Arguments processed, run emulation
 */

void Frodo::ReadyToRun(void)
{
    // Create and start C64
    TheC64 = new C64;

    load_rom_files();

    TheC64->Run();
    delete TheC64;
}


}

bool IsDirectory(const char *path){
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

#include <maxmod9.h>
#include "soundbank.h"

u16 vusCptVBL=0;
void vblankIntro()
{
  vusCptVBL++;
}

/******************************************************************************
* Routine FadeToColor :  Fade from background to black or white
******************************************************************************/
void FadeToColor(unsigned char ucSens, unsigned short ucBG, unsigned char ucScr, unsigned char valEnd, unsigned char uWait)
{
  unsigned short ucFade;
  unsigned char ucBcl;

  // Fade-out to black
  if (ucScr & 0x01) REG_BLDCNT=ucBG;
  if (ucScr & 0x02) REG_BLDCNT_SUB=ucBG;
  if (ucSens == 1) {
    for(ucFade=0;ucFade<valEnd;ucFade++) {
      if (ucScr & 0x01) REG_BLDY=ucFade;
      if (ucScr & 0x02) REG_BLDY_SUB=ucFade;
      for (ucBcl=0;ucBcl<uWait;ucBcl++) {
        swiWaitForVBlank();
      }
    }
  }
  else {
    for(ucFade=16;ucFade>valEnd;ucFade--) {
      if (ucScr & 0x01) REG_BLDY=ucFade;
      if (ucScr & 0x02) REG_BLDY_SUB=ucFade;
      for (ucBcl=0;ucBcl<uWait;ucBcl++) {
        swiWaitForVBlank();
      }
    }
  }
}



// --------------------------------------------------------------
// Intro with GimliDS logo and wavemotion github banner...
// --------------------------------------------------------------
void intro_logo(void)
{
  bool bOK;

  // Init graphics
  videoSetMode(MODE_0_2D | DISPLAY_BG0_ACTIVE );
  videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE );
  vramSetBankA(VRAM_A_MAIN_BG); vramSetBankC(VRAM_C_SUB_BG);
  irqSet(IRQ_VBLANK, vblankIntro);
  irqEnable(IRQ_VBLANK);

  // Init BG
  int bg1 = bgInit(0, BgType_Text8bpp, BgSize_T_256x256, 31,0);

  REG_BLDCNT = BLEND_FADE_BLACK | BLEND_SRC_BG0 | BLEND_DST_BG0; REG_BLDY = 16;

  init_maxmod();
  mmEffect(SFX_MUS_INTRO);

  // Show intro logo...
  decompress(introTiles, bgGetGfxPtr(bg1), LZ77Vram);
  decompress(introMap, (void*) bgGetMapPtr(bg1), LZ77Vram);
  dmaCopy((void *) introPal,(u16*) BG_PALETTE,256*2);

  FadeToColor(0,BLEND_FADE_BLACK | BLEND_SRC_BG0 | BLEND_DST_BG0,3,0,3);

  bOK=false;
  while (!bOK) { if ( !(keysCurrent() & 0x1FFF) ) bOK=true; } // 0x1FFF = key or touch screen
  vusCptVBL=0;bOK=false;
  while (!bOK && (vusCptVBL<5*60)) { if (keysCurrent() & 0x1FFF ) bOK=true; }
  bOK=false;
  while (!bOK) { if ( !(keysCurrent() & 0x1FFF) ) bOK=true; }

  memset((u8*)0x06000000, 0x00, 0x20000);    // Reset VRAM to 0x00 to clear any potential display garbage on way out
}
