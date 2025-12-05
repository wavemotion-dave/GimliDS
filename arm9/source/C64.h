/*
 *  C64.h - Put the pieces together
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

#ifndef _C64_H
#define _C64_H

// Sizes of memory areas
const int C64_RAM_SIZE    = 0x10000;
const int COLOR_RAM_SIZE  = 0x400;
const int BASIC_ROM_SIZE  = 0x2000;
const int KERNAL_ROM_SIZE = 0x2000;
const int CHAR_ROM_SIZE   = 0x1000;
const int DRIVE_RAM_SIZE  = 0x800;
const int DRIVE_ROM_SIZE  = 0x4000;

#define SID_CYCLES_PER_LINE_PAL  63
#define SID_CYCLES_PER_LINE_NTSC 65
#define CIA_CYCLES_PER_LINE_PAL  63
#define CIA_CYCLES_PER_LINE_NTSC 65
#define BAD_CYCLES_PER_LINE_PAL  23
#define BAD_CYCLES_PER_LINE_NTSC 25
#define FLOPPY_CYCLES_PER_LINE   64
#define CPU_CYCLES_PER_LINE_PAL  63
#define CPU_CYCLES_PER_LINE_NTSC 65

#define MEM_TYPE_RAM            0x01
#define MEM_TYPE_KERNAL         0x02
#define MEM_TYPE_BASIC          0x03
#define MEM_TYPE_CART           0x04
#define MEM_TYPE_OTHER          0x05

class DrivePrefs;
class C64Display;
class MOS6510;
class MOS6569;
class MOS6581;
class MOS6526_1;
class MOS6526_2;
class IEC;
class MOS6502_1541;
class Job1541;
class Cartridge;
class CmdPipe;
class REU;

class C64 {
public:
    C64();
    ~C64();

    void InitMemory(void);
    void Run(void);
    void Quit(void);
    void Pause(void);
    void Resume(void);
    void Reset(void);
    void NMI(void);
    void VBlank(bool draw_frame);
    void NewPrefs(DrivePrefs *prefs);
    void PatchKernal(bool true_drive);
    void SaveRAM(char *filename);
    bool SaveSnapshot(char *filename);
    bool LoadSnapshot(char *filename);
    int SaveCPUState(FILE *f);
    int Save1541State(FILE *f);
    bool Save1541JobState(FILE *f);
    bool SaveVICState(FILE *f);
    bool SaveSIDState(FILE *f);
    bool SaveCIAState(FILE *f);
    bool SaveCARTState(FILE *f);
    bool SaveREUState(FILE *f);
    bool LoadCPUState(FILE *f);
    bool Load1541State(FILE *f);
    bool Load1541JobState(FILE *f);
    bool LoadVICState(FILE *f);
    bool LoadSIDState(FILE *f);
    bool LoadCIAState(FILE *f);
    bool LoadCARTState(FILE *f);
    bool LoadREUState(FILE *f);
    void InsertCart(char *filename);
    void RemoveCart(void);
    void LoadPRG(char *filename);
    void SetBrightness(void);

    uint8 *RAM, *Basic, *Kernal, *Char, *Color; // C64
    uint8 *RAM1541, *ROM1541;   // 1541

    C64Display *TheDisplay;

    MOS6510 *TheCPU;            // C64
    MOS6569 *TheVIC;
    MOS6581 *TheSID;
    MOS6526_1 *TheCIA1;
    MOS6526_2 *TheCIA2;
    IEC *TheIEC;
    Cartridge *TheCart;
    REU *TheREU;

    MOS6502_1541 *TheCPU1541;   // 1541
    Job1541 *TheJob1541;

private:
    void c64_ctor1(void);
    void c64_ctor2(void);
    void c64_dtor(void);
    uint8 poll_joystick(int port);
    void main_loop(void);

    bool have_a_break;      // Emulation thread shall pause

    uint8 joykey;           // Joystick keyboard emulation mask value

    uint8 orig_kernal_1d84, // Original contents of kernal locations $1d84 and $1d85
          orig_kernal_1d85; // (for undoing the Fast Reset patch)
};

extern void floppy_soundfx(u8 is_write);
extern uint8 cart_in;
extern u8 *cartROM;

extern uint8 *MemMap[0x10];
extern u8 myBASIC[];
extern u8 myKERNAL[];

#define WAITVBL swiWaitForVBlank();swiWaitForVBlank();swiWaitForVBlank();

#endif
