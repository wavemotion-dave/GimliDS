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
 *  C64.cpp - Put the pieces together
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

#include "C64.h"
#include "CPUC64.h"
#include "CPU1541.h"
#include "VIC.h"
#include "SID.h"
#include "CIA.h"
#include "IEC.h"
#include "REU.h"
#include "1541gcr.h"
#include "Display.h"
#include "Cartridge.h"
#include "Prefs.h"
#include "mainmenu.h"
#include "lzav.h"
#include <maxmod9.h>
#include "soundbank.h"
#include "printf.h"

// Slight speed improvement to have these in global memory where the address is fixed at linker time
uint8 myRAM[C64_RAM_SIZE];
uint8 myKERNAL[KERNAL_ROM_SIZE];
uint8 myBASIC[BASIC_ROM_SIZE];
uint8 myRAM1541[DRIVE_RAM_SIZE] __attribute__((section(".dtcm")));  // Small enough to keep in fast memory
uint8 myCOLOR[0x400]            __attribute__((section(".dtcm")));  // Small enough to keep in fast memory

uint8 bTurboWarp __attribute__((section(".dtcm"))) = 0; // Run the CPU as fast as possible
uint8 cart_in    __attribute__((section(".dtcm"))) = 0; // Will be set to '1' if CART is inserted

MOS6510 myCPU    __attribute__((section(".dtcm")));  // Put the entire CPU object into fast memory...

C64 *gTheC64 = nullptr; // For occasional access in other classes without having to pass it around

u8 CompressBuffer[300*1024]; //300K more than enough (might need to compress RDU at 256K)

#define SNAPSHOT_VERSION 4

/*
 *  Constructor: Allocate objects and memory
 */
C64::C64()
{
    have_a_break = false;

    gTheC64 = this;

    // System-dependent things
    c64_ctor1();

    // Open display
    TheDisplay = new C64Display(this);

    // Allocate RAM/ROM memory
    RAM = myRAM;
    Basic = myBASIC;
    Kernal = myKERNAL;
    Char = new uint8[CHAR_ROM_SIZE];
    Color = myCOLOR;
    RAM1541 = myRAM1541;
    ROM1541 = new uint8[DRIVE_ROM_SIZE];

    // Create the chips
    TheCPU = &myCPU;
    TheCPU->Init(this, RAM, Basic, Kernal, Char, Color);

    TheJob1541 = new Job1541(RAM1541);
    TheCPU1541 = new MOS6502_1541(this, TheJob1541, TheDisplay, RAM1541, ROM1541);

    TheVIC  = TheCPU->TheVIC  = new MOS6569(this, TheDisplay, TheCPU, RAM, Char, Color);
    TheSID  = TheCPU->TheSID  = new MOS6581(this);
    TheCIA1 = TheCPU->TheCIA1 = new MOS6526_1(TheCPU, TheVIC);
    TheCIA2 = TheCPU->TheCIA2 = TheCPU1541->TheCIA2 = new MOS6526_2(TheCPU, TheVIC, TheCPU1541);
    TheIEC  = TheCPU->TheIEC  = new IEC(TheDisplay);
    TheCart = TheCPU->TheCart = new Cartridge();
    TheREU  = TheCPU->TheREU  = new REU(TheCPU);

    // Initialize main C64 memory
    InitMemory();

    // Clear joykey
    joykey = 0xff;

    // System-dependent things
    c64_ctor2();
}

void C64::InitMemory(void)
{
    // Clear all of memory...
    memset(RAM, 0x00, sizeof(myRAM));

    // Then Initialize RAM with powerup pattern
    // Sampled from a PAL C64 (Assy 250425) with Fujitsu MB8264A-15 DRAM chips
    uint8_t *p = RAM;
    for (unsigned i = 0; i < 512; ++i) {
        for (unsigned j = 0; j < 64; ++j) {
            if (j == 4 || j == 5) {
                *p++ = (i & 1) ? 0x03 : 0x01;   // Unstable
            } else if (j == 7) {
                *p++ = 0x07;                    // Unstable
            } else if (j == 32 || j == 57 || j == 58) {
                *p++ = 0xff;
            } else if (j == 55) {
                *p++ = (i & 1) ? 0x07 : 0x05;   // Unstable
            } else if (j == 56) {
                *p++ = (i & 1) ? 0x2f : 0x27;
            } else if (j == 59) {
                *p++ = 0x10;
            } else if (j == 60) {
                *p++ = 0x05;
            } else {
                *p++ = 0x00;
            }
        }
        for (unsigned j = 0; j < 64; ++j) {
            if (j == 36) {
                *p++ = 0xfb;
            } else if (j == 63) {
                *p++ = (i & 1) ? 0xff : 0x7c;   // Unstable
            } else {
                *p++ = 0xff;
            }
        }
    }

    // Initialize color RAM with random values
    p = Color;
    for (unsigned i=0; i<COLOR_RAM_SIZE; i++)
        *p++ = rand() & 0x0f;

    // Clear 1541 RAM
    memset(RAM1541, 0, DRIVE_RAM_SIZE);
}

/*
 *  Destructor: Delete all objects
 */

C64::~C64()
{
    delete TheJob1541;
    delete TheIEC;
    delete TheCIA2;
    delete TheCIA1;
    delete TheSID;
    delete TheVIC;
    delete TheCPU1541;
    delete TheDisplay;
    delete TheREU;

    delete[] Char;
    delete[] ROM1541;

    c64_dtor();
}


/*
 *  Reset C64
 */

void C64::Reset(void)
{
    InitMemory();
    TheCPU->AsyncReset();
    TheCPU1541->AsyncReset();
    TheJob1541->Reset();
    TheSID->Reset();
    TheCIA1->Reset();
    TheCIA2->Reset();
    TheIEC->Reset();
    TheVIC->Reset();
    TheCart->Reset();
    if (myConfig.reuType) TheREU->Reset();

    bTurboWarp = 0;
   
}

/*
 *  NMI C64
 */

void C64::NMI(void)
{
    TheCPU->AsyncNMI();
}

/*
 *  Inject PRG file directly into memory
 */

void C64::LoadPRG(char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (fp)
    {
        int prg_size = fread(CompressBuffer, 1, sizeof(CompressBuffer), fp);
        fclose(fp);

        uint8 start_hi, start_lo;
        uint16 start;

        u8 *prg = CompressBuffer;
        start_lo=*prg++;
        start_hi=*prg++;
        start=(start_hi<<8)+start_lo;

        for(int i=0; i<(prg_size-2); i++)
        {
            myRAM[start+i]=prg[i];
        }        
    }
}

/*
 *  The preferences have changed. prefs is a pointer to the new
 *   preferences, ThePrefs still holds the previous ones.
 *   The emulation must be in the paused state!
 */
void C64::NewPrefs(Prefs *prefs)
{
    PatchKernal(prefs->FastReset, prefs->TrueDrive);
    TheDisplay->NewPrefs(prefs);

    // Changed order of calls. If 1541 mode hasn't changed the order is insignificant.
    if (prefs->TrueDrive)
    {
        // New prefs have 1541 enabled ==> if old prefs had disabled free drives FIRST
        TheIEC->NewPrefs(prefs);
        TheJob1541->NewPrefs(prefs);
    }
    else
    {
        // New prefs has 1541 disabled ==> if old prefs had enabled free job FIRST
        TheJob1541->NewPrefs(prefs);
        TheIEC->NewPrefs(prefs);
    }

    TheSID->NewPrefs(prefs);

    // Reset 1541 processor if turned on or off (to bring IEC lines back to sane state)
    if (ThePrefs.TrueDrive != prefs->TrueDrive)
    {
        TheCPU1541->AsyncReset();
    }
}


/*
 *  Patch kernal IEC routines
 */

void C64::PatchKernal(bool fast_reset, bool true_drive)
{
    if (fast_reset) {
        Kernal[0x1d84] = 0xa0;
        Kernal[0x1d85] = 0x00;
    } else {
        Kernal[0x1d84] = orig_kernal_1d84;
        Kernal[0x1d85] = orig_kernal_1d85;
    }

    if (true_drive) {
        Kernal[0x0d40] = 0x78;
        Kernal[0x0d41] = 0x20;
        Kernal[0x0d23] = 0x78;
        Kernal[0x0d24] = 0x20;
        Kernal[0x0d36] = 0x78;
        Kernal[0x0d37] = 0x20;
        Kernal[0x0e13] = 0x78;
        Kernal[0x0e14] = 0xa9;
        Kernal[0x0def] = 0x78;
        Kernal[0x0df0] = 0x20;
        Kernal[0x0dbe] = 0xad;
        Kernal[0x0dbf] = 0x00;
        Kernal[0x0dcc] = 0x78;
        Kernal[0x0dcd] = 0x20;
        Kernal[0x0e03] = 0x20;
        Kernal[0x0e04] = 0xbe;
    } else {
        Kernal[0x0d40] = 0xf2;  // IECOut
        Kernal[0x0d41] = 0x00;
        Kernal[0x0d23] = 0xf2;  // IECOutATN
        Kernal[0x0d24] = 0x01;
        Kernal[0x0d36] = 0xf2;  // IECOutSec
        Kernal[0x0d37] = 0x02;
        Kernal[0x0e13] = 0xf2;  // IECIn
        Kernal[0x0e14] = 0x03;
        Kernal[0x0def] = 0xf2;  // IECSetATN
        Kernal[0x0df0] = 0x04;
        Kernal[0x0dbe] = 0xf2;  // IECRelATN
        Kernal[0x0dbf] = 0x05;
        Kernal[0x0dcc] = 0xf2;  // IECTurnaround
        Kernal[0x0dcd] = 0x06;
        Kernal[0x0e03] = 0xf2;  // IECRelease
        Kernal[0x0e04] = 0x07;
    }

    // 1541 - Fast Reset
    ROM1541[0x2ab1] = 0xfb;     // Skip zero page test, just clear
    ROM1541[0x2ab2] = 0x4c;     // Skip zero page test, just clear
    ROM1541[0x2ab3] = 0xc9;     // Skip zero page test, just clear
    ROM1541[0x2ab4] = 0xea;     // Skip zero page test, just clear

    ROM1541[0x2ad1] = 0x4c;     // Skip ROM test
    ROM1541[0x2ad2] = 0xea;     // Skip ROM test
    ROM1541[0x2ad3] = 0xea;     // Skip ROM test

    ROM1541[0x2b00] = 0x4c;     // Skip RAM test
    ROM1541[0x2b01] = 0x22;     // Skip RAM test
    ROM1541[0x2b02] = 0xeb;     // Skip RAM test

    ROM1541[0x2af2] = 0xea;     // ... Just clear
    ROM1541[0x2af3] = 0xea;     // ... Just clear
    ROM1541[0x2af4] = 0xa9;     // ... Just clear
    ROM1541[0x2af5] = 0x00;     // ... Just clear


    // 1541
    ROM1541[0x2ae4] = 0xea;     // Don't check ROM checksum
    ROM1541[0x2ae5] = 0xea;
    ROM1541[0x2ae8] = 0xea;
    ROM1541[0x2ae9] = 0xea;
    ROM1541[0x2c9b] = 0xf2;     // DOS idle loop
    ROM1541[0x2c9c] = 0x00;
    ROM1541[0x3594] = 0x20;     // Write sector
    ROM1541[0x3595] = 0xf2;
    ROM1541[0x3596] = 0xf5;
    ROM1541[0x3597] = 0xf2;
    ROM1541[0x3598] = 0x01;
    ROM1541[0x3b0c] = 0xf2;     // Format track
    ROM1541[0x3b0d] = 0x02;
}


/*
 *  Save CPU state to snapshot
 *
 *  0: Error
 *  1: OK
 *  -1: Instruction not completed
 */

int C64::SaveCPUState(FILE *f)
{
    MOS6510State state;
    TheCPU->GetState(&state);

    if (!state.instruction_complete)
        return -1;

    // ---------------------------------------------------------
    // Compress the RAM data using 'high' compression ratio...
    // ---------------------------------------------------------
    int max_len = lzav_compress_bound_hi(0x10000);
    int comp_len = lzav_compress_hi(RAM, CompressBuffer, 0x10000, max_len);

    int i = 0;
    i += fwrite(&comp_len,        sizeof(comp_len), 1, f);
    i += fwrite(&CompressBuffer,  comp_len,         1, f);
    i += fwrite(Color,            0x400,            1, f);
    i += fwrite((void*)&state,    sizeof(state),    1, f);

    return i == 4;
}


/*
 *  Load CPU state from snapshot
 */

bool C64::LoadCPUState(FILE *f)
{
    MOS6510State state;

    int comp_len = 0;
    int i = 0;
    i += fread(&comp_len,       sizeof(comp_len), 1, f);
    i += fread(CompressBuffer,  comp_len,         1, f);
    i += fread(Color, 0x400, 1, f);
    i += fread((void*)&state, sizeof(state), 1, f);

    if (i == 4)
    {
        // ------------------------------------------------------------------
        // Decompress the previously compressed RAM and put it back into the
        // right memory location... this is quite fast all things considered.
        // ------------------------------------------------------------------
        (void)lzav_decompress( CompressBuffer, RAM, comp_len, 0x10000 );

        TheCPU->SetState(&state);
        return true;
    }
    else
    { iprintf("LoadCPUState Failed"); return false;}
}


/*
 *  Save 1541 state to snapshot
 *
 *  0: Error
 *  1: OK
 *  -1: Instruction not completed
 */

int C64::Save1541State(FILE *f)
{
    MOS6502State state;
    TheCPU1541->GetState(&state);

    if (!state.idle && !state.instruction_complete)
        return -1;

    int i = fwrite(RAM1541, 0x800, 1, f);
    i += fwrite((void*)&state, sizeof(state), 1, f);

    return i == 2;
}


/*
 *  Load 1541 state from snapshot
 */

bool C64::Load1541State(FILE *f)
{
    MOS6502State state;

    int i = fread(RAM1541, 0x800, 1, f);
    i += fread((void*)&state, sizeof(state), 1, f);
    if (i == 2)
    {
        TheCPU1541->SetState(&state);
        return true;
    } else
    { iprintf("Load1541State\n"); return false;}
}


/*
 *  Save VIC state to snapshot
 */

bool C64::SaveVICState(FILE *f)
{
    MOS6569State state;
    TheVIC->GetState(&state);
    return fwrite((void*)&state, sizeof(state), 1, f) == 1;
}


/*
 *  Load VIC state from snapshot
 */

bool C64::LoadVICState(FILE *f)
{
    MOS6569State state;

    int k = fread((void*)&state, sizeof(state), 1, f);
    if (k == 1)
    {
        TheVIC->SetState(&state);
        return true;
    } else
    { iprintf("LoadVICState %d\n",k); return false;}
}


/*
 *  Save SID state to snapshot
 */

bool C64::SaveSIDState(FILE *f)
{
    MOS6581State state;
    TheSID->GetState(&state);
    return fwrite((void*)&state, sizeof(state), 1, f) == 1;
}


/*
 *  Load SID state from snapshot
 */

bool C64::LoadSIDState(FILE *f)
{
    MOS6581State state;

    if (fread((void*)&state, sizeof(state), 1, f) == 1)
    {
        TheSID->SetState(&state);
        return true;
    } else
    { iprintf("LoadSIDState\n"); return false;}
}


/*
 *  Save CIA states to snapshot
 */

bool C64::SaveCIAState(FILE *f)
{
    MOS6526State state;
    TheCIA1->GetState(&state);

    if (fwrite((void*)&state, sizeof(state), 1, f) == 1)
    {
        TheCIA2->GetState(&state);
        return fwrite((void*)&state, sizeof(state), 1, f) == 1;
    }
    else
    {
        return false;
    }
}


/*
 *  Load CIA states from snapshot
 */

bool C64::LoadCIAState(FILE *f)
{
    MOS6526State state;

    if (fread((void*)&state, sizeof(state), 1, f) == 1)
    {
        TheCIA1->SetState(&state);
        if (fread((void*)&state, sizeof(state), 1, f) == 1)
        {
            TheCIA2->SetState(&state);
            return true;
        } else { iprintf("LoadCIAState1\n"); return false;}
    } else { iprintf("LoadCIAState2\n"); return false;}
}

/*
 *  Save Cartridge state to snapshot
 */

bool C64::SaveCARTState(FILE *f)
{
    CartridgeState state;
    TheCart->GetState(&state);
    return fwrite((void*)&state, sizeof(state), 1, f) == 1;
}

/*
 *  Load Cartridge state from snapshot
 */

bool C64::LoadCARTState(FILE *f)
{
    CartridgeState state;

    if (fread((void*)&state, sizeof(state), 1, f) == 1)
    {
        TheCart->SetState(&state);
        return true;
    } else { iprintf("LoadCARTState\n"); return false;}
}


/*
 *  Save REU state to snapshot
 */

bool C64::SaveREUState(FILE *f)
{
    if (myConfig.reuType)
    {
        REUState state;
        TheREU->GetState(&state);

        // ---------------------------------------------------------
        // Compress the REU RAM data using 'high' compression ratio...
        // ---------------------------------------------------------
        int max_len = lzav_compress_bound_hi(256*1024);
        int comp_len = lzav_compress_hi(REU_RAM, CompressBuffer, 256*1024, max_len);

        int i = 0;
        i += fwrite(&comp_len,        sizeof(comp_len), 1, f);
        i += fwrite(&CompressBuffer,  comp_len,         1, f);
        i += fwrite((void*)&state,    sizeof(state),    1, f);

        return (i == 3) ? true:false;
    }
    else
    {
        return true;
    }
}

/*
 *  Load REU state from snapshot
 */

bool C64::LoadREUState(FILE *f)
{
    REUState state;

    if (myConfig.reuType)
    {
        int comp_len = 0;
        int i = 0;
        i += fread(&comp_len,       sizeof(comp_len), 1, f);
        i += fread(CompressBuffer,  comp_len,         1, f);
        i += fread((void*)&state,   sizeof(state),    1, f);

        if (i == 3)
        {
            // ---------------------------------------------------------------------
            // Decompress the previously compressed REU_RAM and put it back into the
            // right memory location... this is quite fast all things considered.
            // ---------------------------------------------------------------------
            (void)lzav_decompress( CompressBuffer, REU_RAM, comp_len, 256*1024 );

            TheREU->SetState(&state);
            return true;
        }
        else
        { iprintf("LoadREUState Failed"); return false;}
    }
    else
    {
        return true;
    }
}


/*
 *  Save 1541 GCR state to snapshot
 */

bool C64::Save1541JobState(FILE *f)
{
    Job1541State state;
    TheJob1541->GetState(&state);
    return fwrite((void*)&state, sizeof(state), 1, f) == 1;
}


/*
 *  Load 1541 GCR state from snapshot
 */

bool C64::Load1541JobState(FILE *f)
{
    Job1541State state;

    if (fread((void*)&state, sizeof(state), 1, f) == 1)
    {
        TheJob1541->SetState(&state);
        return true;
    } else { iprintf("Load1541JobState\n"); return false;}
}


#define SNAPSHOT_HEADER  "GimliSnapshot"
#define SNAPSHOT_1541    1

/*
 *  Save snapshot (emulation must be paused and in VBlank)
 *
 *  To be able to use SC snapshots with SL, SC snapshots are made thus that no
 *  partially dealt with instructions are saved. Instead all devices are advanced
 *  cycle by cycle until the current instruction has been finished. The number of
 *  cycles this takes is saved in the snapshot and will be reconstructed if the
 *  snapshot is loaded into FrodoSC again.
 */

bool C64::SaveSnapshot(char *filename)
{
    FILE *f;
    uint8 flags;

    if (strlen(filename) < 5) return false;

    if ((f = fopen(filename, "wb")) == NULL)
    {
        //ShowRequester("Unable to open snapshot file", "OK", NULL);
        return false;
    }

    fprintf(f, "%s%c", SNAPSHOT_HEADER, 10);
    fputc(SNAPSHOT_VERSION, f); // Version number
    flags = 0;
    if (ThePrefs.TrueDrive) flags |= SNAPSHOT_1541;
    fputc(flags, f);
    bool bVICSave  = SaveVICState(f);
    bool bSIDSave  = SaveSIDState(f);
    bool bCIASave  = SaveCIAState(f);
    bool bCPUSave  = SaveCPUState(f);
    bool bCARTSave = SaveCARTState(f);
    bool bREUSave  = SaveREUState(f);
    fputc(0, f);        // No delay

    if (ThePrefs.TrueDrive)
    {
        fwrite(ThePrefs.DrivePath[0], 256, 1, f);
        Save1541State(f);
        fputc(0, f);    // No delay
        Save1541JobState(f);
    }
    fclose(f);

    if (bVICSave && bSIDSave && bCIASave && bCPUSave && bCARTSave && bREUSave) return true;
    return false;
}


/*
 *  Load snapshot (emulation must be paused and in VBlank)
 */

bool C64::LoadSnapshot(char *filename)
{
    uint8 delay;
    FILE *f;

    if ((f = fopen(filename, "rb")) != NULL) {
        char Header[] = SNAPSHOT_HEADER;
        char *b = Header, c = 0;

        // For some reason memcmp()/strcmp() and so forth utterly fail here.
        while (*b > 32) {
            if ((c = fgetc(f)) != *b++) {
                b = NULL;
                break;
            }
        }
        if (b != NULL) {
            uint8 flags;
            bool error = false;
            long vicptr;    // File offset of VIC data

            while (c != 10)
                c = fgetc(f);   // Shouldn't be necessary
            if (fgetc(f) != SNAPSHOT_VERSION) {
                fclose(f);
                return false;
            }
            flags = fgetc(f);
            vicptr = ftell(f);

            error |= !LoadVICState(f);
            error |= !LoadSIDState(f);
            error |= !LoadCIAState(f);
            error |= !LoadCPUState(f);
            error |= !LoadCARTState(f);
            error |= !LoadREUState(f);

            delay = fgetc(f);   // Number of cycles the 6510 is ahead of the previous chips
            (void)delay;

            if ((flags & SNAPSHOT_1541) != 0)
            {
                Prefs *prefs = new Prefs(ThePrefs);

                // First switch on emulation
                int k=fread(prefs->DrivePath[0], 256, 1, f);
                if (k==1)
                {
                    error |=0;
                }
                else
                {
                    error |=1;
                    iprintf("flags & SNAPSHOT_1541\n");
                }
                prefs->TrueDrive = true;
                NewPrefs(prefs);
                ThePrefs = *prefs;
                delete prefs;

                // Then read the context
                error |= !Load1541State(f);

                delay = fgetc(f);   // Number of cycles the 6502 is ahead of the previous chips
                Load1541JobState(f);
            } else if (ThePrefs.TrueDrive) {    // No emulation in snapshot, but currently active?
                Prefs *prefs = new Prefs(ThePrefs);
                prefs->TrueDrive = false;
                NewPrefs(prefs);
                ThePrefs = *prefs;
                delete prefs;
            }

            fseek(f, vicptr, SEEK_SET);
            LoadVICState(f);    // Load VIC data twice in SL (is REALLY necessary sometimes!)
            fclose(f);

            if (error) {
                Reset();
                return false;
            } else
                return true;
        } else {
            fclose(f);
            return false;
        }
    } else {
        return false;
    }
}

/*
 *  C64_GP32.i by Mike Dawson, adapted from:
 *  C64_x.i - Put the pieces together, X specific stuff
 *
 *  Frodo (C) 1994-1997,2002 Christian Bauer
 *  Unix stuff by Bernd Schmidt/Lutz Vieweg
 */

#include "Prefs.h"
#include "main.h"
#include <nds.h>
#include "nds/arm9/console.h"
#include <stdio.h>

#define MATRIX(a,b) (((a) << 3) | (b))

#define timers2ms(tlow,thigh) ((uint32_t)tlow | ((uint32_t)thigh<<16))
#define TICKS_PER_SEC (BUS_CLOCK >> 6)

void StartTimers(void)
{
   TIMER0_CR=0;
   TIMER1_CR=0;
   TIMER0_DATA=0;
   TIMER1_DATA=0;
   TIMER0_CR=TIMER_DIV_64|TIMER_ENABLE;
   TIMER1_CR=TIMER_CASCADE|TIMER_ENABLE;
}

inline uint32 GetTicks(void)
{
   return timers2ms(TIMER0_DATA, TIMER1_DATA);
}

void Pause(uint32 ms)
{
   uint32 now;
   now=timers2ms(TIMER0_DATA, TIMER1_DATA);
   while((uint32)timers2ms(TIMER0_DATA, TIMER1_DATA)<now+ms);
}


int usleep(unsigned long int microSeconds)
{
    Pause(microSeconds);
    return 0;
}

/*
 *  Constructor, system-dependent things
 */

void C64::c64_ctor1(void)
{
    StartTimers();

}

void C64::c64_ctor2(void)
{
}


/*
 *  Destructor, system-dependent things
 */

void C64::c64_dtor(void)
{
}


/*
 *  Start main emulation thread
 */

void C64::Run(void)
{
    // Reset chips
    this->Reset();

    // Patch kernal IEC routines
    orig_kernal_1d84 = Kernal[0x1d84];
    orig_kernal_1d85 = Kernal[0x1d85];
    PatchKernal(ThePrefs.FastReset, ThePrefs.TrueDrive);

    main_loop();
}

char kbd_feedbuf[256];
int kbd_feedbuf_pos;

void kbd_buf_feed(const char *s) {
    strncat(kbd_feedbuf, s, 255);
}

void kbd_buf_reset(void) {
    kbd_feedbuf[0] = 0;
    kbd_feedbuf[255] = 0;
    kbd_feedbuf_pos=0;
}

void kbd_buf_update(C64 *TheC64)
{
    if((kbd_feedbuf[kbd_feedbuf_pos]!=0) && TheC64->RAM[198]==0)
    {
        TheC64->RAM[631]=kbd_feedbuf[kbd_feedbuf_pos];
        TheC64->RAM[198]=1;
        kbd_feedbuf_pos++;
    }
    else
    {
        if (TheC64->RAM[198] == 0)
        {
            kbd_feedbuf_pos = 0;
            kbd_feedbuf[0] = 0;
        }
    }
}


/*
 *  Vertical blank: Poll keyboard and joysticks, update window
 */

ITCM_CODE void C64::VBlank(bool draw_frame)
{
    static int frames=0;
    static int frames_per_sec=0;

    scanKeys();
    kbd_buf_update(this);

    TheDisplay->PollKeyboard(TheCIA1->KeyMatrix, TheCIA1->RevMatrix, &joykey);

    TheCIA1->Joystick1 = poll_joystick(0);
    TheCIA1->Joystick2 = poll_joystick(1);

    TheCIA1->CountTOD();
    TheCIA2->CountTOD();
    
    TheCart->CartFrame();

    frames++;
    while (GetTicks() < (((unsigned int)TICKS_PER_SEC/(unsigned int)SCREEN_FREQ) * (unsigned int)frames))
    {
        if (bTurboWarp) break;
    }

    frames_per_sec++;

    extern u16 DSIvBlanks;
    if (DSIvBlanks >= 60)
    {
        DSIvBlanks = 0;
        TheDisplay->DisplayStatusLine((int)frames_per_sec);
        frames_per_sec = 0;
    }

    if (frames == SCREEN_FREQ)
    {
        frames = 0;
        StartTimers();
    }
}

u8 key_row_map[] __attribute__((section(".dtcm"))) = {7,7,0,0,0,0,6,6,5,5,5,5,5,6,6,5,    1,3,2,2,1,2,3,3,4,4,4,5,4,4,4,5,7,2,1,2,3,3,1,2,3,1,   7,7,1,1,2,2,3,3,4,4};
u8 key_col_map[] __attribute__((section(".dtcm"))) = {7,5,4,5,6,3,1,5,0,3,4,7,5,2,7,6,    2,4,4,2,6,5,2,5,1,2,5,2,4,7,6,1,6,1,5,6,6,7,1,7,1,4,   0,3,0,3,0,3,0,3,0,3};

/*
 *  Poll joystick port, return CIA mask
 */
u8  space=0;
u8  retkey=0;
u16 dampen=0;
extern s16 temp_offset_y,  temp_offset_x;
extern u8 slide_dampen_y,  slide_dampen_x;

// ----------------------------------------------------------------------------
// Chuckie-Style d-pad keeps moving in the last known direction for a few more
// frames to help make those hairpin turns up and off ladders much easier...
// ----------------------------------------------------------------------------
u8 slide_n_glide_key_up = 0;
u8 slide_n_glide_key_down = 0;
u8 slide_n_glide_key_left = 0;
u8 slide_n_glide_key_right = 0;

u8 zoom_dampen = 0;

uint8 C64::poll_joystick(int port)
{
    uint8 j = 0xff;

    if (space == 1)
    {
        TheDisplay->KeyRelease(MATRIX(7,4), TheCIA1->KeyMatrix, TheCIA1->RevMatrix);
        space=0;
    }
    if (retkey == 1)
    {
        TheDisplay->KeyRelease(MATRIX(0,1), TheCIA1->KeyMatrix, TheCIA1->RevMatrix);
        retkey=0;
    }

    u32 keys= keysHeld();

    u8 joy_up    = 0;
    u8 joy_down  = 0;
    u8 joy_left  = 0;
    u8 joy_right = 0;
    u8 joy_fire  = 0;
    u8 mappable_key_press[8] = {0,0,0,0,0,0,0,0};

    if (keys & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_A | KEY_B | KEY_X | KEY_Y))
    {
        if (myConfig.joyMode == JOYMODE_SLIDE_N_GLIDE)
        {
            if (keys & KEY_UP)
            {
                slide_n_glide_key_up    = 20;
                slide_n_glide_key_down  = 0;
            }
            if (keys & KEY_DOWN)
            {
                slide_n_glide_key_down  = 20;
                slide_n_glide_key_up    = 0;
            }
            if (keys & KEY_LEFT)
            {
                slide_n_glide_key_left  = 20;
                slide_n_glide_key_right = 0;
            }
            if (keys & KEY_RIGHT)
            {
                slide_n_glide_key_right = 20;
                slide_n_glide_key_left  = 0;
            }

            if (slide_n_glide_key_up)
            {
                slide_n_glide_key_up--;
                keys |= KEY_UP;
            }

            if (slide_n_glide_key_down)
            {
                slide_n_glide_key_down--;
                keys |= KEY_DOWN;
            }

            if (slide_n_glide_key_left)
            {
                slide_n_glide_key_left--;
                keys |= KEY_LEFT;
            }

            if (slide_n_glide_key_right)
            {
                slide_n_glide_key_right--;
                keys |= KEY_RIGHT;
            }
        }

        if (keys & KEY_UP)    mappable_key_press[0] = 1;
        if (keys & KEY_DOWN)  mappable_key_press[1] = 1;
        if (keys & KEY_LEFT)  mappable_key_press[2] = 1;
        if (keys & KEY_RIGHT) mappable_key_press[3] = 1;

        if (keys & KEY_A)     mappable_key_press[4] = 1;
        if (keys & KEY_B)     mappable_key_press[5] = 1;
        if (keys & KEY_X)     mappable_key_press[6] = 1;
        if (keys & KEY_Y)     mappable_key_press[7] = 1;
      }
      else // No NDS keys pressed...
      {
          if (slide_n_glide_key_up)    slide_n_glide_key_up--;
          if (slide_n_glide_key_down)  slide_n_glide_key_down--;
          if (slide_n_glide_key_left)  slide_n_glide_key_left--;
          if (slide_n_glide_key_right) slide_n_glide_key_right--;
          
          if (zoom_dampen)  zoom_dampen--;
      }

    u8 auto_fire = 0;
    for (int i=0; i<8; i++)
    {
        if (mappable_key_press[i])
        {
            switch (myConfig.key_map[i])
            {
                // Handle space and return specially
                case KEY_MAP_RETURN:
                    TheDisplay->KeyPress(MATRIX(0,1), TheCIA1->KeyMatrix, TheCIA1->RevMatrix);
                    retkey=1;
                    break;
                case KEY_MAP_SPACE:
                    TheDisplay->KeyPress(MATRIX(7,4), TheCIA1->KeyMatrix, TheCIA1->RevMatrix);
                    space=1;
                    break;

                // Handle all joystick mapped buttons
                case KEY_MAP_JOY_UP:
                    joy_up = 1;
                    break;
                case KEY_MAP_JOY_DOWN:
                    joy_down = 1;
                    break;
                case KEY_MAP_JOY_LEFT:
                    joy_left = 1;
                    break;
                case KEY_MAP_JOY_RIGHT:
                    joy_right = 1;
                    break;
                case KEY_MAP_JOY_FIRE:
                    joy_fire = 1;
                    break;
                case KEY_MAP_JOY_AUTO:
                    joy_fire = 1;
                    auto_fire = 1;
                    break;

                // Handle special meta-mapped buttons (pan screen Up/Down)
                case KEY_MAP_PAN_UP16:
                    temp_offset_y = -16;
                    slide_dampen_y = 15;
                    break;
                case KEY_MAP_PAN_UP24:
                    temp_offset_y = -24;
                    slide_dampen_y = 15;
                    break;
                case KEY_MAP_PAN_UP32:
                    temp_offset_y = -32;
                    slide_dampen_y = 15;
                    break;
                
                case KEY_MAP_PAN_DN16:
                    temp_offset_y = 16;
                    slide_dampen_y = 15;
                    break;
                case KEY_MAP_PAN_DN24:
                    temp_offset_y = 24;
                    slide_dampen_y = 15;
                    break;
                case KEY_MAP_PAN_DN32:
                    temp_offset_y = 32;
                    slide_dampen_y = 15;
                    break;

                case KEY_MAP_PAN_LT32:
                    temp_offset_x = -32;
                    slide_dampen_x = 15;
                    break;
                case KEY_MAP_PAN_RT32:
                    temp_offset_x = 32;
                    slide_dampen_x = 15;
                    break;
                case KEY_MAP_PAN_LT64:
                    temp_offset_x = -64;
                    slide_dampen_x = 15;
                    break;
                case KEY_MAP_PAN_RT64:
                    temp_offset_x = 64;
                    slide_dampen_x = 15;
                    break;
                    
                case KEY_MAP_ZOOM_SCR:
                    if (!zoom_dampen)
                    {
                        toggle_zoom();
                    }
                    zoom_dampen = 50;                    
                    break;

                // Handle all other keypresses... mark the key as pressed for the PollKeyboard() routine
                default:
                    TheDisplay->IssueKeypress(key_row_map[myConfig.key_map[i]-8], key_col_map[myConfig.key_map[i]-8], TheCIA1->KeyMatrix, TheCIA1->RevMatrix);
                    break;
            }
        }
    }

    static u32 auto_fire_dampen=0;
    if (auto_fire && joy_fire)
    {
        if (++auto_fire_dampen & 0x08) joy_fire=0;
    } else auto_fire_dampen=0;

    bTurboWarp = 0;
    if((keys & KEY_R) && (keys & KEY_L))
    {
        // Turbo/Warp mode!
        bTurboWarp = 1;
    }
    else
    if((keys & KEY_R) && !dampen)
    {
        if (keys & KEY_UP)
        {
            dampen = 4;
            myConfig.offsetY++;
        }
        if (keys & KEY_DOWN)
        {
            dampen = 4;
            if (myConfig.offsetY > 0) myConfig.offsetY--;
        }
        if (keys & KEY_LEFT)
        {
            dampen = 4;
            if (myConfig.offsetX < 64) myConfig.offsetX++;
        }
        if (keys & KEY_RIGHT)
        {
            dampen = 4;
            if (myConfig.offsetX > 0) myConfig.offsetX--;
        }
    }
    else
    if((keys & KEY_L) && !dampen)
    {
        if (keys & KEY_UP)
        {
            dampen = 4;
            if (myConfig.scaleY < 200) myConfig.scaleY++;
        }
        if (keys & KEY_DOWN)
        {
            dampen = 4;
            if (myConfig.scaleY > 140) myConfig.scaleY--;
        }
        if (keys & KEY_LEFT)
        {
            dampen = 4;
            if (myConfig.scaleX > 200) myConfig.scaleX--;
        }
        if (keys & KEY_RIGHT)
        {
            dampen = 4;
            if (myConfig.scaleX < 320) myConfig.scaleX++;
        }
    }
    
    if (myConfig.joyMode == JOYMODE_DIAGONALS)
    {
             if (joy_up)    {joy_right = 1;}
        else if (joy_down)  {joy_left  = 1;}
        else if (joy_left)  {joy_up    = 1;}
        else if (joy_right) {joy_down  = 1;}
    }

    if( (keys & KEY_SELECT) && !dampen)
    {
        myConfig.joyPort ^= 1;
        extern void show_joysticks();
        show_joysticks();
        dampen=30;
    }

    if( keys & KEY_START && !dampen)
    {
        kbd_buf_feed("\rLOAD\"*\",8,1\rRUN\r");
        dampen = 50; // Full second - do not repeat this often!
    }

    if (!dampen) // Handle joystick
    {
        // Make sure this is the configured joystick...
        if (port != myConfig.joyPort) return j;

        if( joy_up )           j&=0xfe; // Up
        if( joy_down )         j&=0xfd; // Down
        if( joy_left )         j&=0xfb; // Left
        if( joy_right )        j&=0xf7; // Right
        if( joy_fire )         j&=0xef; // Fire button
    }
    else
    {
        dampen--;
    }

    return j;
}

/*
 * C64 .crt Cart Insert
 */

void C64::InsertCart(char *filename)
{
	Cartridge * new_cart = nullptr;

    char errBuffer[40];
    new_cart = Cartridge::FromFile(filename, errBuffer);

    // Swap cartridge object if successful
	if (new_cart)
    {
		delete TheCart;
		TheCart = TheCPU->TheCart = new_cart;
	}
    else
    {
        DSPrint(0, 0, 6, (char*)errBuffer);
        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
        DSPrint(0, 0, 6, (char*)"                              ");
    }
}

/*
 * C64 .crt Cart Remove
 */

void C64::RemoveCart(void)
{
    extern u8 cart_in;
    delete TheCart;
    TheCart = TheCPU->TheCart = new Cartridge();
    extern char CartFilename[];
    extern char CartType[];
    strcpy(CartFilename, "");
    strcpy(CartType, "NONE");
    cart_in = 0;
}


/*
 * The emulation's main loop
 */

void C64::main_loop(void)
{
    while (true)
    {
        if(have_a_break)
        {
            scanKeys();
            continue;
        }

        // The order of calls is important here
        int cpu_cycles_to_execute = TheVIC->EmulateLine();
        TheSID->EmulateLine(SID_CYCLES_PER_LINE);
        TheCIA1->EmulateLine(CIA_CYCLES_PER_LINE);
        TheCIA2->EmulateLine(CIA_CYCLES_PER_LINE);

        // -----------------------------------------------------------------
        // TrueDrive is more complicated as we must interleave the two CPUs
        // -----------------------------------------------------------------
        if (ThePrefs.TrueDrive)
        {
            int cycles_1541 = FLOPPY_CYCLES_PER_LINE + CycleDeltas[myConfig.cpuCycles];
            TheCPU1541->CountVIATimers(cycles_1541);

            if (!TheCPU1541->Idle)
            {
                // -----------------------------------------------------------
                // 1541 processor active, alternately execute 6502 and 6510
                // instructions until both have used up their cycles. This
                // is now handled inside CPU_emuline.h for the 1541 processor
                // to avoid the overhead of lots of function calls...
                // -----------------------------------------------------------
                TheCPU1541->EmulateLine(cycles_1541, cpu_cycles_to_execute);
            }
            else
            {
                TheCPU->EmulateLine(cpu_cycles_to_execute);
            }
        }
        else
        {
            // 1541 processor disabled, only emulate 6510
            TheCPU->EmulateLine(cpu_cycles_to_execute);
        }
    }
}

void C64::Pause() {
    have_a_break=true;
    TheSID->PauseSound();
}

void C64::Resume() {
    have_a_break=false;
    TheSID->ResumeSound();
}

