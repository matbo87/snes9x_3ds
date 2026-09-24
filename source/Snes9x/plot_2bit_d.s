@ DITHER !TRANSPARENT
@ PLOT 2BIT: Draws a pixel at R1,R2 (X,Y), using GSU.vColorReg as the source
handle_fx_plot_2bit_d:
        ldr     vLow, [rGSU, #FX_vColorReg]              @ Load vColorReg (bottom 8) and screen height (top 16)
        ldrh    r1, [rGSU, #FX_R1]                       @ Load X
        ldrb    r2, [rGSU, #FX_R2]                       @ Load Y
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        tst     vLow, #15                                @ If the color is transparent, return
        add     rDREG, r1, #1                            @ X++
        strh    rDREG, [rGSU, #FX_R1]                    @  |
        beq     handle_fx_plot_2bit_d.return             @ If the color is transparent, return
        cmp     r2, vLow, lsr #16                        @ Test Y > screen height
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        and     rSREG, r1, #7                            @ Mask = BIT(7) >> (X & 7)
        bcs     handle_fx_plot_2bit_d.return             @ If Y > screen height, return
        
        @ Dither: odd pixels use the top nibble of COLR
        eor     rR15, r1, r2                             @ X ^ Y
        tst     rR15, #1                                 @ Test if odd
        lsrne   vLow, vLow, #4                           @ Odd X uses top nibble of color
        b       handle_fx_plot_2bit.common
