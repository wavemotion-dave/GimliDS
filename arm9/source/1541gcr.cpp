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
 *  1541gcr.cpp - Emulation of 1541 GCR disk reading/writing
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
 *  - This is only used for processor-level 1541 emulation.
 *    It simulates the 1541 disk controller hardware (R/W head,
 *    GCR reading/writing).
 *  - The preferences settings for drive 8 are used to
 *    specify the .d64 file
 *
 * Incompatibilities:
 * ------------------
 *
 *  - No GCR writing possible (WriteSector is a ROM patch)
 *  - Programs depending on the exact timing of head movement don't work
 *  - The .d64 error info is unused
 */

#include "sysdeps.h"

#include "1541gcr.h"
#include "CPU1541.h"
#include "Prefs.h"


// Number of tracks/sectors
constexpr unsigned NUM_TRACKS = 35;
constexpr unsigned NUM_SECTORS = 683;

// Size of GCR encoded data
constexpr unsigned GCR_SECTOR_SIZE = 5 + 10 + 9 + 5 + 325 + 12; // SYNC + Header + Gap + SYNC + Data + Gap
constexpr unsigned GCR_TRACK_SIZE = GCR_SECTOR_SIZE * 21;       // Each track in gcr_data has room for 21 sectors
constexpr unsigned GCR_DISK_SIZE = GCR_TRACK_SIZE * NUM_TRACKS;

// Clock cycles per GCR byte
// TODO: handle speed selection
constexpr unsigned CYCLES_PER_BYTE = 30;


// Number of sectors of each track
const int num_sectors[36] = {
    0,
    21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
    19,19,19,19,19,19,19,
    18,18,18,18,18,18,
    17,17,17,17,17
};

// Sector offset of start of track in .d64 file
const int sector_offset[36] = {
    0,
    0,21,42,63,84,105,126,147,168,189,210,231,252,273,294,315,336,
    357,376,395,414,433,452,471,
    490,508,526,544,562,580,
    598,615,632,649,666
};


/*
 *  Constructor: Open .d64 file if processor-level 1541
 *   emulation is enabled
 */

Job1541::Job1541(uint8_t *ram1541) : ram(ram1541), the_file(nullptr)
{
    current_halftrack = 2;  // Track 1

    gcr_data = new uint8_t[GCR_DISK_SIZE];
    memset(gcr_data, 0x55, GCR_DISK_SIZE);

    set_gcr_ptr();
    gcr_offset = 1;

    last_byte_cycle = 0;
    byte_latch = 0;

    motor_on = false;
    write_protected = true;
    disk_changed = true;
    byte_ready = false;

    if (ThePrefs.TrueDrive) {
        open_d64_file(ThePrefs.DrivePath[0]);
    }
}


/*
 *  Destructor: Close .d64 file
 */

Job1541::~Job1541()
{
    close_d64_file();

    delete[] gcr_data;
}


/*
 *  Preferences may have changed
 */

void Job1541::NewPrefs(const Prefs *prefs)
{
    // 1541 emulation turned off?
    if (!prefs->TrueDrive) {
        close_d64_file();

    // 1541 emulation turned on?
    } else if (!ThePrefs.TrueDrive && prefs->TrueDrive) {
        open_d64_file(prefs->DrivePath[0]);

    // .d64 file name changed?
    }
    else if (strcmp(ThePrefs.DrivePath[0], prefs->DrivePath[0]))
    {
        close_d64_file();
        open_d64_file(prefs->DrivePath[0]);
        disk_changed = true;
    }
}


/*
 *  Open .d64 file
 */

void Job1541::open_d64_file(const std::string & filepath)
{
    long size;
    uint8_t magic[4];
    uint8_t bam[256];

    // Clear GCR buffer
    memset(gcr_data, 0x55, GCR_DISK_SIZE);

    // Try opening the file for reading/writing first, then for reading only
    write_protected = false;
    the_file = fopen(filepath.c_str(), "rb+");
    if (the_file == nullptr) {
        write_protected = true;
        the_file = fopen(filepath.c_str(), "rb");
    }
    if (the_file != nullptr) {

        // Check length
        fseek(the_file, 0, SEEK_END);
        if (((size = ftell(the_file))) < (int)(NUM_SECTORS * 256)) {
            fclose(the_file);
            the_file = nullptr;
            return;
        }

        // x64 image?
        fseek(the_file, 0, SEEK_SET);
        fread(&magic, 4, 1, the_file);
        if (magic[0] == 0x43 && magic[1] == 0x15 && magic[2] == 0x41 && magic[3] == 0x64) {
            image_header = 64;
        } else {
            image_header = 0;
        }

        // Preset error info (all sectors no error)
        memset(error_info, 1, NUM_SECTORS);

        // Load sector error info from .d64 file, if present
        if (!image_header && size == NUM_SECTORS * 257) {
            fseek(the_file, NUM_SECTORS * 256, SEEK_SET);
            fread(&error_info, NUM_SECTORS, 1, the_file);
        };

        // Read BAM and get ID
        read_sector(18, 0, bam);
        id1 = bam[162];
        id2 = bam[163];

        // Create GCR encoded disk data from image
        disk2gcr();
    }
}


/*
 *  Close .d64 file
 */

void Job1541::close_d64_file()
{
    // Blank out GCR data
 	memset(gcr_data, 0x55, GCR_DISK_SIZE);
    
    if (the_file != nullptr) {
        fclose(the_file);
        the_file = nullptr;
    }
}


/*
 *  Write sector to disk (1541 ROM patch)
 */

void Job1541::WriteSector()
{
    int track = ram[0x18];
    int sector = ram[0x19];
    uint16_t buf = ram[0x30] | (ram[0x31] << 8);

    if (buf <= 0x0700) {
        if (write_sector(track, sector, ram + buf)) {
            sector2gcr(track, sector);
        }
    }
}


/*
 *  Format one track (1541 ROM patch)
 */

void Job1541::FormatTrack()
{
    int track = ram[0x51];

    // Get new ID
    uint8_t bufnum = ram[0x3d];
    id1 = ram[0x12 + bufnum];
    id2 = ram[0x13 + bufnum];

    // Create empty block
    uint8_t buf[256];
    memset(buf, 1, 256);
    buf[0] = 0x4b;

    // Write block to all sectors on track
    for (int sector=0; sector<num_sectors[track]; sector++) {
        write_sector(track, sector, buf);
        sector2gcr(track, sector);
    }

    // Clear error info (all sectors no error)
    if (track == 35) {
        memset(error_info, 1, NUM_SECTORS);
        // Write error_info to disk?
    }
}


/*
 *  Read sector (256 bytes)
 *  true: success, false: error
 */

bool Job1541::read_sector(unsigned track, unsigned sector, uint8_t *buffer)
{
    int offset;

    floppy_soundfx(0);   // Play floppy SFX if needed
    
    // Convert track/sector to byte offset in file
    if ((offset = offset_from_ts(track, sector)) < 0)
        return false;

    fseek(the_file, offset + image_header, SEEK_SET);
    fread(buffer, 256, 1, the_file);
    return true;
}


/*
 *  Write sector (256 bytes) !! -> GCR
 *  true: success, false: error
 */

bool Job1541::write_sector(unsigned track, unsigned sector, uint8_t *buffer)
{
    int offset;
    
    floppy_soundfx(1);   // Play floppy SFX if needed

    // Convert track/sector to byte offset in file
    if ((offset = offset_from_ts(track, sector)) < 0)
        return false;

    fseek(the_file, offset + image_header, SEEK_SET);
    fwrite(buffer, 256, 1, the_file);
    return true;
}


/*
 *  Convert track/sector to offset
 */

unsigned Job1541::secnum_from_ts(unsigned track, unsigned sector)
{
    return sector_offset[track] + sector;
}

int Job1541::offset_from_ts(unsigned track, unsigned sector)
{
    if ((track < 1) || (track > NUM_TRACKS) ||
        (sector < 0) || (sector >= (unsigned)num_sectors[track]))
        return -1;

    return (sector_offset[track] + sector) << 8;
}


/*
 *  Convert 4 bytes to 5 GCR encoded bytes
 */

const uint16_t gcr_table[16] = {
    0x0a, 0x0b, 0x12, 0x13, 0x0e, 0x0f, 0x16, 0x17,
    0x09, 0x19, 0x1a, 0x1b, 0x0d, 0x1d, 0x1e, 0x15
};

void Job1541::gcr_conv4(const uint8_t *from, uint8_t *to)
{
    uint16_t g;

    g = (gcr_table[*from >> 4] << 5) | gcr_table[*from & 15];
    *to++ = g >> 2;
    *to = (g << 6) & 0xc0;
    from++;

    g = (gcr_table[*from >> 4] << 5) | gcr_table[*from & 15];
    *to++ |= (g >> 4) & 0x3f;
    *to = (g << 4) & 0xf0;
    from++;

    g = (gcr_table[*from >> 4] << 5) | gcr_table[*from & 15];
    *to++ |= (g >> 6) & 0x0f;
    *to = (g << 2) & 0xfc;
    from++;

    g = (gcr_table[*from >> 4] << 5) | gcr_table[*from & 15];
    *to++ |= (g >> 8) & 0x03;
    *to = g;
}


/*
 *  Create GCR encoded disk data from image
 */

void Job1541::sector2gcr(unsigned track, unsigned sector)
{
    uint8_t block[256];
    uint8_t buf[4];
    uint8_t *p = gcr_data + (track-1) * GCR_TRACK_SIZE + sector * GCR_SECTOR_SIZE;

    read_sector(track, sector, block);

    // Create GCR header
    memset(p, 0xff, 5);                     // SYNC
    p += 5;
    buf[0] = 0x08;                          // Header mark
    buf[1] = sector ^ track ^ id2 ^ id1;    // Checksum
    buf[2] = sector;
    buf[3] = track;
    gcr_conv4(buf, p);
    p += 5;
    buf[0] = id2;
    buf[1] = id1;
    buf[2] = 0x0f;
    buf[3] = 0x0f;
    gcr_conv4(buf, p);
    p += 5;

    memset(p, 0x55, 9);                     // Gap
    p += 9;

    // Create GCR data
    memset(p, 0xff, 5);                     // SYNC
    p += 5;
    uint8_t sum;
    buf[0] = 0x07;                          // Data mark
    sum =  buf[1] = block[0];
    sum ^= buf[2] = block[1];
    sum ^= buf[3] = block[2];
    gcr_conv4(buf, p);
    p += 5;
    for (unsigned i = 3; i < 255; i += 4) {
        sum ^= buf[0] = block[i];
        sum ^= buf[1] = block[i+1];
        sum ^= buf[2] = block[i+2];
        sum ^= buf[3] = block[i+3];
        gcr_conv4(buf, p);
        p += 5;
    }
    sum ^= buf[0] = block[255];
    buf[1] = sum;                           // Checksum
    buf[2] = 0;
    buf[3] = 0;
    gcr_conv4(buf, p);
    p += 5;

    memset(p, 0x55, 12);                    // Gap
}

void Job1541::disk2gcr()
{
    // Convert all tracks and sectors
    for (unsigned track = 1; track <= NUM_TRACKS; ++track) {
        for(unsigned sector = 0; sector < (unsigned)num_sectors[track]; ++sector) {
            sector2gcr(track, sector);
        }
    }
}


/*
 *  Reset GCR pointers for current halftrack
 */

void Job1541::set_gcr_ptr()
{
    gcr_track_start = gcr_data + ((current_halftrack >> 1) - 1) * GCR_TRACK_SIZE;
    gcr_track_end = gcr_track_start + num_sectors[current_halftrack >> 1] * GCR_SECTOR_SIZE;
    gcr_track_length = gcr_track_end - gcr_track_start;
}


/*
 *  Move R/W head out (lower track numbers)
 */

void Job1541::MoveHeadOut(uint32_t cycle_counter)
{
    if (current_halftrack == 2)
        return;

    current_halftrack--;

    last_byte_cycle = cycle_counter;
    byte_ready = false;

    set_gcr_ptr();
    gcr_offset = 1; // TODO: handle track-to-track skew
}


/*
 *  Move R/W head in (higher track numbers)
 */

void Job1541::MoveHeadIn(uint32_t cycle_counter)
{
    if (current_halftrack == NUM_TRACKS*2)
        return;

    current_halftrack++;

    last_byte_cycle = cycle_counter;
    byte_ready = false;

    set_gcr_ptr();
    gcr_offset = 1; // TODO: handle track-to-track skew
}


/*
 *  Get state
 */

void Job1541::GetState(Job1541State *state) const
{
    state->current_halftrack = current_halftrack;
    state->gcr_offset = gcr_offset;
    state->last_byte_cycle = last_byte_cycle;
    state->byte_latch = byte_latch;

    state->motor_on = motor_on;
    state->write_protected = write_protected;
    state->disk_changed = disk_changed;
    state->byte_ready = byte_ready;
}


/*
 *  Set state
 */

void Job1541::SetState(const Job1541State *state)
{
    current_halftrack = state->current_halftrack;
    set_gcr_ptr();
    gcr_offset = state->gcr_offset;
    last_byte_cycle = state->last_byte_cycle;
    byte_latch = state->byte_latch;

    motor_on = state->motor_on;
    write_protected = state->write_protected;
    disk_changed = state->disk_changed;
    byte_ready = state->byte_ready;
}


/*
 *  Rotate disk (virtually)
 */

void Job1541::rotate_disk(uint32_t cycle_counter)
{
    if (motor_on) {

        uint32_t elapsed = cycle_counter - last_byte_cycle;
        uint32_t advance = elapsed / CYCLES_PER_BYTE;

        if (advance > 0) {
            gcr_offset += advance;
            while (gcr_offset >= gcr_track_length) {
                gcr_offset -= gcr_track_length;
            }
            if (gcr_offset == 0) {  // Always keep >0 so we can access gcr_track_start[gcr_offset - 1]
                ++gcr_offset;
            }

            // Byte is ready if not on sync
            byte_ready = !(((gcr_track_start[gcr_offset - 1] & 0x03) == 0x03)
                         && (gcr_track_start[gcr_offset] == 0xff));
                         
            if (byte_ready) 
            {
 				byte_latch = gcr_track_start[gcr_offset];
 			}

            last_byte_cycle += advance * CYCLES_PER_BYTE;
        }

    } else {

        last_byte_cycle = cycle_counter;
        byte_ready = false;
    }
}


/*
 *  Check if R/W head is over SYNC
 */

bool Job1541::SyncFound(uint32_t cycle_counter)
{
    rotate_disk(cycle_counter);

    // Sync = ten "1" bits
    return motor_on && ((gcr_track_start[gcr_offset - 1] & 0x03) == 0x03)
                     && (gcr_track_start[gcr_offset] == 0xff);
}


/*
 *  Check if GCR byte is available for reading
 */

bool Job1541::ByteReady(uint32_t cycle_counter)
{
    rotate_disk(cycle_counter);

    return byte_ready;
}


/*
 *  Read one GCR byte from disk
 */

uint8_t Job1541::ReadGCRByte(uint32_t cycle_counter)
{
    floppy_soundfx(0);   // Play floppy SFX if needed
    
    rotate_disk(cycle_counter);

    byte_ready = false;
    return byte_latch;
}


void Job1541::Reset(void)
{
    current_halftrack = 2;  // Track 1
    disk_changed = true;
    motor_on = false;
 	byte_ready = false;    
}
