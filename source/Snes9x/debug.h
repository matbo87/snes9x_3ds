#include "copyright.h"


#ifndef _DEBUG_H_
#define _DEBUG_H_

START_EXTERN_C
void S9xDoDebug ();
void S9xTrace ();
void S9xSA1Trace ();
void S9xTraceMessage (const char *);

// Structures
struct SBreakPoint{
	bool8   Enabled;
	uint8  Bank;
	uint16 Address;
};

uint8 S9xOPrint( char *Line, uint8 Bank, uint16 Address);
uint8 S9xOPrintLong (char *Line, uint8 Bank, uint16 Address);
uint8 S9xSA1OPrint( char *Line, uint8 Bank, uint16 Address);

extern struct SBreakPoint S9xBreakpoint[ 6];
extern char *S9xMnemonics[256];
END_EXTERN_C
#endif

