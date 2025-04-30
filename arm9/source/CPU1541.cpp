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
 *  CPU1541.cpp - 6502 (1541) emulation (line based)
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
 *    raster line. It has a cycle counter that is decremented
 *    by every executed opcode and if the counter goes below
 *    zero, the function returns.
 *  - Memory map (1541C, the 1541 and 1541-II are a bit different):
 *      $0000-$07ff RAM (2K)
 *      $0800-$0fff RAM mirror
 *      $1000-$17ff free
 *      $1800-$1bff VIA 1
 *      $1c00-$1fff VIA 2
 *      $2000-$bfff free
 *      $c000-$ffff ROM (16K)
 *  - All memory accesses are done with the read_byte() and
 *    write_byte() functions which also do the memory address
 *    decoding. The read_zp() and write_zp() functions allow
 *    faster access to the zero page, the pop_byte() and
 *    push_byte() macros for the stack.
 *  - The PC is either emulated with a 16 bit address or a
 *    direct memory pointer (for faster access), depending on
 *    the PC_IS_POINTER #define. In the latter case, a second
 *    pointer, pc_base, is kept to allow recalculating the
 *    16 bit 6502 PC if it has to be pushed on the stack.
 *  - The possible interrupt sources are:
 *      INT_VIA1IRQ: I flag is checked, jump to ($fffe)
 *      INT_VIA2IRQ: I flag is checked, jump to ($fffe)
 *      INT_RESET: Jump to ($fffc)
 *  - Interrupts are not checked before every opcode but only
 *    at certain times:
 *      On entering EmulateLine()
 *      On CLI
 *      On PLP if the I flag was cleared
 *      On RTI if the I flag was cleared
 *  - The z_flag variable has the inverse meaning of the
 *    6502 Z flag
 *  - Only the highest bit of the n_flag variable is used
 *  - The $f2 opcode that would normally crash the 6502 is
 *    used to implement emulator-specific functions
 *  - The 1541 6502 emulation also includes a very simple VIA
 *    emulation (enough to make the IEC bus and GCR loading work).
 *    It's too small to move it to a source file of its own.
 *
 * Incompatibilities:
 * ------------------
 *
 *  - If PC_IS_POINTER is set, neither branches accross memory
 *    areas nor jumps to I/O space are possible
 *  - Extra cycles for crossing page boundaries are not
 *    accounted for
 */

#include "sysdeps.h"

#include "CPU1541.h"
#include "CPUC64.h"
#include "1541gcr.h"
#include "C64.h"
#include "CIA.h"
#include "Display.h"
#include "mainmenu.h"

extern uint8 myRAM1541[DRIVE_RAM_SIZE];

/*
 *  6502 constructor: Initialize registers
 */

MOS6502_1541::MOS6502_1541(C64 *c64, Job1541 *job, C64Display *disp, uint8 *Ram, uint8 *Rom)
 : ram(Ram), rom(Rom), the_c64(c64), the_display(disp), the_job(job)
{
    a = x = y = 0;
    sp = 0xff;
    n_flag = z_flag = 0;
    v_flag = d_flag = c_flag = false;
    i_flag = true;

    cycle_counter = 0;
    borrowed_cycles = 0;

    rom = Rom - 0xC000; // So we don't have to mask the ROM when reading

    via1_t1c = via1_t1l = via1_t2c = via1_t2l = 0;
    via1_sr = 0;
    via2_t1c = via2_t1l = via2_t2c = via2_t2l = 0;
    via2_sr = 0;

    Idle = false;
}


/*
 *  Reset CPU asynchronously
 */

void MOS6502_1541::AsyncReset(void)
{
    interrupt.intr[INT_RESET] = true;
    Idle = false;
}


/*
 *  Read a byte from I/O space
 */

inline uint8 MOS6502_1541::read_byte_io(uint16 adr)
{
    if ((adr & 0xfc00) == 0x1800)   // VIA 1
        switch (adr & 0xf) {
            case 0:
                return ((via1_prb & 0x1a)
                    | ((IECLines & TheCIA2->IECLines) >> 7)     // DATA
                    | (((IECLines & TheCIA2->IECLines) >> 4) & 0x04)    // CLK
                    | ((TheCIA2->IECLines << 3) & 0x80)) ^ 0x85;        // ATN
            case 1:
                via1_ifr &= 0xfd;       // Clear CA1 interrupt
                interrupt.intr[INT_VIA1IRQ] = false;
                return 0xff;    // Keep 1541C ROMs happy (track 0 sensor)
            case 15:
                return 0xff;    // Keep 1541C ROMs happy (track 0 sensor)
            case 2:
                return via1_ddrb;
            case 3:
                return via1_ddra;
            case 4:
                via1_ifr &= 0xbf;
                return via1_t1c;
            case 5:
                return via1_t1c >> 8;
            case 6:
                return via1_t1l;
            case 7:
                return via1_t1l >> 8;
            case 8:
                via1_ifr &= 0xdf;
                return via1_t2c;
            case 9:
                return via1_t2c >> 8;
            case 10:
                return via1_sr;
            case 11:
                return via1_acr;
            case 12:
                return via1_pcr;
            case 13:
                return via1_ifr | (via1_ifr & via1_ier ? 0x80 : 0);
            case 14:
                return via1_ier | 0x80;
            default:    // Can't happen
                return 0;
        }

    else if ((adr & 0xfc00) == 0x1c00)  // VIA 2
        switch (adr & 0xf) {
            case 0:
                if (the_job->SyncFound(cycle_counter))
                    return (via2_prb & 0x7f) | the_job->WPState();
                else
                    return (via2_prb | 0x80) | the_job->WPState();
            case 1:
            case 15:
                return the_job->ReadGCRByte(cycle_counter);
            case 2:
                return via2_ddrb;
            case 3:
                return via2_ddra;
            case 4:
                via2_ifr &= 0xbf;
                interrupt.intr[INT_VIA2IRQ] = false;    // Clear IRQ
                return via2_t1c;
            case 5:
                return via2_t1c >> 8;
            case 6:
                return via2_t1l;
            case 7:
                return via2_t1l >> 8;
            case 8:
                via2_ifr &= 0xdf;
                return via2_t2c;
            case 9:
                return via2_t2c >> 8;
            case 10:
                return via2_sr;
            case 11:
                return via2_acr;
            case 12:
                return via2_pcr;
            case 13:
                return via2_ifr | (via2_ifr & via2_ier ? 0x80 : 0);
            case 14:
                return via2_ier | 0x80;
            default:    // Can't happen
                return 0;
        }

    else
        return adr >> 8;
}


/*
 *  Read a byte from the CPU's address space
 */

// This can be used when we know we're reading an 'immediate' byte from ROM
inline uint8 MOS6502_1541::read_byte_fast(uint16 adr)
{
    if (adr & 0xC000)
        return rom[adr];
    return myRAM1541[adr];
}

// The more general read has to handle RAM, ROM and IO space
uint8 MOS6502_1541::read_byte(uint16 adr)
{
    if (adr & 0xC000)
        return rom[adr];
    else if ((adr & 0x1800) == 0x0000)
        return myRAM1541[adr & 0x07ff];
    else
        return read_byte_io(adr);
}


/*
 *  Read a word (little-endian) from the CPU's address space
 */

inline uint16 MOS6502_1541::read_word(uint16 adr)
{
    return read_byte(adr) | (read_byte(adr+1) << 8);
}


/*
 *  Write a byte to I/O space
 */

void MOS6502_1541::write_byte_io(uint16 adr, uint8 byte)
{
    if ((adr & 0xfc00) == 0x1800)   // VIA 1
        switch (adr & 0xf) {
            case 0:
                via1_prb = byte;
                byte = ~via1_prb & via1_ddrb;
                IECLines = ((byte << 6) & (((~byte ^ TheCIA2->IECLines) << 3) & 0x80)) | ((byte << 3) & 0x40);
                break;
            case 1:
            case 15:
                via1_pra = byte;
                break;
            case 2:
                via1_ddrb = byte;
                byte &= ~via1_prb;
                IECLines = ((byte << 6) & (((~byte ^ TheCIA2->IECLines) << 3) & 0x80)) | ((byte << 3) & 0x40);
                break;
            case 3:
                via1_ddra = byte;
                break;
            case 4:
            case 6:
                via1_t1l = (via1_t1l & 0xff00) | byte;
                break;
            case 5:
                via1_t1l = (via1_t1l & 0xff) | (byte << 8);
                via1_ifr &= 0xbf;
                via1_t1c = via1_t1l;
                break;
            case 7:
                via1_t1l = (via1_t1l & 0xff) | (byte << 8);
                break;
            case 8:
                via1_t2l = (via1_t2l & 0xff00) | byte;
                break;
            case 9:
                via1_t2l = (via1_t2l & 0xff) | (byte << 8);
                via1_ifr &= 0xdf;
                via1_t2c = via1_t2l;
                break;
            case 10:
                via1_sr = byte;
                break;
            case 11:
                via1_acr = byte;
                break;
            case 12:
                via1_pcr = byte;
                break;
            case 13:
                via1_ifr &= ~byte;
                break;
            case 14:
                if (byte & 0x80)
                    via1_ier |= byte & 0x7f;
                else
                    via1_ier &= ~byte;
                break;
        }

    else if ((adr & 0xfc00) == 0x1c00)  // VIA 2
        switch (adr & 0xf) {
            case 0:
                if ((via2_prb ^ byte) & 8)  // Bit 3: Drive LED
                    the_display->UpdateLEDs(byte & 8 ? 1 : 0, 0);

                if ((via2_prb ^ byte) & 0x04) // Bit 2: Spindle motor
                    the_job->SetMotor(byte & 0x04);

                if ((via2_prb ^ byte) & 3)  // Bits 0/1: Stepper motor
                {
                    if ((via2_prb & 3) == ((byte+1) & 3))
                        the_job->MoveHeadOut(cycle_counter);
                    else if ((via2_prb & 3) == ((byte-1) & 3))
                        the_job->MoveHeadIn(cycle_counter);
                }
                via2_prb = byte;
                break;
            case 1:
            case 15:
                via2_pra = byte;
                break;
            case 2:
                via2_ddrb = byte;
                break;
            case 3:
                via2_ddra = byte;
                break;
            case 4:
            case 6:
                via2_t1l = (via2_t1l & 0xff00) | byte;
                break;
            case 5:
                via2_t1l = (via2_t1l & 0xff) | (byte << 8);
                via2_ifr &= 0xbf;
                via2_t1c = via2_t1l;
                break;
            case 7:
                via2_t1l = (via2_t1l & 0xff) | (byte << 8);
                break;
            case 8:
                via2_t2l = (via2_t2l & 0xff00) | byte;
                break;
            case 9:
                via2_t2l = (via2_t2l & 0xff) | (byte << 8);
                via2_ifr &= 0xdf;
                via2_t2c = via2_t2l;
                break;
            case 10:
                via2_sr = byte;
                break;
            case 11:
                via2_acr = byte;
                break;
            case 12:
                via2_pcr = byte;
                break;
            case 13:
                via2_ifr &= ~byte;
                break;
            case 14:
                if (byte & 0x80)
                    via2_ier |= byte & 0x7f;
                else
                    via2_ier &= ~byte;
                break;
        }
}


/*
 *  Write a byte to the CPU's address space
 */

inline void MOS6502_1541::write_byte(uint16 adr, uint8 byte)
{
    if (adr & 0xF800) write_byte_io(adr, byte);
    else myRAM1541[adr] = byte;
}


/*
 *  Read a byte from the zeropage
 */

inline uint8 MOS6502_1541::read_zp(uint16 adr)
{
    return myRAM1541[adr];
}


/*
 *  Read a word (little-endian) from the zeropage
 */

inline uint16 MOS6502_1541::read_zp_word(uint16 adr)
{
    return myRAM1541[adr & 0xff] | (myRAM1541[(adr+1) & 0xff] << 8);
}


/*
 *  Write a byte to the zeropage
 */

inline void MOS6502_1541::write_zp(uint16 adr, uint8 byte)
{
    myRAM1541[adr] = byte;
}



/*
 *  Jump to address
 */

inline void MOS6502_1541::jump(uint16 adr)
{
    pc = adr;
}


/*
 *  Adc instruction
 */

void MOS6502_1541::do_adc(uint8 byte)
{
    if (!d_flag) {
        uint16 tmp;

        // Binary mode
        tmp = a + byte + (c_flag ? 1 : 0);
        c_flag = tmp > 0xff;
        v_flag = !((a ^ byte) & 0x80) && ((a ^ tmp) & 0x80);
        z_flag = n_flag = a = tmp;

    } else {
        uint16 al, ah;

        // Decimal mode
        al = (a & 0x0f) + (byte & 0x0f) + (c_flag ? 1 : 0); // Calculate lower nybble
        if (al > 9) al += 6;                                    // BCD fixup for lower nybble

        ah = (a >> 4) + (byte >> 4);                            // Calculate upper nybble
        if (al > 0x0f) ah++;

        z_flag = a + byte + (c_flag ? 1 : 0);                   // Set flags
        n_flag = ah << 4;   // Only highest bit used
        v_flag = (((ah << 4) ^ a) & 0x80) && !((a ^ byte) & 0x80);

        if (ah > 9) ah += 6;                                    // BCD fixup for upper nybble
        c_flag = ah > 0x0f;                                     // Set carry flag
        a = (ah << 4) | (al & 0x0f);                            // Compose result
    }
}


/*
 * Sbc instruction
 */

void MOS6502_1541::do_sbc(uint8 byte)
{
    uint16 tmp = a - byte - (c_flag ? 0 : 1);

    if (!d_flag) {

        // Binary mode
        c_flag = tmp < 0x100;
        v_flag = ((a ^ tmp) & 0x80) && ((a ^ byte) & 0x80);
        z_flag = n_flag = a = tmp;

    } else {
        uint16 al, ah;

        // Decimal mode
        al = (a & 0x0f) - (byte & 0x0f) - (c_flag ? 0 : 1);     // Calculate lower nybble
        ah = (a >> 4) - (byte >> 4);                            // Calculate upper nybble
        if (al & 0x10) {
            al -= 6;                                            // BCD fixup for lower nybble
            ah--;
        }
        if (ah & 0x10) ah -= 6;                                 // BCD fixup for upper nybble

        c_flag = tmp < 0x100;                                   // Set flags
        v_flag = ((a ^ tmp) & 0x80) && ((a ^ byte) & 0x80);
        z_flag = n_flag = tmp;

        a = (ah << 4) | (al & 0x0f);                            // Compose result
    }
}


/*
 *  Get 6502 register state
 */

void MOS6502_1541::GetState(MOS6502State *s)
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

    s->pc = pc;
    s->sp = sp | 0x0100;

    s->intr[INT_VIA1IRQ] = interrupt.intr[INT_VIA1IRQ];
    s->intr[INT_VIA2IRQ] = interrupt.intr[INT_VIA2IRQ];
    s->intr[INT_RESET] = interrupt.intr[INT_RESET];
    s->instruction_complete = true;
    s->idle = Idle;

    s->via1_pra = via1_pra; s->via1_ddra = via1_ddra;
    s->via1_prb = via1_prb; s->via1_ddrb = via1_ddrb;
    s->via1_t1c = via1_t1c; s->via1_t1l = via1_t1l;
    s->via1_t2c = via1_t2c; s->via1_t2l = via1_t2l;
    s->via1_sr = via1_sr;
    s->via1_acr = via1_acr; s->via1_pcr = via1_pcr;
    s->via1_ifr = via1_ifr; s->via1_ier = via1_ier;

    s->via2_pra = via2_pra; s->via2_ddra = via2_ddra;
    s->via2_prb = via2_prb; s->via2_ddrb = via2_ddrb;
    s->via2_t1c = via2_t1c; s->via2_t1l = via2_t1l;
    s->via2_t2c = via2_t2c; s->via2_t2l = via2_t2l;
    s->via2_sr = via2_sr;
    s->via2_acr = via2_acr; s->via2_pcr = via2_pcr;
    s->via2_ifr = via2_ifr; s->via2_ier = via2_ier;
    
    s->cycle_counter = cycle_counter;
}


/*
 *  Restore 6502 state
 */

void MOS6502_1541::SetState(MOS6502State *s)
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

    jump(s->pc);
    sp = s->sp & 0xff;

    interrupt.intr[INT_VIA1IRQ] = s->intr[INT_VIA1IRQ];
    interrupt.intr[INT_VIA2IRQ] = s->intr[INT_VIA2IRQ];
    interrupt.intr[INT_RESET] = s->intr[INT_RESET];
    Idle = s->idle;

    via1_pra = s->via1_pra; via1_ddra = s->via1_ddra;
    via1_prb = s->via1_prb; via1_ddrb = s->via1_ddrb;
    via1_t1c = s->via1_t1c; via1_t1l = s->via1_t1l;
    via1_t2c = s->via1_t2c; via1_t2l = s->via1_t2l;
    via1_sr = s->via1_sr;
    via1_acr = s->via1_acr; via1_pcr = s->via1_pcr;
    via1_ifr = s->via1_ifr; via1_ier = s->via1_ier;

    via2_pra = s->via2_pra; via2_ddra = s->via2_ddra;
    via2_prb = s->via2_prb; via2_ddrb = s->via2_ddrb;
    via2_t1c = s->via2_t1c; via2_t1l = s->via2_t1l;
    via2_t2c = s->via2_t2c; via2_t2l = s->via2_t2l;
    via2_sr = s->via2_sr;
    via2_acr = s->via2_acr; via2_pcr = s->via2_pcr;
    via2_ifr = s->via2_ifr; via2_ier = s->via2_ier;
    
    cycle_counter = s->cycle_counter;
}


/*
 *  Reset CPU
 */

void MOS6502_1541::Reset(void)
{
    the_job->Reset();

    // IEC lines and VIA registers
    IECLines = 0xc0;

    via1_pra = via1_ddra = via1_prb = via1_ddrb = 0;
    via1_acr = via1_pcr = 0;
    via1_ifr = via1_ier = 0;
    via2_pra = via2_ddra = via2_prb = via2_ddrb = 0;
    via2_acr = via2_pcr = 0;
    via2_ifr = via2_ier = 0;

    // Clear all interrupt lines
    interrupt.intr_any = 0;
    
    cycle_counter = 0;
    borrowed_cycles = 0;

    // Read reset vector
    jump(read_word(0xfffc));

    // Wake up 1541
    Idle = false;
}


/*
 *  Illegal opcode encountered
 */

void MOS6502_1541::illegal_op(uint8 op, uint16 at)
{
    char illop_msg[80];

    sprintf(illop_msg, "1541: Illegal opcode %02x at %04x.", op, at);
    if (ShowRequester(illop_msg, "Reset 1541", "Reset C64"))
        the_c64->Reset();
    Reset();
}


/*
 *  Stack macros
 */

// Pop a byte from the stack
#define pop_byte() myRAM1541[(++sp) | 0x0100]

// Push a byte onto the stack
#define push_byte(byte) (myRAM1541[((sp--) & 0xff) | 0x0100] = (byte))

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
    if (((via2_pcr & 0x0e) == 0x0e) && the_job->ByteReady(cycle_counter)) v_flag = true; \
    if (v_flag) tmp |= 0x40; \
    if (b_flag) tmp |= 0x10; \
    if (d_flag) tmp |= 0x08; \
    if (i_flag) tmp |= 0x04; \
    if (!z_flag) tmp |= 0x02; \
    if (c_flag) tmp |= 0x01; \
    push_byte(tmp);


void MOS6502_1541::ext_opcode(void)
{
    if (pc < 0xc000) {
        illegal_op(0xf2, pc-1);
    }
    switch (read_byte(pc++)) {
        case 0x00:  // Go to sleep in DOS idle loop if error flag is clear and no command received
            Idle = !(myRAM1541[0x26c] | myRAM1541[0x7c]);
            jump(0xebff);
            break;
        case 0x01:  // Write sector
            the_job->WriteSector();
            jump(0xf5dc);
            break;
        case 0x02:  // Format track
            the_job->FormatTrack();
            jump(0xfd8b);
            break;
        default:
            illegal_op(0xf2, pc-1);
            break;
    }
}

/*
 *  Emulate cycles_left worth of 6502 instructions
 *  Returns number of cycles of last instruction
 */

int MOS6502_1541::EmulateLine(int cycles_left, int cpu_cycles)
{
    uint8 tmp, tmp2;
    uint16 adr;
    int last_cycles = 0;

    // Any pending interrupts?
    if (interrupt.intr_any) {
handle_int:
        if (interrupt.intr[INT_RESET])
        {
            Reset();
        }
        else if ((interrupt.intr[INT_VIA1IRQ] || interrupt.intr[INT_VIA2IRQ]) && !i_flag)
        {
            push_byte(pc >> 8); push_byte(pc);
            push_flags(false);
            i_flag = true;
            jump(read_word(0xfffe));
            last_cycles = 7;
        }
    }

#define IS_CPU_1541
#include "CPU_emulline.h"

    return last_cycles;
}
