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

// Diskette Menu Loading Code

#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/dir.h>
#include "diskmenu.h"
#include "Display.h"
#include "mainmenu.h"
#include "keyboard.h"
#include "mainmenu_bg.h"
#include "diskmenu_bg.h"
#include "cartmenu_bg.h"
#include "Prefs.h"
#include "C64.h"
#include "printf.h"

char Drive8File[MAX_FILENAME_LEN];
char Drive9File[MAX_FILENAME_LEN];
char CartFilename[MAX_FILENAME_LEN];

extern int bg0b, bg1b;
int         diskCount=0;
int         diskGameAct=0;
int         diskGameChoice = -1;
FIC64       gpFic[MAX_FILES];
char        szName[256];
char        szFile[256];
u32         file_size = 0;
char        strBuf[40];
u8          bLastFileTypeLoaded = 0;

#define WAITVBL swiWaitForVBlank();swiWaitForVBlank();swiWaitForVBlank();

extern u8 bDebugDisplay;

/*********************************************************************************
 * Show The 14 games on the list to allow the user to choose a new game.
 ********************************************************************************/
static char szName2[40];
void dsDisplayFiles(u16 NoDebGame, u8 ucSel)
{
  u16 ucBcl,ucGame;
  u8 maxLen;

  DSPrint(31,5,0,(NoDebGame>0 ? (char*)"<" : (char*)" "));
  DSPrint(31,22,0,(NoDebGame+14<diskCount ? (char*)">" : (char*)" "));

  for (ucBcl=0;ucBcl<18; ucBcl++)
  {
    ucGame= ucBcl+NoDebGame;
    if (ucGame < diskCount)
    {
      maxLen=(int)strlen(gpFic[ucGame].szName);
      strcpy(szName,gpFic[ucGame].szName);
      if (maxLen>30) szName[30]='\0';
      if (gpFic[ucGame].uType == DIRECTORY)
      {
        szName[28] = 0; // Needs to be 2 chars shorter with brackets
        sprintf(szName2, "[%s]",szName);
        sprintf(szName,"%-30s",szName2);
        DSPrint(1,5+ucBcl,(ucSel == ucBcl ? 2 :  0),szName);
      }
      else
      {
        sprintf(szName,"%-30s",strupr(szName));
        DSPrint(1,5+ucBcl,(ucSel == ucBcl ? 2 : 0 ),szName);
      }
    }
    else
    {
        DSPrint(1,5+ucBcl,(ucSel == ucBcl ? 2 : 0 ),(char *)"                              ");
    }
  }
}


// -------------------------------------------------------------------------
// Standard qsort routine for the games - we sort all directory
// listings first and then a case-insenstive sort of all games.
// -------------------------------------------------------------------------
int Filescmp (const void *c1, const void *c2)
{
  FIC64 *p1 = (FIC64 *) c1;
  FIC64 *p2 = (FIC64 *) c2;

  if (p1->szName[0] == '.' && p2->szName[0] != '.')
      return -1;
  if (p2->szName[0] == '.' && p1->szName[0] != '.')
      return 1;
  if ((p1->uType == DIRECTORY) && !(p2->uType == DIRECTORY))
      return -1;
  if ((p2->uType == DIRECTORY) && !(p1->uType == DIRECTORY))
      return 1;
  return strcasecmp (p1->szName, p2->szName);
}

/*********************************************************************************
 * Find files (TAP/TZX/Z80/SNA) available - sort them for display.
 ********************************************************************************/
void gimliDSFindFiles(u8 bCartOnly)
{
  u32 uNbFile;
  DIR *dir;
  struct dirent *pent;

  uNbFile=0;
  diskCount=0;

  dir = opendir(".");
  while (((pent=readdir(dir))!=NULL) && (uNbFile<MAX_FILES))
  {
    strcpy(szFile,pent->d_name);

    if(pent->d_type == DT_DIR)
    {
      if (!((szFile[0] == '.') && ((int)strlen(szFile) == 1)))
      {
        // Do not include the [sav] and [pok] directories
        if ((strcasecmp(szFile, "sav") != 0))
        {
            strcpy(gpFic[uNbFile].szName,szFile);
            gpFic[uNbFile].uType = DIRECTORY;
            uNbFile++;
            diskCount++;
        }
      }
    }
    else {
      if ((strlen(szFile)>4) && (strlen(szFile)<(MAX_FILENAME_LEN-4)) && (szFile[0] != '.') && (szFile[0] != '_'))  // For MAC don't allow files starting with an underscore
      {
        if (bCartOnly)
        {
            if ( (strcasecmp(strrchr(szFile, '.'), ".CRT") == 0) )  
            {
              strcpy(gpFic[uNbFile].szName,szFile);
              gpFic[uNbFile].uType = NORMALFILE;
              uNbFile++;
              diskCount++;
            }
            
            if ( (strcasecmp(strrchr(szFile, '.'), ".PRG") == 0) )
            {
              strcpy(gpFic[uNbFile].szName,szFile);
              gpFic[uNbFile].uType = NORMALFILE;
              uNbFile++;
              diskCount++;
            }
        }
        else
        {
            if ( (strcasecmp(strrchr(szFile, '.'), ".D64") == 0) )  
            {
              strcpy(gpFic[uNbFile].szName,szFile);
              gpFic[uNbFile].uType = NORMALFILE;
              uNbFile++;
              diskCount++;
            }
        }
      }
    }
  }
  closedir(dir);

  // ----------------------------------------------
  // If we found any files, go sort the list...
  // ----------------------------------------------
  if (diskCount)
  {
    qsort (gpFic, diskCount, sizeof(FIC64), Filescmp);
  }
}

// ----------------------------------------------------------------
// Let the user select a new game (rom) file and load it up!
// ----------------------------------------------------------------
u8 gimliDSLoadFile(u8 bCartOnly)
{
  bool bDone=false;
  u16 ucHaut=0x00, ucBas=0x00,ucSHaut=0x00, ucSBas=0x00, romSelected= 0, firstRomDisplay=0,nbRomPerPage, uNbRSPage;
  s16 uLenFic=0, ucFlip=0, ucFlop=0;
  u8 retVal = 0;
  
  if (bLastFileTypeLoaded != bCartOnly)
  {
     diskCount=0;
     diskGameAct=0;
  }

  // Show the menu...
  while ((keysCurrent() & (KEY_TOUCH | KEY_START | KEY_SELECT | KEY_A | KEY_B))!=0)
  {
      currentBrightness = 0;
  }

  gimliDSFindFiles(bCartOnly);

  diskGameChoice = -1;

  nbRomPerPage = (diskCount>=18 ? 18 : diskCount);
  uNbRSPage = (diskCount>=5 ? 5 : diskCount);

  if (diskGameAct>diskCount-nbRomPerPage)
  {
    firstRomDisplay=diskCount-nbRomPerPage;
    romSelected=diskGameAct-diskCount+nbRomPerPage;
  }
  else
  {
    firstRomDisplay=diskGameAct;
    romSelected=0;
  }

  if (romSelected >= diskCount) romSelected = 0; // Just start at the top

  dsDisplayFiles(firstRomDisplay,romSelected);

  // -----------------------------------------------------
  // Until the user selects a file or exits the menu...
  // -----------------------------------------------------
  while (!bDone)
  {
    currentBrightness = 0;
    if (keysCurrent() & KEY_UP)
    {
      if (!ucHaut)
      {
        diskGameAct = (diskGameAct>0 ? diskGameAct-1 : diskCount-1);
        if (romSelected>uNbRSPage) { romSelected -= 1; }
        else {
          if (firstRomDisplay>0) { firstRomDisplay -= 1; }
          else {
            if (romSelected>0) { romSelected -= 1; }
            else {
              firstRomDisplay=diskCount-nbRomPerPage;
              romSelected=nbRomPerPage-1;
            }
          }
        }
        ucHaut=0x01;
        dsDisplayFiles(firstRomDisplay,romSelected);
      }
      else {

        ucHaut++;
        if (ucHaut>10) ucHaut=0;
      }
      uLenFic=0; ucFlip=-50; ucFlop=0;
    }
    else
    {
      ucHaut = 0;
    }
    if (keysCurrent() & KEY_DOWN)
    {
      if (!ucBas) {
        diskGameAct = (diskGameAct< diskCount-1 ? diskGameAct+1 : 0);
        if (romSelected<uNbRSPage-1) { romSelected += 1; }
        else {
          if (firstRomDisplay<diskCount-nbRomPerPage) { firstRomDisplay += 1; }
          else {
            if (romSelected<nbRomPerPage-1) { romSelected += 1; }
            else {
              firstRomDisplay=0;
              romSelected=0;
            }
          }
        }
        ucBas=0x01;
        dsDisplayFiles(firstRomDisplay,romSelected);
      }
      else
      {
        ucBas++;
        if (ucBas>10) ucBas=0;
      }
      uLenFic=0; ucFlip=-50; ucFlop=0;
    }
    else {
      ucBas = 0;
    }

    // -------------------------------------------------------------
    // Left and Right on the D-Pad will scroll 1 page at a time...
    // -------------------------------------------------------------
    if (keysCurrent() & KEY_RIGHT)
    {
      if (!ucSBas)
      {
        diskGameAct = (diskGameAct< diskCount-nbRomPerPage ? diskGameAct+nbRomPerPage : diskCount-nbRomPerPage);
        if (firstRomDisplay<diskCount-nbRomPerPage) { firstRomDisplay += nbRomPerPage; }
        else { firstRomDisplay = diskCount-nbRomPerPage; }
        if (diskGameAct == diskCount-nbRomPerPage) romSelected = 0;
        ucSBas=0x01;
        dsDisplayFiles(firstRomDisplay,romSelected);
      }
      else
      {
        ucSBas++;
        if (ucSBas>10) ucSBas=0;
      }
      uLenFic=0; ucFlip=-50; ucFlop=0;
    }
    else {
      ucSBas = 0;
    }

    // -------------------------------------------------------------
    // Left and Right on the D-Pad will scroll 1 page at a time...
    // -------------------------------------------------------------
    if (keysCurrent() & KEY_LEFT)
    {
      if (!ucSHaut)
      {
        diskGameAct = (diskGameAct> nbRomPerPage ? diskGameAct-nbRomPerPage : 0);
        if (firstRomDisplay>nbRomPerPage) { firstRomDisplay -= nbRomPerPage; }
        else { firstRomDisplay = 0; }
        if (diskGameAct == 0) romSelected = 0;
        if (romSelected > diskGameAct) romSelected = diskGameAct;
        ucSHaut=0x01;
        dsDisplayFiles(firstRomDisplay,romSelected);
      }
      else
      {
        ucSHaut++;
        if (ucSHaut>10) ucSHaut=0;
      }
      uLenFic=0; ucFlip=-50; ucFlop=0;
    }
    else {
      ucSHaut = 0;
    }

    // -------------------------------------------------------------------------
    // They B key will exit out of the ROM selection without picking a new game
    // -------------------------------------------------------------------------
    if ( keysCurrent() & KEY_B )
    {
      bDone=true;
      retVal = 0;
      while (keysCurrent() & KEY_B);
    }

    // -------------------------------------------------------------------
    // Any of these keys will pick the current ROM and try to load it...
    // -------------------------------------------------------------------
    if (keysCurrent() & KEY_A || keysCurrent() & KEY_Y || keysCurrent() & KEY_X)
    {
      if (gpFic[diskGameAct].uType != DIRECTORY)
      {
        bDone=true;
        if (keysCurrent() & KEY_X) retVal = 2; else retVal = 1;
        if (keysCurrent() & KEY_Y) bDebugDisplay = 1; else bDebugDisplay = 0;
        diskGameChoice = diskGameAct;
        WAITVBL;
      }
      else
      {
        chdir(gpFic[diskGameAct].szName);
        gimliDSFindFiles(bCartOnly);
        diskGameAct = 0;
        nbRomPerPage = (diskCount>=14 ? 14 : diskCount);
        uNbRSPage = (diskCount>=5 ? 5 : diskCount);
        if (diskGameAct>diskCount-nbRomPerPage) {
          firstRomDisplay=diskCount-nbRomPerPage;
          romSelected=diskGameAct-diskCount+nbRomPerPage;
        }
        else {
          firstRomDisplay=diskGameAct;
          romSelected=0;
        }
        dsDisplayFiles(firstRomDisplay,romSelected);
        while (keysCurrent() & KEY_A);
      }
    }

    // --------------------------------------------
    // If the filename is too long... scroll it.
    // --------------------------------------------
    if ((int)strlen(gpFic[diskGameAct].szName) > 30)
    {
      ucFlip++;
      if (ucFlip >= 25)
      {
        ucFlip = 0;
        uLenFic++;
        if ((uLenFic+30)>(int)strlen(gpFic[diskGameAct].szName))
        {
          ucFlop++;
          if (ucFlop >= 15)
          {
            uLenFic=0;
            ucFlop = 0;
          }
          else
            uLenFic--;
        }
        strncpy(szName,gpFic[diskGameAct].szName+uLenFic,30);
        szName[30] = '\0';
        DSPrint(1,5+romSelected,2,szName);
      }
    }
    swiWaitForVBlank();
  }

  // Wait for some key to be pressed before returning
  while ((keysCurrent() & (KEY_TOUCH | KEY_START | KEY_SELECT | KEY_A | KEY_B | KEY_R | KEY_L | KEY_UP | KEY_DOWN))!=0);

  return retVal;
}

u16 nds_key;

void LoadGameConfig(void)
{
    // Cart overrides disk...
    if (strlen(CartFilename) > 1)
    {
        file_crc = getCRC32((u8*)CartFilename, strlen(CartFilename));
        FindConfig();
    }
    else if (strlen(Drive8File) > 1)
    {
        file_crc = getCRC32((u8*)Drive8File, strlen(Drive8File));
        FindConfig();
    }    
}

void BottomScreenDiskette(void)
{
    decompress(diskmenu_bgTiles, bgGetGfxPtr(bg0b),  LZ77Vram);
    decompress(diskmenu_bgMap, (void*) bgGetMapPtr(bg0b),  LZ77Vram);
    dmaCopy((void*) bgGetMapPtr(bg0b)+32*30*2,(void*) bgGetMapPtr(bg1b),32*24*2);
    dmaCopy((void*) diskmenu_bgPal,(void*) BG_PALETTE_SUB,256*2);
    unsigned  short dmaVal = *(bgGetMapPtr(bg1b)+24*32);
    dmaFillWords(dmaVal | (dmaVal<<16),(void*)  bgGetMapPtr(bg1b),32*24*2);
}

void BottomScreenCartridge(void)
{
    decompress(cartmenu_bgTiles, bgGetGfxPtr(bg0b),  LZ77Vram);
    decompress(cartmenu_bgMap, (void*) bgGetMapPtr(bg0b),  LZ77Vram);
    dmaCopy((void*) bgGetMapPtr(bg0b)+32*30*2,(void*) bgGetMapPtr(bg1b),32*24*2);
    dmaCopy((void*) cartmenu_bgPal,(void*) BG_PALETTE_SUB,256*2);
    unsigned  short dmaVal = *(bgGetMapPtr(bg1b)+24*32);
    dmaFillWords(dmaVal | (dmaVal<<16),(void*)  bgGetMapPtr(bg1b),32*24*2);
}

void BottomScreenMainMenu(void)
{
    decompress(mainmenu_bgTiles, bgGetGfxPtr(bg0b),  LZ77Vram);
    decompress(mainmenu_bgMap, (void*) bgGetMapPtr(bg0b),  LZ77Vram);
    dmaCopy((void*) bgGetMapPtr(bg0b)+32*30*2,(void*) bgGetMapPtr(bg1b),32*24*2);
    dmaCopy((void*) mainmenu_bgPal,(void*) BG_PALETTE_SUB,256*2);
    unsigned  short dmaVal = *(bgGetMapPtr(bg1b)+24*32);
    dmaFillWords(dmaVal | (dmaVal<<16),(void*)  bgGetMapPtr(bg1b),32*24*2);
}

void DisplayFileNameDiskette(void)
{
    char tmp[34];

    DSPrint(5,6,6,  (char*)"                           ");
    DSPrint(5,10,6, (char*)"                           ");
    if (strlen(Drive8File) > 1)
    {
        DSPrint(5,5,6, (char *)"DRIVE 8 IS MOUNTED WITH:");
        strncpy(tmp, Drive8File, 27); tmp[26]=0;
        DSPrint(5,6,6, tmp);
    }
    else
    {
        DSPrint(5,5,6, (char *)"DRIVE 8 IS NOT MOUNTED ");
    }
    
    if (strlen(Drive9File) > 1)
    {
        DSPrint(5,9,6, (char *)"DRIVE 9 IS MOUNTED WITH:");
        strncpy(tmp, Drive9File, 27); tmp[26]=0;
        DSPrint(5,10,6, tmp);
    }
    else
    {
        DSPrint(5,9,6, (char *)"DRIVE 9 IS NOT MOUNTED ");
    }
    
    if (myConfig.trueDrive)
    {
        DSPrint(0,22,6, (char *)"  TRUE DRIVE IS ENABLED (SLOW) ");
    }
    else
    {
        DSPrint(0,22,6, (char *)" TRUE DRIVE IS DISABLED (FAST) ");
    }
}

void DisplayFileNameCartridge(void)
{
    char tmp[34];

    DSPrint(7,5,6, (char*)"                         ");
    DSPrint(7,7,6, (char*)"                         ");
    if (strlen(CartFilename) > 1)
    {
        DSPrint(7,5,6, (char *)"CARTRIDGE IS MOUNTED AS:");
        strncpy(tmp, CartFilename, 25); tmp[24]=0;
        DSPrint(7,7,6, tmp);
    }
    else
    {
        DSPrint(7,5,6, (char *)"CARTRIDGE IS NOT MOUNTED");
    }
}

// ----------------------------------------------------------------------
// The Disk Menu can be called up directly from the keyboard graphic
// and allows the user to rewind the tape, swap in a new tape, etc.
// ----------------------------------------------------------------------
#define MENU_ACTION_END             255 // Always the last sentinal value
#define MENU_ACTION_EXIT            0   // Exit the menu
#define MENU_ACTION_DRIVE8          1   // Mount Drive 8 File
#define MENU_ACTION_DRIVE9          2   // Mount Drive 9 File
#define MENU_ACTION_EJECT           3   // Eject all drives
#define MENU_ACTION_REBOOT_C64      4   // Force C64 Reboot
#define MENU_ACTION_TRUE_DRIVE      5   // Toggle True Drive
#define MENU_ACTION_CONFIG          6   // Configure Game

#define MENU_ACTION_INSERT_CART     10
#define MENU_ACTION_REMOVE_CART     11

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
} DiskMenu_t;

DiskMenu_t disk_menu =
{
    (char *)" ", 10,
    {
        {(char *)"  MOUNT   DISK 8    ",      MENU_ACTION_DRIVE8},
        {(char *)"  MOUNT   DISK 9    ",      MENU_ACTION_DRIVE9},
        {(char *)"  EJECT   DISKS     ",      MENU_ACTION_EJECT},
        {(char *)"  TOGGLE  TRUEDRIVE ",      MENU_ACTION_TRUE_DRIVE},
        {(char *)"  CONFIG  GAME      ",      MENU_ACTION_CONFIG},
        {(char *)"  RESET   C64       ",      MENU_ACTION_REBOOT_C64},
        {(char *)"  EXIT    MENU      ",      MENU_ACTION_EXIT},
        {(char *)"  NULL              ",      MENU_ACTION_END},
    },
};

DiskMenu_t cart_menu =
{
    (char *)" ", 11,
    {
        {(char *)"  INSERT  CARTRIDGE ",      MENU_ACTION_INSERT_CART},
        {(char *)"  REMOVE  CARTRIDGE ",      MENU_ACTION_REMOVE_CART},
        {(char *)"  EXIT    MENU      ",      MENU_ACTION_EXIT},
        {(char *)"  NULL              ",      MENU_ACTION_END},
    },
};


static DiskMenu_t *menu = &disk_menu;

// -------------------------------------------------------
// Show the Disk Menu text - highlight the selected row.
// -------------------------------------------------------
u8 diskette_menu_items = 0;
void DiskMenuShow(bool bClearScreen, u8 sel)
{
    diskette_menu_items = 0;

    if (bClearScreen)
    {
        // -------------------------------------
        // Put up the Diskette menu background
        // -------------------------------------
        BottomScreenDiskette();
    }

    // ---------------------------------------------------
    // Pick the right context menu based on the machine
    // ---------------------------------------------------
    menu = &disk_menu;

    // Display the menu title
    DSPrint(15-(strlen(menu->title)/2), menu->start_row, 6, menu->title);

    // And display all of the menu items
    while (menu->menulist[diskette_menu_items].menu_action != MENU_ACTION_END)
    {
        DSPrint(16-(strlen(menu->menulist[diskette_menu_items].menu_string)/2), menu->start_row+2+diskette_menu_items, (diskette_menu_items == sel) ? 7:6, menu->menulist[diskette_menu_items].menu_string);
        diskette_menu_items++;
    }

    // ----------------------------------------------------------------------------------------------
    // And near the bottom, display the file/rom/disk that is currently loaded into memory.
    // ----------------------------------------------------------------------------------------------
    DisplayFileNameDiskette();
}

// ------------------------------------------------------------------------
// Handle Disk mini-menu interface... Allows rewind, swap tape, etc.
// ------------------------------------------------------------------------
u8 DisketteMenu(C64 *the_c64)
{
  u8 menuSelection = 0;
  u8 retVal = 0;

  while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_A ))!=0);

  // ------------------------------------------------------------------
  //Show the cassette menu background - we'll draw text on top of this
  // ------------------------------------------------------------------
  DiskMenuShow(true, menuSelection);

  u8 bExitMenu = false;
  while (true)
  {
    currentBrightness = 0;
    nds_key = keysCurrent();
    if (nds_key)
    {
        if (nds_key & KEY_UP)
        {
            menuSelection = (menuSelection > 0) ? (menuSelection-1):(diskette_menu_items-1);
            while (menu->menulist[menuSelection].menu_action == MENU_ACTION_SKIP)
            {
                menuSelection = (menuSelection > 0) ? (menuSelection-1):(diskette_menu_items-1);
            }
            DiskMenuShow(false, menuSelection);
        }
        if (nds_key & KEY_DOWN)
        {
            menuSelection = (menuSelection+1) % diskette_menu_items;
            while (menu->menulist[menuSelection].menu_action == MENU_ACTION_SKIP)
            {
                menuSelection = (menuSelection+1) % diskette_menu_items;
            }
            DiskMenuShow(false, menuSelection);
        }
        
        if (nds_key & KEY_B) // Treat this as selecting 'exit'
        {
            bExitMenu = true;
        }
        else
        if (nds_key & KEY_A)    // User has picked a menu item... let's see what it is!
        {
            switch(menu->menulist[menuSelection].menu_action)
            {
                case MENU_ACTION_EXIT:
                    bExitMenu = true;
                    break;

                case MENU_ACTION_DRIVE8:
                    BottomScreenMainMenu();
                    retVal = gimliDSLoadFile(0);
                    if (diskGameChoice >= 0)
                    {
                        retVal = 1;
                        strcpy(Drive8File, gpFic[diskGameChoice].szName);
                        LoadGameConfig();
                    }
                    DiskMenuShow(true, menuSelection);
                    break;
                    
                case MENU_ACTION_DRIVE9:
                    BottomScreenMainMenu();
                    retVal = gimliDSLoadFile(0);
                    if (diskGameChoice >= 0)
                    {
                        retVal = 1;
                        strcpy(Drive9File, gpFic[diskGameChoice].szName);
                    }
                    DiskMenuShow(true, menuSelection);
                    break;

                case MENU_ACTION_EJECT:
                    strcpy(Drive8File, "");
                    strcpy(Drive9File, "");
                    retVal = 1;
                    DiskMenuShow(true, menuSelection);
                    break;

                case MENU_ACTION_TRUE_DRIVE:
                    myConfig.trueDrive ^= 1;
                    DiskMenuShow(true, menuSelection);
                    break;
                    
                case MENU_ACTION_CONFIG:
                    if (file_crc != 0x00000000)
                    {
                        u8 last_trueDrive = myConfig.trueDrive;
                        BottomScreenMainMenu();
                        GimliDSGameOptions();
                        if (last_trueDrive != myConfig.trueDrive) // Need to reload...
                        {
                            Prefs *prefs = new Prefs(ThePrefs);
                            prefs->TrueDrive = myConfig.trueDrive;
                            the_c64->NewPrefs(prefs);
                            ThePrefs = *prefs;
                            delete prefs;
                        }                        
                        DiskMenuShow(true, menuSelection);
                    }
                    else
                    {
                        DSPrint(0, 20, 6, (char*)"       NO GAME IS LOADED      ");
                        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
                        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
                        WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;WAITVBL;
                        DSPrint(0, 20, 6, (char*)"                              ");
                    }
                    break;
                    
                case MENU_ACTION_REBOOT_C64:
                    retVal = 2;
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

u8 mount_disk(C64 *the_c64)
{
    strcpy(Drive8File, ThePrefs.DrivePath[0]);
    strcpy(Drive9File, ThePrefs.DrivePath[1]);
    
    u8 retVal = DisketteMenu(the_c64);
    
    if (retVal) bLastFileTypeLoaded = 0;
    
    return retVal;
}

// -------------------------------------------------------
// Show the Disk Menu text - highlight the selected row.
// -------------------------------------------------------
void CartMenuShow(bool bClearScreen, u8 sel)
{
    diskette_menu_items = 0;

    if (bClearScreen)
    {
        // -------------------------------------
        // Put up the Cartridge menu background
        // -------------------------------------
        BottomScreenCartridge();
    }

    // ---------------------------------------------------
    // Pick the right context menu based on the machine
    // ---------------------------------------------------
    menu = &cart_menu;

    // Display the menu title
    DSPrint(15-(strlen(menu->title)/2), menu->start_row, 6, menu->title);

    // And display all of the menu items
    while (menu->menulist[diskette_menu_items].menu_action != MENU_ACTION_END)
    {
        DSPrint(16-(strlen(menu->menulist[diskette_menu_items].menu_string)/2), menu->start_row+2+diskette_menu_items, (diskette_menu_items == sel) ? 7:6, menu->menulist[diskette_menu_items].menu_string);
        diskette_menu_items++;
    }

    // ----------------------------------------------------------------------------------------------
    // And near the bottom, display the file/rom/disk that is currently loaded into memory.
    // ----------------------------------------------------------------------------------------------
    DisplayFileNameCartridge();
}


// ------------------------------------------------------------------------
// Handle Disk mini-menu interface... Allows rewind, swap tape, etc.
// ------------------------------------------------------------------------
u8 CartMenu(C64 *the_c64)
{
  u8 menuSelection = 0;
  u8 retVal = 0;

  while ((keysCurrent() & (KEY_TOUCH | KEY_LEFT | KEY_RIGHT | KEY_A ))!=0);

  // ------------------------------------------------------------------
  //Show the cassette menu background - we'll draw text on top of this
  // ------------------------------------------------------------------
  CartMenuShow(true, menuSelection);

  u8 bExitMenu = false;
  while (true)
  {
    currentBrightness = 0;
    nds_key = keysCurrent();
    if (nds_key)
    {
        if (nds_key & KEY_UP)
        {
            menuSelection = (menuSelection > 0) ? (menuSelection-1):(diskette_menu_items-1);
            while (menu->menulist[menuSelection].menu_action == MENU_ACTION_SKIP)
            {
                menuSelection = (menuSelection > 0) ? (menuSelection-1):(diskette_menu_items-1);
            }
            CartMenuShow(false, menuSelection);
        }
        if (nds_key & KEY_DOWN)
        {
            menuSelection = (menuSelection+1) % diskette_menu_items;
            while (menu->menulist[menuSelection].menu_action == MENU_ACTION_SKIP)
            {
                menuSelection = (menuSelection+1) % diskette_menu_items;
            }
            CartMenuShow(false, menuSelection);
        }
        
        if (nds_key & KEY_B) // Treat this as selecting 'exit'
        {
            bExitMenu = true;
        }
        else
        if (nds_key & KEY_A)    // User has picked a menu item... let's see what it is!
        {
            switch(menu->menulist[menuSelection].menu_action)
            {
                case MENU_ACTION_EXIT:
                    bExitMenu = true;
                    break;

                case MENU_ACTION_INSERT_CART:
                    BottomScreenMainMenu();
                    retVal = gimliDSLoadFile(1);
                    if (diskGameChoice >= 0)
                    {
                        retVal = 1;
                        strcpy(CartFilename, gpFic[diskGameChoice].szName);
                        if ( (strcasecmp(strrchr(CartFilename, '.'), ".PRG") == 0) ) retVal = 2;
                        LoadGameConfig();
                    }
                    bExitMenu = true;
                    break;
                    
                case MENU_ACTION_REMOVE_CART:
                    BottomScreenMainMenu();
                    retVal = 3;
                    strcpy(CartFilename, "");
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

u8 mount_cart(C64 *the_c64)
{
    u8 retVal = CartMenu(the_c64);
    
    if (retVal) bLastFileTypeLoaded = 1;

    return retVal;
}
