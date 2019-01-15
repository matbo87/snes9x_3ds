#include "copyright.h"


#ifndef _HWREGISTERS_H_
#define _HWREGISTERS_H_

extern "C" 
{
uint8 S9xGetByteFromRegister (uint8 *GetAddress, uint32 Address);
uint16 S9xGetWordFromRegister (uint8 *GetAddress, uint32 Address);
void S9xSetByteToRegister (uint8 Byte, uint8* SetAddress, uint32 Address);
void S9xSetWordToRegister(uint16 Word, uint8 *SetAddress, uint32 Address);
}

#endif