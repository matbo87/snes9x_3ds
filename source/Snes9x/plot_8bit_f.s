@ !TRANSPARENT FREEZEHIGH
@ PLOT 8BIT: Draws a pixel at R1,R2 (X,Y), using GSU.vColorReg as the source

handle_fx_plot_8bit_f:
        ldr     vLow, [rGSU, #FX_vColorReg]              @ Load vColorReg (bottom 8) and screen height (top 16)
        ldrh    r1, [rGSU, #FX_R1]                       @ Load X
        ldrb    r2, [rGSU, #FX_R2]                       @ Load Y
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        tst     vLow, #15                                @ If the color is transparent, return
        add     rDREG, r1, #1                            @ X++
        strh    rDREG, [rGSU, #FX_R1]                    @  |
        beq     handle_fx_plot_8bit_f.return             @ If the color is transparent, return
        cmp     r2, vLow, lsr #16                        @ Test Y > screen height
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        uxtb    r1, r1                                   @ Truncate X to 8-bit
        bcs     handle_fx_plot_8bit_f.return             @ If Y > screen height, return

        @ R1 is X
        @ R2 is Y
        @ vLow is color
        @ rR15 is free
        and     rSREG, r1, #7                            @ Mask = BIT(7) >> (X & 7)
        lsr     r1, r1, #3                               @ X GSU.x[X >> 3]
        b       handle_fx_plot_8bit.common2

handle_fx_plot_8bit_f.return:
        ldrh    rR15, [rGSU, #FX_R15]                    @ Taken from dispatch to allow branch folding
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch.skip_1                          @ 
