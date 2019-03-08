#include "copyright.h"


#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "sdd1.h"
#include "3dsimpl.h"

void S9xSetSDD1MemoryMap (uint32 bank, uint32 value)
{
    bank = 0xc00 + bank * 0x100;
    value = value * 1024 * 1024;

    for (int c = 0; c < 0x100; c += 16)
    {
        uint8 *block = &Memory.ROM [value + (c << 12)];
        for (int i = c; i < c + 16; i++)
            Memory.Map [i + bank] = block;
    }
}

void S9xResetSDD1 ()
{
    memset (&Memory.FillRAM [0x4800], 0, 4);
    for (int i = 0; i < 4; i++)
    {
        Memory.FillRAM [0x4804 + i] = i;
        S9xSetSDD1MemoryMap (i, i);
    }
}

void S9xSDD1PostLoadState ()
{
    for (int i = 0; i < 4; i++)
	    S9xSetSDD1MemoryMap (i, Memory.FillRAM [0x4804 + i]);
}

static int S9xCompareSDD1LoggedDataEntries (const void *p1, const void *p2)
{
    uint8 *b1 = (uint8 *) p1;
    uint8 *b2 = (uint8 *) p2;
    uint32 a1 = (*b1 << 16) + (*(b1 + 1) << 8) + *(b1 + 2);
    uint32 a2 = (*b2 << 16) + (*(b2 + 1) << 8) + *(b2 + 2);

    return (a1 - a2);
}
