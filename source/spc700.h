#include "copyright.h"


#ifndef _SPC700_H_
#define _SPC700_H_


#define Carry       1
#define Zero        2
#define Interrupt   4
#define HalfCarry   8
#define BreakFlag  16
#define DirectPageFlag 32
#define Overflow   64
#define Negative  128

#define APUClearCarry() (IAPU._Carry = 0)
#define APUSetCarry() (IAPU._Carry = 1)
#define APUSetInterrupt() (APURegisters.P |= Interrupt)
#define APUClearInterrupt() (APURegisters.P &= ~Interrupt)
#define APUSetHalfCarry() (APURegisters.P |= HalfCarry)
#define APUClearHalfCarry() (APURegisters.P &= ~HalfCarry)
#define APUSetBreak() (APURegisters.P |= BreakFlag)
#define APUClearBreak() (APURegisters.P &= ~BreakFlag)
#define APUSetDirectPage() (APURegisters.P |= DirectPageFlag)
#define APUClearDirectPage() (APURegisters.P &= ~DirectPageFlag)
#define APUSetOverflow() (IAPU._Overflow = 1)
#define APUClearOverflow() (IAPU._Overflow = 0)

#define APUCheckZero() (IAPU._Zero == 0)
#define APUCheckCarry() (IAPU._Carry)
#define APUCheckInterrupt() (APURegisters.P & Interrupt)
#define APUCheckHalfCarry() (APURegisters.P & HalfCarry)
#define APUCheckBreak() (APURegisters.P & BreakFlag)
#define APUCheckDirectPage() (APURegisters.P & DirectPageFlag)
#define APUCheckOverflow() (IAPU._Overflow)
#define APUCheckNegative() (IAPU._Zero & 0x80)

#define APUClearFlags(f) (APURegisters.P &= ~(f))
#define APUSetFlags(f)   (APURegisters.P |=  (f))
#define APUCheckFlag(f)  (APURegisters.P &   (f))

typedef union
{
#ifdef LSB_FIRST
    struct { uint8 A, Y; } B;
#else
    struct { uint8 Y, A; } B;
#endif
    uint16 W;
} YAndA;

struct SAPURegisters{
    uint8  P;
    YAndA YA;
    uint8  X;
    uint8  S;
    uint16  PC;
};

//EXTERN_C struct SAPURegisters APURegisters;
#define APURegisters APU.FastRegisters

// Needed by ILLUSION OF GAIA
//#define ONE_APU_CYCLE 14
#define ONE_APU_CYCLE 21

// Needed by all games written by the software company called Human
//#define ONE_APU_CYCLE_HUMAN 17
#define ONE_APU_CYCLE_HUMAN 21

// 1.953us := 1.024065.54MHz

#ifdef SPCTOOL
EXTERN_C int32 ESPC (int32);

#define APU_EXECUTE() \
{ \
    int32 l = (CPU.Cycles - APU.Cycles) / 14; \
    if (l > 0) \
    { \
        l -= _EmuSPC(l); \
        APU.Cycles += l * 14; \
    } \
}

#else

#ifdef DEBUGGER
#define APU_EXECUTE1() \
{ \
    if (APU.Flags & TRACE_FLAG) \
	S9xTraceAPU ();\
    APU.Cycles += APU.S9xAPUCycles [*IAPU.PC]; \
    (*S9xApuOpcodes[*IAPU.PC]) (); \
}
#else
#define APU_EXECUTE1() \
{ \
    APU.Cycles += APU.S9xAPUCycles [*IAPU.PC]; \
    (*S9xApuOpcodes[*IAPU.PC]) (); \
}
#endif

#define APU_EXECUTE() \
if (IAPU.APUExecuting) \
{\
    while (APU.Cycles <= CPU.Cycles) \
    { \
	APU_EXECUTE1(); \
    } \
}
#endif

#endif

