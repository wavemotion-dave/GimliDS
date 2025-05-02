# GimliDS
![image](./arm9/gfx_data/intro.png)

GimliDS is a C64 Emulator for the DS/DSi and is copyright (c) 2025 Dave Bernazzani (wavemotion-dave)

As GimliDS is a port of the Frodo emulator for the DS/DSi/XL/LL handhelds,
any copying or distribution of this emulator, its source code and associated
readme files, with or without modification, are permitted per the original 
Frodo emulator license shown below.  Hugest thanks to Christian Bauer for his
efforts to provide a clean open-source emulation base for the C64.

Numerous hacks and 'unsafe' optimizations have been performed on the original 
Frodo emulator codebase to get it running on the small handheld system. You 
are strongly encouraged to seek out the official Frodo sources if you're at
all interested in this emulator code.

The GimliDS emulator is offered as-is, without any warranty.

## BIOS Files

You must have original Commodore BIOS files to run this emulator. Place them in /roms/bios or /roms/c64
and be sure they are named exactly as follows:
```
  * KERNAL.ROM   8K  with CRC32=dbe3e7c7 (901227-03)
  * BASIC.ROM    8K  with CRC32=f833d117 (901226-01)
  * CHAR.ROM     4K  with CRC32=ec4272ee (901225-01)
  * 1541.ROM    16K  with CRC32=899fa3c5 (251968-03)
```

## Controls

  * Left Trigger + DPAD - Scale the screen in the X and Y directions
  * Right Trigger + DPAD - Offset the screen in the X and Y directions
  * START - Issue the infamous LOAD "*",8,1 (be sure the emulator is at the READY prompt)
  * SELECT - switch between port1 and port2 that the joystick is in
  * D-Pad - Joystick directions
  * A is normally FIRE but can be remapped in Configuration
  * B is normally SPACE but can be remapped in Configuration
  * Y is normally mapped to RETURN but can be remapped in Configuration
  * X is normally mapped to Joy Up to help with games in which you jump but can be remapped

## Emulator Use

To use GimliDS you launch GimliDS.NDS on your DS/DSi/XL/LL. Please note that if you are running from an R4 card (or similar), you 
will be running in 'Compatibility' mode that keeps the CPU at the slower 67MHz. While GimliDS will run, it must use some level 
of light frameskip to get full speed gameplay. If you're running from the SD card on a DSi/XL/LL with something like Twilight Menu++
or Unlaunch - you will be happy to know that you are running at 134MHz and there will be no frameskip and the audio will be 
much improved.

When you first launch the emulator (provided you have the BIOS files in the right place), you will be greeted by the CBM BASIC 
and the famous READY prompt:

![image](./pngs/startup.png)

From here, use the DISK ICON to load up a new game from a .d64 file. Once mounted, get back to the main emulation (exit the menu)
and press the START button to automatically type in the disk load command:  LOAD "*",8,1 and RUN the game.

The *C=* key (lower right) will let you set the configuration for the game, save/load game states, etc.

And be sure to understand the SELECT key on the DS... that swaps Joystick Ports and you will be using that a LOT as every game seems to
use a different port... and sometimes the game loaders/crackers use one port when the game uses the other port. You'll get used to it.

Also understand that the DS screen resolution of 256x192 is not ideal for the Commodore screen output resolution of 320x200 plus the borders.
To that end, you will be using the L/R shoulder buttons with the D-Pad to scale and offset the screen to get it to look as good as you can.
When you save your configuration for that game, it will remember the scale/offset values you last used. Also, experiement with remapping one
of the DS keys to 'PAN UP' or 'PAN DOWN' which can be used to temporarlily shift the screen by 16 pixels up/down and then the system will
smooth scroll that back into place. That works really well for games that have a high score or status indicator at the very top or bottom
of the screen... you can enjoy nearly full resolution output and just occasionally hit a PAN UP/DN button to see your score. Experiment and
have fun - just like we did in 1982!

Lastly, a few games use custom loaders that require you to enable 'True Drive'. Be warned that True Drive will render the floppy driver at 
a speed that is comparable to the original Commodore 1541 floppy drive - that is: extremely slow. It could take 2-5 minutes to load a game
this way. But if the game requires it, that's your only option. Recommended to snap out a Save State so you don't have to repeat the loading.

## Acknowledgements

  * The opening jingle was done by DeNL and comes courtesy of the roalty free jingles at pixabay.
  * The dwarf icon on the title screen was submitted online by 'anonymous' to an artwork site.  If this is yours and you would like to be credited, please contact me.
  * Christian Bauer - Original Frodo author - without whom none of this exists!
  * Troy Davis(GPF) http://gpf.dcemu.co.uk - FrodoDS initial porter and others who have contributed over the years.
  * Wintermute and the contributors for devkitpro and libnds.
  * djleviticus for his time and efforts in helping shake out some bugs during the beta testing.

## Why GimliDS?

Well - this is not enitrely a classic Frodo emulator but is clearly derived from those sources. Mainline Frodo has taken many great steps forward in recent years to be 
more accurate and generally is a couple of full notches above where this port sits. My emulator is largely taken from older DS ports that used Frodo 4.2 (circa 2007) as
a codebase and numerous tweaks and hacks were done to get it running on the venerable DS hardwarwe. I've taken that and tried to back-port some of the newer Frodo mainline
improvements but also add my own tweaks and hacks to make it fully playable on the little handheld. As such, the GimliDS naming is a clear nod to the original emulator
but also distinguishes it as a bit rough around the edges...

## Original Frodo Copyright
```
Frodo Copyright (C) Christian Bauer

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
```
## Change Log

Version 0.9a release 02-May-2025 by wavemotion-dave
  * Fixed TrueDrive loading so it doesn't alter the CPU speed (for games like Pang which were playing quite 'uneven').
  * Bumped up volume of the SID output by 50% (was too quiet)
  * Improved 1541 Drive emulation - mostly in the GCR and IEC areas. Nothing noticeable but should be more accurate.
  * Fixed write floppy icon color (instead of the usual green, will flash blue).
  * Tweaks to C64 keyboard graphic to be more authentic.

Version 0.9 release 30-Apr-2025 by wavemotion-dave
  * New and improved key mapping - including the new Slide-n-Glide joysticks handling.
  * SID filtering improved for better audio rendering (Space Taxi voice heard!).
  * Faster across the board - even the DS-Lite/Phat is now running at (or close to) full speed.
  * Fixed joystick handling so we sample at the correct rate - smoother motion on screen.

Version 0.8 release 27-Apr-2025 by wavemotion-dave
  * First public beta released!

