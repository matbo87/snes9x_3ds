#define SPEEDHACK_ENABLED
#define fx_run_asm fx_run_asm_speedhack
#include "fxinst_asm.s"

@ This file does not count instructions and instead runs until
@ a stop instruction is encountered. Required for Star Fox,
@ breaks Winter Gold, and gives a good speedup for the rest.

@ If you modify fxinst_asm.s, you need to rebuild this file.
