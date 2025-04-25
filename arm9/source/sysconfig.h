/* sysconfig.h.in.  Generated from configure.in by autoheader.  */

/* Define if you have the <dirent.h> header file, and it defines `DIR'. */
#define HAVE_DIRENT_H 1

/* Define if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define if you have the `gettimeofday' function. */
#define HAVE_GETTIMEOFDAY 1

/* Define if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define if you have the `mkdir' function. */
#define HAVE_MKDIR 1

/* Define if you have the <ndir.h> header file, and it defines `DIR'. */
#undef HAVE_NDIR_H

/* Define if you have the `rmdir' function. */
#define HAVE_RMDIR 1

/* Define if you have the `select' function. */
#undef HAVE_SELECT

/* Define if you have the `statfs' function. */
#undef HAVE_STATFS

/* Define if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define if you have the `strstr' function. */
#define HAVE_STRSTR 1

/* Define if `st_blocks' is member of `struct stat'. */
#define HAVE_STRUCT_STAT_ST_BLOCKS 1

/* Define if you have the <sys/dir.h> header file, and it defines `DIR'. */
#define HAVE_SYS_DIR_H 1

/* Define if you have the <sys/mount.h> header file. */
#undef HAVE_SYS_MOUNT_H

/* Define if you have the <sys/ndir.h> header file, and it defines `DIR'. */
#undef HAVE_SYS_NDIR_H

/* Define if you have the <sys/param.h> header file. */
#define HAVE_SYS_PARAM_H 1

/* Define if you have the <sys/select.h> header file. */
#define HAVE_SYS_SELECT_H 1

/* Define if you have the <sys/statfs.h> header file. */
#undef HAVE_SYS_STATFS_H

/* Define if you have the <sys/statvfs.h> header file. */
#define HAVE_SYS_STATVFS_H 1

/* Define if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define if you have the <sys/vfs.h> header file. */
#undef HAVE_SYS_VFS_H

/* Define if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define if you have the `usleep' function. */
#undef HAVE_USLEEP

/* Define if you have the <utime.h> header file. */
#undef HAVE_UTIME_H

/* Define if `utime(file, NULL)' sets file's timestamp to the present. */
#undef HAVE_UTIME_NULL

/* Define if you have the <values.h> header file. */
#undef HAVE_VALUES_H

/* The size of a `char', as computed by sizeof. */
#define SIZEOF_CHAR 1

/* The size of a `int', as computed by sizeof. */
#define SIZEOF_INT 4

/* The size of a `long', as computed by sizeof. */
#define SIZEOF_LONG 4

/* The size of a `long long', as computed by sizeof. */
#define SIZEOF_LONG_LONG 8

/* The size of a `short', as computed by sizeof. */
#define SIZEOF_SHORT 2

/* Define if you have the ANSI C header files. */
#undef STDC_HEADERS

/* Define if you can safely include both <sys/time.h> and <time.h>. */
#undef TIME_WITH_SYS_TIME

/* Define if your <sys/time.h> declares `struct tm'. */
#undef TM_IN_SYS_TIME

// Use iprintf/iscanf on NDS to save ~50 KB
#define sscanf    siscanf
#define printf    iprintf
#define fprintf   fiprintf
#define sprintf   siprintf
#define snprintf  sniprintf
#define vsnprintf vsniprintf
