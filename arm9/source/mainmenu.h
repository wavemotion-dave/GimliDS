
#define MAX_CONFIGS                 1150
#define CONFIG_VERSION              0x0005

extern s16 CycleDeltas[];

struct __attribute__((__packed__)) Config_t
{
    u32 game_crc;
    u8  key_A;
    u8  key_B;
    u8  key_X;
    u8  key_Y;
    u8  trueDrive;
    u8  autoFire;
    u8  jitter;
    u8  diskSFX;
    u8  joyPort;
    u8  reserved2;
    u8  reserved3;
    u8  reserved4;
    u8  reserved5;
    u8  reserved6;
    u8  cpuCycles;
    u8  badCycles;
    s16 offsetX;
    s16 offsetY;
    s16 scaleX;
    s16 scaleY;
};

extern struct Config_t  myConfig;

#define KEY_MAP_JOY_FIRE   0
#define KEY_MAP_SPACE      1
#define KEY_MAP_RETURN     2
#define KEY_MAP_JOY_UP     3
#define KEY_MAP_JOY_DN     4
#define KEY_MAP_PAN_UP     5
#define KEY_MAP_PAN_DN     6

extern u32 getCRC32(u8 *buf, int size);
extern u32 file_crc;
void LoadConfig(void);
void SaveConfig(void);
void FindConfig(void);
void GimliDSGameOptions(void);
