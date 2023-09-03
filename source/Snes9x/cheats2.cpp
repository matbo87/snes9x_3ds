#include "copyright.h"


#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "snes9x.h"
#include "cheats.h"
#include "memmap.h"

extern SCheatData Cheat;

void S9xInitCheatData ()
{
    Cheat.RAM = Memory.RAM;
    Cheat.SRAM = ::SRAM;
    Cheat.FillRAM = Memory.FillRAM;
}

void S9xAddCheat (bool8 enable, bool8 save_current_value, 
		  uint32 address, uint8 byte)
{
    if (Cheat.num_cheats < sizeof (Cheat.c) / sizeof (Cheat. c [0]))
    {
	Cheat.c [Cheat.num_cheats].address = address;
	Cheat.c [Cheat.num_cheats].byte = byte;
	Cheat.c [Cheat.num_cheats].enabled = TRUE;
	if (save_current_value)
	{
        // Critical timing bug fix. Using cheats should not interfere
        // with timing cycles.
        //
	    Cheat.c [Cheat.num_cheats].saved_byte = S9xGetByte (address);
	    Cheat.c [Cheat.num_cheats].saved = TRUE;
	}
    Cheat.c [Cheat.num_cheats].cheat_code[0] = 0;
	Cheat.num_cheats++;
    }
}

void S9xAddCheatWithCode (bool8 enable, bool8 save_current_value, 
		  uint32 address, uint8 byte, char *code, char *name)
{
    if (Cheat.num_cheats < sizeof (Cheat.c) / sizeof (Cheat. c [0]))
    {
	Cheat.c [Cheat.num_cheats].address = address;
	Cheat.c [Cheat.num_cheats].byte = byte;
	Cheat.c [Cheat.num_cheats].enabled = enable;
	if (save_current_value)
	{
        // Critical timing bug fix. Using cheats should not interfere
        // with timing cycles.
        //
	    Cheat.c [Cheat.num_cheats].saved_byte = S9xGetByte (address);
	    Cheat.c [Cheat.num_cheats].saved = TRUE;
	}
    strncpy(Cheat.c [Cheat.num_cheats].name, name, 64 - 1);
    strncpy(Cheat.c [Cheat.num_cheats].cheat_code, code, 49);
	Cheat.num_cheats++;
    }
}

void S9xDeleteCheat (uint32 which1)
{
    if (which1 < Cheat.num_cheats)
    {
	if (Cheat.c [which1].enabled)
	    S9xRemoveCheat (which1);

	memmove (&Cheat.c [which1], &Cheat.c [which1 + 1],
		 sizeof (Cheat.c [0]) * (Cheat.num_cheats - which1 - 1));
	Cheat.num_cheats--; //MK: This used to set it to 0??
    }
}

void S9xDeleteCheats ()
{
    S9xRemoveCheats ();
    Cheat.num_cheats = 0;
}

void S9xEnableCheat (uint32 which1)
{
    if (which1 < Cheat.num_cheats && !Cheat.c [which1].enabled)
    {
	Cheat.c [which1].enabled = TRUE;
	S9xApplyCheat (which1);
    }
}

void S9xDisableCheat (uint32 which1)
{
    if (which1 < Cheat.num_cheats && Cheat.c [which1].enabled)
    {
	S9xRemoveCheat (which1);
	Cheat.c [which1].enabled = FALSE;
    }
}

void S9xRemoveCheat (uint32 which1)
{
    if (Cheat.c [which1].saved)
    {
        uint32 address = Cheat.c [which1].address;

        int block = (address >> MEMMAP_SHIFT) & MEMMAP_MASK;
        uint8 *ptr = Memory.Map [block];
            
        if (ptr >= (uint8 *) CMemory::MAP_LAST)
            *(ptr + (address & 0xffff)) = Cheat.c [which1].saved_byte;
        else
        {
            // Critical timing bug fix. Using cheats should not interfere
            // with timing cycles.
            //
            S9xSetByte (Cheat.c [which1].saved_byte, address);
        }
    }
}

void S9xApplyCheat (uint32 which1)
{
    uint32 address = Cheat.c [which1].address;

    if (!Cheat.c [which1].saved)
        // Critical timing bug fix. Using cheats should not interfere
        // with timing cycles.
        //
        Cheat.c [which1].saved_byte = S9xGetByte (address);

    int block = (address >> MEMMAP_SHIFT) & MEMMAP_MASK;
    uint8 *ptr = Memory.Map [block];
    
    if (ptr >= (uint8 *) CMemory::MAP_LAST)
	    *(ptr + (address & 0xffff)) = Cheat.c [which1].byte;
    else
    {
        // Critical timing bug fix. Using cheats should not interfere
        // with timing cycles.
        //
	    S9xSetByte (Cheat.c [which1].byte, address);
    }
    Cheat.c [which1].saved = TRUE;
}

void S9xApplyCheats ()
{
    if (Settings.ApplyCheats)
    {
        for (uint32 i = 0; i < Cheat.num_cheats; i++)
            if (Cheat.c [i].enabled)
                S9xApplyCheat (i);
    }
}

void S9xRemoveCheats ()
{
    for (uint32 i = 0; i < Cheat.num_cheats; i++)
	if (Cheat.c [i].enabled)
	    S9xRemoveCheat (i);
}

bool S9xCheatExists(uint32 addr)
{
    for (uint32 i = 0; i < Cheat.num_cheats; i++)
    {
        if (Cheat.c [i].address == addr)
            return true;
    }
    return false;
}

bool8 S9xLoadCheatFile (const char *filename)
{
    Cheat.num_cheats = 0;

    FILE *fs = fopen (filename, "rb");
    uint8 data [28];

    if (!fs)
	return (FALSE);

    while (fread ((void *) data, 1, 28, fs) == 28)
    {
        Cheat.c [Cheat.num_cheats].enabled = (data [0] & 4) == 0;
        Cheat.c [Cheat.num_cheats].byte = data [1];
        Cheat.c [Cheat.num_cheats].address = data [2] | (data [3] << 8) |  (data [4] << 16);
        Cheat.c [Cheat.num_cheats].saved_byte = data [5];
        Cheat.c [Cheat.num_cheats].saved = (data [0] & 8) != 0;
        memmove (Cheat.c [Cheat.num_cheats].name, &data [8], 20);
        
        // shows the actual cheat code in the cheat menu
        snprintf(Cheat.c [Cheat.num_cheats].cheat_code, 9, "%8X", ((Cheat.c [Cheat.num_cheats].address << 8) + Cheat.c [Cheat.num_cheats].byte));
        
        Cheat.c [Cheat.num_cheats++].name [20] = 0;
        if (Cheat.num_cheats >= MAX_CHEATS)
            break;    
    }
    fclose (fs);

    Cheat.text_format = false;

    return (TRUE);
}

bool8 S9xSaveCheatFile (const char *filename)
{
    if (Cheat.text_format)
        return false;

    if (Cheat.num_cheats == 0)
    {
	(void) remove (filename);
	return (TRUE);
    }

    FILE *fs = fopen (filename, "wb");
    uint8 data [28];

    if (!fs)
	return (FALSE);

    uint32 i;
    for (i = 0; i < Cheat.num_cheats; i++)
    {
	memset (data, 0, 28);
	if (i == 0)
	{
	    data [6] = 254;
	    data [7] = 252;
	}
	if (!Cheat.c [i].enabled)
	    data [0] |= 4;

	if (Cheat.c [i].saved)
	    data [0] |= 8;

	data [1] = Cheat.c [i].byte;
	data [2] = (uint8) Cheat.c [i].address;
	data [3] = (uint8) (Cheat.c [i].address >> 8);
	data [4] = (uint8) (Cheat.c [i].address >> 16);
	data [5] = Cheat.c [i].saved_byte;

	memmove (&data [8], Cheat.c [i].name, 19);
	if (fwrite (data, 28, 1, fs) != 1)
	{
	    fclose (fs);
	    return (FALSE);
	}
    }
    return (fclose (fs) == 0);
}



void S9xStripNewLine(char *s)
{
    int len = strlen(s);
    for (int i = 0; i < len; i++)
    {
        if (s[i] == '\n' || s[i] == '\r')
            s[i] = 0;
    }
}


// This implements the text file format for supporting
// Game Genie and Pro-Action Replay cheats.
//
bool8 S9xSaveCheatTextFile (const char *filename)
{
    if (!Cheat.text_format)
        return false;
    
    FILE *fp = fopen (filename, "w");
    if (fp == NULL)
        return false;

    for (uint32 i = 0; i < Cheat.num_cheats; i++)
    {
        // If there's no cheat code, then we compose
        // the PAR cheat code (which is a simple (addr << 8) + byte)
        //
        if (Cheat.c [i].cheat_code[0] == 0)
        {
            snprintf(Cheat.c [i].cheat_code, 9, "%8X", 
                ((Cheat.c [i].address << 8) + Cheat.c [i].byte));
        }

        fprintf (fp, "%s,%s,%s\n", 
            Cheat.c [i].enabled ? "Y" : "N",
            Cheat.c [i].cheat_code,
            Cheat.c [i].name);
    }

    fclose(fp);
    return true;
}


// This implements the text file format for supporting
// Game Genie and Pro-Action Replay cheats.
//
bool8 S9xLoadCheatTextFile (const char *filename)
{
    FILE *fp = fopen (filename, "r");
    if (fp == NULL)
        return false;

    char line[200];
    char *enabled;
    char *code;
    char *name;

    // For sanity reasons.
    //
    S9xDeleteCheats();

    while (!feof(fp))
    {
        uint32 addr;
        uint8 byte;

        fgets(line, 199, fp);
        S9xStripNewLine(line);

        enabled = line;

        char *newline = strchr(line, '\n');

        // Fixed crashing bug when loading CHX files.
        //
        if (newline != NULL)
            *newline = 0;

        code = strchr(line, ',');
        if (code == NULL)
            continue;
        *code = 0; code ++;

        name = strchr(code, ',');
        if (name == NULL)
            continue;
        *name = 0; name++;

        // Try the Game Genie and PAR code formats and see which one is valid
        //
        if (S9xGameGenieToRaw(code, addr, byte) == NULL)
        {
            S9xAddCheatWithCode (
                enabled[0] == 'Y' || enabled[0] == 'y', 
                FALSE, addr, byte, code, name);            
        }
        else if (S9xProActionReplayToRaw(code, addr, byte) == NULL)
        {
            S9xAddCheatWithCode (
                enabled[0] == 'Y' || enabled[0] == 'y', 
                FALSE, addr, byte, code, name);            
        }
    }
    fclose(fp);
    Cheat.text_format = true;

    return true;
}

