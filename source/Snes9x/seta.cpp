#include "copyright.h"


#include "seta.h"

void (*SetSETA)(uint32, uint8)=&S9xSetST010;
uint8 (*GetSETA)(uint32)=&S9xGetST010;

extern "C"{
uint8 S9xGetSetaDSP(uint32 Address)
{
	return GetSETA(Address);
}

void S9xSetSetaDSP(uint8 Byte, uint32 Address)
{
	SetSETA(Address, Byte);
}
}

