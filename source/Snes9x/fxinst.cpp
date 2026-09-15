#include "copyright.h"


#define FX_DO_ROMBUFFER

#include "fxemu.h"
#include "fxinst.h"
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
#define ARRAY_COUNT(arr_) (sizeof(arr_) / sizeof(arr_[0]))

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

// If 1, some nonsense C code will ATTEMPT to circumvent
// tail merging of the instruction handlers. This is not
// guaranteed to work perfectly on all compilers, so you
// must double-check the ASM that the compiler produces!
#define ATTEMPT_TO_DISABLE_TAIL_MERGE 1

#if ATTEMPT_TO_DISABLE_TAIL_MERGE == 1
#define DEFEAT_TAIL_MERGE asm volatile ("" : : "i" (__LINE__))
// #define DEFEAT_TAIL_MERGE asm volatile ("eor r0, r0, #0" : : "i" (__LINE__)) // For searching ASM
#else
#define DEFEAT_TAIL_MERGE do {} while(0)
#endif

#if REGISTER_RESERVATIONS == 1

// GSU status register, sans the NZCV flags
#undef SFR
register uint32 statusRegLocal asm("r6");
#define SFR statusRegLocal

// ARM NZCV flags. Always synchronized with GSU flags.
register uint32 armFlagsLocal asm("r7");
#define ARMFLAGS armFlagsLocal

// GSU PIPE
#undef PIPE
register uint8 pipeLocal asm("r8");
#define PIPE pipeLocal

// GSU SREG
#undef SREG
#undef SREG_PTR
register uint16* pvSregLocal asm("r9");
#define SREG_PTR pvSregLocal
#define SREG *SREG_PTR

// GSU DREG
#undef DREG
#undef DREG_PTR
register uint16* pvDregLocal asm("r10");
#define DREG_PTR pvDregLocal
#define DREG *DREG_PTR
// register void* unusedReg asm("lr");

// If any of these registers are used by your function or its
// statically-linked subroutines, these must be placed at the
// start and end of said function if it is externally linked.
// Both must always be an even number of registers per AAPCS!
#define PUSH_RESERVED asm volatile ("push {r6, r7, r8, r9, r10, r11}") // WYATT_TODO Modifying the stackptr is UB
#define POP_RESERVED  asm volatile ("pop  {r6, r7, r8, r9, r10, r11}")

// Necessary redefs if DREG and SREG are pointers
#undef TESTR14
#undef CLRFLAGS
#define CLRFLAGS SFR &= ~(FLG_ALT1|FLG_ALT2|FLG_B); DREG_PTR = SREG_PTR = GETR(0);

// The else case is usually an order of magnitude more common in aggregate here
#define TESTR14 if(UNLIKELY((pvDregLocal) == GETR(14))) { READR14; } else {}

// The compiler doesn't realize it can do this, so it loads from memory
//!!! This relies on the fact that GSU.avReg is at the start of GSU!
static inline uint16* GETR(size_t reg)
{
    uint16* ptr;
    asm ("add %0, %1, %2" : "=r" (ptr) : "r" (&GSU), "iIr" (reg * sizeof(uint16) + 4));
    return ptr;
}

// Saves the reserved registers back to GSU
static inline void fx_save_reserved(void)
{
    GSU.vStatusReg = SFR;
    GSU.armFlags = ARMFLAGS >> 24;
    GSU.vPipe = PIPE;
    GSU.pvSreg = SREG_PTR - GSU.avReg;
    GSU.pvDreg = DREG_PTR - GSU.avReg;
}

// Loads the reserved registers from GSU
static inline void fx_load_reserved(void)
{
    SFR = GSU.vStatusReg;
    ARMFLAGS = GSU.armFlags << 24;
    PIPE = GSU.vPipe;
    pvSregLocal = &GSU.avReg[GSU.pvSreg];
    pvDregLocal = &GSU.avReg[GSU.pvDreg];
}

// register reservations are disabled
#else
uint32 armFlagsLocal;
#define ARMFLAGS (armFlagsLocal)
#define PUSH_RESERVED do {} while(0)
#define POP_RESERVED do {} while(0)
static inline void fx_save_reserved(void) {GSU.armFlags = ARMFLAGS >> 24;} // Stub
static inline void fx_load_reserved(void) {ARMFLAGS = GSU.armFlags << 24;} // Stub
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
static inline void fx_stop(uint8 unused)
{
    CF(G);

    /* Check if we need to generate an IRQ */
    if(!(GSU.pvRegisters[GSU_CFGR] & 0x80))
        SF(IRQ);

    GSU.vPlotOptionReg = 0;
    PIPE = 1;
    CLRFLAGS;
    R15++;

    DEFEAT_TAIL_MERGE;
}

/* 01 - nop - no operation */
static inline void fx_nop(uint8 unused) {
    CLRFLAGS;
    R15++;

    DEFEAT_TAIL_MERGE;
}

/* 02 - cache - reintialize GSU cache */
static inline void fx_cache(uint8 unused)
{
    uint32 c = R15 & 0xfff0;
    if(GSU.vCacheBaseReg != c)
    {
        GSU.vCacheFlags = 0;
        GSU.vCacheBaseReg = c;
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
    
    DEFEAT_TAIL_MERGE;
}

/* 03 - lsr - logic shift right */
static inline void fx_lsr(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
}

/* 04 - rol - rotate left */
static inline void fx_rol(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
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
                                        \
    DEFEAT_TAIL_MERGE;                  \
}

/* 05 - bra - branch always */
static inline void fx_bra(uint8 unused) {
    uint8 v = PIPE;
    uint32 r15 = R15 + 1;
    FETCHPIPE2(r15);
    R15 = r15 + SEX8(v);
    
    DEFEAT_TAIL_MERGE;
}

/* 06 - blt - branch on less than */
static inline void fx_blt(uint8 unused) { BRA_COND( "lt", "ge" ); }

/* 07 - bge - branch on greater or equals */
static inline void fx_bge(uint8 unused) { BRA_COND( "ge", "lt" ); }

/* 08 - bne - branch on not equal */
static inline void fx_bne(uint8 unused) { BRA_COND( "ne", "eq" ); }

/* 09 - beq - branch on equal */
static inline void fx_beq(uint8 unused) { BRA_COND( "eq", "ne" ); }

/* 0a - bpl - branch on plus */
static inline void fx_bpl(uint8 unused) { BRA_COND( "pl", "mi" ); }

/* 0b - bmi - branch on minus */
static inline void fx_bmi(uint8 unused) { BRA_COND( "mi", "pl" ); }

/* 0c - bcc - branch on carry clear */
static inline void fx_bcc(uint8 unused) { BRA_COND( "cc", "cs" ); }

/* 0d - bcs - branch on carry set */
static inline void fx_bcs(uint8 unused) { BRA_COND( "cs", "cc" ); }

/* 0e - bvc - branch on overflow clear */
static inline void fx_bvc(uint8 unused) { BRA_COND( "vc", "vs" ); }

/* 0f - bvs - branch on overflow set */
static inline void fx_bvs(uint8 unused) { BRA_COND( "vs", "vc" ); }

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
    
    DEFEAT_TAIL_MERGE;
}

/* TO_R14: set register 14 as destination register */
/* If B flag is set, move SREG to R14 and READR14 instead */
static inline void fx_to_r14(uint8 unused) {
    if(TF(B)) {
        R14 = SREG;
        CLRFLAGS;
        READR14;
    }
    else
        DREG_PTR = GETR(14);
    R15++;
    
    DEFEAT_TAIL_MERGE;
}

/* TO_R15: Set register 15 as destination register and increment */
/* If B flag is set, move SREG to R15 instead */
static inline void fx_to_r15(uint8 unused) {
    if(TF(B)) {
        R15 = SREG;
        CLRFLAGS;
    }
    else {
        DREG_PTR = GETR(15);
        R15++;
    }
    
    DEFEAT_TAIL_MERGE;
}

/* 20-2f - with rn - set register n as source and destination register */
static inline void fx_with_r(uint8 reg) {
    ASSUME_REG(0, 15);
    SF(B);
    SREG_PTR = DREG_PTR = &GSU.avReg[reg];
    R15++;
    
    DEFEAT_TAIL_MERGE;
}

/* 30-3b - stw (rn) - store word */
static inline void fx_stw_r(uint8 reg) {
    ASSUME_REG(0, 11);
    uint16 r = GSU.vLastRamAdr = GSU.avReg[reg];
    uint16 sReg = SREG;
    uint8* ram = &RAM(0);
    ram[r] = (uint8)sReg;
    ram[r^1] = (uint8)(sReg>>8);
    CLRFLAGS;
    R15++;
    
    DEFEAT_TAIL_MERGE;
}

/* 30-3b(ALT1) - stb (rn) - store byte */
static inline void fx_stb_r(uint8 reg) {
    ASSUME_REG(0, 11);
    GSU.vLastRamAdr = GSU.avReg[reg];
    RAM(GSU.avReg[reg]) = (uint8)SREG;
    CLRFLAGS;
    R15++;
    
    DEFEAT_TAIL_MERGE;
}

/* 3c - loop - decrement loop counter, and branch on not zero */
static inline void fx_loop(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
}

/* 3d - alt1 - set alt1 mode */
static inline void fx_alt1(uint8 unused) {
    SF(ALT1);
    CF(B);
    R15++;
    
    DEFEAT_TAIL_MERGE;
}

/* 3e - alt2 - set alt2 mode */
static inline void fx_alt2(uint8 unused) {
    SF(ALT2);
    CF(B);
    R15++;
    
    DEFEAT_TAIL_MERGE;
}

/* 3f - alt3 - set alt3 mode */
static inline void fx_alt3(uint8 unused) {
    SF(ALT1);
    SF(ALT2);
    CF(B);
    R15++;
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
}

/* 4c - plot - plot pixel with R1,R2 as x,y and the color register as the color */
static inline void fx_plot_2bit(uint8 unused)
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

    if(GSU.vPlotOptionReg & PLOT_DITHER)
        c = (x ^ y) & 1 ? (GSU.vColorReg >> 4) : GSU.vColorReg;
    else
        c = GSU.vColorReg;

    // Avoid overwriting transparent pixels? Seems like just an optimization
    if( !(GSU.vPlotOptionReg & PLOT_TRANSPARENT) && !(c & 0xf)) 
        return;

    a = GSU.apvScreen[y] + GSU.x[x >> 3]; // Highly unlikely
    uint32 v = 128U >> (x&7);

    if(c & 0x01) a[0] |= v;
    else         a[0] &= ~v;
    if(c & 0x02) a[1] |= v;
    else         a[1] &= ~v;

    DEFEAT_TAIL_MERGE;
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
static inline void fx_rpix_2bit(uint8 unused)
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 v;

    R15++;

#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return; // Highly unlikely
#endif

    a = GSU.apvScreen[y] + GSU.x[x >> 3]; // Highly unlikely
    v = 128 >> (x&7);

    uint32 dReg = 0;
    TESTBIT(0, 0);
    TESTBIT(1, 1);
    DREG = dReg;

    TESTR14;
    CLRFLAGS;

    DEFEAT_TAIL_MERGE;
}

/* 4c - plot - plot pixel with R1,R2 as x,y and the color register as the color */
static inline void fx_plot_4bit(uint8 unused)
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

    if(GSU.vPlotOptionReg & PLOT_DITHER) // Likelihood depends on game
        c = (x ^ y) & 1 ? (GSU.vColorReg >> 4) : GSU.vColorReg; // About even chance
    else
        c = GSU.vColorReg;

    // Alpha cutout mode
    if( !((GSU.vPlotOptionReg & PLOT_TRANSPARENT) || (c & 0xf))) // Unlikely
        return;

    a = GSU.apvScreen[y] + GSU.x[x >> 3]; // Highly unlikely
    uint32 v = 128U >> (x&7);

    if(c & 0x01) a[0x00] |= v;
    else         a[0x00] &= ~v;
    if(c & 0x02) a[0x01] |= v;
    else         a[0x01] &= ~v;
    if(c & 0x04) a[0x10] |= v;
    else         a[0x10] &= ~v;
    if(c & 0x08) a[0x11] |= v;
    else         a[0x11] &= ~v;

    DEFEAT_TAIL_MERGE;
}

/* 4c(ALT1) - rpix - read color of the pixel with R1,R2 as x,y */
static inline void fx_rpix_4bit(uint8 unused)
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 v;

    R15++;

#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return; // Highly unlikely
#endif

    a = GSU.apvScreen[y] + GSU.x[x >> 3]; // Highly unlikely
    v = 128 >> (x&7);

    uint32 dReg = 0;
    TESTBIT(0x00, 0);
    TESTBIT(0x01, 1);
    TESTBIT(0x10, 2);
    TESTBIT(0x11, 3);
    DREG = dReg;

    TESTR14;
    CLRFLAGS;

    DEFEAT_TAIL_MERGE;
}

/* 8c - plot - plot pixel with R1,R2 as x,y and the color register as the color */
static inline void fx_plot_8bit(uint8 unused)
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

    if (!(GSU.vPlotOptionReg & PLOT_TRANSPARENT)) {
        if ( (GSU.vPlotOptionReg & PLOT_FREEZEHIGH) && !(c & 0xf)) return;
        if (!(GSU.vPlotOptionReg & PLOT_FREEZEHIGH) && !c)         return;
    }

    a = GSU.apvScreen[y] + GSU.x[x >> 3]; // Highly unlikely
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

    DEFEAT_TAIL_MERGE;
}

/* 4c(ALT1) - rpix - read color of the pixel with R1,R2 as x,y */
static inline void fx_rpix_8bit(uint8 unused)
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 v;

    R15++;

#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return;
#endif
    a = GSU.apvScreen[y] + GSU.x[x >> 3]; // Highly unlikely
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
    CLRFLAGS;

    DEFEAT_TAIL_MERGE;
}

/* 4o - plot - plot pixel with R1,R2 as x,y and the color register as the color */
COLD static inline void fx_plot_obj(uint8 unused)
{
    printf ("ERROR fx_plot_obj called\n");
    DEFEAT_TAIL_MERGE;
}

/* 4c(ALT1) - rpix - read color of the pixel with R1,R2 as x,y */
COLD static inline void fx_rpix_obj(uint8 unused)
{
    printf ("ERROR fx_rpix_obj called\n");
    DEFEAT_TAIL_MERGE;
}

/* 4d - swap - swap upper and lower byte of a register */
static inline void fx_swap(uint8 unused)
{
    uint32 v;
    uint16 r15 = R15 + 1;
    asm ("rev16 %0, %1":"=r"(v):"r"(SREG));
    asm (
        "msr cpsr_f, %0\n\t"
        "movs %0, %1\n\t"
        "mrs %0, cpsr\n\t"
        : "+r" (ARMFLAGS)
        : "r" (v | (v << 16))
        : "cc"
    );

    R15 = r15;
    DREG = v;
    TESTR14;
    CLRFLAGS;
    
    DEFEAT_TAIL_MERGE;
}

/* 4e - color - copy source register to color register */
static inline void fx_color(uint8 unused)
{
    uint8 c = (uint8) SREG;
    if(GSU.vPlotOptionReg & PLOT_HIGHNIBBLE)
        c = (c & 0xf0) | (c >> 4);

    if(GSU.vPlotOptionReg & PLOT_FREEZEHIGH)
    {
        GSU.vColorReg &= 0xf0;
        GSU.vColorReg |= c & 0x0f;
    }
    else
        GSU.vColorReg = USEX8(c);

    CLRFLAGS;
    R15++;
    
    DEFEAT_TAIL_MERGE;
}

/* 4e(ALT1) - cmode - set plot option register */
static inline void fx_cmode(uint8 unused)
{
    GSU.vPlotOptionReg = SREG;

    if(GSU.vPlotOptionReg & PLOT_OBJECT)
        GSU.vScreenHeight = 256; /* OBJ Mode (for drawing into sprites) */
    else
        GSU.vScreenHeight = GSU.vScreenRealHeight;

    if (GSU.vPrevScreenHeight != GSU.vScreenHeight) {
        GSU.vPrevScreenHeight  = GSU.vScreenHeight;
        fx_computeScreenPointers(); // Moving this here increases register pressure too much. Leave it in the other file.
    }
    CLRFLAGS;
    R15++;
    
    DEFEAT_TAIL_MERGE;
}

/* 4f - not - perform exclusive exor with 1 on all bits */
static inline void fx_not(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
}

/* 70 - merge - R7 as upper byte, R8 as lower byte (used for texture-mapping) */
static inline void fx_merge(uint8 unused)
{
    uint32 v = (R7 & 0xff00) | ((R8 & 0xff00) >> 8);
    uint32 offset = ((v >> 12) | (v >> 4)) & 0b1111;
    ARMFLAGS = GSU.mergeFlagLut[offset] << ARM_SHIFT;

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
}
  
/* 80-8f(ALT2) - mult #n - 8 bit to 16 bit signed multiply, register * immediate */
static inline void fx_mult_i(uint8 imm) {
    ASSUME_IMM(0, 15);

    uint32 v = SEX8(SREG) * imm;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
}
  
/* 90 - sbk - store word to last accessed RAM address */
static inline void fx_sbk(uint8 unused)
{
    uint16 sReg = SREG;
    uint16 lastAdr = GSU.vLastRamAdr;
    RAM(lastAdr) = (uint8)sReg;
    RAM(lastAdr^1) = (uint8)(sReg>>8);
    CLRFLAGS;
    R15++;
    
    DEFEAT_TAIL_MERGE;
}

/* 91-94 - link #n - R11 = R15 + immediate */
static inline void fx_link_i(uint8 lkn) {
    ASSUME_LKN(1, 4);
    R11 = R15 + lkn;
    CLRFLAGS;
    R15++;
    
    DEFEAT_TAIL_MERGE;
}

/* 95 - sex - sign extend 8 bit to 16 bit */
static inline void fx_sex(uint8 unused)
{
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
    
    DEFEAT_TAIL_MERGE;
}

/* 96 - asr - aritmetric shift right by one */
static inline void fx_asr(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
}

/* 96(ALT1) - div2 - aritmetric shift right by one */
static inline void fx_div2(uint8 unused)
{
    uint32 v;
    asm (
        "cmn %2, #1\n\t"
        "moveq %2, #1\n\t"
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
    
    DEFEAT_TAIL_MERGE;
}

/* 97 - ror - rotate right by one */
static inline void fx_ror(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
}

/* 98-9d - jmp rn - jump to address of register */
static inline void fx_jmp_r(uint8 reg) {
    ASSUME_REG(8, 13);
    R15 = GSU.avReg[reg];
    CLRFLAGS;
    
    DEFEAT_TAIL_MERGE;
}

/* 98-9d(ALT1) - ljmp rn - set program bank to source register and jump to address of register */
static inline void fx_ljmp_r(uint8 reg) {
    ASSUME_REG(8, 13);
    GSU.vPrgBankReg = GSU.avReg[reg] & 0x7f;
    GSU.pvPrgBank = GSU.apvRomBank[GSU.vPrgBankReg];
    R15 = SREG;
    GSU.vCacheBaseReg |= 1; // Mark cache as inactive. Required to get fx_cache to behave.
    fx_cache(0);
    R15--;
    
    DEFEAT_TAIL_MERGE;
}

/* 9e - lob - set upper byte to zero (keep low byte) */
static inline void fx_lob(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
}

/* 9f - fmult - 16 bit to 32 bit signed multiplication, upper 16 bits only */
static inline void fx_fmult(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
}

/* 9f(ALT1) - lmult - 16 bit to 32 bit signed multiplication */
static inline void fx_lmult(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
}

static inline void fx_ibt_r14(uint8 unused) {
    fx_ibt_r(14);
    READR14;
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
}

static inline void fx_lms_r14(uint8 unused) {
    fx_lms_r(14);
    READR14;
    
    DEFEAT_TAIL_MERGE;
}

/* a0-af(ALT2) - sms (yy),rn - store word in RAM (short address) */
/* If rn == r15, is the value of r15 before or after the extra byte is read? */
static inline void fx_sms_r(uint8 reg) {
    ASSUME_REG(0, 15);
    uint16 v = GSU.avReg[reg];
    uint16 lastRamAdr = GSU.vLastRamAdr = PIPE << 1;
    R15++;
    FETCHPIPE;
    RAM(lastRamAdr) = (uint8)v;
    RAM(lastRamAdr+1) = (uint8)(v>>8);
    CLRFLAGS;
    R15++;
    
    DEFEAT_TAIL_MERGE;
}

/* b0-bf - from rn - set source register */
/* b0-bf(B) - moves rn - move register to register, and set flags, (if B flag is set) */
static inline void fx_from_r(uint8 reg) {
    ASSUME_REG(0, 15);
    if(UNLIKELY(TF(B))) {
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
    
    DEFEAT_TAIL_MERGE;
}

/* c0 - hib - move high-byte to low-byte */
static inline void fx_hib(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
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
          "r" (GSU.avReg[reg] | (GSU.avReg[reg] << 16))
        : "cc"
    );

    R15++;
    DREG = v;
    TESTR14;
    CLRFLAGS;
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
}

static inline void fx_inc_r14(uint8 unused) {
    fx_inc_r(14);
    READR14;
    
    DEFEAT_TAIL_MERGE;
}

/* df - getc - transfer ROM buffer to color register */
static inline void fx_getc(uint8 unused)
{
#ifndef FX_DO_ROMBUFFER
    uint8 c = ROM(R14);
#else
    uint8 c = GSU.vRomBuffer;
#endif
    if(GSU.vPlotOptionReg & PLOT_HIGHNIBBLE)
        c = (c & 0xf0) | (c >> 4);

    if(GSU.vPlotOptionReg & PLOT_FREEZEHIGH)
    {
        GSU.vColorReg &= 0xf0;
        GSU.vColorReg |= c & 0x0f;
    }
    else
        GSU.vColorReg = USEX8(c);

    CLRFLAGS;
    R15++;
    
    DEFEAT_TAIL_MERGE;
}

/* df(ALT2) - ramb - set current RAM bank */
static inline void fx_ramb(uint8 unused)
{
    GSU.vRamBankReg = SREG & (FX_RAM_BANKS-1);
    GSU.pvRamBank = GSU.apvRamBank[GSU.vRamBankReg & (FX_RAM_BANKS-1)];
    CLRFLAGS;
    R15++;
    
    DEFEAT_TAIL_MERGE;
}

/* df(ALT3) - romb - set current ROM bank */
static inline void fx_romb(uint8 unused)
{
    GSU.vRomBankReg = USEX8(SREG) & 0x7f;
    GSU.pvRomBank = GSU.apvRomBank[GSU.vRomBankReg];
    CLRFLAGS;
    R15++;
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
}

static inline void fx_dec_r14(uint8 unused) {
    fx_dec_r(14);
    READR14;
    
    DEFEAT_TAIL_MERGE;
}

/* ef - getb - get byte from ROM at address R14 */
static inline void fx_getb(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
}

/* ef(ALT1) - getbh - get high-byte from ROM at address R14 */
static inline void fx_getbh(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
}

/* ef(ALT2) - getbl - get low-byte from ROM at address R14 */
static inline void fx_getbl(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
}

/* ef(ALT3) - getbs - get sign extended byte from ROM at address R14 */
static inline void fx_getbs(uint8 unused)
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
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
}

static inline void fx_iwt_r14(uint8 unused) {
    fx_iwt_r(14);
    READR14;
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
}

static inline void fx_lm_r14(uint8 unused) {
    fx_lm_r(14);
    READR14;
    
    DEFEAT_TAIL_MERGE;
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
    
    DEFEAT_TAIL_MERGE;
}

/*** GSU execution functions ***/

#define HANDLER(func_)     \
    handle_ ## func_: {    \
        func_(vLow);       \
        continue;          \
    }

#define HANDLER_PTR(func_) &&handle_ ## func_

void fx_run(uint32 nInstructions)
{
    PUSH_RESERVED;
    fx_load_reserved();
    
    static void* opcode_goto_table[0x400] = {
        #include "fxinst_opcode_handler_mappings.inc.c"
    };

    // Obj should never be called
    static void* const plot_rpix_handler_table[][2] = {
        {HANDLER_PTR(fx_plot_2bit), HANDLER_PTR(fx_rpix_2bit)}, // 0
        {HANDLER_PTR(fx_plot_4bit), HANDLER_PTR(fx_rpix_4bit)}, // 1
        {HANDLER_PTR(fx_plot_4bit), HANDLER_PTR(fx_rpix_4bit)}, // 2
        {HANDLER_PTR(fx_plot_8bit), HANDLER_PTR(fx_rpix_8bit)}, // 3
    };

    // Update the goto table with the correct plot/rpix handlers
    uint8 vMode = GSU.vMode;
    if (vMode >= ARRAY_COUNT(plot_rpix_handler_table)) vMode = 0;
    opcode_goto_table[0x04c] = opcode_goto_table[0x24c] = plot_rpix_handler_table[vMode][0],
    opcode_goto_table[0x14c] = opcode_goto_table[0x34c] = plot_rpix_handler_table[vMode][1];

    uint32 vCounter = nInstructions;
    READR14;
    while(LIKELY(vCounter-- > 0))
    {
        uint16 vOpcode = PIPE | (SFR & (FLG_ALT1 | FLG_ALT2));
        uint8 vLow = vOpcode & 0xf;
        FETCHPIPE;
        goto *opcode_goto_table[vOpcode];

        handle_fx_stop:
            fx_stop(vLow);
            goto loop_end;

        #include "fxinst_opcode_handlers.inc.c"
    }

    loop_end:

#if T3DS_COUNT_INSTRUCTIONS == 1
    t3dsCountN(&t3dsMain, Snx_GsuInstructions, nInstructions - vCounter);
#endif

    fx_save_reserved();
    POP_RESERVED;
}
