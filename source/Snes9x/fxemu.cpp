#include "copyright.h"


#include "fxemu.h"
#include "fxinst.h"
#include "fxinst_arm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* The FxChip Emulator's internal variables */
/* Aligned to 3DS L1 cacheline boundary */
// struct FxRegs_s GSU __attribute__((aligned(32)));

/* Used for some ASM optimization. See link.ld for more info. */
struct FxRegs_s GSU __attribute__((section(".gsu_segment")));

#define MIN(a_, b_) ((a_ <= b_) ? a_ : b_)
#define LIKELY(cond_) __builtin_expect(!!(cond_), 1)
#define UNLIKELY(cond_) __builtin_expect(!!(cond_), 0)
#define FXEMU_ENABLE_CALL_COUNTING 0

enum
{
    F_FxCacheWriteAccess,
    F_FxFlushCache,
    F_fx_updateRamBank,
    F_fx_readRegisterSpace,
    F_fx_dirtySCBR,
    F_fx_computeScreenPointers,
    F_fx_writeRegisterSpace,
    F_FxReset,
    F_fx_checkStartAddress,
    F_FxEmulate,
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
    [F_fx_computeScreenPointers]      = {"fx_computeScreenPointers     ", 0, 0},
    [F_fx_writeRegisterSpace]         = {"fx_writeRegisterSpace        ", 0, 0},
    [F_FxReset]                       = {"FxReset                      ", 0, 0},
    [F_fx_checkStartAddress]          = {"fx_checkStartAddress         ", 0, 0},
    [F_FxEmulate]                     = {"FxEmulate                    ", 0, 0},
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
    GSU.vCacheBaseReg = 1; // Marked inactive
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

    // Compliant optimized (0x2cc -> 0x20c)
    /* Update R0-R15 */
    uint8 *const p = GSU.pvRegisters;
    memcpy(GSU.avReg, p, sizeof(GSU.avReg)); // Assumes little-endian

    /* Update other registers */
    GSU.vStatusReg    = p[GSU_SFR] | (p[GSU_SFR+1] << 8);
    uint8 vPrgBankReg = GSU.vPrgBankReg = p[GSU_PBR];
    uint8 vRomBankReg = GSU.vRomBankReg = p[GSU_ROMBR];
    uint8 vRamBankReg = GSU.vRamBankReg = p[GSU_RAMBR] & (FX_RAM_BANKS-1);
    GSU.vCacheBaseReg = p[GSU_CBR] | (p[GSU_CBR+1] << 8) | (GSU.vCacheBaseReg & 1); // Preserve enable flag.

    /* Update status register variables */
    uint32 armFlags = 0;
    if(GSU.vStatusReg & FLG_Z)  armFlags |= ARM_ZERO >> 24;
    if(GSU.vStatusReg & FLG_S)  armFlags |= ARM_NEGATIVE >> 24;
    if(GSU.vStatusReg & FLG_OV) armFlags |= ARM_OVERFLOW >> 24;
    if(GSU.vStatusReg & FLG_CY) armFlags |= ARM_CARRY >> 24;
    GSU.armFlags = armFlags;
    
    /* Set bank pointers */
    GSU.pvRamBank = GSU.apvRamBank[vRamBankReg & (FX_RAM_BANKS-1)];
    GSU.pvRomBank = GSU.apvRomBank[vRomBankReg];
    GSU.pvPrgBank = GSU.apvRomBank[vPrgBankReg];

    /* Set screen pointers */
    GSU.pvScreenBase = &GSU.pvRam[USEX8(p[GSU_SCBR]) << 10];
    uint8 pvGsuScmr = p[GSU_SCMR];
    int i = ((int)(!!(pvGsuScmr & 0x04))) | (((int)(!!(pvGsuScmr & 0x20))) << 1);

    /* Grab height and remove dirty bit from real height */
    uint16 prevScreenHeight = GSU.vScreenHeight;
    GSU.vScreenHeight = GSU.vScreenRealHeight = avHeight[i];

    uint8 oldMode = GSU.vMode;
    GSU.vMode = pvGsuScmr & 0x03;

    /* If vMode is changed, recompute screen pointers */
    if (MIN(GSU.vMode, 2) != MIN(oldMode, 2))
        prevScreenHeight |= BIT(15);

    uint32 vScreenSize;
    if(i == 3) vScreenSize = (256/8) * (256/8) * 32;
    else       vScreenSize = (GSU.vScreenHeight/8) * (256/8) * avMult[GSU.vMode];

    /* OBJ Mode (for drawing into sprites) */
    if (GSU.vPlotOptionReg & PLOT_OBJECT)
        GSU.vScreenHeight = 256;

    if(GSU.pvScreenBase + vScreenSize > GSU.pvRam + (GSU.nRamBanks * 65536))
        GSU.pvScreenBase =  GSU.pvRam + (GSU.nRamBanks * 65536) - vScreenSize;

    if (prevScreenHeight != GSU.vScreenHeight)
        fx_computeScreenPointers();
}

/* Mark the screen base register as dirty. This
   means we have to run fx_computeScreenPointers. */
void fx_dirtySCBR()
{
    logFunctionCall(F_fx_dirtySCBR);
    GSU.vScreenHeight |= BIT(15); // Set the dirty bit
}

void fx_computeScreenPointers ()
{
    logFunctionCall(F_fx_computeScreenPointers);

    uint32 vModeAdj = MIN(GSU.vMode, 2);
    uint32 s1 = 4 + vModeAdj,
           s2 = 8 + vModeAdj,
           s3 = 16 - (7 + vModeAdj);
    uint8* screenBase = GSU.pvScreenBase;

    /* Make a list of pointers to the start of each screen column */
    // Case 128 using the doubleshift is 2 more instructions in the
    // inner loop. The double shift is the same number of
    // instructions in the loop as without.
    switch (GSU.vScreenHeight)
    {
        default: // Should be unreachable
        case 128: s3 = 30; // s3 is not used for height 128, so we rightshift enough to zero its result.
        case 160: s3++;    // 16 - (6 + vModeAdj)
        case 192:
        {
            for (uint32 i = 0; i < 32; i++)
            {
                GSU.x[i] = (i << s2) + ((i << 16) >> s3);
                uint8* screen = screenBase + (i << s1);
                for (uint32 j = 0; j < 8; j++)
                    GSU.apvScreen[8*i + j] = screen + (j << 1);
                // Old version: GSU.x[i] = (i << s2) + (i << s3) // (s3 was alone, not subtraced from 16)
            }
            break;
        }
        case 256:
        {
            s1 = 9 + vModeAdj;
            s2 = 8 + vModeAdj;
            s3 = 4 + vModeAdj;
        
            for (uint32 i = 0; i < 32; i++)
            {
                GSU.x[i] = ((i & 0x10) << s2) + ((i & 0xf) << s3);
                uint8* screen = screenBase + ((i & 0x10) << s1) + ((i & 0xf) << s2);
                for (uint32 j = 0; j < 8; j++)
                    GSU.apvScreen[8*i + j] = screen + (j << 1);
            }
            break;
        }
    }
}

static void fx_writeRegisterSpace()
{
    logFunctionCall(F_fx_writeRegisterSpace);
    uint8 *const p = GSU.pvRegisters;
    
    memcpy(p, GSU.avReg, sizeof(GSU.avReg)); // Assumes little-endian

    /* Update status register */
    uint16 statusReg = GSU.vStatusReg & ~(FLG_Z | FLG_CY | FLG_S | FLG_OV);
    if (GSU.armFlags & (ARM_ZERO >> 24))     statusReg |= FLG_Z;
    if (GSU.armFlags & (ARM_NEGATIVE >> 24)) statusReg |= FLG_S;
    if (GSU.armFlags & (ARM_OVERFLOW >> 24)) statusReg |= FLG_OV;
    if (GSU.armFlags & (ARM_CARRY >> 24))    statusReg |= FLG_CY;
    GSU.vStatusReg = statusReg;
    
    uint16 vStatusReg = GSU.vStatusReg;
    p[GSU_SFR]   = (uint8) vStatusReg;
    p[GSU_SFR+1] = (uint8)(vStatusReg>>8);

    p[GSU_PBR]   = GSU.vPrgBankReg;
    p[GSU_ROMBR] = GSU.vRomBankReg;
    p[GSU_RAMBR] = GSU.vRamBankReg;
    
    {
        uint16 vCacheBaseReg = GSU.vCacheBaseReg & ~1; // Bit 0 is used as a dirty flag
        p[GSU_CBR]   = (uint8) vCacheBaseReg;
        p[GSU_CBR+1] = (uint8)(vCacheBaseReg>>8);
    }
}

/* Reset the FxChip */
void FxReset(struct FxInit_s *psFxInfo)
{
    logFunctionCall(F_FxReset);
    int i;
    
    /* Clear all internal variables */
    GSU = (struct FxRegs_s) {
        .sregDreg0 = {&GSU.avReg[0], &GSU.avReg[0]},
        .mergeFlagLut = {
            0x00, 0x40, 0x60, 0x60,
            0x70, 0x70, 0x70, 0x70,
            0xf0, 0xf0, 0xf0, 0xf0,
            0xf0, 0xf0, 0xf0, 0xf0
        },
    };

    /* Set default registers */
    GSU.pvSreg = GSU.pvDreg = 0;

    /* Set RAM and ROM pointers */
    GSU.pvRegisters = psFxInfo->pvRegisters;
    GSU.nRamBanks = psFxInfo->nRamBanks;
    GSU.pvRam = psFxInfo->pvRam;
    GSU.nRomBanks = psFxInfo->nRomBanks;
    GSU.pvRom = psFxInfo->pvRom;
    fx_dirtySCBR();

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

    fx_readRegisterSpace();
}

static uint8 fx_checkStartAddress()
{
    logFunctionCall(F_fx_checkStartAddress);
    /* Check if we start inside the cache */
    if(!(GSU.vCacheBaseReg & 1) && R15 >= GSU.vCacheBaseReg && R15 < (GSU.vCacheBaseReg+512U))
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

    /* Check if the start address is valid */
    if(UNLIKELY(!fx_checkStartAddress()))
    {
            GSU.pvRegisters[GSU_SFR] &= ~FLG_G;
            return 0;
    }

    /* Read registers and initialize GSU session */
    fx_readRegisterSpace();

    /* Execute GSU session */
    CF(IRQ);

    if (LIKELY(nInstructions == FX_MAGIC_USE_SPEEDHACK))
        fx_run_asm_speedhack();
    else
        fx_run_asm(nInstructions);

    /* Store GSU registers */
    fx_writeRegisterSpace();

    /* Check for error code */
    return nInstructions;
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
