#include "copyright.h"


#ifndef _spc7110_h
#define _spc7110_h
#include "port.h"
#include "spc7110dec.h"

#define DECOMP_BUFFER_SIZE	0x10000


extern void (*LoadUp7110)(char*);
extern void (*CleanUp7110)(void);
extern void (*Copy7110)(void);

extern uint16 cacheMegs;

void Del7110Gfx(void);
void Close7110Gfx(void);
void Drop7110Gfx(void);
extern "C"{
uint8 S9xGetSPC7110(uint16 Address);
uint8 S9xGetSPC7110Byte(uint32 Address);
uint8* Get7110BasePtr(uint32);
}
void S9xSetSPC7110 (uint8 data, uint16 Address);
void S9xSpc7110Init();
uint8* Get7110BasePtr(uint32);
void S9xSpc7110Reset();
void S9xUpdateRTC ();
void Do7110Logging();
int	S9xRTCDaysInMonth( int month, int year );

//These are platform-dependant functions, but should work on
//most systems that use GNU compilers, and on Win32.
void SPC7110Load(char*);
void SPC7110Open(char*);
void SPC7110Grab(char*);

typedef struct SPC7110RTC
{
	unsigned char reg[16];
	short index;
	uint8 control;
	bool init;
	time_t last_used;
} S7RTC;

typedef struct SPC7110EmuVars
{
	unsigned char reg4800;
	unsigned char reg4801;
	unsigned char reg4802;
	unsigned char reg4803;
	unsigned char reg4804;
	unsigned char reg4805;
	unsigned char reg4806;
	unsigned char reg4807;
	unsigned char reg4808;
	unsigned char reg4809;
	unsigned char reg480A;
	unsigned char reg480B;
	unsigned char reg480C;
	unsigned char reg4811;
	unsigned char reg4812;
	unsigned char reg4813;
	unsigned char reg4814;
	unsigned char reg4815;
	unsigned char reg4816;
	unsigned char reg4817;
	unsigned char reg4818;
	unsigned char reg4820;
	unsigned char reg4821;
	unsigned char reg4822;
	unsigned char reg4823;
	unsigned char reg4824;
	unsigned char reg4825;
	unsigned char reg4826;
	unsigned char reg4827;
	unsigned char reg4828;
	unsigned char reg4829;
	unsigned char reg482A;
	unsigned char reg482B;
	unsigned char reg482C;
	unsigned char reg482D;
	unsigned char reg482E;
	unsigned char reg482F;
	unsigned char reg4830;
	unsigned char reg4831;
	unsigned char reg4832;
	unsigned char reg4833;
	unsigned char reg4834;
	unsigned char reg4840;
	unsigned char reg4841;
	unsigned char reg4842;
	uint8 AlignBy;
	uint8 written;
	uint8 offset_add;
	uint32 DataRomOffset;
	uint32 DataRomSize;
	uint32 bank50Internal;
	uint8 bank50[DECOMP_BUFFER_SIZE];

} SPC7110Regs;
extern SPC7110Regs s7r;
extern S7RTC rtc_f9;
extern SPC7110Decomp decomp;

void S9xSpc7110PreSaveState();
void S9xSpc7110PostLoadState();

// These are defined in spc7110.cpp
bool8 S9xSaveSPC7110RTC (S7RTC *rtc_f9);
bool8 S9xLoadSPC7110RTC (S7RTC *rtc_f9);

#endif

