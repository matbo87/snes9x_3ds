@ !DITHER !TRANSPARENT
@ PLOT 2BIT: Draws a pixel at R1,R2 (X,Y), using GSU.vColorReg as the source
handle_fx_plot_2bit:
        ldrb    r2, [rGSU, #FX_R2]                       @ Load Y
        add     rR15, rR15, #1                           @ R15++
        ldrh    rSREG, [rGSU, #FX_vScreenHeight]         @ Load screen height
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldrh    r1, [rGSU, #FX_R1]                       @ Load X
        cmp     r2, rSREG                                @ Test Y > screen height
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        add     rDREG, r1, #1                            @ X++
        strh    rDREG, [rGSU, #FX_R1]                    @  |
        ldrbcc  vLow, [rGSU, #FX_vColorReg]              @ Load vColorReg
        ldrbcc  rSREG, [rGSU, #FX_vPlotOptionReg]        @ Load vPlotOptionReg
        bcs     handle_fx_plot_2bit.return               @ If Y > screen height, return
        tst     vLow, #15                                @ If the color is transparent, return
        uxtb    r1, r1                                   @ Truncate X to 8-bit

        @ R1 is X
        @ R2 is Y
        @ vLow is color
        @ rR15 is free
        and     rSREG, r1, #7                            @ Mask = BIT(7) >> (X & 7)
        beq     handle_fx_plot_2bit.return               @ If the color is transparent, return
        lsr     r1, r1, #3                               @ X GSU.x[X >> 3]
        add     r1, rGSU, r1, lsl #2                     @ X
        add     r2, rGSU, r2, lsl #2                     @ Screen GSU.apvScreen[Y >> 3]
handle_fx_plot_2bit.common:
        ldr     r1, [r1, #FX_x]                          @ X
        ldr     r2, [r2, #FX_apvScreen]                  @ Screen
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
        ldrh    r1, [r2, #0]                             @ Load pixel pair 1
        tst     vLow, #1                                 @ Pixel conditional
        bic     r1, r1, rR15                             @  |
        orrne   r1, r1, rR15, lsr #8                     @  |
        tst     vLow, #2                                 @ Pixel conditional
        orrne   r1, r1, rR15, lsl #8                     @  |
        strh    r1, [r2, #0]                             @ Store pixel pair

handle_fx_plot_2bit.return:
        ldrh    rR15, [rGSU, #FX_R15]                    @ Taken from dispatch to allow branch folding
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch.skip_1                          @ 
