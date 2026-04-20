#include "copyright.h"


#define FX_DO_ROMBUFFER

#include "fxemu.h"
#include "fxinst.h"
#include "3dssnes9x.h"
#include "3dsopt.h"
#include "fxinst_arm.h"
#include <string.h>
#include <stdio.h>

#define LIKELY(cond_) __builtin_expect(!!(cond_), 1)
#define UNLIKELY(cond_) __builtin_expect(!!(cond_), 0)
#define ASSUME(cond_) if (!(cond_)) __builtin_unreachable()
#define COLD __attribute__ ((cold))
#define FETCHPIPE2(r15_) { PIPE = PRGBANK(r15_); } // For optimization
#define REV16(v_) asm ("rev16 %0, %1":"=r"(v_):"r"(v_));
#define ALIGNED16 __attribute__((aligned(16)))

// Our ASSUME_ macros generate these with u8 vLow
#define ENW_ _Pragma("GCC diagnostic push"); _Pragma("GCC diagnostic ignored \"-Wtype-limits\"")
#define DIW_ _Pragma("GCC diagnostic pop")
#define ASSUME_REG(min_, max_) do {ENW_; ASSUME(reg >= min_ && reg <= max_); DIW_; } while(0)
#define ASSUME_IMM(min_, max_) do {ENW_; ASSUME(imm >= min_ && imm <= max_); DIW_; } while(0)
#define ASSUME_LKN(min_, max_) do {ENW_; ASSUME(lkn >= min_ && lkn <= max_); DIW_; } while(0)

extern struct FxRegs_s GSU;

// If 1, this file will reserve registers throughout this
// file. This improves performance significantly. If you
// wish to modify reservations, be sure that the registers
// that you choose are free as per the ARM AAPCS!
// The gist:
// 0-3 are caller-saved and must be manually saved if you
//    call into code in another file.
// 4-10 are callee-saved and are fair game for reservation
// 11-15 have special purposes depending on compiler flags.
//    Best to just let the compiler use them.
#define REGISTER_RESERVATIONS 1

#if REGISTER_RESERVATIONS == 1

// Reserve status register
#undef SFR
register uint32 statusRegLocal asm("r6");
#define SFR statusRegLocal

// Reserve ARM flags
register uint32 armFlagsLocal asm("r7");
#define ARMFLAGS armFlagsLocal

// Reserve PIPE
#undef PIPE
register uint8 pipeLocal asm("r8");
#define PIPE pipeLocal

// Reserve SREG
#undef SREG
#undef SREG_PTR
register uint16* pvSregLocal asm("r9");
#define SREG_PTR pvSregLocal
#define SREG *SREG_PTR

// Reserve DREG
#undef DREG
#undef DREG_PTR
register uint16* pvDregLocal asm("r10");
#define DREG_PTR pvDregLocal
#define DREG *DREG_PTR

// If any of these registers are used by your function or its
// statically-linked subroutines, these must be placed at the
// start and end of said function if it is externally linked.
#define PUSH_RESERVED asm volatile ("push {r6, r7, r8, r9, r10}")
#define POP_RESERVED  asm volatile ("pop  {r6, r7, r8, r9, r10}")

// Necessary redefs if DREG and SREG are pointers
#undef TESTR14
#undef CLRFLAGS
#define CLRFLAGS SFR &= ~(FLG_ALT1|FLG_ALT2|FLG_B); DREG_PTR = SREG_PTR = GETR(0);
#define TESTR14 if((pvDregLocal) == GETR(14)) READR14

// The compiler doesn't realize it can do this, so it loads from memory
//!!! This relies on the fact that GSU.avReg is at the start of GSU!
static inline uint16* GETR(size_t reg)
{
    uint16* ptr;
    asm ("add %0, %1, %2" : "=r" (ptr) : "r" (&GSU), "i" (reg * sizeof(uint16)));
    return ptr;
}

// Saves the reserved registers back to GSU
static inline void fx_save_reserved(void)
{
    GSU.vStatusReg = SFR;
    GSU.armFlags = ARMFLAGS;
    GSU.vPipe = PIPE;
    GSU.pvSreg = SREG_PTR - GSU.avReg;
    GSU.pvDreg = DREG_PTR - GSU.avReg;
}

// Loads the reserved registers from GSU
static inline void fx_load_reserved(void)
{
    SFR = GSU.vStatusReg;
    ARMFLAGS = GSU.armFlags;
    PIPE = GSU.vPipe;
    pvSregLocal = &GSU.avReg[GSU.pvSreg];
    pvDregLocal = &GSU.avReg[GSU.pvDreg];
}

// register reservations are disabled
#else
#define ARMFLAGS (GSU.armFlags)
#define PUSH_RESERVED do {} while(0)
#define POP_RESERVED do {} while(0)
static inline void fx_save_reserved(void) {} // Stub
static inline void fx_load_reserved(void) {} // Stub
#endif

/* Set this define if you wish the plot instruction to check for y-pos limits */
/* (I don't think it's nessecary) */
#define CHECK_LIMITS

/* Codes used:
 *
 * rn   = a GSU register (r0-r15)
 * #n   = 4 bit immediate value
 * #pp  = 8 bit immediate value
 * (yy) = 8 bit word address (0x0000 - 0x01fe)
 * #xx  = 16 bit immediate value
 * (xx) = 16 bit address (0x0000 - 0xffff)
 *
 */

/* 00 - stop - stop GSU execution (and maybe generate an IRQ) */
static inline void fx_stop()
{
    CF(G);

    /* Check if we need to generate an IRQ */
    if(!(GSU.pvRegisters[GSU_CFGR] & 0x80))
	    SF(IRQ);

    GSU.vPlotOptionReg = 0;
    PIPE = 1;
    CLRFLAGS;
    R15++;
}

/* 01 - nop - no operation */
static inline void fx_nop() {
    CLRFLAGS;
    R15++;
}

/* 02 - cache - reintialize GSU cache */
static inline void fx_cache()
{
    uint32 c = R15 & 0xfff0;
    if(GSU.vCacheBaseReg != c || !GSU.bCacheActive)
    {
        GSU.vCacheFlags = 0;
        GSU.vCacheBaseReg = c;
        GSU.bCacheActive = TRUE;
#if 0
        if(c < (0x10000-512))
        {
            uint8 const* t = &ROM(c);
            memcpy(GSU.pvCache,t,512);
        }
        else
        {
            uint8 const* t1;
            uint8 const* t2;
            uint32 i = 0x10000 - c;
            t1 = &ROM(c);
            t2 = &ROM(0);
            memcpy(GSU.pvCache,t1,i);
            memcpy(&GSU.pvCache[i],t2,512-i);
        }
#endif	
    }
    R15++;
    CLRFLAGS;
}

/* 03 - lsr - logic shift right */
static inline void fx_lsr()
{
    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "lsrs %1, %2, #1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" (SREG)
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 04 - rol - rotate left */
static inline void fx_rol()
{
    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "lsl %1, %2, #16\n\t"
        "orrcs %1, %1, %3\n\t"
        "lsls %1, %1, #1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" (SREG),
          "i" (BIT(15))
        : "cc"
    );
    v >>= 16;

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* Branch on condition */
#define BRA_COND(condT_, condF_) {      \
    uint8 v = PIPE;                     \
    uint32 r15 = R15 + 1;               \
    FETCHPIPE2(r15);                    \
    asm (                               \
        "msr cpsr_f, %0\n\t"            \
        "add" condT_ " %1, %1, %2\n\t"  \
        "add" condF_ " %1, %1, #1\n\t"  \
        : "+r" (ARMFLAGS),              \
          "+r" (r15)                    \
        : "r" (SEX8(v))                 \
        : "cc"                          \
    );                                  \
    R15 = r15;                          \
}

/* 05 - bra - branch always */
static inline void fx_bra() {
    uint8 v = PIPE;
    uint32 r15 = R15 + 1;
    FETCHPIPE2(r15);
    R15 = r15 + SEX8(v);
}

/* 06 - blt - branch on less than */
static inline void fx_blt() { BRA_COND( "lt", "ge" ); }

/* 07 - bge - branch on greater or equals */
static inline void fx_bge() { BRA_COND( "ge", "lt" ); }

/* 08 - bne - branch on not equal */
static inline void fx_bne() { BRA_COND( "ne", "eq" ); }

/* 09 - beq - branch on equal */
static inline void fx_beq() { BRA_COND( "eq", "ne" ); }

/* 0a - bpl - branch on plus */
static inline void fx_bpl() { BRA_COND( "pl", "mi" ); }

/* 0b - bmi - branch on minus */
static inline void fx_bmi() { BRA_COND( "mi", "pl" ); }

/* 0c - bcc - branch on carry clear */
static inline void fx_bcc() { BRA_COND( "cc", "cs" ); }

/* 0d - bcs - branch on carry set */
static inline void fx_bcs() { BRA_COND( "cs", "cc" ); }

/* 0e - bvc - branch on overflow clear */
static inline void fx_bvc() { BRA_COND( "vc", "vs" ); }

/* 0f - bvs - branch on overflow set */
static inline void fx_bvs() { BRA_COND( "vs", "vc" ); }

/* 10-1f - to rn - set register n as destination register */
/* 10-1f(B) - move rn - move one register to another (if B flag is set) */

static inline void fx_to_r(uint8 reg) {
    ASSUME_REG(0, 13);
    if(TF(B))
    {
        GSU.avReg[reg] = SREG;
        CLRFLAGS;
    }
    else
        DREG_PTR = &GSU.avReg[reg];

    R15++;
}

static inline void fx_to_r14() {
    if(TF(B)) {
        R14 = SREG;
        CLRFLAGS;
        READR14;
    }
    else
        DREG_PTR = GETR(14);
    R15++;
}

static inline void fx_to_r15() {
    if(TF(B)) {
        R15 = SREG;
        CLRFLAGS;
    }
    else {
        DREG_PTR = GETR(15);
        R15++;
    }
}

/* 20-2f - to rn - set register n as source and destination register */
static inline void fx_with(uint8 reg) {
    ASSUME_REG(0, 15);
    SF(B);
    SREG_PTR = DREG_PTR = &GSU.avReg[reg];
    R15++;
}

/* 30-3b - stw (rn) - store word */
static inline void fx_stw_r(uint8 reg) {
    ASSUME_REG(0, 11);
    uint16 r = GSU.vLastRamAdr = GSU.avReg[reg];
    RAM(r) = (uint8)SREG;
    RAM(r^1) = (uint8)(SREG>>8);
    CLRFLAGS;
    R15++;
}

/* 30-3b(ALT1) - stb (rn) - store byte */
static inline void fx_stb_r(uint8 reg) {
    ASSUME_REG(0, 11);
    GSU.vLastRamAdr = GSU.avReg[reg];
    RAM(GSU.avReg[reg]) = (uint8)SREG;
    CLRFLAGS;
    R15++;
}

/* 3c - loop - decrement loop counter, and branch on not zero */
static inline void fx_loop()
{
    uint32 r12 = R12 - 1; // Gotta do math with a u32 to avoid a UXTH instruction
    asm (
        "msr cpsr_f, %0\n\t"
        "lsl %0, %1, #16\n\t"
        "movs %0, %0\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS)
        : "r" (r12)
        : "cc"
    );

    R12 = r12;
    if(LIKELY( r12 != 0 ))
	    R15 = R13;
    else
	    R15++;

    CLRFLAGS;
}

/* 3d - alt1 - set alt1 mode */
// WYATT_TODO see if this can be collapsed
static inline void fx_alt1() {
    SF(ALT1);
    CF(B);
    R15++;
}

/* 3e - alt2 - set alt2 mode */
static inline void fx_alt2() {
    SF(ALT2);
    CF(B);
    R15++;
}

/* 3f - alt3 - set alt3 mode */
static inline void fx_alt3() {
    SF(ALT1);
    SF(ALT2);
    CF(B);
    R15++;
}
    
/* 40-4b - ldw (rn) - load word from RAM */
static inline void fx_ldw_r(uint8 reg)  { 
    ASSUME_REG(0, 11);
    uint32 v;
    GSU.vLastRamAdr = GSU.avReg[reg];
    v =   (uint32)RAM(GSU.avReg[reg]);
    v |= ((uint32)RAM(GSU.avReg[reg]^1))<<8;
    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 40-4b(ALT1) - ldb (rn) - load byte */
static inline void fx_ldb_r(uint8 reg) {
    ASSUME_REG(0, 11);
    uint32 v;
    GSU.vLastRamAdr = GSU.avReg[reg];
    v = (uint32)RAM(GSU.avReg[reg]);
    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 4c - plot - plot pixel with R1,R2 as x,y and the color register as the color */
static inline void fx_plot_2bit(void)
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 c;

    R15++;
    CLRFLAGS;
    R1++;

#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return;
#endif

    if(GSU.vPlotOptionReg & 0x02)
	    c = (x ^ y) & 1 ? (GSU.vColorReg >> 4) : GSU.vColorReg; // WYATT_TODO check this ASM
    else
	    c = GSU.vColorReg;
    
    if( !(GSU.vPlotOptionReg & 0x01) && !(c & 0xf)) 
        return;

    a = GSU.apvScreen[y >> 3] + GSU.x[x >> 3] + ((y & 7) << 1);
    uint32 v = 128 >> (x&7);

    if(c & 0x01) a[0] |= v;
    else         a[0] &= ~v;
    if(c & 0x02) a[1] |= v;
    else         a[1] &= ~v;
}

#define TESTBIT(offset_, shift_)   \
asm (                              \
    "tst %1, %2\n\t"               \
    "orrne %0, %0, %4, lsl %3\n\t" \
    : "+r" (dReg)                  \
    : "r" (v),                     \
      "r" (a[offset_]),            \
      "i" (shift_),                \
      "r" (1)                      \
    : "cc"                         \
)

/* 2c(ALT1) - rpix - read color of the pixel with R1,R2 as x,y */
static inline void fx_rpix_2bit(void)
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 v;

    R15++;
    CLRFLAGS;

#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return;
#endif

    a = GSU.apvScreen[y >> 3] + GSU.x[x >> 3] + ((y & 7) << 1);
    v = 128 >> (x&7);

    uint32 dReg = 0;
    TESTBIT(0, 0);
    TESTBIT(1, 1);
    DREG = dReg;
    TESTR14;
}

/* 4c - plot - plot pixel with R1,R2 as x,y and the color register as the color */
static inline void fx_plot_4bit(void)
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 c;

    R15++;
    CLRFLAGS;
    R1++;
    
#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return;
#endif

    if(GSU.vPlotOptionReg & 0x02)
	    c = (x ^ y) & 1 ? (GSU.vColorReg >> 4) : GSU.vColorReg;
    else
	    c = GSU.vColorReg;

    if( !(GSU.vPlotOptionReg & 0x01) && !(c & 0xf))
        return;

    a = GSU.apvScreen[y >> 3] + GSU.x[x >> 3] + ((y & 7) << 1);
    uint32 v = 128 >> (x&7);

    if(c & 0x01) a[0x00] |= v;
    else         a[0x00] &= ~v;
    if(c & 0x02) a[0x01] |= v;
    else         a[0x01] &= ~v;
    if(c & 0x04) a[0x10] |= v;
    else         a[0x10] &= ~v;
    if(c & 0x08) a[0x11] |= v;
    else         a[0x11] &= ~v;
}

/* 4c(ALT1) - rpix - read color of the pixel with R1,R2 as x,y */
static inline void fx_rpix_4bit(void)
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 v;

    R15++;
    CLRFLAGS;

#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return;
#endif

    a = GSU.apvScreen[y >> 3] + GSU.x[x >> 3] + ((y & 7) << 1);
    v = 128 >> (x&7);

    uint32 dReg = 0;
    TESTBIT(0x00, 0);
    TESTBIT(0x01, 1);
    TESTBIT(0x10, 2);
    TESTBIT(0x11, 3);
    DREG = dReg;
    TESTR14;
}

/* 8c - plot - plot pixel with R1,R2 as x,y and the color register as the color */
static inline void fx_plot_8bit(void)
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 c;

    R15++;
    CLRFLAGS;
    R1++;
    
#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return;
#endif

    c = GSU.vColorReg;
    
    if( !(GSU.vPlotOptionReg & 0x10) ) {
	    if( !(GSU.vPlotOptionReg & 0x01) && !(c & 0xf))
            return;
    }
    else
	    if( !(GSU.vPlotOptionReg & 0x01) && !c)
            return;

    a = GSU.apvScreen[y >> 3] + GSU.x[x >> 3] + ((y & 7) << 1);
    uint32 v = 128 >> (x&7);

    if(c & 0x01) a[0x00] |= v;
    else         a[0x00] &= ~v;
    if(c & 0x02) a[0x01] |= v;
    else         a[0x01] &= ~v;
    if(c & 0x04) a[0x10] |= v;
    else         a[0x10] &= ~v;
    if(c & 0x08) a[0x11] |= v;
    else         a[0x11] &= ~v;
    if(c & 0x10) a[0x20] |= v;
    else         a[0x20] &= ~v;
    if(c & 0x20) a[0x21] |= v;
    else         a[0x21] &= ~v;
    if(c & 0x40) a[0x30] |= v;
    else         a[0x30] &= ~v;
    if(c & 0x80) a[0x31] |= v;
    else         a[0x31] &= ~v;
}

/* 4c(ALT1) - rpix - read color of the pixel with R1,R2 as x,y */
static inline void fx_rpix_8bit(void)
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 v;

    R15++;
    CLRFLAGS;

#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return;
#endif
    a = GSU.apvScreen[y >> 3] + GSU.x[x >> 3] + ((y & 7) << 1);
    v = 128 >> (x&7);

    uint32 dReg = 0;
    TESTBIT(0x00, 0);
    TESTBIT(0x01, 1);
    TESTBIT(0x10, 2);
    TESTBIT(0x11, 3);
    TESTBIT(0x20, 4);
    TESTBIT(0x21, 5);
    TESTBIT(0x30, 6);
    TESTBIT(0x31, 7);
    DREG = dReg;

    ARMFLAGS &= ~ARM_ZERO;
    if (USEX16(DREG) == 0) ARMFLAGS |= ARM_ZERO;
    TESTR14;
}

/* 4o - plot - plot pixel with R1,R2 as x,y and the color register as the color */
COLD static inline void fx_plot_obj(void)
{
    printf ("ERROR fx_plot_obj called\n");
}

/* 4c(ALT1) - rpix - read color of the pixel with R1,R2 as x,y */
COLD static inline void fx_rpix_obj(void)
{
    printf ("ERROR fx_rpix_obj called\n");
}

/* 4d - swap - swap upper and lower byte of a register */
static inline void fx_swap()
{
    uint32 v;
    asm ("rev16 %0, %1":"=r"(v):"r"(SREG));
    asm (
        "msr cpsr_f, %0\n\t"
        "movs %0, %1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS)
        : "r" (v | (v << 16))
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 4e - color - copy source register to color register */
static inline void fx_color()
{
    uint8 c = (uint8) SREG;
    if(GSU.vPlotOptionReg & 0x04)
	    c = (c & 0xf0) | (c >> 4);

    if(GSU.vPlotOptionReg & 0x08)
    {
        GSU.vColorReg &= 0xf0;
        GSU.vColorReg |= c & 0x0f;
    }
    else
	    GSU.vColorReg = USEX8(c);

    CLRFLAGS;
    R15++;
}

/* 4e(ALT1) - cmode - set plot option register */
static inline void fx_cmode()
{
    GSU.vPlotOptionReg = SREG;

    if(GSU.vPlotOptionReg & 0x10)
        GSU.vScreenHeight = 256; /* OBJ Mode (for drawing into sprites) */
    else
	    GSU.vScreenHeight = GSU.vScreenRealHeight;

    fx_computeScreenPointers(); // Moving this here increases register pressure too much. Leave it in the other file.
    CLRFLAGS;
    R15++;
}

/* 4f - not - perform exclusive exor with 1 on all bits */
static inline void fx_not()
{
    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "mvns %1, %2\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" ((SREG << 16) | SREG)
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 50-5f - add rn - add, register + register */
static inline void fx_add_r(uint8 reg) {
    ASSUME_REG(0, 15);
    
    uint32 s;
    asm (
        "adds %1, %2, %3, lsl #16\n\t"
        "mrs %0, cpsr"
        : "=r" (ARMFLAGS), "=r" (s)
        : "r" (SREG << 16), "r" (GSU.avReg[reg])
        : "cc"
    );
    s >>= 16;

    R15++;
    DREG = s;
    TESTR14;
    CLRFLAGS;
}

/* 50-5f(ALT1) - adc rn - add with carry, register + register */
static inline void fx_adc_r(uint8 reg) {
    ASSUME_REG(0, 15);

    uint32 s = GSU.avReg[reg];
    asm (
        "msr cpsr_f, %0\n\t"
        "lsl %0, %2, #16\n\t"
        "orrcs %0, %0, %3\n\t"
        "orrcs %1, %1, %4\n\t"
        "adds %1, %0, %1, ror #16\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "+r" (s)
        : "r" (SREG),
          "i" (BIT(15)),
          "i" (BIT(31))
        : "cc"
    );
    s >>= 16;

    R15++;
    DREG = s;
    TESTR14;
    CLRFLAGS;
}

/* 50-5f(ALT2) - add #n - add, register + immediate */
static inline void fx_add_i(uint8 imm) {
    ASSUME_IMM(0, 15);

    uint32 s;
    asm (
        "adds %1, %2, %3, lsl #16\n\t"
        "mrs %0, cpsr"
        : "=r" (ARMFLAGS), "=r" (s)
        : "r" (SREG << 16), "r" (imm)
        : "cc"
    );
    s >>= 16;

    R15++;
    DREG = s;
    TESTR14;
    CLRFLAGS;
}

/* 50-5f(ALT3) - adc #n - add with carry, register + immediate */
static inline void fx_adc_i(uint8 imm) {
    ASSUME_IMM(0, 15);

    uint32 s = imm;
    asm (
        "msr cpsr_f, %0\n\t"
        "lsl %0, %2, #16\n\t"
        "orrcs %0, %0, %3\n\t"
        "orrcs %1, %1, %4\n\t"
        "adds %1, %0, %1, ror #16\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "+r" (s)
        : "r" (SREG),
          "i" (BIT(15)),
          "i" (BIT(31))
        : "cc"
    );
    s >>= 16;

    R15++;
    DREG = s;
    TESTR14;
    CLRFLAGS;
}

/* 60-6f - sub rn - subtract, register - register */
static inline void fx_sub_r(uint8 reg) {
    ASSUME_REG(0, 15);

    uint32 s;
    asm (
        "subs %1, %2, %3, lsl #16\n\t"
        "mrs %0, cpsr"
        : "=r" (ARMFLAGS), "=r" (s)
        : "r" ((SREG << 16)), "r" (GSU.avReg[reg])
        : "cc"
    );
    s >>= 16;

    R15++;
    DREG = s;
    TESTR14;
    CLRFLAGS;
}

/* 60-6f(ALT1) - sbc rn - subtract with carry, register - register */
static inline void fx_sbc_r(uint8 reg) {
    ASSUME_REG(0, 15);

    uint32 s;
    asm (
        "msr cpsr_f, %0\n\t" // Copy in the carry flag
        "sbcs %1, %2, %3, lsl #16\n\t" // Do the actual subtraction
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (s)
        : "r" (SREG << 16),
          "r" (GSU.avReg[reg])
        : "cc"
    );
    s >>= 16;
    if (s == 0) ARMFLAGS |= ARM_ZERO;

    R15++;
    DREG = s;
    TESTR14;
    CLRFLAGS;
}

/* 60-6f(ALT2) - sub #n - subtract, register - immediate */
static inline void fx_sub_i(uint8 imm) {
    ASSUME_IMM(0, 15);

    uint32 s;
    asm (
        "subs %1, %2, %3, lsl #16\n\t"
        "mrs %0, cpsr"
        : "=r" (ARMFLAGS), "=r" (s)
        : "r" ((SREG << 16)), "r" (imm)
        : "cc"
    );
    s >>= 16;

    R15++;
    DREG = s;
    TESTR14;
    CLRFLAGS;
}

/* 60-6f(ALT3) - cmp rn - compare, register, register */
static inline void fx_cmp_r(uint8 reg) {
    ASSUME_REG(0, 15);

    asm (
        "cmp %1, %2, lsl #16\n\t"
        "mrs %0, cpsr"
        : "=r" (ARMFLAGS)
        : "r" ((SREG << 16)), "r" (GSU.avReg[reg])
        : "cc"
    );

    R15++;
    CLRFLAGS;
}

/* 70 - merge - R7 as upper byte, R8 as lower byte (used for texture-mapping) */
static inline void fx_merge()
{
    uint32 v = (R7 & 0xff00) | ((R8 & 0xff00) >> 8);
    uint32 offset = ((v >> 12) | (v >> 4)) & 0b1111;
    ARMFLAGS = GSU.mergeFlagLut[offset] << ARM_SHIFT;

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 71-7f - and rn - reister & register */
static inline void fx_and_r(uint8 reg) {
    ASSUME_REG(1, 15);

    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "ands %1, %2, %3\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" (SREG | (SREG << 16)),
          "r" (GSU.avReg[reg] | (GSU.avReg[reg] << 16))
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 71-7f(ALT1) - bic rn - reister & ~register */
static inline void fx_bic_r(uint8 reg) {
    ASSUME_REG(1, 15);

    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "bics %1, %2, %3\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" (SREG | (SREG << 16)),
          "r" (GSU.avReg[reg] | (GSU.avReg[reg] << 16))
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 71-7f(ALT2) - and #n - reister & immediate */
static inline void fx_and_i(uint8 imm) {
    ASSUME_IMM(1, 15);

    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "ands %1, %2, %3\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" (SREG),
          "r" (imm)
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 71-7f(ALT3) - bic #n - reister & ~immediate */
static inline void fx_bic_i(uint8 imm) {
    ASSUME_IMM(1, 15);

    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "bics %1, %2, %3\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" (SREG | (SREG << 16)),
          "r" (imm | (imm << 16))
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 80-8f - mult rn - 8 bit to 16 bit signed multiply, register * register */
static inline void fx_mult_r(uint8 reg) {
    ASSUME_REG(0, 15);

    uint32 v = SEX8(SREG) * SEX8(GSU.avReg[reg]);
    asm (
        "msr cpsr_f, %0\n\t"
        "movs %0, %1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS)
        : "r" (v)
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 80-8f(ALT1) - umult rn - 8 bit to 16 bit unsigned multiply, register * register */
static inline void fx_umult_r(uint8 reg) {
    ASSUME_REG(0, 15);

    uint32 v = USEX8(SREG) * USEX8(GSU.avReg[reg]);
    asm (
        "msr cpsr_f, %0\n\t"
        "lsl %0, %1, #16\n\t"
        "movs %0, %0\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS)
        : "r" (v)
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}
  
/* 80-8f(ALT2) - mult #n - 8 bit to 16 bit signed multiply, register * immediate */
static inline void fx_mult_i(uint8 imm) {
    ASSUME_IMM(0, 15);

    uint32 v = SEX8(SREG) * imm; // WYATT_TODO check that this promotion is correct, and change imm to a u8 globally
    asm (
        "msr cpsr_f, %0\n\t"
        "movs %0, %1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS)
        : "r" (v)
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}
  
/* 80-8f(ALT3) - umult #n - 8 bit to 16 bit unsigned multiply, register * immediate */
static inline void fx_umult_i(uint8 imm) {
    ASSUME_IMM(0, 15);

    uint32 v = USEX8(SREG) * imm;
    asm (
        "msr cpsr_f, %0\n\t"
        "movs %0, %1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS)
        : "r" (v)
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}
  
/* 90 - sbk - store word to last accessed RAM address */
static inline void fx_sbk()
{
    uint16 sReg = SREG;
    RAM(GSU.vLastRamAdr) = (uint8)sReg;
    RAM(GSU.vLastRamAdr^1) = (uint8)(sReg>>8); // WYATT_TODO this RAM alignment can probably be optimized to a 16-bit store
    CLRFLAGS;
    R15++;
}

/* 91-94 - link #n - R11 = R15 + immediate */
static inline void fx_link_i(uint8 lkn) {
    ASSUME_LKN(1, 4);
    R11 = R15 + lkn;
    CLRFLAGS;
    R15++;
}

/* 95 - sex - sign extend 8 bit to 16 bit */
static inline void fx_sex()
{
    // WYATT_TODO It may be faster to set v to SEX(SREG) and use %0 as a scratch, due to memory reordering
    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "movs %1, %2\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" (SEX8(SREG))
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 96 - asr - aritmetric shift right by one */
static inline void fx_asr()
{
    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "asrs %1, %2, #1\n\t" // Shift (sets NZC)
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" (SEX16(SREG))
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 96(ALT1) - div2 - aritmetric shift right by one */
static inline void fx_div2()
{
    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "asrs %1, %2, #1\n\t" // Shift (sets NZC)
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" (SREG == UINT16_MAX ? 1 : SEX16(SREG))
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 97 - ror - rotate right by one */
static inline void fx_ror()
{
    uint32 v = SREG;
    asm (
        "msr cpsr_f, %0\n\t"
        "orrcs %1, %1, %2\n\t"
        "rrxs %1, %1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "+r" (v)
        : "i" (BIT(16))
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 98-9d - jmp rn - jump to address of register */
static inline void fx_jmp_r(uint8 reg) {
    ASSUME_REG(8, 13);
    R15 = GSU.avReg[reg];
    CLRFLAGS;
}

/* 98-9d(ALT1) - ljmp rn - set program bank to source register and jump to address of register */
static inline void fx_ljmp_r(uint8 reg) {
    ASSUME_REG(8, 13);
    GSU.vPrgBankReg = GSU.avReg[reg] & 0x7f;
    GSU.pvPrgBank = GSU.apvRomBank[GSU.vPrgBankReg];
    R15 = SREG;
    GSU.bCacheActive = FALSE;
    fx_cache();
    R15--;
}

/* 9e - lob - set upper byte to zero (keep low byte) */
static inline void fx_lob()
{
    uint32 v = USEX8(SREG);
    asm (
        "msr cpsr_f, %0\n\t"
        "lsl %0, %1, #24\n\t"
        "movs %0, %0\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS)
        : "r" (v)
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 9f - fmult - 16 bit to 32 bit signed multiplication, upper 16 bits only */
static inline void fx_fmult()
{
    uint32 v = SEX16(SREG) * SEX16(R6);
    asm (
        "msr cpsr_f, %0\n\t"
        "asrs %1, %1, #16\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "+r" (v)
        ::"cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* 9f(ALT1) - lmult - 16 bit to 32 bit signed multiplication */
static inline void fx_lmult()
{
    uint32 full = SEX16(SREG) * SEX16(R6);
    uint16 resultHigh, resultLow = full;
    asm (
        "msr cpsr_f, %0\n\t"
        "asrs %1, %2, #16\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (resultHigh)
        : "r" (full)
        : "cc"
    );
    R4 = resultLow;

    R15++;
    DREG = resultHigh;
    TESTR14;
    CLRFLAGS;
}

/* a0-af - ibt rn,#pp - immediate byte transfer */
static inline void fx_ibt_r(uint8 reg) {
    ASSUME_REG(0, 15);
    uint8 v = PIPE;
    R15++;
    FETCHPIPE;
    R15++;
    GSU.avReg[reg] = SEX8(v);
    CLRFLAGS;
}

static inline void fx_ibt_r14() {
    fx_ibt_r(14);
    READR14;
}

/* a0-af(ALT1) - lms rn,(yy) - load word from RAM (short address) */
static inline void fx_lms_r(uint8 reg) {
    ASSUME_REG(0, 15);
    GSU.vLastRamAdr = PIPE << 1;
    uint32 r15 = R15 + 1;
    FETCHPIPE2(r15);
    R15 = r15 + 1;
    GSU.avReg[reg] =   (uint16) RAM(GSU.vLastRamAdr)
                   | (((uint16) RAM(GSU.vLastRamAdr + 1)) << 8);
    CLRFLAGS;
}

static inline void fx_lms_r14() {
    fx_lms_r(14);
    READR14;
}

/* a0-af(ALT2) - sms (yy),rn - store word in RAM (short address) */
/* If rn == r15, is the value of r15 before or after the extra byte is read? */
static inline void fx_sms_r(uint8 reg) {
    ASSUME_REG(0, 15);
    uint16 v = GSU.avReg[reg];
    GSU.vLastRamAdr = PIPE << 1;
    R15++;
    FETCHPIPE;
    RAM(GSU.vLastRamAdr) = (uint8)v;
    RAM(GSU.vLastRamAdr+1) = (uint8)(v>>8);
    CLRFLAGS;
    R15++;
}

/* b0-bf - from rn - set source register */
/* b0-bf(B) - moves rn - move register to register, and set flags, (if B flag is set) */
static inline void fx_from_r(uint8 reg) {
    ASSUME_REG(0, 15);
    if(TF(B)) {
        uint32 tmp, v = GSU.avReg[reg];
        ARMFLAGS &= ~(ARM_NEGATIVE | ARM_ZERO | ARM_OVERFLOW);
        asm (
            "lsls %1, %2, #24\n\t"
            "orrmi %0, %0, %5\n\t"
            "lsls %1, %2, #16\n\t"
            "orrmi %0, %0, %3\n\t"
            "orreq %0, %0, %4\n\t"
            : "+r" (ARMFLAGS),
              "=r" (tmp)
            : "r" (v),
              "i" (ARM_NEGATIVE),
              "i" (ARM_ZERO),
              "i" (ARM_OVERFLOW)
            : "cc"
        );

        R15++;
        DREG = v;
        TESTR14;
        CLRFLAGS;
    }
    else {
        SREG_PTR = &GSU.avReg[reg];
        R15++;
    }
}

/* c0 - hib - move high-byte to low-byte */
static inline void fx_hib()
{
    uint32 v = SREG >> 8;
    asm (
        "msr cpsr_f, %0\n\t"
        "movs %0, %1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS)
        : "r" (SEX8(v))
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* c1-cf - or rn */
static inline void fx_or_r(uint8 reg) {
    ASSUME_REG(1, 15);

    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "orrs %1, %2, %3\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" (SREG | (SREG << 16)),
          "r" (GSU.avReg[reg] | (GSU.avReg[reg] << 16)) // WYATT_TODO check this ASM
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* c1-cf(ALT1) - xor rn */
static inline void fx_xor_r(uint8 reg) {
    ASSUME_REG(1, 15);

    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "eors %1, %2, %3\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" (SREG | (SREG << 16)),
          "r" (GSU.avReg[reg] | (GSU.avReg[reg] << 16))
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* c1-cf(ALT2) - or #n */
static inline void fx_or_i(uint8 imm) {
    ASSUME_IMM(1, 15);

    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "orrs %1, %2, %3\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" (SREG | (SREG << 16)),
          "r" (imm) // Doesn't need shift because this can't change the sign
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* c1-cf(ALT3) - xor #n */
static inline void fx_xor_i(uint8 imm) {
    ASSUME_IMM(1, 15);

    uint32 v;
    asm (
        "msr cpsr_f, %0\n\t"
        "eors %1, %2, %3\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS),
          "=r" (v)
        : "r" (SREG | (SREG << 16)),
          "r" (imm | (imm << 16))
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* d0-de - inc rn - increase by one */
static inline void fx_inc_r(uint8 reg) {
    ASSUME_REG(0, 14);

    uint32 v = GSU.avReg[reg] + 1;
    asm (
        "msr cpsr_f, %0\n\t"
        "lsl %0, %1, #16\n\t"
        "movs %0, %0\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS)
        : "r" (v)
        : "cc"
    );
    GSU.avReg[reg] = v;

    CLRFLAGS;
    R15++;
}

static inline void fx_inc_r14() {
    fx_inc_r(14);
    READR14;
}

/* df - getc - transfer ROM buffer to color register */
static inline void fx_getc()
{
#ifndef FX_DO_ROMBUFFER
    uint8 c = ROM(R14);
#else
    uint8 c = GSU.vRomBuffer;
#endif
    if(GSU.vPlotOptionReg & 0x04)
	    c = (c & 0xf0) | (c >> 4);

    if(GSU.vPlotOptionReg & 0x08)
    {
        GSU.vColorReg &= 0xf0;
        GSU.vColorReg |= c & 0x0f;
    }
    else
	    GSU.vColorReg = USEX8(c);

    CLRFLAGS;
    R15++;
}

/* df(ALT2) - ramb - set current RAM bank */
static inline void fx_ramb()
{
    GSU.vRamBankReg = SREG & (FX_RAM_BANKS-1);
    GSU.pvRamBank = GSU.apvRamBank[GSU.vRamBankReg & (FX_RAM_BANKS-1)];
    CLRFLAGS;
    R15++;
}

/* df(ALT3) - romb - set current ROM bank */
static inline void fx_romb()
{
    GSU.vRomBankReg = USEX8(SREG) & 0x7f;
    GSU.pvRomBank = GSU.apvRomBank[GSU.vRomBankReg];
    CLRFLAGS;
    R15++;
}

/* e0-ee - dec rn - decrement by one */
static inline void fx_dec_r(uint8 reg) {
    ASSUME_REG(0, 14);

    uint32 resultNew = GSU.avReg[reg] - 1;
    asm (
        "msr cpsr_f, %0\n\t"
        "lsl %0, %1, #16\n\t"
        "movs %0, %0\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS)
        : "r" (resultNew)
        : "cc"
    );
    GSU.avReg[reg] = resultNew;

    CLRFLAGS;
    R15++;
}

static inline void fx_dec_r14() {
    fx_dec_r(14);
    READR14;
}

/* ef - getb - get byte from ROM at address R14 */
static inline void fx_getb()
{
    uint32 v;
#ifndef FX_DO_ROMBUFFER
    v = (uint32)ROM(R14);
#else
    v = (uint32)GSU.vRomBuffer;
#endif
    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* ef(ALT1) - getbh - get high-byte from ROM at address R14 */
static inline void fx_getbh()
{
    uint32 v;
#ifndef FX_DO_ROMBUFFER
    uint32 c = (uint32) ROM(R14);
#else
    uint32 c = USEX8(GSU.vRomBuffer);
#endif
    v = USEX8(SREG) | (c<<8);
    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* ef(ALT2) - getbl - get low-byte from ROM at address R14 */
static inline void fx_getbl()
{
    uint32 v;
#ifndef FX_DO_ROMBUFFER
    uint32 c = (uint32) ROM(R14);
#else
    uint32 c = USEX8(GSU.vRomBuffer);
#endif
    v = (SREG & 0xff00) | c;
    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* ef(ALT3) - getbs - get sign extended byte from ROM at address R14 */
static inline void fx_getbs()
{
    uint32 v;
#ifndef FX_DO_ROMBUFFER
    int8 c = ROM(R14);
    v = SEX8(c);
#else
    v = SEX8(GSU.vRomBuffer);
#endif
    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* f0-ff - iwt rn,#xx - immediate word transfer to register */
static inline void fx_iwt_r(uint8 reg) {
    ASSUME_REG(0, 15);
    uint16 v = PIPE;
    uint32 r15 = R15 + 1;
    FETCHPIPE2(r15);
    r15++;
    v |= USEX8(PIPE) << 8;
    FETCHPIPE2(r15);
    R15 = r15 + 1;
    GSU.avReg[reg] = v;
    CLRFLAGS;
}

static inline void fx_iwt_r14() {
    fx_iwt_r(14);
    READR14;
}

/* f0-ff(ALT1) - lm rn,(xx) - load word from RAM */
static inline void fx_lm_r(uint8 reg) {
    ASSUME_REG(0, 15);
    GSU.vLastRamAdr = PIPE;
    uint32 r15 = R15 + 1;
    FETCHPIPE2(r15);
    r15++;
    GSU.vLastRamAdr |= PIPE << 8;
    FETCHPIPE2(r15);
    R15 = r15 + 1;
    GSU.avReg[reg] = RAM(GSU.vLastRamAdr)
                   | USEX8(RAM(GSU.vLastRamAdr^1)) << 8;
    CLRFLAGS;
}

static inline void fx_lm_r14() {
    fx_lm_r(14);
    READR14;
}

/* f0-ff(ALT2) - sm (xx),rn - store word in RAM */
/* If rn == r15, is the value of r15 before or after the extra bytes are read? */
static inline void fx_sm_r(uint8 reg) {
    ASSUME_REG(0, 15);
    uint16 v = GSU.avReg[reg];
    GSU.vLastRamAdr = PIPE;
    R15++;
    FETCHPIPE;
    R15++;
    GSU.vLastRamAdr |= PIPE << 8;
    FETCHPIPE;
    RAM(GSU.vLastRamAdr) = (uint8)v;
    RAM(GSU.vLastRamAdr^1) = (uint8)(v>>8);
    CLRFLAGS;
    R15++;
}

/*** GSU executions functions ***/

static uint32 fx_run(uint32 nInstructions)
{
    PUSH_RESERVED;
    fx_load_reserved();
    
    void (*pfPlot)(void);
    void (*pfRpix)(void);

    ASSUME(GSU.vMode <= 4);
    switch (GSU.vMode) {
        case 0: pfPlot = fx_plot_2bit; pfRpix = fx_rpix_2bit; break;
        case 1: pfPlot = fx_plot_4bit; pfRpix = fx_rpix_4bit; break;
        case 2: pfPlot = fx_plot_4bit; pfRpix = fx_rpix_4bit; break;
        case 3: pfPlot = fx_plot_8bit; pfRpix = fx_rpix_8bit; break;
        case 4: pfPlot = fx_plot_obj;  pfRpix = fx_rpix_obj;  break;
    }

    // WYATT_TODO we could reintroduce the while(1) loop in another
    // translation unit to speed up star fox. In d3c1796, I found
    // that it dropped star fox's control select screen from
    // ~5.15ms to ~4.9ms. For an area without much GSU action,
    // that's pretty good.
    uint32 vCounter = nInstructions;
    READR14;
    while(LIKELY(vCounter-- > 0))
    {
        uint16 vOpcode = PIPE | (SFR & (FLG_ALT1 | FLG_ALT2));
        uint8 vLow = vOpcode & 0xf;
        FETCHPIPE;

		// If you replace this, you must:
		// - Replace fx_stop's break with goto loop_end or equivalent
		// - Replace 0x04c and 0x24c with GSU.pfPlot
		// - Replace 0x14c and 0x34c with GSU.pfRpix
		switch (vOpcode) {
            case 0x000: case 0x100: case 0x200: case 0x300: fx_stop(); goto loop_end;
            case 0x001: case 0x101: case 0x201: case 0x301: fx_nop(); break;
            case 0x002: case 0x102: case 0x202: case 0x302: fx_cache(); break;
            case 0x003: case 0x103: case 0x203: case 0x303: fx_lsr(); break;
            case 0x004: case 0x104: case 0x204: case 0x304: fx_rol(); break;
            case 0x005: case 0x105: case 0x205: case 0x305: fx_bra(); break;
            case 0x006: case 0x106: case 0x206: case 0x306: fx_bge(); break;
            case 0x007: case 0x107: case 0x207: case 0x307: fx_blt(); break;
            case 0x008: case 0x108: case 0x208: case 0x308: fx_bne(); break;
            case 0x009: case 0x109: case 0x209: case 0x309: fx_beq(); break;
            case 0x00a: case 0x10a: case 0x20a: case 0x30a: fx_bpl(); break;
            case 0x00b: case 0x10b: case 0x20b: case 0x30b: fx_bmi(); break;
            case 0x00c: case 0x10c: case 0x20c: case 0x30c: fx_bcc(); break;
            case 0x00d: case 0x10d: case 0x20d: case 0x30d: fx_bcs(); break;
            case 0x00e: case 0x10e: case 0x20e: case 0x30e: fx_bvc(); break;
            case 0x00f: case 0x10f: case 0x20f: case 0x30f: fx_bvs(); break;
            case 0x010: case 0x110: case 0x210: case 0x310:
            case 0x011: case 0x111: case 0x211: case 0x311:
            case 0x012: case 0x112: case 0x212: case 0x312:
            case 0x013: case 0x113: case 0x213: case 0x313:
            case 0x014: case 0x114: case 0x214: case 0x314:
            case 0x015: case 0x115: case 0x215: case 0x315:
            case 0x016: case 0x116: case 0x216: case 0x316:
            case 0x017: case 0x117: case 0x217: case 0x317:
            case 0x018: case 0x118: case 0x218: case 0x318:
            case 0x019: case 0x119: case 0x219: case 0x319:
            case 0x01a: case 0x11a: case 0x21a: case 0x31a:
            case 0x01b: case 0x11b: case 0x21b: case 0x31b:
            case 0x01c: case 0x11c: case 0x21c: case 0x31c:
            case 0x01d: case 0x11d: case 0x21d: case 0x31d: fx_to_r(vLow); break;
            case 0x01e: case 0x11e: case 0x21e: case 0x31e: fx_to_r14(); break;
            case 0x01f: case 0x11f: case 0x21f: case 0x31f: fx_to_r15(); break;
            case 0x020: case 0x120: case 0x220: case 0x320:
            case 0x021: case 0x121: case 0x221: case 0x321:
            case 0x022: case 0x122: case 0x222: case 0x322:
            case 0x023: case 0x123: case 0x223: case 0x323:
            case 0x024: case 0x124: case 0x224: case 0x324:
            case 0x025: case 0x125: case 0x225: case 0x325:
            case 0x026: case 0x126: case 0x226: case 0x326:
            case 0x027: case 0x127: case 0x227: case 0x327:
            case 0x028: case 0x128: case 0x228: case 0x328:
            case 0x029: case 0x129: case 0x229: case 0x329:
            case 0x02a: case 0x12a: case 0x22a: case 0x32a:
            case 0x02b: case 0x12b: case 0x22b: case 0x32b:
            case 0x02c: case 0x12c: case 0x22c: case 0x32c:
            case 0x02d: case 0x12d: case 0x22d: case 0x32d:
            case 0x02e: case 0x12e: case 0x22e: case 0x32e:
            case 0x02f: case 0x12f: case 0x22f: case 0x32f: fx_with(vLow); break;
            case 0x030: case 0x230: 
            case 0x031: case 0x231: 
            case 0x032: case 0x232: 
            case 0x033: case 0x233: 
            case 0x034: case 0x234: 
            case 0x035: case 0x235: 
            case 0x036: case 0x236: 
            case 0x037: case 0x237: 
            case 0x038: case 0x238: 
            case 0x039: case 0x239: 
            case 0x03a: case 0x23a: 
            case 0x03b: case 0x23b: fx_stw_r(vLow); break;
            case 0x03c: case 0x13c: case 0x23c: case 0x33c: fx_loop(); break;
            case 0x03d: case 0x13d: case 0x23d: case 0x33d: fx_alt1(); break;
            case 0x03e: case 0x13e: case 0x23e: case 0x33e: fx_alt2(); break;
            case 0x03f: case 0x13f: case 0x23f: case 0x33f: fx_alt3(); break;
            case 0x040: case 0x240:
            case 0x041: case 0x241:
            case 0x042: case 0x242:
            case 0x043: case 0x243:
            case 0x044: case 0x244:
            case 0x045: case 0x245:
            case 0x046: case 0x246:
            case 0x047: case 0x247:
            case 0x048: case 0x248:
            case 0x049: case 0x249:
            case 0x04a: case 0x24a:
            case 0x04b: case 0x24b: fx_ldw_r(vLow); break;
            case 0x04c: case 0x24c: pfPlot(); break;
            case 0x04d: case 0x14d: case 0x24d: case 0x34d: fx_swap(); break;
            case 0x04e: case 0x24e: fx_color(); break;
            case 0x04f: case 0x14f: case 0x24f: case 0x34f: fx_not(); break;
            case 0x050:
            case 0x051:
            case 0x052:
            case 0x053:
            case 0x054:
            case 0x055:
            case 0x056:
            case 0x057:
            case 0x058:
            case 0x059:
            case 0x05a:
            case 0x05b:
            case 0x05c:
            case 0x05d:
            case 0x05e:
            case 0x05f: fx_add_r(vLow); break;
            case 0x060:
            case 0x061:
            case 0x062:
            case 0x063:
            case 0x064:
            case 0x065:
            case 0x066:
            case 0x067:
            case 0x068:
            case 0x069:
            case 0x06a:
            case 0x06b:
            case 0x06c:
            case 0x06d:
            case 0x06e:
            case 0x06f: fx_sub_r(vLow); break;
            case 0x070: case 0x170: case 0x270: case 0x370: fx_merge(); break;
            case 0x071:
            case 0x072:
            case 0x073:
            case 0x074:
            case 0x075:
            case 0x076:
            case 0x077:
            case 0x078:
            case 0x079:
            case 0x07a:
            case 0x07b:
            case 0x07c:
            case 0x07d:
            case 0x07e:
            case 0x07f: fx_and_r(vLow); break;
            case 0x080:
            case 0x081:
            case 0x082:
            case 0x083:
            case 0x084:
            case 0x085:
            case 0x086:
            case 0x087:
            case 0x088:
            case 0x089:
            case 0x08a:
            case 0x08b:
            case 0x08c:
            case 0x08d:
            case 0x08e:
            case 0x08f: fx_mult_r(vLow); break;
            case 0x090: case 0x190: case 0x290: case 0x390: fx_sbk(); break;
            case 0x091: case 0x191: case 0x291: case 0x391:
            case 0x092: case 0x192: case 0x292: case 0x392:
            case 0x093: case 0x193: case 0x293: case 0x393:
            case 0x094: case 0x194: case 0x294: case 0x394: fx_link_i(vLow); break;
            case 0x095: case 0x195: case 0x295: case 0x395: fx_sex(); break;
            case 0x096: case 0x296: fx_asr(); break;
            case 0x097: case 0x197: case 0x297: case 0x397: fx_ror(); break;
            case 0x098: case 0x298:
            case 0x099: case 0x299:
            case 0x09a: case 0x29a:
            case 0x09b: case 0x29b:
            case 0x09c: case 0x29c:
            case 0x09d: case 0x29d: fx_jmp_r(vLow); break;
            case 0x09e: case 0x19e: case 0x29e: case 0x39e: fx_lob(); break;
            case 0x09f: case 0x29f: fx_fmult(); break;
            case 0x0a0:
            case 0x0a1:
            case 0x0a2:
            case 0x0a3:
            case 0x0a4:
            case 0x0a5:
            case 0x0a6:
            case 0x0a7:
            case 0x0a8:
            case 0x0a9:
            case 0x0aa:
            case 0x0ab:
            case 0x0ac:
            case 0x0ad:
            case 0x0af: fx_ibt_r(vLow); break; // Out-of-order: 14 is a special case
            case 0x0ae: fx_ibt_r14(); break;
            case 0x0b0: case 0x1b0: case 0x2b0: case 0x3b0:
            case 0x0b1: case 0x1b1: case 0x2b1: case 0x3b1:
            case 0x0b2: case 0x1b2: case 0x2b2: case 0x3b2:
            case 0x0b3: case 0x1b3: case 0x2b3: case 0x3b3:
            case 0x0b4: case 0x1b4: case 0x2b4: case 0x3b4:
            case 0x0b5: case 0x1b5: case 0x2b5: case 0x3b5:
            case 0x0b6: case 0x1b6: case 0x2b6: case 0x3b6:
            case 0x0b7: case 0x1b7: case 0x2b7: case 0x3b7:
            case 0x0b8: case 0x1b8: case 0x2b8: case 0x3b8:
            case 0x0b9: case 0x1b9: case 0x2b9: case 0x3b9:
            case 0x0ba: case 0x1ba: case 0x2ba: case 0x3ba:
            case 0x0bb: case 0x1bb: case 0x2bb: case 0x3bb:
            case 0x0bc: case 0x1bc: case 0x2bc: case 0x3bc:
            case 0x0bd: case 0x1bd: case 0x2bd: case 0x3bd:
            case 0x0be: case 0x1be: case 0x2be: case 0x3be:
            case 0x0bf: case 0x1bf: case 0x2bf: case 0x3bf: fx_from_r(vLow); break;
            case 0x0c0: case 0x1c0: case 0x2c0: case 0x3c0: fx_hib(); break;
            case 0x0c1:
            case 0x0c2:
            case 0x0c3:
            case 0x0c4:
            case 0x0c5:
            case 0x0c6:
            case 0x0c7:
            case 0x0c8:
            case 0x0c9:
            case 0x0ca:
            case 0x0cb:
            case 0x0cc:
            case 0x0cd:
            case 0x0ce:
            case 0x0cf: fx_or_r(vLow); break;
            case 0x0d0: case 0x1d0: case 0x2d0: case 0x3d0:
            case 0x0d1: case 0x1d1: case 0x2d1: case 0x3d1:
            case 0x0d2: case 0x1d2: case 0x2d2: case 0x3d2:
            case 0x0d3: case 0x1d3: case 0x2d3: case 0x3d3:
            case 0x0d4: case 0x1d4: case 0x2d4: case 0x3d4:
            case 0x0d5: case 0x1d5: case 0x2d5: case 0x3d5:
            case 0x0d6: case 0x1d6: case 0x2d6: case 0x3d6:
            case 0x0d7: case 0x1d7: case 0x2d7: case 0x3d7:
            case 0x0d8: case 0x1d8: case 0x2d8: case 0x3d8:
            case 0x0d9: case 0x1d9: case 0x2d9: case 0x3d9:
            case 0x0da: case 0x1da: case 0x2da: case 0x3da:
            case 0x0db: case 0x1db: case 0x2db: case 0x3db:
            case 0x0dc: case 0x1dc: case 0x2dc: case 0x3dc:
            case 0x0dd: case 0x1dd: case 0x2dd: case 0x3dd: fx_inc_r(vLow); break;
            case 0x0de: case 0x1de: case 0x2de: case 0x3de: fx_inc_r14(); break;
            case 0x0df: case 0x1df: fx_getc(); break;
            case 0x0e0: case 0x1e0: case 0x2e0: case 0x3e0:
            case 0x0e1: case 0x1e1: case 0x2e1: case 0x3e1:
            case 0x0e2: case 0x1e2: case 0x2e2: case 0x3e2:
            case 0x0e3: case 0x1e3: case 0x2e3: case 0x3e3:
            case 0x0e4: case 0x1e4: case 0x2e4: case 0x3e4:
            case 0x0e5: case 0x1e5: case 0x2e5: case 0x3e5:
            case 0x0e6: case 0x1e6: case 0x2e6: case 0x3e6:
            case 0x0e7: case 0x1e7: case 0x2e7: case 0x3e7:
            case 0x0e8: case 0x1e8: case 0x2e8: case 0x3e8:
            case 0x0e9: case 0x1e9: case 0x2e9: case 0x3e9:
            case 0x0ea: case 0x1ea: case 0x2ea: case 0x3ea:
            case 0x0eb: case 0x1eb: case 0x2eb: case 0x3eb:
            case 0x0ec: case 0x1ec: case 0x2ec: case 0x3ec:
            case 0x0ed: case 0x1ed: case 0x2ed: case 0x3ed: fx_dec_r(vLow); break;
            case 0x0ee: case 0x1ee: case 0x2ee: case 0x3ee: fx_dec_r14(); break;
            case 0x0ef: fx_getb(); break;
            case 0x0f0:
            case 0x0f1:
            case 0x0f2:
            case 0x0f3:
            case 0x0f4:
            case 0x0f5:
            case 0x0f6:
            case 0x0f7:
            case 0x0f8:
            case 0x0f9:
            case 0x0fa:
            case 0x0fb:
            case 0x0fc:
            case 0x0fd:
            case 0x0ff: fx_iwt_r(vLow); break; // Out-of-order: 14 is a special case
            case 0x0fe: fx_iwt_r14(); break;
            case 0x130: case 0x330: 
            case 0x131: case 0x331: 
            case 0x132: case 0x332: 
            case 0x133: case 0x333: 
            case 0x134: case 0x334: 
            case 0x135: case 0x335: 
            case 0x136: case 0x336: 
            case 0x137: case 0x337: 
            case 0x138: case 0x338: 
            case 0x139: case 0x339: 
            case 0x13a: case 0x33a: 
            case 0x13b: case 0x33b: fx_stb_r(vLow); break;
            case 0x140: case 0x340: 
            case 0x141: case 0x341: 
            case 0x142: case 0x342: 
            case 0x143: case 0x343: 
            case 0x144: case 0x344: 
            case 0x145: case 0x345: 
            case 0x146: case 0x346: 
            case 0x147: case 0x347: 
            case 0x148: case 0x348: 
            case 0x149: case 0x349: 
            case 0x14a: case 0x34a: 
            case 0x14b: case 0x34b: fx_ldb_r(vLow); break;
            case 0x14c: case 0x34c: pfRpix(); break;
            case 0x14e: case 0x34e: fx_cmode(); break;
            case 0x150:
            case 0x151:
            case 0x152:
            case 0x153:
            case 0x154:
            case 0x155:
            case 0x156:
            case 0x157:
            case 0x158:
            case 0x159:
            case 0x15a:
            case 0x15b:
            case 0x15c:
            case 0x15d:
            case 0x15e:
            case 0x15f: fx_adc_r(vLow); break;
            case 0x160: 
            case 0x161: 
            case 0x162: 
            case 0x163: 
            case 0x164: 
            case 0x165: 
            case 0x166: 
            case 0x167: 
            case 0x168: 
            case 0x169: 
            case 0x16a: 
            case 0x16b: 
            case 0x16c: 
            case 0x16d: 
            case 0x16e: 
            case 0x16f: fx_sbc_r(vLow); break;
            case 0x171: 
            case 0x172: 
            case 0x173: 
            case 0x174: 
            case 0x175: 
            case 0x176: 
            case 0x177: 
            case 0x178: 
            case 0x179: 
            case 0x17a: 
            case 0x17b: 
            case 0x17c: 
            case 0x17d: 
            case 0x17e: 
            case 0x17f: fx_bic_r(vLow); break;
            case 0x180:
            case 0x181:
            case 0x182:
            case 0x183:
            case 0x184:
            case 0x185:
            case 0x186:
            case 0x187:
            case 0x188:
            case 0x189:
            case 0x18a:
            case 0x18b:
            case 0x18c:
            case 0x18d:
            case 0x18e:
            case 0x18f: fx_umult_r(vLow); break;
            case 0x196: case 0x396: fx_div2(); break;
            case 0x198: case 0x398:
            case 0x199: case 0x399:
            case 0x19a: case 0x39a:
            case 0x19b: case 0x39b:
            case 0x19c: case 0x39c:
            case 0x19d: case 0x39d: fx_ljmp_r(vLow); break;
            case 0x19f: case 0x39f: fx_lmult(); break;
            case 0x1a0: case 0x3a0: 
            case 0x1a1: case 0x3a1: 
            case 0x1a2: case 0x3a2: 
            case 0x1a3: case 0x3a3: 
            case 0x1a4: case 0x3a4: 
            case 0x1a5: case 0x3a5: 
            case 0x1a6: case 0x3a6: 
            case 0x1a7: case 0x3a7: 
            case 0x1a8: case 0x3a8: 
            case 0x1a9: case 0x3a9: 
            case 0x1aa: case 0x3aa: 
            case 0x1ab: case 0x3ab: 
            case 0x1ac: case 0x3ac: 
            case 0x1ad: case 0x3ad: 
            case 0x1af: case 0x3af: fx_lms_r(vLow); break; // Out-of-order: 14 is a special case
            case 0x1ae: case 0x3ae: fx_lms_r14(); break;
            case 0x1c1: 
            case 0x1c2: 
            case 0x1c3: 
            case 0x1c4: 
            case 0x1c5: 
            case 0x1c6: 
            case 0x1c7: 
            case 0x1c8: 
            case 0x1c9: 
            case 0x1ca: 
            case 0x1cb: 
            case 0x1cc: 
            case 0x1cd: 
            case 0x1ce: 
            case 0x1cf: fx_xor_r(vLow); break;
            case 0x1ef: fx_getbh(); break;
            case 0x1f0: case 0x3f0:
            case 0x1f1: case 0x3f1:
            case 0x1f2: case 0x3f2:
            case 0x1f3: case 0x3f3:
            case 0x1f4: case 0x3f4:
            case 0x1f5: case 0x3f5:
            case 0x1f6: case 0x3f6:
            case 0x1f7: case 0x3f7:
            case 0x1f8: case 0x3f8:
            case 0x1f9: case 0x3f9:
            case 0x1fa: case 0x3fa:
            case 0x1fb: case 0x3fb:
            case 0x1fc: case 0x3fc:
            case 0x1fd: case 0x3fd:
            case 0x1ff: case 0x3ff: fx_lm_r(vLow); break; // Out-of-order: 14 is a special case
            case 0x1fe: case 0x3fe: fx_lm_r14(); break;
            case 0x250:
            case 0x251:
            case 0x252:
            case 0x253:
            case 0x254:
            case 0x255:
            case 0x256:
            case 0x257:
            case 0x258:
            case 0x259:
            case 0x25a:
            case 0x25b:
            case 0x25c:
            case 0x25d:
            case 0x25e:
            case 0x25f: fx_add_i(vLow); break;
            case 0x260:
            case 0x261:
            case 0x262:
            case 0x263:
            case 0x264:
            case 0x265:
            case 0x266:
            case 0x267:
            case 0x268:
            case 0x269:
            case 0x26a:
            case 0x26b:
            case 0x26c:
            case 0x26d:
            case 0x26e:
            case 0x26f: fx_sub_i(vLow); break;
            case 0x271:
            case 0x272:
            case 0x273:
            case 0x274:
            case 0x275:
            case 0x276:
            case 0x277:
            case 0x278:
            case 0x279:
            case 0x27a:
            case 0x27b:
            case 0x27c:
            case 0x27d:
            case 0x27e:
            case 0x27f: fx_and_i(vLow); break;
            case 0x280:
            case 0x281:
            case 0x282:
            case 0x283:
            case 0x284:
            case 0x285:
            case 0x286:
            case 0x287:
            case 0x288:
            case 0x289:
            case 0x28a:
            case 0x28b:
            case 0x28c:
            case 0x28d:
            case 0x28e:
            case 0x28f: fx_mult_i(vLow); break;
            case 0x2a0:
            case 0x2a1:
            case 0x2a2:
            case 0x2a3:
            case 0x2a4:
            case 0x2a5:
            case 0x2a6:
            case 0x2a7:
            case 0x2a8:
            case 0x2a9:
            case 0x2aa:
            case 0x2ab:
            case 0x2ac:
            case 0x2ad:
            case 0x2ae:
            case 0x2af: fx_sms_r(vLow); break;
            case 0x2c1:
            case 0x2c2:
            case 0x2c3:
            case 0x2c4:
            case 0x2c5:
            case 0x2c6:
            case 0x2c7:
            case 0x2c8:
            case 0x2c9:
            case 0x2ca:
            case 0x2cb:
            case 0x2cc:
            case 0x2cd:
            case 0x2ce:
            case 0x2cf: fx_or_i(vLow); break;
            case 0x2df: fx_ramb(); break;
            case 0x2ef: fx_getbl(); break;
            case 0x2f0:
            case 0x2f1:
            case 0x2f2:
            case 0x2f3:
            case 0x2f4:
            case 0x2f5:
            case 0x2f6:
            case 0x2f7:
            case 0x2f8:
            case 0x2f9:
            case 0x2fa:
            case 0x2fb:
            case 0x2fc:
            case 0x2fd:
            case 0x2fe:
            case 0x2ff: fx_sm_r(vLow); break;
            case 0x350:
            case 0x351:
            case 0x352:
            case 0x353:
            case 0x354:
            case 0x355:
            case 0x356:
            case 0x357:
            case 0x358:
            case 0x359:
            case 0x35a:
            case 0x35b:
            case 0x35c:
            case 0x35d:
            case 0x35e:
            case 0x35f: fx_adc_i(vLow); break;
            case 0x360: 
            case 0x361: 
            case 0x362: 
            case 0x363: 
            case 0x364: 
            case 0x365: 
            case 0x366: 
            case 0x367: 
            case 0x368: 
            case 0x369: 
            case 0x36a: 
            case 0x36b: 
            case 0x36c: 
            case 0x36d: 
            case 0x36e: 
            case 0x36f: fx_cmp_r(vLow); break;
            case 0x371:
            case 0x372:
            case 0x373:
            case 0x374:
            case 0x375:
            case 0x376:
            case 0x377:
            case 0x378:
            case 0x379:
            case 0x37a:
            case 0x37b:
            case 0x37c:
            case 0x37d:
            case 0x37e:
            case 0x37f: fx_bic_i(vLow); break;
            case 0x380:
            case 0x381:
            case 0x382:
            case 0x383:
            case 0x384:
            case 0x385:
            case 0x386:
            case 0x387:
            case 0x388:
            case 0x389:
            case 0x38a:
            case 0x38b:
            case 0x38c:
            case 0x38d:
            case 0x38e:
            case 0x38f: fx_umult_i(vLow); break;
            case 0x3c1:
            case 0x3c2:
            case 0x3c3:
            case 0x3c4:
            case 0x3c5:
            case 0x3c6:
            case 0x3c7:
            case 0x3c8:
            case 0x3c9:
            case 0x3ca:
            case 0x3cb:
            case 0x3cc:
            case 0x3cd:
            case 0x3ce:
            case 0x3cf: fx_xor_i(vLow); break;
            case 0x3df: fx_romb(); break;
            case 0x3ef: fx_getbs(); break;
		}
	}

    loop_end:

 /*
#ifndef FX_ADDRESS_CHECK
    GSU.vPipeAdr = USEX16(R15-1) | (USEX8(GSU.vPrgBankReg)<<16);
#endif
*/

#if T3DS_COUNT_INSTRUCTIONS == 1
    t3dsCountN(&t3dsMain, Snx_GsuInstructions, nInstructions - vCounter);
#endif

    fx_save_reserved();
    POP_RESERVED;
    return nInstructions;
}

COLD static uint32 fx_run_to_breakpoint(uint32 nInstructions)
{
    printf ("run_to_bp\n");
    uint32 vCounter = 0;
    while(TF(G) && vCounter < nInstructions)
    {
		vCounter++;
        // FX_STEP; // WYATT_TODO fix this.
        if(USEX16(R15) == GSU.vBreakPoint)
        {
            GSU.vErrorCode = FX_BREAKPOINT;
            break;
        }
    }
    /*
#ifndef FX_ADDRESS_CHECK
    GSU.vPipeAdr = USEX16(R15-1) | (USEX8(GSU.vPrgBankReg)<<16);
#endif
*/
    return vCounter;
}

COLD static uint32 fx_step_over(uint32 nInstructions)
{
    printf ("run_step_over\n");
    
    uint32 vCounter = 0;
    while(TF(G) && vCounter < nInstructions)
    {
		vCounter++;
        // FX_STEP; // WYATT_TODO fix this.
        if(USEX16(R15) == GSU.vBreakPoint)
        {
            GSU.vErrorCode = FX_BREAKPOINT;
            break;
        }
        if(USEX16(R15) == GSU.vStepPoint)
            break;
        }
    /*
#ifndef FX_ADDRESS_CHECK
    GSU.vPipeAdr = USEX16(R15-1) | (USEX8(GSU.vPrgBankReg)<<16);
#endif
*/
    return vCounter;
}

#ifdef FX_FUNCTION_TABLE
uint32 (*FX_FUNCTION_TABLE[])(uint32) =
#else
uint32 (*fx_apfFunctionTable[])(uint32) =
#endif
{
    &fx_run,
    &fx_run_to_breakpoint,
    &fx_step_over,
};
