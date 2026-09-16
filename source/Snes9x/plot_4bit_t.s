@ !DITHER TRANSPARENT
@ PLOT 4BIT: Draws a pixel at R1,R2 (X,Y), using GSU.vColorReg as the source
handle_fx_plot_4bit_t:
        ldrb    r2, [rGSU, #FX_R2]                       @ Load Y
        add     rR15, rR15, #1                           @ R15++
        ldrh    rSREG, [rGSU, #FX_vScreenHeight]         @ Load screen height
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        ldrh    r1, [rGSU, #FX_R1]                       @ Load X
        cmp     r2, rSREG                                @ Test Y > screen height
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        add     rDREG, r1, #1                            @ X++
        strh    rDREG, [rGSU, #FX_R1]                    @  |
        ldrbcc  rSREG, [rGSU, #FX_vPlotOptionReg]        @ Load vPlotOptionReg
        ldrbcc  vLow, [rGSU, #FX_vColorReg]              @ Load vColorReg
        bcs     handle_fx_plot_4bit_dt.return            @ If Y > screen height, return
        uxtb    r1, r1                                   @ Truncate X to 8-bit

        @ R1 is X
        @ R2 is Y
        @ vLow is color
        @ rR15 is free
        and     rSREG, r1, #7                            @ Mask = BIT(7) >> (X & 7)
        lsr     r1, r1, #3                               @ X GSU.x[X >> 3]
        add     r1, rGSU, r1, lsl #2                     @ X
        add     r2, rGSU, r2, lsl #2                     @ Screen GSU.apvScreen[Y >> 3]
        b       handle_fx_plot_4bit.common
