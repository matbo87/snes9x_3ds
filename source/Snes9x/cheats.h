#include "copyright.h"


#ifndef _CHEATS_H_
#define _CHEATS_H_

struct SCheat
{
    uint32  address;
    uint8   byte;
    uint8   saved_byte;
    bool8   enabled;
    bool8   saved;
    char    name [64];

    // So that we can save the text file format.
    char    cheat_code[50];
};

// Increase the max number of cheats
#define MAX_CHEATS 200

struct SCheatData
{
    struct SCheat   c [MAX_CHEATS];
    uint32	    num_cheats;
    uint8	    CWRAM [0x20000];
    uint8	    CSRAM [0x10000];
    uint8	    CIRAM [0x2000];
    uint8           *RAM;
    uint8           *FillRAM;
    uint8           *SRAM;
    uint32	    WRAM_BITS [0x20000 >> 3];
    uint32	    SRAM_BITS [0x10000 >> 3];
    uint32	    IRAM_BITS [0x2000 >> 3];

    bool8       text_format;
};

typedef enum
{
    S9X_LESS_THAN, S9X_GREATER_THAN, S9X_LESS_THAN_OR_EQUAL,
    S9X_GREATER_THAN_OR_EQUAL, S9X_EQUAL, S9X_NOT_EQUAL
} S9xCheatComparisonType;

typedef enum
{
    S9X_8_BITS, S9X_16_BITS, S9X_24_BITS, S9X_32_BITS
} S9xCheatDataSize;

void S9xInitCheatData ();

const char *S9xGameGenieToRaw (const char *code, uint32 &address, uint8 &byte);
const char *S9xProActionReplayToRaw (const char *code, uint32 &address, uint8 &byte);
const char *S9xGoldFingerToRaw (const char *code, uint32 &address, bool8 &sram,
				uint8 &num_bytes, uint8 bytes[3]);
void S9xApplyCheats ();
void S9xApplyCheat (uint32 which1);
void S9xRemoveCheats ();
void S9xRemoveCheat (uint32 which1);
void S9xEnableCheat (uint32 which1);
void S9xDisableCheat (uint32 which1);
void S9xAddCheat (bool8 enable, bool8 save_current_value, uint32 address,
		  uint8 byte);
void S9xAddCheatWithCode (bool8 enable, bool8 save_current_value, 
		  uint32 address, uint8 byte, char *code);
void S9xDeleteCheats ();
void S9xDeleteCheat (uint32 which1);
bool8 S9xLoadCheatFile (const char *filename);
bool8 S9xLoadCheatTextFile (const char *filename);
bool8 S9xSaveCheatFile (const char *filename);
bool8 S9xSaveCheatTextFile (const char *filename);

void S9xStartCheatSearch (SCheatData *);
void S9xSearchForChange (SCheatData *, S9xCheatComparisonType cmp,
                         S9xCheatDataSize size, bool8 is_signed, bool8 update);
void S9xSearchForValue (SCheatData *, S9xCheatComparisonType cmp,
                        S9xCheatDataSize size, uint32 value,
                        bool8 is_signed, bool8 update);
void S9xOutputCheatSearchResults (SCheatData *);

#endif

