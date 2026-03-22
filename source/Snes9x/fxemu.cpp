#include "copyright.h"


#include "fxemu.h"
#include "fxinst.h"
#include "fxinst_arm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* The FxChip Emulator's internal variables */
/* Aligned to 3DS L1 cacheline boundary */
struct FxRegs_s GSU __attribute__((aligned(32)));

uint32 (**fx_ppfFunctionTable)(uint32) = 0;
void (**fx_ppfPlotTable)() = 0;

#define FXEMU_ENABLE_CALL_COUNTING 0

enum
{
	F_FxCacheWriteAccess,
	F_FxFlushCache,
	F_fx_updateRamBank,
	F_fx_readRegisterSpace,
	F_fx_dirtySCBR,
	F_fx_computeScreenPointersY,
	F_fx_computeScreenPointersN,
	F_fx_writeRegisterSpace,
	F_FxReset,
	F_fx_checkStartAddress,
	F_FxEmulate,
	F_FxBreakPointSet,
	F_FxBreakPointClear,
	F_FxStepOver,
	F_FxGetErrorCode,
	F_FxGetIllegalAddress,
	F_FxGetColorRegister,
	F_FxGetPlotOptionRegister,
	F_FxGetSourceRegisterIndex,
	F_FxGetDestinationRegisterIndex,
	F_FxPipe,
	F_COUNT // Number of function log slots
};

typedef struct
{
	char* name;
	int count;
	int max;
} CallCount;

#if FXEMU_ENABLE_CALL_COUNTING == 1
CallCount callCounts[F_COUNT] = {
	[F_FxCacheWriteAccess]            = {"FxCacheWriteAccess           ", 0, 0},
	[F_FxFlushCache]                  = {"FxFlushCache                 ", 0, 0},
	[F_fx_updateRamBank]              = {"fx_updateRamBank             ", 0, 0},
	[F_fx_readRegisterSpace]          = {"fx_readRegisterSpace         ", 0, 0},
	[F_fx_dirtySCBR]                  = {"fx_dirtySCBR                 ", 0, 0},
	[F_fx_computeScreenPointersY]     = {"fx_computeScreenPointersY    ", 0, 0},
	[F_fx_computeScreenPointersN]     = {"fx_computeScreenPointersN    ", 0, 0},
	[F_fx_writeRegisterSpace]         = {"fx_writeRegisterSpace        ", 0, 0},
	[F_FxReset]                       = {"FxReset                      ", 0, 0},
	[F_fx_checkStartAddress]          = {"fx_checkStartAddress         ", 0, 0},
	[F_FxEmulate]                     = {"FxEmulate                    ", 0, 0},
	[F_FxBreakPointSet]               = {"FxBreakPointSet              ", 0, 0},
	[F_FxBreakPointClear]             = {"FxBreakPointClear            ", 0, 0},
	[F_FxStepOver]                    = {"FxStepOver                   ", 0, 0},
	[F_FxGetErrorCode]                = {"FxGetErrorCode               ", 0, 0},
	[F_FxGetIllegalAddress]           = {"FxGetIllegalAddress          ", 0, 0},
	[F_FxGetColorRegister]            = {"FxGetColorRegister           ", 0, 0},
	[F_FxGetPlotOptionRegister]       = {"FxGetPlotOptionRegister      ", 0, 0},
	[F_FxGetSourceRegisterIndex]      = {"FxGetSourceRegisterIndex     ", 0, 0},
	[F_FxGetDestinationRegisterIndex] = {"FxGetDestinationRegisterIndex", 0, 0},
	[F_FxPipe]                        = {"FxPipe                       ", 0, 0},
};

void fxPrintCounts(void)
{
	for (int i = 0; i < F_COUNT; i++)
		printf("%s %2d %3d\n", callCounts[i].name, callCounts[i].count, callCounts[i].max);
}

void fxStartFrame(void)
{
	for (int i = 0; i < F_COUNT; i++)
		callCounts[i].count = 0;
}

static void logFunctionCall(int id)
{
	callCounts[id].count++;
	if (callCounts[id].count > callCounts[id].max)
		callCounts[id].max = callCounts[id].count;
}

#else
void fxPrintCounts(void) {} // Stub
void fxStartFrame(void) {} // Stub
static void logFunctionCall(int id) {} // Stub
#endif

void FxCacheWriteAccess(uint16 vAddress)
{
	logFunctionCall(F_FxCacheWriteAccess);
    if((vAddress & 0x00f) == 0x00f)
	GSU.vCacheFlags |= 1 << ((vAddress&0x1f0) >> 4);
}

void FxFlushCache()
{
	logFunctionCall(F_FxFlushCache);
    GSU.vCacheFlags = 0;
    GSU.vCacheBaseReg = 0;
    GSU.bCacheActive = FALSE;
}

void fx_updateRamBank(uint8 Byte)
{
	logFunctionCall(F_fx_updateRamBank);
	// Update BankReg and Bank pointer
    GSU.vRamBankReg = Byte & (FX_RAM_BANKS-1);
    GSU.pvRamBank = GSU.apvRamBank[Byte & (FX_RAM_BANKS-1)];
}


static void fx_readRegisterSpace()
{
	logFunctionCall(F_fx_readRegisterSpace);
    static uint32 avHeight[] = { 128, 160, 192, 256 };
    static uint32 avMult[] = { 16, 32, 32, 64 };

    GSU.vErrorCode = 0;

	// Compliant optimized (0x2cc -> 0x20c)
    /* Update R0-R15 */
    uint8* p = GSU.pvRegisters;
    for(int i = 0; i < 16; i++)
    {
			GSU.avReg[i] = p[0] | ((uint32)(p[1])) << 8;
			p += 2;
    }

    /* Update other registers */
	p = GSU.pvRegisters;
    GSU.vStatusReg    = p[GSU_SFR] | (p[GSU_SFR+1] << 8);
    uint8 vPrgBankReg = GSU.vPrgBankReg = p[GSU_PBR];
	uint8 vRomBankReg = GSU.vRomBankReg = p[GSU_ROMBR];
    uint8 vRamBankReg = GSU.vRamBankReg = p[GSU_RAMBR] & (FX_RAM_BANKS-1);
    GSU.vCacheBaseReg = p[GSU_CBR] | (p[GSU_CBR+1] << 8);

    /* Update status register variables */
	GSU.armFlags &= ~ARM_FLAGS;
    if(GSU.vStatusReg & FLG_Z)  GSU.armFlags |= ARM_ZERO;
    if(GSU.vStatusReg & FLG_S)  GSU.armFlags |= ARM_NEGATIVE;
    if(GSU.vStatusReg & FLG_OV) GSU.armFlags |= ARM_OVERFLOW;
    if(GSU.vStatusReg & FLG_CY) GSU.armFlags |= ARM_CARRY;
    
    /* Set bank pointers */
    GSU.pvRamBank = GSU.apvRamBank[vRamBankReg & (FX_RAM_BANKS-1)];
    GSU.pvRomBank = GSU.apvRomBank[vRomBankReg];
    GSU.pvPrgBank = GSU.apvRomBank[vPrgBankReg];

    /* Set screen pointers */
    GSU.pvScreenBase = &GSU.pvRam[USEX8(p[GSU_SCBR]) << 10];
	uint8 pvGsuScmr = p[GSU_SCMR];
    int i = ((int)(!!(pvGsuScmr & 0x04))) | (((int)(!!(pvGsuScmr & 0x20))) << 1);
    GSU.vScreenHeight = GSU.vScreenRealHeight = avHeight[i];
    uint32 vMode = GSU.vMode = pvGsuScmr & 0x03;

    if(i == 3)
		GSU.vScreenSize = (256/8) * (256/8) * 32;
    else
		GSU.vScreenSize = (GSU.vScreenHeight/8) * (256/8) * avMult[GSU.vMode];

    if (GSU.vPlotOptionReg & 0x10)
		/* OBJ Mode (for drawing into sprites) */
		GSU.vScreenHeight = 256;

    if(GSU.pvScreenBase + GSU.vScreenSize > GSU.pvRam + (GSU.nRamBanks * 65536))
		GSU.pvScreenBase =  GSU.pvRam + (GSU.nRamBanks * 65536) - GSU.vScreenSize;

    GSU.pfPlot = fx_apfPlotTable[vMode];
    GSU.pfRpix = fx_apfPlotTable[vMode + 5];

    fx_computeScreenPointers ();
}

void fx_dirtySCBR()
{
	logFunctionCall(F_fx_dirtySCBR);
	GSU.vSCBRDirty = TRUE;
}

void fx_computeScreenPointers ()
{
	uint32 vModeAdj = GSU.vMode, vModeAdjOld = GSU.vPrevMode;
	if (vModeAdj >= 3)
		vModeAdj = 2;
	if (vModeAdjOld >= 3)
		vModeAdjOld = 2;

    if (vModeAdj != vModeAdjOld || GSU.vPrevScreenHeight != GSU.vScreenHeight || GSU.vSCBRDirty)
    {
		logFunctionCall(F_fx_computeScreenPointersY);
		uint32 i;
		GSU.vSCBRDirty = FALSE;

		uint32 s1 = 4 + vModeAdj,
			   s2 = 8 + vModeAdj,
			   s3 = 16 - (7 + vModeAdj);

		/* Make a list of pointers to the start of each screen column */
		// Case 128 using the doubleshift is 2 more instructions in the
		// inner loop. The double shift is the same number of
		// instructions in the loop as without.
		switch (GSU.vScreenHeight)
		{
			case 128: s3 = 30; // s3 is not used for height 128, so we rightshift enough to zero its result.
			case 160: s3++;    // 16 - (6 + vModeAdj)
			case 192:
			{
				for (i = 0; i < 32; i++)
				{
					GSU.apvScreen[i] = GSU.pvScreenBase + (i << s1);
					GSU.x[i] = (i << s2) + ((i << 16) >> s3);
					// Old version: GSU.x[i] = (i << s2) + (i << s3) // (s3 was alone, not subtraced from 16)
				}
				break;
			}
			case 256:
			{
				s1 = 9 + vModeAdj;
				s2 = 8 + vModeAdj;
				s3 = 4 + vModeAdj;
			
				for (i = 0; i < 32; i++)
				{
					GSU.apvScreen[i] = GSU.pvScreenBase + ((i & 0x10) << s1) + ((i & 0xf) << s2);
					GSU.x[i] = ((i & 0x10) << s2) + ((i & 0xf) << s3);
				}
				break;
			}
		}
		GSU.vPrevMode = GSU.vMode;
		GSU.vPrevScreenHeight = GSU.vScreenHeight;
	}
	else
	{
		logFunctionCall(F_fx_computeScreenPointersN);
	}
}

static void fx_writeRegisterSpace()
{
	logFunctionCall(F_fx_writeRegisterSpace);
	uint8* p;
	
	// Non-compliant optimized (0x118)
	// WYATT_TODO if I can properly ensure the alignment here, it'd be a decent speedup.
    // for(int i = 0; i < 16; i++)
    // {
	// 	*(uint16*) __builtin_assume_aligned(&((uint16*) GSU.pvRegisters)[i], _Alignof(uint16)) = GSU.avReg[i];
    // }
    
	// Compliant Optimized (0x1dc -> 0x19c)
    p = GSU.pvRegisters;
    for(int i = 0; i < 16; i++)
    {
		uint32 reg = GSU.avReg[i];
		*p++ = (uint8)reg;
		*p++ = (uint8)(reg >> 8);
    }

	CF(Z);
	CF(S);
	CF(OV);
	CF(CY);

    /* Update status register */
    if (GSU.armFlags & ARM_ZERO)     SF(Z);
    if (GSU.armFlags & ARM_NEGATIVE) SF(S);
    if (GSU.armFlags & ARM_OVERFLOW) SF(OV);
    if (GSU.armFlags & ARM_CARRY)    SF(CY);
    
    p = GSU.pvRegisters;
	{
		uint16 vStatusReg = GSU.vStatusReg;
		p[GSU_SFR]   = (uint8) vStatusReg;
		p[GSU_SFR+1] = (uint8)(vStatusReg>>8);
	}

    p[GSU_PBR]   = GSU.vPrgBankReg;
    p[GSU_ROMBR] = GSU.vRomBankReg;
    p[GSU_RAMBR] = GSU.vRamBankReg;
	
	{
		uint16 vCacheBaseReg = GSU.vCacheBaseReg;
		p[GSU_CBR]   = (uint8) vCacheBaseReg;
		p[GSU_CBR+1] = (uint8)(vCacheBaseReg>>8);
	}
}

/* Reset the FxChip */
void FxReset(struct FxInit_s *psFxInfo)
{
	logFunctionCall(F_FxReset);
    int i;
	
	// These seem to be for a potential debug feature where different
	// opcode sets could be loaded based on config flags, but the array
	// only has one set, as you can see.
    static uint32 (**appfFunction[])(uint32) = { &fx_apfFunctionTable[0] };
    static void (**appfPlot[])() = { &fx_apfPlotTable[0] };

	uint32 opcodeTableId = psFxInfo->vFlags & 0x3;

    /* Get function pointers for the current emulation mode */
    fx_ppfFunctionTable = appfFunction[opcodeTableId];
    fx_ppfPlotTable = appfPlot[opcodeTableId];
    // fx_ppfOpcodeTable = appfOpcode[psFxInfo->vFlags & 0x3];
    
    /* Clear all internal variables */
    memset((uint8*)&GSU,0,sizeof(struct FxRegs_s));

    /* Set default registers */
    GSU.pvSreg = GSU.pvDreg = 0;

    /* Set RAM and ROM pointers */
    GSU.pvRegisters = psFxInfo->pvRegisters;
    GSU.nRamBanks = psFxInfo->nRamBanks;
    GSU.pvRam = psFxInfo->pvRam;
    GSU.nRomBanks = psFxInfo->nRomBanks;
    GSU.pvRom = psFxInfo->pvRom;
    GSU.vPrevScreenHeight = ~0;
    GSU.vPrevMode = ~0;

    /* The GSU can't access more than 2mb (16mbits) */
    if(GSU.nRomBanks > 0x20)
		GSU.nRomBanks = 0x20;
    
    /* Clear FxChip register space */
    memset(GSU.pvRegisters,0,0x300);

    /* Set FxChip version Number */
    GSU.pvRegisters[0x3b] = 0;

    /* Make ROM bank table */
    for(i=0; i<256; i++)
    {
			uint32 b = i & 0x7f;
			if (b >= 0x40)
			{
				if (GSU.nRomBanks > 1)
					b %= GSU.nRomBanks;
				else
					b &= 1;

				GSU.apvRomBank[i] = &GSU.pvRom[ b << 16 ];
			}
			else
			{
				b %= GSU.nRomBanks * 2;
				GSU.apvRomBank[i] = &GSU.pvRom[ (b << 16) + 0x200000];
			}
		}

    /* Make RAM bank table */
    for(i=0; i<4; i++)
    {
			GSU.apvRamBank[i] = &GSU.pvRam[(i % GSU.nRamBanks) << 16];
			GSU.apvRomBank[0x70 + i] = GSU.apvRamBank[i];
    }
    
    /* Start with a nop in the pipe */
    GSU.vPipe = 0x01;

    /* Set pointer to GSU cache */
    GSU.pvCache = &GSU.pvRegisters[0x100];

    fx_readRegisterSpace();
}

static uint8 fx_checkStartAddress()
{
	logFunctionCall(F_fx_checkStartAddress);
    /* Check if we start inside the cache */
    if(GSU.bCacheActive && R15 >= GSU.vCacheBaseReg && R15 < (GSU.vCacheBaseReg+512U))
			return TRUE;
   
    /*  Check if we're in an unused area */
    if(GSU.vPrgBankReg < 0x40 && R15 < 0x8000)
			return FALSE;
    if(GSU.vPrgBankReg >= 0x60 && GSU.vPrgBankReg <= 0x6f)
			return FALSE;
    if(GSU.vPrgBankReg >= 0x74)
			return FALSE;

    /* Check if we're in RAM and the RAN flag is not set */
    if(GSU.vPrgBankReg >= 0x70 && GSU.vPrgBankReg <= 0x73 && !(SCMR&(1<<3)) )
			return FALSE;

    /* If not, we're in ROM, so check if the RON flag is set */
    if(!(SCMR&(1<<4)))
			return FALSE;
    
    return TRUE;
}

/* Execute until the next stop instruction */
int FxEmulate(uint32 nInstructions)
{
	logFunctionCall(F_FxEmulate);
    uint32 vCount;

    /* Read registers and initialize GSU session */
    fx_readRegisterSpace();

    /* Check if the start address is valid */
    if(!fx_checkStartAddress())
    {
			CF(G);
			fx_writeRegisterSpace();
			return 0;
    }

    /* Execute GSU session */
    CF(IRQ);

	vCount = fx_ppfFunctionTable[FX_FUNCTION_RUN](nInstructions);

    /* Store GSU registers */
    fx_writeRegisterSpace();

    /* Check for error code */
    if(GSU.vErrorCode)
			return GSU.vErrorCode;
    else
			return vCount;
}

/* Breakpoints */
void FxBreakPointSet(uint32 vAddress)
{
	logFunctionCall(F_FxBreakPointSet);
    GSU.bBreakPoint = TRUE;
    GSU.vBreakPoint = USEX16(vAddress);
}
void FxBreakPointClear()
{
	logFunctionCall(F_FxBreakPointClear);
    GSU.bBreakPoint = FALSE;
}

/* Step by step execution */
int FxStepOver(uint32 nInstructions)
{
	logFunctionCall(F_FxStepOver);
    uint32 vCount;
    fx_readRegisterSpace();

    /* Check if the start address is valid */
    if(!fx_checkStartAddress())
    {
			CF(G);
			return 0;
    }
    
    if( PIPE >= 0xf0 )
			GSU.vStepPoint = USEX16(R15+3);
    else if( (PIPE >= 0x05 && PIPE <= 0x0f) || (PIPE >= 0xa0 && PIPE <= 0xaf) )
			GSU.vStepPoint = USEX16(R15+2);
    else
			GSU.vStepPoint = USEX16(R15+1);

    vCount = fx_ppfFunctionTable[FX_FUNCTION_STEP_OVER](nInstructions);
    fx_writeRegisterSpace();
    if(GSU.vErrorCode)
			return GSU.vErrorCode;
    else
			return vCount;
}

/* Errors */
int FxGetErrorCode()
{
	logFunctionCall(F_FxGetErrorCode);
    return GSU.vErrorCode;
}

int FxGetIllegalAddress()
{
	logFunctionCall(F_FxGetIllegalAddress);
    return GSU.vIllegalAddress;
}

/* Access to internal registers */
uint32 FxGetColorRegister()
{
	logFunctionCall(F_FxGetColorRegister);
    return GSU.vColorReg & 0xff;
}

uint32 FxGetPlotOptionRegister()
{
	logFunctionCall(F_FxGetPlotOptionRegister);
    return GSU.vPlotOptionReg & 0x1f;
}

uint32 FxGetSourceRegisterIndex()
{
	logFunctionCall(F_FxGetSourceRegisterIndex);
    return GSU.pvSreg;
}

uint32 FxGetDestinationRegisterIndex()
{
	logFunctionCall(F_FxGetDestinationRegisterIndex);
    return GSU.pvDreg;
}

uint8 FxPipe()
{
	logFunctionCall(F_FxPipe);
    return GSU.vPipe;
}
