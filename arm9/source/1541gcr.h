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
 *  1541job.h - Emulation of 1541 GCR disk reading/writing
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

#ifndef _1541GCR_H
#define _1541GCR_H
#include <nds.h>
#include "sysdeps.h"

class MOS6502_1541;
class Prefs;
struct Job1541State;

class Job1541 {
public:
    Job1541(uint8 *ram1541);
    ~Job1541();

    void GetState(Job1541State *state);
    void SetState(Job1541State *state);
    void NewPrefs(Prefs *prefs);
    void MoveHeadOut(void);
    void MoveHeadIn(void);
    bool SyncFound(void);
    bool ByteReady(void);
    uint8 ReadGCRByte(void);
    uint8 WPState(void);
    void WriteSector(void);
    void FormatTrack(void);
    void Reset(void);
    void SetMotor(bool on);

private:
    void open_d64_file(char *filepath);
    void close_d64_file(void);
    bool read_sector(int track, int sector, uint8 *buffer);
    bool write_sector(int track, int sector, uint8 *buffer);
    void format_disk(void);
    int secnum_from_ts(int track, int sector);
    int offset_from_ts(int track, int sector);
    void gcr_conv4(uint8 *from, uint8 *to);
    void sector2gcr(int track, int sector);
    void disk2gcr(void);

    uint8 *ram;             // Pointer to 1541 RAM
    FILE *the_file;         // File pointer for .d64 file
    int image_header;       // Length of .d64/.x64 file header

    uint8 id1, id2;         // ID of disk
    uint8 error_info[683];  // Sector error information (1 byte/sector)

    uint8 *gcr_data;        // Pointer to GCR encoded disk data
    uint8 *gcr_ptr;         // Pointer to GCR data under R/W head
    uint8 *gcr_track_start; // Pointer to start of GCR data of current track
    uint8 *gcr_track_end;   // Pointer to end of GCR data of current track
    int current_halftrack;  // Current halftrack number (2..70)

    bool write_protected;   // Flag: Disk write-protected
    bool disk_changed;      // Flag: Disk changed (WP sensor strobe control)
    bool motor_on;
};

// 1541 GCR state
struct Job1541State {
    int current_halftrack;
    uint32 gcr_ptr;
    bool write_protected;
    bool disk_changed;
};


/*
 *  Check if R/W head is over SYNC
 */

inline bool Job1541::SyncFound(void)
{
    if (*gcr_ptr == 0xff && gcr_ptr[1] != 0xff)
        return true;
    else {
        gcr_ptr++;      // Rotate disk
        if (gcr_ptr == gcr_track_end)
            gcr_ptr = gcr_track_start;
        return false;
    }
}


/*
 *  Read one GCR byte from disk
 */

inline uint8 Job1541::ReadGCRByte(void)
{
    extern void floppy_soundfx(u8 type);
    floppy_soundfx(0);   // Play floppy SFX if needed
    
    uint8 byte = *gcr_ptr++;    // Rotate disk
    if (gcr_ptr == gcr_track_end)
        gcr_ptr = gcr_track_start;
    return byte;
}


/*
 *  Return state of write protect sensor
 */

inline uint8 Job1541::WPState(void)
{
    if (disk_changed) { // Disk change -> WP sensor strobe
        disk_changed = false;
        return write_protected ? 0x10 : 0;
    } else
        return write_protected ? 0 : 0x10;
}

#endif
