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
 *  1541gcr.h - Emulation of 1541 GCR disk reading/writing
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

#include <string>


class MOS6502_1541;
class Prefs;
struct Job1541State;

class Job1541 {
public:
    Job1541(uint8_t *ram1541);
    ~Job1541();

    void GetState(Job1541State *state) const;
    void SetState(const Job1541State *state);
    void NewPrefs(const Prefs *prefs);
    void Reset(void);

    void SetMotor(bool on);
    void MoveHeadOut(uint32_t cycle_counter);
    void MoveHeadIn(uint32_t cycle_counter);

    bool SyncFound(uint32_t cycle_counter);
    bool ByteReady(uint32_t cycle_counter);
    uint8_t ReadGCRByte(uint32_t cycle_counter);
    uint8_t WPState();

    void WriteSector();
    void FormatTrack();

private:
    void open_d64_file(const std::string & filepath);
    void close_d64_file();
    bool read_sector(unsigned track, unsigned sector, uint8_t *buffer);
    bool write_sector(unsigned track, unsigned sector, uint8_t *buffer);
    void format_disk();
    unsigned secnum_from_ts(unsigned track, unsigned sector);
    int offset_from_ts(unsigned track, unsigned sector);
    void gcr_conv4(const uint8_t *from, uint8_t *to);
    void sector2gcr(unsigned track, unsigned sector);
    void disk2gcr();
    void set_gcr_ptr();
    void rotate_disk(uint32_t cycle_counter);

    uint8_t *ram;               // Pointer to 1541 RAM
    FILE *the_file;             // File pointer for .d64 file
    unsigned image_header;      // Length of .d64/.x64 file header

    uint8_t id1, id2;           // ID of disk
    uint8_t error_info[683];    // Sector error information (1 byte/sector)

    unsigned current_halftrack; // Current halftrack number (2..70)

    uint8_t *gcr_data;          // Pointer to GCR encoded disk data
    uint8_t *gcr_track_start;   // Pointer to start of GCR data of current track
    uint8_t *gcr_track_end;     // Pointer to end of GCR data of current track
    size_t gcr_track_length;    // Number of GCR bytes in current track
    size_t gcr_offset;          // Offset of GCR data byte under R/W head, relative to gcr_track_start
                                // Note: This is never 0, so we can access the previous GCR byte for sync detection

    uint32_t last_byte_cycle;   // Cycle when last byte was available
    uint8_t byte_latch;         // Latch for read GCR byte

    bool motor_on;              // Flag: Spindle motor on
    bool write_protected;       // Flag: Disk write-protected
    bool disk_changed;          // Flag: Disk changed (WP sensor strobe control)
    bool byte_ready;            // Flag: GCR byte ready for reading
};

// 1541 GCR state
struct Job1541State {
    uint32_t current_halftrack;
    uint32_t gcr_offset;
    uint32_t last_byte_cycle;
    uint8_t byte_latch;
    bool motor_on;
    bool write_protected;
    bool disk_changed;
    bool byte_ready;
};


/*
 *  Control spindle motor
 */

inline void Job1541::SetMotor(bool on)
{
    motor_on = on;
}


/*
 *  Return state of write protect sensor as VIA port value (PB4)
 */

inline uint8_t Job1541::WPState()
{
    if (disk_changed) { // Disk change -> WP sensor strobe
        disk_changed = false;
        return write_protected ? 0x10 : 0;
    } else {
        return write_protected ? 0 : 0x10;
    }
}

#endif
