
#define MAX_CONFIGS                 960
#define CONFIG_VERSION              0x0006

extern s16 CycleDeltas[];

struct __attribute__((__packed__)) Config_t
{
    u32 game_crc;
    u8  key_map[10];  // U,D,L,R, A,B,X,Y, +2 spares
    u8  trueDrive;
    u8  jitter;
    u8  diskSFX;
    u8  joyPort;
    u8  joyMode;
    u8  reserved2;
    u8  reserved3;
    u8  reserved4;
    u8  reserved5;
    u8  reserved6;
    u8  reserved7;
    u8  reserved8;
    u8  cpuCycles;
    u8  flopCycles;
    s8  offsetX;
    s8  offsetY;
    s16 scaleX;
    s16 scaleY;
};

extern struct Config_t  myConfig;

#define KEY_MAP_JOY_FIRE   0
#define KEY_MAP_JOY_UP     1
#define KEY_MAP_JOY_DOWN   2
#define KEY_MAP_JOY_LEFT   3
#define KEY_MAP_JOY_RIGHT  4
#define KEY_MAP_JOY_AUTO   5

#define KEY_MAP_SPACE      6
#define KEY_MAP_RETURN     7

#define KEY_MAP_RUNSTOP    8
#define KEY_MAP_COMMODORE  9
#define KEY_MAP_F1         10
#define KEY_MAP_F3         11
#define KEY_MAP_F5         12
#define KEY_MAP_F7         13
#define KEY_MAP_ASTERISK   14
#define KEY_MAP_EQUALS     15
#define KEY_MAP_PLUS       16
#define KEY_MAP_MINUS      17
#define KEY_MAP_PERIOD     18
#define KEY_MAP_COMMA      19
#define KEY_MAP_COLON      20
#define KEY_MAP_SEMI       21
#define KEY_MAP_SLASH      22
#define KEY_MAP_ATSIGN     23

#define KEY_MAP_A          24
#define KEY_MAP_B          25
#define KEY_MAP_C          26
#define KEY_MAP_D          27
#define KEY_MAP_E          28
#define KEY_MAP_F          29
#define KEY_MAP_G          30
#define KEY_MAP_H          31
#define KEY_MAP_I          32
#define KEY_MAP_J          33
#define KEY_MAP_K          34
#define KEY_MAP_L          35
#define KEY_MAP_M          36
#define KEY_MAP_N          37
#define KEY_MAP_O          38
#define KEY_MAP_P          39
#define KEY_MAP_Q          40
#define KEY_MAP_R          41
#define KEY_MAP_S          42
#define KEY_MAP_T          43
#define KEY_MAP_U          44
#define KEY_MAP_V          45
#define KEY_MAP_W          46
#define KEY_MAP_X          47
#define KEY_MAP_Y          48
#define KEY_MAP_Z          49
#define KEY_MAP_1          50
#define KEY_MAP_2          51
#define KEY_MAP_3          52
#define KEY_MAP_4          53
#define KEY_MAP_5          54
#define KEY_MAP_6          55
#define KEY_MAP_7          56
#define KEY_MAP_8          57
#define KEY_MAP_9          58
#define KEY_MAP_0          59

#define KEY_MAP_PAN_UP16   60
#define KEY_MAP_PAN_UP24   61
#define KEY_MAP_PAN_DN16   62
#define KEY_MAP_PAN_DN24   63

#define KEY_MAP_MAX        64

#define JOYMODE_NORMAL          0
#define JOYMODE_SLIDE_N_GLIDE   1

extern u32 getCRC32(u8 *buf, int size);
extern u32 file_crc;
void LoadConfig(void);
void SaveConfig(void);
void FindConfig(void);
void GimliDSGameOptions(void);
