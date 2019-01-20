#include "copyright.h"


#ifndef _apumemory_h_
#define _apumemory_h_

START_EXTERN_C
extern uint8 APUROM[64];
END_EXTERN_C

static INLINE uint8 apu_get_reg (uint8 Address)
{
	switch (Address)
	{
		case 0xf0:	// -w TEST
			return 0;

		case 0xf1:	// -w CONTROL
			return 0;

		case 0xf2:	// rw DSPADDR
			return (IAPU.RAM[Address]);

		case 0xf3:	// rw DSPDATA
			return (S9xGetAPUDSP());

		case 0xf4:	// r- CPUI0
		case 0xf5:	// r- CPUI1
		case 0xf6:	// r- CPUI2
		case 0xf7:	// r- CPUI3
		#ifdef SPC700_SHUTDOWN
			IAPU.WaitAddress2 = IAPU.WaitAddress1;
			IAPU.WaitAddress1 = IAPU.PC;
		#endif
			return (IAPU.RAM[Address]);

		case 0xf8:	// rw - Normal RAM
		case 0xf9:	// rw - Normal RAM
			return (IAPU.RAM[Address]);

		case 0xfa:	// -w T0TARGET
		case 0xfb:	// -w T1TARGET
		case 0xfc:	// -w T2TARGET
			return 0;

		case 0xfd:	// r- T0OUT
		case 0xfe:	// r- T1OUT
		case 0xff:	// r- T2OUT
		#ifdef SPC700_SHUTDOWN
			IAPU.WaitAddress2 = IAPU.WaitAddress1;
			IAPU.WaitAddress1 = IAPU.PC;
		#endif
			uint8 t = IAPU.RAM[Address] & 0xF;
			IAPU.RAM[Address] = 0;
			return (t);
	}

	return 0;
}

extern "C" void DspWriteByte(u8 val, u8 address);

static INLINE void apu_set_reg (uint8 byte, uint8 Address)
{
	switch (Address)
	{
		case 0xf0:	// -w TEST
			//printf("Write %02X to APU 0xF0!\n", byte);
			return;

		case 0xf1:	// -w CONTROL
			S9xSetAPUControl(byte);
			return;

		case 0xf2:	// rw DSPADDR
			IAPU.RAM[Address] = byte;
			return;

		case 0xf3:	// rw DSPDATA
			S9xSetAPUDSPLater(byte);
      DspWriteByte(byte, IAPU.RAM[0xf2]);
      IAPU.DSPCopy[IAPU.RAM[0xf2]] = byte;
      if (Settings.UseFastDSPCore)
      {
        // We need this here so that when we save the state while using FastDSPCore mode,
        // the DSP registers are saved.
        //
        APU.DSP[IAPU.RAM[0xf2]] = byte;
      }
			return;

		case 0xf4:	// -w CPUO0
		case 0xf5:	// -w CPUO1
		case 0xf6:	// -w CPUO2
		case 0xf7:	// -w CPUO3
			APU.OutPorts[Address - 0xf4] = byte;
			return;

		case 0xf8:	// rw - Normal RAM
		case 0xf9:	// rw - Normal RAM
			IAPU.RAM[Address] = byte;
			return;

		case 0xfa:	// -w T0TARGET
		case 0xfb:	// -w T1TARGET
		case 0xfc:	// -w T2TARGET
			IAPU.RAM[Address] = byte;
			if (byte == 0)
				APU.TimerTarget[Address - 0xfa] = 0x100;
			else
				APU.TimerTarget[Address - 0xfa] = byte;
			return;

		case 0xfd:	// r- T0OUT
		case 0xfe:	// r- T1OUT
		case 0xff:	// r- T2OUT
			return;
	}
}

INLINE uint8 S9xAPUGetByteZ (uint8 Address)
{
	if (Address >= 0xf0 && IAPU.DirectPage == IAPU.RAM)
		return (apu_get_reg(Address));
	else
		return (IAPU.DirectPage[Address]);
}

INLINE void S9xAPUSetByteZ (uint8 byte, uint8 Address)
{
    if (Address >= 0xf0 && IAPU.DirectPage == IAPU.RAM)
		apu_set_reg(byte, Address);
    else
		IAPU.DirectPage[Address] = byte;
}

INLINE uint8 S9xAPUGetByte (uint32 Address)
{
    Address &= 0xffff;
    if (Address <= 0xff && Address >= 0xf0)
		return (apu_get_reg(Address & 0xff));
    else
		return (IAPU.RAM[Address]);
}

INLINE void S9xAPUSetByte (uint8 byte, uint32 Address)
{
    Address &= 0xffff;
    if (Address <= 0xff && Address >= 0xf0)
		apu_set_reg(byte, Address & 0xff);
	else
	if (Address < 0xffc0)
	    IAPU.RAM[Address] = byte;
	else
	{
	    APU.ExtraRAM[Address - 0xffc0] = byte;
	    if (!APU.ShowROM)
		IAPU.RAM[Address] = byte;
	}
}

#endif // _apumemory_h_
