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
 *  Display.h - C64 graphics display, emulator window handling
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

#ifndef _DISPLAY_H
#define _DISPLAY_H

const int DISPLAY_X = 0x180;
const int DISPLAY_Y = 0x11f;

class C64Window;
class C64Screen;
class C64;
class Prefs;

// Class for C64 graphics display
class C64Display {
public:
    C64Display(C64 *the_c64);
    ~C64Display();
    void UpdateRasterLine(int raster, u8 *src);
    void UpdateLEDs(int l0, int l1);
    void DisplayStatusLine(int speed);
    void KeyPress(int key, uint8 *key_matrix, uint8 *rev_matrix);
    void KeyRelease(int key, uint8 *key_matrix, uint8 *rev_matrix);
    void PollKeyboard(uint8 *key_matrix, uint8 *rev_matrix, uint8 *joystick);
    void InitColors(uint8 *colors);
    void NewPrefs(Prefs *prefs);
    void IssueKeypress(uint8 row, uint8 col, uint8 *key_matrix, uint8 *rev_matrix);
    C64 *TheC64;

public:
    int led_state[2];
};


// Exported functions
extern long ShowRequester(const char *str, const char *button1, const char *button2 = NULL);
extern u8 issue_commodore_key;
extern int8 currentBrightness;
extern void toggle_zoom(void);

#endif
