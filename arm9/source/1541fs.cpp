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
 *  1541fs.cpp - 1541 emulation in host file system
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
 *  - If the directory is opened (file name "$"), a temporary file
 *    with the structure of a 1541 directory file is created and
 *    opened. It can then be accessed in the same way as all other
 *    files.
 *
 * Incompatibilities:
 * ------------------
 *
 *  - No "raw" directory reading
 *  - No relative/sequential/user files
 *  - Only "I" and "UJ" commands implemented
 */

#include "sysdeps.h"

#include "1541fs.h"
#include "IEC.h"
#include "main.h"
#include "Prefs.h"

// Prototypes
static bool match(const char *p, const char *n);

/*
 *  Constructor: Prepare emulation
 */

FSDrive::FSDrive(IEC *iec, const char *path) : Drive(iec)
{
    strcpy(orig_dir_path, path);
    dir_path[0] = 0;
    
    if (change_dir(orig_dir_path)) {
        for (int i=0; i<16; i++)
            file[i] = NULL;

        Reset();

        Ready = true;
    }
}


/*
 *  Destructor
 */

FSDrive::~FSDrive()
{
    if (Ready) {
        close_all_channels();
        Ready = false;
    }
}


/*
 *  Change emulation directory
 */

bool FSDrive::change_dir(char *dirpath)
{
    DIR *dir;

    if ((dir = opendir(dirpath)) != NULL) {
        closedir(dir);
        strcpy(dir_path, dirpath);
        strncpy(dir_title, dir_path, 16);
        return true;
    } else
        return false;
}


/*
 *  Open channel
 */

uint8 FSDrive::Open(int channel, const uint8 *name, int name_len)
{
    set_error(ERR_OK);

    // Channel 15: Execute file name as command
    if (channel == 15) {
        execute_cmd(name, name_len);
        return ST_OK;
    }

    // Close previous file if still open
    if (file[channel]) {
        fclose(file[channel]);
        file[channel] = NULL;
    }

    if (name[0] == '#') {
        set_error(ERR_NOCHANNEL);
        return ST_OK;
    }

    if (name[0] == '$')
        return open_directory(channel, name + 1, name_len - 1);

    return open_file(channel, name, name_len);
}


/*
 *  Open file
 */

uint8 FSDrive::open_file(int channel, const uint8 *name, int name_len)
{
    char plain_name[NAMEBUF_LENGTH];
    int plain_name_len;
    int mode = FMODE_READ;
    int type = FTYPE_PRG;
    int rec_len = 0;
    parse_file_name(name, name_len, (uint8 *)plain_name, plain_name_len, mode, type, rec_len, true);

    // Channel 0 is READ, channel 1 is WRITE
    if (channel == 0 || channel == 1) {
        mode = channel ? FMODE_WRITE : FMODE_READ;
        if (type == FTYPE_DEL)
            type = FTYPE_PRG;
    }

    bool writing = (mode == FMODE_WRITE || mode == FMODE_APPEND);

    // Expand wildcards (only allowed on reading)
    if (strchr(plain_name, '*') || strchr(plain_name, '?')) {
        if (writing) {
            set_error(ERR_SYNTAX33);
            return ST_OK;
        } else
            find_first_file(plain_name);
    }

    // Relative files are not supported
    if (type == FTYPE_REL) {
        set_error(ERR_UNIMPLEMENTED);
        return ST_OK;
    }

    // Select fopen() mode according to file mode
    const char *mode_str = "rb";
    switch (mode) {
        case FMODE_WRITE:
            mode_str = "wb";
            break;
        case FMODE_APPEND:
            mode_str = "ab";
            break;
    }

    // Open file
    if (chdir(dir_path))
        set_error(ERR_NOTREADY);
    else if ((file[channel] = fopen(plain_name, mode_str)) != NULL) {
        if (mode == FMODE_READ || mode == FMODE_M)  // Read and buffer first byte
            read_char[channel] = fgetc(file[channel]);
    } else
        set_error(ERR_FILENOTFOUND);

    return ST_OK;
}


/*
 *  Find first file matching wildcard pattern and get its real name
 */

// Return true if name 'n' matches pattern 'p'
static bool match(const char *p, const char *n)
{
    if (!*p)        // Null pattern matches everything
        return true;

    do {
        if (*p == '*')  // Wildcard '*' matches all following characters
            return true;
        if ((*p != *n) && (*p != '?'))  // Wildcard '?' matches single character
            return false;
        p++; n++;
    } while (*p);

    return !*n;
}

void FSDrive::find_first_file(char *pattern)
{
    DIR *dir;
    struct dirent *de;

    // Open directory for reading and skip '.' and '..'
    if ((dir = opendir(dir_path)) == NULL)
        return;
    de = readdir(dir);
    while (de && (0 == strcmp(".", de->d_name) || 0 == strcmp("..", de->d_name))) 
        de = readdir(dir);

    while (de) {

        // Match found? Then copy real file name
        if (match(pattern, de->d_name)) {
            strncpy(pattern, de->d_name, NAMEBUF_LENGTH);
            closedir(dir);
            return;
        }

        // Get next directory entry
        de = readdir(dir);
    }

    closedir(dir);
}


/*
 *  Open directory, create temporary file
 */

uint8 FSDrive::open_directory(int channel, const uint8 *pattern, int pattern_len)
{
    char buf[] = "\001\004\001\001\0\0\022\042                \042 00 2A";
    char str[NAMEBUF_LENGTH];
    char *p, *q;
    int i;

    DIR *dir;
    struct dirent *de;
    struct stat statbuf;

    // Special treatment for "$0"
    if (pattern[0] == '0' && pattern[1] == 0) {
        pattern++;
        pattern_len--;
    }

    // Skip everything before the ':' in the pattern
    uint8 *t = (uint8 *)memchr(pattern, ':', pattern_len);
    if (t)
        pattern = t + 1;

    // Convert pattern to ASCII
    char ascii_pattern[NAMEBUF_LENGTH];
    petscii2ascii(ascii_pattern, pattern, NAMEBUF_LENGTH);

    // Open directory for reading and skip '.' and '..'
    if ((dir = opendir(dir_path)) == NULL) {
        set_error(ERR_NOTREADY);
        return ST_OK;
    }
    de = readdir(dir);
    while (de && (0 == strcmp(".", de->d_name) || 0 == strcmp("..", de->d_name))) 
        de = readdir(dir);

    // Create temporary file
    if ((file[channel] = fopen("ramdiskfs:/ram/test4.txt","wb")) == NULL) {
        closedir(dir);
        return ST_OK;
    }

    // Create directory title
    p = &buf[8];
    for (i=0; i<16 && dir_title[i]; i++)
        *p++ = ascii2petscii(dir_title[i]);
    fwrite(buf, 1, 32, file[channel]);

    // Create and write one line for every directory entry
    while (de) {

        // Include only files matching the ascii_pattern
        if (match(ascii_pattern, de->d_name)) {

            // Get file statistics
            chdir(dir_path);
            stat(de->d_name, &statbuf);

            // Clear line with spaces and terminate with null byte
            memset(buf, ' ', 31);
            buf[31] = 0;

            p = buf;
            *p++ = 0x01;    // Dummy line link
            *p++ = 0x01;

            // Calculate size in blocks (254 bytes each)
            i = (statbuf.st_size + 254) / 254;
            *p++ = i & 0xff;
            *p++ = (i >> 8) & 0xff;

            p++;
            if (i < 10) p++;    // Less than 10: add one space
            if (i < 100) p++;   // Less than 100: add another space

            // Convert and insert file name
            strcpy(str, de->d_name);
            *p++ = '\"';
            q = p;
            for (i=0; i<16 && str[i]; i++)
                *q++ = ascii2petscii(str[i]);
            *q++ = '\"';
            p += 18;

            // File type
            if (S_ISDIR(statbuf.st_mode)) {
                *p++ = 'D';
                *p++ = 'I';
                *p++ = 'R';
            } else {
                *p++ = 'P';
                *p++ = 'R';
                *p++ = 'G';
            }

            // Write line
            fwrite(buf, 1, 32, file[channel]);
        }

        // Get next directory entry
        de = readdir(dir);
    }

    // Final line
    fwrite("\001\001\0\0BLOCKS FREE.             \0\0", 1, 32, file[channel]);

    // Rewind file for reading and read first byte
    rewind(file[channel]);
    read_char[channel] = fgetc(file[channel]);

#ifndef __riscos
    // Close directory
    closedir(dir);
#endif
    return ST_OK;
}


/*
 *  Close channel
 */

uint8 FSDrive::Close(int channel)
{
    if (channel == 15) {
        close_all_channels();
        return ST_OK;
    }

    if (file[channel]) {
        fclose(file[channel]);
        file[channel] = NULL;
    }

    return ST_OK;
}


/*
 *  Close all channels
 */

void FSDrive::close_all_channels(void)
{
    for (int i=0; i<15; i++)
        Close(i);

    cmd_len = 0;
}


/*
 *  Read from channel
 */

uint8 FSDrive::Read(int channel, uint8 &byte)
{
    int c;

    // Channel 15: Error channel
    if (channel == 15) {
        byte = *error_ptr++;

        if (byte != '\r')
            return ST_OK;
        else {  // End of message
            set_error(ERR_OK);
            return ST_EOF;
        }
    }

    if (!file[channel]) return ST_READ_TIMEOUT;

    // Read one byte
    byte = read_char[channel];
    c = fgetc(file[channel]);
    if (c == EOF)
        return ST_EOF;
    else {
        read_char[channel] = c;
        return ST_OK;
    }
}


/*
 *  Write to channel
 */

uint8 FSDrive::Write(int channel, uint8 byte, bool eoi)
{
    // Channel 15: Collect chars and execute command on EOI
    if (channel == 15) {
        if (cmd_len >= 58)
            return ST_TIMEOUT;
        
        cmd_buf[cmd_len++] = byte;

        if (eoi) {
            execute_cmd(cmd_buf, cmd_len);
            cmd_len = 0;
        }
        return ST_OK;
    }

    if (!file[channel]) {
        set_error(ERR_FILENOTOPEN);
        return ST_TIMEOUT;
    }

    if (putc(byte, file[channel]) == EOF) {
        set_error(ERR_WRITE25);
        return ST_TIMEOUT;
    }

    return ST_OK;
}


/*
 *  Execute drive commands
 */

// INITIALIZE
void FSDrive::initialize_cmd(void)
{
    close_all_channels();
}

// VALIDATE
void FSDrive::validate_cmd(void)
{
}


/*
 *  Reset drive
 */

void FSDrive::Reset(void)
{
    close_all_channels();
    cmd_len = 0;    
    set_error(ERR_STARTUP);
}
