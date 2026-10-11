/*
 * This wacky linker script has the specific goal of putting .gsu_segment at a memory address
 * where the address of R14 can be expressed as an ARM32 immediate value (AKA an 8-bit value,
 * rotated right any even number of bits)
 * 
 * This is used to simplify the TESTR14 operation in fxinst_asm.s, as it can be reduced from 
 * 2 instructions to 1 (or, from 1 register + 1 instruction to only 1 instruction)
 * 
 * I would love to place this into BSS, since this would prevent the ELF file from ballooning,
 * but I'm not sure that this is possible.
 */

/*
 *  IF YOU MODIFY THIS, YOU MUST ALSO MODIFY THE CONSTANT IN FXINST_ASM.S ACCORDINGLY
 */

SECTIONS
{
  .gsu_segment 0x009FFFE0  : {KEEP(*(.bss))}
}

