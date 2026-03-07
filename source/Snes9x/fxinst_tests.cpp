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

static FX_Result packResult(FX_Gsu GSU, bool correctValue, uint16 result, uint16 expected)
{ 
    return (FX_Result) {
        .gsuFlags = packGsuFlags(GSU),
        .armFlags = packArmFlags(GSU.armFlags),
        .correctValue = correctValue,
        .result = result,
        .expected = expected,
    };
}

// Passed in commit 4fc5d97
FX_Result fxtest_adc_r(const FX_Gsu* GSU, uint16 v1, uint16 v2)
{
    int32 resultOld = SUSEX16(v1) + SUSEX16(v2) + SEX16(GSU->vCarry);
    FX_Gsu GSU2;
    GSU2.vCarry = resultOld >= 0x10000;
    GSU2.vZero  = resultOld;
    GSU2.vSign  = resultOld;
    GSU2.vOverflow = ~(v1 ^ v2) & (v2 ^ resultOld) & 0x8000;
    resultOld &= 0xFFFF;
    
    // uint32 armFlagsS = GSU->armFlags << (31 - ARM_C_SHIFT); // Shift carry flag to highest bit
    // uint32 fuckass = (v1 << 16) | ((uint32) (((int32) (armFlagsS)) >> 16)) >> 16;
    uint32 resultNew;
    asm (
        "cmn %4, %4\n\t" // Set the carry flag by adding the shifted carry flag to itself
        "adcs %1, %2, %3, lsl #16\n\t" // Do the actual addition
        "mrs %0, cpsr\n\t"
        : "=r" (GSU2.armFlags), 
          "=r" (resultNew)
        : "r" ((v1 << 16) | UINT16_MAX),
          "r" (v2),
          "r" (GSU->armFlags << (31 - ARM_C_SHIFT)) // Shift carry flag to highest bit
        : "cc"
    );
    resultNew >>= 16;
    if (resultNew == 0)
        GSU2.armFlags |= (ARM_ZERO);
    /*((GSU->armFlags & ARM_CARRY) ? UINT16_MAX : 0)*/
    
    return packResult(GSU2, (uint32) resultOld == resultNew, resultNew, resultOld);
}

// Passed in commit 4fc5d97
FX_Result fxtest_adc_r_weird_optimized(const FX_Gsu* GSU, uint16 v1, uint16 v2)
{
    int32 resultOld = SUSEX16(v1) + SUSEX16(v2) + SEX16(GSU->vCarry);
    FX_Gsu GSU2;
    GSU2.vCarry = resultOld >= 0x10000;
    GSU2.vZero  = resultOld;
    GSU2.vSign  = resultOld;
    GSU2.vOverflow = ~(v1 ^ v2) & (v2 ^ resultOld) & 0x8000;
    resultOld &= 0xFFFF;
    
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
    
    return packResult(GSU2, (uint32) resultOld == resultNew, resultNew, resultOld);
}

// Passed in commit 4fc5d97
FX_Result fxtest_add_r(const FX_Gsu* GSU, uint16 v1, uint16 v2)
{
    int32 resultOld = SUSEX16(v1) + SUSEX16(v2);
    FX_Gsu GSU2;
    GSU2.vCarry = resultOld >= 0x10000;
    GSU2.vZero  = resultOld;
    GSU2.vSign  = resultOld;
    GSU2.vOverflow = ~(v1 ^ v2) & (v2 ^ resultOld) & 0x8000;
    resultOld &= 0xFFFF;

    uint32 resultNew;
    asm (
        "adds %1, %2, %3, lsl #16\n\t"
        "mrs %0, cpsr"
        : "=r" (GSU2.armFlags), "=r" (resultNew)
        : "r" (v1 << 16), "r" (v2)
        : "cc"
    );
    resultNew >>= 16;
    
    return packResult(GSU2, (uint32) resultOld == resultNew, resultNew, resultOld);
}
