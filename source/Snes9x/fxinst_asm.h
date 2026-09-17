#ifndef FXINST_ASM_H
#define FXINST_ASM_H

// GSU struct offsets. These have to be hard-coded for use in assembly files.
// If you modify the struct layout, you MUST update these.
// See fxstatic.cpp and fxinst.h.

#include "../3dssnes9x.h"

#define GSU_STRUCT_PTR_OFFSET 4

// Bad GSU offsets can cause the assembly file to fail to compile. This works around that.
#if PRINT_GSU_OFFSETS == 0
#define O_(v_) (v_ - GSU_STRUCT_PTR_OFFSET)
#else
#define O_(v_) (0)
#endif


#define FX_pvPrgBank            O_(0)
#define FX_R0                   O_(4)
#define FX_R1                   O_(6)
#define FX_R2                   O_(8)
#define FX_R3                   O_(10)
#define FX_R4                   O_(12)
#define FX_R5                   O_(14)
#define FX_R6                   O_(16)
#define FX_R7                   O_(18)
#define FX_R8                   O_(20)
#define FX_R9                   O_(22)
#define FX_R10                  O_(24)
#define FX_R11                  O_(26)
#define FX_R12                  O_(28)
#define FX_R13                  O_(30)
#define FX_R14                  O_(32)
#define FX_R15                  O_(34)
#define FX_vCacheBaseReg        O_(36)
#define FX_vLastRamAdr          O_(38)
#define FX_vColorReg            O_(40)
#define FX_vPlotOptionReg       O_(41)
#define FX_vScreenHeight        O_(42)
#define FX_vScreenRealHeight    O_(44)
#define FX_vRomBuffer           O_(46)
#define FX_vPrgBankReg          O_(47)
#define FX_pvRamBank            O_(48)
#define FX_pvRomBank            O_(52)
#define FX_sregDreg0            O_(56)
#define FX_mergeFlagLut         O_(64)
#define FX_vStatusReg           O_(80)
#define FX_vPrevScreenHeight    O_(82)
#define FX_vRomBankReg          O_(84)
#define FX_vRamBankReg          O_(85)
#define FX_armFlags             O_(86)
#define FX_pvDreg               O_(87)
#define FX_pvSreg               O_(88)
#define FX_vPipe                O_(89)
#define FX_vMode                O_(90)
#define FX_vPrevMode            O_(91)
#define FX_nRamBanks            O_(92)
#define FX_nRomBanks            O_(93)
#define FX_vPrevPlotOptionReg   O_(94)
#define FX___pad1               O_(95)
#define FX_x                    O_(96)
#define FX_apvRamBank           O_(224)
#define FX_apvRomBank           O_(240)
#define FX_pvScreenBase         O_(1264)
#define FX_pvRom                O_(1268)
#define FX_pvRam                O_(1272)
#define FX_pvRegisters          O_(1276)
#define FX_vCacheFlags          O_(1280)
#define FX_apvScreen            O_(1284)

#endif
