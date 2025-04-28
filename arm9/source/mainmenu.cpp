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

// Main Menu Loading Code

#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/dir.h>
#include "C64.h"
#include "diskmenu.h"
#include "mainmenu.h"
#include "mainmenu_bg.h"
#include "Prefs.h"

extern C64 *TheC64;
extern int bg0b, bg1b;

#define WAITVBL swiWaitForVBlank();swiWaitForVBlank();swiWaitForVBlank();
static u16 nds_key;
extern void BottomScreenMainMenu(void);

// ----------------------------------------------------------------------
// The Disk Menu can be called up directly from the keyboard graphic
// and allows the user to rewind the tape, swap in a new tape, etc.
// ----------------------------------------------------------------------
#define MENU_ACTION_END             255 // Always the last sentinal value
#define MENU_ACTION_EXIT            0   // Exit the menu
#define MENU_ACTION_RESET_EMU       1   // Reset Emulator
#define MENU_ACTION_SAVE_STATE      2   // 
#define MENU_ACTION_LOAD_STATE      3   // 
#define MENU_ACTION_CONFIG          4   // 
#define MENU_ACTION_PRESS_C64       5   // Issue the actual C= key!
#define MENU_ACTION_SKIP            99  // Skip this MENU choice

typedef struct
{
    char *menu_string;
    u8    menu_action;
} MenuItem_t;

typedef struct
{
    char *title;
    u8   start_row;
    MenuItem_t menulist[15];
} MainMenu_t;

MainMenu_t main_menu =
{
    (char *)"MAIN MENU", 7,
    {
        {(char *)"  CONFIG   GAME   ",      MENU_ACTION_CONFIG},
        {(char *)"  SAVE     STATE  ",      MENU_ACTION_SAVE_STATE},
        {(char *)"  LOAD     STATE  ",      MENU_ACTION_LOAD_STATE},
        {(char *)"  RESET    C64    ",      MENU_ACTION_RESET_EMU},
        {(char *)"  PRESS    @ KEY  ",      MENU_ACTION_PRESS_C64},
        {(char *)"  EXIT     MENU   ",      MENU_ACTION_EXIT},
        {(char *)"  NULL            ",      MENU_ACTION_END},
    },
};

static MainMenu_t *menu = &main_menu;

// -------------------------------------------------------
// Show the Main Menu text - highlight the selected row.
// -------------------------------------------------------
u8 main_menu_items = 0;
void MainMenuShow(bool bClearScreen, u8 sel)
{
    main_menu_items = 0;

    if (bClearScreen)
    {
        BottomScreenMainMenu();
    }

    // ---------------------------------------------------
    // Pick the right context menu based on the machine
    // ---------------------------------------------------
    menu = &main_menu;

    // Display the menu title
    DSPrint(15-(strlen(menu->title)/2), menu->start_row, 6, menu->title);

    // And display all of the menu items
    while (menu->menulist[main_menu_items].menu_action != MENU_ACTION_END)
    {
        DSPrint(16-(strlen(menu->menulist[main_menu_items].menu_string)/2), menu->start_row+2+main_menu_items, (main_menu_items == sel) ? 7:6, menu->menulist[main_menu_items].menu_string);
        main_menu_items++;
    }
}

static char theDrivePath[256];

void check_and_make_sav_directory(void)
{
  // Init filename = romname and SAV in place of ROM
  DIR* dir = opendir("sav");
  if (dir) closedir(dir);    // Directory exists... close it out and move on.
  else mkdir("sav", 0777);   // Otherwise create the directory...
}


// ------------------------------------------------------------------------
// Handle Main Menu interface...
// ------------------------------------------------------------------------
u8 MainMenu(C64 *the_c64)
{
  u8 menuSelection = 0;
  u8 retVal = 0;

  while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_A ))!=0);

  // ------------------------------------------------------------------
  //Show the cassette menu background - we'll draw text on top of this
  // ------------------------------------------------------------------
  MainMenuShow(true, menuSelection);

  u8 bExitMenu = false;
  while (true)
  {
    nds_key = keysCurrent();
    if (nds_key)
    {
        if (nds_key & KEY_UP)
        {
            menuSelection = (menuSelection > 0) ? (menuSelection-1):(main_menu_items-1);
            while (menu->menulist[menuSelection].menu_action == MENU_ACTION_SKIP)
            {
                menuSelection = (menuSelection > 0) ? (menuSelection-1):(main_menu_items-1);
            }
            MainMenuShow(false, menuSelection);
        }
        if (nds_key & KEY_DOWN)
        {
            menuSelection = (menuSelection+1) % main_menu_items;
            while (menu->menulist[menuSelection].menu_action == MENU_ACTION_SKIP)
            {
                menuSelection = (menuSelection+1) % main_menu_items;
            }
            MainMenuShow(false, menuSelection);
        }
        if (nds_key & KEY_B) // Treat this as selecting 'exit'
        {
            bExitMenu = true;
        }
        else if (nds_key & KEY_A)    // User has picked a menu item... let's see what it is!
        {
            switch(menu->menulist[menuSelection].menu_action)
            {
                case MENU_ACTION_PRESS_C64:
                    extern u8 issue_commodore_key;
                    issue_commodore_key = 1;
                    bExitMenu = true;
                    break;
                    
                case MENU_ACTION_RESET_EMU:
                    the_c64->PatchKernal(ThePrefs.FastReset, ThePrefs.TrueDrive);
                    the_c64->Reset();
                    bExitMenu = true;
                    break;
                    
                case MENU_ACTION_CONFIG:
                    if (file_crc != 0x00000000)
                    {
                        u8 last_trueDrive = myConfig.trueDrive;
                        GimliDSGameOptions();
                        if (last_trueDrive != myConfig.trueDrive) // Need to reload...
                        {
                            Prefs *prefs = new Prefs(ThePrefs);
                            prefs->TrueDrive = myConfig.trueDrive;
                            the_c64->NewPrefs(prefs);
                            ThePrefs = *prefs;
                            delete prefs;
                        }                        
                        bExitMenu = true;
                    }
                    else
                    {
                        DSPrint(0, 18, 6, (char*)"       NO GAME IS LOADED      ");
                        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
                        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
                        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
                        DSPrint(0, 18, 6, (char*)"                              ");
                    }
                    break;
                    
                case MENU_ACTION_SAVE_STATE:
                {
                    check_and_make_sav_directory();
                    sprintf(theDrivePath,"sav/%s", ThePrefs.DrivePath[0]);
                    int len = strlen(theDrivePath);
                    if (len > 4)
                    {
                        char *p=&theDrivePath[len-4];
                        strcpy(p,".GSS");
                    }
                    if (the_c64->SaveSnapshot(theDrivePath) == false)
                    {
                        DSPrint(0, 18, 6, (char*)"      UNABLE TO SAVE STATE     ");
                    }
                    else
                    {
                        DSPrint(0, 18, 6, (char*)"      .GSS SNAPSHOT SAVED      ");
                    }
                    WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
                    WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
                    WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
                    DSPrint(0, 18, 6, (char*)"                               ");
                    bExitMenu = true;
                }
                    break;

                case MENU_ACTION_LOAD_STATE:
                {
                    check_and_make_sav_directory();
                    sprintf(theDrivePath,"sav/%s", ThePrefs.DrivePath[0]);
                    int len = strlen(theDrivePath);
                    if (len > 4)
                    {
                        char *p=&theDrivePath[len-4];
                        strcpy(p,".GSS");
                    }
                    if (the_c64->LoadSnapshot(theDrivePath) == false)
                    {
                        DSPrint(0, 18, 6, (char*)"    NO VALID SNAPSHOT FOUND    ");
                        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
                        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
                        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
                        DSPrint(0, 18, 6, (char*)"                               ");
                    }
                    bExitMenu = true;
                }
                    break;
                    
                case MENU_ACTION_EXIT:
                    bExitMenu = true;
                    break;
            }
        }

        if (bExitMenu) break;
        while ((keysCurrent() & (KEY_UP | KEY_DOWN | KEY_A ))!=0);
        WAITVBL;WAITVBL;WAITVBL;
    }
  }

  while ((keysCurrent() & (KEY_UP | KEY_DOWN | KEY_A ))!=0);
  WAITVBL;WAITVBL;WAITVBL;
  
  return retVal;
}


// zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz

#define CRC32_POLY 0x04C11DB7

const u32 crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,  //   0 [0x00 .. 0x07]
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,  //   8 [0x08 .. 0x0F]
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,  //  16 [0x10 .. 0x17]
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,  //  24 [0x18 .. 0x1F]
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,  //  32 [0x20 .. 0x27]
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,  //  40 [0x28 .. 0x2F]
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,  //  48 [0x30 .. 0x37]
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,  //  56 [0x38 .. 0x3F]
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,  //  64 [0x40 .. 0x47]
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,  //  72 [0x48 .. 0x4F]
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,  //  80 [0x50 .. 0x57]
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,  //  88 [0x58 .. 0x5F]
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,  //  96 [0x60 .. 0x67]
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,  // 104 [0x68 .. 0x6F]
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,  // 112 [0x70 .. 0x77]
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,  // 120 [0x78 .. 0x7F]
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,  // 128 [0x80 .. 0x87]
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,  // 136 [0x88 .. 0x8F]
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,  // 144 [0x90 .. 0x97]
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,  // 152 [0x98 .. 0x9F]
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,  // 160 [0xA0 .. 0xA7]
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,  // 168 [0xA8 .. 0xAF]
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,  // 176 [0xB0 .. 0xB7]
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,  // 184 [0xB8 .. 0xBF]
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,  // 192 [0xC0 .. 0xC7]
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,  // 200 [0xC8 .. 0xCF]
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,  // 208 [0xD0 .. 0xD7]
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,  // 216 [0xD8 .. 0xDF]
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,  // 224 [0xE0 .. 0xE7]
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,  // 232 [0xE8 .. 0xEF]
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,  // 240 [0xF0 .. 0xF7]
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,  // 248 [0xF8 .. 0xFF]
};

// --------------------------------------------------
// Compute the CRC of a memory buffer of any size...
// --------------------------------------------------
u32 getCRC32(u8 *buf, int size)
{
    u32 crc = 0xFFFFFFFF;

    for (int i=0; i < size; i++)
    {
        crc = (crc >> 8) ^ crc32_table[(crc & 0xFF) ^ (u8)buf[i]]; 
    }
    
    return ~crc;
}

extern char strBuf[];
u32 file_crc = 0x00000000;
u8 option_table=0;

struct Config_t AllConfigs[MAX_CONFIGS];
struct Config_t myConfig __attribute((aligned(4))) __attribute__((section(".dtcm")));


void SetDefaultGameConfig(void)
{
    myConfig.game_crc    = 0;    // No game in this slot yet

    myConfig.key_A       = 0;
    myConfig.key_B       = 1;
    myConfig.key_Y       = 2;
    myConfig.key_X       = 3;
    myConfig.autoFire    = 0;
    myConfig.trueDrive   = 0;
    myConfig.jitter      = 1; // Medium 
    myConfig.diskSFX     = 1; // Disk sound effects on
    myConfig.joyPort     = 0; // Default to Joy1
    myConfig.reserved2   = 0;
    myConfig.reserved3   = 0;
    myConfig.reserved4   = 0;
    myConfig.reserved5   = 0;
    myConfig.reserved6   = 0xA5;    // So it's easy to spot on an "upgrade" and we can re-default it
    
    myConfig.cpuCycles   = 0;   // Normal 63 - this is the adjustment to that
    myConfig.badCycles   = 0;   // Normal 23 - this is the adjustment to that
    
    myConfig.offsetX     = 32;
    myConfig.offsetY     = 19;
    myConfig.scaleX      = 256;
    myConfig.scaleY      = 200;
}

s16 CycleDeltas[] = {0,1,2,3,4,5,6,7,8,9,-9,-8,-7,-6,-5,-4,-3,-2,-1};

// ----------------------------------------------------------------------
// Read file twice and ensure we get the same CRC... if not, do it again
// until we get a clean read. Return the filesize to the caller...
// ----------------------------------------------------------------------
u32 ReadFileCarefully(char *filename, u8 *buf, u32 buf_size, u32 buf_offset)
{
    u32 crc1 = 0;
    u32 crc2 = 1;
    u32 fileSize = 0;

    // --------------------------------------------------------------------------------------------
    // I've seen some rare issues with reading files from the SD card on a DSi so we're doing
    // this slow and careful - we will read twice and ensure that we get the same CRC both times.
    // --------------------------------------------------------------------------------------------
    do
    {
        // Read #1
        crc1 = 0xFFFFFFFF;
        FILE* file = fopen(filename, "rb");
        if (file)
        {
            if (buf_offset) fseek(file, buf_offset, SEEK_SET);
            fileSize = fread(buf, 1, buf_size, file);
            crc1 = getCRC32(buf, buf_size);
            fclose(file);
        }

        // Read #2
        crc2 = 0xFFFFFFFF;
        FILE* file2 = fopen(filename, "rb");
        if (file2)
        {
            if (buf_offset) fseek(file2, buf_offset, SEEK_SET);
            fread(buf, 1, buf_size, file2);
            crc2 = getCRC32(buf, buf_size);
            fclose(file2);
        }
   } while (crc1 != crc2); // If the file couldn't be read, file_size will be 0 and the CRCs will both be 0xFFFFFFFF

   return fileSize;
}


// ---------------------------------------------------------------------------
// Write out the GimliDS.DAT configuration file to capture the settings for
// each game.  This one file contains global settings ~1000 game settings.
// ---------------------------------------------------------------------------
void SaveConfig(bool bShow)
{
    FILE *fp;
    int slot = 0;

    if (bShow) DSPrint(6,23,0, (char*)"SAVING CONFIGURATION");

    // If there is a game loaded, save that into a slot... re-use the same slot if it exists
    myConfig.game_crc = file_crc;

    // Find the slot we should save into...
    for (slot=0; slot<MAX_CONFIGS; slot++)
    {
        if (AllConfigs[slot].game_crc == myConfig.game_crc)  // Got a match?!
        {
            break;
        }
        if (AllConfigs[slot].game_crc == 0x00000000)  // Didn't find it... use a blank slot...
        {
            break;
        }
    }

    // --------------------------------------------------------------------------
    // Copy our current game configuration to the main configuration database...
    // --------------------------------------------------------------------------
    if (myConfig.game_crc != 0x00000000)
    {
        memcpy(&AllConfigs[slot], &myConfig, sizeof(struct Config_t));
    }

    // --------------------------------------------------
    // Now save the config file out to the SD card...
    // --------------------------------------------------
    DIR* dir = opendir("/data");
    if (dir)
    {
        closedir(dir);  // directory exists.
    }
    else
    {
        mkdir("/data", 0777);   // Doesn't exist - make it...
    }
    fp = fopen("/data/GimliDS.DAT", "wb+");
    if (fp != NULL)
    {
        u16 ver = CONFIG_VERSION;
        fwrite(&ver, sizeof(ver), 1, fp);                   // Write the config version
        fwrite(&AllConfigs, sizeof(AllConfigs), 1, fp);     // Write the array of all configurations
        fclose(fp);
    } else DSPrint(4,23,0, (char*)"ERROR SAVING CONFIG FILE");

    if (bShow)
    {
        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
        DSPrint(4,23,0, (char*)"                        ");
    }
}
// ----------------------------------------------------------
// Load configuration into memory where we can use it.
// The configuration is stored in GimliDS.DAT
// ----------------------------------------------------------
void LoadConfig(void)
{
    // -----------------------------------------------------------------
    // Start with defaults.. if we find a match in our config database
    // below, we will fill in the config with data read from the file.
    // -----------------------------------------------------------------
    SetDefaultGameConfig();

    u16 ver = 0x0000;
    if (ReadFileCarefully((char *)"/data/GimliDS.DAT", (u8*)&ver, sizeof(ver), 0))  // Read Global Config
    {
        ReadFileCarefully((char *)"/data/GimliDS.DAT", (u8*)&AllConfigs, sizeof(AllConfigs), sizeof(ver)); // Read the full game array of configs

        if (ver != CONFIG_VERSION)
        {
            memset(&AllConfigs, 0x00, sizeof(AllConfigs));
            SetDefaultGameConfig();
            SaveConfig(FALSE);
        }
    }
    else    // Not found... init the entire database...
    {
        memset(&AllConfigs, 0x00, sizeof(AllConfigs));
        SetDefaultGameConfig();
        SaveConfig(FALSE);
    }
}

// -------------------------------------------------------------------------
// Try to match our loaded game to a configuration my matching CRCs
// -------------------------------------------------------------------------
void FindConfig(void)
{
    // -----------------------------------------------------------------
    // Start with defaults.. if we find a match in our config database
    // below, we will fill in the config with data read from the file.
    // -----------------------------------------------------------------
    SetDefaultGameConfig();

    for (u16 slot=0; slot<MAX_CONFIGS; slot++)
    {
        if (AllConfigs[slot].game_crc == file_crc)  // Got a match?!
        {
            memcpy(&myConfig, &AllConfigs[slot], sizeof(struct Config_t));
            break;
        }
    }
}


// ------------------------------------------------------------------------------
// Options are handled here... we have a number of things the user can tweak
// and these options are applied immediately. The user can also save off
// their option choices for the currently running game into the NINTV-DS.DAT
// configuration database. When games are loaded back up, NINTV-DS.DAT is read
// to see if we have a match and the user settings can be restored for the game.
// ------------------------------------------------------------------------------
struct options_t
{
    const char  *label;
    const char  *option[20];
    u8          *option_val;
    u8           option_max;
};

#define CYCLE_DELTA_STR  "+0","+1","+2","+3","+4","+5","+6","+7","+8","+9","-9","-8","-7","-6","-5","-4","-3","-2","-1",


const struct options_t Option_Table[1][20] =
{
    // Game Specific Configuration
    {
        {"TRUE DRIVE",     {"DISABLE (FAST)", "ENABLED (SLOW)"},                                        &myConfig.trueDrive,   2},
        {"AUTO FIRE",      {"OFF", "ON"},                                                               &myConfig.autoFire,    2},
        {"JOY PORT",       {"PORT 1", "PORT 2"},                                                        &myConfig.joyPort,     2},
        {"LCD JITTER",     {"NONE", "LIGHT", "HEAVY"},                                                  &myConfig.jitter,      3},
        {"DISK SOUND",     {"SFX OFF", "SFX ON"},                                                       &myConfig.diskSFX,     2},
        {"A BUTTON",       {"JOY FIRE", "SPACE", "RETURN", "JOY UP", "JOY DOWN", "PAN-UP", "PAN-DOWN"}, &myConfig.key_A,       7},
        {"B BUTTON",       {"JOY FIRE", "SPACE", "RETURN", "JOY UP", "JOY DOWN", "PAN-UP", "PAN-DOWN"}, &myConfig.key_B,       7},
        {"X BUTTON",       {"JOY FIRE", "SPACE", "RETURN", "JOY UP", "JOY DOWN", "PAN-UP", "PAN-DOWN"}, &myConfig.key_X,       7},
        {"Y BUTTON",       {"JOY FIRE", "SPACE", "RETURN", "JOY UP", "JOY DOWN", "PAN-UP", "PAN-DOWN"}, &myConfig.key_Y,       7},
        {"CPU CYCLES",     {CYCLE_DELTA_STR},                                                           &myConfig.cpuCycles,   19},
        {"BAD CYCLES",     {CYCLE_DELTA_STR},                                                           &myConfig.badCycles,   19},        
        {NULL,             {"",      ""},                                                               NULL,                  1},
    }
};


// ------------------------------------------------------------------
// Display the current list of options for the user.
// ------------------------------------------------------------------
u8 display_options_list(bool bFullDisplay)
{
    s16 len=0;

    DSPrint(1,21, 0, (char *)"                              ");
    if (bFullDisplay)
    {
        while (true)
        {
            sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][len].label, Option_Table[option_table][len].option[*(Option_Table[option_table][len].option_val)]);
            DSPrint(1,5+len, (len==0 ? 2:0), strBuf); len++;
            if (Option_Table[option_table][len].label == NULL) break;
        }

        // Blank out rest of the screen... option menus are of different lengths...
        for (int i=len; i<16; i++)
        {
            DSPrint(1,5+i, 0, (char *)"                               ");
        }
    }

    DSPrint(1,22, 0, (char *)"  A or B=EXIT,   START=SAVE    ");
    return len;
}


//*****************************************************************************
// Change Game Options for the current game
//*****************************************************************************
void GimliDSGameOptions(void)
{
    u8 optionHighlighted;
    u8 idx;
    bool bDone=false;
    int keys_pressed;
    int last_keys_pressed = 999;

    option_table = 0;

    idx=display_options_list(true);
    optionHighlighted = 0;
    while (keysCurrent() != 0)
    {
        WAITVBL;
    }
    while (!bDone)
    {
        keys_pressed = keysCurrent();
        if (keys_pressed != last_keys_pressed)
        {
            last_keys_pressed = keys_pressed;
            if (keysCurrent() & KEY_UP) // Previous option
            {
                sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][optionHighlighted].label, Option_Table[option_table][optionHighlighted].option[*(Option_Table[option_table][optionHighlighted].option_val)]);
                DSPrint(1,5+optionHighlighted,0, strBuf);
                if (optionHighlighted > 0) optionHighlighted--; else optionHighlighted=(idx-1);
                sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][optionHighlighted].label, Option_Table[option_table][optionHighlighted].option[*(Option_Table[option_table][optionHighlighted].option_val)]);
                DSPrint(1,5+optionHighlighted,2, strBuf);
            }
            if (keysCurrent() & KEY_DOWN) // Next option
            {
                sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][optionHighlighted].label, Option_Table[option_table][optionHighlighted].option[*(Option_Table[option_table][optionHighlighted].option_val)]);
                DSPrint(1,5+optionHighlighted,0, strBuf);
                if (optionHighlighted < (idx-1)) optionHighlighted++;  else optionHighlighted=0;
                sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][optionHighlighted].label, Option_Table[option_table][optionHighlighted].option[*(Option_Table[option_table][optionHighlighted].option_val)]);
                DSPrint(1,5+optionHighlighted,2, strBuf);
            }

            if (keysCurrent() & KEY_RIGHT)  // Toggle option clockwise
            {
                *(Option_Table[option_table][optionHighlighted].option_val) = (*(Option_Table[option_table][optionHighlighted].option_val) + 1) % Option_Table[option_table][optionHighlighted].option_max;
                sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][optionHighlighted].label, Option_Table[option_table][optionHighlighted].option[*(Option_Table[option_table][optionHighlighted].option_val)]);
                DSPrint(1,5+optionHighlighted,2, strBuf);
            }
            if (keysCurrent() & KEY_LEFT)  // Toggle option counterclockwise
            {
                if ((*(Option_Table[option_table][optionHighlighted].option_val)) == 0)
                    *(Option_Table[option_table][optionHighlighted].option_val) = Option_Table[option_table][optionHighlighted].option_max -1;
                else
                    *(Option_Table[option_table][optionHighlighted].option_val) = (*(Option_Table[option_table][optionHighlighted].option_val) - 1) % Option_Table[option_table][optionHighlighted].option_max;
                sprintf(strBuf, " %-12s : %-14s", Option_Table[option_table][optionHighlighted].label, Option_Table[option_table][optionHighlighted].option[*(Option_Table[option_table][optionHighlighted].option_val)]);
                DSPrint(1,5+optionHighlighted,2, strBuf);
            }
            if (keysCurrent() & KEY_START)  // Save Options
            {
                SaveConfig(TRUE);
            }
            if ((keysCurrent() & KEY_B) || (keysCurrent() & KEY_A))  // Exit options
            {
                option_table = 0;   // Reset for next time
                break;
            }
        }
        swiWaitForVBlank();
    }

    // Give a third of a second time delay...
    for (int i=0; i<20; i++)
    {
        swiWaitForVBlank();
    }

    return;
}
