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
#include "1541job.h"
#include "Display.h"
#include "Prefs.h"
#include "mainmenu.h"
#include "lzav.h"
#include <maxmod9.h>
#include "soundbank.h"


uint8 myRAM[C64_RAM_SIZE];
uint8 myKERNAL[KERNAL_ROM_SIZE];
uint8 myRAM1541[DRIVE_RAM_SIZE] __attribute__((section(".dtcm")));

#define SNAPSHOT_VERSION 1

/*
 *  Constructor: Allocate objects and memory
 */

C64::C64()
{
    uint8 *p;

    // The thread is not yet running
    thread_running = false;
    quit_thyself = false;
    have_a_break = false;

    // System-dependent things
    c64_ctor1();

    // Open display
    TheDisplay = new C64Display(this);

    // Allocate RAM/ROM memory
    RAM = myRAM;
    Basic = new uint8[BASIC_ROM_SIZE];
    Kernal = myKERNAL;
    Char = new uint8[CHAR_ROM_SIZE];
    Color = new uint8[COLOR_RAM_SIZE];
    RAM1541 = myRAM1541;
    ROM1541 = new uint8[DRIVE_ROM_SIZE];

    // Create the chips
    TheCPU = new MOS6510(this, RAM, Basic, Kernal, Char, Color);

    TheJob1541 = new Job1541(RAM1541);
    TheCPU1541 = new MOS6502_1541(this, TheJob1541, TheDisplay, RAM1541, ROM1541);

    TheVIC = TheCPU->TheVIC = new MOS6569(this, TheDisplay, TheCPU, RAM, Char, Color);
    TheSID = TheCPU->TheSID = new MOS6581(this);
    TheCIA1 = TheCPU->TheCIA1 = new MOS6526_1(TheCPU, TheVIC);
    TheCIA2 = TheCPU->TheCIA2 = TheCPU1541->TheCIA2 = new MOS6526_2(TheCPU, TheVIC, TheCPU1541);
    TheIEC = TheCPU->TheIEC = new IEC(TheDisplay);

    // Initialize RAM with powerup pattern
    p = RAM;
    for (unsigned i=0; i<512; i++) {
        for (unsigned j=0; j<64; j++)
            *p++ = 0;
        for (unsigned j=0; j<64; j++)
            *p++ = 0xff;
    }

    // Initialize color RAM with random values
    p = Color;
    for (unsigned i=0; i<COLOR_RAM_SIZE; i++)
        *p++ = rand() & 0x0f;

    // Clear 1541 RAM
    memset(RAM1541, 0, DRIVE_RAM_SIZE);

    // Clear joykey
    joykey = 0xff;

    // System-dependent things
    c64_ctor2();
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
    delete TheCPU;
    delete TheDisplay;

    //delete[] RAM;
    delete[] Basic;
    //delete[] Kernal;
    delete[] Char;
    delete[] Color;
    //delete[] RAM1541;
    delete[] ROM1541;

    c64_dtor();
}


/*
 *  Reset C64
 */

void C64::Reset(void)
{
    TheCPU->AsyncReset();
    TheCPU1541->AsyncReset();
    TheSID->Reset();
    TheCIA1->Reset();
    TheCIA2->Reset();
    TheIEC->Reset();
}


/*
 *  NMI C64
 */

void C64::NMI(void)
{
    TheCPU->AsyncNMI();
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

#ifdef __NDS__
    // Changed order of calls. If 1541 mode hasn't changed the order is insignificant.
    if (prefs->TrueDrive) {
        // New prefs have 1541 enabled ==> if old prefs had disabled free drives FIRST
        TheIEC->NewPrefs(prefs);
        TheJob1541->NewPrefs(prefs);
    } else {
        // New prefs has 1541 disabled ==> if old prefs had enabled free job FIRST
        TheJob1541->NewPrefs(prefs);
        TheIEC->NewPrefs(prefs);
    }
#else
    TheIEC->NewPrefs(prefs);
    TheJob1541->NewPrefs(prefs);
#endif

    TheSID->NewPrefs(prefs);

    // Reset 1541 processor if turned on
    if (!ThePrefs.TrueDrive && prefs->TrueDrive)
        TheCPU1541->AsyncReset();
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

u8 CompressBuffer[0x20000]; //128K more than enough

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
    bool bVICSave = SaveVICState(f);
    bool bSIDSave = SaveSIDState(f);
    bool bCIASave = SaveCIAState(f);
    bool bCPUSave = SaveCPUState(f);
    fputc(0, f);        // No delay

    if (ThePrefs.TrueDrive)
    {
        fwrite(ThePrefs.DrivePath[0], 256, 1, f);
        Save1541State(f);
        fputc(0, f);    // No delay
        Save1541JobState(f);
    }
    fclose(f);

    if (bVICSave && bSIDSave && bCIASave && bCPUSave) return true;
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
                ShowRequester("Unknown snapshot format", "OK", NULL);
                fclose(f);
                return false;
            }
            flags = fgetc(f);
            vicptr = ftell(f);

            error |= !LoadVICState(f);
            error |= !LoadSIDState(f);
            error |= !LoadCIAState(f);
            error |= !LoadCPUState(f);

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
                ShowRequester("Error reading snapshot file", "OK", NULL);
                Reset();
                return false;
            } else
                return true;
        } else {
            fclose(f);
            ShowRequester("Not a Frodo snapshot file", "OK", NULL);
            return false;
        }
    } else {
        ShowRequester("Can't open snapshot file", "OK", NULL);
        return false;
    }
}

/*
 *  C64_GP32.i by Mike Dawson, adapted from:
 *  C64_x.i - Put the pieces together, X specific stuff
 *f
 *  Frodo (C) 1994-1997,2002 Christian Bauer
 *  Unix stuff by Bernd Schmidt/Lutz Vieweg
 */

#include "Prefs.h"
#include "main.h"
extern "C" {

//#include "menu.h"
//#include "ui.h"
//#include "input.h"
//#include "gpmisc.h"
}

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


int frames_per_sec=0;

int current_joystick=0;

#ifndef HAVE_USLEEP

int usleep(unsigned long int microSeconds)
{
    Pause(microSeconds);
    return 0;
}
#endif


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

    current_joystick = 0;

    quit_thyself = false;
    thread_func();
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

void load_prg(C64 *TheC64, uint8 *prg, int prg_size) {
    uint8 start_hi, start_lo;
    uint16 start;
    int i;

    start_lo=*prg++;
    start_hi=*prg++;
    start=(start_hi<<8)+start_lo;

    for(i=0; i<(prg_size-2); i++) {
        TheC64->RAM[start+i]=prg[i];
    }
}

/*
 *  Vertical blank: Poll keyboard and joysticks, update window
 */

ITCM_CODE void C64::VBlank(bool draw_frame)
{
    static int frames=0;

    scanKeys();
    kbd_buf_update(this);

    TheDisplay->PollKeyboard(TheCIA1->KeyMatrix, TheCIA1->RevMatrix, &joykey);

    TheCIA1->Joystick1 = poll_joystick(0);
    TheCIA1->Joystick2 = poll_joystick(1);

    if (draw_frame)
    {
        TheDisplay->Update();

        frames++;
        while (GetTicks() < (((unsigned int)TICKS_PER_SEC/(unsigned int)50) * (unsigned int)frames))
        {
            if (ThePrefs.TrueDrive && TheDisplay->led_state[0]) break; // If reading the drive in 'true drive' mode, just plow along...
            asm("nop");
            //break;  // Uncomment this for full speed...
        }

        frames_per_sec++;

        extern u16 vBlanks;
        if (vBlanks >= 60)
        {
            vBlanks = 0;
            TheDisplay->Speedometer((int)frames_per_sec);
            frames_per_sec = 0;
        }

        if (frames == 50)
        {
            frames = 0;
            StartTimers();
        }
    }
}


/*
 *  Poll joystick port, return CIA mask
 */
u8  space=0;
u8  retkey=0;
u16 dampen=0;
extern s16 temp_offset;
extern u16 slide_dampen;
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

    u8 joy_up = 0;
    u8 joy_dn = 0;
    u8 joy_fire = 0;

    if(keys & KEY_B)
    {
        switch (myConfig.key_B)
        {
            case KEY_MAP_RETURN:
                TheDisplay->KeyPress(MATRIX(0,1), TheCIA1->KeyMatrix, TheCIA1->RevMatrix);
                retkey=1;
                break;
            case KEY_MAP_SPACE:
                TheDisplay->KeyPress(MATRIX(7,4), TheCIA1->KeyMatrix, TheCIA1->RevMatrix);
                space=1;
                break;
            case KEY_MAP_PAN_UP:
                temp_offset = -16;
                slide_dampen = 15;
                break;
            case KEY_MAP_PAN_DN:
                temp_offset = 16;
                slide_dampen = 15;
                break;
            case KEY_MAP_JOY_UP:
                joy_up = 1;
                break;
            case KEY_MAP_JOY_DN:
                joy_dn = 1;
                break;
            case KEY_MAP_JOY_FIRE:
                joy_fire = 1;
                break;
        }
    }

    if(keys & KEY_A)
    {
        switch (myConfig.key_A)
        {
            case KEY_MAP_RETURN:
                TheDisplay->KeyPress(MATRIX(0,1), TheCIA1->KeyMatrix, TheCIA1->RevMatrix);
                retkey=1;
                break;
            case KEY_MAP_SPACE:
                TheDisplay->KeyPress(MATRIX(7,4), TheCIA1->KeyMatrix, TheCIA1->RevMatrix);
                space=1;
                break;
            case KEY_MAP_PAN_UP:
                temp_offset = -16;
                slide_dampen = 15;
                break;
            case KEY_MAP_PAN_DN:
                temp_offset = 16;
                slide_dampen = 15;
                break;
            case KEY_MAP_JOY_UP:
                joy_up = 1;
                break;
            case KEY_MAP_JOY_DN:
                joy_dn = 1;
                break;
            case KEY_MAP_JOY_FIRE:
                joy_fire = 1;
                break;
        }
    }

    if(keys & KEY_X)
    {
        switch (myConfig.key_X)
        {
            case KEY_MAP_RETURN:
                TheDisplay->KeyPress(MATRIX(0,1), TheCIA1->KeyMatrix, TheCIA1->RevMatrix);
                retkey=1;
                break;
            case KEY_MAP_SPACE:
                TheDisplay->KeyPress(MATRIX(7,4), TheCIA1->KeyMatrix, TheCIA1->RevMatrix);
                space=1;
                break;
            case KEY_MAP_PAN_UP:
                temp_offset = -16;
                slide_dampen = 15;
                break;
            case KEY_MAP_PAN_DN:
                temp_offset = 16;
                slide_dampen = 15;
                break;
            case KEY_MAP_JOY_UP:
                joy_up = 1;
                break;
            case KEY_MAP_JOY_DN:
                joy_dn = 1;
                break;
            case KEY_MAP_JOY_FIRE:
                joy_fire = 1;
                break;
        }
    }

    if(keys & KEY_Y)
    {
        switch (myConfig.key_Y)
        {
            case KEY_MAP_RETURN:
                TheDisplay->KeyPress(MATRIX(0,1), TheCIA1->KeyMatrix, TheCIA1->RevMatrix);
                retkey=1;
                break;
            case KEY_MAP_SPACE:
                TheDisplay->KeyPress(MATRIX(7,4), TheCIA1->KeyMatrix, TheCIA1->RevMatrix);
                space=1;
                break;
            case KEY_MAP_PAN_UP:
                temp_offset = -16;
                slide_dampen = 15;
                break;
            case KEY_MAP_PAN_DN:
                temp_offset = 16;
                slide_dampen = 15;
                break;
            case KEY_MAP_JOY_UP:
                joy_up = 1;
                break;
            case KEY_MAP_JOY_DN:
                joy_dn = 1;
                break;
            case KEY_MAP_JOY_FIRE:
                joy_fire = 1;
                break;
        }
    }

    static u32 auto_fire_dampen=0;
    if ((myConfig.autoFire) && joy_fire)
    {
        if (++auto_fire_dampen & 0x08) joy_fire=0;
    } else auto_fire_dampen=0;

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
            myConfig.offsetX++;
        }
        if (keys & KEY_RIGHT)
        {
            dampen = 4;
            if (myConfig.offsetX > 0) myConfig.offsetX--;
        }
    }

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
            myConfig.scaleY--;
        }
        if (keys & KEY_LEFT)
        {
            dampen = 4;
            myConfig.scaleX--;
        }
        if (keys & KEY_RIGHT)
        {
            dampen = 4;
            if (myConfig.scaleX < 320) myConfig.scaleX++;
        }
    }

    if( (keys & KEY_SELECT) && !dampen)
    {
        current_joystick ^= 1;
        extern void show_joysticks();
        show_joysticks();
        dampen=30;
    }

    if( keys & KEY_START && !dampen)
    {
        kbd_buf_feed("\rLOAD\"*\",8,1\rRUN\r");
        dampen = 30;
    }

    if (dampen) dampen--;

    if (!dampen)
    {
        if(port!=current_joystick) return j;

        if( keys & KEY_LEFT  ) j&=0xfb;
        if( keys & KEY_RIGHT ) j&=0xf7;
        if( keys & KEY_UP    ) j&=0xfe;
        if( keys & KEY_DOWN  ) j&=0xfd;
        if( joy_fire )         j&=0xef; // Fire button
        if( joy_up )           j&=0xfe;
        if( joy_dn )           j&=0xfd;
    }

    return j;
}


/*
 * The emulation's main loop
 */

void C64::thread_func(void)
{
    int linecnt = 0;

    while (!quit_thyself)
    {
        if(have_a_break)
        {
            scanKeys();
            continue;
        }

        // The order of calls is important here
        int cycles = TheVIC->EmulateLine();
        TheSID->EmulateLine();
#if !PRECISE_CIA_CYCLES
        TheCIA1->EmulateLine(63);
        TheCIA2->EmulateLine(63);
#endif

        if (ThePrefs.TrueDrive)
        {
            int cycles_1541 = 64;
            TheCPU1541->CountVIATimers(cycles_1541);

            if (!TheCPU1541->Idle)
            {
                // -----------------------------------------------------------------------------------------
                // Warp speed - only draw every 10 frames to give max time to CPUs when the drive is active
                // Note, we can't just use the Idle flag here as the drive periodically goes non-idle to
                // check status - so we use the LED state which is a better indicator of drive activity...
                // -----------------------------------------------------------------------------------------
                if (TheDisplay->led_state[0]) ThePrefs.DrawEveryN = 10;
                else ThePrefs.DrawEveryN = isDSiMode() ? 1:2;

                // -----------------------------------------------------------
                // 1541 processor active, alternately execute 6502 and 6510
                // instructions until both have used up their cycles. This
                // is now handled inside CPU_emuline.h for the 1541 processor
                // to avoid the overhead of lots of function calls...
                // -----------------------------------------------------------
                TheCPU1541->EmulateLine(cycles_1541, cycles);
            }
            else
            {
                ThePrefs.DrawEveryN = isDSiMode() ? 1:2;
                TheCPU->EmulateLine(cycles);
            }
        }
        else
        {
            // 1541 processor disabled, only emulate 6510
            TheCPU->EmulateLine(cycles);
        }

        linecnt++;
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

