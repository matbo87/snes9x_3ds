#include "copyright.h"


#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "cpuexec.h"

#include "sa1.h"
#define CPU SA1
#define ICPU SA1
#define Registers SA1Registers
#define S9xGetByte S9xSA1GetByteFast
#define S9xGetWord S9xSA1GetWordFast
#define S9xSetByte S9xSA1SetByteFast
#define S9xSetWord S9xSA1SetWordFast
#define S9xSetPCBase S9xSA1SetPCBase
#define S9xOpcodesM1X1 S9xSA1OpcodesM1X1
#define S9xOpcodesM1X0 S9xSA1OpcodesM1X0
#define S9xOpcodesM0X1 S9xSA1OpcodesM0X1
#define S9xOpcodesM0X0 S9xSA1OpcodesM0X0
#define S9xOpcodesE1   S9xSA1OpcodesE1
#define S9xOpcode_IRQ S9xSA1Opcode_IRQ
#define S9xOpcode_NMI S9xSA1Opcode_NMI
#define S9xUnpackStatus S9xSA1UnpackStatus
#define S9xPackStatus S9xSA1PackStatus
#define S9xFixCycles S9xSA1FixCycles
#define Immediate8 SA1Immediate8
#define Immediate16 SA1Immediate16
#define Relative SA1Relative
#define RelativeLong SA1RelativeLong
#define AbsoluteIndexedIndirect SA1AbsoluteIndexedIndirect
#define AbsoluteIndirectLong SA1AbsoluteIndirectLong
#define AbsoluteIndirect SA1AbsoluteIndirect
#define Absolute SA1Absolute
#define AbsoluteLong SA1AbsoluteLong
#define Direct SA1Direct
#define DirectIndirectIndexed SA1DirectIndirectIndexed
#define DirectIndirectIndexedLong SA1DirectIndirectIndexedLong
#define DirectIndexedIndirect SA1DirectIndexedIndirect
#define DirectIndexedX SA1DirectIndexedX
#define DirectIndexedY SA1DirectIndexedY
#define AbsoluteIndexedX SA1AbsoluteIndexedX
#define AbsoluteIndexedY SA1AbsoluteIndexedY
#define AbsoluteLongIndexedX SA1AbsoluteLongIndexedX
#define DirectIndirect SA1DirectIndirect
#define DirectIndirectLong SA1DirectIndirectLong
#define StackRelative SA1StackRelative
#define StackRelativeIndirectIndexed SA1StackRelativeIndirectIndexed

//#undef CPU_SHUTDOWN
#undef VAR_CYCLES
#define SA1_OPCODES

long OpAddress = 0;

#include "sa1cpuops.cpp"

void S9xSA1MainLoop ()
{
    int i;

/*
#if 0
    if (SA1.Flags & NMI_FLAG)
    {
	SA1.Flags &= ~NMI_FLAG;
	if (SA1.WaitingForInterrupt)
	{
	    SA1.WaitingForInterrupt = FALSE;
	    SA1.PC++;
	}
	S9xSA1Opcode_NMI ();
    }
#endif*/
    if (SA1.Flags & IRQ_PENDING_FLAG)
    {
	if (SA1.IRQActive)
	{
	    if (SA1.WaitingForInterrupt)
	    {
		SA1.WaitingForInterrupt = FALSE;
		SA1.PC++;
	    }
	    if (!SA1CheckFlag (IRQ))
		S9xSA1Opcode_IRQ ();
	}
	else
	    SA1.Flags &= ~IRQ_PENDING_FLAG;
    }
    /*
#ifdef DEBUGGER
    if (SA1.Flags & TRACE_FLAG)
    {
	for (i = 0; i < 3 && SA1.Executing; i++)
	{
	    S9xSA1Trace ();
#ifdef CPU_SHUTDOWN
	    SA1.PCAtOpcodeStart = SA1.PC;
#endif
	    (*SA1.S9xOpcodes [*SA1.PC++].S9xOpcode) ();
	}
    }
    else
#endif*/

    /*
    for (i = 0; i < 3 && SA1.Executing; i++)
    {
#ifdef CPU_SHUTDOWN
	SA1.PCAtOpcodeStart = SA1.PC;
#endif
	(*SA1.S9xOpcodes [*SA1.PC++].S9xOpcode) ();
    }
    */
    //if (!SA1.Executing) return; 
    (*SA1.S9xOpcodes [*SA1.PC++].S9xOpcode) ();
    //if (!SA1.Executing) return; 
    (*SA1.S9xOpcodes [*SA1.PC++].S9xOpcode) ();
    //if (!SA1.Executing) return; 
    (*SA1.S9xOpcodes [*SA1.PC++].S9xOpcode) ();
}


void S9xSA1CheckIRQ()
{
	if (SA1.IRQActive)
	{
	    if (SA1.WaitingForInterrupt)
	    {
		SA1.WaitingForInterrupt = FALSE;
		SA1.PC++;
	    }
	    if (!SA1CheckFlag (IRQ))
		S9xSA1Opcode_IRQ ();
	}
	else
	    SA1.Flags &= ~IRQ_PENDING_FLAG;    
}