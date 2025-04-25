/*
 *  Prefs.cpp - Global preferences
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

#include "sysdeps.h"

#include "Prefs.h"
#include "Display.h"
#include "C64.h"
#include "main.h"


// These are the active preferences
Prefs ThePrefs __attribute__((section(".dtcm")));


/*
 *  Constructor: Set up preferences with defaults
 */

Prefs::Prefs()
{
    // These are PAL specific values
    DrawEveryN = isDSiMode() ? 1:2;

    strcpy(DrivePath[0], "");
    strcpy(DrivePath[1], "");

    SIDType = SIDTYPE_DIGITAL;

    LimitSpeed = true;
    FastReset = true;
    CIAIRQHack = false;
    TrueDrive = false; // True Drive Emulation when TRUE (slower emulation)
    SIDFilters = true;
}

