@ !DITHER TRANSPARENT
@ PLOT 2BIT: Draws a pixel at R1,R2 (X,Y), using GSU.vColorReg as the source
handle_fx_plot_2bit_t:
        ldr     vLow, [rGSU, #FX_vColorReg]              @ Load vColorReg (bottom 8) and screen height (top 16)
        ldrb    r2, [rGSU, #FX_R2]                       @ Load Y
        ldrh    r1, [rGSU, #FX_R1]                       @ Load X
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        cmp     r2, vLow, lsr #16                        @ Test Y > screen height
        add     rDREG, r1, #1                            @ X++
        strh    rDREG, [rGSU, #FX_R1]                    @  |
        bcs     handle_fx_plot_2bit_t.return             @ If Y > screen height, return
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        and     rSREG, r1, #7                            @ Mask = BIT(7) >> (X & 7)
        b       handle_fx_plot_2bit.common
