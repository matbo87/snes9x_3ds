#ifndef FXINST_ARM_H
#define FXINST_ARM_H

#undef BIT
#define BIT(n_) (1U << n_)

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

// Flag bits in their ARM locations
#define ARM_N_SHIFT (28 + PACKED_N_SHIFT) // ARM Negative flag
#define ARM_Z_SHIFT (28 + PACKED_Z_SHIFT) // ARM Zero flag
#define ARM_C_SHIFT (28 + PACKED_C_SHIFT) // ARM Carry flag
#define ARM_V_SHIFT (28 + PACKED_V_SHIFT) // ARM Overflow flag
#define ARM_SHIFT ARM_V_SHIFT // Overall shift of ARM flags

// ARM flag bits
#define ARM_NEGATIVE BIT(ARM_N_SHIFT)
#define ARM_ZERO     BIT(ARM_Z_SHIFT)
#define ARM_CARRY    BIT(ARM_C_SHIFT)
#define ARM_OVERFLOW BIT(ARM_V_SHIFT)
#define ARM_FLAGS (ARM_NEGATIVE | ARM_ZERO | ARM_CARRY | ARM_OVERFLOW)

#endif // FXINST_ARM_H
