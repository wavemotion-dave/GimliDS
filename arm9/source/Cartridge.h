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
 *  Cartridge.h - Cartridge emulation
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

#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include "sysdeps.h"

#include <string>

struct CartridgeState {
    u8 notEXROM;
    u8 notGAME;
    u8 bank;
    uint8_t ram[256];
};


// Base class for cartridges
class Cartridge {
public:
    Cartridge() { }
    virtual ~Cartridge() { }

    void GetState(CartridgeState *cs);
    void SetState(CartridgeState *cs);

    static Cartridge * FromFile(char *filename, char *errBuffer);

    virtual void Reset() { }

    bool isTrueDriveRequired(void);

    // Map cart into the CPU memory map...
    virtual void MapThyself(void)
    {
        // Do nothing... must be overridden.
    }

    // Default for I/O 1 and 2 is open bus
    virtual uint8_t ReadIO1(uint16_t adr, uint8_t bus_byte) { return bus_byte; }
    virtual void WriteIO1(uint16_t adr, uint8_t byte) { }
    virtual uint8_t ReadIO2(uint16_t adr, uint8_t bus_byte) { return bus_byte; }
    virtual void WriteIO2(uint16_t adr, uint8_t byte) { }

    u32 total_cart_size = 0;
    u8  last_bank = 0;
    u8  cart_type = 0;
    // Memory mapping control lines
    u8 notEXROM = true;
    u8 notGAME = true;
    u8 bank = 0;          // Selected bank
    u8 bTrueDriveRequired = false;
    uint8 ram[256];             // Mostly for EasyFlash but can be used for any cart that maps some RAM
    uint8_t * rom = nullptr;    // Pointer to ROM contents
};


// No cartridge
class NoCartridge : public Cartridge {
public:
    NoCartridge() { }
    ~NoCartridge() { }
};


// Base class for cartridge with ROM
class ROMCartridge : public Cartridge {
public:
    ROMCartridge(unsigned num_banks, unsigned bank_size);
    ~ROMCartridge();
    void StandardMapping(int32 hi_bank_offset);

    uint8_t * ROM() const { return rom; }

    const unsigned numBanks;
    const unsigned bankSize;
};


// 8K ROM cartridge (EXROM = 0, GAME = 1)
class Cartridge8K : public ROMCartridge {
public:
    Cartridge8K();

    void MapThyself(void) override;
};


// 16K ROM cartridge (EXROM = 0, GAME = 0)
class Cartridge16K : public ROMCartridge {
public:
    Cartridge16K();

    void MapThyself(void) override;
};


// Ocean cartridge (banked 8K/16K ROM cartridge)
class CartridgeOcean : public ROMCartridge {
public:
    CartridgeOcean(bool not_game);

    void Reset() override;
    void MapThyself(void) override;
    void WriteIO1(uint16_t adr, uint8_t byte) override;
};


// Super Games cartridge (banked 16K ROM cartridge)
class CartridgeSuperGames : public ROMCartridge {
public:
    CartridgeSuperGames();

    void Reset() override;
    void MapThyself(void);
    void WriteIO2(uint16_t adr, uint8_t byte) override;

protected:
    bool disableIO2 = false;    // Flag: I/O 2 area disabled
};


// C64 Games System / System 3 cartridge (banked 8K ROM cartridge)
class CartridgeC64GS : public ROMCartridge {
public:
    CartridgeC64GS();

    void Reset() override;
    void MapThyself(void) override;
    uint8_t ReadIO1(uint16_t adr, uint8_t bus_byte) override;
    void WriteIO1(uint16_t adr, uint8_t byte) override;
};


// Dinamic cartridge (banked 8K ROM cartridge)
class CartridgeDinamic : public ROMCartridge {
public:
    CartridgeDinamic();

    void Reset() override;
    void MapThyself(void) override;
    uint8_t ReadIO1(uint16_t adr, uint8_t bus_byte) override;
};


// Magic Desk cartridge (banked 8K ROM cartridge)
class CartridgeMagicDesk : public ROMCartridge {
public:
    CartridgeMagicDesk();

    void Reset() override;
    void MapThyself(void) override;
    void WriteIO1(uint16_t adr, uint8_t byte) override;
};


// Ocean cartridge (banked 8K/16K ROM cartridge)
class CartridgeEasyFlash : public ROMCartridge {
public:
    CartridgeEasyFlash(bool not_game, bool not_exrom);

    void Reset() override;
    void MapThyself(void) override;
    void WriteIO1(uint16_t adr, uint8_t byte) override;
    void WriteIO2(uint16_t adr, uint8_t byte) override;
    uint8_t ReadIO1(uint16_t adr, uint8_t bus_byte) override;
    uint8_t ReadIO2(uint16_t adr, uint8_t bus_byte) override;
};


#endif // ndef CARTRIDGE_H
