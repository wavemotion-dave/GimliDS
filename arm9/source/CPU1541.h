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
 *  CPU1541.h - 6502 (1541) emulation (line based)
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

#ifndef _CPU_1541_H
#define _CPU_1541_H

#include "CIA.h"
#include "C64.h"


// Set this to 1 for more precise CPU cycle calculation
#define PRECISE_CPU_CYCLES 1


// Interrupt types
enum {
    INT_VIA1IRQ,
    INT_VIA2IRQ,
    INT_RESET
};


class C64;
class Job1541;
class C64Display;
struct MOS6502State;


// 6502 emulation (1541)
class MOS6502_1541 {
public:
    MOS6502_1541(C64 *c64, Job1541 *job, C64Display *disp, uint8 *Ram, uint8 *Rom);

    int EmulateLine(int cycles_left, int cpu_cycles);   // Emulate until cycles_left underflows
    void extended_opcode(void);
    void Reset(void);
    void AsyncReset(void);              // Reset the CPU asynchronously
    void GetState(MOS6502State *s);
    void SetState(MOS6502State *s);
    void CountVIATimers(int cycles);
    void NewATNState(void);
    void TriggerIECInterrupt(void);
    void trigger_via1_irq(void);
    void trigger_via2_irq(void);
    bool InterruptEnabled(void);
    uint32_t CycleCounter() const { return cycle_counter; }

    MOS6526_2 *TheCIA2;     // Pointer to C64 CIA 2

    uint8 IECLines;         // State of IEC lines (bit 7 - DATA, bit 6 - CLK)
    uint8 Idle;             // true: 1541 is idle

private:
    uint8 read_byte(uint16 adr);
    uint8 read_byte_fast(uint16 adr);
    uint8 read_byte_io(uint16 adr);
    uint16 read_word(uint16 adr);
    uint16 read_word_pc(void);
    void write_byte(uint16 adr, uint8 byte);
    void write_byte_io(uint16 adr, uint8 byte);

    uint8 read_zp(uint16 adr);
    uint16 read_zp_word(uint16 adr);
    void write_zp(uint16 adr, uint8 byte);

    void jump(uint16 adr);
    void illegal_op(uint8 op, uint16 at);

    void do_adc(uint8 byte);
    void do_sbc(uint8 byte);

    uint8 *ram;             // Pointer to main RAM
    uint8 *rom;             // Pointer to ROM
    C64 *the_c64;           // Pointer to C64 object
    C64Display *the_display; // Pointer to C64 display object
    Job1541 *the_job;       // Pointer to 1541 job object

    union {                 // Pending interrupts
        uint8 intr[4];      // Index: See definitions above
        unsigned long intr_any;
    } interrupt;

    uint8 z_flag, n_flag;
    uint8 v_flag, d_flag, i_flag, c_flag;
    uint8 a, x, y, sp;
    uint16_t pc;

    uint32 cycle_counter;// Track total cycles of 1541 emulation
    int borrowed_cycles; // Borrowed cycles from next line

    uint8 via1_pra;     // PRA of VIA 1
    uint8 via1_ddra;    // DDRA of VIA 1
    uint8 via1_prb;     // PRB of VIA 1
    uint8 via1_ddrb;    // DDRB of VIA 1
    uint16 via1_t1c;    // T1 Counter of VIA 1
    uint16 via1_t1l;    // T1 Latch of VIA 1
    uint16 via1_t2c;    // T2 Counter of VIA 1
    uint16 via1_t2l;    // T2 Latch of VIA 1
    uint8 via1_sr;      // SR of VIA 1
    uint8 via1_acr;     // ACR of VIA 1
    uint8 via1_pcr;     // PCR of VIA 1
    uint8 via1_ifr;     // IFR of VIA 1
    uint8 via1_ier;     // IER of VIA 1

    uint8 via2_pra;     // PRA of VIA 2
    uint8 via2_ddra;    // DDRA of VIA 2
    uint8 via2_prb;     // PRB of VIA 2
    uint8 via2_ddrb;    // DDRB of VIA 2
    uint16 via2_t1c;    // T1 Counter of VIA 2
    uint16 via2_t1l;    // T1 Latch of VIA 2
    uint16 via2_t2c;    // T2 Counter of VIA 2
    uint16 via2_t2l;    // T2 Latch of VIA 2
    uint8 via2_sr;      // SR of VIA 2
    uint8 via2_acr;     // ACR of VIA 2
    uint8 via2_pcr;     // PCR of VIA 2
    uint8 via2_ifr;     // IFR of VIA 2
    uint8 via2_ier;     // IER of VIA 2
};

// 6502 state
struct MOS6502State {
    uint8 a, x, y;
    uint8 p;            // Processor flags
    uint16 pc, sp;

    uint8 intr[4];      // Interrupt state
    bool instruction_complete;
    bool idle;

    uint8 via1_pra;     // VIA 1
    uint8 via1_ddra;
    uint8 via1_prb;
    uint8 via1_ddrb;
    uint16 via1_t1c;
    uint16 via1_t1l;
    uint16 via1_t2c;
    uint16 via1_t2l;
    uint8 via1_sr;
    uint8 via1_acr;
    uint8 via1_pcr;
    uint8 via1_ifr;
    uint8 via1_ier;

    uint8 via2_pra;     // VIA 2
    uint8 via2_ddra;
    uint8 via2_prb;
    uint8 via2_ddrb;
    uint16 via2_t1c;
    uint16 via2_t1l;
    uint16 via2_t2c;
    uint16 via2_t2l;
    uint8 via2_sr;
    uint8 via2_acr;
    uint8 via2_pcr;
    uint8 via2_ifr;
    uint8 via2_ier;
    
    uint32 cycle_counter;
    
    uint8 spare1;
    uint8 spare2;
    uint16 spare3;
    uint32 spare4;        
};



/*
 *  Trigger VIA1 IRQ
 */
inline void MOS6502_1541::trigger_via1_irq(void)
{
    interrupt.intr[INT_VIA1IRQ] = true;
    Idle = false;
}

/*
 *  Trigger VIA2 IRQ
 */
inline void MOS6502_1541::trigger_via2_irq(void)
{
    interrupt.intr[INT_VIA2IRQ] = true;
    Idle = false;
}


/*
 *  Count VIA timers
 */

inline void MOS6502_1541::CountVIATimers(int cycles)
{
    unsigned long tmp;

    via1_t1c = tmp = via1_t1c - cycles;
    if (tmp > 0xffff) {
        via1_t1c = via1_t1l;  // Reload from latch
        via1_ifr |= 0x40;
    }

    if (!(via1_acr & 0x20)) {   // Only count in one-shot mode
        via1_t2c = tmp = via1_t2c - cycles;
        if (tmp > 0xffff)
            via1_ifr |= 0x20;
    }

    via2_t1c = tmp = via2_t1c - cycles;
    if (tmp > 0xffff) {
        via2_t1c = via2_t1l;  // Reload from latch
        via2_ifr |= 0x40;
        if (via2_ier & 0x40)
            trigger_via2_irq();
    }

    if (!(via2_acr & 0x20)) {   // Only count in one-shot mode
        via2_t2c = tmp = via2_t2c - cycles;
        if (tmp > 0xffff)
            via2_ifr |= 0x20;
    }
}


/*
 *  ATN line probably changed state, recalc IECLines
 */

inline void MOS6502_1541::NewATNState(void)
{
    uint8 byte = ~via1_prb & via1_ddrb;
    IECLines = ((byte << 6) & ((~byte ^ TheCIA2->IECLines) << 3) & 0x80)    // DATA (incl. ATN acknowledge)
        | ((byte << 3) & 0x40);                                         // CLK
}


/*
 *  Interrupt by negative edge of ATN on IEC bus
 */

inline void MOS6502_1541::TriggerIECInterrupt(void)
{
    if (via1_pcr & 0x01) // CA1 positive edge (1541 gets inverted bus signal)
    {
        via1_ifr |= 0x02;
        if (via1_ier & 0x02) // CA1 interrupt enabled?
        {
            trigger_via1_irq();
        }
    }
}


/*
 *  Test if interrupts are enabled (for job loop)
 */

inline bool MOS6502_1541::InterruptEnabled(void)
{
    return !i_flag;
}

#endif
