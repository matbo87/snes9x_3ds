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

// Passed in commit 2d6b68f
FX_Result fxtest_lsr(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = USEX16(v1) >> 1;
    GSU.vCarry = v1 & 1;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // WYATT_TODO we need to preserve the overflow flag.
    uint32 resultNew;
        asm (
        "cmn %3, %3\n\t" // Copy GSU overflow to CPSR
        "lsrs %1, %2, #1\n\t"
        "mrs %0, cpsr\n\t"
        : "=r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1),
          "r" (GSU.armFlags << (31 - ARM_V_SHIFT)) // Move overflow to bit 31
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 2d6b68f
FX_Result fxtest_rol(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = USEX16((v1 << 1) + GSU.vCarry);
    GSU.vCarry = (v1 >> 15) & 1;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // We need to preserve the overflow flag
    // uint32 resultNew;
    // asm (
    //     // "cmn %3, %3\n\t" // Set the carry flag by adding the shifted carry flag to itself
    //     "rors %1, %2, #31\n\t" // Rotate left by 1
    //     "mrs %0, cpsr\n\t"
    //     : "=r" (GSU.armFlags),
    //       "=r" (resultNew)
    //     : "r" ((((int32) (int16) v1) & ~BIT(31)) | (BIT(31) & (GSU.armFlags << (31 - ARM_C_SHIFT)))) // Sign-extended
    //     : "cc"
    // );

    // Software implementation. 4 instructions slower than the hardware version.
    // uint32 resultNew = USEX16((v1 << 1) + ((GSU.armFlags & ARM_CARRY) >> ARM_C_SHIFT));
    // GSU.armFlags &= ~(ARM_CARRY | ARM_NEGATIVE | ARM_ZERO);
    // GSU.armFlags |=
    //    ((v1 >> 15) << ARM_C_SHIFT)
    //  | ((resultNew & 0x8000) << (ARM_N_SHIFT - 15))
    //  | (resultNew == 0 ? ARM_ZERO : 0);

    // 11 instructions total. More than before, but it's an uncommon instruction
    uint32 resultNew, armFlagsTmp;
    asm (
        "cmn %3, %3\n\t" // Set the carry flag by adding the shifted carry flag to itself
        "adcs %1, %2, %2\n\t"  // This emulates a rotate left w/ carry, but needs both halfwords to be the same
        "mrs %0, cpsr\n\t"
        : "=r" (armFlagsTmp),
          "=r" (resultNew)
        : "r" (v1 | (v1 << 16)),
          "r" (GSU.armFlags << (31 - ARM_C_SHIFT)) // Shift carry flag to highest bit
        : "cc"
    );
    GSU.armFlags = (armFlagsTmp & ~ARM_OVERFLOW) | (GSU.armFlags & ARM_OVERFLOW);
    if ((resultNew << 16) == 0) GSU.armFlags |= ARM_ZERO;

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 2d6b68f
FX_Result fxtest_loop(const FX_Gsu* GSUi, const uint16 r12)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = GSU.vSign = GSU.vZero = (r12 - 1);

    // Software implementation. Seems about 3 instruction slower.
    // uint32 resultNew = r12 - 1;
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // GSU.armFlags |= ((resultNew & 0x8000) << (ARM_N_SHIFT - 15))
    //              |   (resultNew == 0 ? ARM_ZERO : 0);

    // Hardware implementation
    // WYATT_TODO this can probably be optimized but my brain has turned to MUSH.
    // Specifically, we can probably always get SUBS to output 0 for the O and C
    // flags, saving us the bit clears below. We could also probably use the
    // MRS-with-shift encoding to optimize flag preservation, but the assembler
    // doesn't support it.
    uint32 resultNew, armFlagsTmp;
    asm (
        "subs %1, %2, #65536\n\t"
        "mrs %0, cpsr"
        : "=r" (armFlagsTmp), "=r" (resultNew)
        : "r" (r12 << 16)
        : "cc"
    );
    resultNew >>= 16;
    GSU.armFlags = (armFlagsTmp & ~(ARM_OVERFLOW | ARM_CARRY)) | (GSU.armFlags & (ARM_OVERFLOW | ARM_CARRY));

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 767428a
FX_Result fxtest_swap(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld;
    asm ("rev16 %0, %1":"=r"(resultOld):"r"(v1));
    GSU.vSign = GSU.vZero = resultOld;

    // This probably can't be optimized further, but it's not that common so it's ok
    uint32 resultNew;
    asm ("rev16 %0, %1":"=r"(resultNew):"r"(v1));
    GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    GSU.armFlags |= ((resultNew & 0x8000) ? ARM_NEGATIVE : 0) | ((USEX16(resultNew) == 0) ? ARM_ZERO : 0);

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 767428a
FX_Result fxtest_not(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = ~v1;
    GSU.vSign = GSU.vZero = resultOld;

    // Software implementation
    // uint32 resultNew = ~v1;
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // GSU.armFlags |= ((resultNew & 0x8000) ? ARM_NEGATIVE : 0) | ((USEX16(resultNew) == 0) ? ARM_ZERO : 0);

    GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    uint32 resultNew;
    asm (
        "mvns %1, %2\n\t"
        "orreq %0, %0, %3\n\t"
        "orrmi %0, %0, %4\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" ((v1 << 16) | v1),
          "i" (ARM_ZERO),
          "i" (ARM_NEGATIVE)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 2d6b68f
FX_Result fxtest_add_r(const FX_Gsu* GSUi, const uint16 v1, const uint16 v2)
{
    FX_Gsu GSU = *GSUi;

    int32 resultOld = SUSEX16(v1) + SUSEX16(v2);
    GSU.vCarry = resultOld >= 0x10000;
    GSU.vZero  = resultOld;
    GSU.vSign  = resultOld;
    GSU.vOverflow = ~(v1 ^ v2) & (v2 ^ resultOld) & 0x8000;

    uint32 resultNew;
    asm (
        "adds %1, %2, %3, lsl #16\n\t"
        "mrs %0, cpsr"
        : "=r" (GSU.armFlags), "=r" (resultNew)
        : "r" (v1 << 16), "r" (v2)
        : "cc"
    );
    resultNew >>= 16;
    
    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 2d6b68f
FX_Result fxtest_adc_r(const FX_Gsu* GSUi, const uint16 v1, const uint16 v2)
{
    FX_Gsu GSU = *GSUi;

    int32 resultOld = SUSEX16(v1) + SUSEX16(v2) + SEX16(GSU.vCarry);
    GSU.vCarry = resultOld >= 0x10000;
    GSU.vZero  = resultOld;
    GSU.vSign  = resultOld;
    GSU.vOverflow = ~(v1 ^ v2) & (v2 ^ resultOld) & 0x8000;
    
    uint32 armFlagsShifted = GSU.armFlags << (31 - ARM_C_SHIFT); // Shift carry flag to highest bit
    uint32 v1Shift = (v1 << 16) | ((uint32) (((int32) (armFlagsShifted)) >> 15)) >> 16; // Lower 16 bits are filled with carry flag
    uint32 resultNew;
    asm (
        "cmn %4, %4\n\t" // Set the carry flag by adding the shifted carry flag to itself
        "adcs %1, %2, %3, lsl #16\n\t" // Do the actual addition
        "mrs %0, cpsr\n\t"
        : "=r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1Shift),
          "r" (v2),
          "r" (armFlagsShifted)
        : "cc"
    );
    resultNew >>= 16;
    
    return packResult(GSU, resultNew, resultOld);
}

FX_Result fxtest_add_i(const FX_Gsu* GSUi, const uint16 v1, const uint8 imm)
{
    FX_Gsu GSU = *GSUi;

    int32 resultOld = SUSEX16(v1) + imm;
    GSU.vCarry = resultOld >= 0x10000;
    GSU.vZero  = resultOld;
    GSU.vSign  = resultOld;
    GSU.vOverflow = ~(v1 ^ imm) & (imm ^ resultOld) & 0x8000;

    uint32 resultNew;
    asm (
        "adds %1, %2, %3, lsl #16\n\t"
        "mrs %0, cpsr"
        : "=r" (GSU.armFlags), "=r" (resultNew)
        : "r" (v1 << 16), "r" (imm)
        : "cc"
    );
    resultNew >>= 16;
    
    return packResult(GSU, resultNew, resultOld);
}

FX_Result fxtest_adc_i(const FX_Gsu* GSUi, const uint16 v1, const uint8 imm)
{
    FX_Gsu GSU = *GSUi;

    int32 resultOld = SUSEX16(v1) + imm + SEX16(GSU.vCarry);
    GSU.vCarry = resultOld >= 0x10000;
    GSU.vZero  = resultOld;
    GSU.vSign  = resultOld;
    GSU.vOverflow = ~(v1 ^ imm) & (imm ^ resultOld) & 0x8000;
    
    uint32 armFlagsShifted = GSU.armFlags << (31 - ARM_C_SHIFT); // Shift carry flag to highest bit
    uint32 v1Shift = (v1 << 16) | ((uint32) (((int32) (armFlagsShifted)) >> 15)) >> 16; // Lower 16 bits are filled with carry flag
    uint32 resultNew;
    asm (
        "cmn %4, %4\n\t" // Set the carry flag by adding the shifted carry flag to itself
        "adcs %1, %2, %3, lsl #16\n\t" // Do the actual addition
        "mrs %0, cpsr\n\t"
        : "=r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1Shift),
          "r" (imm),
          "r" (armFlagsShifted)
        : "cc"
    );
    resultNew >>= 16;
    
    return packResult(GSU, resultNew, resultOld);
}
