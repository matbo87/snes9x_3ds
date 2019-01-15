#include "copyright.h"


#include <string.h>
#include "memmap.h"
#include "obc1.h"

static uint8 *OBC1_RAM = NULL;

int OBC1_Address;
int OBC1_BasePtr;
int OBC1_Shift;

extern "C"
{
uint8 GetOBC1 (uint16 Address)
{
	switch(Address) {
		case 0x7ff0:
			return OBC1_RAM[OBC1_BasePtr + (OBC1_Address << 2)];
		
		case 0x7ff1:
			return OBC1_RAM[OBC1_BasePtr + (OBC1_Address << 2) + 1];
		
		case 0x7ff2:
			return OBC1_RAM[OBC1_BasePtr + (OBC1_Address << 2) + 2];
		
		case 0x7ff3:
			return OBC1_RAM[OBC1_BasePtr + (OBC1_Address << 2) + 3];
		
		case 0x7ff4:
			return OBC1_RAM[OBC1_BasePtr + (OBC1_Address >> 2) + 0x200];	
	}

	return OBC1_RAM[Address & 0x1fff];
}

void SetOBC1 (uint8 Byte, uint16 Address)
{
	switch(Address) {
		case 0x7ff0:
		{
			OBC1_RAM[OBC1_BasePtr + (OBC1_Address << 2)] = Byte;
			break;
		}
		
		case 0x7ff1:
		{
			OBC1_RAM[OBC1_BasePtr + (OBC1_Address << 2) + 1] = Byte;
			break;
		}
		
		case 0x7ff2:
		{
			OBC1_RAM[OBC1_BasePtr + (OBC1_Address << 2) + 2] = Byte;
			break;
		}
		
		case 0x7ff3:
		{
			OBC1_RAM[OBC1_BasePtr + (OBC1_Address << 2) + 3] = Byte;
			break;
		}
		
		case 0x7ff4:
		{
			unsigned char Temp;

			Temp = OBC1_RAM[OBC1_BasePtr + (OBC1_Address >> 2) + 0x200];
			Temp = (Temp & ~(3 << OBC1_Shift)) | ((Byte & 3) << OBC1_Shift);	
			OBC1_RAM[OBC1_BasePtr + (OBC1_Address >> 2) + 0x200] = Temp;
			break;
		}
		
		case 0x7ff5:
		{
			if (Byte & 1) 
				OBC1_BasePtr = 0x1800;
			else
				OBC1_BasePtr = 0x1c00;

			break;
		}
		
		case 0x7ff6:
		{
			OBC1_Address = Byte & 0x7f;	
			OBC1_Shift = (Byte & 3) << 1;	
			break;
		}	
	}

	OBC1_RAM[Address & 0x1fff] = Byte;
}

uint8 *GetBasePointerOBC1(uint32 Address)
{
	return Memory.FillRAM;
}

uint8 *GetMemPointerOBC1(uint32 Address)
{
	return (Memory.FillRAM + (Address & 0xffff));
}

void ResetOBC1()
{
	OBC1_RAM = &Memory.FillRAM[0x6000];

	if (OBC1_RAM[0x1ff5] & 1) 
		OBC1_BasePtr = 0x1800;
	else
		OBC1_BasePtr = 0x1c00;

	OBC1_Address = OBC1_RAM[0x1ff6] & 0x7f;	
	OBC1_Shift = (OBC1_RAM[0x1ff6] & 3) << 1;
}

}
