@ "FX_" offset defines
#include "fxinst_asm.h"

#ifndef SPEEDHACK_ENABLED
#define SPEEDHACK_DISABLED
#endif

#define vLow   r0
#define rR15   r3
#define rGSU   r4
#define rVCNT  r5
#define rSTAT  r6
#define rARM   r7
#define rSREG  r8
#define rDREG  r9
#define rPIPE  r10
#define rGOTO  fp
#define rPRG   ip

@ Constants for a wacky linker optimization. See link.ld for more info.
@ THIS MUST BE KEPT UP-TO-DATE WITH THE CONSTANT IN LINK.LD
#define R14_PTR #0x00A00000

@ R0 contains vLow
@ R1 contains the dispatch branch destination after interpreter
@ R2 contains extended opcode after interpreter
@ R3 contains GSU R15 after interpreter
@ R4 contains the pointer to GSU
@ R5 contains vCounter, the instruction counter
@ R6 contains the GSU STAT register
@ R7 contains the ARM flag register, NZCV flags synchronized with GSU
@ R8 contains the GSU PIPE register
@ R9 contains a pointer to the GSU SREG
@ R10 contains a pointer to the GSU DREG
@ FP contains a pointer to the instruction dispatch table
@ IP contains GSU.pvPrgBank, for fetching PIPE
@ LR is reserved and must be preserved

@ ----- Preferred regalloc order -----
@ R1, vLow
@ rSREG, rDREG (if overwritten later)
@ R2 (be careful with store locks affecting dispatch)
@ rARM (if overwritten later)
@ R1, rR15 (reload necessary if modified)

@ Dynarec note: TESTR14 branch prediction is unsolvable in dynarec because execution must flow forward.
@ We can emit based on the prior instruction setting R14 or not, and fall back to interpreter for things
@ like jumps and resuming a session. This should have pretty substantial savings.

    .section .text.fx_run_asm,"ax",%progbits
    .align    5
    .global fx_run_asm
    .syntax unified
    .arm
    .type fx_run_asm, %function
    .cfi_startproc

@ Data table for fx_run_asm
.L242:
        .word   GSU + GSU_STRUCT_PTR_OFFSET
        .word   opcode_goto_table
        .word   plot_rpix_handler_table

fx_run_asm:
        push    {r0, rGSU, rVCNT, rSTAT, rARM, rSREG, rDREG, rPIPE, rGOTO, lr}
        ldr     rGSU, .L242                              @ Load GSU pointer
        ldr     rGOTO, .L242+4                           @ Load GOTO table
#ifdef SPEEDHACK_DISABLED
        mov     rVCNT, r0                                @ Move vCounter to correct variable
#endif
        ldrh    rPRG, [rGSU, #FX_R14]                    @ READR14: Load R14
        ldr     vLow, [rGSU, #FX_pvRomBank]              @ READR14: Load ROM base pointer
        ldrb    rSREG, [rGSU, #FX_pvSreg]                @ Load reserved regs
        ldrb    rDREG, [rGSU, #FX_pvDreg]                @  |
        ldrh    rSTAT, [rGSU, #FX_vStatusReg]            @  |
        ldrb    rPRG, [vLow, rPRG]                       @ READR14: Load ROM(R14)
        ldrb    rARM, [rGSU, #FX_armFlags]               @  |
        ldrb    rPIPE, [rGSU, #FX_vPipe]                 @  |
        strb    rPRG, [rGSU, #FX_vRomBuffer]             @ READR14: Store to ROMBUFFER
        add     rSREG, rGSU, rSREG, lsl #1               @  |
        lsl     rARM, rARM, #24                          @  |
        add     rDREG, rGSU, rDREG, lsl #1               @  V
        ldr     rPRG, [rGSU, #FX_pvPrgBank]              @ FETCHPIPE: Load GSU.pvPrgBank. Taken from dispatch to save a cycle.

select_plots_and_dispatch_flags:
        ldrb    rR15, [rGSU, #FX_vMode]                  @ Load GSU.vMode
        ldrb    vLow, [rGSU, #FX_vPlotOptionReg]         @ Load GSU.vPlotOptionReg
select_plots_and_dispatch_flags.skip_2:
        ldr     r1, .L242+8                              @ Load plot/rpix table
        cmp     rR15, #3                                 @ If plot mode is 8-bit, replace DITHER with FREEZEHIGH
        strb    vLow, [rGSU, #FX_vPrevPlotOptionReg]     @ Store prevPlotOptionReg
        andeq   r2, vLow, #9                             @ 8bit: keep FREEZEHIGH and TRANSPARENT
        andne   r2, vLow, #3                             @ Else: keep DITHER and TRANSPARENT
        tst     r2, #8                                   @ If FREEZEHIGH is set, OR it to DITHER
        orrne   r2, r2, #2                               @  |
        and     r2, r2, #3                               @ Remove unused bit
        orr     rR15, r2, rR15, lsl #2                   @ Combine both, vMode in high bits
        add     rR15, r1, rR15, lsl #3                   @ Compute target address
        ldm     rR15, {vLow, r1}                         @ Load plot and rpix from the table
        str     vLow, [rGOTO, #304]                      @ Populate GOTO table
        str     r1, [rGOTO, #1328]                       @  |
        str     vLow, [rGOTO, #2352]                     @  |
        str     r1, [rGOTO, #3376]                       @  V

#ifdef SPEEDHACK_ENABLED
@ Dispatch for after instructions that do not run CLRFLAGS
dispatch_flags:
        ldrh    rR15, [rGSU, #FX_R15]                    @ FETCHPIPE: Load R15
        and     r2, rSTAT, #768                          @ Get opcode mode bits
        orr     r2, rPIPE, r2                            @ Compute opcode
        ldr     r1, [rGOTO, r2, lsl #2]                  @ Load destination handler
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler

@ Dispatch for after instructions that run CLRFLAGS
dispatch:
        ldrh    rR15, [rGSU, #FX_R15]                    @ FETCHPIPE: Load R15
dispatch.skip_1:                                         @ If used, be careful to ensure that R15 is still 16-bit
        ldr     r1, [rGOTO, rPIPE, lsl #2]               @ Load destination handler
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler

#else

dispatch_flags:
        ldrh    rR15, [rGSU, #FX_R15]                    @ FETCHPIPE: Load R15
dispatch_flags.skip_1:                                   @ If used, be careful to ensure that R15 is still 16-bit
        and     r2, rSTAT, #768                          @ Get opcode mode bits
        orr     r2, rPIPE, r2                            @ Compute opcode
        subs    rVCNT, rVCNT, #1                         @ Decrement vCounter and exit if 0
        ldr     r1, [rGOTO, r2, lsl #2]                  @ Load destination handler
        beq     loop_end                                 @ 
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler

@ Dispatch for after instructions that run CLRFLAGS
dispatch:
        ldrh    rR15, [rGSU, #FX_R15]                    @ FETCHPIPE: Load R15
dispatch.skip_1:                                         @ If used, be careful to ensure that R15 is still 16-bit
        ldr     r1, [rGOTO, rPIPE, lsl #2]               @ Load destination handler
        subs    rVCNT, rVCNT, #1                         @ Decrement vCounter and exit if 0
        beq     loop_end                                 @ 
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler
#endif

@ GETBS: get sign extended byte from ROM at address R14
handle_fx_getbs:
        ldrsb   r1, [rGSU, #FX_vRomBuffer]               @ R15 = SEX8(ROMBUFFER)
        add     rR15, rR15, #1                           @ R15++
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ STOP: stop GSU execution
handle_fx_stop:
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldr     r2, [rGSU, #FX_pvRegisters]              @ Load pointer to pvRegisters
        mov     rR15, #0                                 @ plotOptionReg = 0
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        bic     rSTAT, rSTAT, #32                        @ CF(G)
        ldrsb   r2, [r2, #55]                            @ Load CFGR
        mov     rPIPE, #1                                @ PIPE = 1
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        cmp     r2, #0                                   @ If (GSU_CFGR & 0x80) == 0, Raise IRQ
        orrge   rSTAT, rSTAT, #32768                     @  |
        bic     rSTAT, rSTAT, #4864                      @  |
        strb    rR15, [rGSU, #FX_vPlotOptionReg]         @ Store plotOptionReg
        b       loop_end                                 @ 

@ PLOT 2BIT: Draws a pixel at R1,R2 (X,Y), using GSU.vColorReg as the source
#include "plot_2bit.s"
#include "plot_2bit_t.s"
#include "plot_2bit_d.s"
#include "plot_2bit_dt.s"

@ RPIX 2BIT: Reads the color of pixel R1,R2 (X, Y) and stores to DREG.
handle_fx_rpix_2bit:
        ldrh    r1, [rGSU, #FX_vScreenHeight]            @ R1 = screen height
        ldrb    r2, [rGSU, #FX_R2]                       @ R2 = Y
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldrb    vLow, [rGSU, #FX_R1]                     @ vLow = X
        cmp     r2, r1                                   @ Test Y > screen height
        mov     rSREG, #128                              @ Mask = BIT(7) >> (X & 7)
        
        @ vLow is X, R2 is Y
        lsr     rR15, vLow, #3                           @ DREG = GSU.x[X >> 3]
        bcs     handle_fx_rpix_8bit.return               @ If Y > screen height, return
        add     r2, rGSU, r2, lsl #2                     @ R2 = GSU.apvScreen[Y]
        add     rR15, rGSU, rR15, lsl #2                 @ DREG
        ldr     r2, [r2, #FX_apvScreen]                  @ R2
        ldr     rR15, [rR15, #FX_x]                      @ DREG
        and     r1, vLow, #7                             @ Mask
        lsr     rSREG, rSREG, r1                         @ Mask
        add     vLow, rR15, r2                           @ Pixel 0 pointer
        mov     rR15, #0                                 @ Initial result

        @ vLow is pixel 0 Pointer, rSREG is the pixel mask
        ldrh    r1, [vLow, #0]                           @ Load pixel pair 1
        tst     r1, rSREG                                @ Pixel pair 1
        orrne   rR15, rR15, #1                           @  |
        tst     r1, rSREG, lsl #8                        @  |
        orrne   rR15, rR15, #2                           @  |

        strh    rR15, [rDREG]                            @ Store result
        b handle_fx_rpix_8bit.return

@ PLOT 4BIT: Draws a pixel at R1,R2 (X,Y), using GSU.vColorReg as the source
#include "plot_4bit.s"
#include "plot_4bit_t.s"
#include "plot_4bit_d.s"
#include "plot_4bit_dt.s"

@ RPIX 4BIT: Reads the color of pixel R1,R2 (X, Y) and stores to DREG.
handle_fx_rpix_4bit:
        ldrh    r1, [rGSU, #FX_vScreenHeight]            @ R1 = screen height
        ldrb    r2, [rGSU, #FX_R2]                       @ R2 = Y
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldrb    vLow, [rGSU, #FX_R1]                     @ vLow = X
        cmp     r2, r1                                   @ Test Y > screen height
        mov     rSREG, #128                              @ Mask = BIT(7) >> (X & 7)
        
        @ vLow is X, R2 is Y
        lsr     rR15, vLow, #3                           @ DREG = GSU.x[X >> 3]
        bcs     handle_fx_rpix_8bit.return               @ If Y > screen height, return
        add     r2, rGSU, r2, lsl #2                     @ R2 = GSU.apvScreen[Y]
        add     rR15, rGSU, rR15, lsl #2                 @ DREG
        ldr     r2, [r2, #FX_apvScreen]                  @ R2
        ldr     rR15, [rR15, #FX_x]                      @ DREG
        and     r1, vLow, #7                             @ Mask
        lsr     rSREG, rSREG, r1                         @ Mask
        add     vLow, rR15, r2                           @ Pixel 0 pointer
        mov     rR15, #0                                 @ Initial result

        @ r2 is pixel 0 Pointer, IP is the pixel mask
        ldrh    r1, [vLow, #0]                           @ Load pixel pair 1
        ldrh    r2, [vLow, #16]                          @ Load pixel pair 2

        tst     r1, rSREG                                @ Pixel pair 1
        orrne   rR15, rR15, #1                           @  |
        tst     r1, rSREG, lsl #8                        @  |
        orrne   rR15, rR15, #2                           @  |

        tst     r2, rSREG                                @ Pixel pair 2
        orrne   rR15, rR15, #4                           @  |
        tst     r2, rSREG, lsl #8                        @  |
        orrne   rR15, rR15, #8                           @  |

        strh    rR15, [rDREG]                            @ Store result
        b handle_fx_rpix_8bit.return

@ PLOT 8BIT: Draws a pixel at R1,R2 (X,Y), using GSU.vColorReg as the source
#include "plot_8bit.s"
#include "plot_8bit_f.s"
#include "plot_8bit_t.s"

@ RPIX 8BIT: Reads the color of pixel R1,R2 (X, Y) and stores to DREG.
handle_fx_rpix_8bit:
        ldrh    r1, [rGSU, #FX_vScreenHeight]            @ R1 = screen height
        ldrb    r2, [rGSU, #FX_R2]                       @ R2 = Y
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldrb    vLow, [rGSU, #FX_R1]                     @ vLow = X
        cmp     r2, r1                                   @ Test Y > screen height
        mov     rSREG, #128                              @ Mask = BIT(7) >> (X & 7)
        
        @ vLow is X, R2 is Y
        lsr     rR15, vLow, #3                           @ DREG = GSU.x[X >> 3]
        bcs     handle_fx_rpix_8bit.return               @ If Y > screen height, return
        add     r2, rGSU, r2, lsl #2                     @ R2 = GSU.apvScreen[Y]
        add     rR15, rGSU, rR15, lsl #2                 @ DREG
        ldr     r2, [r2, #FX_apvScreen]                  @ R2
        ldr     rR15, [rR15, #FX_x]                      @ DREG
        and     r1, vLow, #7                             @ Mask
        lsr     rSREG, rSREG, r1                         @ Mask
        add     vLow, rR15, r2                           @ Pixel 0 pointer
        mov     rR15, #0                                 @ Initial result

        @ r2 is pixel 0 Pointer, IP is the pixel mask
        bic     rARM, rARM, #1073741824                  @  |
        ldrh    r1, [vLow, #0]                           @ Load pixel pair 1
        ldrh    r2, [vLow, #16]                          @ Load pixel pair 2

        tst     r1, rSREG                                @ Pixel pair 1
        orrne   rR15, rR15, #1                           @  |
        tst     r1, rSREG, lsl #8                        @  |
        orrne   rR15, rR15, #2                           @  |

        ldrh    r1, [vLow, #32]                          @ Load pixel pair 3
        tst     r2, rSREG                                @ Pixel pair 2
        orrne   rR15, rR15, #4                           @  |
        tst     r2, rSREG, lsl #8                        @  |
        orrne   rR15, rR15, #8                           @  |

        ldrh    r2, [vLow, #48]                          @ Load pixel pair 4
        tst     r1, rSREG                                @ Pixel pair 3
        orrne   rR15, rR15, #16                          @  |
        tst     r1, rSREG, lsl #8                        @  |
        orrne   rR15, rR15, #32                          @  |

        tst     r2, rSREG                                @ Pixel pair 4
        orrne   rR15, rR15, #64                          @  |
        tst     r2, rSREG, lsl #8                        @  |
        orrne   rR15, rR15, #128                         @  |

        strh    rR15, [rDREG]                            @ Store result
        cmp     rR15, #0                                 @ Update ARM Z flag
        orreq   rARM, rARM, #1073741824                  @  |

handle_fx_rpix_8bit.return:
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ NOP: Clears flags and advances R15
handle_fx_nop:
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ CACHE: reintialize GSU cache
handle_fx_cache:
        ldrh    r1, [rGSU, #FX_vCacheBaseReg]            @ r1 = GSU.vCacheBaseReg
        bic     r2, rR15, #15                            @ r2 = R15 & 0xfff0
        add     rR15, rR15, #1                           @ R15++
        cmp     r1, r2                                   @ If cache base is incorrect or if cache is disabled, reload
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        beq     dispatch.skip_1                          @ 
        
        @ Reload cache
        strh    r2, [rGSU, #FX_vCacheBaseReg]            @ GSU.vCacheBaseReg = R15 & 0xfff0
        mov     r1, #0                                   @ 
        str     r1, [rGSU, #FX_vCacheFlags]              @ GSU.vCacheFlags = 0
        b       dispatch                                 @ 

@ LSR: logical shift right
handle_fx_lsr:
        ldrh    r1, [rSREG]                              @ Load SREG
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        lsrs    r1, r1, #1                               @ Do the rightshift
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ ROL: rotate left
handle_fx_rol:
        ldrh    r1, [rSREG]                              @ Load SREG
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        lsl     r1, r1, #16                              @ Shift value into upper half of reg
        orrcs   r1, r1, #32768                           @ If carry is set, set bit 15
        lsls    r1, r1, #1                               @ Shift left 1 to set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        lsr     r1, r1, #16                              @ Shift down from top half of register
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

#ifdef SPEEDHACK_ENABLED
@ BRA: unconditional branch
handle_fx_bra:
        add     rR15, rR15, #1                           @ R15++
        uxth    r2, rR15                                 @ Wrap R15 at 16 bits
        sxtab16 rR15, rR15, rPIPE                        @ Add PIPE to R15
        ldrb    rPIPE, [rPRG, r2]                        @ FETCHPIPE
        and     r2, rSTAT, #768                          @ Get opcode mode bits
        orr     r2, rPIPE, r2                            @ Compute opcode
        strh    rR15, [rGSU, #FX_R15]                    @ Store destination to R15
        ldr     r1, [rGOTO, r2, lsl #2]                  @ Load destination handler
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler

@ BGE: branch if greater or equal
handle_fx_bge:
        mov        r1, #1                                @ Needed for SXTAB
        uadd16     rR15, rR15, r1                        @ R15++
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        ldrb       vLow, [rPRG, rR15]                    @ FETCHPIPE
        uadd16lt   rR15, rR15, r1                        @ R15++ if not taken
        and        r1, rSTAT, #768                       @ Get opcode mode bits
        orr        r1, vLow, r1                          @ Compute opcode
        sxtab16ge  rR15, rR15, rPIPE                     @ Handle branch
        ldr        r1, [rGOTO, r1, lsl #2]               @ Load destination handler
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        and        vLow, vLow, #15                       @ Compute vLow
        ldrb       rPIPE, [rPRG, rR15]                   @ FETCHPIPE
        bx         r1                                    @ Branch to handler

@ BLT: branch if less than
handle_fx_blt:
        mov        r1, #1                                @ Needed for SXTAB
        uadd16     rR15, rR15, r1                        @ R15++
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        ldrb       vLow, [rPRG, rR15]                    @ FETCHPIPE
        uadd16ge   rR15, rR15, r1                        @ R15++ if not taken
        and        r1, rSTAT, #768                       @ Get opcode mode bits
        orr        r1, vLow, r1                          @ Compute opcode
        sxtab16lt  rR15, rR15, rPIPE                     @ Handle branch
        ldr        r1, [rGOTO, r1, lsl #2]               @ Load destination handler
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        and        vLow, vLow, #15                       @ Compute vLow
        ldrb       rPIPE, [rPRG, rR15]                   @ FETCHPIPE
        bx         r1                                    @ Branch to handler

@ BNE: branch if not equal
handle_fx_bne:
        mov        r1, #1                                @ Needed for SXTAB
        uadd16     rR15, rR15, r1                        @ R15++
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        ldrb       vLow, [rPRG, rR15]                    @ FETCHPIPE
        uadd16eq   rR15, rR15, r1                        @ R15++ if not taken
        and        r1, rSTAT, #768                       @ Get opcode mode bits
        orr        r1, vLow, r1                          @ Compute opcode
        sxtab16ne  rR15, rR15, rPIPE                     @ Handle branch
        ldr        r1, [rGOTO, r1, lsl #2]               @ Load destination handler
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        and        vLow, vLow, #15                       @ Compute vLow
        ldrb       rPIPE, [rPRG, rR15]                   @ FETCHPIPE
        bx         r1                                    @ Branch to handler

@ BEQ: branch if equal
handle_fx_beq:
        mov        r1, #1                                @ Needed for SXTAB
        uadd16     rR15, rR15, r1                        @ R15++
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        ldrb       vLow, [rPRG, rR15]                    @ FETCHPIPE
        uadd16ne   rR15, rR15, r1                        @ R15++ if not taken
        and        r1, rSTAT, #768                       @ Get opcode mode bits
        orr        r1, vLow, r1                          @ Compute opcode
        sxtab16eq  rR15, rR15, rPIPE                     @ Handle branch
        ldr        r1, [rGOTO, r1, lsl #2]               @ Load destination handler
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        and        vLow, vLow, #15                       @ Compute vLow
        ldrb       rPIPE, [rPRG, rR15]                   @ FETCHPIPE
        bx         r1                                    @ Branch to handler

@ BPL: branch if positive or zero
handle_fx_bpl:
        mov        r1, #1                                @ Needed for SXTAB
        uadd16     rR15, rR15, r1                        @ R15++
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        ldrb       vLow, [rPRG, rR15]                    @ FETCHPIPE
        uadd16mi   rR15, rR15, r1                        @ R15++ if not taken
        and        r1, rSTAT, #768                       @ Get opcode mode bits
        orr        r1, vLow, r1                          @ Compute opcode
        sxtab16pl  rR15, rR15, rPIPE                     @ Handle branch
        ldr        r1, [rGOTO, r1, lsl #2]               @ Load destination handler
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        and        vLow, vLow, #15                       @ Compute vLow
        ldrb       rPIPE, [rPRG, rR15]                   @ FETCHPIPE
        bx         r1                                    @ Branch to handler

@ BMI: branch if negative
handle_fx_bmi:
        mov        r1, #1                                @ Needed for SXTAB
        uadd16     rR15, rR15, r1                        @ R15++
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        ldrb       vLow, [rPRG, rR15]                    @ FETCHPIPE
        uadd16pl   rR15, rR15, r1                        @ R15++ if not taken
        and        r1, rSTAT, #768                       @ Get opcode mode bits
        orr        r1, vLow, r1                          @ Compute opcode
        sxtab16mi  rR15, rR15, rPIPE                     @ Handle branch
        ldr        r1, [rGOTO, r1, lsl #2]               @ Load destination handler
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        and        vLow, vLow, #15                       @ Compute vLow
        ldrb       rPIPE, [rPRG, rR15]                   @ FETCHPIPE
        bx         r1                                    @ Branch to handler

@ BCC: branch if lower (unsigned <)
handle_fx_bcc:
        mov        r1, #1                                @ Needed for SXTAB
        uadd16     rR15, rR15, r1                        @ R15++
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        ldrb       vLow, [rPRG, rR15]                    @ FETCHPIPE
        uadd16cs   rR15, rR15, r1                        @ R15++ if not taken
        and        r1, rSTAT, #768                       @ Get opcode mode bits
        orr        r1, vLow, r1                          @ Compute opcode
        sxtab16cc  rR15, rR15, rPIPE                     @ Handle branch
        ldr        r1, [rGOTO, r1, lsl #2]               @ Load destination handler
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        and        vLow, vLow, #15                       @ Compute vLow
        ldrb       rPIPE, [rPRG, rR15]                   @ FETCHPIPE
        bx         r1                                    @ Branch to handler

@ BCS: branch if higher or same (unsigned >=)
handle_fx_bcs:
        mov        r1, #1                                @ Needed for SXTAB
        uadd16     rR15, rR15, r1                        @ R15++
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        ldrb       vLow, [rPRG, rR15]                    @ FETCHPIPE
        uadd16cc   rR15, rR15, r1                        @ R15++ if not taken
        and        r1, rSTAT, #768                       @ Get opcode mode bits
        orr        r1, vLow, r1                          @ Compute opcode
        sxtab16cs  rR15, rR15, rPIPE                     @ Handle branch
        ldr        r1, [rGOTO, r1, lsl #2]               @ Load destination handler
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        and        vLow, vLow, #15                       @ Compute vLow
        ldrb       rPIPE, [rPRG, rR15]                   @ FETCHPIPE
        bx         r1                                    @ Branch to handler

@ BVC: branch if no overflow
handle_fx_bvc:
        mov        r1, #1                                @ Needed for SXTAB
        uadd16     rR15, rR15, r1                        @ R15++
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        ldrb       vLow, [rPRG, rR15]                    @ FETCHPIPE
        uadd16vs   rR15, rR15, r1                        @ R15++ if not taken
        and        r1, rSTAT, #768                       @ Get opcode mode bits
        orr        r1, vLow, r1                          @ Compute opcode
        sxtab16vc  rR15, rR15, rPIPE                     @ Handle branch
        ldr        r1, [rGOTO, r1, lsl #2]               @ Load destination handler
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        and        vLow, vLow, #15                       @ Compute vLow
        ldrb       rPIPE, [rPRG, rR15]                   @ FETCHPIPE
        bx         r1                                    @ Branch to handler

@ BVS: branch if overflow
handle_fx_bvs:
        mov        r1, #1                                @ Needed for SXTAB
        uadd16     rR15, rR15, r1                        @ R15++
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        ldrb       vLow, [rPRG, rR15]                    @ FETCHPIPE
        uadd16vc   rR15, rR15, r1                        @ R15++ if not taken
        and        r1, rSTAT, #768                       @ Get opcode mode bits
        orr        r1, vLow, r1                          @ Compute opcode
        sxtab16vs  rR15, rR15, rPIPE                     @ Handle branch
        ldr        r1, [rGOTO, r1, lsl #2]               @ Load destination handler
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        and        vLow, vLow, #15                       @ Compute vLow
        ldrb       rPIPE, [rPRG, rR15]                   @ FETCHPIPE
        bx         r1                                    @ Branch to handler
#else
handle_fx_bra:
        add     rR15, rR15, #1                           @ R15++
        uxth    r2, rR15                                 @ Wrap R15 at 16 bits
        sxtab16 rR15, rR15, rPIPE                        @ Add PIPE to R15
        ldrb    rPIPE, [rPRG, r2]                        @ FETCHPIPE
        strh    rR15, [rGSU, #FX_R15]                    @ Store destination to R15
        b       dispatch_flags.skip_1                    @ 

@ BGE: branch if greater or equal
handle_fx_bge:
        mov        vLow, #1                              @ Needed for SXTAB
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        uadd16     r2, rR15, vLow                        @ R15++
        sxtab16ge  rR15, r2, rPIPE                       @ Handle branch
        uadd16lt   rR15, r2, vLow                        @ 
        ldrb       rPIPE, [rPRG, r2]                     @ FETCHPIPE
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        b          dispatch_flags.skip_1                 @ 

@ BLT: branch if less than
handle_fx_blt:
        mov        vLow, #1                              @ Needed for SXTAB
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        uadd16     r2, rR15, vLow                        @ R15++
        sxtab16lt  rR15, r2, rPIPE                       @ Handle branch
        uadd16ge   rR15, r2, vLow                        @ 
        ldrb       rPIPE, [rPRG, r2]                     @ FETCHPIPE
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        b          dispatch_flags.skip_1                 @ 

@ BNE: branch if not equal
handle_fx_bne:
        mov        vLow, #1                              @ Needed for SXTAB
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        uadd16     r2, rR15, vLow                        @ R15++
        sxtab16ne  rR15, r2, rPIPE                       @ Handle branch
        uadd16eq   rR15, r2, vLow                        @ 
        ldrb       rPIPE, [rPRG, r2]                     @ FETCHPIPE
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        b          dispatch_flags.skip_1                 @ 

@ BEQ: branch if equal
handle_fx_beq:
        mov        vLow, #1                              @ Needed for SXTAB
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        uadd16     r2, rR15, vLow                        @ R15++
        sxtab16eq  rR15, r2, rPIPE                       @ Handle branch
        uadd16ne   rR15, r2, vLow                        @ 
        ldrb       rPIPE, [rPRG, r2]                     @ FETCHPIPE
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        b          dispatch_flags.skip_1                 @ 

@ BPL: branch if positive or zero
handle_fx_bpl:
        mov        vLow, #1                              @ Needed for SXTAB
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        uadd16     r2, rR15, vLow                        @ R15++
        sxtab16pl  rR15, r2, rPIPE                       @ Handle branch
        uadd16mi   rR15, r2, vLow                        @ 
        ldrb       rPIPE, [rPRG, r2]                     @ FETCHPIPE
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        b          dispatch_flags.skip_1                 @ 

@ BMI: branch if negative
handle_fx_bmi:
        mov        vLow, #1                              @ Needed for SXTAB
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        uadd16     r2, rR15, vLow                        @ R15++
        sxtab16mi  rR15, r2, rPIPE                       @ Handle branch
        uadd16pl   rR15, r2, vLow                        @ 
        ldrb       rPIPE, [rPRG, r2]                     @ FETCHPIPE
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        b          dispatch_flags.skip_1                 @ 

@ BCC: branch if lower (unsigned <)
handle_fx_bcc:
        mov        vLow, #1                              @ Needed for SXTAB
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        uadd16     r2, rR15, vLow                        @ R15++
        sxtab16cc  rR15, r2, rPIPE                       @ Handle branch
        uadd16cs   rR15, r2, vLow                        @ 
        ldrb       rPIPE, [rPRG, r2]                     @ FETCHPIPE
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        b          dispatch_flags.skip_1                 @ 

@ BCS: branch if higher or same (unsigned >=)
handle_fx_bcs:
        mov        vLow, #1                              @ Needed for SXTAB
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        uadd16     r2, rR15, vLow                        @ R15++
        sxtab16cs  rR15, r2, rPIPE                       @ Handle branch
        uadd16cc   rR15, r2, vLow                        @ 
        ldrb       rPIPE, [rPRG, r2]                     @ FETCHPIPE
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        b          dispatch_flags.skip_1                 @ 

@ BVC: branch if no overflow
handle_fx_bvc:
        mov        vLow, #1                              @ Needed for SXTAB
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        uadd16     r2, rR15, vLow                        @ R15++
        sxtab16vc  rR15, r2, rPIPE                       @ Handle branch
        uadd16vs   rR15, r2, vLow                        @ 
        ldrb       rPIPE, [rPRG, r2]                     @ FETCHPIPE
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        b          dispatch_flags.skip_1                 @ 

@ BVS: branch if overflow
handle_fx_bvs:
        mov        vLow, #1                              @ Needed for SXTAB
        msr        cpsr_f, rARM                          @ Load flags into CPSR
        uadd16     r2, rR15, vLow                        @ R15++
        sxtab16vs  rR15, r2, rPIPE                       @ Handle branch
        uadd16vc   rR15, r2, vLow                        @ 
        ldrb       rPIPE, [rPRG, r2]                     @ FETCHPIPE
        strh       rR15, [rGSU, #FX_R15]                 @ Store R15
        b          dispatch_flags.skip_1                 @ 
#endif

@ TO: set register n as destination register
@ move one register to another (if B flag is set)
@ Cannot be called on R14 or R15.
handle_fx_to_r:
        tst     rSTAT, #4096                             @ Test B
        add     rR15, rR15, #1                           @ R15++
        beq     handle_fx_to_r.b_is_not_set              @ If B is not set, branch
@ B is set
        ldrh    r2, [rSREG]                              @ Load SREG
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        strh    r2, [rGSU, vLow]                         @ Register N = SREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 
handle_fx_to_r.b_is_not_set:
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15. vLow cannot be 15, so early store is valid
        add     rDREG, rGSU, vLow, lsl #1                @ DREG = vLow
#ifdef SPEEDHACK_ENABLED
        and     r2, rSTAT, #768                          @ Get opcode mode bits
        orr     r2, rPIPE, r2                            @ Compute opcode
        uxth    rR15, rR15                               @ Zero-extend R15
        ldr     r1, [rGOTO, r2, lsl #2]                  @ Load destination handler
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler
#else
        b       dispatch_flags                           @ 
#endif

@ TO_R14: set register 14 as destination register
@ If B flag is set, move SREG to R14, CLRFLAGS, and READR14 instead
handle_fx_to_r14:
        tst     rSTAT, #4096                             @ Test B
        add     rR15, rR15, #1                           @ R15++
        bne     handle_fx_to_r14.b_is_set                @ If B is set, branch
@ B is not set
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        add     rDREG, rGSU, #FX_R14                     @ DREG = pointer to R14
#ifdef SPEEDHACK_ENABLED
        and     r2, rSTAT, #768                          @ Get opcode mode bits
        orr     r2, rPIPE, r2                            @ Compute opcode
        uxth    rR15, rR15                               @ Zero-extend R15
        ldr     r1, [rGOTO, r2, lsl #2]                  @ Load destination handler
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler
#else
        b       dispatch_flags                           @ 
#endif
handle_fx_to_r14.b_is_set:
        ldrh    r2, [rSREG]                              @ Load SREG
        ldr     r1, [rGSU, #FX_pvRomBank]                @ READR14: Load GSU.pvRomBank
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        strh    r2, [rGSU, #FX_R14]                      @ R14 = SREG
        ldrb    vLow, [r1, r2]                           @ READR14: Load GSU.pvRomBank[R14]
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        strb    vLow, [rGSU, #FX_vRomBuffer]             @ READR14: Store ROMBUFFER
        b       dispatch                                 @ 

@ TO_R15: Set DREG to R15 and increment R15
@ If B flag is set, move SREG to R15 instead
handle_fx_to_r15:
        tst     rSTAT, #4096                             @ Test B
        beq     handle_fx_to_r15.b_is_not_set            @ If B is not set, branch
@ B is set
        ldrh    rR15, [rSREG]                            @ R15 = SREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        strh    rR15, [rGSU, #FX_R15]                    @ R15 = SREG
        b       dispatch.skip_1                          @ 
handle_fx_to_r15.b_is_not_set:
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        add     rDREG, rGSU, #FX_R15                     @ DREG = pointer to R15
#ifdef SPEEDHACK_ENABLED
        and     r2, rSTAT, #768                          @ Get opcode mode bits
        orr     r2, rPIPE, r2                            @ Compute opcode
        uxth    rR15, rR15                               @ Zero-extend R15
        ldr     r1, [rGOTO, r2, lsl #2]                  @ Load destination handler
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler
#else
        b       dispatch_flags                           @ 
#endif

@ WITH: set register n as source and destination register
handle_fx_with_r:
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        add     rDREG, rGSU, vLow, lsl #1                @ Calculate register
        mov     rSREG, rDREG                             @ Copy register to SREG
        orr     rSTAT, rSTAT, #4096                      @ Set flag B
#ifdef SPEEDHACK_ENABLED
        and     r2, rSTAT, #768                          @ Get opcode mode bits
        orr     r2, rPIPE, r2                            @ Compute opcode
        uxth    rR15, rR15                               @ Zero-extend R15
        ldr     r1, [rGOTO, r2, lsl #2]                  @ Load destination handler
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler
#else
        b       dispatch_flags                           @ 
#endif

@ STW: store word (16 bits)
handle_fx_stw_r:
        lsl     vLow, vLow, #1                           @ Double vLow for 16-bit offset
        ldrh    vLow, [rGSU, vLow]                       @ Load offset
        ldr     r1, [rGSU, #FX_pvRamBank]                @ Load RAM base pointer
        strh    vLow, [rGSU, #FX_vLastRamAdr]            @ Store offset to GSU.vLastRamAdr
        ldrh    r2, [rSREG]                              @ Load source data
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        strb    r2, [r1, vLow]                           @ Store bottom byte
        eor     vLow, vLow, #1                           @ Flip bottom bit of offset
        add     rR15, rR15, #1                           @ R15++
        lsr     r2, r2, #8                               @ Prep top byte
        strb    r2, [r1, vLow]                           @ Store top byte
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch                                 @ 

@ LOOP: decrement loop counter R12 and branch to R13 on not zero
handle_fx_loop:
        ldrh    r2, [rGSU, #FX_R12]                      @ Load counter
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        sub     r2, r2, #1                               @ Decrement counter
        strh    r2, [rGSU, #FX_R12]                      @ Store counter
        lsl     rARM, r2, #16                            @ Shift counter to top half of register and test flags
        movs    rARM, rARM                               @ Set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        beq     handle_fx_loop.loop_end                  @ 

        ldrh    rR15, [rGSU, #FX_R13]                    @ Load destination
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        b       dispatch.skip_1                          @ 
handle_fx_loop.loop_end:
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

#ifdef SPEEDHACK_ENABLED
@ ALT1: set ALT mode 1
handle_fx_alt1:
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        bic     rSTAT, rSTAT, #4096                      @ Clear B flag
        orr     rSTAT, rSTAT, #256                       @ Set ALT1 flag
        and     r2, rSTAT, #768                          @ Get opcode mode bits
        orr     r2, rPIPE, r2                            @ Compute opcode
        uxth    rR15, rR15                               @ Zero-Extend R15
        ldr     r1, [rGOTO, r2, lsl #2]                  @ Load destination handler
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler

@ ALT2: set ALT mode 2
handle_fx_alt2:
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        bic     rSTAT, rSTAT, #4096                      @ Clear B flag
        orr     rSTAT, rSTAT, #512                       @ Set ALT2 flag
        and     r2, rSTAT, #768                          @ Get opcode mode bits
        orr     r2, rPIPE, r2                            @ Compute opcode
        uxth    rR15, rR15                               @ Zero-Extend R15
        ldr     r1, [rGOTO, r2, lsl #2]                  @ Load destination handler
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler

@ ALT3: set ALT mode 3
handle_fx_alt3:
        add     r2, rR15, #1                             @ R15++
        strh    r2, [rGSU, #FX_R15]                      @ Store R15
        orr     r1, rPIPE, #768                          @ Compute opcode
        uxth    rR15, r2                                 @ Wrap R15 at 16 bits
        ldr     r1, [rGOTO, r1, lsl #2]                  @ Load destination handler
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bic     rSTAT, rSTAT, #4096                      @ Clear B flag
        orr     rSTAT, rSTAT, #768                       @ Set ALT1 + ALT2 flags
        bx      r1                                       @ Branch to handler
#else
@ ALT1: set ALT mode 1
handle_fx_alt1:
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        bic     rSTAT, rSTAT, #4096                      @ Clear B flag
        orr     rSTAT, rSTAT, #256                       @ Set ALT1 flag
        b       dispatch_flags                           @ 

@ ALT2: set ALT mode 2
handle_fx_alt2:
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        bic     rSTAT, rSTAT, #4096                      @ Clear B flag
        orr     rSTAT, rSTAT, #512                       @ Set ALT2 flag
        b       dispatch_flags                           @ 
        
@ ALT3: set ALT mode 3
handle_fx_alt3:
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        bic     rSTAT, rSTAT, #4096                      @ Clear B flag
        orr     rSTAT, rSTAT, #768                       @ Set ALT1 + ALT2 flags
        b       dispatch_flags                           @ 
#endif

@ LDW: load word
handle_fx_ldw_r:
        lsl     vLow, vLow, #1                           @ Double vLow for 16-bit offset
        add     rR15, rR15, #1                           @ R15++
        ldrh    vLow, [rGSU, vLow]                       @ Load RAM offset
        ldr     rSREG, [rGSU, #FX_pvRamBank]             @ Load RAM base pointer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        eor     r1, vLow, #1                             @ Flip bottom bit of offset, stored in a separate register
        strh    vLow, [rGSU, #FX_vLastRamAdr]            @ Store offset to GSU.vLastRamAdr
        ldrb    r1, [rSREG, r1]                          @ Load top byte
        ldrb    vLow, [rSREG, vLow]                      @ Load bottom byte
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        orr     vLow, vLow, r1, lsl #8                   @ Combine bytes into word
        strh    vLow, [rDREG]                            @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch                                 @ 

@ SWAP: swap low and high bytes of SREG, store in DREG
handle_fx_swap:
        ldrh    r1, [rSREG]                              @ Load value from SREG
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        rev16   r1, r1                                   @ Byteswap value
        lsl     rARM, r1, #16                            @ Shift for flag setting
        movs    rARM, rARM                               @ Set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: Branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ COLOR: copy SREG to color register
handle_fx_color:
        ldrb    r1, [rGSU, #FX_vPlotOptionReg]           @ Load plotOptionReg
        ldrb    r2, [rSREG]                              @ Load color from SREG
        add     rR15, rR15, #1                           @ R15++
        tst     r1, #4                                   @ If PLOT_HIGHNIBBLE, duplicate the high nibble of color to the low nibble
        andne   vLow, r2, #240                           @  |
        orrne   r2, vLow, r2, lsr #4                     @  V
        tst     r1, #8                                   @ If PLOT_FREEZEHIGH, only update the bottom nibble
        ldrbne  r1, [rGSU, #FX_vColorReg]                @  |
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        andne   r2, r2, #15                              @  |
        bicne   r1, r1, #15                              @  |
        orrne   r2, r1, r2                               @  V
        strb    r2, [rGSU, #FX_vColorReg]                @ Store result to GSU.vColorReg
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch                                 @ 

@ NOT: bitwise NOT of SREG, store in DREG
handle_fx_not:
        ldrh    r1, [rSREG]                              @ Load value from SREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        orr     r1, r1, r1, lsl #16                      @ Duplicate value into both halves of a register
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        mvns    r1, r1                                   @ Negate value and set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch                                 @ 

@ ADD: SREG + register n, store in DREG
handle_fx_add_r:
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        ldrh    r1, [rSREG]                              @ Load value 1 from SREG
        ldrh    r2, [rGSU, vLow]                         @ Load value 2 from register N
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        lsl     r1, r1, #16                              @ Shift value 1 to the top half of its register
        adds    r1, r1, r2, lsl #16                      @ Add both values. Overwrites all flags.
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        lsr     r1, r1, #16                              @ Shift result down from top half of register
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ SUB: SREG - register n, store in DREG
handle_fx_sub_r:
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        ldrh    r1, [rSREG]                              @ Load value 1 from SREG
        ldrh    r2, [rGSU, vLow]                         @ Load value 2 from register N
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        lsl     r1, r1, #16                              @ Shift value 1 to the top half of its register
        subs    r1, r1, r2, lsl #16                      @ Subtract value 2 from value 1. Overwrites all flags.
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        lsr     r1, r1, #16                              @ Shift result down from top half of register
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ MERGE: Top halves of R7 and R8, merged with R7 as top and R8 as bottom bytes, store in DREG
handle_fx_merge:
        ldrb    r1, [rGSU, #FX_R7+1]                     @ Load R7 (top byte)
        ldrb    r2, [rGSU, #FX_R8+1]                     @ Load R8 (top byte)
        add     rR15, rR15, #1                           @ R15++
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        orr     rARM, r1, r2                             @ Calculate merge flag LUT offset
        orr     r2, r2, r1, lsl #8                       @ Merge
        add     rARM, rGSU, rARM, lsr #4                 @  |
        strh    r2, [rDREG]                              @ Store result to DREG
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldr     rARM, [rARM, #FX_mergeFlagLut-3]         @ Load flags from LUT (unaligned)
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ AND: bitwise AND of SREG and register n, store in DREG
handle_fx_and_r:
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        ldrh    r1, [rSREG]                              @ Load value 1 from SREG
        ldrh    r2, [rGSU, vLow]                         @ Load value 2 from register N
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        orr     r1, r1, r1, lsl #16                      @ Duplicate values into both halves of their registers
        orr     r2, r2, r2, lsl #16                      @  |
        ands    r1, r1, r2                               @ AND the two values together
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ MULT: multiply SREG and register n as signed 8-bit ints, store in DREG
handle_fx_mult_r:
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        ldrsb   r1, [rSREG]                              @ Load s8 value 1 from SREG
        ldrsb   r2, [rGSU, vLow]                         @ Load s8 value 2 from register N
        add     rR15, rR15, #1                           @ R15++
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        smulbb  r1, r1, r2                               @ Multiply to get the result
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        movs    r1, r1                                   @ Set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        b       dispatch                                 @ 

@ SBK: store word to last accessed RAM address
handle_fx_sbk:
        ldrh    r2, [rGSU, #FX_vLastRamAdr]              @ Load RAM offset
        ldr     vLow, [rGSU, #FX_pvRamBank]              @ Load RAM base pointer
        ldrh    r1, [rSREG]                              @ Load value from SREG
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strb    r1, [vLow, r2]                           @ Store bottom byte
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        eor     r2, r2, #1                               @ Flip bottom bit of offset
        lsr     r1, r1, #8                               @ Shift top byte down
        strb    r1, [vLow, r2]                           @ Store bottom byte
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch                                 @ 

@ LINK: R11 = R15 + immediate
handle_fx_link_i:
        add     vLow, vLow, rR15                         @ Add R15 and immediate
        add     rR15, rR15, #1                           @ R15++
        strh    vLow, [rGSU, #FX_R11]                    @ Store R11
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch                                 @ 

@ SEX: sign-extend 8-bit to 16-bit, SREG to DREG
handle_fx_sex:
        ldrsb   r1, [rSREG]                              @ Load value from SREG and sign-extend
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        movs    r1, r1                                   @ Set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store value
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ ASR: arithmetic shift right, SREG to DREG
handle_fx_asr:
        ldrsh   r1, [rSREG]                              @ Load value from SREG and sign-extend to 32-bit
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        asrs    r1, r1, #1                               @ ASR by 1 and set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ ROR: rotate right, SREG to DREG
handle_fx_ror:
        ldrh    r1, [rSREG]                              @ Load value from SREG and sign-extend to 32-bit
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        orrcs   r1, r1, #65536                           @ If the carry flag was set, set bit 16 of value
        rrxs    r1, r1                                   @ ASR by 1 and set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ JMP: jump to address of register N. No delay slot.
handle_fx_jmp_r:
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrh    rR15, [rGSU, vLow]                       @ Load destination from register N
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        strh    rR15, [rGSU, #FX_R15]                    @ Store destination to R15
        b       dispatch.skip_1                          @ 

@ LOB: set upper byte to 0, SREG to DREG
handle_fx_lob:
        ldrb    r1, [rSREG]                              @ Load bottom byte of register N and zero-extend
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        lsl     rARM, r1, #24                            @ Shift result to top byte of register
        movs    rARM, rARM                               @ Set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ FMULT: 16 to 32 bit signed multiply, keep top 16. SREG * R6, store to DREG
handle_fx_fmult:
        ldrh    r1, [rSREG]                              @ Load value 1 from SREG
        ldrh    r2, [rGSU, #FX_R6]                       @ Load value 2 from R6
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        smulbb  r1, r1, r2                               @ Signed multiply
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        asrs    r1, r1, #16                              @ Shift top half down and set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        uxth    rR15, rR15                               @ Taken from dispatch to enable branch folding
        b       dispatch.skip_1                          @ 

@ IBT: fetch PIPE and store to register N
handle_fx_ibt_r:
        mov     r1, #1                                   @ Prep R15 increment value
        uadd16  rR15, rR15, r1                           @ R15++
        sxtb    r2, rPIPE                                @ Sign-extend PIPE into temporary variable
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        uadd16  rR15, rR15, r1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        strh    r2, [rGSU, vLow]                         @ Store result
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch.skip_1                          @ 

@ IBT R14: fetch PIPE and store to Register 14, then READR14
handle_fx_ibt_r14:
        mov     r1, #1                                   @ Prep R15 increment value
        sxtb16  r2, rPIPE                                @ Sign-extend PIPE into temporary variable
        uadd16  rR15, rR15, r1                           @ R15++
        strh    r2, [rGSU, #FX_R14]                      @ Store result
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        ldr     vLow, [rGSU, #FX_pvRomBank]              @ READR14: Load ROM base pointer
        uadd16  rR15, rR15, r1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldrb    vLow, [vLow, r2]                         @ READR14: Load ROM(R14)
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        strb    vLow, [rGSU, #FX_vRomBuffer]             @ READR14: Store to ROMBUFFER
        b       dispatch.skip_1                          @ 

@ FROM: Set SREG to register N
@ If B flag is set, move register N to DREG and set flags instead
@ If B flag is not set, set SREG to register N and increment R15
handle_fx_from_r:
        tst     rSTAT, #4096                             @ Test B flag
        add     rR15, rR15, #1                           @ R15++
        bne     handle_fx_from_r.b_is_set                @ 
@ B is not set
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        add     rSREG, rGSU, vLow, lsl #1                @ SREG = register N
#ifdef SPEEDHACK_ENABLED
        and     r2, rSTAT, #768                          @ Get opcode mode bits
        orr     r2, rPIPE, r2                            @ Compute opcode
        uxth    rR15, rR15                               @ Zero-extend R15
        ldr     r1, [rGOTO, r2, lsl #2]                  @ Load destination handler
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler
#else
        b       dispatch_flags                           @ 
#endif
handle_fx_from_r.b_is_set:
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrh    r1, [rGSU, vLow]                         @ Load result
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        bic     rARM, rARM, #-805306368                  @ Clear NZO flags
        lsls    r2, r1, #24                              @ Set the flags we need
        orrmi   rARM, rARM, #268435456                   @  |
        lsls    r2, r1, #16                              @  |
        orrmi   rARM, rARM, #-2147483648                 @  |
        orreq   rARM, rARM, #1073741824                  @  V
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch                                 @ 

@ HIB: logical right-shift register by 8, SREG to DREG
handle_fx_hib:
        ldrh    r1, [rSREG]                              @ Load result from SREG
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        lsr     r1, r1, #8                               @ Prep high byte
        strh    r1, [rDREG]                              @ Store result
        sxtb    r2, r1                                   @ Sign-extend result into scratch register
        movs    r2, r2                                   @ Set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ OR: logically OR SREG and register N, store in DREG
handle_fx_or_r:
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        ldrh    r1, [rSREG]                              @ Load value 1 from SREG
        ldrh    r2, [rGSU, vLow]                         @ Load value 2 from register N
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        orr     r1, r1, r1, lsl #16                      @ Duplicate values into both halves of their registers
        orr     r2, r2, r2, lsl #16                      @  |
        orrs    r1, r1, r2                               @ OR the two values together
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ INC: increment a register. Cannot be called with R15.
handle_fx_inc_r:
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrh    r2, [rGSU, vLow]                         @ Load value
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        add     r2, r2, #1                               @ Increment value
        strh    r2, [rGSU, vLow]                         @ Store result
        lsl     rARM, r2, #16                            @ Set flags
        movs    rARM, rARM                               @  |
        mrs     rARM, cpsr                               @ Read flags from CPSR
        b       dispatch                                 @ 

@ INC R14: increment R14 and READR14
handle_fx_inc_r14:
        ldrh    r2, [rGSU, #FX_R14]                      @ Load value from R14
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        add     r2, r2, #1                               @ Increment value
        strh    r2, [rGSU, #FX_R14]                      @ Store result to R14
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        ldr     r1, [rGSU, #FX_pvRomBank]                @ READR14: Load ROM base pointer
        lsl     r2, r2, #16                              @ Set flags
        movs    vLow, r2                                 @  |
        ldrb    r2, [r1, r2, lsr #16]                    @ READR14: Load ROM(R14)
        mrs     rARM, cpsr                               @ Read flags from CPSR
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        strb    r2, [rGSU, #FX_vRomBuffer]               @ READR14: Store to ROMBUFFER
        b       dispatch                                 @ 

@ GETC: transfer ROMBUFFER to color register
handle_fx_getc:
        ldrb    r1, [rGSU, #FX_vPlotOptionReg]           @ Load plotOptionReg
        ldrb    r2, [rGSU, #FX_vRomBuffer]               @ Load ROMBUFFER
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        tst     r1, #4                                   @ If PLOT_HIGHNIBBLE, duplicate the high nibble of color to the low nibble
        andne   vLow, r2, #240                           @  |
        orrne   r2, vLow, r2, lsr #4                     @  V
        tst     r1, #8                                   @ If PLOT_FREEZEHIGH, only update the bottom nibble
        ldrbne  r1, [rGSU, #FX_vColorReg]                @  |
        andne   r2, r2, #15                              @  |
        bicne   r1, r1, #15                              @  |
        orrne   r2, r1, r2                               @  V
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strb    r2, [rGSU, #FX_vColorReg]                @ Store result to GSU.vColorReg
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch                                 @ 

@ DEC: decrement a register
handle_fx_dec_r:
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrh    r2, [rGSU, vLow]                         @ Load value
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        sub     r2, r2, #1                               @ Decrement value
        strh    r2, [rGSU, vLow]                         @ Store result
        lsl     rARM, r2, #16                            @ Set flags
        movs    rARM, rARM                               @  |
        mrs     rARM, cpsr                               @ Read flags from CPSR
        b       dispatch                                 @ 

@ DEC R14: decrement R14 and then READR14
handle_fx_dec_r14:
        ldrh    r2, [rGSU, #FX_R14]                      @ Load value from R14
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        sub     r2, r2, #1                               @ Decrement value
        strh    r2, [rGSU, #FX_R14]                      @ Store result to R14
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        ldr     r1, [rGSU, #FX_pvRomBank]                @ READR14: Load ROM base pointer
        lsl     r2, r2, #16                              @ Set flags
        movs    vLow, r2                                 @  |
        ldrb    r2, [r1, r2, lsr #16]                    @ READR14: Load ROM(R14)
        mrs     rARM, cpsr                               @ Read flags from CPSR
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        strb    r2, [rGSU, #FX_vRomBuffer]               @ READR14: Store to ROMBUFFER
        b       dispatch                                 @ 

@ GETB: get byte from ROMBUFFER
handle_fx_getb:
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        ldrb    r1, [rGSU, #FX_vRomBuffer]               @ Load value from ROMBUFFER
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store value to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ IWT: Combine existing PIPE and next PIPE into register N, then FETCHPIPE again
handle_fx_iwt_r:
        mov     r1, #1                                   @ Prep R15 increment value
        uadd16  rR15, rR15, r1                           @ R15++
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        ldrb    r2, [rPRG, rR15]                         @ FETCHPIPE
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        uadd16  rR15, rR15, r1                           @ R15++
        orr     r2, rPIPE, r2, lsl #8                    @ Combine both PIPEs into result
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        strh    r2, [rGSU, vLow]                         @ Store result to register N
        uadd16  rR15, rR15, r1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        b       dispatch.skip_1                          @ 

@ IWT R14: Combine existing PIPE and next PIPE into register 14, then FETCHPIPE again and READR14
handle_fx_iwt_r14:
        mov     r1, #1                                   @ Prep R15 increment value
        uadd16  rR15, rR15, r1                           @ R15++
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        ldrb    r2, [rPRG, rR15]                         @ FETCHPIPE
        ldr     vLow, [rGSU, #FX_pvRomBank]              @ READR14: Load ROM base pointer
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        uadd16  rR15, rR15, r1                           @ R15++
        orr     r2, rPIPE, r2, lsl #8                    @ Combine both PIPEs into result
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        uadd16  rR15, rR15, r1                           @ R15++
        strh    r2, [rGSU, #FX_R14]                      @ Store result to R14
        ldrb    vLow, [vLow, r2]                         @ READR14: Load ROM(R14)
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strb    vLow, [rGSU, #FX_vRomBuffer]             @ READR14: Store to ROMBUFFER
        b       dispatch.skip_1                          @ 

@ IWT: Combine existing PIPE and next PIPE into register 15, then FETCHPIPE again
handle_fx_iwt_r15:
        mov     r1, #1                                   @ Prep R15 increment value
        uadd16  rR15, rR15, r1                           @ R15++
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrb    r2, [rPRG, rR15]                         @ FETCHPIPE
        uadd16  r1, rR15, r1                             @ R15++
        mov     vLow, rPIPE                              @ Sacrifice 1cyc here to save 2cyc in dispatch
        ldrb    rPIPE, [rPRG, r1]                        @ FETCHPIPE
        orr     rR15, vLow, r2, lsl #8                   @ Combine both PIPEs into result
        strh    rR15, [rGSU, #FX_R15]                    @ Store result to register 15
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch.skip_1                          @ 

@ STB: Store byte in SREG at the RAM location pointed to by register N
handle_fx_stb_r:
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        add     rR15, rR15, #1                           @ R15++
        ldrh    r1, [rGSU, vLow]                         @ Load destination pointer
        ldr     r2, [rGSU, #FX_pvRamBank]                @ Load RAM base pointer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rGSU, #FX_vLastRamAdr]              @ Store destination pointer to GSU.vLastRamAdr
        ldrh    vLow, [rSREG]                            @ Load value from SREG
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        strb    vLow, [r2, r1]                           @ Store value to RAM(register N)
        b       dispatch                                 @ 

@ LDB: Load byte from the RAM location pointed to by register N into DREG
handle_fx_ldb_r:
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        ldrh    r2, [rGSU, vLow]                         @ Load source pointer
        ldr     rSREG, [rGSU, #FX_pvRamBank]             @ Load RAM base pointer
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    r2, [rGSU, #FX_vLastRamAdr]              @ Store source pointer to GSU.vLastRamAdr
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrb    r1, [rSREG, r2]                          @ Load result
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch                                 @ 

@ CMODE: set plot option register to the value in SREG
@ Call clobbers r0-r3, ip, lr (vLow, r1, r2, rR15, rPRG, lr)
handle_fx_cmode:
        ldrb    rSREG, [rSREG]                           @ Load plotOptionReg
        ldrh    r1, [rGSU, #FX_vScreenRealHeight]        @ Load real screen height
        ldrh    r2, [rGSU, #FX_vScreenHeight]            @ Load previous screen height
        add     rR15, rR15, #1                           @ R15++
        tst     rSREG, #16                               @ If PLOT_OBJECT, use a screen height of 256
        movne   r1, #256                                 @  |
        cmp     r1, r2                                   @ Check if height has changed
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strb    rSREG, [rGSU, #FX_vPlotOptionReg]        @ Store plotOptionReg
        beq     handle_fx_cmode.skip_height              @ Skip computing screen pointers if height has changed

        @ Height is different
        strh    r1, [rGSU, #FX_vScreenHeight]            @ Store screenHeight
        bl      fx_computeScreenPointers                 @ Recompute screen pointers

handle_fx_cmode.skip_height:
        ldrb    r1, [rGSU, #FX_vPrevPlotOptionReg]       @ Load prevPlotOptionReg
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        eor     rSREG, rSREG, r1                         @ XOR new and old modes
        tst     rSREG, #11                               @ Test if any of FREEZEHIGH, DITHER, TRANSPARENT were changed
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        ldr     rPRG, [rGSU, #FX_pvPrgBank]              @ FETCHPIPE: Load GSU.pvPrgBank. Taken from dispatch to save a cycle.
        beq     dispatch                                 @ If none changed, dispatch (likely)
        ldrb    rR15, [rGSU, #FX_vMode]                  @ Load GSU.vMode
        ldrb    vLow, [rGSU, #FX_vPlotOptionReg]         @ Load GSU.vPlotOptionReg
        b       select_plots_and_dispatch_flags.skip_2   @ We need to update the plotfuncs

@ ADC: add-with-carry, SREG + register N, store in DREG
handle_fx_adc_r:
        ldrh    r2, [rSREG]                              @ Load value 1 from SREG
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        add     rR15, rR15, #1                           @ R15++
        ldrh    r1, [rGSU, vLow]                         @ Load value 2 from register N
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        lsl     r2, r2, #16                              @ Shift value 1 into the upper half of the register
        orrcs   r1, r1, #-2147483648                     @ Move carry flag into value 2
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        orrcs   r2, r2, #32768                           @ Move carry flag into value 1
        adds    r1, r2, r1, ror #16                      @ Add the values and set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        lsr     r1, r1, #16                              @ Shift result into bottom half of register
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ SBC: subtract-with-carry, SREG - register N, store in DREG
handle_fx_sbc_r:
        ldrh    r1, [rSREG]                              @ Load value 1 frpm SREG
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        add     rR15, rR15, #1                           @ R15++
        ldrh    r2, [rGSU, vLow]                         @ Load value 2 from register N
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        lsl     r1, r1, #16                              @ Shift value 1 into top half of register
        sbcs    r1, r1, r2, lsl #16                      @ Do the subtract-with-carry
        mrs     rARM, cpsr                               @ Read flags from CPSR
        lsrs    r1, r1, #16                              @ Shift result into bottom half of register and set flags
        orreq   rARM, rARM, #1073741824                  @ If the result is 0, set the Z flag
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ BIC: DREG = SREG & ~register N
handle_fx_bic_r:
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        ldrh    r1, [rSREG]                              @ Load value 1 from SREG
        ldrh    r2, [rGSU, vLow]                         @ Load value 2 from register N
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        orr     r1, r1, r1, lsl #16                      @ Duplicate values into both halves of their registers
        orr     r2, r2, r2, lsl #16                      @  |
        bics    r1, r1, r2                               @ Bit clear and set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ UMULT: 8-bit to 16-bit unsigned multiply, SREG * register N, stored in DREG
handle_fx_umult_r:
        ldrb    r2, [rGSU, vLow, lsl #1]                 @ Load value 2
        ldrb    r1, [rSREG]                              @ Load value 1
        add     rR15, rR15, #1                           @ R15++
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        smulbb  r1, r1, r2                               @ Multiply
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        lsl     rARM, r1, #16                            @ Shift result to top of register
        movs    rARM, rARM                               @ Set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        b       dispatch                                 @ 

@ DIV2: Divides SREG by 2 and stores in DREG
handle_fx_div2:
        ldrsh   r1, [rSREG]                              @ Load value
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        cmn     r1, #1                                   @ If value == -1, set value to 1
        moveq   r1, #1                                   @  |
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        asrs    r1, r1, #1                               @ Divide value by 2 with ASR
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ LMULT: 16-bit to 32-bit signed multiplication SREG * R6, low result in R4, then high result in DREG.
handle_fx_lmult:
        ldrh    r1, [rSREG]                              @ Load value 1
        ldrh    r2, [rGSU, #FX_R6]                       @ Load value 2
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        smulbb  r2, r1, r2                               @ Signed multiply
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        strh    r2, [rGSU, #FX_R4]                       @ Store low result
        asrs    r1, r2, #16                              @ Shift top half down and set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    r1, [rDREG]                              @ Store high result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        b       dispatch                                 @ 

@ LMS: load word from RAM (short address), store in register N
handle_fx_lms_r:
        ldr     r1, [rGSU, #FX_pvRamBank]                @ Load RAM base pointer
        lsl     r2, rPIPE, #1                            @ Shift source address left 1
        strh    r2, [rGSU, #FX_vLastRamAdr]              @ Store shifted pipe to GSU.vLastRamAdr
        mov     rSREG, #1                                @ Prep R15 increment value
        ldrh    r1, [r1, r2]                             @ Load the halfword
        uadd16  rR15, rR15, rSREG                        @ R15++
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        uadd16  rR15, rR15, rSREG                        @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rGSU, vLow]                         @ Store result
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch.skip_1                          @ 
        
@ LMS: load word from RAM (short address), store in register 14, then READR14
handle_fx_lms_r14:
        ldr     vLow, [rGSU, #FX_pvRomBank]              @ READR14: Load ROM base pointer
        ldr     r1, [rGSU, #FX_pvRamBank]                @ Load RAM base pointer
        lsl     r2, rPIPE, #1                            @ Shift source address left 1
        strh    r2, [rGSU, #FX_vLastRamAdr]              @ Store shifted pipe to GSU.vLastRamAdr
        mov     rSREG, #1                                @ Prep R15 increment value
        ldrh    r1, [r1, r2]                             @ Load the halfword
        uadd16  rR15, rR15, rSREG                        @ R15++
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        uadd16  rR15, rR15, rSREG                        @ R15++
        ldrb    r2, [vLow, r1]                           @ READR14: Load ROM(R14)
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rGSU, $FX_R14]                      @ Store result
        strb    r2, [rGSU, #FX_vRomBuffer]               @ READR14: Store to ROMBUFFER
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch.skip_1                          @ 

@ LMS: load word from RAM (short address), store in register 15
handle_fx_lms_r15:
        ldr     r2, [rGSU, #FX_pvRamBank]                @ Load RAM base pointer
        lsl     r1, rPIPE, #1                            @ Shift source address left 1
        strh    r1, [rGSU, #FX_vLastRamAdr]              @ Store shifted pipe to GSU.vLastRamAdr
        add     rPIPE, rR15, #1                          @ R15++
        ldrh    rR15, [r2, r1]                           @ Load the halfword
        uxth    rPIPE, rPIPE                             @ Zero-extend R15
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrb    rPIPE, [rPRG, rPIPE]                     @ FETCHPIPE
        strh    rR15, [rGSU, #FX_R15]                    @ Store result
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch.skip_1                          @ 

@ XOR: exclusive OR between SREG and register N, stored in DREG
handle_fx_xor_r:
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        ldrh    r1, [rSREG]                              @ Load value 1 from SREG
        ldrh    r2, [rGSU, vLow]                         @ Load value 2 from register N
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        orr     r1, r1, r1, lsl #16                      @ Duplicate values into both halves of their registers
        orr     r2, r2, r2, lsl #16                      @  |
        eors    r1, r1, r2                               @ XOR the two values together
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result to DREG
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ GETBH: Overwrite the high byte in SREG with ROMBUFFER, stored in DREG
handle_fx_getbh:
        ldrb    r2, [rGSU, #FX_vRomBuffer]               @ Load ROMBUFFER
        ldrb    r1, [rSREG]                              @ Load SREG bottom byte
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        orr     r1, r1, r2, lsl #8                       @ Combine both sources
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ LM: Load word from RAM and store it in register N. The address is fetched from PIPE.
handle_fx_lm_r:
        mov     r2, #1                                   @ Prep R15 increment value
        uadd16  rR15, rR15, r2                           @ R15++
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        ldrb    r1, [rPRG, rR15]                         @ FETCHPIPE
        uadd16  rR15, rR15, r2                           @ R15++
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        orr     r1, rPIPE, r1, lsl #8                    @ Combine both bytes of address
        strh    r1, [rGSU, #FX_vLastRamAdr]              @ Store address to vLastRamAdr
        ldr     rSREG, [rGSU, #FX_pvRamBank]             @ Load RAM base pointer
        tst     r1, #1                                   @ If low bit of address is set, swap the bytes
        bic     r1, r1, #1                               @ Zero low bit of address
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        ldrh    r1, [rSREG, r1]                          @ Load the value
        uadd16  rR15, rR15, r2                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        rev16ne r1, r1                                   @ Swap the bytes
        strh    r1, [rGSU, vLow]                         @ Store result
        b       dispatch.skip_1                          @ 

@ LM: Load word from RAM and store it in register 14, then READR14. The address is fetched from PIPE.
handle_fx_lm_r14:
        mov     r2, #1                                   @ Prep R15 increment value
        uadd16  rR15, rR15, r2                           @ R15++
        ldr     vLow, [rGSU, #FX_pvRomBank]              @ READR14: Load ROM base pointer
        ldrb    r1, [rPRG, rR15]                         @ FETCHPIPE
        uadd16  rR15, rR15, r2                           @ R15++
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        orr     r1, rPIPE, r1, lsl #8                    @ Combine both bytes of address
        strh    r1, [rGSU, #FX_vLastRamAdr]              @ Store address to vLastRamAdr
        ldr     rSREG, [rGSU, #FX_pvRamBank]             @ Load RAM base pointer
        tst     r1, #1                                   @ If low bit of address is set, swap the bytes
        bic     r1, r1, #1                               @ Zero low bit of address
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        ldrh    r1, [rSREG, r1]                          @ Load the value
        uadd16  rR15, rR15, r2                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        rev16ne r1, r1                                   @ Swap the bytes
        strh    r1, [rGSU, #FX_R14]                      @ Store result
        ldrb    vLow, [vLow, r1]                         @ READR14: Load ROM(R14)
        strb    vLow, [rGSU, #FX_vRomBuffer]   @ stall 2 @ READR14: Store to ROMBUFFER
        b       dispatch.skip_1                          @ 

@ ADD_I: Add SREG + 4-bit immediate, store in DREG
handle_fx_add_i:
        ldrh    rARM, [rSREG]                            @ Load SREG
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        lsl     rARM, rARM, #16                          @ Shift SREG into top half of register
        adds    r1, rARM, vLow, lsl #16                  @ Add SREG and immediate, set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        lsr     r1, r1, #16                              @ Shift result to bottom half of register
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ SUB_I: Subtract SREG - 4-bit immediate, store in DREG
handle_fx_sub_i:
        ldrh    rARM, [rSREG]                            @ Load SREG
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        lsl     rARM, rARM, #16                          @ Shift SREG into top half of register
        subs    r1, rARM, vLow, lsl #16                  @ Add SREG and immediate, set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        lsr     r1, r1, #16                              @ Shift result to bottom half of register
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ AND_I: Logically AND SREG and 4-bit immediate, store in DREG
handle_fx_and_i:
        ldrh    r1, [rSREG]                              @ Load SREG
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        ands    r1, r1, vLow                             @ AND SREG and immediate, set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ MULT_I: multiply SREG and 4-bit immediate as signed 8-bit ints, store in DREG
handle_fx_mult_i:
        ldrsb   r1, [rSREG]                              @ Load SREG as s8
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        smulbb  r1, r1, vLow                             @ Multiply to get the result
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        movs    r1, r1                                   @ Set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        b       dispatch                                 @ 

@ SMS: Store register N in RAM (short address). The address is fetched from PIPE.
handle_fx_sms_r:
        ldr     r1, [rGSU, #FX_pvRamBank]                @ Load RAM base pointer
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        lsl     r2, rPIPE, #1                            @ Shift source address left 1
        ldrh    vLow, [rGSU, vLow]                       @ Load the value
        strh    r2, [rGSU, #FX_vLastRamAdr]              @ Store shifted pipe to GSU.vLastRamAdr
        mov     rSREG, #1                                @ Prep R15 increment value
        uadd16  rR15, rR15, rSREG                        @ R15++
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        uadd16  rR15, rR15, rSREG                        @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    vLow, [r1, r2]                           @ Store result
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch.skip_1                          @ 

@ OR_I: Logically OR SREG and 4-bit immediate, store in DREG
handle_fx_or_i:
        ldrh    r1, [rSREG]                              @ Load SREG
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        orr     r1, r1, r1, lsl #16                      @ Duplicate value into both halves of a register
        orrs    r1, r1, vLow                             @ OR SREG and immediate, set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ RAMB: Set current RAM bank to SREG
handle_fx_ramb:
        ldrh    r2, [rSREG]                              @ Load SREG
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        and     r2, r2, #3                               @ SREG & (FX_RAM_BANKS - 1)
        strb    r2, [rGSU, #FX_vRamBankReg]              @ Store to GSU.vRamBankReg
        add     r1, r2, #FX_apvRamBank >> 2              @ Add apvRamBank table offset, pre-shifted down
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldr     r1, [rGSU, r1, lsl #2]                   @ Load pointer at GSU.apvRamBank[GSU.vRamBankReg]
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        str     r1, [rGSU, #FX_pvRamBank]                @ Store to GSU.pvRamBank
        b       dispatch                                 @ 

@ GETBL: Overwrite the low byte in SREG with ROMBUFFER, stored in DREG
handle_fx_getbl:
        ldrb    r1, [rSREG, #1]                          @ Load SREG top byte
        ldrb    r2, [rGSU, #FX_vRomBuffer]               @ Load ROMBUFFER
        add     rR15, rR15, #1                           @ R15++
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        orr     r1, r2, r1, lsl #8                       @ Combine both sources
        strh    r1, [rDREG]                              @ Store result
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ SM: Store register N in RAM. The address is fetched from PIPE.
handle_fx_sm_r:
        mov     rDREG, #1                                @ Prep R15 increment value
        uadd16  rR15, rR15, rDREG                        @ R15++
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        ldrb    r2, [rPRG, rR15]                         @ FETCHPIPE
        ldrh    r1, [rGSU, vLow]                         @ Load register N
        uadd16  rR15, rR15, rDREG                        @ R15++
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        orr     r2, rPIPE, r2, lsl #8                    @ Combine both bytes of address
        strh    r2, [rGSU, #FX_vLastRamAdr]              @ Store address to vLastRamAdr
        ldr     rSREG, [rGSU, #FX_pvRamBank]             @ Load RAM base pointer
        tst     r2, #1                                   @ If low bit of address is set, swap the bytes
        rev16ne r1, r1                                   @ Swap the bytes
        bic     r2, r2, #1                               @ Zero low bit of address
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        strh    r1, [rSREG, r2]                          @ Store the value
        uadd16  rR15, rR15, rDREG                        @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch.skip_1                          @ 

@ ADC_I: add-with-carry, SREG + 4-bit immediate, store in DREG
handle_fx_adc_i:
        ldrh    r1, [rSREG]                              @ Load SREG
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        orrcs   vLow, vLow, #-2147483648                 @ Move carry flag into immediate
        lsl     r1, r1, #16                              @ Shift SREG into the upper half of the register
        orrcs   r1, r1, #32768                           @ Move carry flag into SREG
        adds    r1, r1, vLow, ror #16                    @ Add the values and set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        lsr     r1, r1, #16                              @ Shift result into bottom half of register
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ CMP: Compare SREG to register N. Effectively a subtract with no result.
handle_fx_cmp_r:
        ldrh    r1, [rSREG]                              @ Load SREG
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        add     rR15, rR15, #1                           @ R15++
        ldrh    r2, [rGSU, vLow]                         @ Load register N
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        lsl     r1, r1, #16                              @ Shift SREG into the upper half of the register
        cmp     r1, r2, lsl #16                          @ Compare to set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        b       dispatch                                 @ 

@ BIC_I: DREG = SREG & ~4-bit immediate
handle_fx_bic_i:
        ldrh    r1, [rSREG]                              @ Load SREG
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        orr     vLow, vLow, vLow, lsl #16                @ Duplicate value into both halves of a register
        orr     r1, r1, r1, lsl #16                      @ Duplicate value into both halves of a register
        bics    r1, r1, vLow                             @ OR SREG and immediate, set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ UMULT_I: 8-bit to 16-bit unsigned multiply, SREG * 4-bit immediate, stored in DREG
handle_fx_umult_i:
        ldrb    r1, [rSREG]                              @ Load SREG as u8
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        smulbb  r1, r1, vLow                             @ Multiply to get the result
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        movs    r1, r1                                   @ Set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        b       dispatch                                 @ 

@ XOR_I: exclusive OR between SREG and 4-bit immediate, stored in DREG
handle_fx_xor_i:
        ldrh    r1, [rSREG]                              @ Load SREG
        add     rR15, rR15, #1                           @ R15++
        msr     cpsr_f, rARM                             @ Load flags into CPSR
        orr     vLow, vLow, vLow, lsl #16                @ Duplicate value into both halves of a register
        orr     r1, r1, r1, lsl #16                      @ Duplicate value into both halves of a register
        eors    r1, r1, vLow                             @ OR SREG and immediate, set flags
        mrs     rARM, cpsr                               @ Read flags from CPSR
        cmp     rDREG, R14_PTR                           @ TESTR14: If DREG == 14, load rombuffer
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        strh    r1, [rDREG]                              @ Store result
        beq     testr14_clrflags_dispatch                @ TESTR14: branch
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch                                 @ 

@ ROMB: set program bank to SREG
handle_fx_romb:
        ldrh    r2, [rSREG]                              @ Load SREG
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        and     r2, r2, #127                             @ SREG & (FX_ROM_BANKS - 1)
        strb    r2, [rGSU, #FX_vRomBankReg]              @ Store to GSU.vRomBankReg
        add     r1, r2, #FX_apvRomBank >> 2              @ Add apvRomBank table offset, pre-shifted down
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldr     r1, [rGSU, r1, lsl #2]                   @ Load pointer at GSU.apvRomBank[GSU.vRomBankReg]
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        str     r1, [rGSU, #FX_pvRomBank]                @ Store to GSU.pvRomBank
        b       dispatch                                 @ 

#ifdef SPEEDHACK_ENABLED
testr14_clrflags_dispatch:
        ldrh    vLow, [rGSU, #FX_R14]                    @ READR14: Load R14
        ldr     r2, [rGSU, #FX_pvRomBank]                @ READR14: Load ROM pointer
        ldrh    rR15, [rGSU, #FX_R15]                    @ Reload R15
        ldr     r1, [rGOTO, rPIPE, lsl #2]               @ Load next opcode destination
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        ldrb    r2, [r2, vLow]                           @ READR14: Load ROM(R14)
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        and     vLow, rPIPE, #15                         @ Compute vLow
        strb    r2, [rGSU, #FX_vRomBuffer]               @ READR14: Store value to ROMBUFFER
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler
#else
testr14_clrflags_dispatch:
        ldrh    vLow, [rGSU, #FX_R14]                    @ READR14: Load R14
        ldr     r2, [rGSU, #FX_pvRomBank]                @ READR14: Load ROM pointer
        ldrh    rR15, [rGSU, #FX_R15]                    @ Reload R15
        ldr     r1, [rGOTO, rPIPE, lsl #2]               @ Load next opcode destination
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        ldrb    r2, [r2, vLow]                           @ READR14: Load ROM(R14)
        subs    rVCNT, rVCNT, #1                         @ Decrement vCounter and exit if 0
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        strb    r2, [rGSU, #FX_vRomBuffer]               @ READR14: Store value to ROMBUFFER
        beq     loop_end                                 @ 
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler
#endif

@ ---------- Rare Calls ----------
@ Down here to keep icache happier

#ifdef SPEEDHACK_ENABLED
@ LJMP: set program bank to register N and jump to SREG
handle_fx_ljmp_r:
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrh    r2, [rGSU, vLow]                         @ Load destination ROM bank
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        mov     vLow, #0                                 @ GSU.vCacheFlags = 0
        ldrh    rR15, [rSREG]                            @ Load destination R15
        and     r2, r2, #127                             @ AND bank to 7-bit
        strb    r2, [rGSU, #FX_vPrgBankReg]              @ Store bank to GSU.vPrgBankReg
        bic     r1, rR15, #15                            @ R15 & 0xfff0
        strh    r1, [rGSU, #FX_vCacheBaseReg]            @ GSU.vCacheBaseReg = R15 & 0xfff0
        add     r2, r2, #FX_apvRomBank >> 2              @ Offset magic for apvRomBank, pre-shifted down
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        ldr     rPRG, [rGSU, r2, lsl #2]                 @ Load pointer at GSU.apvRomBank[GSU.vPrgBankReg]
        str     vLow, [rGSU, #FX_vCacheFlags]            @ Store GSU.vCacheFlags
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        and     r2, rSTAT, #768                          @ Get opcode mode bits
        orr     r2, rPIPE, r2                            @ Compute opcode
        str     rPRG, [rGSU, #FX_pvPrgBank]              @ Store GSU.pvPrgBank pointer
        ldr     r1, [rGOTO, r2, lsl #2]                  @ Load destination handler
        and     vLow, rPIPE, #15                         @ Compute vLow
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        bx      r1                                       @ Branch to handler
#else
@ LJMP: set program bank to register N and jump to SREG
handle_fx_ljmp_r:
        lsl     vLow, vLow, #1                           @ Shift vLow for 2-byte offset
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        ldrh    r2, [rGSU, vLow]                         @ Load destination ROM bank
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        mov     vLow, #0                                 @ GSU.vCacheFlags = 0
        ldrh    rR15, [rSREG]                            @ Load destination R15
        and     r2, r2, #127                             @ AND bank to 7-bit
        strb    r2, [rGSU, #FX_vPrgBankReg]              @ Store bank to GSU.vPrgBankReg
        bic     r1, rR15, #15                            @ R15 & 0xfff0
        strh    r1, [rGSU, #FX_vCacheBaseReg]            @ GSU.vCacheBaseReg = R15 & 0xfff0
        add     r2, r2, #FX_apvRomBank >> 2              @ Offset magic for apvRomBank, pre-shifted down
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        ldr     rPRG, [rGSU, r2, lsl #2]                 @ Load pointer at GSU.apvRomBank[GSU.vPrgBankReg]
        str     vLow, [rGSU, #FX_vCacheFlags]            @ Store GSU.vCacheFlags
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        str     rPRG, [rGSU, #FX_pvPrgBank]              @ Store GSU.pvPrgBank pointer
        b       dispatch_flags.skip_1                    @ 
#endif

@ IBT R15: fetch PIPE and store to Register 15
handle_fx_ibt_r15:
        mov     r1, #1                                   @ Prep R15 increment value
        uadd16  rR15, rR15, r1                           @ R15++
        sxtb16  r2, rPIPE                                @ Sign-extend PIPE into temporary variable
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        strh    r2, [rGSU, #FX_R15]                      @ Store result
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        b       dispatch.skip_1                          @ 

@ LM: Load word from RAM and store it in register 15. The address is fetched from PIPE.
@ Untested. Don't have a game that uses it!
handle_fx_lm_r15:
        mov     r2, #1                                   @ Prep R15 increment value
        uadd16  rR15, rR15, r2                           @ R15++
        tst     rPIPE, #1                                @ If low bit of address is set, swap the bytes
        ldrb    r1, [rPRG, rR15]                         @ FETCHPIPE
        uadd16  rR15, rR15, r2                           @ R15++
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        mov     rDREG, rGSU                              @ CLRFLAGS: DREG = 0
        orr     r1, rPIPE, r1, lsl #8                    @ Combine both bytes of address
        strh    r1, [rGSU, #FX_vLastRamAdr]              @ Store address to vLastRamAdr
        ldr     rSREG, [rGSU, #FX_pvRamBank]             @ Load RAM base pointer
        bic     r1, r1, #1                               @ Zero low bit of address
        ldrb    rPIPE, [rPRG, rR15]                      @ FETCHPIPE
        ldrh    rR15, [rSREG, r1]              @ stall 1 @ Load the value
        mov     rSREG, rGSU                              @ CLRFLAGS: SREG = 0
        rev16ne rR15, rR15                     @ stall 2 @ Swap the bytes
        strh    rR15, [rGSU, #FX_R15]                    @ Store result
        b       dispatch.skip_1                          @ 

loop_end:
        strh    rSTAT, [rGSU, #FX_vStatusReg]            @ Save reserved registers
        lsr     rARM, rARM, #24                          @  |
        strb    rARM, [rGSU, #FX_armFlags]               @  |
        strb    rPIPE, [rGSU, #FX_vPipe]                 @  |
        sub     rR15, rSREG, rGSU                        @  |
        asr     rR15, rR15, #1                           @  |
        strb    rR15, [rGSU, #FX_pvSreg]                 @  |
        sub     rR15, rDREG, rGSU                        @  |
        asr     rR15, rR15, #1                           @  |
        strb    rR15, [rGSU, #FX_pvDreg]                 @  V
        pop     {r0, rGSU, rVCNT, rSTAT, rARM, rSREG, rDREG, rPIPE, rGOTO, pc} @ Return

    .cfi_endproc

    .section    .rodata
    .align      5
    .type       plot_rpix_handler_table, %object
plot_rpix_handler_table:
        .word   handle_fx_plot_2bit
        .word   handle_fx_rpix_2bit
        .word   handle_fx_plot_2bit_t
        .word   handle_fx_rpix_2bit
        .word   handle_fx_plot_2bit_d
        .word   handle_fx_rpix_2bit
        .word   handle_fx_plot_2bit_dt
        .word   handle_fx_rpix_2bit

        .word   handle_fx_plot_4bit
        .word   handle_fx_rpix_4bit
        .word   handle_fx_plot_4bit_t
        .word   handle_fx_rpix_4bit
        .word   handle_fx_plot_4bit_d
        .word   handle_fx_rpix_4bit
        .word   handle_fx_plot_4bit_dt
        .word   handle_fx_rpix_4bit

        .word   handle_fx_plot_4bit
        .word   handle_fx_rpix_4bit
        .word   handle_fx_plot_4bit_t
        .word   handle_fx_rpix_4bit
        .word   handle_fx_plot_4bit_d
        .word   handle_fx_rpix_4bit
        .word   handle_fx_plot_4bit_dt
        .word   handle_fx_rpix_4bit

        @ F bit is moved to D bit for 8bit
        .word   handle_fx_plot_8bit
        .word   handle_fx_rpix_8bit
        .word   handle_fx_plot_8bit_t
        .word   handle_fx_rpix_8bit
        .word   handle_fx_plot_8bit_f
        .word   handle_fx_rpix_8bit
        .word   handle_fx_plot_8bit_ft
        .word   handle_fx_rpix_8bit

    .data
    .align      5
    .type       opcode_goto_table, %object
opcode_goto_table:
        #include "fxinst_asm_opcode_table.h"
