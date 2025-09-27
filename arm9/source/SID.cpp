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
 *  SID.cpp - 6581 emulation
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
 * Incompatibilities:
 * ------------------
 *
 *  - Lots of empirically determined constants in the filter calculations
 */

#include "sysdeps.h"
#include "VIC.h"
#include <nds.h>
#include <maxmod9.h>
#include "soundbank.h"
#include "soundbank_bin.h"

#include <stdio.h>
#include <math.h>

#include "SID.h"
#include "Prefs.h"

#define FIXPOINT_PREC           16    // number of fractional bits used in fixpoint representation
#define PRECOMPUTE_RESONANCE          // For a bit of added speed
#define ldSINTAB                9     // size of sinus table (0 to 90 degrees)

#include "FixPoint.h"

uint8 regs[32]                __attribute__((section(".dtcm")));  // Copies of the 32 write-only SID registers
uint8 last_sid_byte           __attribute__((section(".dtcm")));  // Last value written to SID
uint32_t sid_random_seed      __attribute__((section(".dtcm")));  // Random seed - global so it's deterministic


/*
 *  Resonance frequency polynomials
 */

#define CALC_RESONANCE_LP(f) (227.755 - 1.7635 * f - 0.0176385 * f * f + 0.00333484 * f * f * f)
#define CALC_RESONANCE_HP(f) (366.374 - 14.0052 * f + 0.603212 * f * f - 0.000880196 * f * f * f)

/*
 *  Random number generator for noise waveform
 */
static uint8 sid_random(void)
{
    sid_random_seed = sid_random_seed * 1103515245 + 12345;
    return sid_random_seed >> 16;
}


/*
 *  Constructor
 */

MOS6581::MOS6581(C64 *c64) : the_c64(c64)
{
    the_renderer = NULL;
    for (int i=0; i<32; i++)
        regs[i] = 0;

    // Open the renderer
    open_close_renderer(SIDTYPE_NONE, SIDTYPE_DIGITAL);
}


/*
 *  Destructor
 */

MOS6581::~MOS6581()
{
    // Close the renderer
    open_close_renderer(SIDTYPE_DIGITAL, SIDTYPE_NONE);
}


/*
 *  Reset the SID
 */

void MOS6581::Reset(void)
{
    for (int i=0; i<32; i++)
    {
        regs[i] = 0;
    }
    last_sid_byte = 0;
    fake_v3_count = 0x555555;
    sid_random_seed = 1;

    // Reset the renderer
    if (the_renderer != NULL)
    {
        the_renderer->Reset();
    }
}


/*
 *  Preferences may have changed
 */

void MOS6581::NewPrefs(Prefs *prefs)
{
    open_close_renderer(SIDTYPE_DIGITAL, SIDTYPE_DIGITAL);
    if (the_renderer != NULL)
    {
        the_renderer->NewPrefs(prefs);
    }
}


/*
 *  Pause sound output
 */

void MOS6581::PauseSound(void)
{
    if (the_renderer != NULL)
    {
        the_renderer->Pause();
    }
}


/*
 *  Resume sound output
 */

void MOS6581::ResumeSound(void)
{
    if (the_renderer != NULL)
    {
        the_renderer->Resume();
    }
}


/*
 *  Get SID state
 */

void MOS6581::GetState(MOS6581State *ss)
{
    ss->freq_lo_1 = regs[0];
    ss->freq_hi_1 = regs[1];
    ss->pw_lo_1 = regs[2];
    ss->pw_hi_1 = regs[3];
    ss->ctrl_1 = regs[4];
    ss->AD_1 = regs[5];
    ss->SR_1 = regs[6];

    ss->freq_lo_2 = regs[7];
    ss->freq_hi_2 = regs[8];
    ss->pw_lo_2 = regs[9];
    ss->pw_hi_2 = regs[10];
    ss->ctrl_2 = regs[11];
    ss->AD_2 = regs[12];
    ss->SR_2 = regs[13];

    ss->freq_lo_3 = regs[14];
    ss->freq_hi_3 = regs[15];
    ss->pw_lo_3 = regs[16];
    ss->pw_hi_3 = regs[17];
    ss->ctrl_3 = regs[18];
    ss->AD_3 = regs[19];
    ss->SR_3 = regs[20];

    ss->fc_lo = regs[21];
    ss->fc_hi = regs[22];
    ss->res_filt = regs[23];
    ss->mode_vol = regs[24];

    ss->pot_x = 0xff;
    ss->pot_y = 0xff;
    ss->osc_3 = 0;
    ss->env_3 = 0;

    ss->v3_count    = fake_v3_count;
    ss->v3_eg_level = fake_v3_eg_level;
    ss->v3_eg_state = fake_v3_eg_state;
    ss->sid_seed    = sid_random_seed;
    
    ss->spare1 = 0;
    ss->spare2 = 0;
    ss->spare3 = 0;
    ss->spare4 = 0;
}


/*
 *  Restore SID state
 */

void MOS6581::SetState(MOS6581State *ss)
{
    regs[0] = ss->freq_lo_1;
    regs[1] = ss->freq_hi_1;
    regs[2] = ss->pw_lo_1;
    regs[3] = ss->pw_hi_1;
    regs[4] = ss->ctrl_1;
    regs[5] = ss->AD_1;
    regs[6] = ss->SR_1;

    regs[7] = ss->freq_lo_2;
    regs[8] = ss->freq_hi_2;
    regs[9] = ss->pw_lo_2;
    regs[10] = ss->pw_hi_2;
    regs[11] = ss->ctrl_2;
    regs[12] = ss->AD_2;
    regs[13] = ss->SR_2;

    regs[14] = ss->freq_lo_3;
    regs[15] = ss->freq_hi_3;
    regs[16] = ss->pw_lo_3;
    regs[17] = ss->pw_hi_3;
    regs[18] = ss->ctrl_3;
    regs[19] = ss->AD_3;
    regs[20] = ss->SR_3;

    regs[21] = ss->fc_lo;
    regs[22] = ss->fc_hi;
    regs[23] = ss->res_filt;
    regs[24] = ss->mode_vol;

    fake_v3_count = ss->v3_count;
    fake_v3_eg_level = ss->v3_eg_level;
    fake_v3_eg_state = ss->v3_eg_state;
    sid_random_seed = ss->sid_seed;

    // Stuff the new register values into the renderer
    if (the_renderer != NULL)
        for (int i=0; i<25; i++)
            the_renderer->WriteRegister(i, regs[i]);
}


/**
 **  Renderer for digital SID emulation (SIDTYPE_DIGITAL)
 **/

const uint32 SAMPLE_FREQ        = 15600;                                  // NDS Sample Rate - 50 frames x 312 scanlines = 15600 samples per second - normal sample rate for older DS hardware
const uint32 SID_FREQ           = 985248;                                 // SID frequency in Hz
const uint32 SID_CYCLES_FIX     = ((SID_FREQ << 11)/SAMPLE_FREQ)<<5;      // # of SID clocks per sample frame * 65536

const uint32 SAMPLE_FREQ_DSI    = 2*15600;                                // NDS Sample Rate - 50 frames x 312 scanlines = 15600 samples per second - doubled sample rate
const uint32 SID_CYCLES_FIX_DSI = ((SID_FREQ << 11)/SAMPLE_FREQ_DSI)<<5;  // # of SID clocks per sample frame * 65536

const int SAMPLE_BUF_SIZE       = 0x138*4;                                // Size of buffer for sampled voice (double buffered)

uint8 sample_vol_filt[SAMPLE_BUF_SIZE] __attribute__((section(".dtcm"))); // Buffer for sampled volumes and filter bits shifted up
int sample_in_ptr                      __attribute__((section(".dtcm"))); // Index in sample_vol_filt[] for writing

// SID waveforms (some of them :-)
enum {
    WAVE_NONE,
    WAVE_TRI,
    WAVE_SAW,
    WAVE_TRISAW,
    WAVE_RECT,
    WAVE_TRIRECT,
    WAVE_SAWRECT,
    WAVE_TRISAWRECT,
    WAVE_NOISE
};


// Filter types
enum {
    FILT_NONE,
    FILT_LP,
    FILT_BP,
    FILT_LPBP,
    FILT_HP,
    FILT_NOTCH,
    FILT_HPBP,
    FILT_ALL
};

// Structure for one voice
struct DRVoice {
    int wave;        // Selected waveform
    int eg_state;    // Current state of EG
    DRVoice *mod_by; // Voice that modulates this one
    DRVoice *mod_to; // Voice that is modulated by this one

    uint32 count;   // Counter for waveform generator, 8.16 fixed
    uint32 add;     // Added to counter in every frame

    uint16 freq;    // SID frequency value
    uint16 pw;      // SID pulse-width value

    int32 a_add;    // EG parameters
    int32 d_sub;
    int32 s_level;
    int32 r_sub;
    int32 eg_level;    // Current EG level, 8.16 fixed

    uint32 noise;   // Last noise generator output value

    bool gate;      // EG gate bit
    bool ring;      // Ring modulation bit
    bool test;      // Test bit
    
                    // The following bit is set for the modulating
                    // voice, not for the modulated one (as the SID bits)
    bool sync;      // Sync modulation bit
    bool mute;      // Voice muted (voice 3 only)
};

DRVoice voice[3] __attribute__((section(".dtcm"))); // Data for 3 voices

// Renderer class
class DigitalRenderer : public SIDRenderer {
public:

    DigitalRenderer();
    virtual ~DigitalRenderer();

    virtual void Reset(void);
    virtual void EmulateLine(void);
    virtual void WriteRegister(uint16 adr, uint8 byte);
    virtual void NewPrefs(Prefs *prefs);
    virtual void Pause(void);
    virtual void Resume(void);
    int16 calc_buffer(int16 *buf, long count);
    //bool ready;                     // Flag: Renderer has initialized and is ready
private:
    void init_sound(void);
    void calc_filter(void);
    uint8 volume;                   // Master volume
    uint8_t res_filt;				// RES/FILT register

    static const uint16 TriSawTable[0x100];
    static const uint16 TriRectTable[0x100];
    static const uint16 SawRectTable[0x100];
    static const uint16 TriSawRectTable[0x100];
    static const int32_t EGTable[16];      // Increment/decrement values for all A/D/R settings
    static const int32_t EGTableDSi[16];   // Increment/decrement values for all A/D/R settings

    uint8 f_type;                   // Filter type
    uint8 f_freq;                   // SID filter frequency (upper 8 bits)
    uint8 f_freq_low;               // SID filter frequency (lower 4 bits)
    uint8 f_res;                    // Filter resonance (0..15)
    FixPoint f_ampl;
    FixPoint d1, d2, g1, g2;
    int32 xn1, xn2, yn1, yn2;       // can become very large
    FixPoint sidquot;
#ifdef PRECOMPUTE_RESONANCE
    FixPoint resonanceLP[257];
    FixPoint resonanceHP[257];
#endif
};


// Sampled from a 6581R4
const uint16 DigitalRenderer::TriSawTable[0x100] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0808,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1010, 0x3C3C,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0808,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1010, 0x3C3C
};

const uint16 DigitalRenderer::TriRectTable[0x100] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x8080,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x8080,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x8080, 0xC0C0,
    0x0000, 0x8080, 0x8080, 0xE0E0, 0x8080, 0xE0E0, 0xF0F0, 0xFCFC,
    0xFFFF, 0xFCFC, 0xFAFA, 0xF0F0, 0xF6F6, 0xE0E0, 0xE0E0, 0x8080,
    0xEEEE, 0xE0E0, 0xE0E0, 0x8080, 0xC0C0, 0x0000, 0x0000, 0x0000,
    0xDEDE, 0xC0C0, 0xC0C0, 0x0000, 0x8080, 0x0000, 0x0000, 0x0000,
    0x8080, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0xBEBE, 0x8080, 0x8080, 0x0000, 0x8080, 0x0000, 0x0000, 0x0000,
    0x8080, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x7E7E, 0x4040, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

const uint16 DigitalRenderer::SawRectTable[0x100] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x7878,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x7878
};

const uint16 DigitalRenderer::TriSawRectTable[0x100] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

int16_t EGDivTable[16] __attribute__((section(".dtcm"))) = {
    9, 32,
    63, 95,
    149, 220,
    267, 313,
    392, 977,
    1954, 3126,
    3906, 11720,
    19531, 31251
};

 const int32_t DigitalRenderer::EGTable[16] = {
    SID_CYCLES_FIX / 9,     SID_CYCLES_FIX / 32,
    SID_CYCLES_FIX / 63,    SID_CYCLES_FIX / 95,
    SID_CYCLES_FIX / 149,   SID_CYCLES_FIX / 220,
    SID_CYCLES_FIX / 267,   SID_CYCLES_FIX / 313,
    SID_CYCLES_FIX / 392,   SID_CYCLES_FIX / 977,
    SID_CYCLES_FIX / 1954,  SID_CYCLES_FIX / 3126,
    SID_CYCLES_FIX / 3906,  SID_CYCLES_FIX / 11720,
    SID_CYCLES_FIX / 19531, SID_CYCLES_FIX / 31251
};

 const int32_t DigitalRenderer::EGTableDSi[16] = {
    SID_CYCLES_FIX_DSI / 9,     SID_CYCLES_FIX_DSI / 32,
    SID_CYCLES_FIX_DSI / 63,    SID_CYCLES_FIX_DSI / 95,
    SID_CYCLES_FIX_DSI / 149,   SID_CYCLES_FIX_DSI / 220,
    SID_CYCLES_FIX_DSI / 267,   SID_CYCLES_FIX_DSI / 313,
    SID_CYCLES_FIX_DSI / 392,   SID_CYCLES_FIX_DSI / 977,
    SID_CYCLES_FIX_DSI / 1954,  SID_CYCLES_FIX_DSI / 3126,
    SID_CYCLES_FIX_DSI / 3906,  SID_CYCLES_FIX_DSI / 11720,
    SID_CYCLES_FIX_DSI / 19531, SID_CYCLES_FIX_DSI / 31251
};


uint8_t EGDRShift[256] __attribute__((section(".dtcm"))) = {
    5,5,5,5,5,5,5,5,4,4,4,4,4,4,4,4,
    3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

/*
 *  Constructor
 */

DigitalRenderer::DigitalRenderer()
{
    // Link voices together
    voice[0].mod_by = &voice[2];
    voice[1].mod_by = &voice[0];
    voice[2].mod_by = &voice[1];
    voice[0].mod_to = &voice[1];
    voice[1].mod_to = &voice[2];
    voice[2].mod_to = &voice[0];

#ifdef PRECOMPUTE_RESONANCE
    // slow floating point doesn't matter much on startup!
    for (int i=0; i<257; i++) {
      resonanceLP[i] = FixNo(CALC_RESONANCE_LP(i));
      resonanceHP[i] = FixNo(CALC_RESONANCE_HP(i));
    }
    // Pre-compute the quotient. No problem since int-part is small enough
    sidquot = (isDSiMode() ? SID_CYCLES_FIX_DSI : SID_CYCLES_FIX);
    // compute lookup table for sin and cos
    InitFixSinTab();
#endif

    Reset();

    // System specific initialization
    init_sound();
}


/*
 *  Reset emulation
 */

void DigitalRenderer::Reset(void)
{
    volume = 0;
    res_filt = 0;

    for (int v=0; v<3; v++) {
        voice[v].wave = WAVE_NONE;
        voice[v].eg_state = EG_RELEASE;
        voice[v].count = 0x555555;
        voice[v].add = 0;
        voice[v].freq = voice[v].pw = 0;
        voice[v].eg_level = voice[v].s_level = 0;
        voice[v].a_add = voice[v].d_sub = voice[v].r_sub = (isDSiMode() ? EGTableDSi[0] : EGTable[0]);
        voice[v].gate = voice[v].ring = voice[v].test = false;
        voice[v].sync = voice[v].mute = false;
    }

    f_type = FILT_NONE;
    f_freq = f_res = 0;
    f_freq_low = 0;
    f_ampl = FixNo(1);
    d1 = d2 = g1 = g2 = 0;
    xn1 = xn2 = yn1 = yn2 = 0;

    sample_in_ptr = 0;
    memset(sample_vol_filt, 0, SAMPLE_BUF_SIZE);
    
    // -------------------------------------------------------------------
    // Copy sawtooth tables to VRAM where they are a bit faster to access
    // -------------------------------------------------------------------
    u16 *ptr1 = (u16*) 0x068A0000;
    u16 *ptr2 = (u16*) 0x068A1000;
    u16 *ptr3 = (u16*) 0x068A2000;
    u16 *ptr4 = (u16*) 0x068A3000;
    
    for (int i=0; i<256; i++)
    {
        ptr1[i] = TriSawTable[i];
        ptr2[i] = TriRectTable[i];
        ptr3[i] = SawRectTable[i];
        ptr4[i] = TriSawRectTable[i];
    }
}


/*
 *  Write to register
 */

void DigitalRenderer::WriteRegister(uint16 adr, uint8 byte)
{
    int v = adr/7;  // Voice number

    switch (adr) {
        case 0:
        case 7:
        case 14:
            voice[v].freq = (voice[v].freq & 0xff00) | byte;
            voice[v].add = sidquot.imul((int)voice[v].freq);
            break;

        case 1:
        case 8:
        case 15:
            voice[v].freq = (voice[v].freq & 0xff) | (byte << 8);
            voice[v].add = sidquot.imul((int)voice[v].freq);
            break;

        case 2:
        case 9:
        case 16:
            voice[v].pw = (voice[v].pw & 0x0f00) | byte;
            break;

        case 3:
        case 10:
        case 17:
            voice[v].pw = (voice[v].pw & 0xff) | ((byte & 0xf) << 8);
            break;

        case 4:
        case 11:
        case 18:
            voice[v].wave = (byte >> 4) & 0xf;
            if ((byte & 1) != voice[v].gate)
            {
                if (byte & 1)   // Gate turned on
                    voice[v].eg_state = EG_ATTACK;
                else            // Gate turned off
                {
                    voice[v].eg_state = EG_RELEASE;
                }
            }
            voice[v].gate = byte & 1;
            voice[v].mod_by->sync = byte & 2;
            voice[v].ring = byte & 4;
            if ((voice[v].test = byte & 8) != 0)
                voice[v].count = 0;
            break;

        case 5:
        case 12:
        case 19:
            voice[v].a_add = (isDSiMode() ? EGTableDSi[byte >> 4] : EGTable[byte >> 4]);
            voice[v].d_sub = (isDSiMode() ? EGTableDSi[byte & 0xf] : EGTable[byte & 0xf]);
            break;

        case 6:
        case 13:
        case 20:
            voice[v].s_level = (byte >> 4) * 0x111111;
            voice[v].r_sub = (isDSiMode() ? EGTableDSi[byte & 0xf] : EGTable[byte & 0xf]);
            break;

        case 21: // Filter Frequency - lower 3 bits
            f_freq_low = ((byte & 0x7) > 3) ? 1:0;
            calc_filter();
            break;

        case 22: // Filter Frequency - upper 8 bits
            f_freq = byte;
            calc_filter();
            break;

        case 23:
            res_filt = byte;
            if ((byte >> 4) != f_res) {
                f_res = byte >> 4;
                calc_filter();
            }
            break;

        case 24:
            volume = byte & 0xf;
            voice[2].mute = byte & 0x80;
            if (((byte >> 4) & 7) != f_type) {
                f_type = (byte >> 4) & 7;
                xn1 = xn2 = yn1 = yn2 = 0;
                calc_filter();
            }
            break;
    }
}


/*
 *  Preferences may have changed
 */

void DigitalRenderer::NewPrefs(Prefs *prefs)
{
    calc_filter();
}


/*
 *  Calculate IIR filter coefficients
 */

void DigitalRenderer::calc_filter(void)
{
    FixPoint fr, arg;

    if (f_type == FILT_ALL)
    {
        d1 = 0; d2 = 0; g1 = 0; g2 = 0; f_ampl = FixNo(1); return;
    }
    else if (f_type == FILT_NONE)
    {
        d1 = 0; d2 = 0; g1 = 0; g2 = 0; f_ampl = 0; return;
    }

    // Calculate resonance frequency
    if (f_type == FILT_LP || f_type == FILT_LPBP)
#ifdef PRECOMPUTE_RESONANCE
        fr = resonanceLP[f_freq + f_freq_low];
#else
        fr = CALC_RESONANCE_LP(f_freq);
#endif
    else
#ifdef PRECOMPUTE_RESONANCE
        fr = resonanceHP[f_freq+f_freq_low];
#else
        fr = CALC_RESONANCE_HP(f_freq);
#endif

    // explanations see below.
    arg = fr / (int)((isDSiMode() ? SAMPLE_FREQ_DSI : SAMPLE_FREQ) >> 1);

    if (arg > FixNo(0.99)) {arg = FixNo(0.99);}
    if (arg < FixNo(0.01)) {arg = FixNo(0.01);}

    g2 = FixNo(0.55) + FixNo(1.2) * arg * (arg - 1) + FixNo(0.0133333333) * f_res;
    g1 = FixNo(-2) * g2.sqrt() * fixcos(arg);

    if (f_type == FILT_LPBP || f_type == FILT_HPBP) {g2 += FixNo(0.1);}

    if (g1.abs() >= g2 + 1)
    {
      if (g1 > 0) {g1 = g2 + FixNo(0.99);}
      else {g1 = -(g2 + FixNo(0.99));}
    }

    switch (f_type)
    {
      case FILT_LPBP:
      case FILT_LP:
        d1 = FixNo(2); d2 = FixNo(1); f_ampl = FixNo(0.25) * (1 + g1 + g2); break;
      case FILT_HPBP:
      case FILT_HP:
        d1 = FixNo(-2); d2 = FixNo(1); f_ampl = FixNo(0.25) * (1 - g1 + g2); break;
      case FILT_BP:
        d1 = 0; d2 = FixNo(-1);
        {
        FixPoint c = fixsqrt(g2*g2 + FixNo(2.0)*g2 - g1*g1 + FixNo(1.0));
        f_ampl = FixNo(0.25) * (FixNo(-2.0)*g2*g2 - (FixNo(4.0)+FixNo(2.0)*c)*g2 - FixNo(2.0)*c + (c+FixNo(2.0))*g1*g1 - FixNo(2.0)) / (-g2*g2 - (c+FixNo(2.0))*g2 - c + g1*g1 - FixNo(1.0));
        }
        
        break;
      case FILT_NOTCH:
        d1 = FixNo(-2) * fixcos(arg); d2 = FixNo(1);
        f_ampl = FixNo(0.25) * (1 + g1 + g2) * (1 + fixcos(arg)) / fixsin(arg);
        break;
      default: break;
    }
}


/*
 *  Fill one audio buffer with calculated SID sound
 */

ITCM_CODE int16 DigitalRenderer::calc_buffer(int16 *buf, long count)
{
    // Get filter coefficients, so the emulator won't change
    // them in the middle of our calculations
    FixPoint cf_ampl = f_ampl;
    FixPoint cd1 = d1, cd2 = d2, cg1 = g1, cg2 = g2;

    // Index in sample_vol_filt[] for reading, 16.16 fixed
    uint32 sample_count = (sample_in_ptr + SAMPLE_BUF_SIZE/(isDSiMode() ? 2:4)) << 16;
    
    // Output DC offset
 	int32_t dc_offset = 0x100000;

    count >>= 1;    // 16 bit mono output, count is in bytes

    while (count--)
    {
		// Get current master volume and RES/FILT setting from sample buffers
 		uint8_t master_volume = sample_vol_filt[(sample_count >> 16) % (isDSiMode() ? SAMPLE_BUF_SIZE : (SAMPLE_BUF_SIZE/2))] & 0xf;
 		uint8_t res_filt = sample_vol_filt[(sample_count >> 16) % (isDSiMode() ? SAMPLE_BUF_SIZE : (SAMPLE_BUF_SIZE/2))] >> 4;
                
        // calculate sampled voice
        sample_count += ((0x138 * (isDSiMode() ? 100:50)) << 16) / (isDSiMode() ? SAMPLE_FREQ_DSI : SAMPLE_FREQ);
        int32_t sum_output = 0;
        int32 sum_output_filter = 0;

        // Loop for all three voices
        for (int j=0; j<3; j++)
        {
            DRVoice *v = &voice[j];

            // Envelope generator
            uint16 envelope;

            switch (v->eg_state) {
                case EG_ATTACK:
                    v->eg_level += v->a_add;
                    if (v->eg_level > 0xffffff) {
                        v->eg_level = 0xffffff;
                        v->eg_state = EG_DECAY_SUSTAIN;
                    }
                    break;
                case EG_DECAY_SUSTAIN:
                    v->eg_level -= v->d_sub >> EGDRShift[v->eg_level >> 16];
                    if (v->eg_level < v->s_level) {
                        v->eg_level = v->s_level;
                    }
                    break;
                case EG_RELEASE:
                    v->eg_level -= v->r_sub >> EGDRShift[v->eg_level >> 16];
                    if (v->eg_level < 0) {
                        v->eg_level = 0;
                    }
                    break;
            }
            envelope = v->eg_level >> 16;

            // Waveform generator
            if (v->mute)
                continue;
            uint16 output;

            if (!v->test)
                v->count += v->add;

            if (v->sync && (v->count > 0x1000000))
                v->mod_to->count = 0;

            v->count &= 0xffffff;

            switch (v->wave) 
            {
                case WAVE_TRI: {
                        uint32_t ctrl = v->count;
                        if (v->ring) {
                            ctrl ^= v->mod_by->count;
                        }
                        if (ctrl & 0x800000) {
                            output = (v->count >> 7) ^ 0xffff;
                        } else {
                            output = v->count >> 7;
                        }
                    }
                    break;
                case WAVE_SAW:
                    output = v->count >> 8;
                    break;
                case WAVE_RECT:
                    if (v->test || v->count >= (uint32_t)(v->pw << 12))
                        output = 0xffff;
                    else
                        output = 0;
                    break;
                case WAVE_TRISAW:
                    output = ((u16*) 0x068A0000)[v->count >> 16];
                    break;
                case WAVE_TRIRECT:
                    if (v->test || v->count >= (uint32_t)(v->pw << 12))
                    {
                        uint32_t ctrl = v->count;
                        if (v->ring) 
                        {
                            ctrl ^= ~(v->mod_by->count) & 0x800000;
                        }
                        output = TriRectTable[ctrl >> 16];
                    }
                    else
                    {
                        output = 0;
                    }
                    break;
                case WAVE_SAWRECT:
                    if (v->test || v->count >= (uint32_t)(v->pw << 12))
                        output = ((u16*) 0x068A2000)[v->count >> 16];
                    else
                        output = 0;
                    break;
                case WAVE_TRISAWRECT:
                    if (v->test || v->count >= (uint32_t)(v->pw << 12))
                        output = ((u16*) 0x068A3000)[v->count >> 16];
                    else
                        output = 0;
                    break;
                case WAVE_NOISE:
                    if (v->count > 0x100000) {
                        output = v->noise = sid_random() << 8;
                        v->count &= 0xfffff;
                    } else
                        output = v->noise;
                    break;
                default:
                    output = 0x8000;
                    break;
            }
            
            // Route voice through filter if selected
 			if (res_filt & (1 << j))
                sum_output_filter += (int16)(output ^ 0x8000) * envelope;
            else
                sum_output += (int16)(output ^ 0x8000) * envelope;
        }

        // Filter
        int32 xn = cf_ampl.imul(sum_output_filter);
        int32 yn = xn+cd1.imul(xn1)+cd2.imul(xn2)-cg1.imul(yn1)-cg2.imul(yn2);
        yn2 = yn1; yn1 = yn; xn2 = xn1; xn1 = xn;
        sum_output_filter = yn;

        int32_t ext_output = (sum_output - sum_output_filter + dc_offset) * master_volume;
        ext_output >>= 13;

		// Write to buffer
 		if (ext_output > 0x7fff) {	// Using filters can cause minor clipping
 			ext_output = 0x7fff;
 		} else if (ext_output < -0x8000) {
 			ext_output = -0x8000;
 		}
        
        *buf++ = ext_output;
    }
    buf--; return *buf;
}

DigitalRenderer* p   __attribute__((section(".dtcm")));
bool paused          __attribute__((section(".dtcm"))) = false;
int16 last_sample    __attribute__((section(".dtcm"))) = 0x8000;

ITCM_CODE mm_word SoundMixCallback(mm_word len, mm_addr stream, mm_stream_formats format)
{
    if (paused)
    {
        s16 *p = (s16*)stream;
        for (mm_word i=0; i<len; i++)
        {
           *p++ = last_sample;      // To prevent pops and clicks... just keep outputting the last sample
           *p++ = last_sample;      // To prevent pops and clicks... just keep outputting the last sample
        }
    }
    else
    {
        last_sample = p->calc_buffer((int16*)stream, len*2);
    }
    return len;
}

void init_maxmod(void)
{
    //----------------------------------------------------------------
    //  initialize maxmod with our small 3-effect soundbank
    //----------------------------------------------------------------
    mmInitDefaultMem((mm_addr)soundbank_bin);

    mmLoadEffect(SFX_FLOPPY);
    mmLoadEffect(SFX_KEYCLICK);
    mmLoadEffect(SFX_MUS_INTRO);
}

void DigitalRenderer::init_sound(void)
{
    p = this;

    mm_stream mstream;
    memset(&mstream, 0, sizeof(mstream));
    mstream.sampling_rate = (isDSiMode() ? SAMPLE_FREQ_DSI : SAMPLE_FREQ);
    mstream.buffer_length = 0x138 * 2; // Even for DSi we keep the buffer small so we get faster sampling
    mstream.callback = SoundMixCallback;
    mstream.format = MM_STREAM_16BIT_MONO;
    mstream.timer = MM_TIMER2;
    mstream.manual = false;
    DC_FlushAll();
    mmStreamOpen(&mstream);
}

DigitalRenderer::~DigitalRenderer()
{

}

void DigitalRenderer::EmulateLine(void)
{
    sample_vol_filt[sample_in_ptr] = volume | ((res_filt & 7) << 4);
    sample_in_ptr = (sample_in_ptr + 1) % (isDSiMode() ? SAMPLE_BUF_SIZE : (SAMPLE_BUF_SIZE/2));
}

void DigitalRenderer::Pause(void)
{
    paused = true;
}

void DigitalRenderer::Resume(void)
{
    paused = false;
}


/*
 *  Open/close the renderer, according to old and new prefs
 */

void MOS6581::open_close_renderer(int old_type, int new_type)
{
    if (old_type == new_type)
        return;

    // Delete the old renderer
    delete the_renderer;

    // Create new renderer
    the_renderer = new DigitalRenderer;

    // Stuff the current register values into the new renderer
    if (the_renderer != NULL)
        for (int i=0; i<25; i++)
            the_renderer->WriteRegister(i, regs[i]);
}

/*
 *  Simulate oscillator 3 read-back
 */
uint8_t MOS6581::read_osc3() const
 {
    uint8_t v3_ctrl = regs[0x12];   // Voice 3 control register
    if (v3_ctrl & 0x10) {           // Triangle wave
        // TODO: ring modulation from voice 2
        if (fake_v3_count & 0x800000) {
            return (fake_v3_count >> 15) ^ 0xff;
        } else {
            return fake_v3_count >> 15;
        }
    } else if (v3_ctrl & 0x20) {    // Sawtooth wave
        return fake_v3_count >> 16;
    } else if (v3_ctrl & 0x40) {    // Rectangle wave
        uint32_t pw = ((regs[0x11] & 0x0f) << 8) | regs[0x10];
        if (fake_v3_count > (pw << 12)) {
            return 0xff;
        } else {
            return 0x00;
        }
    } else if (v3_ctrl & 0x80) {    // Noise wave
        return sid_random();
    } else {
        // TODO: combined waveforms
        return 0;
    }
 }


/*
 *  Simulate EG 3 read-back
 */

uint8_t MOS6581::read_env3() const
{
    return (uint8_t)(fake_v3_eg_level >> 16);
}

