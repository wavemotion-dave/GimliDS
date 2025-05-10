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
 *  - This is only used for processor-level 1541 emulation. It simulates the
 *    1541 disk controller hardware (R/W head, GCR reading/writing).
 *  - The preferences settings for drive 8 are used to specify the disk
 *    image file.
 *
 * Incompatibilities:
 * ------------------
 *
 *  - No GCR writing implemented (WriteSector is a ROM patch).
 *  - GCR disk images must be byte-aligned.
 *  - Programs depending on the exact timing of head movement or doing
 *    bit rate and motor speed tricks don't work.
 */

#include "sysdeps.h"

#include "1541gcr.h"
#include "CPU1541.h"
#include "IEC.h"
#include "Prefs.h"


// Size of standard GCR sector encoded from D64 image
constexpr unsigned GCR_SECTOR_SIZE = 5 + 10 + 9 + 5 + 325 + 12;	// SYNC + Header + Gap + SYNC + Data + Gap

// Duration of disk change sequence step in cycles
constexpr unsigned DISK_CHANGE_SEQ_CYCLES = 500000;	// 0.5 s


// Number of sectors of each track
const unsigned num_sectors[41] = {
	0,
	21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
	19,19,19,19,19,19,19,
	18,18,18,18,18,18,
	17,17,17,17,17,
	17,17,17,17,17		// Tracks 36..40
};

// Sector offset of start of track in D64 file
const unsigned sector_offset[41] = {
	0,
	0,21,42,63,84,105,126,147,168,189,210,231,252,273,294,315,336,
	357,376,395,414,433,452,471,
	490,508,526,544,562,580,
	598,615,632,649,666,
	683,700,717,734,751	// Tracks 36..40
};


/*
 *  Constructor: Open image file if processor-level 1541 emulation is enabled
 */

Job1541::Job1541(uint8_t *ram1541) : ram(ram1541), the_file(nullptr)
{
	num_tracks = 0;
	header_size = 0;

	current_halftrack = 0;
	gcr_offset = 0;

	disk_change_cycle = 0;
	disk_change_seq = 0;

	cycles_per_byte = 30;
	last_byte_cycle = 0;
	byte_latch = 0;

	motor_on = false;
	write_protected = false;
	on_sync = false;
	byte_ready = false;

	for (unsigned i = 0; i < MAX_NUM_HALFTRACKS; ++i) {
		gcr_data[i] = nullptr;
		gcr_track_length[i] = 0;
	}

	if (ThePrefs.TrueDrive) {
		open_image_file(ThePrefs.DrivePath[0]);
	}
}


/*
 *  Destructor: Close disk image file
 */

Job1541::~Job1541()
{
	close_image_file();
}


/*
 *  Reset GCR emulation
 */

void Job1541::Reset()
{
	current_halftrack = 2 * (18 - 1);	// Track 18
	gcr_offset = 0;

	disk_change_seq = 0;

	motor_on = false;
	on_sync = false;
	byte_ready = false;
}


/*
 *  Preferences may have changed
 */

void Job1541::NewPrefs(const Prefs * prefs)
{
	// 1541 emulation turned off?
	if (!prefs->TrueDrive) {
		close_image_file();

	// 1541 emulation turned on?
	} else if (!ThePrefs.TrueDrive && prefs->TrueDrive) {
		open_image_file(prefs->DrivePath[0]);

	// Image file name changed?
	}
    else if (strcmp(ThePrefs.DrivePath[0], prefs->DrivePath[0]))
    {
		close_image_file();
		open_image_file(prefs->DrivePath[0]);

		disk_change_cycle = the_cpu->CycleCounter();
		disk_change_seq = 3;	// Start disk change WP sensor sequence

		the_cpu->Idle = false;	// Wake up CPU
	}
}


/*
 *  Check whether file with given header (64 bytes) and size looks like a GCR
 *  disk image file
 */

bool IsGCRImageFile(const std::string & path, const uint8_t *header, long size)
{
	return memcmp(header, "GCR-1541\0", 9) == 0;
}


/*
 *  Open disk image file
 */

void Job1541::open_image_file(const std::string & filepath)
{
	// WP sensor open
	write_protected = false;

	// Check file type
	int type = FILE_IMAGE;
	if (type != FILE_IMAGE)
		return;

	// Try opening the file for reading/writing first, then for reading only
	bool read_only = false;
	the_file = fopen(filepath.c_str(), "rb+");
	if (the_file == nullptr) {
		read_only = true;
		the_file = fopen(filepath.c_str(), "rb");
	}

	if (the_file == nullptr)
		return;

	// Load image file
	bool ok = false;
    ok = load_image_file();

	if (ok) {
		// Set write protect status
		write_protected = read_only;
	} else {
		fclose(the_file);
		the_file = nullptr;
	}
}


/*
 *  Close disk image file
 */

void Job1541::close_image_file()
{
	// Deallocate GCR data
	for (unsigned i = 0; i < MAX_NUM_HALFTRACKS; ++i) {
		delete[] gcr_data[i];
		gcr_data[i] = nullptr;
		gcr_track_length[i] = 0;
	}

	// Close file
	if (the_file != nullptr) {
		fclose(the_file);
		the_file = nullptr;
	}

	// WP sensor open
	write_protected = false;
}


/*
 *  Load D64/x64 disk image file
 */

bool Job1541::load_image_file()
{
	bool has_error_info = false;

	// Check length
	fseek(the_file, 0, SEEK_END);
	size_t size = ftell(the_file);
	fseek(the_file, 0, SEEK_SET);

	if (size == NUM_SECTORS_35 * 256) {

		// 35-track D64
		num_tracks = 35;
		header_size = 0;

	} else if (size == NUM_SECTORS_35 * 257) {

		// 35-track D64 with error info
		num_tracks = 35;
		header_size = 0;
		has_error_info = true;

	} else if (size == NUM_SECTORS_40 * 256) {

		// 40-track D64
		num_tracks = 40;
		header_size = 0;

	} else if (size == NUM_SECTORS_40 * 257) {

		// 40-track D64 with error info
		num_tracks = 40;
		header_size = 0;
		has_error_info = true;

	} else {

		// Check for x64 header
		uint8_t header[64];
		memset(header, 0, sizeof(header));
		fread(header, sizeof(header), 1, the_file);

		if (memcmp(header, "C\x15\x41\x64\x01\x02", 6) == 0) {
			num_tracks = header[7];
			header_size = 64;

			if (num_tracks > 40) {
				num_tracks = 0;
			}
		}
	}

	if (num_tracks == 0) {
		return false;
	}

	// Preset error info (all sectors no error)
	memset(error_info, 1, sizeof(error_info));

	// Load sector error info from D64 file, if present
	if (has_error_info) {
		unsigned num_sectors = (num_tracks == 40) ? NUM_SECTORS_40 : NUM_SECTORS_35;
		fseek(the_file, num_sectors * 256, SEEK_SET);
		fread(&error_info, num_sectors, 1, the_file);
	};

	// Read BAM and get disk ID
	uint8_t bam[256];
	read_sector(18, 0, bam);
	disk_id1 = bam[162];
	disk_id2 = bam[163];

	// Create GCR encoded disk data from image
	for (unsigned track = 1; track <= num_tracks; ++track) {

		// Allocate GCR data
		unsigned halftrack = (track - 1) * 2;

		gcr_track_length[halftrack] = GCR_SECTOR_SIZE * num_sectors[track];
		gcr_data[halftrack] = new uint8_t[gcr_track_length[halftrack]];

		// Convert track
		for (unsigned sector = 0; sector < num_sectors[track]; ++sector) {
			sector2gcr(track, sector, gcr_data[halftrack] + GCR_SECTOR_SIZE * sector);
		}
	}

	return true;
}


/*
 *  Load G64 disk image file
 */

bool Job1541::load_gcr_file()
{
	// Read header
	uint8_t header[12];
	fread(header, sizeof(header), 1, the_file);

	unsigned num_halftracks = header[9];
	if (num_halftracks > MAX_NUM_HALFTRACKS)
		return false;

	num_tracks = num_halftracks / 2;
	header_size = 0;	// Not relevant for GCR image

	// Read track offset table
	uint8_t track_offsets[MAX_NUM_HALFTRACKS * 4];
	memset(track_offsets, 0, sizeof(track_offsets));
	fread(track_offsets, num_halftracks * 4, 1, the_file);

	// Read GCR data from file
	for (unsigned halftrack = 0; halftrack < num_halftracks; ++halftrack) {
		uint32_t offset = ((uint32_t) track_offsets[halftrack * 4 + 0] <<  0)
		                | ((uint32_t) track_offsets[halftrack * 4 + 1] <<  8)
		                | ((uint32_t) track_offsets[halftrack * 4 + 2] << 16)
		                | ((uint32_t) track_offsets[halftrack * 4 + 3] << 24);

		if (offset == 0)
			continue;

		uint8_t len[2] = { 0, 0 };
		fseek(the_file, offset, SEEK_SET);
		fread(len, sizeof(len), 1, the_file);

		uint16_t length = len[0] | (len[1] << 8);

		gcr_track_length[halftrack] = length;
		gcr_data[halftrack] = new uint8_t[length];
		fread(gcr_data[halftrack], length, 1, the_file);
	}

	return true;
}


/*
 *  Write sector to disk (1541 ROM patch)
 */

void Job1541::WriteSector()
{
    floppy_soundfx(0);   // Play floppy SFX if needed
    
	unsigned track = ram[0x18];
	unsigned halftrack = (track - 1) * 2;
	unsigned sector = ram[0x19];
	uint16_t buf = ram[0x30] | (ram[0x31] << 8);

	if (buf <= 0x0700) {
		if (write_sector(track, sector, ram + buf)) {
			sector2gcr(track, sector, gcr_data[halftrack] + GCR_SECTOR_SIZE * sector);
		}
	}
}


/*
 *  Format one track (1541 ROM patch)
 */

void Job1541::FormatTrack()
{
	unsigned track = ram[0x51];
	unsigned halftrack = (track - 1) * 2;

	// Get new ID
	uint8_t bufnum = ram[0x3d];
	disk_id1 = ram[0x12 + bufnum];
	disk_id2 = ram[0x13 + bufnum];

	// Create empty block
	uint8_t buf[256];
	memset(buf, 1, 256);
	buf[0] = 0x4b;

	// Write block to all sectors on track
	for (unsigned sector = 0; sector < num_sectors[track]; ++sector) {
		if (write_sector(track, sector, buf)) {
			sector2gcr(track, sector, gcr_data[halftrack] + GCR_SECTOR_SIZE * sector);
		}
	}

	// Clear error info (all sectors no error)
	if (track == 35) {
		memset(error_info, 1, sizeof(error_info));
		// Write error_info to disk?
	}
}


/*
 *  Read sector (256 bytes) from image file, return DOS error code (ERR_*)
 */

int Job1541::read_sector(unsigned track, unsigned sector, uint8_t *buffer)
{
	if (the_file == nullptr)
		return ERR_NOTREADY;

    floppy_soundfx(0);   // Play floppy SFX if needed
    
	// Convert track/sector to byte offset in file
	int offset = offset_from_ts(track, sector);
	if (offset < 0)
		return ERR_ILLEGALTS;

	fseek(the_file, offset + header_size, SEEK_SET);
	if (fread(buffer, 1, 256, the_file) != 256) {
		return ERR_READ22;
	} else {
		uint8_t error = error_info[sector_offset[track] + sector];
        int get_job_error(int error);
        return get_job_error(error);
	}
}


/*
 *  Write sector (256 bytes) to image file
 *  true: success, false: error
 */

bool Job1541::write_sector(unsigned track, unsigned sector, const uint8_t *buffer)
{
	if (the_file == nullptr || write_protected)
		return false;

    floppy_soundfx(1);   // Play floppy SFX if needed
    
	// Convert track/sector to byte offset in image file
	int offset = offset_from_ts(track, sector);
	if (offset < 0)
		return false;

	fseek(the_file, offset + header_size, SEEK_SET);
	fwrite(buffer, 1, 256, the_file);
	return true;
}


/*
 *  Convert track/sector to offset
 */

int Job1541::offset_from_ts(unsigned track, unsigned sector)
{
	if ((track < 1) || (track > num_tracks) ||
		(sector < 0) || (sector >= num_sectors[track]))
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

void Job1541::gcr_conv4(const uint8_t * from, uint8_t * to)
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

void Job1541::sector2gcr(unsigned track, unsigned sector, uint8_t * gcr)
{
	uint8_t block[256];
	uint8_t buf[4];

	int error = read_sector(track, sector, block);

	uint8_t id1 = disk_id1;
	uint8_t id2 = disk_id2;
	if (error == ERR_DISKID) {		// Disk ID mismatch
		id1 ^= 0xff;
		id2 ^= 0xff;
	}

	// Create GCR block header
	memset(gcr, 0xff, 5);					// SYNC
	gcr += 5;

	buf[0] = 0x08;							// Header mark
	buf[1] = sector ^ track ^ id2 ^ id1;	// Checksum
	buf[2] = sector;
	buf[3] = track;
	if (error == ERR_READ20) {		// Block header not found
		buf[0] ^= 0xff;
	}
	if (error == ERR_READ27) {		// Checksum error in header
		buf[1] ^= 0xff;
	}
	gcr_conv4(buf, gcr);
	gcr += 5;

	buf[0] = id2;
	buf[1] = id1;
	buf[2] = 0x0f;
	buf[3] = 0x0f;
	gcr_conv4(buf, gcr);
	gcr += 5;

	memset(gcr, 0x55, 9);					// Gap
	gcr += 9;

	// Create GCR data block
	memset(gcr, 0xff, 5);					// SYNC
	gcr += 5;

	uint8_t sum;
	buf[0] = 0x07;							// Data mark
	sum =  buf[1] = block[0];
	sum ^= buf[2] = block[1];
	sum ^= buf[3] = block[2];
	if (error == ERR_READ22) {		// Data block not present
		buf[0] ^= 0xff;
	}
	gcr_conv4(buf, gcr);
	gcr += 5;

	for (unsigned i = 3; i < 255; i += 4) {
		sum ^= buf[0] = block[i];
		sum ^= buf[1] = block[i+1];
		sum ^= buf[2] = block[i+2];
		sum ^= buf[3] = block[i+3];
		gcr_conv4(buf, gcr);
		gcr += 5;
	}

	sum ^= buf[0] = block[255];
	buf[1] = sum;							// Checksum
	buf[2] = 0;
	buf[3] = 0;
	if (error == ERR_READ23) {		// Checksum error in data block
		buf[1] ^= 0xff;
	}
	gcr_conv4(buf, gcr);
	gcr += 5;

	memset(gcr, 0x55, 12);					// Gap
}


/*
 *  Set read/write bit rate
 */

void Job1541::SetBitRate(uint8_t rate)
{
	static const unsigned cpb[4] = { 32, 30, 28, 26 };
	cycles_per_byte = cpb[rate];
}


/*
 *  Move R/W head out (lower track numbers)
 */

void Job1541::MoveHeadOut()
{
	if (!motor_on)	// Stepper is inhibited if spindle motor is off
		return;
	if (current_halftrack == 0)
		return;

	--current_halftrack;
}


/*
 *  Move R/W head in (higher track numbers)
 */

void Job1541::MoveHeadIn()
{
	if (!motor_on)	// Stepper is inhibited if spindle motor is off
		return;
	if (current_halftrack >= MAX_NUM_HALFTRACKS - 1)
		return;

	++current_halftrack;
}


/*
 *  Get state
 */

void Job1541::GetState(Job1541State * s) const
{
	s->current_halftrack = current_halftrack;
	s->gcr_offset = gcr_offset;

	s->cycles_per_byte = cycles_per_byte;
	s->last_byte_cycle = last_byte_cycle;
	s->disk_change_cycle = disk_change_cycle;

	s->byte_latch = byte_latch;
	s->disk_change_seq = disk_change_seq;

	s->motor_on = motor_on;
	s->write_protected = write_protected;
	s->on_sync = on_sync;
	s->byte_ready = byte_ready;
}


/*
 *  Set state
 */

void Job1541::SetState(const Job1541State * s)
{
	current_halftrack = s->current_halftrack;
	gcr_offset = s->gcr_offset;

	cycles_per_byte = s->cycles_per_byte;
	last_byte_cycle = s->last_byte_cycle;
	disk_change_cycle = s->disk_change_cycle;

	byte_latch = s->byte_latch;
	disk_change_seq = s->disk_change_seq;

	motor_on = s->motor_on;
	write_protected = s->write_protected;
	on_sync = s->on_sync;
	byte_ready = s->byte_ready;
}


/*
 *  Advance disk change sequence state
 */

void Job1541::advance_disk_change_seq(uint32_t cycle_counter)
{
	if (disk_change_seq > 0) {

		// Time for next step in sequence?
		uint32_t elapsed = cycle_counter - disk_change_cycle;
		if (elapsed >= DISK_CHANGE_SEQ_CYCLES) {
			--disk_change_seq;
			disk_change_cycle = cycle_counter;
		}
	}
}


/*
 *  Rotate disk (virtually)
 */

void Job1541::rotate_disk(uint32_t cycle_counter)
{
	advance_disk_change_seq(cycle_counter);

	if (motor_on && disk_change_seq == 0 && gcr_data[current_halftrack] != nullptr) {

		uint32_t elapsed = cycle_counter - last_byte_cycle;
		uint32_t advance = elapsed / cycles_per_byte;

		if (advance > 0) {
			size_t track_length = gcr_track_length[current_halftrack];

			gcr_offset += advance;
			while (gcr_offset >= track_length) {
				gcr_offset -= track_length;
			}

			const uint8_t * p = gcr_data[current_halftrack] + gcr_offset;

			// Sync = ten "1" bits
			if (gcr_offset != 0) {
				on_sync = ((p[-1] & 0x03) == 0x03) && (p[0] == 0xff);
			} else {
				on_sync = ((gcr_data[current_halftrack][track_length - 1] & 0x03) == 0x03) && (p[0] == 0xff);
			}

			// Byte is ready if not on sync
			if (! on_sync) {
				if (! byte_ready) {
					byte_latch = p[0];
					byte_ready = true;
				}
			} else {
				byte_ready = false;
			}

			last_byte_cycle += advance * cycles_per_byte;
		}

	} else {

		last_byte_cycle = cycle_counter;
		on_sync = false;
		byte_ready = false;
	}
}


/*
 *  Check if R/W head is over SYNC
 */

bool Job1541::SyncFound(uint32_t cycle_counter)
{
	rotate_disk(cycle_counter);

	return on_sync;
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


/*
 *  Return state of write protect sensor
 */

bool Job1541::WPSensorClosed(uint32_t cycle_counter)
{
	advance_disk_change_seq(cycle_counter);
#if 0 //TODO: figure out why this isn't working...
	if (disk_change_seq == 3 || disk_change_seq == 1) {
		return true;
	} else if (disk_change_seq == 2) {
		return false;
	}
#endif
	// Default behavior
	return write_protected;
}
