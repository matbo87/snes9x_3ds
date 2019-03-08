#include "copyright.h"


#ifndef NO_SETA
#ifndef _seta_h
#define _seta_h

#include "port.h"

#define ST_010 0x01
#define ST_011 0x02
#define ST_018 0x03


extern "C"
{
uint8 S9xGetSetaDSP(uint32 Address);
void S9xSetSetaDSP(uint8 byte,uint32 Address);
uint8 S9xGetST018(uint32 Address);
void S9xSetST018(uint8 Byte, uint32 Address);

uint8 S9xGetST010(uint32 Address);
void S9xSetST010(uint32 Address, uint8 Byte);
uint8 S9xGetST011(uint32 Address);
void S9xSetST011(uint32 Address, uint8 Byte);
}

extern void (*SetSETA)(uint32, uint8);
extern uint8 (*GetSETA)(uint32);

typedef struct SETA_ST010_STRUCT
{
	uint8 input_params[16];
	uint8 output_params[16];
	uint8 op_reg;
	uint8 execute;
	bool8 control_enable;
} ST010_Regs;

typedef struct SETA_ST011_STRUCT
{
	bool8 waiting4command;
	uint8 status;
	uint8 command;
	uint32 in_count;
	uint32 in_index;
	uint32 out_count;
	uint32 out_index;
	uint8 parameters [512];
	uint8 output [512];
} ST011_Regs;

typedef struct SETA_ST018_STRUCT
{
	bool8 waiting4command;
	uint8 status;
	uint8 part_command;
	uint8 pass;
	uint32 command;
	uint32 in_count;
	uint32 in_index;
	uint32 out_count;
	uint32 out_index;
	uint8 parameters [512];
	uint8 output [512];
} ST018_Regs;

#endif
#endif

