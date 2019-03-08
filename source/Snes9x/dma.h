#include "copyright.h"


#ifndef _DMA_H_
#define _DMA_H_

START_EXTERN_C
void S9xResetDMA (void);
uint8 S9xDoHDMA (uint8);
void S9xStartHDMA ();
void S9xDoDMA (uint8);
END_EXTERN_C

#endif

