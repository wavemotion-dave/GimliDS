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
 *  REU.h - 17xx REU and GeoRAM emulation
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

#ifndef REU_H
#define REU_H

#include "Cartridge.h"

class MOS6510;

struct REUState 
{
    uint32_t ram_size;  // Size of expansion RAM
    uint32_t ram_mask;  // Expansion RAM address bit mask

    uint8_t regs[16];   // REU registers

    uint8_t autoload_c64_adr_lo;    // Autoload registers
    uint8_t autoload_c64_adr_hi;
    uint8_t autoload_reu_adr_lo;
    uint8_t autoload_reu_adr_hi;
    uint8_t autoload_reu_adr_bank;
    uint8_t autoload_length_lo;
    uint8_t autoload_length_hi;
};

extern u8 REU_RAM[256 * 1024];

// REU cartridge object
class REU : public Cartridge {
public:
    REU(MOS6510 * cpu);
    ~REU();

    void Reset() override;

    uint8_t ReadIO2(uint16_t adr, uint8_t bus_byte) override;
    void WriteIO2(uint16_t adr, uint8_t byte) override;
    
    void GetState(REUState *rs);
    void SetState(REUState *rs);

private:
    void execute_dma();

    MOS6510 * the_cpu;  // Pointer to 6510 object

    uint8_t * ex_ram;   // Expansion RAM

    uint32_t ram_size;  // Size of expansion RAM
    uint32_t ram_mask;  // Expansion RAM address bit mask

    uint8_t regs[16];   // REU registers

    uint8_t autoload_c64_adr_lo;    // Autoload registers
    uint8_t autoload_c64_adr_hi;
    uint8_t autoload_reu_adr_lo;
    uint8_t autoload_reu_adr_hi;
    uint8_t autoload_reu_adr_bank;
    uint8_t autoload_length_lo;
    uint8_t autoload_length_hi;
};

// RAM expansion types
enum {
    REU_NONE,       // No REU
    REU_128K,       // 128K REU
    REU_256K,       // 256K REU
    REU_512K,       // 512K REU
    REU_GEORAM      // 512K GeoRAM
};

#endif // ndef REU_H
