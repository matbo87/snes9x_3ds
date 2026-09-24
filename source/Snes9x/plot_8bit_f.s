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
        and     rSREG, r1, #7                            @ Mask = BIT(7) >> (X & 7)
        bcs     handle_fx_plot_8bit_f.return             @ If Y > screen height, return

        @ R1 is X
        @ R2 is Y
        @ vLow is color
        @ rR15 is free
        and     r1, r1, #248                             @ X GSU.x[(x & 11111000) >> 1]
        add     r2, rGSU, r2, lsl #2                     @ Screen GSU.apvScreen[Y >> 3]
        b       handle_fx_plot_8bit.common2
