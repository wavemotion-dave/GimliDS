/*
 *  main.h - Main program
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

#ifndef _MAIN_H
#define _MAIN_H


class C64;

// Global variables

class Prefs;

class Frodo {
public:
    Frodo();
    void ArgvReceived(int argc, char **argv);
    void ReadyToRun(void);

    C64 *TheC64;

private:
    u8 load_rom(const char *which, const char *path, uint8 *where, size_t size);
    void load_rom_files();
};


// Global C64 object
extern C64 *TheC64;

/*
 *  Functions
 */

// Determine whether path name refers to a directory
extern bool IsDirectory(const char *path);
extern void DSPrint(int iX,int iY,int iScr,char *szMessage);
extern void kbd_buf_reset(void);
extern int current_joystick;

#endif
