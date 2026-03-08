#include "fxinst_tests.h"
#include <stdint.h>
// WYATT_TODO guard the compilation of this around whether the tests are enabled or not

// Shifts ARM flags down to the LSBs of a u8
static uint8 packArmFlags(uint32 armFlags)
{
    return (armFlags & (ARM_NEGATIVE | ARM_ZERO | ARM_CARRY | ARM_OVERFLOW)) >> 28;
}

// Packs GSU flags into the LSBs of a u8
static uint8 packGsuFlags(FX_Gsu GSU)
{
    return (TEST_S ? PACKED_N : 0) | (TEST_Z ? PACKED_Z : 0) | (TEST_CY ? PACKED_C : 0) | (TEST_OV ? PACKED_V : 0);
}

static FX_Result packResult(FX_Gsu GSU, uint16 result, uint16 expected)
{ 
    return (FX_Result) {
        .gsuFlags = packGsuFlags(GSU),
        .armFlags = packArmFlags(GSU.armFlags),
        .result = result,
        .expected = expected,
    };
}

// Passed in commit ab942e0
FX_Result fxtest_lsr(const FX_Gsu* GSU, uint16 v1)
{
    FX_Gsu GSU2 = *GSU;

    uint32 resultOld = USEX16(v1) >> 1;
    GSU2.vCarry = v1 & 1;
    GSU2.vSign = resultOld;
    GSU2.vZero = resultOld;

    // WYATT_TODO we need to preserve the overflow flag.
    uint32 resultNew;
        asm (
        "cmn %3, %3\n\t" // Copy GSU overflow to CPSR
        "lsrs %1, %2, #1\n\t"
        "mrs %0, cpsr\n\t"
        : "=r" (GSU2.armFlags),
          "=r" (resultNew)
        : "r" (v1),
          "r" (GSU->armFlags << (31 - ARM_V_SHIFT)) // Move overflow to bit 31
        : "cc"
    );

    return packResult(GSU2, resultNew, resultOld);
}

// Passed in commit ea737fc
FX_Result fxtest_rol(const FX_Gsu* GSU, uint16 v1)
{
    FX_Gsu GSU2 = *GSU;

    uint32 resultOld = USEX16((v1 << 1) + GSU->vCarry);
    GSU2.vCarry = (v1 >> 15) & 1;
    GSU2.vSign = resultOld;
    GSU2.vZero = resultOld;

    // We need to preserve the overflow flag
    // uint32 resultNew;
    // asm (
    //     // "cmn %3, %3\n\t" // Set the carry flag by adding the shifted carry flag to itself
    //     "rors %1, %2, #31\n\t" // Rotate left by 1
    //     "mrs %0, cpsr\n\t"
    //     : "=r" (GSU2.armFlags),
    //       "=r" (resultNew)
    //     : "r" ((((int32) (int16) v1) & ~BIT(31)) | (BIT(31) & (GSU->armFlags << (31 - ARM_C_SHIFT)))) // Sign-extended
    //     : "cc"
    // );

    // 11 instructions total. More than before, but it's an uncommon instruction
    uint32 resultNew, armFlagsTmp;
    asm (
        "cmn %3, %3\n\t" // Set the carry flag by adding the shifted carry flag to itself
        "adcs %1, %2, %2\n\t"  // This emulates a rotate left w/ carry, but needs both halfwords to be the same
        "mrs %0, cpsr\n\t"
        : "=r" (armFlagsTmp),
          "=r" (resultNew)
        : "r" (v1 | (v1 << 16)),
          "r" (GSU->armFlags << (31 - ARM_C_SHIFT)) // Shift carry flag to highest bit
        : "cc"
    );
    GSU2.armFlags = (armFlagsTmp & ~ARM_OVERFLOW) | (GSU2.armFlags & ARM_OVERFLOW);
    if ((resultNew << 16) == 0) GSU2.armFlags |= ARM_ZERO;

    return packResult(GSU2, resultNew, resultOld);
}

// Passed in commit ab942e0
FX_Result fxtest_add_r(const FX_Gsu* GSU, uint16 v1, uint16 v2)
{
    FX_Gsu GSU2 = *GSU;

    int32 resultOld = SUSEX16(v1) + SUSEX16(v2);
    GSU2.vCarry = resultOld >= 0x10000;
    GSU2.vZero  = resultOld;
    GSU2.vSign  = resultOld;
    GSU2.vOverflow = ~(v1 ^ v2) & (v2 ^ resultOld) & 0x8000;

    uint32 resultNew;
    asm (
        "adds %1, %2, %3, lsl #16\n\t"
        "mrs %0, cpsr"
        : "=r" (GSU2.armFlags), "=r" (resultNew)
        : "r" (v1 << 16), "r" (v2)
        : "cc"
    );
    resultNew >>= 16;
    
    return packResult(GSU2, resultNew, resultOld);
}

// Passed in commit 972ae9a
FX_Result fxtest_adc_r(const FX_Gsu* GSU, uint16 v1, uint16 v2)
{
    FX_Gsu GSU2 = *GSU;

    int32 resultOld = SUSEX16(v1) + SUSEX16(v2) + SEX16(GSU->vCarry);
    GSU2.vCarry = resultOld >= 0x10000;
    GSU2.vZero  = resultOld;
    GSU2.vSign  = resultOld;
    GSU2.vOverflow = ~(v1 ^ v2) & (v2 ^ resultOld) & 0x8000;
    
    uint32 armFlagsShifted = GSU->armFlags << (31 - ARM_C_SHIFT); // Shift carry flag to highest bit
    uint32 v1Shift = (v1 << 16) | ((uint32) (((int32) (armFlagsShifted)) >> 15)) >> 16; // Lower 16 bits are filled with carry flag
    uint32 resultNew;
    asm (
        "cmn %4, %4\n\t" // Set the carry flag by adding the shifted carry flag to itself
        "adcs %1, %2, %3, lsl #16\n\t" // Do the actual addition
        "mrs %0, cpsr\n\t"
        : "=r" (GSU2.armFlags),
          "=r" (resultNew)
        : "r" (v1Shift),
          "r" (v2),
          "r" (armFlagsShifted)
        : "cc"
    );
    resultNew >>= 16;
    
    return packResult(GSU2, resultNew, resultOld);
}
