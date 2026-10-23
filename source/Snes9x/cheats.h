#include "copyright.h"


#ifndef _CHEATS_H_
#define _CHEATS_H_

// Limits shared with the .chx pack generator (snes9x_3ds-assets); change both together.
#define MAX_CHEATS          256
#define CHEAT_CODE_SIZE     256     // codes up to 255 chars
#define CHEAT_NAME_SIZE     250     // names up to 249 chars
#define CHEAT_LINE_SIZE     512     // "N," + code + "," + name + "\r\n\0"
// Up to 28 eight-digit PAR parts fit in a 255-character code.
#define MAX_CHEAT_PARTS     (CHEAT_CODE_SIZE / 9)

struct SCheatPart
{
    uint32  address;
    uint8   byte;
    uint8   saved_byte;
    bool8   saved;
};

// One cheat slot for all '+'-joined parts.
struct SCheat
{
    uint32  num_parts;
    bool8   enabled;
    bool8   game_genie;     // any part is a Game Genie code
    struct SCheatPart parts [MAX_CHEAT_PARTS];
    char    name [CHEAT_NAME_SIZE];

    // Original code for .chx serialization.
    char    cheat_code [CHEAT_CODE_SIZE];
};

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
void S9xAddCheat (bool8 enable, bool8 save_current_value, uint32 address,
		  uint8 byte);
void S9xDeleteCheats ();
void S9xDeleteCheat (uint32 which1);
bool8 S9xLoadCheatTextFile (const char *filename);
bool8 S9xSaveCheatTextFile (const char *filename);

void S9xStartCheatSearch (SCheatData *);
void S9xSearchForChange (SCheatData *, S9xCheatComparisonType cmp,
                         S9xCheatDataSize size, bool8 is_signed, bool8 update);
void S9xSearchForValue (SCheatData *, S9xCheatComparisonType cmp,
                        S9xCheatDataSize size, uint32 value,
                        bool8 is_signed, bool8 update);
void S9xOutputCheatSearchResults (SCheatData *);

#endif

