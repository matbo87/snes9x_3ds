@ !TRANSPARENT FREEZEHIGH
@ PLOT 8BIT: Draws a pixel at R1,R2 (X,Y), using GSU.vColorReg as the source

handle_fx_plot_8bit_f:
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
        bcs     handle_fx_plot_8bit_f.return             @ If Y > screen height, return
        tst     vLow, #15                                @ If COLOR == 0, return. Else, continue drawing
        uxtb    r1, r1                                   @ Truncate X to 8-bit

        @ R1 is X
        @ R2 is Y
        @ vLow is color
        @ rR15 is free
        and     rSREG, r1, #7                            @ Mask = BIT(7) >> (X & 7)
        beq     handle_fx_plot_8bit.return               @ Transparency return
        lsr     r1, r1, #3                               @ X GSU.x[X >> 3]
        add     r1, rGSU, r1, lsl #2                     @ X
        b       handle_fx_plot_8bit.common

handle_fx_plot_8bit_f.return:
        ldrh    rR15, [rGSU, #FX_R15]                    @ Taken from dispatch to allow branch folding
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch.skip_1                          @ 
