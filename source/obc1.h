#include "copyright.h"


#ifndef _OBC1_H_
#define _OBC1_H_

START_EXTERN_C
uint8 GetOBC1 (uint16 Address);
void SetOBC1 (uint8 Byte, uint16 Address);
uint8 *GetBasePointerOBC1(uint32 Address);
uint8 *GetMemPointerOBC1(uint32 Address);
void ResetOBC1();//bool8 full);
END_EXTERN_C 

#endif

