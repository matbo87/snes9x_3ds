#include "copyright.h"


#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "snes9x.h"
#include "cheats.h"
#include "memmap.h"

#include "3dsfiles.h"

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
    if (Cheat.num_cheats >= MAX_CHEATS)
        return;

    SCheat *cheat = &Cheat.c [Cheat.num_cheats];
    SCheatPart *part = &cheat->parts [0];
    part->address = address;
    part->byte = byte;
    part->saved = save_current_value;
    if (save_current_value)
        // Critical timing bug fix. Using cheats should not interfere
        // with timing cycles.
        //
        part->saved_byte = S9xGetByte (address);

    cheat->num_parts = 1;
    cheat->enabled = TRUE;
    cheat->game_genie = FALSE;
    cheat->name [0] = 0;
    cheat->cheat_code [0] = 0;
    Cheat.num_cheats++;
}

// Parse '+'-joined Game Genie or PAR codes.
static bool8 S9xParseCheatCode (const char *code, SCheat *cheat)
{
    char buf [CHEAT_CODE_SIZE];
    snprintf (buf, sizeof (buf), "%s", code);

    cheat->num_parts = 0;
    cheat->game_genie = FALSE;
    char *save = NULL;
    for (char *token = strtok_r (buf, "+", &save); token; token = strtok_r (NULL, "+", &save))
    {
        if (cheat->num_parts >= MAX_CHEAT_PARTS)
            return FALSE;

        SCheatPart *part = &cheat->parts [cheat->num_parts];
        if (S9xGameGenieToRaw (token, part->address, part->byte) == NULL)
            cheat->game_genie = TRUE;
        else if (S9xProActionReplayToRaw (token, part->address, part->byte) != NULL)
            return FALSE;

        part->saved = FALSE;
        cheat->num_parts++;
    }
    return cheat->num_parts > 0;
}

void S9xDeleteCheat (uint32 which1)
{
    if (which1 < Cheat.num_cheats)
    {
	S9xRemoveCheat (which1);

	memmove (&Cheat.c [which1], &Cheat.c [which1 + 1],
		 sizeof (Cheat.c [0]) * (Cheat.num_cheats - which1 - 1));
	Cheat.num_cheats--;
    }
}

void S9xDeleteCheats ()
{
    S9xRemoveCheats ();
    Cheat.num_cheats = 0;
}

static void S9xCheatWriteByte (uint32 address, uint8 byte)
{
    int block = (address >> MEMMAP_SHIFT) & MEMMAP_MASK;
    uint8 *ptr = Memory.Map [block];

    if (ptr >= (uint8 *) CMemory::MAP_LAST)
        *(ptr + (address & 0xffff)) = byte;
    else
    {
        // Critical timing bug fix. Using cheats should not interfere
        // with timing cycles.
        //
        S9xSetByte (byte, address);
    }
}

// Restore state is independent of the enabled flag.
void S9xRemoveCheat (uint32 which1)
{
    SCheat *cheat = &Cheat.c [which1];

    // Reverse order, so parts sharing an address restore the original byte last.
    for (uint32 i = cheat->num_parts; i-- > 0; )
    {
        SCheatPart *part = &cheat->parts [i];
        if (!part->saved)
            continue;

        S9xCheatWriteByte (part->address, part->saved_byte);
        // Capture a fresh byte on the next apply.
        part->saved = FALSE;
    }
}

void S9xApplyCheat (uint32 which1)
{
    SCheat *cheat = &Cheat.c [which1];

    for (uint32 i = 0; i < cheat->num_parts; i++)
    {
        SCheatPart *part = &cheat->parts [i];

        if (!part->saved)
            // Critical timing bug fix. Using cheats should not interfere
            // with timing cycles.
            //
            part->saved_byte = S9xGetByte (part->address);

        S9xCheatWriteByte (part->address, part->byte);
        part->saved = TRUE;
    }
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

// Restore every saved part in reverse apply order, regardless of enabled flags.
void S9xRemoveCheats ()
{
    for (uint32 i = Cheat.num_cheats; i-- > 0; )
	S9xRemoveCheat (i);
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
    if (filename == nullptr || filename[0] == '\0') {
        return false;
    }

    FILE *fp = file3dsOpen (filename, "w");
    if (fp == NULL)
        return false;

    for (uint32 i = 0; i < Cheat.num_cheats; i++)
    {
        SCheat *cheat = &Cheat.c [i];

        // Raw-address cheats need a PAR code for .chx serialization.
        if (cheat->cheat_code[0] == 0 && cheat->num_parts > 0)
        {
            snprintf(cheat->cheat_code, sizeof (cheat->cheat_code), "%08X", 
                (unsigned int) ((cheat->parts [0].address << 8) + cheat->parts [0].byte));
        }

        fprintf (fp, "%s,%s,%s\n", 
            cheat->enabled ? "Y" : "N",
            cheat->cheat_code,
            cheat->name);
    }

    file3dsClose(fp);
    return true;
}


// This implements the text file format for supporting
// Game Genie and Pro-Action Replay cheats.
//
bool8 S9xLoadCheatTextFile (const char *filename)
{
    if (filename == nullptr || filename[0] == '\0') {
        return false;
    }

    FILE *fp = file3dsOpen (filename, "r");
    if (fp == NULL)
        return false;

    char line[CHEAT_LINE_SIZE];
    char *enabled;
    char *code;
    char *name;

    Cheat.num_cheats = 0;

    while (Cheat.num_cheats < MAX_CHEATS && fgets(line, sizeof(line), fp))
    {
        // Discard the entire oversized line.
        if (strchr(line, '\n') == NULL && !feof(fp))
        {
            int ch;
            while ((ch = fgetc(fp)) != EOF && ch != '\n')
                ;
            continue;
        }

        S9xStripNewLine(line);

        enabled = line;

        code = strchr(line, ',');
        if (code == NULL)
            continue;
        *code = 0; code ++;

        name = strchr(code, ',');
        if (name == NULL)
            continue;
        *name = 0; name++;

        if (strlen(code) >= CHEAT_CODE_SIZE || strlen(name) >= CHEAT_NAME_SIZE)
            continue;

        SCheat *cheat = &Cheat.c [Cheat.num_cheats];
        if (!S9xParseCheatCode(code, cheat))
            continue;

        cheat->enabled = enabled[0] == 'Y' || enabled[0] == 'y';
        snprintf(cheat->name, sizeof(cheat->name), "%s", name);
        snprintf(cheat->cheat_code, sizeof(cheat->cheat_code), "%s", code);
        Cheat.num_cheats++;
    }
    file3dsClose(fp);

    return true;
}
