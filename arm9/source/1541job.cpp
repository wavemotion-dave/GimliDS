/*
 *  1541job.cpp - Emulation of 1541 GCR disk reading/writing
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
 *  - Programs depending on the exact timing of head movement/disk
 *    rotation don't work
 *  - The .d64 error info is unused
 */
#include <nds.h>
#include "sysdeps.h"

#include "1541job.h"
#include "CPU1541.h"
#include "Prefs.h"

// Number of tracks/sectors
const int NUM_TRACKS = 35;
const int NUM_SECTORS = 683;

// Size of GCR encoded data
const int GCR_SECTOR_SIZE = 5 + 10 + 9 + 5 + 325 + 12;  // SYNC + Header + Gap + SYNC + Data + Gap

const int GCR_TRACK_SIZE = GCR_SECTOR_SIZE * 21;    // Each track in gcr_data has 21 sectors
const int GCR_DISK_SIZE = GCR_TRACK_SIZE * NUM_TRACKS;

// Job return codes
const int RET_OK = 1;               // No error
const int RET_NOT_FOUND = 2;        // Block not found
const int RET_NOT_READY = 15;       // Drive not ready


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

Job1541::Job1541(uint8 *ram1541) : ram(ram1541)
{
    the_file = NULL;

    gcr_data = gcr_ptr = gcr_track_start = new uint8[GCR_DISK_SIZE];
    gcr_track_end = gcr_track_start + GCR_TRACK_SIZE;
    current_halftrack = 2;

    disk_changed = true;
    motor_on = false;

    if (ThePrefs.TrueDrive)
        open_d64_file(ThePrefs.DrivePath[0]);
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

void Job1541::NewPrefs(Prefs *prefs)
{
    // 1541 emulation turned off?
    if (!prefs->TrueDrive)
        close_d64_file();

    // 1541 emulation turned on?
    else if (!ThePrefs.TrueDrive && prefs->TrueDrive)
        open_d64_file(prefs->DrivePath[0]);

    // .d64 file name changed?
    else
    {
        if (strcmp(ThePrefs.DrivePath[0], prefs->DrivePath[0]))
        {
            close_d64_file();
            open_d64_file(prefs->DrivePath[0]);
            disk_changed = true;
        }
    }
}


/*
 *  Open .d64 file
 */

void Job1541::open_d64_file(char *filepath)
{
    long size;
    uint8 magic[4];
    static uint8 bam[256];
    //printdbg(filepath);
    // Clear GCR buffer
    memset(gcr_data, 0x55, GCR_DISK_SIZE);

    // Try opening the file for reading/writing first, then for reading only
    write_protected = false;
    the_file = fopen(filepath, "rb+");
    if (the_file == NULL) {
        write_protected = true;
        the_file = fopen(filepath, "rb");
    }
    if (the_file != NULL) {

        // Check length
        fseek(the_file, 0, SEEK_END);
        if ((size = ftell(the_file)) < NUM_SECTORS * 256) {
            fclose(the_file);
            the_file = NULL;
            return;
        }

        // x64 image?
        fseek(the_file, 0, SEEK_SET);
        fread(&magic, 4, 1, the_file);
        if (magic[0] == 0x43 && magic[1] == 0x15 && magic[2] == 0x41 && magic[3] == 0x64)
            image_header = 64;
        else
            image_header = 0;

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

void Job1541::close_d64_file(void)
{
    if (the_file != NULL) {
        fclose(the_file);
        the_file = NULL;
    }
}

void Job1541::Reset(void)
{
    current_halftrack = 2;
    disk_changed = true;
    motor_on = false;
}


/*
 *  Write sector to disk (1541 ROM patch)
 */

void Job1541::WriteSector(void)
{
    int track  = ram[0x18];
    int sector = ram[0x19];
    uint16 buf = ram[0x30] | (ram[0x31] << 8);

    if (buf <= 0x0700)
        if (write_sector(track, sector, ram + buf))
            sector2gcr(track, sector);
}


/*
 *  Format one track (1541 ROM patch)
 */

void Job1541::FormatTrack(void)
{
    int track = ram[0x51];

    // Get new ID
    uint8 bufnum = ram[0x3d];
    id1 = ram[0x12 + bufnum];
    id2 = ram[0x13 + bufnum];

    // Create empty block
    uint8 buf[256];
    memset(buf, 1, 256);
    buf[0] = 0x4b;

    // Write block to all sectors on track
    for(int sector=0; sector<num_sectors[track]; sector++) {
        write_sector(track, sector, buf);
        sector2gcr(track, sector);
    }

    // Clear error info (all sectors no error)
    if (track == 35)
        memset(error_info, 1, NUM_SECTORS);
        // Write error_info to disk?
}


/*
 *  Read sector (256 bytes)
 *  true: success, false: error
 */

bool Job1541::read_sector(int track, int sector, uint8 *buffer)
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

bool Job1541::write_sector(int track, int sector, uint8 *buffer)
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

int Job1541::secnum_from_ts(int track, int sector)
{
    return sector_offset[track] + sector;
}

int Job1541::offset_from_ts(int track, int sector)
{
    if ((track < 1) || (track > NUM_TRACKS) ||
        (sector < 0) || (sector >= num_sectors[track]))
        return -1;

    return (sector_offset[track] + sector) << 8;
}


/*
 *  Convert 4 bytes to 5 GCR encoded bytes
 */

const uint16 gcr_table[16] = {
    0x0a, 0x0b, 0x12, 0x13, 0x0e, 0x0f, 0x16, 0x17,
    0x09, 0x19, 0x1a, 0x1b, 0x0d, 0x1d, 0x1e, 0x15
};

void Job1541::gcr_conv4(uint8 *from, uint8 *to)
{
    uint16 g;

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

void Job1541::sector2gcr(int track, int sector)
{
    uint8 block[256];
    uint8 buf[4];
    uint8 *p = gcr_data + (track-1) * GCR_TRACK_SIZE + sector * GCR_SECTOR_SIZE;

    read_sector(track, sector, block);

    // Create GCR header
    *p++ = 0xff;*p++ = 0xff;*p++ = 0xff;*p++ = 0xff;*p++ = 0xff; // SYNC
    buf[0] = 0x08;                          // Header mark
    buf[1] = sector ^ track ^ id2 ^ id1;    // Checksum
    buf[2] = sector;
    buf[3] = track;
    gcr_conv4(buf, p);
    buf[0] = id2;
    buf[1] = id1;
    buf[2] = 0x0f;
    buf[3] = 0x0f;
    gcr_conv4(buf, p+5);
    p += 10;
    memset(p, 0x55, 9);                     // Gap
    p += 9;

    // Create GCR data
    uint8 sum;
    *p++ = 0xff;*p++ = 0xff;*p++ = 0xff;*p++ = 0xff;*p++ = 0xff; // SYNC
    buf[0] = 0x07;                          // Data mark
    sum = buf[1] = block[0];
    sum ^= buf[2] = block[1];
    sum ^= buf[3] = block[2];
    gcr_conv4(buf, p);
    p += 5;
    for (int i=3; i<255; i+=4) {
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
    memset(p, 0x55, 12);                        // Gap
}

void Job1541::disk2gcr(void)
{
    // Convert all tracks and sectors
    for (int track=1; track<=NUM_TRACKS; track++)
        for(int sector=0; sector<num_sectors[track]; sector++)
            sector2gcr(track, sector);
}


/*
 *  Move R/W head out (lower track numbers)
 */

void Job1541::MoveHeadOut(void)
{
    if (current_halftrack == 2)
        return;
    current_halftrack--;

    gcr_ptr = gcr_track_start = gcr_data + ((current_halftrack >> 1) - 1) * GCR_TRACK_SIZE;
    gcr_track_end = gcr_track_start + num_sectors[current_halftrack >> 1] * GCR_SECTOR_SIZE;
}

/*
  *  Control spindle motor
  */

void Job1541::SetMotor(bool on)
{
    motor_on = on;
}


bool Job1541::ByteReady(void)
{
    return motor_on;
}


/*
 *  Move R/W head in (higher track numbers)
 */

void Job1541::MoveHeadIn(void)
{
    if (current_halftrack == NUM_TRACKS*2)
        return;
    current_halftrack++;

    gcr_ptr = gcr_track_start = gcr_data + ((current_halftrack >> 1) - 1) * GCR_TRACK_SIZE;
    gcr_track_end = gcr_track_start + num_sectors[current_halftrack >> 1] * GCR_SECTOR_SIZE;
}


/*
 *  Get state
 */

void Job1541::GetState(Job1541State *state)
{
    state->current_halftrack = current_halftrack;
    state->gcr_ptr = gcr_ptr - gcr_data;
    state->write_protected = write_protected;
    state->disk_changed = disk_changed;
}


/*
 *  Set state
 */

void Job1541::SetState(Job1541State *state)
{
    current_halftrack = state->current_halftrack;
    gcr_ptr = gcr_data + state->gcr_ptr;
    gcr_track_start = gcr_data + ((current_halftrack >> 1) - 1) * GCR_TRACK_SIZE;
    gcr_track_end = gcr_track_start + num_sectors[current_halftrack >> 1] * GCR_SECTOR_SIZE;
    write_protected = state->write_protected;
    disk_changed = state->disk_changed;
}
