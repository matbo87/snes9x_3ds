#include "copyright.h"


#ifndef _memmap_h_
#define _memmap_h_

#include "snes9x.h"

#ifdef FAST_LSB_WORD_ACCESS
#define READ_WORD(s) (*(uint16 *) (s))
#define READ_DWORD(s) (*(uint32 *) (s))
#define WRITE_WORD(s, d) (*(uint16 *) (s)) = (d)
#define WRITE_DWORD(s, d) (*(uint32 *) (s)) = (d)

#define READ_3WORD(s) (0x00ffffff & *(uint32 *) (s))
#define WRITE_3WORD(s, d) *(uint16 *) (s) = (uint16)(d),\
                          *((uint8 *) (s) + 2) = (uint8) ((d) >> 16)


#else
#define READ_WORD(s) ( *(uint8 *) (s) |\
		      (*((uint8 *) (s) + 1) << 8))
#define READ_DWORD(s) ( *(uint8 *) (s) |\
		       (*((uint8 *) (s) + 1) << 8) |\
		       (*((uint8 *) (s) + 2) << 16) |\
		       (*((uint8 *) (s) + 3) << 24))
#define WRITE_WORD(s, d) *(uint8 *) (s) = (d), \
                         *((uint8 *) (s) + 1) = (d) >> 8
#define WRITE_DWORD(s, d) *(uint8 *) (s) = (uint8) (d), \
                          *((uint8 *) (s) + 1) = (uint8) ((d) >> 8),\
                          *((uint8 *) (s) + 2) = (uint8) ((d) >> 16),\
                          *((uint8 *) (s) + 3) = (uint8) ((d) >> 24)
#define WRITE_3WORD(s, d) *(uint8 *) (s) = (uint8) (d), \
                          *((uint8 *) (s) + 1) = (uint8) ((d) >> 8),\
                          *((uint8 *) (s) + 2) = (uint8) ((d) >> 16)
#define READ_3WORD(s) ( *(uint8 *) (s) |\
                       (*((uint8 *) (s) + 1) << 8) |\
                       (*((uint8 *) (s) + 2) << 16))
#endif

#define MEMMAP_BLOCK_SIZE (0x1000)
#define MEMMAP_NUM_BLOCKS (0x1000000 / MEMMAP_BLOCK_SIZE)
#define MEMMAP_BLOCKS_PER_BANK (0x10000 / MEMMAP_BLOCK_SIZE)
#define MEMMAP_SHIFT 12
#define MEMMAP_MASK (MEMMAP_BLOCK_SIZE - 1)
#define MEMMAP_MAX_SDD1_LOGGED_ENTRIES (0x10000 / 8)

//Extended ROM Formats
#define NOPE 0
#define YEAH 1
#define BIGFIRST 2
#define SMALLFIRST 3

//File Formats go here
enum file_formats { FILE_ZIP, FILE_RAR, FILE_DEFAULT };

class CMemory {
public:
    bool8 LoadROM (const char *);
    uint32 FileLoader (uint8* buffer, const char* filename, int32 maxsize);
    void  InitROM (bool8);
    bool8 LoadSRAM (const char *);
    bool8 SaveSRAM (const char *);
    bool8 Init ();
    void  Deinit ();
    void  FreeSDD1Data ();
    
    void WriteProtectROM ();
    void FixROMSpeed ();
    void MapRAM ();
    void MapExtraRAM ();
    char *Safe (const char *);
    
	void BSLoROMMap();
	void JumboLoROMMap (bool8);
    void LoROMMap ();
    void LoROM24MBSMap ();
    void SRAM512KLoROMMap ();
//    void SRAM1024KLoROMMap ();
    void SufamiTurboLoROMMap ();
    void HiROMMap ();
    void SuperFXROMMap ();
    void TalesROMMap (bool8);
    void AlphaROMMap ();
    void SA1ROMMap ();
    void BSHiROMMap ();
	void SPC7110HiROMMap();
	void SPC7110Sram(uint8);
	void SetaDSPMap();
    bool8 AllASCII (uint8 *b, int size);
    int  ScoreHiROM (bool8 skip_header, int32 offset=0);
    int  ScoreLoROM (bool8 skip_header, int32 offset=0);

    uint8 GetByte (uint32 Address);
    bool SpeedHackAdd(int address, int cyclesPerSkip, int16 originalByte1, int16 originalByte2 = -1, int16 originalByte3 = -1, int16 originalByte4 = -1);
    bool SpeedHackSA1Add(int address, int16 originalByte1, int16 originalByte2 = -1, int16 originalByte3 = -1, int16 originalByte4 = -1);
    void ApplySpeedHackPatches();
    void ApplyROMFixes ();
    void CheckForIPSPatch (const char *rom_filename, bool8 header,int32 &rom_size);
    
    const char *TVStandard ();
    const char *Speed ();
    const char *StaticRAMSize ();
    const char *MapType ();
    const char *MapMode ();
    const char *KartContents ();
    const char *Size ();
    const char *Headers ();
    const char *ROMID ();
    const char *CompanyID ();
    void ParseSNESHeader(uint8*);
	enum {
	MAP_PPU, MAP_CPU, MAP_DSP, MAP_LOROM_SRAM, MAP_HIROM_SRAM,
	MAP_NONE, MAP_DEBUG, MAP_C4, MAP_BWRAM, MAP_BWRAM_BITMAP,
	MAP_BWRAM_BITMAP2, MAP_SA1RAM, MAP_SPC7110_ROM, MAP_SPC7110_DRAM,
	MAP_RONLY_SRAM, MAP_OBC_RAM, MAP_SETA_DSP, MAP_SETA_RISC, MAP_BSX, MAP_LAST
    };
    enum { MAX_ROM_SIZE = 0x800000 };
    
    uint8 *RAM;
    uint8 *ROM;
    uint8 *VRAM;
    uint8 *SRAM;
    uint8 *BWRAM;
    uint8 *FillRAM;
    uint8 *C4RAM;
    bool8 HiROM;
    bool8 LoROM;
    uint32 SRAMMask;
    uint8 SRAMSize;
    uint8 *Map [MEMMAP_NUM_BLOCKS];
    uint8 *WriteMap [MEMMAP_NUM_BLOCKS];
    uint8 MemorySpeed [MEMMAP_NUM_BLOCKS];
    uint8 BlockIsRAM [MEMMAP_NUM_BLOCKS];
    uint8 BlockIsROM [MEMMAP_NUM_BLOCKS];
    char  ROMName [ROM_NAME_LEN];
    char  ROMId [5];
    char  CompanyId [3];
    uint8 ROMSpeed;
    uint8 ROMType;
    uint8 ROMSize;
    int32 ROMFramesPerSecond;
    int32 HeaderCount;
    uint32 CalculatedSize;
    uint32 CalculatedChecksum;
    uint32 ROMChecksum;
    uint32 ROMComplementChecksum;
    char ROMFilename [_MAX_PATH];
	uint8 ROMRegion;
    uint32 ROMCRC32;
	uint8 ExtendedFormat;
	uint8 *BSRAM;

    // Added from Snes9x v1.52
    uint8	*BIOSROM;

	void ResetSpeedMap();
    uint32 map_mirror (uint32 size, uint32 pos);
    void map_hirom_offset (uint32 bank_s, uint32 bank_e, uint32 addr_s, uint32 addr_e, uint32 size, uint32 offset);

};

START_EXTERN_C
extern CMemory Memory;
extern uint8 *SRAM;
extern uint8 *ROM;
extern uint8 *RegRAM;
void S9xDeinterleaveMode2 ();

END_EXTERN_C

void S9xAutoSaveSRAM ();

#define INLINE inline
#include "getset.h"


#endif // _memmap_h_

