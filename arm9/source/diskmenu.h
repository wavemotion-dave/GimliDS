#define MAX_FILES                   2048
#define MAX_FILENAME_LEN            256

#define NORMALFILE                  0x01
#define DIRECTORY                   0x02
class C64;
extern void DSPrint(int iX,int iY,int iScr,char *szMessage);

typedef struct {
  char szName[MAX_FILENAME_LEN+1];
  u8 uType;
  u32 uCrc;
} FIC64;

extern char Drive8File[MAX_FILENAME_LEN];
extern char Drive9File[MAX_FILENAME_LEN];
extern char CartFilename[MAX_FILENAME_LEN];

extern FIC64 gpFic[MAX_FILES];
extern int   ucGameChoice;

extern u8  mount_disk(C64 *the_c64);
extern u8  mount_cart(C64 *the_c64);
