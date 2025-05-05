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

#include <filesystem>
namespace fs = std::filesystem;

extern uint8 *MemMap[0x10];
extern u8 myRAM[];
extern u8 myBASIC[];
extern u8 myKERNAL[];

u8 cartROM[1024*1024]; // 1MB max supported cart size (not including .crt and chip headers)
extern C64 *gTheC64;


// Base class for cartridge with ROM
ROMCartridge::ROMCartridge(unsigned num_banks, unsigned bank_size) : numBanks(num_banks), bankSize(bank_size)
{
    // Allocate ROM
    rom = cartROM;
    memset(rom, 0xff, num_banks * bank_size);
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
//   1       0      1       1       RAM           -            CARTLO       -              -             CHAR/IO         CARTHI
//   1       0      1       0       RAM           -            CARTLO       -              -             CHAR/IO         CARTHI
//   1       0      0       1       RAM           -            CARTLO       -              -             CHAR/IO         CARTHI
//   1       0      0       0       RAM           -            CARTLO       -              -             CHAR/IO         CARTHI
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

// 8K ROM cartridge (EXROM = 0, GAME = 1)
Cartridge8K::Cartridge8K() : ROMCartridge(1, 0x2000)
{
    notEXROM = false;
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
    notEXROM = true; // Disable ROM
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
    bTrueDriveRequired = true;

    notEXROM = false;
    bank = 0;
    MapThyself();
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


// Easyflash cartridge (banked 8K ROM cartridge)
CartridgeEasyFlash::CartridgeEasyFlash(bool not_game, bool not_exrom) : ROMCartridge(128, 0x2000)
{
    notEXROM = not_exrom;
    notGAME = not_game;
    MapThyself();
}

void CartridgeEasyFlash::Reset()
{
    bank = 0;
    MapThyself();
}

void CartridgeEasyFlash::MapThyself(void)
{
    StandardMapping(64 * 0x2000);
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


/*
 *  Functions to save and restore the Cartridge State
 */

void Cartridge::GetState(CartridgeState *cs)
{
    cs->notEXROM = notEXROM;
    cs->notGAME = notGAME;
    cs->bank = bank;
    memcpy(cs->ram, ram, 256);
}

void Cartridge::SetState(CartridgeState *cs)
{
    notEXROM = cs->notEXROM;
    notGAME = cs->notGAME;
    bank = cs->bank;
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

        debug[3] = type;
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
                offset += (64*0x2000);
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
