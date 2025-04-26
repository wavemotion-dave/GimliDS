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
 *  SID.h - 6581 emulation
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

#ifndef _SID_H
#define _SID_H
#include <nds.h>
#include <stdlib.h>


// Define this if you want an emulation of an 8580
// (affects combined waveforms)
#undef EMUL_MOS8580

#define SID_CYCLES_PER_LINE 63

extern uint8 regs[32];
extern uint8 last_sid_byte;
extern int16_t EGDivTable[16];    // Clock divisors for A/D/R settings
extern uint8_t EGDRShift[256];    // For exponential approximation of D/R

class Prefs;
class C64;
class SIDRenderer;
struct MOS6581State;

// Class for administrative functions
class MOS6581 {
public:
    MOS6581(C64 *c64);
    ~MOS6581();

    void Reset(void);
    uint8 ReadRegister(uint16 adr);
    void WriteRegister(uint16 adr, uint8 byte);
    void NewPrefs(Prefs *prefs);
    void PauseSound(void);
    void ResumeSound(void);
    void GetState(MOS6581State *ss);
    void SetState(MOS6581State *ss);
    void EmulateLine(void);
private:
    void open_close_renderer(int old_type, int new_type);

    C64 *the_c64;               // Pointer to C64 object
    SIDRenderer *the_renderer;  // Pointer to current renderer
    uint32_t fake_v3_count;		// Fake voice 3 phase accumulator for oscillator read-back
    int32_t fake_v3_eg_level;	// Fake voice 3 EG level (8.16 fixed) for EG read-back
    int16   fake_v3_eg_state;	// Fake voice 3 EG state

    uint8_t read_osc3() const;
    uint8_t read_env3() const;
};

// EG states
 enum {
 	EG_ATTACK,
 	EG_DECAY_SUSTAIN,
 	EG_RELEASE
 };


// Renderers do the actual audio data processing
class SIDRenderer {
public:
    virtual ~SIDRenderer() {}
    virtual void Reset(void)=0;
    virtual void EmulateLine(void)=0;
    virtual void WriteRegister(uint16 adr, uint8 byte)=0;
    virtual void NewPrefs(Prefs *prefs)=0;
    virtual void Pause(void)=0;
    virtual void Resume(void)=0;
};


// SID state
struct MOS6581State {
    uint8 freq_lo_1;
    uint8 freq_hi_1;
    uint8 pw_lo_1;
    uint8 pw_hi_1;
    uint8 ctrl_1;
    uint8 AD_1;
    uint8 SR_1;

    uint8 freq_lo_2;
    uint8 freq_hi_2;
    uint8 pw_lo_2;
    uint8 pw_hi_2;
    uint8 ctrl_2;
    uint8 AD_2;
    uint8 SR_2;

    uint8 freq_lo_3;
    uint8 freq_hi_3;
    uint8 pw_lo_3;
    uint8 pw_hi_3;
    uint8 ctrl_3;
    uint8 AD_3;
    uint8 SR_3;

    uint8 fc_lo;
    uint8 fc_hi;
    uint8 res_filt;
    uint8 mode_vol;

    uint8 pot_x;
    uint8 pot_y;
    uint8 osc_3;
    uint8 env_3;
    
    uint32 v3_count;
    int32_t v3_eg_level;
    int16 v3_eg_state;
    uint32 sid_seed;
};


/*
 * Fill buffer (for Unix sound routines), sample volume (for sampled voice)
 */

inline void MOS6581::EmulateLine(void)
{
	// Simulate voice 3 phase accumulator
 	uint8_t v3_ctrl = regs[0x12];	// Voice 3 control register
 	if (v3_ctrl & 0x08)  			// Test bit
    {
 		fake_v3_count = 0;
 	} 
    else 
    {
 		uint32_t add = (regs[0x0f] << 8) | regs[0x0e];
 		fake_v3_count = (fake_v3_count + add * 63) & 0xffffff;
 	}

    // Simulate voice 3 envelope generator
 	switch (fake_v3_eg_state) 
    {
 		case EG_ATTACK:
 			fake_v3_eg_level +=  (SID_CYCLES_PER_LINE << 16) / EGDivTable[regs[0x13] >> 4];
 			if (fake_v3_eg_level > 0xffffff)
            {
 				fake_v3_eg_level = 0xffffff;
 				fake_v3_eg_state = EG_DECAY_SUSTAIN;
 			}
 			break;
 		case EG_DECAY_SUSTAIN: 
        {
 			int32_t s_level = (regs[0x14] >> 4) * 0x111111;
 			fake_v3_eg_level -= ((SID_CYCLES_PER_LINE << 16) / EGDivTable[regs[0x13] & 0x0f]) >> EGDRShift[fake_v3_eg_level >> 16];
 			if (fake_v3_eg_level < s_level) 
            {
 				fake_v3_eg_level = s_level;
 			}
 			break;
 		}
 		case EG_RELEASE:
 			if (fake_v3_eg_level != 0) 
            {
 				fake_v3_eg_level -= ((SID_CYCLES_PER_LINE << 16) / EGDivTable[regs[0x14] & 0x0f]) >> EGDRShift[fake_v3_eg_level >> 16];
 				if (fake_v3_eg_level < 0) 
                {
 					fake_v3_eg_level = 0;
 				}
 			}
 			break;
 	}
    
    if (the_renderer != NULL)
    {
        the_renderer->EmulateLine();
    }
}


/*
 *  Read from register
 */

inline uint8 MOS6581::ReadRegister(uint16 adr)
{
    // A/D converters
    if (adr == 0x19 || adr == 0x1a) {
        last_sid_byte = 0;
        return 0xff;
    }

    // Voice 3 oscillator
    else if (adr == 0x1b) {
        last_sid_byte = 0;
        return read_osc3();
    }

    // Voice 3 Envelope
    else if (adr == 0x1c) {
        last_sid_byte = 0;
        return read_env3();
    }

    // Write-only register: Return last value written to SID
    return last_sid_byte;
}


/*
 *  Write to register
 */

inline void MOS6581::WriteRegister(uint16 adr, uint8 byte)
{
	// Handle fake voice 3 EG state
 	if (adr == 0x12) {	// Voice 3 control register
 		uint8_t gate = byte & 0x01;
 		if ((regs[0x12] & 0x01) != gate) {
 			if (gate) {		// Gate turned on
 				fake_v3_eg_state = EG_ATTACK;
 			} else {		// Gate turned off
 				fake_v3_eg_state = EG_RELEASE;
 			}
 		}
 	}    
    
    // Keep a local copy of the register values
    last_sid_byte = regs[adr] = byte;

    if (the_renderer != NULL)
        the_renderer->WriteRegister(adr, byte);
}

#endif
