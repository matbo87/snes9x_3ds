#include "copyright.h"


#ifndef _65c816_h_
#define _65c816_h_

#define AL A.B.l
#define AH A.B.h
#define XL X.B.l
#define XH X.B.h
#define YL Y.B.l
#define YH Y.B.h
#define SL S.B.l
#define SH S.B.h
#define DL D.B.l
#define DH D.B.h
#define PL P.B.l
#define PH P.B.h

#define Carry       1
#define Zero        2
#define IRQ         4
#define Decimal     8
#define IndexFlag  16
#define MemoryFlag 32
#define Overflow   64
#define Negative  128
#define Emulation 256

#define ClearCarry() (ICPU._Carry = 0)
#define SetCarry() (ICPU._Carry = 1)
#define SetZero() (ICPU._Zero = 0)
#define ClearZero() (ICPU._Zero = 1)
#define SetIRQ() (Registers.PL |= IRQ)
#define ClearIRQ() (Registers.PL &= ~IRQ)
#define SetDecimal() (Registers.PL |= Decimal)
#define ClearDecimal() (Registers.PL &= ~Decimal)
#define SetIndex() (Registers.PL |= IndexFlag)
#define ClearIndex() (Registers.PL &= ~IndexFlag)
#define SetMemory() (Registers.PL |= MemoryFlag)
#define ClearMemory() (Registers.PL &= ~MemoryFlag)
#define SetOverflow() (ICPU._Overflow = 1)
#define ClearOverflow() (ICPU._Overflow = 0)
#define SetNegative() (ICPU._Negative = 0x80)
#define ClearNegative() (ICPU._Negative = 0)

#define CheckZero() (ICPU._Zero == 0)
#define CheckCarry() (ICPU._Carry)
#define CheckIRQ() (Registers.PL & IRQ)
#define CheckDecimal() (Registers.PL & Decimal)
#define CheckIndex() (Registers.PL & IndexFlag)
#define CheckMemory() (Registers.PL & MemoryFlag)
#define CheckOverflow() (ICPU._Overflow)
#define CheckNegative() (ICPU._Negative & 0x80)
#define CheckEmulation() (Registers.P.W & Emulation)

#define ClearFlags(f) (Registers.P.W &= ~(f))
#define SetFlags(f)   (Registers.P.W |=  (f))
#define CheckFlag(f)  (Registers.PL & (f))

typedef union
{
#ifdef LSB_FIRST
    struct { uint8 l,h; } B;
#else
    struct { uint8 h,l; } B;
#endif
    uint16 W;
} pair;

struct SRegisters{
    uint8  PB;
    uint8  DB;
    pair   P;
    pair   A;
    pair   D;
    pair   S;
    pair   X;
    pair   Y;
    uint16 PC;
};

//EXTERN_C struct SRegisters Registers;
#define Registers OCPU.FastRegisters

#endif

