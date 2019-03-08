#include "copyright.h"


#ifndef _sa1_h_
#define _sa1_h_

#include "memmap.h"

struct SSA1Registers {
    uint8   PB;
    uint8   DB;
    pair    P;
    pair    A;
    pair    D;
    pair    S;
    pair    X;
    pair    Y;
    uint16  PC;
};

struct SSA1 {
    struct  SOpcodes *S9xOpcodes;
    uint8   _Carry;
    uint8   _Zero;
    uint8   _Negative;
    uint8   _Overflow;
    bool8   CPUExecuting;
    uint32  ShiftedPB;
    uint32  ShiftedDB;
    uint32  Flags;
    bool8   Executing;
    bool8   NMIActive;
    bool8   IRQActive;
    bool8   WaitingForInterrupt;
    bool8   Waiting;
//    uint8   WhichEvent;
    uint8   *PC;
    uint8   *PCBase;
    uint8   *BWRAM;
    uint8   *PCAtOpcodeStart;
    uint8   *WaitAddress;
    uint32  WaitCounter;
    uint8   *WaitByteAddress1;
    uint8   *WaitByteAddress2;
//    long    Cycles;
//    long    NextEvent;
//    long    V_Counter;
    uint8   *Map [MEMMAP_NUM_BLOCKS];
    uint8   *WriteMap [MEMMAP_NUM_BLOCKS];
    int16   op1;
    int16   op2;
    int     arithmetic_op;
    int64   sum;
    bool8   overflow;
    uint8   VirtualBitmapFormat;
    bool8   in_char_dma;
    uint8   variable_bit_pos;
    
    long    OpAddress;

    bool    isInIdleLoop;
};

#define SA1CheckZero() (SA1._Zero == 0)
#define SA1CheckCarry() (SA1._Carry)
#define SA1CheckIRQ() (SA1Registers.PL & IRQ)
#define SA1CheckDecimal() (SA1Registers.PL & Decimal)
#define SA1CheckIndex() (SA1Registers.PL & IndexFlag)
#define SA1CheckMemory() (SA1Registers.PL & MemoryFlag)
#define SA1CheckOverflow() (SA1._Overflow)
#define SA1CheckNegative() (SA1._Negative & 0x80)
#define SA1CheckEmulation() (SA1Registers.P.W & Emulation)

#define SA1ClearFlags(f) (SA1Registers.P.W &= ~(f))
#define SA1SetFlags(f)   (SA1Registers.P.W |=  (f))
#define SA1CheckFlag(f)  (SA1Registers.PL & (f))


START_EXTERN_C
uint8 S9xSA1GetByte (uint32);
uint8 S9xSA1GetByteSlow (uint32 address, int GetAddress);
uint16 S9xSA1GetWord (uint32);
uint16 S9xSA1GetWordFast (uint32);

void S9xSA1SetByte (uint8, uint32);
void S9xSA1SetByteSlow (uint8 byte, uint32 address, int Setaddress);
void S9xSA1SetWord (uint16, uint32);
void S9xSA1SetWordFast (uint16, uint32);


void S9xSA1SetPCBase (uint32);
uint8 S9xGetSA1 (uint32);
void S9xSetSA1 (uint8, uint32);

extern struct SOpcodes S9xSA1OpcodesM1X1 [256];
extern struct SOpcodes S9xSA1OpcodesM1X0 [256];
extern struct SOpcodes S9xSA1OpcodesM0X1 [256];
extern struct SOpcodes S9xSA1OpcodesM0X0 [256];
extern struct SSA1Registers SA1Registers;
extern struct SSA1 SA1;

void S9xSA1MainLoop ();
void S9xSA1CheckIRQ ();
void S9xSA1Init ();
void S9xFixSA1AfterSnapshotLoad ();
void S9xSA1ExecuteDuringSleep ();
END_EXTERN_C

#define OpenBus OCPU.FastOpenBus

INLINE uint8 __attribute__((always_inline)) S9xSA1GetByteFast (uint32 address)
{
    uint8 *GetAddress = SA1.Map [(address >> MEMMAP_SHIFT) & MEMMAP_MASK];
    if (GetAddress >= (uint8 *) CMemory::MAP_LAST)
	    return (*(GetAddress + (address & 0xffff)));
    else
        return S9xSA1GetByteSlow(address, (int)GetAddress);
}

INLINE void __attribute__((always_inline)) S9xSA1SetByteFast (uint8 byte, uint32 address)
{
    uint8 *Setaddress = SA1.WriteMap [(address >> MEMMAP_SHIFT) & MEMMAP_MASK];

    if (Setaddress >= (uint8 *) CMemory::MAP_LAST)
    {
        *(Setaddress + (address & 0xffff)) = byte;
        return;
    }

    S9xSA1SetByteSlow (byte, address, (int)Setaddress);
}

inline uint16 __attribute__((always_inline)) S9xSA1GetWordFast (uint32 address)
{
    OpenBus = S9xSA1GetByteFast (address);
    return (OpenBus | (S9xSA1GetByteFast (address + 1) << 8));
}

inline void __attribute__((always_inline)) S9xSA1SetWordFast (uint16 Word, uint32 address)
{
    S9xSA1SetByteFast ((uint8) Word, address);
    S9xSA1SetByteFast ((uint8) (Word >> 8), address + 1);
}


#define SNES_IRQ_SOURCE	    (1 << 7)
#define TIMER_IRQ_SOURCE    (1 << 6)
#define DMA_IRQ_SOURCE	    (1 << 5)

STATIC inline void S9xSA1UnpackStatus()
{
    SA1._Zero = (SA1Registers.PL & Zero) == 0;
    SA1._Negative = (SA1Registers.PL & Negative);
    SA1._Carry = (SA1Registers.PL & Carry);
    SA1._Overflow = (SA1Registers.PL & Overflow) >> 6;
}

STATIC inline void S9xSA1PackStatus()
{
    SA1Registers.PL &= ~(Zero | Negative | Carry | Overflow);
    SA1Registers.PL |= SA1._Carry | ((SA1._Zero == 0) << 1) |
		       (SA1._Negative & 0x80) | (SA1._Overflow << 6);
}

STATIC inline void S9xSA1FixCycles ()
{
    if (SA1CheckEmulation ())
	SA1.S9xOpcodes = S9xSA1OpcodesM1X1;
    else
    if (SA1CheckMemory ())
    {
	if (SA1CheckIndex ())
	    SA1.S9xOpcodes = S9xSA1OpcodesM1X1;
	else
	    SA1.S9xOpcodes = S9xSA1OpcodesM1X0;
    }
    else
    {
	if (SA1CheckIndex ())
	    SA1.S9xOpcodes = S9xSA1OpcodesM0X1;
	else
	    SA1.S9xOpcodes = S9xSA1OpcodesM0X0;
    }
}
#endif

