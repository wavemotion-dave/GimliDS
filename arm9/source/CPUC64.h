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
 *  CPUC64.h - 6510 (C64) emulation (line based)
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

#ifndef _CPU_C64_H
#define _CPU_C64_H

#include "C64.h"

// Set this to 1 for more precise CPU cycle calculation
#ifndef PRECISE_CPU_CYCLES
#define PRECISE_CPU_CYCLES 0
#endif

// Interrupt types
enum {
    INT_VICIRQ,
    INT_CIAIRQ,
    INT_NMI
    // INT_RESET (private)
};

class MOS6569;
class MOS6581;
class MOS6526_1;
class MOS6526_2;
class IEC;
class Cartridge;
struct MOS6510State;

// 6510 emulation (C64)
class MOS6510 {
public:
    MOS6510();

    void Init(C64 *c64, uint8 *Ram, uint8 *Basic, uint8 *Kernal, uint8 *Char, uint8 *Color);
    int EmulateLine(int cycles_left);   // Emulate until cycles_left underflows
    void IntNMI(void);
    void Reset(void);
    void AsyncReset(void);              // Reset the CPU asynchronously
    void AsyncNMI(void);                // Raise NMI asynchronously (NMI pulse)
    void GetState(MOS6510State *s);
    void SetState(MOS6510State *s);

    void TriggerVICIRQ(void);
    void ClearVICIRQ(void);
    void TriggerCIAIRQ(void);
    void ClearCIAIRQ(void);
    void TriggerNMI(void);
    void ClearNMI(void);
    void setCharVsIO(void);
    uint8_t REUReadByte(uint16_t adr);
    void REUWriteByte(uint16_t adr, uint8_t byte);

    MOS6569 *TheVIC;    // Pointer to VIC
    MOS6581 *TheSID;    // Pointer to SID
    MOS6526_1 *TheCIA1; // Pointer to CIA 1
    MOS6526_2 *TheCIA2; // Pointer to CIA 2
    IEC *TheIEC;        // Pointer to drive array
    Cartridge *TheCart; // Pointer to cartridge object
    REU *TheREU;        // Pointer to REU object

private:
    void extended_opcode(void);
    uint8 read_byte(uint16 adr);
    uint8 read_byte_io(uint16 adr);
    uint8 read_byte_io_cart(uint16 adr);
    uint16 read_word(uint16 adr);
    uint16 read_word_slow(uint16 adr);
    void write_byte(uint16 adr, uint8 byte);
    void write_byte_io(uint16 adr, uint8 byte);

    uint8 read_zp(uint16 adr);
    uint16 read_zp_word(uint16 adr);
    void write_zp(uint16 adr, uint8 byte);

    void new_config(void);
    void illegal_op(uint8 op, uint16 at);

    void do_adc(uint8 byte);
    void do_sbc(uint8 byte);

    uint8 read_emulator_id(uint16 adr);

    uint16_t pc;

    C64 *the_c64;       // Pointer to C64 object

    uint8 *ram;         // Pointer to main RAM
    uint8 *basic_rom, *kernal_rom, *char_rom, *color_ram; // Pointers to ROMs and color RAM
    
    union {             // Pending interrupts
        uint8 intr[4];  // Index: See definitions above
        unsigned long intr_any;
    } interrupt;
    bool nmi_state;     // State of NMI line

    uint8 z_flag, n_flag;
    uint8 v_flag, d_flag, i_flag, c_flag;
    uint8 a, x, y, sp;

    int borrowed_cycles;    // Borrowed cycles from next line

    uint8 basic_in, kernal_in, char_in, io_in;
    uint8 dfff_byte;
};

// 6510 state
struct MOS6510State {
    uint8 a, x, y;
    uint8 p;            // Processor flags
    uint8 ddr, pr;      // Port
    uint16 pc, sp;
    uint8 intr[4];      // Interrupt state
    bool nmi_state;
    uint8 dfff_byte;
    bool instruction_complete;
    uint8 MemMap_Type[0x10];
    int32 MemMap_Offset[0x10];
    uint8 spare1;
    uint8 spare2;
    uint16 spare3;
    uint32 spare4;
};


// Interrupt functions
inline void MOS6510::TriggerVICIRQ(void)
{
    interrupt.intr[INT_VICIRQ] = true;
}

inline void MOS6510::TriggerCIAIRQ(void)
{
    interrupt.intr[INT_CIAIRQ] = true;
}

inline void MOS6510::TriggerNMI(void)
{
    if (!nmi_state) {
        nmi_state = true;
        interrupt.intr[INT_NMI] = true;
    }
}

inline void MOS6510::ClearVICIRQ(void)
{
    interrupt.intr[INT_VICIRQ] = false;
}

inline void MOS6510::ClearCIAIRQ(void)
{
    interrupt.intr[INT_CIAIRQ] = false;
}

inline void MOS6510::ClearNMI(void)
{
    nmi_state = false;
}

#endif
