@ TRANSPARENT
@ PLOT 8BIT: Draws a pixel at R1,R2 (X,Y), using GSU.vColorReg as the source
handle_fx_plot_8bit_ft: @ Freezehigh is ignored if transparency is on
handle_fx_plot_8bit_t:
        ldr     vLow, [rGSU, #FX_vColorReg]              @ Load vColorReg (bottom 8) and screen height (top 16)
        ldrb    r2, [rGSU, #FX_R2]                       @ Load Y
        ldrh    r1, [rGSU, #FX_R1]                       @ Load X
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        cmp     r2, vLow, lsr #16                        @ Test Y > screen height
        add     rDREG, r1, #1                            @ X++
        strh    rDREG, [rGSU, #FX_R1]                    @  |
        bcs     handle_fx_plot_8bit_t.return             @ If Y > screen height, return
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        uxtb    r1, r1                                   @ Truncate X to 8-bit
        b       handle_fx_plot_8bit.common

handle_fx_plot_8bit_t.return:
        ldrh    rR15, [rGSU, #FX_R15]                    @ Taken from dispatch to allow branch folding
        ldrd    rSREG, [rGSU, #FX_sregDreg0]             @ CLRFLAGS: Reset SREG/DREG
        b       dispatch.skip_1                          @ 
