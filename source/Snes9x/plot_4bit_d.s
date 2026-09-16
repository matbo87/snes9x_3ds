@ DITHER !TRANSPARENT
@ PLOT 4BIT: Draws a pixel at R1,R2 (X,Y), using GSU.vColorReg as the source
handle_fx_plot_4bit_d:
        ldrb    vLow, [rGSU, #FX_vColorReg]              @ Load vColorReg
        ldrh    r1, [rGSU, #FX_R1]                       @ Load X
        ldrb    r2, [rGSU, #FX_R2]                       @ Load Y
        add     rR15, rR15, #1                           @ R15++
        strh    rR15, [rGSU, #FX_R15]                    @ Store R15
        tst     vLow, #15                                @ If the color is transparent, return
        add     rDREG, r1, #1                            @ X++
        strh    rDREG, [rGSU, #FX_R1]                    @  |
        beq     handle_fx_plot_4bit_d.return             @ If the color is transparent, return
        ldrh    rR15, [rGSU, #FX_vScreenHeight]          @ Load screen height
        bic     rSTAT, rSTAT, #4864                      @ CLRFLAGS: STAT
        uxtb    r1, r1                                   @ Truncate X to 8-bit
        cmp     r2, rR15                                 @ Test Y > screen height
        bcs     handle_fx_plot_4bit_d.return             @ If Y > screen height, return
        
        @ Dither: odd pixels use the top nibble of COLR
        eor     rR15, r1, r2                             @ X ^ Y
        tst     rR15, #1                                 @ Test if odd
        lsrne   vLow, vLow, #4                           @ Odd X uses top nibble of color
        b       handle_fx_plot_4bit.common
