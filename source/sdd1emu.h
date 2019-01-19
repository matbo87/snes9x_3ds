#include "copyright.h"

#ifndef SDD1EMU_H
#define SDD1EMU_H

/* for START_EXTERN_C/END_EXTERN_C */
#include "port.h"

START_EXTERN_C

void SDD1_decompress(uint8 *out, uint8 *in, int output_length);

void SDD1_init(uint8 *in);
uint8 SDD1_get_byte(void);

END_EXTERN_C

#endif
