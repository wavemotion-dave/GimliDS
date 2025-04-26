/*
 *  Prefs.h - Global preferences
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

#ifndef _PREFS_H
#define _PREFS_H
#include "sysdeps.h"

// SID types
enum {
    SIDTYPE_NONE,       // SID emulation off
    SIDTYPE_DIGITAL,    // Digital SID emulation
    SIDTYPE_SIDCARD     // SID card
};


// Preferences data
class Prefs {
public:
    Prefs();
    bool ShowEditor(bool startup, char *prefs_name);
    void Check(void);
    void Load(char *filename);
    bool Save(char *filename);

    bool operator==(const Prefs &rhs) const;
    bool operator!=(const Prefs &rhs) const;

    char DrivePath[2][256]; // Path for drive 8 and 9
    int DrawEveryN;         // Draw every n-th frame
    int SIDType;            // SID emulation type
    bool LimitSpeed;        // Limit speed to 100%
    bool FastReset;         // Skip RAM test on reset
    bool CIAIRQHack;        // Write to CIA ICR clears IRQ
    bool TrueDrive;         // Enable processor-level 1541 emulation
};

// These are the active preferences
extern Prefs ThePrefs;

#endif
