#include "copyright.h"


#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "dsp1.h"
#include "cpuexec.h"
#include "debug.h"
#include "apu.h"
#include "dma.h"
#include "sa1.h"
#include "cheats.h"
#include "srtc.h"
#include "sdd1.h"
#include "spc7110.h"
#include "obc1.h"
#include "bsx.h"


#ifndef ZSNES_FX
#include "fxemu.h"

extern struct FxInit_s SuperFX;

void S9xResetSuperFX ()
{
    SuperFX.vFlags = 0; //FX_FLAG_ROM_BUFFER;// | FX_FLAG_ADDRESS_CHECKING;
	SuperFX.speedPerLine = (uint32) (0.417 * 10.5e6 * ((1.0 / (float) Memory.ROMFramesPerSecond) / 
        ((float) ((Settings.PAL ? SNES_MAX_PAL_VCOUNTER : SNES_MAX_NTSC_VCOUNTER)))));
	//printf("SFX:%d\n", SuperFX.speedPerLine);
	SuperFX.oneLineDone = FALSE;
    FxReset (&SuperFX);
}
#endif

void S9xResetCPU ()
{
    Registers.PB = 0;
    Registers.PC = S9xGetWordNoCycles (0xFFFC);
    Registers.D.W = 0;
    Registers.DB = 0;
    Registers.SH = 1;
    Registers.SL = 0xFF;
    Registers.XH = 0;
    Registers.YH = 0;
    Registers.P.W = 0;

    ICPU.ShiftedPB = 0;
    ICPU.ShiftedDB = 0;
    SetFlags (MemoryFlag | IndexFlag | IRQ | Emulation);
    ClearFlags (Decimal);

    CPU.Flags = CPU.Flags & (DEBUG_MODE_FLAG | TRACE_FLAG);
    CPU.BranchSkip = FALSE;
    CPU.NMIActive = FALSE;
    CPU.IRQActive = FALSE;
    CPU.WaitingForInterrupt = FALSE;
    CPU.InDMA = FALSE;
    CPU.WhichEvent = HBLANK_START_EVENT;
    CPU.PC = NULL;
    CPU.PCBase = NULL;
    CPU.PCAtOpcodeStart = NULL;
    CPU.WaitAddress = NULL;
    CPU.WaitCounter = 0;
    CPU.Cycles = 0;
    CPU.PrevCycles = 0;
    CPU.NextEvent = Settings.HBlankStart;
    CPU.V_Counter = 0;
    CPU.MemSpeed = SLOW_ONE_CYCLE;
    CPU.MemSpeedx2 = SLOW_ONE_CYCLE * 2;
    CPU.FastROMSpeed = SLOW_ONE_CYCLE;
    CPU.AutoSaveTimer = 0;
    //CPU.AccumulatedAutoSaveTimer = 0;
    CPU.SRAMModified = FALSE;
    // CPU.NMITriggerPoint = 4; // Set when ROM image loaded
    CPU.BRKTriggered = FALSE;
    //CPU.TriedInterleavedMode2 = FALSE; // Reset when ROM image loaded
    CPU.NMICycleCount = 0;
    CPU.IRQCycleCount = 0;
    S9xSetPCBase (Registers.PC);

    ICPU.S9xOpcodes = S9xOpcodesE1;
    ICPU.CPUExecuting = TRUE;

    S9xUnpackStatus();
}

#ifdef ZSNES_FX
START_EXTERN_C
void S9xResetSuperFX ();
bool8 WinterGold = 0;
extern uint8 *C4Ram;
END_EXTERN_C
#endif

void S9xReset (void)
{
    if (Settings.SuperFX)
        S9xResetSuperFX ();

#ifdef ZSNES_FX
    WinterGold = Settings.WinterGold;
#endif
    ZeroMemory (Memory.FillRAM, 0x8000);
    memset (Memory.VRAM, 0x00, 0x10000);
    memset (Memory.RAM, 0x55, 0x20000);

	if (Settings.BS)
		S9xResetBSX();

	if(Settings.SPC7110)
		S9xSpc7110Reset();
    S9xResetCPU ();
    S9xResetPPU ();
    S9xResetSRTC ();
    if (Settings.SDD1)
        S9xResetSDD1 ();

    S9xResetDMA ();
    S9xResetAPU ();
    S9xResetDSP1 ();
    S9xSA1Init ();
    if (Settings.C4)
        S9xInitC4 ();
    S9xInitCheatData ();
	if(Settings.OBC1)
		ResetOBC1();

//    Settings.Paused = FALSE;
}
void S9xSoftReset (void)
{
    if (Settings.SuperFX)
        S9xResetSuperFX ();

#ifdef ZSNES_FX
    WinterGold = Settings.WinterGold;
#endif
    ZeroMemory (Memory.FillRAM, 0x8000);
    memset (Memory.VRAM, 0x00, 0x10000);
 //   memset (Memory.RAM, 0x55, 0x20000);

	if (Settings.BS)
		S9xResetBSX();

	if(Settings.SPC7110)
		S9xSpc7110Reset();
    S9xResetCPU ();
    S9xSoftResetPPU ();
    S9xResetSRTC ();
    if (Settings.SDD1)
        S9xResetSDD1 ();

    S9xResetDMA ();
    S9xResetAPU ();
    S9xResetDSP1 ();
	if(Settings.OBC1)
		ResetOBC1();
    S9xSA1Init ();
    if (Settings.C4)
        S9xInitC4 ();
    S9xInitCheatData ();

//    Settings.Paused = FALSE;
}

