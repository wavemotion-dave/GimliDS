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
 *  1541d64.h - 1541 emulation in disk image files (.d64/.x64/zipcode)
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

#ifndef _1541D64_H
#define _1541D64_H
#include "sysdeps.h"

#include "IEC.h"


/*
 *  Definitions
 */

// Constants
const int NUM_SECTORS_35 = 683; // Number of sectors in a 35-track image
const int NUM_SECTORS_40 = 768; // Number of sectors in a 40-track image

// Disk image types
enum {
    TYPE_D64,           // D64 file
    TYPE_ED64,          // Converted zipcode file (D64 with header ID)
    TYPE_X64            // x64 file
};

// Channel descriptor
struct channel_desc {
    int mode;           // Channel mode
    bool writing;       // Flag: writing to file (for file channels)
    int buf_num;        // Buffer number for direct access and file channels
    uint8 *buf;         // Pointer to start of buffer
    uint8 *buf_ptr;     // Pointer to current position in buffer
    int buf_len;        // Remaining bytes in buffer
    int track, sector;  // Track and sector the buffer will be written to (for writing to file channels)
    int num_blocks;     // Number of blocks in file (for writing to file channels)
    int dir_track;      // Track...
    int dir_sector;     // ...and sector of directory block containing file entry
    int entry;          // Number of entry in directory block
};

// Disk image file descriptor
struct image_file_desc {
    int type;               // See definitions above
    int header_size;        // Size of file header
    int num_tracks;         // Number of tracks
    uint8 id1, id2;         // Block header ID (as opposed to BAM ID)
    uint8 error_info[NUM_SECTORS_40]; // Sector error information (1 byte/sector)
    bool has_error_info;    // Flag: error info present in file
};

// Disk image drive class
class ImageDrive : public Drive {
public:
    ImageDrive(IEC *iec, const char *filepath);
    virtual ~ImageDrive();

    virtual uint8 Open(int channel, const uint8 *name, int name_len);
    virtual uint8 Close(int channel);
    virtual uint8 Read(int channel, uint8 &byte);
    virtual uint8 Write(int channel, uint8 byte, bool eoi);
    virtual void Reset(void);

private:
    void close_image(void);
    bool change_image(const char *path);

    uint8 open_file(int channel, const uint8 *name, int name_len);
    uint8 open_file_ts(int channel, int track, int sector);
    uint8 create_file(int channel, const uint8 *name, int name_len, int type, bool overwrite = false);
    uint8 open_directory(const uint8 *pattern, int pattern_len);
    uint8 open_direct(int channel, const uint8 *filename);
    void close_all_channels(void);

    int alloc_buffer(int want);
    void free_buffer(int buf);

    bool find_file(const uint8 *pattern, int pattern_len, int &dir_track, int &dir_sector, int &entry, bool cont);
    bool find_first_file(const uint8 *pattern, int pattern_len, int &dir_track, int &dir_sector, int &entry);
    bool find_next_file(const uint8 *pattern, int pattern_len, int &dir_track, int &dir_sector, int &entry);
    bool alloc_dir_entry(int &track, int &sector, int &entry);

    bool is_block_free(int track, int sector);
    int num_free_blocks(int track);
    int alloc_block(int track, int sector);
    int free_block(int track, int sector);
    bool alloc_block_chain(int track, int sector);
    bool free_block_chain(int track, int sector);
    bool alloc_next_block(int &track, int &sector, int interleave);

    bool read_sector(int track, int sector, uint8 *buffer);
    bool write_sector(int track, int sector, uint8 *buffer);
    void write_error_info(void);

    virtual void block_read_cmd(int channel, int track, int sector, bool user_cmd = false);
    virtual void block_write_cmd(int channel, int track, int sector, bool user_cmd = false);
    virtual void block_allocate_cmd(int track, int sector);
    virtual void block_free_cmd(int track, int sector);
    virtual void buffer_pointer_cmd(int channel, int pos);
    virtual void mem_read_cmd(uint16 adr, uint8 len);
    virtual void mem_write_cmd(uint16 adr, uint8 len, uint8 *p);
    virtual void copy_cmd(const uint8 *new_file, int new_file_len, const uint8 *old_files, int old_files_len);
    virtual void rename_cmd(const uint8 *new_file, int new_file_len, const uint8 *old_file, int old_file_len);
    virtual void scratch_cmd(const uint8 *files, int files_len);
    virtual void initialize_cmd(void);
    virtual void new_cmd(const uint8 *name, int name_len, const uint8 *comma);
    virtual void validate_cmd(void);

    FILE *the_file;         // File pointer for image file
    image_file_desc desc;   // Image file descriptor
    bool write_protected;   // Flag: image file write-protected

    uint8 ram[0x800];       // 2k 1541 RAM
    uint8 dir[258];         // Buffer for directory blocks
    uint8 *bam;             // Pointer to BAM in 1541 RAM (buffer 4, upper 256 bytes)
    bool bam_dirty;         // Flag: BAM modified, needs to be written back

    channel_desc ch[18];    // Descriptors for channels 0..17 (16 = internal read, 17 = internal write)
    bool buf_free[4];       // Flags: buffer 0..3 free?
};


/*
 *  Functions
 */

// Check whether file with given header (64 bytes) and size looks like one
// of the file types supported by this module
extern bool IsImageFile(const char *path, const uint8 *header, long size);

// Read directory of disk image file into (empty) c64_dir_entry vector
extern bool ReadImageDirectory(const char *path, vector<c64_dir_entry> &vec);

// Create new blank disk image file
extern bool CreateImageFile(const char *path);

#endif
