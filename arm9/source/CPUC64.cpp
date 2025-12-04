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
 *  CPUC64.cpp - 6510 (C64) emulation (line based)
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
 *    raster line. It has a cycle counter that is decremented by every
 *    executed opcode and if the counter goes below zero, the function
 *    returns.
 *  - All memory accesses are done with the read_byte() and
 *    write_byte() functions which also do the memory address decoding. The
 *    read_zp() and write_zp() functions allow faster access to the zero
 *    page, the pop_byte() and push_byte() macros for the stack.
 *  - If a write occurs to addresses 0 or 1, new_config() is called to check
 *    whether the memory configuration has changed
 *  - The possible interrupt sources are:
 *      INT_VICIRQ: I flag is checked, jump to ($fffe)
 *      INT_CIAIRQ: I flag is checked, jump to ($fffe)
 *      INT_NMI: Jump to ($fffa)
 *      INT_RESET: Jump to ($fffc)
 *  - Interrupts are not checked before every opcode but only at certain
 *    times:
 *      On entering EmulateLine()
 *      On CLI
 *      On PLP if the I flag was cleared
 *      On RTI if the I flag was cleared
 *  - The z_flag variable has the inverse meaning of the 6510 Z flag.
 *  - Only the highest bit of the n_flag variable is used.
 *  - The $f2 opcode that would normally crash the 6510 is used to implement
 *    emulator-specific functions, mainly those for the IEC routines.
 */

#include <nds.h>
#include "sysdeps.h"

#include "CPUC64.h"
#include "C64.h"
#include "VIC.h"
#include "SID.h"
#include "CIA.h"
#include "IEC.h"
#include "REU.h"
#include "Display.h"
#include "Cartridge.h"
#include "mainmenu.h"
#include "printf.h"

enum 
{
    INT_RESET = 3
};

extern uint8 myRAM[];
extern uint8 myCOLOR[];

uint8 *MemMap[0x10]         __attribute__((section(".dtcm")));
uint8 flash_write_supported __attribute__((section(".dtcm"))) = 0;

/*
 *  6510 constructor: Initialize registers
 */

MOS6510::MOS6510()
{
  // Most of init done in Init() so we can keep the constructor simple and allocate the object in the 'fast memory'
}

void MOS6510::Init(C64 *c64, uint8 *Ram, uint8 *Basic, uint8 *Kernal, uint8 *Char, uint8 *Color)
{
    the_c64 = c64;
    ram = Ram;
    basic_rom = Basic;
    kernal_rom = Kernal;
    char_rom = Char;
    color_ram = Color;

    a = x = y = 0;
    sp = 0xff;
    n_flag = z_flag = 0;
    v_flag = d_flag = c_flag = false;
    i_flag = true;

    interrupt.intr[INT_VICIRQ] = false;
    interrupt.intr[INT_CIAIRQ] = false;
    interrupt.intr[INT_NMI] = false;
    interrupt.intr[INT_RESET] = false;

    nmi_state = false;

    borrowed_cycles = 0;
    dfff_byte = 0x55;
}

/*
 *  Reset CPU asynchronously
 */

void MOS6510::AsyncReset(void)
{
    interrupt.intr[INT_RESET] = true;
}


/*
 *  Raise NMI asynchronously (Restore key)
 */

void MOS6510::AsyncNMI(void)
{
    if (!nmi_state)
    {
        interrupt.intr[INT_NMI] = true;
    }
}

void MOS6510::setCharVsIO(void)
{
    uint8 port = ~ram[0] | ram[1];
    char_in = (port & 3) && !(port & 4);
    io_in = (port & 3) && (port & 4);
}

/*
 *  Memory configuration has probably changed
 */

__attribute__ ((noinline)) void MOS6510::new_config(void)
{
    if ((ram[0] & 0x10) == 0) 
    {
        ram[1] |= 0x10; // Keep cassette sense line high
    }

    uint8 port = ~ram[0] | ram[1];

    basic_in = (port & 3) == 3;
    kernal_in = port & 2;
    char_in = (port & 3) && !(port & 4);
    io_in = (port & 3) && (port & 4);
    
    MemMap[0x0] = myRAM;
    MemMap[0x1] = myRAM;
    MemMap[0x2] = myRAM;
    MemMap[0x3] = myRAM;
    MemMap[0x4] = myRAM;
    MemMap[0x5] = myRAM;
    MemMap[0x6] = myRAM;
    MemMap[0x7] = myRAM;
    
    MemMap[0x8] = myRAM;
    MemMap[0x9] = myRAM;
    MemMap[0xa] = basic_in ? (basic_rom - 0xa000) : myRAM;
    MemMap[0xb] = basic_in ? (basic_rom - 0xa000) : myRAM;
    MemMap[0xc] = myRAM;
    MemMap[0xd] = 0;
    MemMap[0xe] = kernal_in ? (kernal_rom - 0xe000) : myRAM;
    MemMap[0xf] = kernal_in ? (kernal_rom - 0xe000) : myRAM;
    
    // If a Cartridge is inserted, it may respond in some of these memory regions
    TheCart->MapThyself();
}

/*
 *  Read a byte from I/O / ROM space
 */
__attribute__ ((noinline)) uint8_t  MOS6510::read_byte_io(uint16 adr)
{    
    if (io_in || vic_ultimax_mode)
    {
        switch ((adr >> 8) & 0x0f) 
        {
            case 0x0:   // VIC
            case 0x1:
            case 0x2:
            case 0x3:
                return TheVIC->ReadRegister(adr & 0x3f);
            case 0x4:   // SID
            case 0x5:
            case 0x6:
            case 0x7:
                return TheSID->ReadRegister(adr & 0x1f);
            case 0x8:   // Color RAM
            case 0x9:
            case 0xa:
            case 0xb:
                return myCOLOR[adr & 0x03ff] | (rand() & 0xf0);
            case 0xc:   // CIA 1
                return TheCIA1->ReadRegister(adr & 0x0f);
            case 0xd:   // CIA 2
                return TheCIA2->ReadRegister(adr & 0x0f);
            case 0xe:   // Cartridge I/O 1 (or open)
                return TheCart->ReadIO1(adr & 0xff, rand());
            case 0xf:   // Cartridge I/O 2 (or open)
                if (myConfig.reuType) return TheREU->ReadIO2(adr & 0xff, rand());
                return TheCart->ReadIO2(adr & 0xff, rand());
        }
    }
    else if (char_in) 
    {
         return char_rom[adr & 0x0fff];
    }

    return myRAM[adr];
}


/*
 *  Read a byte from the CPU's address space
 */

inline __attribute__((always_inline)) uint8 MOS6510::read_byte(uint16 adr)
{
    if (MemMap[adr>>12]) return MemMap[adr>>12][adr];
    else return read_byte_io(adr);
}

/*
 *  Read a word (little-endian) from the CPU's address space
 */
inline __attribute__((always_inline)) ITCM_CODE uint16 MOS6510::read_word(uint16 adr)
{
    if ((adr>>12) == 0xd) return (read_byte_io(adr) | (read_byte_io(adr+1) << 8));
    else return (MemMap[adr>>12][adr] | (MemMap[adr>>12][adr+1] << 8));
}

__attribute__ ((noinline)) ITCM_CODE uint16 MOS6510::read_word_pc(void)
{
    if ((pc>>12) == 0xd) return (read_byte_io(pc) | (read_byte_io(pc+1) << 8));
    else return (MemMap[pc>>12][pc] | (MemMap[pc>>12][pc+1] << 8));
}

/*
 *  Read byte from 6510 address space with current memory config (used by REU)
 */

uint8_t MOS6510::REUReadByte(uint16_t adr)
{
	return read_byte(adr);
}


/*
 *  Write byte to 6510 address space with current memory config (used by REU)
 */

void MOS6510::REUWriteByte(uint16_t adr, uint8_t byte)
{
	write_byte(adr, byte);
}

/*
 *  Write byte to I/O space
 */

__attribute__ ((noinline)) void MOS6510::write_byte_io(uint16 adr, uint8 byte)
{
    if (io_in || vic_ultimax_mode)
    {
        switch ((adr >> 8) & 0x0f) 
        {
            case 0x0:   // VIC
            case 0x1:
            case 0x2:
            case 0x3:
                TheVIC->WriteRegister(adr & 0x3f, byte);
                return;
            case 0x4:   // SID
            case 0x5:
            case 0x6:
            case 0x7:
                TheSID->WriteRegister(adr & 0x1f, byte);
                return;
            case 0x8:   // Color RAM
            case 0x9:
            case 0xa:
            case 0xb:
                myCOLOR[adr & 0x03ff] = byte & 0x0f;
                return;
            case 0xc:   // CIA 1
                TheCIA1->WriteRegister(adr & 0x0f, byte);
                return;
            case 0xd:   // CIA 2
                TheCIA2->WriteRegister(adr & 0x0f, byte);
                return;
            case 0xe:   // Cartridge I/O 1 (or open)
                TheCart->WriteIO1(adr & 0xff, byte);
                return;
            case 0xf:   // Cartridge I/O 2 (or open)
                TheCart->WriteIO2(adr & 0xff, byte);
                if (myConfig.reuType) TheREU->WriteIO2(adr & 0xff, byte);
                return;
        }
    }
    else // Write through even if char_in is enabled
    {
        myRAM[adr] = byte;
    }
}


inline __attribute__((always_inline)) void MOS6510::write_byte_flash(uint16 adr, uint8 byte)
{
    if ((adr >> 12) == 0xd)
    {
        write_byte_io(adr, byte);
    }
    else
    {
        if (flash_write_supported) TheCart->WriteFlash(adr, byte);
        else myRAM[adr] = byte;
    }
}

/*
 *  Write a byte to the CPU's address space
 */
inline __attribute__((always_inline)) void MOS6510::write_byte(uint16 adr, uint8 byte)
{
    if (adr & 0x8000)
    {
        write_byte_flash(adr, byte);
    }
    else
    {
        myRAM[adr] = byte;
        if (adr < 2) new_config(); // First two bytes are special...
    }
}


/*
 *  Read a byte from the zeropage
 */

inline uint8 MOS6510::read_zp(uint16 adr)
{
    return myRAM[adr];
}


/*
 *  Read a word (little-endian) from the zeropage
 */

inline uint16 MOS6510::read_zp_word(uint16 adr)
{
// !! zeropage word addressing wraps around !!
    return myRAM[adr & 0xff] | (myRAM[(adr+1) & 0xff] << 8);
}


/*
 *  Write a byte to the zeropage
 */

inline void MOS6510::write_zp(uint16 adr, uint8 byte)
{
    myRAM[adr] = byte;

    // Check if memory configuration may have changed.
    if (adr < 2) new_config(); // First two bytes are special...
}


/*
 *  Jump to address
 */
#define jump(adr) pc = (adr)


/*
 *  Adc instruction
 */

void MOS6510::do_adc(uint8 byte)
{
    if (!d_flag) {
        uint16 tmp = a + (byte) + (c_flag ? 1 : 0);
        c_flag = tmp > 0xff;
        v_flag = !((a ^ (byte)) & 0x80) && ((a ^ tmp) & 0x80);
        z_flag = n_flag = a = tmp;
    } else {
        uint16 al, ah;
        al = (a & 0x0f) + ((byte) & 0x0f) + (c_flag ? 1 : 0);
        if (al > 9) al += 6;
        ah = (a >> 4) + ((byte) >> 4);
        if (al > 0x0f) ah++;
        z_flag = a + (byte) + (c_flag ? 1 : 0);
        n_flag = ah << 4;
        v_flag = (((ah << 4) ^ a) & 0x80) && !((a ^ (byte)) & 0x80);
        if (ah > 9) ah += 6;
        c_flag = ah > 0x0f;
        a = (ah << 4) | (al & 0x0f);
    }
}


/*
 * Sbc instruction
 */

void MOS6510::do_sbc(uint8 byte)
{
    uint16 tmp = a - (byte) - (c_flag ? 0 : 1);
    if (!d_flag) {
        c_flag = tmp < 0x100;
        v_flag = ((a ^ tmp) & 0x80) && ((a ^ (byte)) & 0x80);
        z_flag = n_flag = a = tmp;
    } else {
        uint16 al, ah;
        al = (a & 0x0f) - ((byte) & 0x0f) - (c_flag ? 0 : 1);
        ah = (a >> 4) - ((byte) >> 4);
        if (al & 0x10) {
            al -= 6;
            ah--;
        }
        if (ah & 0x10) ah -= 6;
        c_flag = tmp < 0x100;
        v_flag = ((a ^ tmp) & 0x80) && ((a ^ (byte)) & 0x80);
        z_flag = n_flag = tmp;
        a = (ah << 4) | (al & 0x0f);
    }
}


/*
 *  Get 6510 register state
 */

void MOS6510::GetState(MOS6510State *s)
{
    s->a = a;
    s->x = x;
    s->y = y;

    s->p = 0x20 | (n_flag & 0x80);
    if (v_flag) s->p |= 0x40;
    if (d_flag) s->p |= 0x08;
    if (i_flag) s->p |= 0x04;
    if (!z_flag) s->p |= 0x02;
    if (c_flag) s->p |= 0x01;

    s->ddr = ram[0];
    s->pr = ram[1] & 0x3f;

    s->pc = pc;
    s->sp = sp | 0x0100;

    s->intr[INT_VICIRQ] = interrupt.intr[INT_VICIRQ];
    s->intr[INT_CIAIRQ] = interrupt.intr[INT_CIAIRQ];
    s->intr[INT_NMI] = interrupt.intr[INT_NMI];
    s->intr[INT_RESET] = interrupt.intr[INT_RESET];
    s->nmi_state = nmi_state;
    s->dfff_byte = dfff_byte;
    s->instruction_complete = true;
    
    // ----------------------------------------------------------------
    // Now the tricky part... we use MemMap[] as our CPU memory mapper 
    // so we can quickly index into RAM, Kernal, Basic or Cart ROM and
    // it's possible that from build-to-build that this memory moves...
    // So we must determine the memory type and save the offset so we
    // can properly restore it when loading save states.
    // ----------------------------------------------------------------
    for (u8 i=0; i<16; i++) 
    {
        if ((MemMap[i] >= myRAM) && (MemMap[i] <= (myRAM+0x10000)))
        {
            s->MemMap_Type[i] = MEM_TYPE_RAM;
            s->MemMap_Offset[i] = MemMap[i] - myRAM;
        }
        else 
        if ((MemMap[i] >= (kernal_rom-0xe000)) && (MemMap[i] <= (kernal_rom+0x2000)))
        {
            s->MemMap_Type[i] = MEM_TYPE_KERNAL;
            s->MemMap_Offset[i] = MemMap[i] - kernal_rom;
        }
        else 
        if ((MemMap[i] >= (basic_rom-0xa000)) && (MemMap[i] <= (basic_rom+0x2000)))
        {
            s->MemMap_Type[i] = MEM_TYPE_BASIC;
            s->MemMap_Offset[i] = MemMap[i] - basic_rom;
        }
        else 
        if ((MemMap[i] >= (cartROM-0xe000)) && (MemMap[i] <= (cartROM+(1024*1024))))
        {
            s->MemMap_Type[i] = MEM_TYPE_CART;
            s->MemMap_Offset[i] = MemMap[i] - cartROM;
        }
        else  // None one of the above... save raw address
        {
            s->MemMap_Type[i] = MEM_TYPE_OTHER;
            s->MemMap_Offset[i] = (u32)MemMap[i];
        }
    }
    
    s->spare1 = 0;
    s->spare2 = 0;
    s->spare3 = 0;
    s->spare4 = 0;
}


/*
 *  Restore 6510 state
 */

void MOS6510::SetState(MOS6510State *s)
{
    a = s->a;
    x = s->x;
    y = s->y;

    n_flag = s->p;
    v_flag = s->p & 0x40;
    d_flag = s->p & 0x08;
    i_flag = s->p & 0x04;
    z_flag = !(s->p & 0x02);
    c_flag = s->p & 0x01;

    ram[0] = s->ddr;
    ram[1] = s->pr;
    new_config();

    jump(s->pc);
    sp = s->sp & 0xff;

    interrupt.intr[INT_VICIRQ] = s->intr[INT_VICIRQ];
    interrupt.intr[INT_CIAIRQ] = s->intr[INT_CIAIRQ];
    interrupt.intr[INT_NMI] = s->intr[INT_NMI];
    interrupt.intr[INT_RESET] = s->intr[INT_RESET];
    nmi_state = s->nmi_state;
    dfff_byte = s->dfff_byte;
    
    for (u8 i=0; i<16; i++) 
    {
        if (s->MemMap_Type[i] == MEM_TYPE_RAM)
        {
            MemMap[i] = myRAM + s->MemMap_Offset[i];
        }
        else if (s->MemMap_Type[i] == MEM_TYPE_KERNAL)
        {
            MemMap[i] = kernal_rom + s->MemMap_Offset[i];
        }
        else if (s->MemMap_Type[i] == MEM_TYPE_BASIC)
        {
            MemMap[i] = basic_rom + s->MemMap_Offset[i];
        }
        else if (s->MemMap_Type[i] == MEM_TYPE_CART)
        {
            MemMap[i] = cartROM + s->MemMap_Offset[i];
        }
        else // MEM_TYPE_OTHER
        {
            MemMap[i] = (uint8_t *)s->MemMap_Offset[i];
        }
    }    
}


/*
 *  Reset CPU
 */

void MOS6510::Reset(void)
{
    // Delete 'CBM80' if present
    if (ram[0x8004] == 0xc3 && ram[0x8005] == 0xc2 && ram[0x8006] == 0xcd
     && ram[0x8007] == 0x38 && ram[0x8008] == 0x30)
        ram[0x8004] = 0;

    // Initialize extra 6510 registers and memory configuration
    ram[0] = ram[1] = 0;
    new_config();

    // Clear all interrupt lines
    interrupt.intr_any = 0;
    nmi_state = false;
    
    // The CPU starts fresh with no borrowed cycles from a previous scanline
    borrowed_cycles = 0;
    
    interrupt.intr[INT_VICIRQ] = false;
    interrupt.intr[INT_CIAIRQ] = false;
    interrupt.intr[INT_NMI] = false;
    interrupt.intr[INT_RESET] = false;

    // Read reset vector
    jump(read_word(0xfffc));
}


/*
 *  Illegal opcode encountered
 */

void MOS6510::illegal_op(uint8 op, uint16 at)
{
    char illop_msg[40];

    sprintf(illop_msg, "Illegal opcode %02X at %04X", op, at);
    ShowRequester(illop_msg, "Reset");
    the_c64->Reset();
    Reset();
}



/*
 *  Stack macros
 */

// Pop a byte from the stack
#define pop_byte() myRAM[(++sp) | 0x0100]

// Push a byte onto the stack
#define push_byte(byte) (myRAM[((sp--) & 0xff) | 0x0100] = (byte))

// Pop processor flags from the stack
#define pop_flags() \
    n_flag = tmp = pop_byte(); \
    v_flag = tmp & 0x40; \
    d_flag = tmp & 0x08; \
    i_flag = tmp & 0x04; \
    z_flag = !(tmp & 0x02); \
    c_flag = tmp & 0x01;

// Push processor flags onto the stack
#define push_flags(b_flag) \
    tmp = 0x20 | (n_flag & 0x80); \
    if (v_flag) tmp |= 0x40; \
    if (b_flag) tmp |= 0x10; \
    if (d_flag) tmp |= 0x08; \
    if (i_flag) tmp |= 0x04; \
    if (!z_flag) tmp |= 0x02; \
    if (c_flag) tmp |= 0x01; \
    push_byte(tmp);

void MOS6510::extended_opcode(void)
{
    if (pc < 0xe000) {
        illegal_op(0xf2, pc-1);
    }
    switch (read_byte(pc++)) {
        case 0x00:
            ram[0x90] |= TheIEC->Out(ram[0x95], ram[0xa3] & 0x80);
            c_flag = false;
            jump(0xedac);
            break;
        case 0x01:
            ram[0x90] |= TheIEC->OutATN(ram[0x95]);
            c_flag = false;
            jump(0xedac);
            break;
        case 0x02:
            ram[0x90] |= TheIEC->OutSec(ram[0x95]);
            c_flag = false;
            jump(0xedac);
            break;
        case 0x03:
            ram[0x90] |= TheIEC->In(a);
            z_flag = n_flag = a;
            c_flag = false;
            jump(0xedac);
            break;
        case 0x04:
            TheIEC->SetATN();
            jump(0xedfb);
            break;
        case 0x05:
            TheIEC->RelATN();
            jump(0xedac);
            break;
        case 0x06:
            TheIEC->Turnaround();
            jump(0xedac);
            break;
        case 0x07:
            TheIEC->Release();
            jump(0xedac);
            break;
        default:
            illegal_op(0xf2, pc-1);
            break;
    }
}

// -----------------------------------------------------
// Not very frequent... so pull out from ITCM memory...
// -----------------------------------------------------
__attribute__((noinline)) void MOS6510::IntNMI(void)
{
    uint16 adr;
    uint8 tmp;
    interrupt.intr[INT_NMI] = false;    // Simulate an edge-triggered input
    push_byte(pc >> 8); push_byte(pc);
    push_flags(false);
    i_flag = true;
    adr = read_word(0xfffa);
    jump(adr);
}

// Called whenever a vblank occurs - resync the borrowed cycles so every frame starts fresh
void MOS6510::VBlank(void)
{
    borrowed_cycles = 0;
}

/*
 *  Emulate cycles_left worth of 6510 instructions
 *  Returns number of cycles of last instruction
 */
ITCM_CODE int MOS6510::EmulateLine(int cycles_left)
{
    uint8 tmp, tmp2;
    uint16 adr;     // Used by read_adr_abs()!
    int last_cycles = 0;

    // Any pending interrupts?
    if (interrupt.intr_any)
    {
handle_int:
        if (interrupt.intr[INT_RESET])
        {
            Reset();
        }
        else if (interrupt.intr[INT_NMI])
        {
            IntNMI();
            last_cycles += 7;
        }
        else if ((interrupt.intr[INT_VICIRQ] || interrupt.intr[INT_CIAIRQ]) && !i_flag)
        {
            push_byte(pc >> 8); push_byte(pc);
            push_flags(false);
            i_flag = true;
            adr = read_word(0xfffe);
            jump(adr);
            last_cycles += 7;
        }
    }

#include "CPU_emulline.h"

    return last_cycles;
}

