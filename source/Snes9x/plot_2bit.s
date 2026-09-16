@ !DITHER !TRANSPARENT
@ PLOT 2BIT: Draws a pixel at R1,R2 (X,Y), using GSU.vColorReg as the source
handle_fx_plot_2bit:
        ldrb    vLow, [rGSU, #FX_vColorReg]              @ Load vColorReg
        ldrh    r1, [rGSU, #FX_R1]                       @ Load X
        ldrb    r2, [rGSU, #FX_R2]                       @ Load Y
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        tst     vLow, #15                                @ If the color is transparent, return
        add     rDREG, r1, #1                            @ X++
        strh    rDREG, [rGSU, #FX_R1]                    @  |
        beq     handle_fx_plot_2bit.return               @ If the color is transparent, return
        ldrh    rR15, [rGSU, #FX_vScreenHeight]          @ Load screen height
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        uxtb    r1, r1                                   @ Truncate X to 8-bit
        cmp     r2, rR15                                 @ Test Y > screen height
        bcs     handle_fx_plot_2bit.return               @ If Y > screen height, return

        @ R1 is X
        @ R2 is Y
        @ vLow is color
        @ rR15 is free
handle_fx_plot_2bit.common:
        and     rSREG, r1, #7                            @ Mask = BIT(7) >> (X & 7)
        lsr     r1, r1, #3                               @ X GSU.x[X >> 3]
        add     r2, rGSU, r2, lsl #2                     @ Screen GSU.apvScreen[Y >> 3]
        add     r1, rGSU, r1, lsl #2                     @ X
        ldr     r2, [r2, #FX_apvScreen]                  @ Screen
        ldr     r1, [r1, #FX_x]                          @ X
        mov     rR15, #128                               @ Mask
        lsr     rR15, rR15, rSREG                        @ Mask
        add     r2, r1, r2                               @ Pixel 0 pointer
        orr     rR15, rR15, rR15, lsl #8                 @ Duplicate mask to both bytes of reg

        @ R1 is free
        @ R2 is the pixel 0 Pointer
        @ rR15 is the pixel mask
        @ vLow is color
        @ rSREG is free

        @ The pointer seems to always be 2-byte aligned, so this is a free speedup
        ldrh    rSREG, [r2, #0]                          @ Load pixel pair 1
        ldrh    r1, [r2, #16]                            @ Load pixel pair 2. Up here to avoid a stall.
        tst     vLow, #1                                 @ Pixel conditional
        bic     rSREG, rSREG, rR15                       @  |
        orrne   rSREG, rSREG, rR15, lsr #8               @  |
        tst     vLow, #2                                 @ Pixel conditional
        orrne   rSREG, rSREG, rR15, lsl #8               @  |
        strh    rSREG, [r2, #0]                          @ Store pixel pair

        @ Interleave between rSREG and r1 to prevent stalls
        tst     vLow, #4                                 @ Pixel conditional
        bic     r1, r1, rR15                             @  |
        orrne   r1, r1, rR15, lsr #8                     @  |
        tst     vLow, #8                                 @ Pixel conditional
        orrne   r1, r1, rR15, lsl #8                     @  |
        strh    r1, [r2, #16]                            @ Store pixel pair

handle_fx_plot_2bit.return:
        ldrh    rR15, [rGSU, #FX_R15]                    @ Taken from dispatch to allow branch folding
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch.skip_1                          @ 
