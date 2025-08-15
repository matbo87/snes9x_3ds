#include "copyright.h"


#ifndef _PORT_H_
#define _PORT_H_

#include <limits.h>
#include <string.h>
#include <sys/types.h>

/* #define PIXEL_FORMAT RGB565 */
#define GFX_MULTI_FORMAT


#ifndef snes9x_types_defined
#define snes9x_types_defined

typedef unsigned char bool8;

/* FIXME: Refactor this by moving out the BORLAND part and unifying typedefs */

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef signed char int8;
typedef short int16;
typedef int int32;
typedef unsigned int uint32;
# ifdef __GNUC__  /* long long is not part of ISO C++ */
__extension__
# endif
typedef long long int64;

#endif /* snes9x_types_defined */


#include "pixform.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifdef STORM
#define EXTERN_C
#define START_EXTERN_C
#define END_EXTERN_C
#else
#if defined(__cplusplus) || defined(c_plusplus)
#define EXTERN_C extern "C"
#define START_EXTERN_C extern "C" {
#define END_EXTERN_C }
#else
#define EXTERN_C extern
#define START_EXTERN_C
#define END_EXTERN_C
#endif
#endif


#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define _MAX_DIR PATH_MAX
#define _MAX_DRIVE 1
#define _MAX_FNAME PATH_MAX
#define _MAX_EXT PATH_MAX
#define _MAX_PATH PATH_MAX

#define ZeroMemory(a,b) memset((a),0,(b))

void _makepath (char *path, const char *drive, const char *dir,
		const char *fname, const char *ext);
void _splitpath (const char *path, char *drive, char *dir, char *fname,
		 char *ext);



EXTERN_C void S9xGenerateSound ();

#ifdef STORM
EXTERN_C int soundsignal;
EXTERN_C void MixSound(void);
/* Yes, CHECK_SOUND is getting defined correctly! */
#define CHECK_SOUND if (Settings.APUEnabled) if(SetSignalPPC(0L, soundsignal) & soundsignal) MixSound
#else
#define CHECK_SOUND()
#endif


#define SLASH_STR "/"
#define SLASH_CHAR '/'


/* Taken care of in signal.h on Linux.
 * #ifdef __linux
 * typedef void (*SignalHandler)(int);
 * #define SIG_PF SignalHandler
 * #endif
 */

/* If including signal.h, do it before snes9.h and port.h to avoid clashes. */
#ifndef SIG_PF
#define SIG_PF void(*)(int)
#endif

#if defined(__i386__) || defined(__i486__) || defined(__i586__) || \
	defined(__WIN32__) || defined(__alpha__) || defined(__3DS__)
#define LSB_FIRST
#define FAST_LSB_WORD_ACCESS
#else
#define MSB_FIRST
#endif



#ifdef STORM
#define STATIC
#define strncasecmp strnicmp
#else
#define STATIC static
#endif

#endif

