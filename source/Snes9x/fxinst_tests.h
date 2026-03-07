#ifndef FXINST_TESTS_H
#define FXINST_TESTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned char bool8;
typedef signed char int8;
typedef short int16;
typedef int int32;

// Misc macros
#define BIT(n_) (1 << n_)

// Sign extensions
#define SEX16(a) ((int32)((int16)(a)))
#define SEX8(a) ((int32)((int8)(a)))
#define USEX8(a) ((uint32)((uint8)(a)))
#define USEX16(a) ((uint32)((uint16)(a)))
#define SUSEX16(a) ((int32)((uint16)(a)))

// GSU flag tests
#define TEST_S (GSU.vSign & 0x8000) /* True if 16-bit sign bit is 1 */
#define TEST_Z (USEX16(GSU.vZero) == 0) /* True if 16-bit result is 0 */
#define TEST_OV (GSU.vOverflow >= 0x8000 || GSU.vOverflow < -0x8000) /* Bit 16 is usually the only one set */
#define TEST_CY (GSU.vCarry) /* Essentially just a bool */

// Flag bit locations packed to the bottom of an int
#define PACKED_N_SHIFT 3 // Negative/Sign flag
#define PACKED_Z_SHIFT 2 // Zero flag
#define PACKED_C_SHIFT 1 // Carry flag
#define PACKED_V_SHIFT 0 // Overflow flag

// Flag bits packed to the bottom of an int
#define PACKED_N BIT(PACKED_N_SHIFT) // Negative/Sign flag
#define PACKED_Z BIT(PACKED_Z_SHIFT) // Zero flag
#define PACKED_C BIT(PACKED_C_SHIFT) // Carry flag
#define PACKED_V BIT(PACKED_V_SHIFT) // Overflow flag

// Flag bits in their original locations
#define ARM_N_SHIFT (28 + PACKED_N_SHIFT) // ARM Negative flag
#define ARM_Z_SHIFT (28 + PACKED_Z_SHIFT) // ARM Zero flag
#define ARM_C_SHIFT (28 + PACKED_C_SHIFT) // ARM Carry flag
#define ARM_V_SHIFT (28 + PACKED_V_SHIFT) // ARM Overflow flag

// ARM flag bits (alternate names)
#define ARM_NEGATIVE BIT(ARM_N_SHIFT)
#define ARM_ZERO     BIT(ARM_Z_SHIFT)
#define ARM_CARRY    BIT(ARM_C_SHIFT)
#define ARM_OVERFLOW BIT(ARM_V_SHIFT)

// Result of a singular FX test
typedef struct
{
    uint8 gsuFlags, armFlags;
    uint16 result, expected;
} FX_Result;

// GSU status structure. See gsuCreate
typedef struct
{
    uint8   vCarry;    // a value of 1 or 0
    uint16  vZero;     // 0 or nonzero
    uint16  vSign;     // true if bit 16 set
    int32   vOverflow; // (v >= 0x8000 || v < -0x8000)
    uint32  armFlags;
} FX_Gsu;

FX_Result fxtest_lsr(const FX_Gsu* GSU, uint16 v1);
FX_Result fxtest_add_r(const FX_Gsu* GSU, uint16 v1, uint16 v2);
FX_Result fxtest_adc_r(const FX_Gsu* GSU, uint16 v1, uint16 v2);

#ifdef __cplusplus
}
#endif // Extern C
#endif // FXINST_TESTS_H
