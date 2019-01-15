#include "copyright.h"


#include <3ds.h>
#include "snes9x.h"
#include "memmap.h"
#include "hwregisters.h"
#include "apu.h"
#include "bsx.h"

uint8 S9xGetByteFromRegister (uint8 *GetAddress, uint32 Address)
{
    switch ((int) GetAddress)
    {
		case CMemory::MAP_PPU:
			return (S9xGetPPU (Address & 0xffff));
						
		case CMemory::MAP_CPU:
			return (S9xGetCPU (Address & 0xffff));
						
		case CMemory::MAP_DSP:
			return (S9xGetDSP (Address & 0xffff));
						
		case CMemory::MAP_SA1RAM:
		case CMemory::MAP_LOROM_SRAM:
			//Address &0x7FFF -offset into bank
			//Address&0xFF0000 -bank
			//bank>>1 | offset = s-ram address, unbound
			//unbound & SRAMMask = Sram offset
			return (*(Memory.SRAM + ((((Address&0xFF0000)>>1) |(Address&0x7FFF)) &Memory.SRAMMask)));
			//		return (*(Memory.SRAM + ((Address & Memory.SRAMMask))));
		
		case CMemory::MAP_RONLY_SRAM:
		case CMemory::MAP_HIROM_SRAM:
			return (*(Memory.SRAM + (((Address & 0x7fff) - 0x6000 +
					((Address & 0xf0000) >> 3)) & Memory.SRAMMask)));
		
		case CMemory::MAP_BWRAM:
			return (*(Memory.BWRAM + ((Address & 0x7fff) - 0x6000)));
		
		case CMemory::MAP_C4:
			return (S9xGetC4 (Address & 0xffff));
		
		case CMemory::MAP_SPC7110_ROM:
#ifdef SPC7110_DEBUG
			printf("reading spc7110 ROM (byte) at %06X\n", Address);
#endif
			return S9xGetSPC7110Byte(Address);

		case CMemory::MAP_SPC7110_DRAM:
#ifdef SPC7110_DEBUG
			printf("reading Bank 50 (byte)\n");
#endif
			return S9xGetSPC7110(0x4800);

		case CMemory::MAP_OBC_RAM:
			return GetOBC1(Address & 0xffff);

		case CMemory::MAP_SETA_DSP:
			return S9xGetSetaDSP(Address);
	
		case CMemory::MAP_SETA_RISC:
			return S9xGetST018(Address);


 
		case CMemory::MAP_DEBUG:
 #ifdef DEBUGGER
			printf ("DEBUG R(B) %06x\n", Address);
 #endif
			return OpenBus;

		case CMemory::MAP_BSX:
			return (S9xGetBSX(Address));

		default:
		case CMemory::MAP_NONE:
#ifdef MK_TRACE_BAD_READS
			char address[20];
			sprintf(address, TEXT("%06X"),Address);
			MessageBox(GUI.hWnd, address, TEXT("GetByte"), MB_OK);
#endif

#ifdef DEBUGGER
			printf ("R(B) %06x\n", Address);
#endif
			return OpenBus;
    }
}


uint16 S9xGetWordFromRegister (uint8 *GetAddress, uint32 Address)
{
    switch ((int) GetAddress)
    {
		case CMemory::MAP_PPU:
			return (S9xGetPPU (Address & 0xffff) |
				(S9xGetPPU ((Address + 1) & 0xffff) << 8));

		case CMemory::MAP_CPU:
			return (S9xGetCPU (Address & 0xffff) |
				(S9xGetCPU ((Address + 1) & 0xffff) << 8));

		case CMemory::MAP_DSP:
#ifdef DSP_DUMMY_LOOPS
				printf("Get DSP Word @ %06X\n", Address);
#endif
				return (S9xGetDSP (Address & 0xffff) |
					(S9xGetDSP ((Address + 1) & 0xffff) << 8));

		case CMemory::MAP_SA1RAM:
		case CMemory::MAP_LOROM_SRAM:
			//Address &0x7FFF -offset into bank
			//Address&0xFF0000 -bank
			//bank>>1 | offset = s-ram address, unbound
			//unbound & SRAMMask = Sram offset
			// BJ: no FAST_LSB_WORD_ACCESS here, since if Memory.SRAMMask=0x7ff
			// then the high byte doesn't follow the low byte. 
			return 
				(*(Memory.SRAM + ((((Address&0xFF0000)>>1) |(Address&0x7FFF)) &Memory.SRAMMask)))|
				((*(Memory.SRAM + (((((Address+1)&0xFF0000)>>1) |((Address+1)&0x7FFF)) &Memory.SRAMMask)))<<8);

			//return (*(uint16*)(Memory.SRAM + ((((Address&0xFF0000)>>1)|(Address&0x7FFF)) & Memory.SRAMMask));// |
			//		(*(Memory.SRAM + ((Address + 1) & Memory.SRAMMask)) << 8));
		
		case CMemory::MAP_RONLY_SRAM:
		case CMemory::MAP_HIROM_SRAM:
			// BJ: no FAST_LSB_WORD_ACCESS here, since if Memory.SRAMMask=0x7ff
			// then the high byte doesn't follow the low byte. 
			return (*(Memory.SRAM +
				(((Address & 0x7fff) - 0x6000 +
				((Address & 0xf0000) >> 3)) & Memory.SRAMMask)) |
				(*(Memory.SRAM +
				((((Address + 1) & 0x7fff) - 0x6000 +
				(((Address + 1) & 0xf0000) >> 3)) & Memory.SRAMMask)) << 8));
		
		case CMemory::MAP_BWRAM:
#ifdef FAST_LSB_WORD_ACCESS
			return (*(uint16 *) (Memory.BWRAM + ((Address & 0x7fff) - 0x6000)));
#else
			return (*(Memory.BWRAM + ((Address & 0x7fff) - 0x6000)) |
					(*(Memory.BWRAM + (((Address + 1) & 0x7fff) - 0x6000)) << 8));
#endif
		
		case CMemory::MAP_C4:
			return (S9xGetC4 (Address & 0xffff) |
					(S9xGetC4 ((Address + 1) & 0xffff) << 8));
	
				case CMemory::MAP_SPC7110_ROM:
#ifdef SPC7110_DEBUG
						printf("reading spc7110 ROM (word) at %06X\n", Address);
#endif
						return (S9xGetSPC7110Byte(Address)|
								(S9xGetSPC7110Byte (Address+1))<<8);	
								
		case CMemory::MAP_SPC7110_DRAM:
#ifdef SPC7110_DEBUG
			printf("reading Bank 50 (word)\n");
#endif
			return (S9xGetSPC7110(0x4800)|
				(S9xGetSPC7110 (0x4800) << 8));

		case CMemory::MAP_OBC_RAM:
			return GetOBC1(Address&0xFFFF)| (GetOBC1((Address+1)&0xFFFF)<<8);

		case CMemory::MAP_SETA_DSP:
			return S9xGetSetaDSP(Address)| (S9xGetSetaDSP((Address+1))<<8);
	
		case CMemory::MAP_SETA_RISC:
			return S9xGetST018(Address)| (S9xGetST018((Address+1))<<8);

		case CMemory::MAP_DEBUG:
 #ifdef DEBUGGER
			printf ("DEBUG R(W) %06x\n", Address);
 #endif
			return (OpenBus | (OpenBus<<8));

		case CMemory::MAP_BSX:
			return (S9xGetBSX(Address) | (S9xGetBSX(Address + 1) << 8));

		default:
		case CMemory::MAP_NONE:
#ifdef MK_TRACE_BAD_READS
			char address[20];
			sprintf(address, TEXT("%06X"),Address);
			MessageBox(GUI.hWnd, address, TEXT("GetWord"), MB_OK);
#endif

#ifdef DEBUGGER
			printf ("R(W) %06x\n", Address);
#endif
			return (OpenBus | (OpenBus<<8));
    }
		
}


#define COMPARE_WRITE_SRAM(addr, data)  if (*(addr) != data) { *(addr) = data; CPU.SRAMModified = TRUE; }

void S9xSetByteToRegister (uint8 Byte, uint8* SetAddress, uint32 Address)
{
    switch ((int) SetAddress)
    {
		case CMemory::MAP_PPU:
			S9xSetPPU (Byte, Address & 0xffff);

			return;
		
		case CMemory::MAP_CPU:
			S9xSetCPU (Byte, Address & 0xffff);
			return;
		
		case CMemory::MAP_DSP:
#ifdef DSP_DUMMY_LOOPS
			printf("DSP Byte: %02X to %06X\n", Byte, Address);
#endif
			S9xSetDSP (Byte, Address & 0xffff);
			return;
		
		case CMemory::MAP_LOROM_SRAM:
			if (Memory.SRAMMask)
			{
				/*
				*(Memory.SRAM + ((((Address&0xFF0000)>>1)|(Address&0x7FFF))& Memory.SRAMMask))=Byte;
				// *(Memory.SRAM + (Address & Memory.SRAMMask)) = Byte;
				CPU.SRAMModified = TRUE;
				*/
				COMPARE_WRITE_SRAM(Memory.SRAM + ((((Address&0xFF0000)>>1)|(Address&0x7FFF))& Memory.SRAMMask), Byte);
			}
			return;
		
		case CMemory::MAP_HIROM_SRAM:
			if (Memory.SRAMMask)
			{
				/*
				*(Memory.SRAM + (((Address & 0x7fff) - 0x6000 +
					((Address & 0xf0000) >> 3)) & Memory.SRAMMask)) = Byte;
				CPU.SRAMModified = TRUE;
				*/
				COMPARE_WRITE_SRAM(Memory.SRAM + (((Address & 0x7fff) - 0x6000 + 
					((Address & 0xf0000) >> 3)) & Memory.SRAMMask), Byte);
			}
			return;

		case CMemory::MAP_BWRAM:
			/*
			*(Memory.BWRAM + ((Address & 0x7fff) - 0x6000)) = Byte;
			CPU.SRAMModified = TRUE;
			*/
			COMPARE_WRITE_SRAM(Memory.BWRAM + ((Address & 0x7fff) - 0x6000), Byte);

			return;
		
		case CMemory::MAP_DEBUG:
#ifdef DEBUGGER
			printf ("W(B) %06x\n", Address);
#endif
		
		case CMemory::MAP_SA1RAM:
			*(Memory.SRAM + (Address & 0xffff)) = Byte;
			SA1.Executing = !SA1.Waiting;
			SA1.WaitCounter = 3;
			//if (SA1.Executing)
			//	printf("Write SA1RAM - Wake SA1\n");
			break;
		
		case CMemory::MAP_C4:
			S9xSetC4 (Byte, Address & 0xffff);
			return;
	
		case CMemory::MAP_SPC7110_DRAM:
#ifdef SPC7110_DEBUG
			printf("Writing Byte at %06X\n", Address);
#endif
			// Based on Snes9x 1.52
			//s7r.bank50[(Address & 0xffff)]= (uint8) Byte;
			// Cannot write to $50:xxxx for SPC7110.
			break;
	
		case CMemory::MAP_OBC_RAM:
			SetOBC1(Byte, Address &0xFFFF);
			return;
	
		case CMemory::MAP_SETA_DSP:
			S9xSetSetaDSP(Byte,Address);
			return;

		case CMemory::MAP_SETA_RISC:
			S9xSetST018(Byte,Address);
			return;
    
		case CMemory::MAP_BSX:
			S9xSetBSX(Byte, Address);
			return;

		default:
		case CMemory::MAP_NONE:
#ifdef MK_TRACE_BAD_WRITES
			char address[20];
			sprintf(address, TEXT("%06X"),Address);
			MessageBox(GUI.hWnd, address, TEXT("SetByte"), MB_OK);
#endif

#ifdef DEBUGGER
			printf ("W(B) %06x\n", Address);
#endif
			return;
    }		
}


void S9xSetWordToRegister(uint16 Word, uint8 *SetAddress, uint32 Address)
{
    switch ((int) SetAddress)
    {
		case CMemory::MAP_PPU:

			S9xSetPPU ((uint8) Word, Address & 0xffff);
			S9xSetPPU (Word >> 8, (Address & 0xffff) + 1);

			return;

		case CMemory::MAP_CPU:
			S9xSetCPU ((uint8) Word, (Address & 0xffff));
			S9xSetCPU (Word >> 8, (Address & 0xffff) + 1);
			return;
		
		case CMemory::MAP_DSP:
#ifdef DSP_DUMMY_LOOPS
			printf("DSP Word: %04X to %06X\n", Word, Address);
#endif
			S9xSetDSP ((uint8) Word, (Address & 0xffff));
			S9xSetDSP (Word >> 8, (Address & 0xffff) + 1);
			return;
		
		case CMemory::MAP_LOROM_SRAM:
			if (Memory.SRAMMask)
			{
				/*
				// BJ: no FAST_LSB_WORD_ACCESS here, since if Memory.SRAMMask=0x7ff
				// then the high byte doesn't follow the low byte. 
				*(Memory.SRAM + ((((Address&0xFF0000)>>1)|(Address&0x7FFF))& Memory.SRAMMask)) = (uint8) Word;
				*(Memory.SRAM + (((((Address+1)&0xFF0000)>>1)|((Address+1)&0x7FFF))& Memory.SRAMMask)) = Word >> 8;

				// *(Memory.SRAM + (Address & Memory.SRAMMask)) = (uint8) Word;
				// *(Memory.SRAM + ((Address + 1) & Memory.SRAMMask)) = Word >> 8;
				CPU.SRAMModified = TRUE;
				*/
				COMPARE_WRITE_SRAM(Memory.SRAM + ((((Address&0xFF0000)>>1)|(Address&0x7FFF))& Memory.SRAMMask), (uint8) Word);
				COMPARE_WRITE_SRAM(Memory.SRAM + (((((Address+1)&0xFF0000)>>1)|((Address+1)&0x7FFF))& Memory.SRAMMask), Word >> 8);
			}
			return;
		
		case CMemory::MAP_HIROM_SRAM:
			if (Memory.SRAMMask)
			{
				// Bug fix: The original source has a bug with the bracket placement,
				// causing the SRAMMask to be applied to part of the Address.
				// By fixing the bug, the games that do SRAM checks will work properly.
				// DKC1 works now! 

				// BJ: no FAST_LSB_WORD_ACCESS here, since if Memory.SRAMMask=0x7ff
				// then the high byte doesn't follow the low byte. 
				/*
				*(Memory.SRAM + 
					((((Address & 0x7fff) - 0x6000 +
					((Address & 0xf0000) >> 3)) & Memory.SRAMMask))) = (uint8) Word;
				*(Memory.SRAM + 
					(((((Address + 1) & 0x7fff) - 0x6000 +
					(((Address + 1) & 0xf0000) >> 3)) & Memory.SRAMMask))) = (uint8) (Word >> 8);
				CPU.SRAMModified = TRUE;
				*/
				COMPARE_WRITE_SRAM(Memory.SRAM + 
					((((Address & 0x7fff) - 0x6000 +
					((Address & 0xf0000) >> 3)) & Memory.SRAMMask)), (uint8) Word);
				COMPARE_WRITE_SRAM(Memory.SRAM + 
					(((((Address + 1) & 0x7fff) - 0x6000 +
					(((Address + 1) & 0xf0000) >> 3)) & Memory.SRAMMask)), (uint8) (Word >> 8));
			}
			return;
		
		case CMemory::MAP_BWRAM:
		/*
#ifdef FAST_LSB_WORD_ACCESS
			*(uint16 *) (Memory.BWRAM + ((Address & 0x7fff) - 0x6000)) = Word;
#else
			*(Memory.BWRAM + ((Address & 0x7fff) - 0x6000)) = (uint8) Word;
			*(Memory.BWRAM + (((Address + 1) & 0x7fff) - 0x6000)) = (uint8) (Word >> 8);
#endif
			CPU.SRAMModified = TRUE;
			*/
			COMPARE_WRITE_SRAM((uint16 *) (Memory.BWRAM + ((Address & 0x7fff) - 0x6000)), Word);
			return;
		
		case CMemory::MAP_DEBUG:
#ifdef DEBUGGER
			printf ("W(W) %06x\n", Address);
#endif
	
		case CMemory::MAP_SPC7110_DRAM:
#ifdef SPC7110_DEBUG
			printf("Writing Word at %06X\n", Address);
#endif
			// Based on Snes9x 1.52
			//s7r.bank50[(Address & 0xffff)]= (uint8) Word;
			//s7r.bank50[((Address + 1) & 0xffff)]= (uint8) Word;
			// Cannot write to $50:xxxx for SPC7110.
			break;
			
		case CMemory::MAP_SA1RAM:
			*(Memory.SRAM + (Address & 0xffff)) = (uint8) Word;
			*(Memory.SRAM + ((Address + 1) & 0xffff)) = (uint8) (Word >> 8);
			SA1.Executing = !SA1.Waiting;
			SA1.WaitCounter = 3;
			//if (SA1.Executing)
			//	printf("Write SA1RAM - Wake SA1\n");
			break;
	
		case CMemory::MAP_C4:
			S9xSetC4 (Word & 0xff, Address & 0xffff);
			S9xSetC4 ((uint8) (Word >> 8), (Address + 1) & 0xffff);
			return;

		case CMemory::MAP_OBC_RAM:
			SetOBC1(Word & 0xff, Address &0xFFFF);
			SetOBC1 ((uint8) (Word >> 8), (Address + 1) & 0xffff);
			return;
	
		case CMemory::MAP_SETA_DSP:
			S9xSetSetaDSP (Word & 0xff, Address);
			S9xSetSetaDSP ((uint8) (Word >> 8),(Address + 1));
			return;
	
		case CMemory::MAP_SETA_RISC:
			S9xSetST018 (Word & 0xff, Address);
			S9xSetST018 ((uint8) (Word >> 8),(Address + 1));
			return;

		case CMemory::MAP_BSX:
			S9xSetBSX((uint8) Word, Address);
			S9xSetBSX(Word >> 8, Address + 1);

			return;

		default:
		case CMemory::MAP_NONE:
#ifdef MK_TRACE_BAD_WRITES
			char address[20];
			sprintf(address, TEXT("%06X"),Address);
			MessageBox(GUI.hWnd, address, TEXT("SetWord"), MB_OK);
#endif

#ifdef DEBUGGER
			printf ("W(W) %06x\n", Address);
#endif
			return;
    }		
}

