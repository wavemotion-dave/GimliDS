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
 *  IEC.cpp - IEC bus routines, 1541 emulation (DOS level)
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
 *  - There are three kinds of devices on the IEC bus: controllers,
 *    listeners and talkers. We are always the controller and we
 *    can additionally be either listener or talker. There can be
 *    only one listener and one talker active at the same time (the
 *    real IEC bus allows multiple listeners, but we don't).
 *  - There is one Drive object for every emulated drive (8..11).
 *    A pointer to one of them is stored in "listener"/"talker"
 *    when talk()/listen() is called and is used by the functions
 *    called afterwards.
 *  - The Drive objects have four virtual functions so that the
 *    interface to them is independent of their implementation:
 *      Open() opens a channel
 *      Close() closes a channel
 *      Read() reads from a channel
 *      Write() writes to a channel
 *  - The EOI/EOF signal is special on the IEC bus in that it is
 *    Sent before the last byte, not after it.
 */

#include "sysdeps.h"
#include "IEC.h"
#include "1541d64.h"
#include "Prefs.h"
#include "Display.h"
#include "main.h"

// IEC command codes
enum {
    CMD_DATA = 0x60,    // Data transfer
    CMD_CLOSE = 0xe0,   // Close channel
    CMD_OPEN = 0xf0     // Open channel
};

// IEC ATN codes
enum {
    ATN_LISTEN = 0x20,
    ATN_UNLISTEN = 0x30,
    ATN_TALK = 0x40,
    ATN_UNTALK = 0x50
};


/*
 *  Constructor: Initialize variables
 */

Drive *IEC::create_drive(const char *path)
{
    if (!IsDirectory(path))
    {
        // Not a directory, check for mountable file type
        int type;
        if (IsMountableFile(path, type)) 
        {
            if (type == FILE_IMAGE) 
            {
                // Mount disk image
                return new ImageDrive(this, path);
            }
        }
    }
    return NULL;
}

IEC::IEC(C64Display *display) : the_display(display)
{
    int i;

    // Create drives 8..9
    for (i=0; i<2; i++)
        drive[i] = NULL;    // Important because UpdateLEDs is called from the drive constructors (via set_error)

    if (!ThePrefs.TrueDrive) {
        for (i=0; i<2; i++)
            drive[i] = create_drive(ThePrefs.DrivePath[i]);
    }

    listener_active = talker_active = false;
    listening = false;
}


/*
 *  Destructor: Delete drives
 */

IEC::~IEC()
{
    for (int i=0; i<2; i++)
        delete drive[i];
}


/*
 *  Reset all drives
 */

void IEC::Reset(void)
{
    for (int i=0; i<2; i++)
        if (drive[i] != NULL && drive[i]->Ready)
            drive[i]->Reset();

    UpdateLEDs();
}


/*
 *  Preferences have changed, prefs points to new preferences,
 *  ThePrefs still holds the previous ones. Check if drive settings
 *  have changed.
 */

void IEC::NewPrefs(Prefs *prefs)
{
    // Delete and recreate all changed drives
    for (int i=0; i<2; i++)
    {
        if (strcmp(ThePrefs.DrivePath[i], prefs->DrivePath[i]) || ThePrefs.TrueDrive != prefs->TrueDrive)
        {
            delete drive[i];
            drive[i] = NULL;    // Important because UpdateLEDs is called from drive constructors (via set_error())
            if (!prefs->TrueDrive)
            {
                drive[i] = create_drive(prefs->DrivePath[i]);
            }
        }
    }

    UpdateLEDs();
}


/*
 *  Update drive LED display
 */

void IEC::UpdateLEDs(void)
{
    u8 l0=0; u8 l1=0;

    if (drive[0] != NULL) l0 = drive[0]->LED;
    if (drive[1] != NULL) l1 = drive[1]->LED;

    the_display->UpdateLEDs(l0, l1);
}


/*
 *  Output one byte
 */

uint8 IEC::Out(uint8 byte, bool eoi)
{
    if (listener_active) {
        if (received_cmd == CMD_OPEN)
            return open_out(byte, eoi);
        if (received_cmd == CMD_DATA)
            return data_out(byte, eoi);
        return ST_TIMEOUT;
    } else
        return ST_TIMEOUT;
}


/*
 *  Output one byte with ATN (Talk/Listen/Untalk/Unlisten)
 */

uint8 IEC::OutATN(uint8 byte)
{
    received_cmd = sec_addr = 0;    // Command is sent with secondary address
    switch (byte & 0xf0) {
        case ATN_LISTEN:
            listening = true;
            return listen(byte & 0x0f);
        case ATN_UNLISTEN:
            listening = false;
            return unlisten();
        case ATN_TALK:
            listening = false;
            return talk(byte & 0x0f);
        case ATN_UNTALK:
            listening = false;
            return untalk();
    }
    return ST_TIMEOUT;
}


/*
 *  Output secondary address
 */

uint8 IEC::OutSec(uint8 byte)
{
    if (listening) {
        if (listener_active) {
            sec_addr = byte & 0x0f;
            received_cmd = byte & 0xf0;
            return sec_listen();
        }
    } else {
        if (talker_active) {
            sec_addr = byte & 0x0f;
            received_cmd = byte & 0xf0;
            return sec_talk();
        }
    }
    return ST_TIMEOUT;
}


/*
 *  Read one byte
 */

uint8 IEC::In(uint8 &byte)
{
    if (talker_active && (received_cmd == CMD_DATA))
        return data_in(byte);

    byte = 0;
    return ST_TIMEOUT;
}


/*
 *  Assert ATN (for Untalk)
 */

void IEC::SetATN(void)
{
    // Only needed for real IEC
}


/*
 *  Release ATN
 */

void IEC::RelATN(void)
{
    // Only needed for real IEC
}


/*
 *  Talk-attention turn-around
 */

void IEC::Turnaround(void)
{
    // Only needed for real IEC
}


/*
 *  System line release
 */

void IEC::Release(void)
{
    // Only needed for real IEC
}


/*
 *  Listen
 */

uint8 IEC::listen(int device)
{
    //printdbg("IEC::listen");
    if ((device >= 8) && (device <= 11)) {
        //printdbg("IEC::listen2");
        if ((listener = drive[device-8]) != NULL && listener->Ready) {
            //printdbg("IEC::listen3");
            listener_active = true;
            return ST_OK;
        }
    }

    listener_active = false;
    return ST_NOTPRESENT;
}


/*
 *  Talk
 */

uint8 IEC::talk(int device)
{
    if ((device >= 8) && (device <= 11)) {
        if ((talker = drive[device-8]) != NULL && talker->Ready) {
            talker_active = true;
            return ST_OK;
        }
    }

    talker_active = false;
    return ST_NOTPRESENT;
}


/*
 *  Unlisten
 */

uint8 IEC::unlisten(void)
{
    listener_active = false;
    return ST_OK;
}


/*
 *  Untalk
 */

uint8 IEC::untalk(void)
{
    talker_active = false;
    return ST_OK;
}


/*
 *  Secondary address after Listen
 */

uint8 IEC::sec_listen(void)
{
    switch (received_cmd) {

        case CMD_OPEN:  // Prepare for receiving the file name
            name_ptr = name_buf;
            name_len = 0;
            return ST_OK;

        case CMD_CLOSE: // Close channel
            if (listener->LED != DRVLED_ERROR) {
                listener->LED = DRVLED_OFF;     // Turn off drive LED
                UpdateLEDs();
            }
            return listener->Close(sec_addr);
    }
    return ST_OK;
}


/*
 *  Secondary address after Talk
 */

uint8 IEC::sec_talk(void)
{
    return ST_OK;
}


/*
 *  Byte after Open command: Store character in file name, open file on EOI
 */

uint8 IEC::open_out(uint8 byte, bool eoi)
{
    if (name_len < NAMEBUF_LENGTH) {
        *name_ptr++ = byte;
        name_len++;
    }

    if (eoi) {
        *name_ptr = 0;              // End string
        listener->LED = DRVLED_ON;  // Turn on drive LED
        UpdateLEDs();
        return listener->Open(sec_addr, name_buf, name_len);
    }

    return ST_OK;
}


/*
 *  Write byte to channel
 */

uint8 IEC::data_out(uint8 byte, bool eoi)
{
    return listener->Write(sec_addr, byte, eoi);
}


/*
 *  Read byte from channel
 */

uint8 IEC::data_in(uint8 &byte)
{
    return talker->Read(sec_addr, byte);
}


/*
 *  Drive constructor
 */

Drive::Drive(IEC *iec)
{
    the_iec = iec;
    LED = DRVLED_OFF;
    Ready = false;
    set_error(ERR_STARTUP);
}


/*
 *  Set error message on drive
 */

// 1541 error messages
static const char *Errors_1541[] = {
    "00, OK,%02d,%02d\x0d",
    "01, FILES SCRATCHED,%02d,%02d\x0d",
    "03, UNIMPLEMENTED,%02d,%02d\x0d",
    "20, READ ERROR,%02d,%02d\x0d",
    "21, READ ERROR,%02d,%02d\x0d",
    "22, READ ERROR,%02d,%02d\x0d",
    "23, READ ERROR,%02d,%02d\x0d",
    "24, READ ERROR,%02d,%02d\x0d",
    "25, WRITE ERROR,%02d,%02d\x0d",
    "26, WRITE PROTECT ON,%02d,%02d\x0d",
    "27, READ ERROR,%02d,%02d\x0d",
    "28, WRITE ERROR,%02d,%02d\x0d",
    "29, DISK ID MISMATCH,%02d,%02d\x0d",
    "30, SYNTAX ERROR,%02d,%02d\x0d",
    "31, SYNTAX ERROR,%02d,%02d\x0d",
    "32, SYNTAX ERROR,%02d,%02d\x0d",
    "33, SYNTAX ERROR,%02d,%02d\x0d",
    "34, SYNTAX ERROR,%02d,%02d\x0d",
    "60, WRITE FILE OPEN,%02d,%02d\x0d",
    "61, FILE NOT OPEN,%02d,%02d\x0d",
    "62, FILE NOT FOUND,%02d,%02d\x0d",
    "63, FILE EXISTS,%02d,%02d\x0d",
    "64, FILE TYPE MISMATCH,%02d,%02d\x0d",
    "65, NO BLOCK,%02d,%02d\x0d",
    "66, ILLEGAL TRACK OR SECTOR,%02d,%02d\x0d",
    "70, NO CHANNEL,%02d,%02d\x0d",
    "71, DIR ERROR,%02d,%02d\x0d",
    "72, DISK FULL,%02d,%02d\x0d",
    "73, CBM DOS V2.6 1541,%02d,%02d\x0d",
    "74, DRIVE NOT READY,%02d,%02d\x0d"
};

void Drive::set_error(int error, int track, int sector)
{
    // Write error message to buffer
    sprintf(error_buf, Errors_1541[error], track, sector);
    error_ptr = error_buf;
    error_len = strlen(error_buf);
    current_error = error;
    //DSPrint(0,1,6,error_buf);

    // Set drive condition
    if (error != ERR_OK && error != ERR_SCRATCHED)
        if (error == ERR_STARTUP)
            LED = DRVLED_OFF;
        else
            LED = DRVLED_ERROR;
    else if (LED == DRVLED_ERROR)
        LED = DRVLED_OFF;
    the_iec->UpdateLEDs();
}


/*
 *  Parse file name, determine access mode and file type
 */

void Drive::parse_file_name(const uint8 *src, int src_len, uint8 *dest, int &dest_len, int &mode, int &type, int &rec_len, bool convert_charset)
{

    // If the string contains a ':', the file name starts after that
    const uint8 *p = (const uint8 *)memchr(src, ':', src_len);
    if (p) {
        p++;
        src_len -= p - src;
    } else
        p = src;

    // Transfer file name upto ','
    dest_len = 0;
    uint8 *q = dest;
    while (*p != ',' && src_len-- > 0) {
        if (convert_charset)
            *q++ = petscii2ascii(*p++);
        else
            *q++ = *p++;
        dest_len++;
    }
    *q++ = 0;

    // Strip trailing CRs
    while (dest_len > 0 && dest[dest_len - 1] == 0x0d)
        dest[--dest_len] = 0;

    // Look for mode and type parameters separated by ','
    p++; src_len--;
    while (src_len > 0) {
        switch (*p) {
            case 'D':
                type = FTYPE_DEL;
                break;
            case 'S':
                type = FTYPE_SEQ;
                break;
            case 'P':
                type = FTYPE_PRG;
                break;
            case 'U':
                type = FTYPE_USR;
                break;
            case 'L':
                type = FTYPE_REL;
                while (*p != ',' && src_len-- > 0) p++;
                p++; src_len--;
                rec_len = *p++; src_len--;
                if (src_len < 0)
                    rec_len = 0;
                break;
            case 'R':
                mode = FMODE_READ;
                break;
            case 'W':
                mode = FMODE_WRITE;
                break;
            case 'A':
                mode = FMODE_APPEND;
                break;
            case 'M':
                mode = FMODE_M;
                break;
        }

        // Skip to ','
        while (*p != ',' && src_len-- > 0) p++;
        p++; src_len--;
    }
}


/*
 *  Execute DOS command (parse command and call appropriate routine)
 */

static void parse_block_cmd_args(const uint8 *p, int &arg1, int &arg2, int &arg3, int &arg4)
{
    arg1 = arg2 = arg3 = arg4 = 0;

    while (*p == ' ' || *p == 0x1d || *p == ',') p++;
    while (*p >= '0' && *p < '@')
        arg1 = arg1 * 10 + (*p++ & 0x0f);

    while (*p == ' ' || *p == 0x1d || *p == ',') p++;
    while (*p >= '0' && *p < '@')
        arg2 = arg2 * 10 + (*p++ & 0x0f);

    while (*p == ' ' || *p == 0x1d || *p == ',') p++;
    while (*p >= '0' && *p < '@')
        arg3 = arg3 * 10 + (*p++ & 0x0f);

    while (*p == ' ' || *p == 0x1d || *p == ',') p++;
    while (*p >= '0' && *p < '@')
        arg4 = arg4 * 10 + (*p++ & 0x0f);
}

void Drive::execute_cmd(const uint8 *cmd, int cmd_len)
{
    // Strip trailing CRs
    while (cmd_len > 0 && cmd[cmd_len - 1] == 0x0d)
        cmd_len--;

    // Find token delimiters
    const uint8 *colon = (const uint8 *)memchr(cmd, ':', cmd_len);
    const uint8 *equal = colon ? (const uint8 *)memchr(colon, '=', cmd_len - (colon - cmd)) : NULL;
    const uint8 *comma = (const uint8 *)memchr(cmd, ',', cmd_len);
    const uint8 *minus = (const uint8 *)memchr(cmd, '-', cmd_len);

    // Parse command name
    set_error(ERR_OK);
    switch (cmd[0]) {
        case 'B':   // Block/buffer
            if (!minus)
                set_error(ERR_SYNTAX31);
            else {
                // Parse arguments (up to 4 decimal numbers separated by
                // space, cursor right or comma)
                const uint8 *p = colon ? colon + 1 : cmd + 3;
                int arg1, arg2, arg3, arg4;
                parse_block_cmd_args(p, arg1, arg2, arg3, arg4);

                // Switch on command
                switch (minus[1]) {
                    case 'R':
                        block_read_cmd(arg1, arg3, arg4);
                        break;
                    case 'W':
                        block_write_cmd(arg1, arg3, arg4);
                        break;
                    case 'E':
                        block_execute_cmd(arg1, arg3, arg4);
                        break;
                    case 'A':
                        block_allocate_cmd(arg2, arg3);
                        break;
                    case 'F':
                        block_free_cmd(arg2, arg3);
                        break;
                    case 'P':
                        buffer_pointer_cmd(arg1, arg2);
                        break;
                    default:
                        set_error(ERR_SYNTAX31);
                        break;
                }
            }
            break;

        case 'M':   // Memory
            if (cmd[1] != '-')
                set_error(ERR_SYNTAX31);
            else {
                // Read parameters
                uint16 adr = uint8(cmd[3]) | (uint8(cmd[4]) << 8);
                uint8 len = uint8(cmd[5]);

                // Switch on command
                switch (cmd[2]) {
                    case 'R':
                        mem_read_cmd(adr, (cmd_len < 6) ? 1 : len);
                        break;
                    case 'W':
                        mem_write_cmd(adr, len, (uint8 *)cmd + 6);
                        break;
                    case 'E':
                        mem_execute_cmd(adr);
                        break;
                    default:
                        set_error(ERR_SYNTAX31);
                        break;
                }
            }
            break;

        case 'C':   // Copy
            if (!colon)
                set_error(ERR_SYNTAX31);
            else if (!equal || memchr(cmd, '*', cmd_len) || memchr(cmd, '?', cmd_len) || (comma && comma < equal))
                set_error(ERR_SYNTAX30);
            else
                copy_cmd(colon + 1, equal - colon - 1, equal + 1, cmd_len - (equal + 1 - cmd));
            break;

        case 'R':   // Rename
            if (!colon)
                set_error(ERR_SYNTAX34);
            else if (!equal || comma || memchr(cmd, '*', cmd_len) || memchr(cmd, '?', cmd_len))
                set_error(ERR_SYNTAX30);
            else
                rename_cmd(colon + 1, equal - colon - 1, equal + 1, cmd_len - (equal + 1 - cmd));
            break;

        case 'S':   // Scratch
            if (!colon)
                set_error(ERR_SYNTAX34);
            else
                scratch_cmd(colon + 1, cmd_len - (colon + 1 - cmd));
            break;

        case 'P':   // Position
            position_cmd(cmd + 1, cmd_len - 1);
            break;

        case 'I':   // Initialize
            initialize_cmd();
            break;

        case 'N':   // New (format)
            if (!colon)
                set_error(ERR_SYNTAX34);
            else
                new_cmd(colon + 1, comma ? (comma - colon - 1) : cmd_len - (colon + 1 - cmd), comma);
            break;

        case 'V':   // Validate
            validate_cmd();
            break;

        case 'U':   // User
            if (cmd[1] == '0')
                break;
            switch (cmd[1] & 0x0f) {
                case 1: {   // U1/UA: Read block
                    const uint8 *p = colon ? colon + 1 : cmd + 2;
                    int arg1, arg2, arg3, arg4;
                    parse_block_cmd_args(p, arg1, arg2, arg3, arg4);
                    block_read_cmd(arg1, arg3, arg4, true);
                    break;
                }
                case 2: {   // U2/UB: Write block
                    const uint8 *p = colon ? colon + 1 : cmd + 2;
                    int arg1, arg2, arg3, arg4;
                    parse_block_cmd_args(p, arg1, arg2, arg3, arg4);
                    block_write_cmd(arg1, arg3, arg4, true);
                    break;
                }
                case 9:     // U9/UI: C64/VC20 mode switch
                    if (cmd[2] != '+' && cmd[2] != '-')
                        Reset();
                    break;
                case 10:    // U:/UJ: Reset
                    Reset();
                    break;
                default:
                    set_error(ERR_UNIMPLEMENTED);
                    break;
            }
            break;

        default:
            set_error(ERR_SYNTAX31);
            break;
    }
}

// BLOCK-READ:channel,0,track,sector
void Drive::block_read_cmd(int channel, int track, int sector, bool user_cmd)
{
    set_error(ERR_UNIMPLEMENTED);
}

// BLOCK-WRITE:channel,0,track,sector
void Drive::block_write_cmd(int channel, int track, int sector, bool user_cmd)
{
    set_error(ERR_UNIMPLEMENTED);
}

// BLOCK-EXECUTE:channel,0,track,sector
void Drive::block_execute_cmd(int channel, int track, int sector)
{
    set_error(ERR_UNIMPLEMENTED);
}

// BLOCK-ALLOCATE:0,track,sector
void Drive::block_allocate_cmd(int track, int sector)
{
    set_error(ERR_UNIMPLEMENTED);
}

// BLOCK-FREE:0,track,sector
void Drive::block_free_cmd(int track, int sector)
{
    set_error(ERR_UNIMPLEMENTED);
}

// BUFFER-POINTER:channel,pos
void Drive::buffer_pointer_cmd(int channel, int pos)
{
    set_error(ERR_UNIMPLEMENTED);
}

// M-R<adr low><adr high>[<number>]
void Drive::mem_read_cmd(uint16 adr, uint8 len)
{
    unsupp_cmd();
    error_ptr = error_buf;
    error_buf[0] = 0;
    error_len = 0;
    set_error(ERR_OK);
}

// M-W<adr low><adr high><number><data...>
void Drive::mem_write_cmd(uint16 adr, uint8 len, uint8 *p)
{
    set_error(ERR_UNIMPLEMENTED);
}

// M-E<adr low><adr high>
void Drive::mem_execute_cmd(uint16 adr)
{
    set_error(ERR_UNIMPLEMENTED);
}

//   COPY:new=file1,file2,...
//        ^   ^
// new_file   old_files
void Drive::copy_cmd(const uint8 *new_file, int new_file_len, const uint8 *old_files, int old_files_len)
{
    set_error(ERR_UNIMPLEMENTED);
}

// RENAME:new=old
//        ^   ^
// new_file   old_file
void Drive::rename_cmd(const uint8 *new_file, int new_file_len, const uint8 *old_file, int old_file_len)
{
    set_error(ERR_UNIMPLEMENTED);
}

// SCRATCH:file1,file2,...
//         ^
//         files
void Drive::scratch_cmd(const uint8 *files, int files_len)
{
    set_error(ERR_UNIMPLEMENTED);
}

// P<channel><record low><record high><byte>
//  ^
//  cmd
void Drive::position_cmd(const uint8 *cmd, int cmd_len)
{
    set_error(ERR_UNIMPLEMENTED);
}

// INITIALIZE
void Drive::initialize_cmd(void)
{
    set_error(ERR_UNIMPLEMENTED);
}

// NEW:name,id
//     ^   ^
//  name   comma (or NULL)
void Drive::new_cmd(const uint8 *name, int name_len, const uint8 *comma)
{
    set_error(ERR_UNIMPLEMENTED);
}

// VALIDATE
void Drive::validate_cmd(void)
{
    set_error(ERR_UNIMPLEMENTED);
}


/*
 *  Notice user of unsupported drive command
 */

void Drive::unsupp_cmd(void)
{
}


/*
 *  Convert PETSCII<->ASCII
 */

uint8 ascii2petscii(char c)
{
    if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')))
        return c ^ 0x20;
    return c;
}

void ascii2petscii(uint8 *dest, const char *src, int n)
{
    while (n-- && (*dest++ = ascii2petscii(*src++))) ;
}

char petscii2ascii(uint8 c)
{
    if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')))
        return c ^ 0x20;
    if ((c >= 0xc1) && (c <= 0xda))
        return c ^ 0x80;
    return c;
    return c;
}

void petscii2ascii(char *dest, const uint8 *src, int n)
{
    while (n-- && (*dest++ = petscii2ascii(*src++))) ;
}


/*
 *  Check whether file is a mountable disk image or archive file, return type
 */

bool IsMountableFile(const char *path, int &type)
{
    // Read header and determine file size
    uint8 header[64];
    memset(header, 0, sizeof(header));
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    fread(header, 1, sizeof(header), f);
    fclose(f);

    if (IsImageFile(path, header, size)) {
        type = FILE_IMAGE;
        return true;
    }
    else return false;
}
