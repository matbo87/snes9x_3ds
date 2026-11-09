#include "fxemu.h" // Required because snes9x types are a mess
#include "fxinst.h"
#include "3dssnes9x.h"
#include "fxinst_asm.h"
#include <stdio.h>
#include <string.h>

// Contains various stuff that assists in integrating fxinst_asm.s

#define ASSERT_GSU_OFFSET(field) _Static_assert(((int) offsetof(struct FxRegs_s, field) - GSU_STRUCT_PTR_OFFSET == FX_ ## field), "GSU." #field " offset is incorrect. See fxstatic.cpp.")
#define ASSERT_GSU_REG(r) _Static_assert(((int) offsetof(struct FxRegs_s, avReg[r]) - GSU_STRUCT_PTR_OFFSET == FX_R ## r), "GSU.R" #r " offset is incorrect. See fxstatic.cpp.")

#define appendInternal(f_, d_)                                                                                      \
do {                                                                                                                \
    if (error == errorOk) {                                                                                         \
        int result = snprintf(worker, sizeof(worker), "#define FX_" d_ " O_(%u)\n", offsetof(struct FxRegs_s, f_)); \
        if (result < 0) {                                                                                           \
            error = #d_;                                                                                            \
        } else {                                                                                                    \
            size_t len = strlen(worker);                                                                            \
            if (curLen + len < sizeof(str)) {                                                                       \
                strcat(str + curLen, worker);                                                                       \
                curLen += len;                                                                                      \
            } else                                                                                                  \
                error = #d_;                                                                                        \
        }                                                                                                           \
    }                                                                                                               \
} while(0)

#define append(field_) appendInternal(field_, #field_)
#define append2(field_, define_) appendInternal(field_, define_)

// If printing is enabled, this function is called at boot.
// Hook it with GDB and print str at the breakpoint.
#if PRINT_GSU_OFFSETS == 1

void FX_printGsuOffsets(void)
{
    const char *error = "OK", *errorOk = error; // If an error occurs, this is set to the field's name
    char str[8192];
    char worker[50];
    str[0] = worker[0] = '\0';
    
    size_t curLen = 0;
    
    append(pvPrgBank);
    append2(avReg[0],  "R0");
    append2(avReg[1],  "R1");
    append2(avReg[2],  "R2");
    append2(avReg[3],  "R3");
    append2(avReg[4],  "R4");
    append2(avReg[5],  "R5");
    append2(avReg[6],  "R6");
    append2(avReg[7],  "R7");
    append2(avReg[8],  "R8");
    append2(avReg[9],  "R9");
    append2(avReg[10], "R10");
    append2(avReg[11], "R11");
    append2(avReg[12], "R12");
    append2(avReg[13], "R13");
    append2(avReg[14], "R14");
    append2(avReg[15], "R15");
    append(vCacheBaseReg);
    append(vLastRamAdr);
    append(vColorReg);
    append(vPlotOptionReg);
    append(vScreenHeight);
    append(vScreenRealHeight);
    append(vRomBuffer);
    append(vPrgBankReg);
    append(pvRamBank);
    append(pvRomBank);
    append(sregDreg0);
    append(mergeFlagLut);
    append(vStatusReg);
    append(__pad2);
    append(vRomBankReg);
    append(vRamBankReg);
    append(armFlags);
    append(pvDreg);
    append(pvSreg);
    append(vPipe);
    append(vMode);
    append(__pad3);
    append(nRamBanks);
    append(nRomBanks);
    append(vPrevPlotOptionReg);
    append(__pad1);
    append(x);
    append(apvRamBank);
    append(apvRomBank);
    append(pvScreenBase);
    append(pvRom);
    append(pvRam);
    append(pvRegisters);
    append(vCacheFlags);
    append(apvScreen);

    str[sizeof(str) - 1] = '\0';
    asm volatile ("nop" :: "r" (str), "r" (error)); // Breakpoint. printf "%s\n", str
}

#else // PRINT_GSU_OFFSETS

// Verifies that the offsets are correct.
// Only assert these if printing is disabled.
_Static_assert(offsetof(struct FxRegs_s, avReg) == GSU_STRUCT_PTR_OFFSET, "Overall GSU pointer offset is incorrect. See fxstatic.cpp.");
ASSERT_GSU_OFFSET(pvPrgBank);
ASSERT_GSU_REG(0);
ASSERT_GSU_REG(1);
ASSERT_GSU_REG(2);
ASSERT_GSU_REG(3);
ASSERT_GSU_REG(4);
ASSERT_GSU_REG(5);
ASSERT_GSU_REG(6);
ASSERT_GSU_REG(7);
ASSERT_GSU_REG(8);
ASSERT_GSU_REG(9);
ASSERT_GSU_REG(10);
ASSERT_GSU_REG(11);
ASSERT_GSU_REG(12);
ASSERT_GSU_REG(13);
ASSERT_GSU_REG(14);
ASSERT_GSU_REG(15);
ASSERT_GSU_OFFSET(vCacheBaseReg);
ASSERT_GSU_OFFSET(vLastRamAdr);
ASSERT_GSU_OFFSET(vColorReg);
ASSERT_GSU_OFFSET(vPlotOptionReg);
ASSERT_GSU_OFFSET(vScreenHeight);
ASSERT_GSU_OFFSET(vScreenRealHeight);
ASSERT_GSU_OFFSET(vRomBuffer);
ASSERT_GSU_OFFSET(vPrgBankReg);
ASSERT_GSU_OFFSET(pvRamBank);
ASSERT_GSU_OFFSET(pvRomBank);
ASSERT_GSU_OFFSET(sregDreg0);
ASSERT_GSU_OFFSET(mergeFlagLut);
ASSERT_GSU_OFFSET(vStatusReg);
ASSERT_GSU_OFFSET(__pad2);
ASSERT_GSU_OFFSET(vRomBankReg);
ASSERT_GSU_OFFSET(vRamBankReg);
ASSERT_GSU_OFFSET(armFlags);
ASSERT_GSU_OFFSET(pvDreg);
ASSERT_GSU_OFFSET(pvSreg);
ASSERT_GSU_OFFSET(vPipe);
ASSERT_GSU_OFFSET(vMode);
ASSERT_GSU_OFFSET(__pad3);
ASSERT_GSU_OFFSET(nRamBanks);
ASSERT_GSU_OFFSET(nRomBanks);
ASSERT_GSU_OFFSET(vPrevPlotOptionReg);
ASSERT_GSU_OFFSET(__pad1);
ASSERT_GSU_OFFSET(x);
ASSERT_GSU_OFFSET(apvRamBank);
ASSERT_GSU_OFFSET(apvRomBank);
ASSERT_GSU_OFFSET(pvScreenBase);
ASSERT_GSU_OFFSET(pvRom);
ASSERT_GSU_OFFSET(pvRam);
ASSERT_GSU_OFFSET(pvRegisters);
ASSERT_GSU_OFFSET(vCacheFlags);
ASSERT_GSU_OFFSET(apvScreen);

#endif // PRINT_GSU_OFFSETS
