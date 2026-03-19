#include "fxinst_tests.h"
#include <stdint.h>
// WYATT_TODO guard the compilation of this around whether the tests are enabled or not

/* Correctness tests for FX instruction emulation using native ARM flags
 * These tests, at present, are intended solely to test CORRECTNESS.
 * Feed them every possible input, and the new and old versions should
 * give identical results. This includes flag preservation and taking
 * flag OR-ing into account! (Though most tests currently haven't been
 * run with all flag permutations yet. I'll do that at the end).
 * 
 * Ideally, "software" AND "hardware" versions of each test should be
 * written. Software versions are essentially copies of the original
 * code with immediate flag evaluation, while hardware versions use
 * optimized ARM-specific assembly code.
 * 
 * Generally speaking, the compiler is quite poor at evaluating flags.
 * It tends to produce a comparison instruction for each flag, as well
 * as sometimes producing branches. The branches are *probably* just
 * an artifact of how the tests include two implementations at once;
 * for actual performance tests, these will need to be split into
 * completely separate functions.
 * 
 * Performance is tested here and there. The long and short FOR NOW is:
 * - Use MRS and MSR instructions to shrink code whenever possible.
 *   These instructions execute in a single cycle, so long as MSR
 *   instructions only ever target cpsr_f. NEVER use any other form
 *   of MSR unless absolutely necessary. MRS is always single-cycle.
 * - Manual instruction counting is a skill and you should hone it.
 * - Learn everything you can about the hardware. There exist many
 *   clever solutions to ugly problems.
 * - Know which instructions are common and which are rare. Rarer
 *   instructions may be better off with small solutions to
 *   minimize code size.
 */

#define ALIGNED16 __attribute__((aligned(16)))

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

static FX_Result32 packResult32(FX_Gsu GSU, uint32 result, uint32 expected)
{ 
    return (FX_Result32) {
        .gsuFlags = packGsuFlags(GSU),
        .armFlags = packArmFlags(GSU.armFlags),
        .result = result,
        .expected = expected,
    };
}

static FX_Result32 packResultDual16(FX_Gsu GSU, uint16 resultH, uint16 resultL, uint16 expectedH, uint16 expectedL)
{
    return packResult32(GSU, (resultH << 16) | resultL, (expectedH << 16) | expectedL);
}

// Passed in commit 104bf93 (full NZCV)
FX_Result fxtest_lsr(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = USEX16(v1) >> 1;
    GSU.vCarry = v1 & 1;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    uint32 resultNew;
    asm (
        "msr cpsr_f, %0\n\t"
        "lsrs %1, %2, #1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 104bf93 (full NZCV)
FX_Result fxtest_rol(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = USEX16((v1 << 1) + GSU.vCarry);
    GSU.vCarry = (v1 >> 15) & 1;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // uint32 resultNew = USEX16((v1 << 1) + ((GSU.armFlags & ARM_CARRY) >> ARM_C_SHIFT));
    // GSU.armFlags &= ~(ARM_CARRY | ARM_NEGATIVE | ARM_ZERO);
    // GSU.armFlags |=
    //    ((v1 >> 15) << ARM_C_SHIFT)
    //  | ((resultNew & 0x8000) << (ARM_N_SHIFT - 15))
    //  | (resultNew == 0 ? ARM_ZERO : 0);

    // 6 instructions
    uint32 resultNew;
    asm (
        "msr cpsr_f, %0\n\t"
        "lsl %1, %2, #16\n\t"
        "orrcs %1, %1, %3\n\t"
        "lsls %1, %1, #1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1),
          "i" (BIT(15))
        : "cc"
    );
    resultNew >>= 16;

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 104bf93 (full NZCV)
FX_Result fxtest_loop(const FX_Gsu* GSUi, const uint16 R12)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = R12 - 1;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // uint32 resultNew = R12 - 1;
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // GSU.armFlags |= ((resultNew & 0x8000) << (ARM_N_SHIFT - 15))
    //              |   (resultNew == 0 ? ARM_ZERO : 0);

    // Identical to fxtest_dec
    uint32 resultNew = R12 - 1;
    asm (
        "msr cpsr_f, %0\n\t"
        "lsl %0, %1, #16\n\t"
        "movs %0, %0\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags)
        : "r" (resultNew)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 104bf93 (full NZCV)
FX_Result fxtest_swap(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld;
    asm ("rev16 %0, %1":"=r"(resultOld):"r"(v1));
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // uint32 resultNew;
    // asm ("rev16 %0, %1":"=r"(resultNew):"r"(v1));
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // GSU.armFlags |= ((resultNew & 0x8000) ? ARM_NEGATIVE : 0) | ((USEX16(resultNew) == 0) ? ARM_ZERO : 0);

    uint32 resultNew;
    asm ("rev16 %0, %1":"=r"(resultNew):"r"(v1));
    asm (
        "msr cpsr_f, %0\n\t"
        "movs %1, %1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (resultNew | (resultNew << 16))
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 104bf93 (full NZCV)
FX_Result fxtest_not(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = ~v1;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // Software implementation
    // uint32 resultNew = ~v1;
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // GSU.armFlags |= ((resultNew & 0x8000) ? ARM_NEGATIVE : 0) | ((USEX16(resultNew) == 0) ? ARM_ZERO : 0);

    uint32 resultNew;
    asm (
        "msr cpsr_f, %0\n\t"
        "mvns %1, %2\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" ((v1 << 16) | v1)
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

// Passed in commit 89e3b57 (full NZCV)
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

// Passed in commit 89e3b57 (full NZCV)
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
        "msr cpsr_f, %0\n\t" // Copy in the carry flag
        "adcs %1, %2, %3, lsl #16\n\t" // Do the actual addition
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1Shift),
          "r" (imm)
        : "cc"
    );
    resultNew >>= 16;
    
    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit ee690e1
FX_Result fxtest_sub_r(const FX_Gsu* GSUi, const uint16 v1, const uint16 v2)
{
    FX_Gsu GSU = *GSUi;
    
    int32 resultOld = SUSEX16(v1) - SUSEX16(v2);
    GSU.vCarry = resultOld >= 0;
    GSU.vOverflow = (v1 ^ v2) & (v1 ^ resultOld) & 0x8000;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    uint32 resultNew;
    asm (
        "subs %1, %2, %3, lsl #16\n\t"
        "mrs %0, cpsr"
        : "=r" (GSU.armFlags), "=r" (resultNew)
        : "r" ((v1 << 16)), "r" (v2)
        : "cc"
    );
    resultNew >>= 16;

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 21402fb
FX_Result fxtest_sbc_r(const FX_Gsu* GSUi, const uint16 v1, const uint16 v2)
{
    FX_Gsu GSU = *GSUi;
    
    int32 resultOld = SUSEX16(v1) - SUSEX16(v2) - (SUSEX16(GSU.vCarry^1));
    GSU.vCarry = resultOld >= 0;
    GSU.vOverflow = (v1 ^ v2) & (v1 ^ resultOld) & 0x8000;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    uint32 armFlagsShifted = GSU.armFlags << (31 - ARM_C_SHIFT); // Shift carry flag to highest bit
    uint32 resultNew;
    asm (
        "cmn %4, %4\n\t" // Set the carry flag by adding the shifted carry flag to itself
        "sbcs %1, %2, %3, lsl #16\n\t" // Do the actual subtraction
        "mrs %0, cpsr\n\t"
        : "=r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1 << 16),
          "r" (v2),
          "r" (armFlagsShifted)
        : "cc"
    );
    resultNew >>= 16;
    if (resultNew == 0) GSU.armFlags |= ARM_ZERO;

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 89e3b57 (full NZCV)
FX_Result fxtest_sub_i(const FX_Gsu* GSUi, const uint16 v1, const uint8 imm)
{
    FX_Gsu GSU = *GSUi;

    int32 resultOld = SUSEX16(v1) - imm;
    GSU.vCarry = resultOld >= 0;
    GSU.vOverflow = (v1 ^ imm) & (v1 ^ resultOld) & 0x8000;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    uint32 resultNew;
    asm (
        "subs %1, %2, %3, lsl #16\n\t"
        "mrs %0, cpsr"
        : "=r" (GSU.armFlags), "=r" (resultNew)
        : "r" ((v1 << 16)), "r" (imm)
        : "cc"
    );
    resultNew >>= 16;

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 6891c49
FX_Result fxtest_cmp_r(const FX_Gsu* GSUi, const uint16 v1, const uint16 v2)
{
    FX_Gsu GSU = *GSUi;

    int32 resultOld = SUSEX16(v1) - SUSEX16(v2);
    GSU.vCarry = resultOld >= 0;
    GSU.vOverflow = (v1 ^ v2) & (v1 ^ resultOld) & 0x8000;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    asm (
        "cmp %1, %2, lsl #16\n\t"
        "mrs %0, cpsr"
        : "=r" (GSU.armFlags)
        : "r" ((v1 << 16)), "r" (v2)
        : "cc"
    );

    return packResult(GSU, 0, 0);
}

// Passed in commit c62ee05
FX_Result fxtest_merge(const FX_Gsu* GSUi, const uint16 R7, const uint16 R8)
{
    FX_Gsu GSU = *GSUi;

    uint32 vOld = (R7 & 0xff00) | ((R8 & 0xff00) >> 8);
    GSU.vOverflow = (vOld & 0xc0c0) << 16;
    GSU.vZero = !(vOld & 0xf0f0);
    GSU.vSign = ((vOld | (vOld << 8)) & 0x8000);
    GSU.vCarry = (vOld & 0xe0e0) != 0;

    // Non-LUT version
    // uint32 vNew = (R7 & 0xff00) | ((R8 & 0xff00) >> 8);
    // bool  vZero     = (vNew & 0xf0f0) != 0;            // High 4 bits == 0
    // bool  vCarry    = (vNew & 0xe0e0) != 0;            // High 3 bits != 0
    // int32 ov        = (vNew & 0xc0c0) << 16;           // High 2 bits >= 0x80 || < -0x80
    // bool  vSign     = ((vNew | (vNew << 8)) & 0x8000); // High 1 bit is set
    // bool  vOverflow = (ov >= 0x8000 || ov < -0x8000);

    // GSU.armFlags |= vSign     ? PACKED_N : 0;
    // GSU.armFlags |= vOverflow ? PACKED_V : 0;
    // GSU.armFlags |= vCarry    ? PACKED_C : 0;
    // GSU.armFlags |= vZero     ? PACKED_Z : 0;
    // GSU.armFlags <<= ARM_V_SHIFT;

    // WYATT_TODO probably move this to the GSU struct for the actual implementation
    static ALIGNED16 const uint8 flags[16] = {
        0x0, 0x4, 0x6, 0x6,
        0x7, 0x7, 0x7, 0x7,
        0xf, 0xf, 0xf, 0xf,
        0xf, 0xf, 0xf, 0xf
    };

    uint32 vNew = (R7 & 0xff00) | ((R8 & 0xff00) >> 8);
    uint32 offset = ((vNew >> 12) | (vNew >> 4)) & 0b1111;
    GSU.armFlags = flags[offset] << ARM_SHIFT;

    return packResult(GSU, vNew, vOld);
}

// Passed in commit 547689d
FX_Result fxtest_and_r(const FX_Gsu* GSUi, const uint16 v1, const uint16 v2)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = v1 & v2;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // Software implementation (7 instructions, but generates a branch that probably wouldn't be present in fxinst)
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // uint32 resultNew = v1 & v2;
    // if (resultNew & 0x8000) GSU.armFlags |= ARM_NEGATIVE;
    // if (resultNew == 0) GSU.armFlags |= ARM_ZERO;

    // Hardware implementation (6 instructions)
    uint32 resultNew;
    GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    asm (
        "ands %1, %2, %3\n\t"
        "orreq %0, %0, %4\n\t"
        "orrmi %0, %0, %5\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1 | (v1 << 16)),
          "r" (v2 | (v2 << 16)),
          "i" (ARM_ZERO),
          "i" (ARM_NEGATIVE)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 547689d
FX_Result fxtest_bic_r(const FX_Gsu* GSUi, const uint16 v1, const uint16 v2)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = v1 & ~v2;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // Software implementation (a lot of instructions, but generates a branch that probably wouldn't be present in fxinst)
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // uint32 resultNew = v1 & ~v2;
    // if (resultNew & 0x8000) GSU.armFlags |= ARM_NEGATIVE;
    // if (resultNew == 0) GSU.armFlags |= ARM_ZERO;

    // Hardware implementation (6 instructions)
    uint32 resultNew;
    GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    asm (
        "bics %1, %2, %3\n\t"
        "orreq %0, %0, %4\n\t"
        "orrmi %0, %0, %5\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1 | (v1 << 16)),
          "r" (v2 | (v2 << 16)),
          "i" (ARM_ZERO),
          "i" (ARM_NEGATIVE)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 89e3b57 (full NZCV)
FX_Result fxtest_and_i(const FX_Gsu* GSUi, const uint16 v1, const uint8 imm)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = v1 & imm;
    GSU.vSign = resultOld; // Always positive due to the range of imm
    GSU.vZero = resultOld;

    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // uint32 resultNew = v1 & imm;
    // if (resultNew == 0) GSU.armFlags |= ARM_ZERO;

    uint32 resultNew;
    asm (
        "msr cpsr_f, %0\n\t"
        "ands %1, %2, %3\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1),
          "r" (imm)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 89e3b57 (full NZCV)
FX_Result fxtest_bic_i(const FX_Gsu* GSUi, const uint16 v1, const uint8 imm)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = v1 & ~imm;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // Software implementation (7 instructions)
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // uint32 resultNew = v1 & ~imm;
    // if (resultNew & 0x8000) GSU.armFlags |= ARM_NEGATIVE;
    // if (resultNew == 0) GSU.armFlags |= ARM_ZERO;

    // Hardware implementation (6 instructions)
    uint32 resultNew;
    asm (
        "msr cpsr_f, %0\n\t"
        "bics %1, %2, %3\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1 | (v1 << 16)),
          "r" (imm | (imm << 16))
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 4f5fc97 (run with 8-bit register range)
FX_Result fxtest_mult_r(const FX_Gsu* GSUi, const uint16 v1, const uint16 v2)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = SEX8(v1) * SEX8(v2);
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // uint32 resultNew = SEX8(v1) * SEX8(v2);
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // if (USEX16(resultNew) == 0) GSU.armFlags |= ARM_ZERO;
    // if (resultNew & 0x8000) GSU.armFlags |= ARM_NEGATIVE;
    
    GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    uint32 tmp, resultNew = SEX8(v1) * SEX8(v2);
    asm (
        "movs %1, %2\n\t"
        "orreq %0, %0, %3\n\t"
        "orrmi %0, %0, %4\n\t"
        : "+r" (GSU.armFlags),
          "=r" (tmp)
        : "r" (resultNew),
          "i" (ARM_ZERO),
          "i" (ARM_NEGATIVE)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 4f5fc97 (run with 8-bit register range)
FX_Result fxtest_umult_r(const FX_Gsu* GSUi, const uint16 v1, const uint16 v2)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = USEX8(v1) * USEX8(v2);
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // uint32 resultNew = USEX8(v1) * USEX8(v2);
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // if (USEX16(resultNew) == 0) GSU.armFlags |= ARM_ZERO;
    // if (resultNew & 0x8000) GSU.armFlags |= ARM_NEGATIVE;

    GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    uint32 tmp, resultNew = USEX8(v1) * USEX8(v2);
    asm (
        "lsls %1, %2, #16\n\t"
        "orreq %0, %0, %3\n\t"
        "orrmi %0, %0, %4\n\t"
        : "+r" (GSU.armFlags),
          "=r" (tmp)
        : "r" (resultNew),
          "i" (ARM_ZERO),
          "i" (ARM_NEGATIVE)
        : "cc"
    );
    
    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 89e3b57 (full NZCV)
FX_Result fxtest_mult_i(const FX_Gsu* GSUi, const uint16 v1, const uint8 imm)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = SEX8(v1) * imm;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // uint32 resultNew = SEX8(v1) * imm;
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // if (USEX16(resultNew) == 0) GSU.armFlags |= ARM_ZERO;
    // if (resultNew & 0x8000) GSU.armFlags |= ARM_NEGATIVE;
    
    uint32 resultNew = SEX8(v1) * imm;
    asm (
        "msr cpsr_f, %0\n\t"
        "movs %0, %1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags)
        : "r" (resultNew)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 89e3b57 (full NZCV)
FX_Result fxtest_umult_i(const FX_Gsu* GSUi, const uint16 v1, const uint8 imm)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = USEX8(v1) * imm;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // uint32 resultNew = USEX8(v1) * imm;
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // if (USEX16(resultNew) == 0) GSU.armFlags |= ARM_ZERO;
    // if (resultNew & 0x8000) GSU.armFlags |= ARM_NEGATIVE;
    
    // It would be faster to use muls and eschew the movs instruction.
    // However, multiplication has high latency, and doing it this
    // way lets the compiler reorder instructions. Ideally we'd do
    // it manually, but that would require a lot more ASM.
    uint32 resultNew = USEX8(v1) * imm;
    asm (
        "msr cpsr_f, %0\n\t"
        "movs %0, %1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags)
        : "r" (resultNew)
        : "cc"
    );
    
    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 104bf93 (full NZCV)
FX_Result fxtest_sex(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = SEX8(v1);
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // uint32 resultNew = SEX8(v1);
    // if (resultNew & 0x8000) GSU.armFlags |= ARM_NEGATIVE;
    // if (USEX16(resultNew) == 0) GSU.armFlags |= ARM_ZERO;
    
    uint32 resultNew;
    asm (
        "msr cpsr_f, %0\n\t"
        "movs %1, %2\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (SEX8(v1))
        : "cc"
    );
    
    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 104bf93 (full NZCV)
FX_Result fxtest_asr(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;
    
    GSU.vCarry = v1 & 1;
    uint32 resultOld = (uint32)(SEX16(v1)>>1);
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO | ARM_CARRY);
    // GSU.armFlags |= (v1 & 1) << ARM_C_SHIFT;
    // uint32 resultNew = (uint32)(SEX16(v1)>>1);
    // if (resultNew & 0x8000) GSU.armFlags |= ARM_NEGATIVE;
    // if (USEX16(resultNew) == 0) GSU.armFlags |= ARM_ZERO;
    
    uint32 resultNew;
    asm (
        "msr cpsr_f, %0\n\t"
        "asrs %1, %2, #1\n\t" // Shift (sets NZC)
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (SEX16(v1))
        : "cc"
    );
    
    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 104bf93 (full NZCV)
FX_Result fxtest_div2(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    int32 tmp = SEX16(v1);
    GSU.vCarry = tmp & 1;
    uint32 resultOld = (tmp == -1) ? 0 : (tmp >> 1);
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO | ARM_CARRY);
    // GSU.armFlags |= (v1 & 1) << ARM_C_SHIFT;
    // uint32 resultNew = (SEX16(v1) == -1) ? 0 : (SEX16(v1) >> 1);
    // if (resultNew & 0x8000) GSU.armFlags |= ARM_NEGATIVE;
    // if (USEX16(resultNew) == 0) GSU.armFlags |= ARM_ZERO;

    uint32 resultNew;
    asm (
        "msr cpsr_f, %0\n\t"
        "asrs %1, %2, #1\n\t" // Shift (sets NZC)
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1 == UINT16_MAX ? 1 : SEX16(v1))
        : "cc"
    );
    
    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 104bf93 (full NZCV)
FX_Result fxtest_ror(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = (USEX16(v1)>>1) | (GSU.vCarry<<15);
    GSU.vCarry = v1 & 1;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;
    
    // uint32 resultNew = (USEX16(v1) >> 1) | (((GSU.armFlags & ARM_CARRY) >> ARM_C_SHIFT) << 15);
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO | ARM_CARRY);
    // GSU.armFlags |=
    //    ((v1 & 1) << ARM_C_SHIFT)
    //  | ((resultNew & 0x8000) << (ARM_N_SHIFT - 15))
    //  | (resultNew == 0 ? ARM_ZERO : 0);

    // 4 instructions
    // I have accidentally written some of the
    // most clever code I have ever seen
    uint32 resultNew = v1;
    asm (
        "msr cpsr_f, %0\n\t"
        "orrcs %1, %1, %2\n\t"
        "rrxs %1, %1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags),
          "+r" (resultNew)
        : "i" (BIT(16))
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 59a8454
FX_Result fxtest_lob(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = USEX8(v1);
    GSU.vSign = resultOld << 8;
    GSU.vZero = resultOld << 8;

    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // uint32 resultNew = USEX8(v1);
    // GSU.armFlags |= (resultNew >> 7) << ARM_N_SHIFT;
    // if (resultNew == 0) GSU.armFlags |= ARM_ZERO;

    GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    uint32 resultNew;
    asm (
        "lsls %1, %2, #24\n\t"
        "orrmi %0, %0, %3\n\t"
        "orreq %0, %0, %4\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1),
          "i" (ARM_NEGATIVE),
          "i" (ARM_ZERO)
        : "cc"
    );
    resultNew >>= 24;

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit e8e55f8
FX_Result fxtest_fmult(const FX_Gsu* GSUi, const uint16 v1, const uint16 R6)
{
    FX_Gsu GSU = *GSUi;

    uint32 full = SEX16(v1) * SEX16(R6);
    uint32 resultOld = full >> 16;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;
    GSU.vCarry = (full >> 15) & 1; // High bit of the low word

    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO | ARM_CARRY);
    // uint32 full2 = SEX16(v1) * SEX16(R6);
    // uint32 resultNew = full2 >> 16;
    // GSU.armFlags |= full2 & BIT(31); // These line up
    // GSU.armFlags |= (full2 & BIT(15)) << ((ARM_C_SHIFT - 15)); // High bit of the low word
    // if (USEX16(resultNew) == 0) GSU.armFlags |= ARM_ZERO;

    // Doing the mult with SMULBB is faster than MULS, and that's also
    // what the compiler gives us here.
    GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO | ARM_CARRY);
    uint32 resultNew = SEX16(v1) * SEX16(R6);
    asm (
        "movs %1, %1\n\t"
        "orrmi %0, %0, %2\n\t"
        "lsrs %1, %1, #16\n\t"
        "orreq %0, %0, %3\n\t"
        "orrcs %0, %0, %4\n\t"
        : "+r" (GSU.armFlags),
          "+r" (resultNew)
        : "i" (ARM_NEGATIVE),
          "i" (ARM_ZERO),
          "i" (ARM_CARRY)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit bdf8f52
FX_Result32 fxtest_lmult(const FX_Gsu* GSUi, const uint16 SREG, const uint16 R6)
{
    FX_Gsu GSU = *GSUi;

    uint32 full = SEX16(SREG) * SEX16(R6);
    uint16 resultOldLow = full;
    uint16 resultOldHigh = full >> 16;
    GSU.vSign = resultOldHigh;
    GSU.vZero = resultOldHigh;
    GSU.vCarry = (resultOldLow >> 15) & 1;

    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO | ARM_CARRY);
    // uint32 full2 = SEX16(SREG) * SEX16(R6);
    // uint16 resultNewLow = full;
    // uint16 resultNewHigh = full >> 16;
    // GSU.armFlags |= full2 & BIT(31); // These line up
    // GSU.armFlags |= (full2 & BIT(15)) << ((ARM_C_SHIFT - 15)); // High bit of the low word
    // if (resultNewHigh == 0) GSU.armFlags |= ARM_ZERO;

    // Doing the mult with SMULBB is faster than MULS, and that's also
    // what the compiler gives us here.
    GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO | ARM_CARRY);
    uint32 fullNew = SEX16(SREG) * SEX16(R6);
    uint16 resultNewHigh, resultNewLow = fullNew;
    asm (
        "movs %1, %2\n\t"
        "orrmi %0, %0, %3\n\t"
        "lsrs %1, %1, #16\n\t"
        "orreq %0, %0, %4\n\t"
        "orrcs %0, %0, %5\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNewHigh)
        : "r" (fullNew),
          "i" (ARM_NEGATIVE),
          "i" (ARM_ZERO),
          "i" (ARM_CARRY)
        : "cc"
    );

    return packResultDual16(GSU, resultNewHigh, resultNewLow, resultOldHigh, resultOldLow);
}

// Passed in commit 3899481
FX_Result fxtest_from_r(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = v1;
    GSU.vOverflow = (resultOld & 0x80) << 16;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // uint32 resultNew = v1;
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO | ARM_OVERFLOW);
    // GSU.armFlags |= (resultNew & 0x80) << (ARM_V_SHIFT - 7);
    // GSU.armFlags |= (resultNew & 0x8000) << (ARM_N_SHIFT - 15);
    // if (USEX16(resultNew) == 0) GSU.armFlags |= ARM_ZERO;

    uint32 tmp, resultNew = v1;
    GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO | ARM_OVERFLOW);
    asm (
        "lsls %1, %2, #24\n\t"
        "orrmi %0, %0, %5\n\t"
        "lsls %1, %2, #16\n\t"
        "orrmi %0, %0, %3\n\t"
        "orreq %0, %0, %4\n\t"
        : "+r" (GSU.armFlags),
          "=r" (tmp)
        : "r" (resultNew),
          "i" (ARM_NEGATIVE),
          "i" (ARM_ZERO),
          "i" (ARM_OVERFLOW)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 104bf93 (full NZCV)
FX_Result fxtest_hib(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = USEX8(v1 >> 8);
    GSU.vSign = resultOld << 8;
    GSU.vZero = resultOld << 8;

    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // uint32 resultNew = USEX8(v1 >> 8);
    // GSU.armFlags |= (resultNew >> 7) << ARM_N_SHIFT;
    // if (USEX16(resultNew) == 0) GSU.armFlags |= ARM_ZERO;

    uint32 resultNew = v1 >> 8;
    asm (
        "msr cpsr_f, %0\n\t"
        "movs %0, %1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags)
        : "r" (SEX8(resultNew))
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 3899481
FX_Result fxtest_or_r(const FX_Gsu* GSUi, const uint16 v1, const uint16 v2)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = v1 | v2;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // Software implementation (not yet fully tested but c'mon)
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // uint32 resultNew = v1 | v2;
    // GSU.armFlags |= (resultNew & 0x8000) << (ARM_N_SHIFT - 15);
    // if (resultNew == 0) GSU.armFlags |= ARM_ZERO;

    uint32 resultNew;
    GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    asm (
        "orrs %1, %2, %3\n\t"
        "orreq %0, %0, %4\n\t"
        "orrmi %0, %0, %5\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1 | (v1 << 16)),
          "r" (v2 | (v2 << 16)),
          "i" (ARM_ZERO),
          "i" (ARM_NEGATIVE)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 3899481
FX_Result fxtest_xor_r(const FX_Gsu* GSUi, const uint16 v1, const uint16 v2)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = v1 ^ v2;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // Software implementation (not yet fully tested but c'mon)
    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // uint32 resultNew = v1 ^ v2;
    // GSU.armFlags |= (resultNew & 0x8000) << (ARM_N_SHIFT - 15);
    // if (resultNew == 0) GSU.armFlags |= ARM_ZERO;

    uint32 resultNew;
    GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    asm (
        "eors %1, %2, %3\n\t"
        "orreq %0, %0, %4\n\t"
        "orrmi %0, %0, %5\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1 | (v1 << 16)),
          "r" (v2 | (v2 << 16)),
          "i" (ARM_ZERO),
          "i" (ARM_NEGATIVE)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 89e3b57 (full NZCV)
FX_Result fxtest_or_i(const FX_Gsu* GSUi, const uint16 v1, const uint8 imm)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = v1 | imm;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // uint32 resultNew = v1 | imm;
    // GSU.armFlags |= (resultNew & 0x8000) << (ARM_N_SHIFT - 15);
    // if (resultNew == 0) GSU.armFlags |= ARM_ZERO;

    uint32 resultNew;
    asm (
        "msr cpsr_f, %0\n\t"
        "orrs %1, %2, %3\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1 | (v1 << 16)),
          "r" (imm) // Doesn't need shift because this can't make nonzero into zero.
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 89e3b57 (full NZCV)
FX_Result fxtest_xor_i(const FX_Gsu* GSUi, const uint16 v1, const uint8 imm)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = v1 ^ imm;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // uint32 resultNew = v1 ^ imm;
    // GSU.armFlags |= (resultNew & 0x8000) << (ARM_N_SHIFT - 15);
    // if (resultNew == 0) GSU.armFlags |= ARM_ZERO;

    uint32 resultNew;
    asm (
        "msr cpsr_f, %0\n\t"
        "eors %1, %2, %3\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags),
          "=r" (resultNew)
        : "r" (v1 | (v1 << 16)),
          "r" (imm | (imm << 16))
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 104bf93 (full NZCV)
FX_Result fxtest_inc_r(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;

    uint32 resultOld = v1 + 1;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // uint16 resultNew = v1 + 1;
    // GSU.armFlags |= (resultNew & 0x8000) << (ARM_N_SHIFT - 15);
    // if (resultNew == 0) GSU.armFlags |= ARM_ZERO;

    uint32 resultNew = v1 + 1;
    asm (
        "msr cpsr_f, %0\n\t"
        "lsl %0, %1, #16\n\t"
        "movs %0, %0\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags)
        : "r" (resultNew)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}

// Passed in commit 104bf93 (full NZCV)
FX_Result fxtest_dec_r(const FX_Gsu* GSUi, const uint16 v1)
{
    FX_Gsu GSU = *GSUi;
    
    uint32 resultOld = v1 - 1;
    GSU.vSign = resultOld;
    GSU.vZero = resultOld;

    // GSU.armFlags &= ~(ARM_NEGATIVE | ARM_ZERO);
    // uint16 resultNew = v1 - 1;
    // GSU.armFlags |= (resultNew & 0x8000) << (ARM_N_SHIFT - 15);
    // if (resultNew == 0) GSU.armFlags |= ARM_ZERO;

    uint32 resultNew = v1 - 1;
    asm (
        "msr cpsr_f, %0\n\t"
        "lsl %0, %1, #16\n\t"
        "movs %0, %0\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (GSU.armFlags)
        : "r" (resultNew)
        : "cc"
    );

    return packResult(GSU, resultNew, resultOld);
}
