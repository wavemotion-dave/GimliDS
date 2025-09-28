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
 *  Cartridge.cpp - Cartridge emulation
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
#include "Cartridge.h"
#include "mainmenu.h"
#include "diskmenu.h"
#include "printf.h"
#include <filesystem>
namespace fs = std::filesystem;

uint8 cart_led = 0;         // Used to briefly 'light up' the cart icon for Easy Flash 256 byte RAM access
uint8 cart_led_color = 0;   // Yellow

extern uint8 *MemMap[0x10];
extern u8 myBASIC[];
extern u8 myKERNAL[];

u8 *cartROM = NULL;    // 1MB max supported cart size (not including .crt and chip headers). For DSi this gets bumped up to 2MB.
extern C64 *gTheC64;   // Easy access to the main C64 object

extern char CartType[16];   // For debug mostly
char tmpFilename[256];

// Base class for cartridge with ROM
ROMCartridge::ROMCartridge(unsigned num_banks, unsigned bank_size) : numBanks(num_banks), bankSize(bank_size)
{
    // Allocate the cart memory only once... for DSi we can support 2MB carts (MagicDesk 16K) and for DS-Lite/Phat, 1MB max
    if (cartROM == NULL)
    {
        if (isDSiMode())
        {
            cartROM = (u8*)malloc(2*1024*1024);
        }
        else 
        {
            cartROM = (u8*)malloc(1*1024*1024);
        }
    }
    // We always re-use the same cart ROM buffer...
    rom = cartROM;
    memset(rom, 0xff, (isDSiMode() ? (2*1024*1024) : (1*1024*1024)));
    cart_led = 0;
    cart_led_color = 0;
    dirtyFlash = false;
    memset(ram, 0xff, sizeof(ram));
    
    strcpy(CartType, "NONE");
}

ROMCartridge::~ROMCartridge()
{
}

// Simplified mapping table without CHAREN
// _EXROM  _GAME   HIRAM   LORAM   $0000-$0FFF   $1000-$7FFF  $8000-$9FFF   $A000-$BFFF   $C000-$CFFF    $D000-$DFFF    $E000-$FFFF
//   1       1      1       1       RAM           RAM          RAM          BASIC          RAM           CHAR/IO         KERNAL
//   1       1      1       0       RAM           RAM          RAM          RAM            RAM           CHAR/IO         KERNAL
//   1       1      0       1       RAM           RAM          RAM          RAM            RAM           CHAR/IO         RAM
//   1       1      0       0       RAM           RAM          RAM          RAM            RAM           RAM             RAM
//
//   1       0      1       1       RAM           -            CARTLO       -              -             CHAR/IO         CARTHI     (Ultimax mode)
//   1       0      1       0       RAM           -            CARTLO       -              -             CHAR/IO         CARTHI     (Ultimax mode)
//   1       0      0       1       RAM           -            CARTLO       -              -             CHAR/IO         CARTHI     (Ultimax mode)
//   1       0      0       0       RAM           -            CARTLO       -              -             CHAR/IO         CARTHI     (Ultimax mode)
//
//   0       1      1       1       RAM           RAM          CARTLO       BASIC          RAM           CHAR/IO         KERNAL
//   0       1      1       0       RAM           RAM          RAM          RAM            RAM           CHAR/IO         KERNAL
//   0       1      0       1       RAM           RAM          RAM          RAM            RAM           CHAR/IO         RAM
//   0       1      0       0       RAM           RAM          RAM          RAM            RAM           RAM             RAM
//
//   0       0      1       1       RAM           RAM          CARTLO       CARTHI         RAM           CHAR/IO         KERNAL
//   0       0      1       0       RAM           RAM          RAM          CARTHI         RAM           CHAR/IO         KERNAL
//   0       0      0       1       RAM           RAM          RAM          RAM            RAM           RAM             RAM
//   0       0      0       0       RAM           RAM          RAM          RAM            RAM           RAM             RAM

void ROMCartridge::StandardMapping(int32 hi_bank_offset)
{
    uint8 port = (~myRAM[0] | myRAM[1]);
    uint8 portMap = 0x00;
    if (notEXROM)  portMap |= 0x08;  // _EXROM
    if (notGAME)   portMap |= 0x04;  // _GAME
    if (port & 2)  portMap |= 0x02;  // HI_RAM
    if (port & 1)  portMap |= 0x01;  // LO_RAM
    
    ultimax_mode = false;

    switch (portMap)
    {
        case 0xF:
            MemMap[0x8]=myRAM;
            MemMap[0x9]=myRAM;
            MemMap[0xa]=myBASIC  - 0xa000;
            MemMap[0xb]=myBASIC  - 0xa000;
            MemMap[0xe]=myKERNAL - 0xe000;
            MemMap[0xf]=myKERNAL - 0xe000;
            break;
        case 0xE:
            MemMap[0x8]=myRAM;
            MemMap[0x9]=myRAM;
            MemMap[0xa]=myRAM;
            MemMap[0xb]=myRAM;
            MemMap[0xe]=myKERNAL - 0xe000;
            MemMap[0xf]=myKERNAL - 0xe000;
            break;

        case 0xB:
        case 0xA:
        case 0x9:
        case 0x8:
            // Cart Lo at 8000, Cart Hi at E000 (UltiMax mode)
            MemMap[0x8]=rom + ((uint32)bank * bankSize) - 0x8000;
            MemMap[0x9]=rom + ((uint32)bank * bankSize) - 0x8000;
            MemMap[0xa]=myRAM; // Technically N/C but for now...
            MemMap[0xb]=myRAM; // Technically N/C but for now...
            MemMap[0xe]=rom + ((uint32)bank * bankSize) + hi_bank_offset - 0xe000;
            MemMap[0xf]=rom + ((uint32)bank * bankSize) + hi_bank_offset - 0xe000;
            ultimax_mode = true;
            break;

        case 0x7:
            MemMap[0x8]=rom + ((uint32)bank * bankSize) - 0x8000;
            MemMap[0x9]=rom + ((uint32)bank * bankSize) - 0x8000;
            MemMap[0xa]=myBASIC  - 0xa000;
            MemMap[0xb]=myBASIC  - 0xa000;
            MemMap[0xe]=myKERNAL - 0xe000;
            MemMap[0xf]=myKERNAL - 0xe000;
            break;

        case 0x6:
            MemMap[0x8]=myRAM;
            MemMap[0x9]=myRAM;
            MemMap[0xa]=myRAM;
            MemMap[0xb]=myRAM;
            MemMap[0xe]=myKERNAL - 0xe000;
            MemMap[0xf]=myKERNAL - 0xe000;
            break;

        case 0x3:
            MemMap[0x8]=rom + ((uint32)bank * bankSize) - 0x8000;
            MemMap[0x9]=rom + ((uint32)bank * bankSize) - 0x8000;
            MemMap[0xa]=rom + ((uint32)bank * bankSize) + hi_bank_offset - 0xa000;
            MemMap[0xb]=rom + ((uint32)bank * bankSize) + hi_bank_offset - 0xa000;
            MemMap[0xe]=myKERNAL - 0xe000;
            MemMap[0xf]=myKERNAL - 0xe000;
            break;

        case 0x2:
            MemMap[0x8]=myRAM;
            MemMap[0x9]=myRAM;
            MemMap[0xa]=rom + ((uint32)bank * bankSize) + hi_bank_offset - 0xa000;
            MemMap[0xb]=rom + ((uint32)bank * bankSize) + hi_bank_offset - 0xa000;
            MemMap[0xe]=myKERNAL - 0xe000;
            MemMap[0xf]=myKERNAL - 0xe000;
            break;

        default:
            MemMap[0x8]=myRAM;
            MemMap[0x9]=myRAM;
            MemMap[0xa]=myRAM;
            MemMap[0xb]=myRAM;
            MemMap[0xe]=myRAM;
            MemMap[0xf]=myRAM;
            break;
    }

    gTheC64->TheCPU->setCharVsIO();
}


bool Cartridge::isTrueDriveRequired(void)
{
    return bTrueDriveRequired;
}

// Called once per frame - see if there is any cart data that needs persisting
void Cartridge::CartFrame(void)
{
    if (dirtyFlash)
    {
        if (--dirtyFlash == 0)
        {
            PersistFlash();
        }
    }
}

// 8K ROM cartridge (EXROM = 0, GAME = 1)
Cartridge8K::Cartridge8K() : ROMCartridge(1, 0x2000)
{
    notEXROM = false;
    strcpy(CartType, "STD8K");
}

void Cartridge8K::MapThyself(void)
{
    uint8 lo_ram = (~myRAM[0] | myRAM[1]) & 1;
    uint8 hi_ram = (~myRAM[0] | myRAM[1]) & 2;

    if (lo_ram && hi_ram)
    {
        MemMap[0x8]=rom-0x8000;
        MemMap[0x9]=rom-0x8000;
    }
}


// 16K ROM cartridge (EXROM = 0, GAME = 0)
Cartridge16K::Cartridge16K() : ROMCartridge(1, 0x4000)
{
    notEXROM = false;
    notGAME = false;
    strcpy(CartType, "STD16K");
}

void Cartridge16K::MapThyself(void)
{
    uint8 lo_ram = (~myRAM[0] | myRAM[1]) & 1;
    uint8 hi_ram = (~myRAM[0] | myRAM[1]) & 2;

    if (lo_ram && hi_ram)
    {
        MemMap[0x8]=rom-0x8000;
        MemMap[0x9]=rom-0x8000;
    }

    if (hi_ram)
    {
        MemMap[0xa]=rom-0xa000+0x2000;
        MemMap[0xb]=rom-0xa000+0x2000;
    }
}


// Ocean cartridge (banked 8K/16K ROM cartridge)
CartridgeOcean::CartridgeOcean(bool not_game) : ROMCartridge(64, 0x2000)
{
    notEXROM = false;
    notGAME = not_game;
    MapThyself();
    strcpy(CartType, "OCEAN");
}

void CartridgeOcean::Reset()
{
    bank = 0;
    MapThyself();
}

void CartridgeOcean::MapThyself(void)
{
    StandardMapping(0); // Same LO and HI ROM map
}

void CartridgeOcean::WriteIO1(uint16_t adr, uint8_t byte)
{
    bank = byte & 0x3f;
    MapThyself();
}


// Super Games cartridge (banked 16K ROM cartridge)
CartridgeSuperGames::CartridgeSuperGames() : ROMCartridge(4, 0x4000)
{
    notEXROM = false;
    notGAME = false;
    MapThyself();
    strcpy(CartType, "SUPERGAME");
}

void CartridgeSuperGames::Reset()
{
    notEXROM = false;
    notGAME = false;

    bank = 0;
    disableIO2 = false;
    MapThyself();
}

void CartridgeSuperGames::MapThyself(void)
{
    StandardMapping(0x2000); // HI ROM is offset by 0x2000
}

void CartridgeSuperGames::WriteIO2(uint16_t adr, uint8_t byte)
{
    if (!disableIO2)
    {
        bank = byte & 0x03;
        notEXROM = notGAME = byte & 0x04;
        disableIO2 = byte & 0x08;
        MapThyself();
    }
}


// C64 Games System cartridge (banked 8K ROM cartridge)
CartridgeC64GS::CartridgeC64GS() : ROMCartridge(64, 0x2000)
{
    notEXROM = false;
    MapThyself();
    strcpy(CartType, "C64GS");
}

void CartridgeC64GS::Reset()
{
    bank = 0;
    MapThyself();
}

void CartridgeC64GS::MapThyself(void)
{
    StandardMapping(64 * 0x2000); // HI ROM not used... map into area with 0xFFs
}

uint8_t CartridgeC64GS::ReadIO1(uint16_t adr, uint8_t bus_byte)
{
    bank = 0;
    MapThyself();
    return bus_byte;
}

void CartridgeC64GS::WriteIO1(uint16_t adr, uint8_t byte)
{
    bank = adr & 0x3f;
    MapThyself();
}


// Dinamic cartridge (banked 8K ROM cartridge)
CartridgeDinamic::CartridgeDinamic() : ROMCartridge(16, 0x2000)
{
    notEXROM = false;
    MapThyself();
    strcpy(CartType, "DINAMIC");
}

void CartridgeDinamic::Reset()
{
    bank = 0;
    MapThyself();
}

void CartridgeDinamic::MapThyself(void)
{
    StandardMapping(64 * 0x2000); // HI ROM not used... map into area with 0xFFs
}

uint8_t CartridgeDinamic::ReadIO1(uint16_t adr, uint8_t bus_byte)
{
    bank = adr & 0x0f;
    MapThyself();
    return bus_byte;
}


// Magic Desk / Marina64 cartridge (banked 8K ROM cartridge)
CartridgeMagicDesk::CartridgeMagicDesk() : ROMCartridge(128, 0x2000)
{
    bTrueDriveRequired = true; // Magic Desk won't load properly without the true drive infrastructure

    notEXROM = false;
    bank = 0;
    MapThyself();
    strcpy(CartType, "MAGICDESK");
}

void CartridgeMagicDesk::Reset()
{
    notEXROM = false;
    bank = 0;
    MapThyself();
}

void CartridgeMagicDesk::MapThyself(void)
{
    StandardMapping(64 * 0x2000); // HI ROM not used... map into area with 0xFFs
}

void CartridgeMagicDesk::WriteIO1(uint16_t adr, uint8_t byte)
{
    bank = byte & 0x7f;
    notEXROM = byte & 0x80;
    MapThyself();
}

// Magic Desk2 / Magic Desk 16K (mainly for SNK vs Capcom - Stronger Edition)
CartridgeMagicDesk2::CartridgeMagicDesk2() : ROMCartridge(128, 0x4000)
{
    bTrueDriveRequired = true; // Magic Desk won't load properly without the true drive infrastructure

    notEXROM = false;
    notGAME = false;
    bank = 0;
    MapThyself();
    strcpy(CartType, "MAGICDESK 16K");
}

void CartridgeMagicDesk2::Reset()
{
    notEXROM = false;
    notGAME = false;
    bank = 0;
    MapThyself();
}

void CartridgeMagicDesk2::MapThyself(void)
{
    StandardMapping(0x2000); // HI ROM offset by 8K (16K maps)
}

void CartridgeMagicDesk2::WriteIO1(uint16_t adr, uint8_t byte)
{
    bank = byte & 0x7f;
    notEXROM = byte & 0x80;
    MapThyself();
}

// Borrowed from VICE - many thanks!!
static const unsigned char eapiam29f040[768] = {
    0x65, 0x61, 0x70, 0x69, 0xc1, 0x4d,
    0x2f, 0xcd, 0x32, 0x39, 0xc6, 0x30, 0x34, 0x30,
    0x20, 0xd6, 0x31, 0x2e, 0x34, 0x00, 0x08, 0x78,
    0xa5, 0x4b, 0x48, 0xa5, 0x4c, 0x48, 0xa9, 0x60,
    0x85, 0x4b, 0x20, 0x4b, 0x00, 0xba, 0xbd, 0x00,
    0x01, 0x85, 0x4c, 0xca, 0xbd, 0x00, 0x01, 0x85,
    0x4b, 0x18, 0x90, 0x70, 0x4c, 0x67, 0x01, 0x4c,
    0xa4, 0x01, 0x4c, 0x39, 0x02, 0x4c, 0x40, 0x02,
    0x4c, 0x44, 0x02, 0x4c, 0x4e, 0x02, 0x4c, 0x58,
    0x02, 0x4c, 0x8e, 0x02, 0x4c, 0xd9, 0x02, 0x4c,
    0xd9, 0x02, 0x8d, 0x02, 0xde, 0xa9, 0xaa, 0x8d,
    0x55, 0x85, 0xa9, 0x55, 0x8d, 0xaa, 0x82, 0xa9,
    0xa0, 0x8d, 0x55, 0x85, 0xad, 0xf2, 0xdf, 0x8d,
    0x00, 0xde, 0xa9, 0x00, 0x8d, 0xff, 0xff, 0xa2,
    0x07, 0x8e, 0x02, 0xde, 0x60, 0x8d, 0x02, 0xde,
    0xa9, 0xaa, 0x8d, 0x55, 0xe5, 0xa9, 0x55, 0x8d,
    0xaa, 0xe2, 0xa9, 0xa0, 0x8d, 0x55, 0xe5, 0xd0,
    0xdb, 0xa2, 0x55, 0x8e, 0xe3, 0xdf, 0x8c, 0xe4,
    0xdf, 0xa2, 0x85, 0x8e, 0x02, 0xde, 0x8d, 0xff,
    0xff, 0x4c, 0xbb, 0xdf, 0xad, 0xff, 0xff, 0x60,
    0xcd, 0xff, 0xff, 0x60, 0xa2, 0x6f, 0xa0, 0x7f,
    0xb1, 0x4b, 0x9d, 0x80, 0xdf, 0xdd, 0x80, 0xdf,
    0xd0, 0x21, 0x88, 0xca, 0x10, 0xf2, 0xa2, 0x00,
    0xe8, 0x18, 0xbd, 0x80, 0xdf, 0x65, 0x4b, 0x9d,
    0x80, 0xdf, 0xe8, 0xbd, 0x80, 0xdf, 0x65, 0x4c,
    0x9d, 0x80, 0xdf, 0xe8, 0xe0, 0x1e, 0xd0, 0xe8,
    0x18, 0x90, 0x06, 0xa9, 0x01, 0x8d, 0xb9, 0xdf,
    0x38, 0x68, 0x85, 0x4c, 0x68, 0x85, 0x4b, 0xb0,
    0x48, 0xa9, 0xaa, 0xa0, 0xe5, 0x20, 0xd5, 0xdf,
    0xa0, 0x85, 0x20, 0xd5, 0xdf, 0xa9, 0x55, 0xa2,
    0xaa, 0xa0, 0xe2, 0x20, 0xd7, 0xdf, 0xa2, 0xaa,
    0xa0, 0x82, 0x20, 0xd7, 0xdf, 0xa9, 0x90, 0xa0,
    0xe5, 0x20, 0xd5, 0xdf, 0xa0, 0x85, 0x20, 0xd5,
    0xdf, 0xad, 0x00, 0xa0, 0x8d, 0xf1, 0xdf, 0xae,
    0x01, 0xa0, 0x8e, 0xb9, 0xdf, 0xc9, 0x01, 0xd0,
    0x06, 0xe0, 0xa4, 0xd0, 0x02, 0xf0, 0x0c, 0xc9,
    0x20, 0xd0, 0x39, 0xe0, 0xe2, 0xd0, 0x35, 0xf0,
    0x02, 0xb0, 0x50, 0xad, 0x00, 0x80, 0xae, 0x01,
    0x80, 0xc9, 0x01, 0xd0, 0x06, 0xe0, 0xa4, 0xd0,
    0x02, 0xf0, 0x08, 0xc9, 0x20, 0xd0, 0x19, 0xe0,
    0xe2, 0xd0, 0x15, 0xa0, 0x3f, 0x8c, 0x00, 0xde,
    0xae, 0x02, 0x80, 0xd0, 0x13, 0xae, 0x02, 0xa0,
    0xd0, 0x12, 0x88, 0x10, 0xf0, 0x18, 0x90, 0x12,
    0xa9, 0x02, 0xd0, 0x0a, 0xa9, 0x03, 0xd0, 0x06,
    0xa9, 0x04, 0xd0, 0x02, 0xa9, 0x05, 0x8d, 0xb9,
    0xdf, 0x38, 0xa9, 0x00, 0x8d, 0x00, 0xde, 0xa0,
    0xe0, 0xa9, 0xf0, 0x20, 0xd7, 0xdf, 0xa0, 0x80,
    0x20, 0xd7, 0xdf, 0xad, 0xb9, 0xdf, 0xb0, 0x08,
    0xae, 0xf1, 0xdf, 0xa0, 0x40, 0x28, 0x18, 0x60,
    0x28, 0x38, 0x60, 0x8d, 0xb7, 0xdf, 0x8e, 0xb9,
    0xdf, 0x8e, 0xed, 0xdf, 0x8c, 0xba, 0xdf, 0x08,
    0x78, 0x98, 0x29, 0xbf, 0x8d, 0xee, 0xdf, 0xa9,
    0x00, 0x8d, 0x00, 0xde, 0xa9, 0x85, 0xc0, 0xe0,
    0x90, 0x05, 0x20, 0xc1, 0xdf, 0xb0, 0x03, 0x20,
    0x9e, 0xdf, 0xa2, 0x14, 0x20, 0xec, 0xdf, 0xf0,
    0x06, 0xca, 0xd0, 0xf8, 0x18, 0x90, 0x63, 0xad,
    0xf2, 0xdf, 0x8d, 0x00, 0xde, 0x18, 0x90, 0x72,
    0x8d, 0xb7, 0xdf, 0x8e, 0xb9, 0xdf, 0x8c, 0xba,
    0xdf, 0x08, 0x78, 0x98, 0xc0, 0x80, 0xf0, 0x04,
    0xa0, 0xe0, 0xa9, 0xa0, 0x8d, 0xee, 0xdf, 0xc8,
    0xc8, 0xc8, 0xc8, 0xc8, 0xa9, 0xaa, 0x20, 0xd5,
    0xdf, 0xa9, 0x55, 0xa2, 0xaa, 0x88, 0x88, 0x88,
    0x20, 0xd7, 0xdf, 0xa9, 0x80, 0xc8, 0xc8, 0xc8,
    0x20, 0xd5, 0xdf, 0xa9, 0xaa, 0x20, 0xd5, 0xdf,
    0xa9, 0x55, 0xa2, 0xaa, 0x88, 0x88, 0x88, 0x20,
    0xd7, 0xdf, 0xad, 0xb7, 0xdf, 0x8d, 0x00, 0xde,
    0xa2, 0x00, 0x8e, 0xed, 0xdf, 0x88, 0x88, 0xa9,
    0x30, 0x20, 0xd7, 0xdf, 0xa9, 0xff, 0xaa, 0xa8,
    0xd0, 0x24, 0xad, 0xf2, 0xdf, 0x8d, 0x00, 0xde,
    0xa0, 0x80, 0xa9, 0xf0, 0x20, 0xd7, 0xdf, 0xa0,
    0xe0, 0xa9, 0xf0, 0x20, 0xd7, 0xdf, 0x28, 0x38,
    0xb0, 0x02, 0x28, 0x18, 0xac, 0xba, 0xdf, 0xae,
    0xb9, 0xdf, 0xad, 0xb7, 0xdf, 0x60, 0x20, 0xec,
    0xdf, 0xf0, 0x09, 0xca, 0xd0, 0xf8, 0x88, 0xd0,
    0xf5, 0x18, 0x90, 0xce, 0xad, 0xf2, 0xdf, 0x8d,
    0x00, 0xde, 0x18, 0x90, 0xdd, 0x8d, 0xf2, 0xdf,
    0x8d, 0x00, 0xde, 0x60, 0xad, 0xf2, 0xdf, 0x60,
    0x8d, 0xf3, 0xdf, 0x8e, 0xe9, 0xdf, 0x8c, 0xea,
    0xdf, 0x60, 0x8e, 0xf4, 0xdf, 0x8c, 0xf5, 0xdf,
    0x8d, 0xf6, 0xdf, 0x60, 0xad, 0xf2, 0xdf, 0x8d,
    0x00, 0xde, 0x20, 0xe8, 0xdf, 0x8d, 0xb7, 0xdf,
    0x8e, 0xf0, 0xdf, 0x8c, 0xf1, 0xdf, 0xa9, 0x00,
    0x8d, 0xba, 0xdf, 0xf0, 0x3b, 0xad, 0xf4, 0xdf,
    0xd0, 0x10, 0xad, 0xf5, 0xdf, 0xd0, 0x08, 0xad,
    0xf6, 0xdf, 0xf0, 0x0b, 0xce, 0xf6, 0xdf, 0xce,
    0xf5, 0xdf, 0xce, 0xf4, 0xdf, 0x90, 0x45, 0x38,
    0xb0, 0x42, 0x8d, 0xb7, 0xdf, 0x8e, 0xf0, 0xdf,
    0x8c, 0xf1, 0xdf, 0xae, 0xe9, 0xdf, 0xad, 0xea,
    0xdf, 0xc9, 0xa0, 0x90, 0x02, 0x09, 0x40, 0xa8,
    0xad, 0xb7, 0xdf, 0x20, 0x80, 0xdf, 0xb0, 0x24,
    0xee, 0xe9, 0xdf, 0xd0, 0x19, 0xee, 0xea, 0xdf,
    0xad, 0xf3, 0xdf, 0x29, 0xe0, 0xcd, 0xea, 0xdf,
    0xd0, 0x0c, 0xad, 0xf3, 0xdf, 0x0a, 0x0a, 0x0a,
    0x8d, 0xea, 0xdf, 0xee, 0xf2, 0xdf, 0x18, 0xad,
    0xba, 0xdf, 0xf0, 0xa1, 0xac, 0xf1, 0xdf, 0xae,
    0xf0, 0xdf, 0xad, 0xb7, 0xdf, 0x60, 0xff, 0xff,
    0xff, 0xff
};

// -------------------------------------------------------------------------------------------------------
// Easyflash cartridge (banked 16K ROM cartridge with 8K LOROM and 8K HIGH ROM and flash write supported)
// -------------------------------------------------------------------------------------------------------
CartridgeEasyFlash::CartridgeEasyFlash(bool not_game, bool not_exrom) : ROMCartridge(128, 0x2000)
{
    notEXROM = not_exrom;
    notGAME = not_game;
    MapThyself();
    strcpy(CartType, "EASYFLASH");
}

void CartridgeEasyFlash::PatchEAPI(void)
{
    // -------------------------------------------------------------------------------------------
    // If the EAPI driver is located at the correct offset in the HIROM chip, we patch over
    // it with the standard EasyFlash driver which is all we support for GimliDS. Most likely
    // the driver we are patching is also an EasyFlash driver - in which case, no harm, no foul.
    // -------------------------------------------------------------------------------------------
    if ((rom[0x81800] == 'e') && (rom[0x81801] == 'a') && (rom[0x81802] == 'p') && (rom[0x81803] == 'i'))
    {
        memcpy(rom+0x81800, eapiam29f040, sizeof(eapiam29f040));
    }
    
    // ------------------------------------------------------------------------------------------------
    // There might be an ".ezf" flash patch file sitting on the SD card... if so read it and apply it.
    // ------------------------------------------------------------------------------------------------
    if (myConfig.diskFlash & 0x02)
    {
        sprintf(tmpFilename,"sav/%s", CartFilename);
        tmpFilename[strlen(tmpFilename)-3] = 'e';
        tmpFilename[strlen(tmpFilename)-2] = 'z';
        tmpFilename[strlen(tmpFilename)-1] = 'f';
        
        FILE *fp = fopen(tmpFilename, "rb");
        if (fp)
        {
            fread(dirtySectors, sizeof(dirtySectors), 1, fp);
            for (int i=0; i<128; i++)
            {
                if (dirtySectors[i])
                {
                    fread(rom + (i * 0x2000), 0x2000, 1, fp);
                }
            }
            fclose(fp);
        }
    }
}

void CartridgeEasyFlash::Reset()
{
    flash_write_supported = 1;
    flash_state_lo = FLASH_IDLE;
    flash_state_hi = FLASH_IDLE;
    flash_base_state_lo = FLASH_IDLE;
    flash_base_state_hi = FLASH_IDLE;
    
    memset(dirtySectors, 0x00, sizeof(dirtySectors));
    bank = 0;
    MapThyself();
    PatchEAPI();
}

void CartridgeEasyFlash::MapThyself(void)
{
    StandardMapping(64 * 0x2000); // Easyflash is 2 ROMs of 512K each... so when we map, the HIROM bank is just offset by 512K
}


void CartridgeEasyFlash::WriteIO1(uint16_t adr, uint8_t byte)
{
    if ((adr&0xff) == 0x00)
    {
        bank = byte & 0x3f;
    }
    else if ((adr&0xff) == 0x02)
    {
        notEXROM = (byte & 2) ? false:true;
        notGAME  = (byte & 4) ? ((byte & 1) ? false:true) : false;
        if (byte & 0x80) {cart_led=2;cart_led_color=0;}
    }
    MapThyself();
}

void CartridgeEasyFlash::WriteIO2(uint16_t adr, uint8_t byte)
{
    ram[adr & 0xff] = byte;
}

uint8_t CartridgeEasyFlash::ReadIO1(uint16_t adr, uint8_t bus_byte)
{
    return bus_byte;
}

uint8_t CartridgeEasyFlash::ReadIO2(uint16_t adr, uint8_t bus_byte)
{
    return ram[adr & 0xff];
}

// --------------------------------------------------------------------------
// Easy Flash supports write to the flash. Erase happens on 64K sized blocks.
// --------------------------------------------------------------------------
void CartridgeEasyFlash::WriteFlash(uint16_t adr, uint8_t byte)
{
    // -----------------------------------------------------------------
    // Trap any writes that would be hitting our Cartridge ROM space...
    // -----------------------------------------------------------------
    if ((MemMap[adr>>12] >= (rom-0xe000)) && (MemMap[adr>>12] < (rom+(1024*1024))))
    {
        cart_led=2;
        cart_led_color=1;
        
        uint8 *flash_state      = ((adr & 0xE000) == 0x8000) ? &flash_state_lo : &flash_state_hi;
        uint8 *flash_base_state = ((adr & 0xE000) == 0x8000) ? &flash_base_state_lo : &flash_base_state_hi;
        uint32 flash_offset     = ((adr & 0xE000) == 0x8000) ? 0x00000000 : (64 * 0x2000);

        switch (*flash_state)
        {
            case FLASH_IDLE:
                if ((bank == 0) && ((adr & 0x7FF) == 0x555) && (byte == 0xAA)) // The first keyed sequence to wake up the flash controller
                {
                    *flash_state = FLASH_x555_AA;
                }
                else if (byte == 0xF0)
                {
                    *flash_state = FLASH_IDLE;
                    *flash_base_state = FLASH_IDLE;
                }
                break;
                
            case FLASH_CHIP_ID: // Chip ID (aka AutoSelect mode)
                if ((bank == 0) && ((adr & 0x7FF) == 0x555) && (byte == 0xAA)) // The first keyed sequence to wake up the flash controller
                {
                    *flash_state = FLASH_x555_AA;
                }
                else if (byte == 0xF0)
                {
                    *flash_state = FLASH_IDLE;
                    *flash_base_state = FLASH_IDLE;
                    // Revert back to true flash contents
                    for (int i=0; i<64; i++)
                    {
                        memcpy(rom + flash_offset + (i*0x2000), ((adr & 0xE000) == 0x8000) ? under_autoselect_lo[i] : under_autoselect_hi[i], 4);
                    }
                }
                break;
                
                
            case FLASH_x555_AA:
                if ((bank == 0) && ((adr & 0x7FF) == 0x2AA) && (byte == 0x55)) // The second keyed sequence to wake up the flash controller
                {
                    *flash_state = FLASH_x2AA_55;
                }
                else
                {
                    *flash_state = *flash_base_state;
                }
                break;
            
            // -----------------------------------------------------------    
            // Determine what kind of flash command is being requested...
            // -----------------------------------------------------------
            case FLASH_x2AA_55:
                if (((adr & 0x7FF) == 0x555) && (byte == 0x80)) 
                {
                    *flash_state = FLASH_x555_80;    // Sector Erase
                }
                else if (((adr & 0x7FF) == 0x555) && (byte == 0x90))
                {
                    *flash_base_state = FLASH_CHIP_ID;
                    *flash_state = FLASH_CHIP_ID;
                    
                    // ----------------------------------------------
                    // We are in 'AutoSelect' mode where the program
                    // is trying to read out the Chip ID / Mfr and
                    // possibly the state of the bank protection.
                    // ----------------------------------------------
                    for (int i=0; i<64; i++)
                    {
                        memcpy(((adr & 0xE000) == 0x8000) ? under_autoselect_lo[i] : under_autoselect_hi[i], rom + flash_offset + (i*(0x2000)), 4);
                        rom[flash_offset+(i*(0x2000))+0] = 0x01; // AMD
                        rom[flash_offset+(i*(0x2000))+1] = 0xA4; // AM29F040
                        rom[flash_offset+(i*(0x2000))+2] = 0x00; // Not Protected
                        rom[flash_offset+(i*(0x2000))+3] = 0x00; // Unused but padded just in case
                    }
                }
                else if (((adr & 0x7FF) == 0x555) && (byte == 0xA0))
                {
                    *flash_state = FLASH_x555_A0;    // Byte Write
                }
                else if (((adr & 0x7FF) == 0x555) && (byte == 0xF0))
                {
                    *flash_state = FLASH_IDLE;
                    *flash_base_state = FLASH_IDLE;
                }
                else
                {
                    *flash_state = *flash_base_state;
                }
                break;
                
            case FLASH_x555_80: // Sector Erase
                if (((adr & 0x7FF) == 0x555) && (byte == 0xAA))
                {
                    *flash_state = FLASH_x555_SE;
                }
                else
                {
                    *flash_state = *flash_base_state;
                }
                break;

            case FLASH_x555_SE: // Sector Erase - Key 1
                if (((adr & 0x7FF) == 0x2AA) && (byte == 0x55))
                {
                    *flash_state = FLASH_x2AA_SE;
                }
                else
                {
                    *flash_state = *flash_base_state;
                }
                break;
            
            case FLASH_x2AA_SE: // Sector Erase - Key 2
                if (byte == 0x30)
                {
                    // Erase the chip sector -- 64K bank is erased.
                    memset(rom + flash_offset + ((bank/8) * (64 * 1024)), 0xFF, (64 * 1024));
                    for (int i=(bank/8)*8; i<((bank/8)*8)+8; i++)
                    {
                        dirtySectors[i + (flash_offset ? 64:0)] = true;
                    }
                    dirtyFlash = 10;
                }
                else if (byte == 0x10)
                {
                    // Erase the WHOLE chip!!!
                    memset(rom + flash_offset, 0xFF, (512 * 1024));
                    for (int i=0; i<64; i++)
                    {
                        dirtySectors[i + (flash_offset ? 64:0)] = true;
                    }
                    dirtyFlash = 10;
                }
                *flash_state = FLASH_IDLE;
                *flash_base_state = FLASH_IDLE;
                break;
            
            case FLASH_x555_A0: // Byte Write
                // -------------------------------------------------------------------------
                // Write the byte into the rom[] memory in the appropriate banking location
                // This is a flash write so we AND with the current contents (that is... we
                // can turn a '1' into a '0' but not the other way around).
                // -------------------------------------------------------------------------
                rom[flash_offset + (bank * 0x2000) + (adr & 0x1FFF)] &= byte;
                dirtySectors[bank + (flash_offset ? 64:0)] = true;
                dirtyFlash = 10;
                *flash_state = *flash_base_state;
                break;
        }
        
        // -------------------------------------------------------
        // In Ultimax mode, the cartridge takes over the bus and 
        // does not allow the normal C64 write-through to RAM.
        // -------------------------------------------------------
        if (ultimax_mode) return; 
    }

    // --------------------------------------------------------
    // Non ultimax modes allows write-thru to the RAM below...
    // --------------------------------------------------------
    myRAM[adr] = byte;
}

// -------------------------------------------------------------------------
// We don't write back to the original .CRT file as we want to keep that
// pristine so we instead write any modified 8K sectors into a special .ezf
// patch file and when we reload the cartridge, we simply apply the patch.
// -------------------------------------------------------------------------
void CartridgeEasyFlash::PersistFlash(void)
{
    if (myConfig.diskFlash & 0x02)
    {
        check_and_make_sav_directory();
        sprintf(tmpFilename,"sav/%s", CartFilename);
        tmpFilename[strlen(tmpFilename)-3] = 'e';
        tmpFilename[strlen(tmpFilename)-2] = 'z';
        tmpFilename[strlen(tmpFilename)-1] = 'f';
        
        FILE *fp = fopen(tmpFilename, "wb");
        if (fp)
        {
            fwrite(dirtySectors, sizeof(dirtySectors), 1, fp);
            for (int i=0; i<128; i++)
            {
                if (dirtySectors[i])
                {
                    fwrite(rom + (i * 0x2000), 0x2000, 1, fp);
                }
            }
            fclose(fp);
        }
    }
}



#define EEPROM_DATAOUT      0x80
#define EEPROM_SELECT       0x40
#define EEPROM_CLOCK        0x20
#define EEPROM_DATAIN       0x10


#define EEPROM_STATE_IDLE                   0
#define EEPROM_STATE_START                  1
#define EEPROM_STATE_CLOCK_OP               2
#define EEPROM_STATE_CLOCK_ADDR             3
#define EEPROM_STATE_READ_DATA              4
#define EEPROM_STATE_WRITE_DATA             5
#define EEPROM_STATE_READ_DATA_DUMMY_ZERO   6

// GMOD2 cartridge (banked 8K ROM cartridge)
CartridgeGMOD2::CartridgeGMOD2(bool not_game, bool not_exrom) : ROMCartridge(64, 0x2000)
{
    notEXROM = not_exrom;
    notGAME = not_game;
    MapThyself();
    strcpy(CartType, "GMOD2");
}

void CartridgeGMOD2::Reset()
{
    bank = 0;
    eeprom_state = EEPROM_STATE_IDLE;
    eeprom_opcode = 0x00;
    eeprom_address = 0x000;
    eeprom_clock = 0;
    eeprom_data_in = 0x0000;
    eeprom_data_out = 0x0000;
    memset(eeprom_data, 0xff, sizeof(eeprom_data));
    
    sprintf(tmpFilename,"sav/%s", CartFilename);
    tmpFilename[strlen(tmpFilename)-3] = 'e';
    tmpFilename[strlen(tmpFilename)-2] = 'e';
    tmpFilename[strlen(tmpFilename)-1] = 'p';
   
    FILE *fp = fopen(tmpFilename, "rb");
    if (fp)
    {
        fread(eeprom_data, sizeof(eeprom_data), 1, fp);
        fclose(fp);
    }    
        
    MapThyself();
}

void CartridgeGMOD2::MapThyself(void)
{
    StandardMapping(64 * 0x2000); // GMOD2 is 512K each with only a LOROM (no HIROM bank) - map HIROM into area with all 0xFF
}

// -------------------------------------------------------------------------------
// GMOD2 has a 2K byte (16K-bit) Serial EEPROM (M93C86 from STMicroelectronics). 
// This is wired in x16 mode so the addressing and data are all 16-bits wide.
// -------------------------------------------------------------------------------
void CartridgeGMOD2::WriteIO1(uint16_t adr, uint8_t byte)
{
    if (byte & EEPROM_SELECT) // EEPROM Selected?
    {
        if ((byte & EEPROM_CLOCK) != eeprom_clock)
        {
            eeprom_clock = byte & EEPROM_CLOCK;
            u8 data  = (byte & EEPROM_DATAIN) ? 1:0;
            if (eeprom_clock) // Clock has gone HI
            {
                switch (eeprom_state)
                {
                    case EEPROM_STATE_IDLE:
                        if (data) 
                        {
                            eeprom_bit_count = 2;
                            eeprom_opcode = 0x00;
                            eeprom_state = EEPROM_STATE_CLOCK_OP;
                        }
                        break;
                    case EEPROM_STATE_CLOCK_OP:
                        if (eeprom_bit_count) eeprom_bit_count--;
                        eeprom_opcode |= (data << eeprom_bit_count);
                        
                        if (!eeprom_bit_count)
                        {
                            eeprom_address = 0;
                            eeprom_bit_count = 10;
                            eeprom_state = EEPROM_STATE_CLOCK_ADDR;
                        }
                        break;
                        
                    case EEPROM_STATE_CLOCK_ADDR:
                        if (eeprom_bit_count) eeprom_bit_count--;
                        eeprom_address |= (data << eeprom_bit_count);
                        if (!eeprom_bit_count)
                        {
                            bWriteAll = false;
                            if (eeprom_opcode == 1) // Write Data
                            {
                                eeprom_data_in = 0x0000;
                                eeprom_bit_count = 16;
                                eeprom_state = EEPROM_STATE_WRITE_DATA;
                            }
                            if (eeprom_opcode == 2) // Read Data
                            {
                                eeprom_bit_count = 16;
                                eeprom_data_out = (eeprom_data[(eeprom_address<<1)+0] << 8) | eeprom_data[(eeprom_address<<1)+1];
                                eeprom_bit_out = 0;
                                eeprom_state = EEPROM_STATE_READ_DATA;
                            }
                            if (eeprom_opcode == 3) // Erase Word
                            {
                                eeprom_data[(eeprom_address<<1)+1] = 0xFF;
                                eeprom_data[(eeprom_address<<1)+0] = 0xFF;
                                dirtyFlash = 10;
                                eeprom_state = EEPROM_STATE_IDLE;
                            }
                            if (eeprom_opcode == 0) // Special - needs top 2 bits of address to define the operation
                            {
                                if ((eeprom_address >> 8) == 0x00)
                                {
                                    // Write Disable - ignored for the purposes of emulation (we assume well-behaved carts)
                                }
                                if ((eeprom_address >> 8) == 0x01)
                                {
                                    // Write All Memory with Same Value
                                    bWriteAll = true;
                                    eeprom_data_in = 0x0000;
                                    eeprom_bit_count = 16;
                                    eeprom_state = EEPROM_STATE_WRITE_DATA;
                                }
                                if ((eeprom_address >> 8) == 0x02)
                                {
                                    // Erase All Memory
                                    memset(eeprom_data, 0xff, sizeof(eeprom_data));
                                    dirtyFlash = 10;
                                }
                                if ((eeprom_address >> 8) == 0x03)
                                {
                                    // Write Enable - ignored for the purposes of emulation (we assume well-behaved carts)
                                }
                                
                                eeprom_state = EEPROM_STATE_IDLE;
                            }
                        }
                        break;
                    
                    case EEPROM_STATE_READ_DATA_DUMMY_ZERO:
                        eeprom_bit_out = 0;
                        eeprom_state = EEPROM_STATE_READ_DATA;
                        break;

                    case EEPROM_STATE_READ_DATA:
                        if (eeprom_bit_count) eeprom_bit_count--;                            
                        eeprom_bit_out = (eeprom_data_out & (1<<eeprom_bit_count));
                        if (!eeprom_bit_count)
                        {
                            eeprom_bit_count = 16;
                            eeprom_address = (eeprom_address + 1) & 0x3FF;
                            eeprom_data_out = (eeprom_data[(eeprom_address<<1)+0] << 8) | eeprom_data[(eeprom_address<<1)+1];
                        }
                        break;
                        
                    case EEPROM_STATE_WRITE_DATA:
                        if (eeprom_bit_count) eeprom_bit_count--;
                        eeprom_data_in |= (data << eeprom_bit_count);
                        if (!eeprom_bit_count)
                        {
                            dirtyFlash = 10;
                         
                            if (bWriteAll)
                            {
                                for (int address = 0; address < 0x400; address++)
                                {
                                    eeprom_data[(address<<1)+1] = (eeprom_data_in>>0) & 0xFF;
                                    eeprom_data[(address<<1)+0] = (eeprom_data_in>>8) & 0xFF;
                                }
                                bWriteAll = false;
                            }
                            else
                            {
                                eeprom_data[(eeprom_address<<1)+1] = (eeprom_data_in>>0) & 0xFF;
                                eeprom_data[(eeprom_address<<1)+0] = (eeprom_data_in>>8) & 0xFF;
                            }
                            eeprom_state = EEPROM_STATE_IDLE;
                        }
                        break;
                }
            }
        }
    }
    else // Must be EXROM - handle bank switch
    {
        eeprom_clock = 0;
        eeprom_state = EEPROM_STATE_IDLE;
        eeprom_bit_out = 1;
        eeprom_bit_count = 0;
        
        bank = byte & 0x3f;
        MapThyself();
    }        
}

uint8_t CartridgeGMOD2::ReadIO1(uint16_t adr, uint8_t bus_byte)
{
    bus_byte &= ~EEPROM_DATAOUT;
    if (eeprom_bit_out) bus_byte |= EEPROM_DATAOUT;
    
    return bus_byte;
}

void CartridgeGMOD2::PersistFlash(void)
{
    sprintf(tmpFilename,"sav/%s", CartFilename);
    tmpFilename[strlen(tmpFilename)-3] = 'e';
    tmpFilename[strlen(tmpFilename)-2] = 'e';
    tmpFilename[strlen(tmpFilename)-1] = 'p';
    FILE *fp = fopen(tmpFilename, "wb");
    if (fp)
    {
        fwrite(eeprom_data, sizeof(eeprom_data), 1, fp);
        fclose(fp);
    }
}


/*
 *  Functions to save and restore the Cartridge State
 */

void Cartridge::GetState(CartridgeState *cs)
{
    cs->notEXROM = notEXROM;
    cs->notGAME = notGAME;
    cs->bank = bank;
    cs->dirtyFlash = dirtyFlash;
    cs->bTrueDriveRequired = bTrueDriveRequired;
    cs->ultimax_mode = ultimax_mode;    
    cs->flash_state_lo = flash_state_lo;
    cs->flash_state_hi = flash_state_hi;
    cs->flash_base_state_lo = flash_base_state_lo;
    cs->flash_base_state_hi = flash_base_state_hi;
    cs->spare1 = 0;
    cs->spare2 = 0;
    cs->spare3 = 0;
    cs->spare4 = 0;
    cs->spare32 = 0x00000000;
    memcpy(cs->ram, ram, 256);
}

void Cartridge::SetState(CartridgeState *cs)
{
    notEXROM = cs->notEXROM;
    notGAME = cs->notGAME;
    bank = cs->bank;
    dirtyFlash = cs->dirtyFlash;
    bTrueDriveRequired = cs->bTrueDriveRequired;
    ultimax_mode = cs->ultimax_mode;    
    flash_state_lo = cs->flash_state_lo;
    flash_state_hi = cs->flash_state_hi;
    flash_base_state_lo = cs->flash_base_state_lo;
    flash_base_state_hi = cs->flash_base_state_hi;    
    memcpy(ram, cs->ram, 256);
}

/*
 *  Construct cartridge object from image file path, return nullptr on error
 */

Cartridge * Cartridge::FromFile(char *filename, char *errBuffer)
{
    uint16_t type = 0;

    ROMCartridge * cart = nullptr;
    FILE * f = nullptr;
    {
        // Read file header
        f = fopen(filename, "rb");
        if (f == nullptr) {
            return nullptr;
        }

        uint8_t header[64];
        if (fread(header, sizeof(header), 1, f) != 1)
            goto error_read;

        // Check for signature and version
        uint16_t version = (header[0x14] << 8) | header[0x15];
        if (memcmp(header, "C64 CARTRIDGE   ", 16) != 0 || version != 0x0100)
            goto error_unsupp;

        // Create cartridge object according to type
        type = (header[0x16] << 8) | header[0x17];
        uint8_t exrom = header[0x18];
        uint8_t game = header[0x19];

        switch (type) {
            case 0:
                if (exrom != 0)     // Ultimax or not a ROM cartridge
                    goto error_unsupp;
                if (game == 0)
                {
                    cart = new Cartridge16K;
                }
                else
                {
                    cart = new Cartridge8K;
                }
                break;
            case 5:
                cart = new CartridgeOcean(game);
                break;
            case 8:
                cart = new CartridgeSuperGames;
                break;
            case 15:
                cart = new CartridgeC64GS;
                break;
            case 17:
                cart = new CartridgeDinamic;
                break;
            case 19:
                cart = new CartridgeMagicDesk;
                break;
            case 32:
                cart = new CartridgeEasyFlash(game,exrom);
                break;
            case 60:
                cart = new CartridgeGMOD2(game,exrom);
                break;
            case 85:
                if (isDSiMode()) cart = new CartridgeMagicDesk2;
                else goto error_unsupp;
                break;
            default:
                goto error_unsupp;
        }

        cart->total_cart_size = 0;

        // Load CHIP packets
        while (true)
        {
            // Load packet header
            size_t actual = fread(header, 1, 16, f);
            if (actual == 0)
                break;
            if (actual != 16)
                goto error_read;

            // Check for signature and size
            uint16_t chip_type  = (header[0x08] << 8) | header[0x09];
            uint16_t chip_bank  = (header[0x0a] << 8) | header[0x0b];
            uint16_t chip_start = (header[0x0c] << 8) | header[0x0d];
            uint16_t chip_size  = (header[0x0e] << 8) | header[0x0f];

            cart->total_cart_size += chip_size;
            if (chip_bank > cart->last_bank) cart->last_bank = chip_bank;

            if (memcmp(header, "CHIP", 4) != 0 || chip_type == 1  || chip_bank >= cart->numBanks || chip_size > cart->bankSize)
                goto error_unsupp; // Chip Type of 1 is RAM - not yet supported... 0 is ROM and 2 is FLASH ROM

            // Load packet contents
            uint32_t offset = chip_bank * cart->bankSize;

            if (type == 32 && chip_start == 0xa000)   // Special mapping for EasyFlash - move upper bank
            {
                offset += (64 * 0x2000);
            }

            if (fread(cart->ROM() + offset, chip_size, 1, f) != 1)
                goto error_read;
        }

        fclose(f);
    }

    return cart;

error_read:
    delete cart;
    fclose(f);
    siprintf(errBuffer, "    UNABLE TO READ CARTRIDGE   ");
    return nullptr;

error_unsupp:
    delete cart;
    fclose(f);
    siprintf(errBuffer, "    UNKNOWN CART TYPE: %02d    ", type);
    return nullptr;
}
