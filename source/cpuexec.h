#include "copyright.h"


#ifndef _CPUEXEC_H_
#define _CPUEXEC_H_
#include "ppu.h"
#include "memmap.h"
#include "65c816.h"

#define DO_HBLANK_CHECK() \
    if (CPU.Cycles >= CPU.NextEvent) \
	S9xDoHBlankProcessing ();

struct SOpcodes {
#ifdef __WIN32__
	void (__cdecl *S9xOpcode)( void);
#else
	void (*S9xOpcode)( void);
#endif
};
/*
struct SICPU
{
    uint8  *Speed;
    struct SOpcodes *S9xOpcodes;
    uint8  _Carry;
    uint8  _Zero;
    uint8  _Negative;
    uint8  _Overflow;
    bool8  CPUExecuting;
    uint32 ShiftedPB;
    uint32 ShiftedDB;
    uint32 Frame;
    uint32 Scanline;
    uint32 FrameAdvanceCount;
};
*/

START_EXTERN_C
void S9xMainLoop (void);
void S9xMainLoopWithSA1 (void);
void S9xReset (void);
void S9xSoftReset (void);
void S9xDoHBlankProcessing ();
void S9xClearIRQ (uint32);
void S9xSetIRQ (uint32);

extern struct SOpcodes S9xOpcodesE1 [256];
extern struct SOpcodes S9xOpcodesM1X1 [256];
extern struct SOpcodes S9xOpcodesM1X0 [256];
extern struct SOpcodes S9xOpcodesM0X1 [256];
extern struct SOpcodes S9xOpcodesM0X0 [256];

void S9xUseInstructionSet(int set);

//extern struct SICPU ICPU;
#define ICPU OCPU.FastICPU
END_EXTERN_C


STATIC inline void S9xUnpackStatus()
{
    ICPU._Zero = (Registers.PL & Zero) == 0;
    ICPU._Negative = (Registers.PL & Negative);
    ICPU._Carry = (Registers.PL & Carry);
    ICPU._Overflow = (Registers.PL & Overflow) >> 6;
}

STATIC inline void S9xPackStatus()
{
    Registers.PL &= ~(Zero | Negative | Carry | Overflow);
    Registers.PL |= ICPU._Carry | ((ICPU._Zero == 0) << 1) |
		    (ICPU._Negative & 0x80) | (ICPU._Overflow << 6);
}

STATIC inline void CLEAR_IRQ_SOURCE (uint32 M)
{
    CPU.IRQActive &= ~M;
    if (!CPU.IRQActive)
	CPU.Flags &= ~IRQ_PENDING_FLAG;
}
	
STATIC inline void S9xFixCycles ()
{
    if (CheckEmulation ())
    {
	    ICPU.S9xOpcodes = S9xOpcodesE1;
    }
    else
        if (CheckMemory ())
        {
            if (CheckIndex ())
            {
                ICPU.S9xOpcodes = S9xOpcodesM1X1;
            }
            else
            {
                ICPU.S9xOpcodes = S9xOpcodesM1X0;
            }
        }
        else
        {
            if (CheckIndex ())
            {
                ICPU.S9xOpcodes = S9xOpcodesM0X1;
            }
            else
            {
                ICPU.S9xOpcodes = S9xOpcodesM0X0;
            }
        }
}

STATIC inline void S9xReschedule ()
{
    uint8 which;
    long max;
    
    if (CPU.WhichEvent == HBLANK_START_EVENT ||
        CPU.WhichEvent == HTIMER_AFTER_EVENT)
    {
	    which = HBLANK_END_EVENT;
	    max = Settings.H_Max;
    }
    else
    {
	    which = HBLANK_START_EVENT;
	    max = Settings.HBlankStart;
    }

    if (PPU.HTimerEnabled &&
        (long) PPU.HTimerPosition < max &&
        (long) PPU.HTimerPosition > CPU.NextEvent &&
        (!PPU.VTimerEnabled ||
        (PPU.VTimerEnabled && CPU.V_Counter == PPU.IRQVBeamPos)))
    {
        which = (long) PPU.HTimerPosition < Settings.HBlankStart ?
                HTIMER_BEFORE_EVENT : HTIMER_AFTER_EVENT;
        max = PPU.HTimerPosition;
    }
    
    //printf("NextEvent: %ld\n", max); 
    CPU.NextEvent = max;
    CPU.WhichEvent = which;
}

#endif

