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
 *  IEC.h - IEC bus routines, 1541 emulation (DOS level)
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

#ifndef _IEC_H
#define _IEC_H


/*
 *  Definitions
 */

// Maximum length of file names
const int NAMEBUF_LENGTH = 256;

// C64 status codes
enum {
    ST_OK = 0,              // No error
    ST_READ_TIMEOUT = 0x02, // Timeout on reading
    ST_TIMEOUT = 0x03,      // Timeout
    ST_EOF = 0x40,          // End of file
    ST_NOTPRESENT = 0x80    // Device not present
};

// 1541 error codes
enum {
    ERR_OK,             // 00 OK
    ERR_SCRATCHED,      // 01 FILES SCRATCHED
    ERR_UNIMPLEMENTED,  // 03 UNIMPLEMENTED
    ERR_READ20,         // 20 READ ERROR (block header not found)
    ERR_READ21,         // 21 READ ERROR (no sync character)
    ERR_READ22,         // 22 READ ERROR (data block not present)
    ERR_READ23,         // 23 READ ERROR (checksum error in data block)
    ERR_READ24,         // 24 READ ERROR (byte decoding error)
    ERR_WRITE25,        // 25 WRITE ERROR (write-verify error)
    ERR_WRITEPROTECT,   // 26 WRITE PROTECT ON
    ERR_READ27,         // 27 READ ERROR (checksum error in header)
    ERR_WRITE28,        // 28 WRITE ERROR (long data block)
    ERR_DISKID,         // 29 DISK ID MISMATCH
    ERR_SYNTAX30,       // 30 SYNTAX ERROR (general syntax)
    ERR_SYNTAX31,       // 31 SYNTAX ERROR (invalid command)
    ERR_SYNTAX32,       // 32 SYNTAX ERROR (command too long)
    ERR_SYNTAX33,       // 33 SYNTAX ERROR (wildcards on writing)
    ERR_SYNTAX34,       // 34 SYNTAX ERROR (missing file name)
    ERR_WRITEFILEOPEN,  // 60 WRITE FILE OPEN
    ERR_FILENOTOPEN,    // 61 FILE NOT OPEN
    ERR_FILENOTFOUND,   // 62 FILE NOT FOUND
    ERR_FILEEXISTS,     // 63 FILE EXISTS
    ERR_FILETYPE,       // 64 FILE TYPE MISMATCH
    ERR_NOBLOCK,        // 65 NO BLOCK
    ERR_ILLEGALTS,      // 66 ILLEGAL TRACK OR SECTOR
    ERR_NOCHANNEL,      // 70 NO CHANNEL
    ERR_DIRERROR,       // 71 DIR ERROR
    ERR_DISKFULL,       // 72 DISK FULL
    ERR_STARTUP,        // 73 Power-up message
    ERR_NOTREADY        // 74 DRIVE NOT READY
};

// Mountable file types
enum {
    FILE_IMAGE,         // Disk image, handled by ImageDrive
    FILE_ARCH           // Archive file, handled by ArchDrive
};

// 1541 file types
enum {
    FTYPE_DEL,          // Deleted
    FTYPE_SEQ,          // Sequential
    FTYPE_PRG,          // Program
    FTYPE_USR,          // User
    FTYPE_REL,          // Relative
    FTYPE_UNKNOWN
};

static const char ftype_char[9] = "DSPUL   ";

// 1541 file access modes
enum {
    FMODE_READ,         // Read
    FMODE_WRITE,        // Write
    FMODE_APPEND,       // Append
    FMODE_M             // Read open file
};

// Drive LED states
enum {
    DRVLED_OFF,     // Inactive, LED off
    DRVLED_ON,      // Active, LED on
    DRVLED_ERROR    // Error, blink LED
};

// Information about file in disk image/archive file
struct c64_dir_entry {
    c64_dir_entry(const uint8 *n, int t, bool o, bool p, size_t s, off_t ofs = 0, uint8 sal = 0, uint8 sah = 0)
        : type(t), is_open(o), is_protected(p), size(s), offset(ofs), sa_lo(sal), sa_hi(sah)
    {
        strncpy((char *)name, (const char *)n, 17);
        name[16] = 0;
    }

    // Basic information
    uint8 name[17];     // File name (C64 charset, null-terminated)
    int type;           // File type (see defines above)
    bool is_open;       // Flag: file open
    bool is_protected;  // Flag: file protected
    size_t size;        // File size (may be approximated)

    // Special information
    off_t offset;       // Offset of file in archive file
    uint8 sa_lo, sa_hi; // C64 start address
};

class Drive;
class C64Display;
class Prefs;

// Class for complete IEC bus system with drives 8..11
class IEC {
public:
    IEC(C64Display *display);
    ~IEC();

    void Reset(void);
    void NewPrefs(Prefs *prefs);
    void UpdateLEDs(void);

    uint8 Out(uint8 byte, bool eoi);
    uint8 OutATN(uint8 byte);
    uint8 OutSec(uint8 byte);
    uint8 In(uint8 &byte);
    void SetATN(void);
    void RelATN(void);
    void Turnaround(void);
    void Release(void);

private:
    Drive *create_drive(const char *path);

    uint8 listen(int device);
    uint8 talk(int device);
    uint8 unlisten(void);
    uint8 untalk(void);
    uint8 sec_listen(void);
    uint8 sec_talk(void);
    uint8 open_out(uint8 byte, bool eoi);
    uint8 data_out(uint8 byte, bool eoi);
    uint8 data_in(uint8 &byte);

    C64Display *the_display;    // Pointer to display object (for drive LEDs)

    uint8 name_buf[NAMEBUF_LENGTH]; // Buffer for file names and command strings
    uint8 *name_ptr;        // Pointer for reception of file name
    int name_len;           // Received length of file name

    Drive *drive[2];        // 2 drives (8 and 9)

    Drive *listener;        // Pointer to active listener
    Drive *talker;          // Pointer to active talker

    bool listener_active;   // Listener selected, listener_data is valid
    bool talker_active;     // Talker selected, talker_data is valid
    bool listening;         // Last ATN was listen (to decide between sec_listen/sec_talk)

    uint8 received_cmd;     // Received command code ($x0)
    uint8 sec_addr;         // Received secondary address ($0x)
};

// Abstract superclass for individual drives
class Drive {
public:
    Drive(IEC *iec);
    virtual ~Drive() {}

    virtual uint8 Open(int channel, const uint8 *name, int name_len)=0;
    virtual uint8 Close(int channel)=0;
    virtual uint8 Read(int channel, uint8 &byte)=0;
    virtual uint8 Write(int channel, uint8 byte, bool eoi)=0;
    virtual void Reset(void)=0;

    int LED;            // Drive LED state
    bool Ready;         // Drive is ready for operation

protected:
    void set_error(int error, int track = 0, int sector = 0);

    void parse_file_name(const uint8 *src, int src_len, uint8 *dest, int &dest_len, int &mode, int &type, int &rec_len, bool convert_charset = false);

    void execute_cmd(const uint8 *cmd, int cmd_len);
    virtual void block_read_cmd(int channel, int track, int sector, bool user_cmd = false);
    virtual void block_write_cmd(int channel, int track, int sector, bool user_cmd = false);
    virtual void block_execute_cmd(int channel, int track, int sector);
    virtual void block_allocate_cmd(int track, int sector);
    virtual void block_free_cmd(int track, int sector);
    virtual void buffer_pointer_cmd(int channel, int pos);
    virtual void mem_read_cmd(uint16 adr, uint8 len);
    virtual void mem_write_cmd(uint16 adr, uint8 len, uint8 *p);
    virtual void mem_execute_cmd(uint16 adr);
    virtual void copy_cmd(const uint8 *new_file, int new_file_len, const uint8 *old_files, int old_files_len);
    virtual void rename_cmd(const uint8 *new_file, int new_file_len, const uint8 *old_file, int old_file_len);
    virtual void scratch_cmd(const uint8 *files, int files_len);
    virtual void position_cmd(const uint8 *cmd, int cmd_len);
    virtual void initialize_cmd(void);
    virtual void new_cmd(const uint8 *name, int name_len, const uint8 *comma);
    virtual void validate_cmd(void);
    void unsupp_cmd(void);

    char error_buf[256];    // Buffer with current error message
    char *error_ptr;        // Pointer within error message
    int error_len;          // Remaining length of error message
    int current_error;      // Number of current error

    uint8 cmd_buf[64];      // Buffer for incoming command strings
    int cmd_len;            // Length of received command

private:
    IEC *the_iec;           // Pointer to IEC object
};


/*
 *  Functions
 */

// Convert ASCII character to PETSCII character
extern uint8 ascii2petscii(char c);

// Convert ASCII string to PETSCII string
extern void ascii2petscii(uint8 *dest, const char *src, int max);

// Convert PETSCII character to ASCII character
extern char petscii2ascii(uint8 c);

// Convert PETSCII string to ASCII string
extern void petscii2ascii(char *dest, const uint8 *src, int max);

// Check whether file is a mountable disk image or archive file, return type
extern bool IsMountableFile(const char *path, int &type);

// Read directory of mountable disk image or archive file into c64_dir_entry vector
extern bool ReadDirectory(const char *path, int type, vector<c64_dir_entry> &vec);

#endif
