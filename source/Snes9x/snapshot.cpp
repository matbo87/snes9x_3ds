#include "copyright.h"


#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <ctype.h>
#include <stdlib.h>

#if defined(__unix) || defined(__linux) || defined(__sun) || defined(__DJGPP)
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "snapshot.h"

#include "memmap.h"
#include "snes9x.h"
#include "65c816.h"
#include "ppu.h"
#include "cpuexec.h"
#include "apu.h"
#include "soundux.h"
#include "sa1.h"
#include "srtc.h"
#include "sdd1.h"
#include "spc7110.h"
#include "bufferedfilewriter.h"

#include "3dsimpl.h"

extern uint8 *SRAM;

#ifdef ZSNES_FX
START_EXTERN_C
void S9xSuperFXPreSaveState ();
void S9xSuperFXPostSaveState ();
void S9xSuperFXPostLoadState ();
END_EXTERN_C
#endif

typedef struct {
    int offset;
    int size;
    int type;
} FreezeData;

enum {
    INT_V, uint8_ARRAY_V, uint16_ARRAY_V, uint32_ARRAY_V
};

#define Offset(field,structure) \
((int) (((char *) (&(((structure)NULL)->field))) - ((char *) NULL)))

#define COUNT(ARRAY) (sizeof (ARRAY) / sizeof (ARRAY[0]))

struct SnapshotMovieInfo
{
	uint32	MovieInputDataSize;
};

#undef OFFSET
#define OFFSET(f) Offset(f,struct SnapshotMovieInfo *)

static FreezeData SnapMovie [] __attribute__((unused)) = {
    {OFFSET (MovieInputDataSize), 4, INT_V},
};

#undef OFFSET
#define OFFSET(f) Offset(f,struct SCPUState *)

static FreezeData SnapCPU [] = {
    {OFFSET (Flags), 4, INT_V},
    {OFFSET (BranchSkip), 1, INT_V},
    {OFFSET (NMIActive), 1, INT_V},
    {OFFSET (IRQActive), 1, INT_V},
    {OFFSET (WaitingForInterrupt), 1, INT_V},
    {OFFSET (WhichEvent), 1, INT_V},
    {OFFSET (Cycles), 4, INT_V},
    {OFFSET (NextEvent), 4, INT_V},
    {OFFSET (V_Counter), 4, INT_V},
    {OFFSET (MemSpeed), 4, INT_V},
    {OFFSET (MemSpeedx2), 4, INT_V},
    {OFFSET (FastROMSpeed), 4, INT_V}
};

#undef OFFSET
#define OFFSET(f) Offset(f,struct SRegisters *)

static FreezeData SnapRegisters [] = {
    {OFFSET (PB),  1, INT_V},
    {OFFSET (DB),  1, INT_V},
    {OFFSET (P.W), 2, INT_V},
    {OFFSET (A.W), 2, INT_V},
    {OFFSET (D.W), 2, INT_V},
    {OFFSET (S.W), 2, INT_V},
    {OFFSET (X.W), 2, INT_V},
    {OFFSET (Y.W), 2, INT_V},
    {OFFSET (PC),  2, INT_V}
};

#undef OFFSET
#define OFFSET(f) Offset(f,struct SPPU *)

static FreezeData SnapPPU [] = {
    {OFFSET (BGMode), 1, INT_V},
    {OFFSET (BG3Priority), 1, INT_V},
    {OFFSET (Brightness), 1, INT_V},
    {OFFSET (VMA.High), 1, INT_V},
    {OFFSET (VMA.Increment), 1, INT_V},
    {OFFSET (VMA.Address), 2, INT_V},
    {OFFSET (VMA.Mask1), 2, INT_V},
    {OFFSET (VMA.FullGraphicCount), 2, INT_V},
    {OFFSET (VMA.Shift), 2, INT_V},
    {OFFSET (BG[0].SCBase), 2, INT_V},
    {OFFSET (BG[0].VOffset), 2, INT_V},
    {OFFSET (BG[0].HOffset), 2, INT_V},
    {OFFSET (BG[0].BGSize), 1, INT_V},
    {OFFSET (BG[0].NameBase), 2, INT_V},
    {OFFSET (BG[0].SCSize), 2, INT_V},
	
    {OFFSET (BG[1].SCBase), 2, INT_V},
    {OFFSET (BG[1].VOffset), 2, INT_V},
    {OFFSET (BG[1].HOffset), 2, INT_V},
    {OFFSET (BG[1].BGSize), 1, INT_V},
    {OFFSET (BG[1].NameBase), 2, INT_V},
    {OFFSET (BG[1].SCSize), 2, INT_V},
	
    {OFFSET (BG[2].SCBase), 2, INT_V},
    {OFFSET (BG[2].VOffset), 2, INT_V},
    {OFFSET (BG[2].HOffset), 2, INT_V},
    {OFFSET (BG[2].BGSize), 1, INT_V},
    {OFFSET (BG[2].NameBase), 2, INT_V},
    {OFFSET (BG[2].SCSize), 2, INT_V},
	
    {OFFSET (BG[3].SCBase), 2, INT_V},
    {OFFSET (BG[3].VOffset), 2, INT_V},
    {OFFSET (BG[3].HOffset), 2, INT_V},
    {OFFSET (BG[3].BGSize), 1, INT_V},
    {OFFSET (BG[3].NameBase), 2, INT_V},
    {OFFSET (BG[3].SCSize), 2, INT_V},
	
    {OFFSET (CGFLIP), 1, INT_V},
    {OFFSET (CGDATA), 256, uint16_ARRAY_V},
    {OFFSET (FirstSprite), 1, INT_V},
#define O(N) \
    {OFFSET (OBJ[N].HPos), 2, INT_V}, \
    {OFFSET (OBJ[N].VPos), 2, INT_V}, \
    {OFFSET (OBJ[N].Name), 2, INT_V}, \
    {OFFSET (OBJ[N].VFlip), 1, INT_V}, \
    {OFFSET (OBJ[N].HFlip), 1, INT_V}, \
    {OFFSET (OBJ[N].Priority), 1, INT_V}, \
    {OFFSET (OBJ[N].Palette), 1, INT_V}, \
    {OFFSET (OBJ[N].Size), 1, INT_V}
	
    O(  0), O(  1), O(  2), O(  3), O(  4), O(  5), O(  6), O(  7),
    O(  8), O(  9), O( 10), O( 11), O( 12), O( 13), O( 14), O( 15),
    O( 16), O( 17), O( 18), O( 19), O( 20), O( 21), O( 22), O( 23),
    O( 24), O( 25), O( 26), O( 27), O( 28), O( 29), O( 30), O( 31),
    O( 32), O( 33), O( 34), O( 35), O( 36), O( 37), O( 38), O( 39),
    O( 40), O( 41), O( 42), O( 43), O( 44), O( 45), O( 46), O( 47),
    O( 48), O( 49), O( 50), O( 51), O( 52), O( 53), O( 54), O( 55),
    O( 56), O( 57), O( 58), O( 59), O( 60), O( 61), O( 62), O( 63),
    O( 64), O( 65), O( 66), O( 67), O( 68), O( 69), O( 70), O( 71),
    O( 72), O( 73), O( 74), O( 75), O( 76), O( 77), O( 78), O( 79),
    O( 80), O( 81), O( 82), O( 83), O( 84), O( 85), O( 86), O( 87),
    O( 88), O( 89), O( 90), O( 91), O( 92), O( 93), O( 94), O( 95),
    O( 96), O( 97), O( 98), O( 99), O(100), O(101), O(102), O(103),
    O(104), O(105), O(106), O(107), O(108), O(109), O(110), O(111),
    O(112), O(113), O(114), O(115), O(116), O(117), O(118), O(119),
    O(120), O(121), O(122), O(123), O(124), O(125), O(126), O(127),
#undef O
    {OFFSET (OAMPriorityRotation), 1, INT_V},
    {OFFSET (OAMAddr), 2, INT_V},
    {OFFSET (OAMFlip), 1, INT_V},
    {OFFSET (OAMTileAddress), 2, INT_V},
    {OFFSET (IRQVBeamPos), 2, INT_V},
    {OFFSET (IRQHBeamPos), 2, INT_V},
    {OFFSET (VBeamPosLatched), 2, INT_V},
    {OFFSET (HBeamPosLatched), 2, INT_V},
    {OFFSET (HBeamFlip), 1, INT_V},
    {OFFSET (VBeamFlip), 1, INT_V},
    {OFFSET (HVBeamCounterLatched), 1, INT_V},
    {OFFSET (MatrixA), 2, INT_V},
    {OFFSET (MatrixB), 2, INT_V},
    {OFFSET (MatrixC), 2, INT_V},
    {OFFSET (MatrixD), 2, INT_V},
    {OFFSET (CentreX), 2, INT_V},
    {OFFSET (CentreY), 2, INT_V},
    {OFFSET (Joypad1ButtonReadPos), 1, INT_V},
    {OFFSET (Joypad2ButtonReadPos), 1, INT_V},
    {OFFSET (Joypad3ButtonReadPos), 1, INT_V},
    {OFFSET (CGADD), 1, INT_V},
    {OFFSET (FixedColourRed), 1, INT_V},
    {OFFSET (FixedColourGreen), 1, INT_V},
    {OFFSET (FixedColourBlue), 1, INT_V},
    {OFFSET (SavedOAMAddr), 2, INT_V},
    {OFFSET (ScreenHeight), 2, INT_V},
    {OFFSET (WRAM), 4, INT_V},
    {OFFSET (ForcedBlanking), 1, INT_V},
    {OFFSET (OBJNameSelect), 2, INT_V},
    {OFFSET (OBJSizeSelect), 1, INT_V},
    {OFFSET (OBJNameBase), 2, INT_V},
    {OFFSET (OAMReadFlip), 1, INT_V},
    {OFFSET (VTimerEnabled), 1, INT_V},
    {OFFSET (HTimerEnabled), 1, INT_V},
    {OFFSET (HTimerPosition), 2, INT_V},
    {OFFSET (Mosaic), 1, INT_V},
    {OFFSET (Mode7HFlip), 1, INT_V},
    {OFFSET (Mode7VFlip), 1, INT_V},
    {OFFSET (Mode7Repeat), 1, INT_V},
    {OFFSET (Window1Left), 1, INT_V},
    {OFFSET (Window1Right), 1, INT_V},
    {OFFSET (Window2Left), 1, INT_V},
    {OFFSET (Window2Right), 1, INT_V},
#define O(N) \
    {OFFSET (ClipWindowOverlapLogic[N]), 1, INT_V}, \
    {OFFSET (ClipWindow1Enable[N]), 1, INT_V}, \
    {OFFSET (ClipWindow2Enable[N]), 1, INT_V}, \
    {OFFSET (ClipWindow1Inside[N]), 1, INT_V}, \
    {OFFSET (ClipWindow2Inside[N]), 1, INT_V}
	
    O(0), O(1), O(2), O(3), O(4), O(5),
	
#undef O
	
    {OFFSET (CGFLIPRead), 1, INT_V},
    {OFFSET (Need16x8Mulitply), 1, INT_V},
    {OFFSET (BGMosaic), 4, uint8_ARRAY_V},
    {OFFSET (OAMData), 512 + 32, uint8_ARRAY_V},
    {OFFSET (Need16x8Mulitply), 1, INT_V},
    {OFFSET (MouseSpeed), 2, uint8_ARRAY_V}
};

#undef OFFSET
#define OFFSET(f) Offset(f,struct SDMA *)

static FreezeData SnapDMA [] = {
#define O(N) \
    {OFFSET (TransferDirection) + N * sizeof (struct SDMA), 1, INT_V}, \
    {OFFSET (AAddressFixed) + N * sizeof (struct SDMA), 1, INT_V}, \
    {OFFSET (AAddressDecrement) + N * sizeof (struct SDMA), 1, INT_V}, \
    {OFFSET (TransferMode) + N * sizeof (struct SDMA), 1, INT_V}, \
    {OFFSET (ABank) + N * sizeof (struct SDMA), 1, INT_V}, \
    {OFFSET (AAddress) + N * sizeof (struct SDMA), 2, INT_V}, \
    {OFFSET (Address) + N * sizeof (struct SDMA), 2, INT_V}, \
    {OFFSET (BAddress) + N * sizeof (struct SDMA), 1, INT_V}, \
    {OFFSET (TransferBytes) + N * sizeof (struct SDMA), 2, INT_V}, \
    {OFFSET (HDMAIndirectAddressing) + N * sizeof (struct SDMA), 1, INT_V}, \
    {OFFSET (IndirectAddress) + N * sizeof (struct SDMA), 2, INT_V}, \
    {OFFSET (IndirectBank) + N * sizeof (struct SDMA), 1, INT_V}, \
    {OFFSET (Repeat) + N * sizeof (struct SDMA), 1, INT_V}, \
    {OFFSET (LineCount) + N * sizeof (struct SDMA), 1, INT_V}, \
    {OFFSET (FirstLine) + N * sizeof (struct SDMA), 1, INT_V}
	
    O(0), O(1), O(2), O(3), O(4), O(5), O(6), O(7)
#undef O
};

#undef OFFSET
#define OFFSET(f) Offset(f,struct SAPU *)

static FreezeData SnapAPU [] = {
    {OFFSET (Cycles), 4, INT_V},
    {OFFSET (ShowROM), 1, INT_V},
    {OFFSET (Flags), 1, INT_V},
    {OFFSET (KeyedChannels), 1, INT_V},
    {OFFSET (OutPorts), 4, uint8_ARRAY_V},
    {OFFSET (DSP), 0x80, uint8_ARRAY_V},
    {OFFSET (ExtraRAM), 64, uint8_ARRAY_V},
    {OFFSET (Timer), 3, uint16_ARRAY_V},
    {OFFSET (TimerTarget), 3, uint16_ARRAY_V},
    {OFFSET (TimerEnabled), 3, uint8_ARRAY_V},
    {OFFSET (TimerValueWritten), 3, uint8_ARRAY_V}
};

#undef OFFSET
#define OFFSET(f) Offset(f,struct SAPURegisters *)

static FreezeData SnapAPURegisters [] = {
    {OFFSET (P), 1, INT_V},
    {OFFSET (YA.W), 2, INT_V},
    {OFFSET (X), 1, INT_V},
    {OFFSET (S), 1, INT_V},
    {OFFSET (PC), 2, INT_V},
};

#undef OFFSET
#define OFFSET(f) Offset(f,SSoundData *)

static FreezeData SnapSoundData [] = {
    {OFFSET (master_volume_left), 2, INT_V},
    {OFFSET (master_volume_right), 2, INT_V},
    {OFFSET (echo_volume_left), 2, INT_V},
    {OFFSET (echo_volume_right), 2, INT_V},
    {OFFSET (echo_enable), 4, INT_V},
    {OFFSET (echo_feedback), 4, INT_V},
    {OFFSET (echo_ptr), 4, INT_V},
    {OFFSET (echo_buffer_size), 4, INT_V},
    {OFFSET (echo_write_enabled), 4, INT_V},
    {OFFSET (echo_channel_enable), 4, INT_V},
    {OFFSET (pitch_mod), 4, INT_V},
    {OFFSET (dummy), 3, uint32_ARRAY_V},
#define O(N) \
    {OFFSET (channels [N].state), 4, INT_V}, \
    {OFFSET (channels [N].type), 4, INT_V}, \
    {OFFSET (channels [N].volume_left), 2, INT_V}, \
    {OFFSET (channels [N].volume_right), 2, INT_V}, \
    {OFFSET (channels [N].hertz), 4, INT_V}, \
    {OFFSET (channels [N].count), 4, INT_V}, \
    {OFFSET (channels [N].loop), 1, INT_V}, \
    {OFFSET (channels [N].envx), 4, INT_V}, \
    {OFFSET (channels [N].left_vol_level), 2, INT_V}, \
    {OFFSET (channels [N].right_vol_level), 2, INT_V}, \
    {OFFSET (channels [N].envx_target), 2, INT_V}, \
    {OFFSET (channels [N].env_error), 4, INT_V}, \
    {OFFSET (channels [N].erate), 4, INT_V}, \
    {OFFSET (channels [N].direction), 4, INT_V}, \
    {OFFSET (channels [N].attack_rate), 4, INT_V}, \
    {OFFSET (channels [N].decay_rate), 4, INT_V}, \
    {OFFSET (channels [N].sustain_rate), 4, INT_V}, \
    {OFFSET (channels [N].release_rate), 4, INT_V}, \
    {OFFSET (channels [N].sustain_level), 4, INT_V}, \
    {OFFSET (channels [N].sample), 2, INT_V}, \
    {OFFSET (channels [N].decoded), 16, uint16_ARRAY_V}, \
    {OFFSET (channels [N].previous16), 2, uint16_ARRAY_V}, \
    {OFFSET (channels [N].sample_number), 2, INT_V}, \
    {OFFSET (channels [N].last_block), 1, INT_V}, \
    {OFFSET (channels [N].needs_decode), 1, INT_V}, \
    {OFFSET (channels [N].block_pointer), 4, INT_V}, \
    {OFFSET (channels [N].sample_pointer), 4, INT_V}, \
    {OFFSET (channels [N].mode), 4, INT_V}
	
    O(0), O(1), O(2), O(3), O(4), O(5), O(6), O(7)
#undef O
};

#undef OFFSET
#define OFFSET(f) Offset(f,struct SSA1Registers *)

static FreezeData SnapSA1Registers [] = {
    {OFFSET (PB),  1, INT_V},
    {OFFSET (DB),  1, INT_V},
    {OFFSET (P.W), 2, INT_V},
    {OFFSET (A.W), 2, INT_V},
    {OFFSET (D.W), 2, INT_V},
    {OFFSET (S.W), 2, INT_V},
    {OFFSET (X.W), 2, INT_V},
    {OFFSET (Y.W), 2, INT_V},
    {OFFSET (PC),  2, INT_V}
};

#undef OFFSET
#define OFFSET(f) Offset(f,struct SSA1 *)

static FreezeData SnapSA1 [] = {
    {OFFSET (Flags), 4, INT_V},
    {OFFSET (NMIActive), 1, INT_V},
    {OFFSET (IRQActive), 1, INT_V},
    {OFFSET (WaitingForInterrupt), 1, INT_V},
    {OFFSET (op1), 2, INT_V},
    {OFFSET (op2), 2, INT_V},
    {OFFSET (arithmetic_op), 4, INT_V},
    {OFFSET (sum), 8, INT_V},
    {OFFSET (overflow), 1, INT_V}
};

#undef OFFSET
#define OFFSET(f) Offset(f,struct SPC7110EmuVars *)

static FreezeData SnapSPC7110 [] = {
    {OFFSET (reg4800), 1, INT_V},
    {OFFSET (reg4801), 1, INT_V},
    {OFFSET (reg4802), 1, INT_V},
    {OFFSET (reg4803), 1, INT_V},
    {OFFSET (reg4804), 1, INT_V},
    {OFFSET (reg4805), 1, INT_V},
    {OFFSET (reg4806), 1, INT_V},
    {OFFSET (reg4807), 1, INT_V},
    {OFFSET (reg4808), 1, INT_V},
    {OFFSET (reg4809), 1, INT_V},
    {OFFSET (reg480A), 1, INT_V},
    {OFFSET (reg480B), 1, INT_V},
    {OFFSET (reg480C), 1, INT_V},
    {OFFSET (reg4811), 1, INT_V},
    {OFFSET (reg4812), 1, INT_V},
    {OFFSET (reg4813), 1, INT_V},
    {OFFSET (reg4814), 1, INT_V},
    {OFFSET (reg4815), 1, INT_V},
    {OFFSET (reg4816), 1, INT_V},
    {OFFSET (reg4817), 1, INT_V},
    {OFFSET (reg4818), 1, INT_V},
    {OFFSET (reg4820), 1, INT_V},
    {OFFSET (reg4821), 1, INT_V},
    {OFFSET (reg4822), 1, INT_V},
    {OFFSET (reg4823), 1, INT_V},
    {OFFSET (reg4824), 1, INT_V},
    {OFFSET (reg4825), 1, INT_V},
    {OFFSET (reg4826), 1, INT_V},
    {OFFSET (reg4827), 1, INT_V},
    {OFFSET (reg4828), 1, INT_V},
    {OFFSET (reg4829), 1, INT_V},
    {OFFSET (reg482A), 1, INT_V},
    {OFFSET (reg482B), 1, INT_V},
    {OFFSET (reg482C), 1, INT_V},
    {OFFSET (reg482D), 1, INT_V},
    {OFFSET (reg482E), 1, INT_V},
    {OFFSET (reg482F), 1, INT_V},
    {OFFSET (reg4830), 1, INT_V},
    {OFFSET (reg4831), 1, INT_V},
    {OFFSET (reg4832), 1, INT_V},
    {OFFSET (reg4833), 1, INT_V},
    {OFFSET (reg4834), 1, INT_V},
    {OFFSET (reg4840), 1, INT_V},
    {OFFSET (reg4841), 1, INT_V},
    {OFFSET (reg4842), 1, INT_V},
    {OFFSET (AlignBy), 1, INT_V},
    {OFFSET (written), 1, INT_V},
    {OFFSET (offset_add), 1, INT_V},
    {OFFSET (DataRomOffset), 4, INT_V},
    {OFFSET (DataRomSize), 4, INT_V},
    {OFFSET (bank50Internal), 4, INT_V},
	{OFFSET (bank50), 0x10000, uint8_ARRAY_V}
};

#undef OFFSET
#define OFFSET(f) Offset(f,struct SPC7110RTC *)

static FreezeData SnapS7RTC [] = {
    {OFFSET (reg), 16, uint8_ARRAY_V},
    {OFFSET (index), 2, INT_V},
    {OFFSET (control), 1, INT_V},
    {OFFSET (init), 1, INT_V},
	{OFFSET (last_used),4,INT_V}
};

static char ROMFilename [_MAX_PATH];

void FreezeStruct (BufferedFileWriter& stream, const char *name, void *base, FreezeData *fields,
				   int num_fields);
void FreezeBlock (BufferedFileWriter& stream, const char *name, uint8 *block, int size);

int UnfreezeStruct (STREAM stream, const char *name, void *base, FreezeData *fields,
					int num_fields);
int UnfreezeBlock (STREAM stream, const char *name, uint8 *block, int size);

bool8 Snapshot (const char *filename)
{
    return (S9xFreezeGame (filename));
}

bool8 S9xFreezeGame (const char *filename)
{
    BufferedFileWriter stream;

    if (stream.open(filename, "wb"))
    {
        S9xPrepareSoundForSnapshotSave (FALSE);
        
        S9xFreezeToStream (stream);

        // we do this manually here so it happens while sound is still muted
        // to avoid audio to crackle or stutter
        stream.close(); 

        S9xPrepareSoundForSnapshotSave (TRUE);
        
        return (TRUE);
    }
    return (FALSE);
}

bool8 S9xLoadSnapshot (const char *filename)
{
    return (S9xUnfreezeGame (filename));
}

bool8 S9xUnfreezeGame (const char *filename)
{
	
    STREAM snapshot = NULL;
    if (S9xOpenSnapshotFile (filename, TRUE, &snapshot))
    {
		int result;
		if ((result = S9xUnfreezeFromStream (snapshot)) != SUCCESS)
		{
			switch (result)
			{
			case WRONG_FORMAT:
				S9xMessage (S9X_ERROR, S9X_WRONG_FORMAT, 
					"File not in Snes9x freeze format");
				break;
			case WRONG_VERSION:
				S9xMessage (S9X_ERROR, S9X_WRONG_VERSION,
					"Incompatable Snes9x freeze file format version");
				break;
			case WRONG_MOVIE_SNAPSHOT:
				S9xMessage (S9X_ERROR, S9X_WRONG_MOVIE_SNAPSHOT, "MOVIE_ERR_SNAPSHOT_WRONG_MOVIE");
				break;
			case NOT_A_MOVIE_SNAPSHOT:
				S9xMessage (S9X_ERROR, S9X_NOT_A_MOVIE_SNAPSHOT, "MOVIE_ERR_SNAPSHOT_NOT_MOVIE");
				break;
			default:
			case FILE_NOT_FOUND:
				snprintf (String, sizeof (String), "ROM image for freeze file not found: \"%s\"",
					ROMFilename);
				S9xMessage (S9X_ERROR, S9X_ROM_NOT_FOUND, String);
				break;
			}
			S9xCloseSnapshotFile (snapshot);
			return (FALSE);
		}

		/*if(!S9xMovieActive())
		{
			sprintf(String, "Loaded %s", S9xBasename (filename));
			S9xMessage (S9X_INFO, S9X_FREEZE_FILE_INFO, String);
		}*/

		S9xCloseSnapshotFile (snapshot);
		return (TRUE);
    }
    return (FALSE);
}

void S9xFreezeToStream (BufferedFileWriter& stream)
{
    char buffer [1024];
    int i;
	
    S9xSetSoundMute (TRUE);
	
	S9xUpdateRTC();
    S9xSRTCPreSaveState ();
	
    for (i = 0; i < 8; i++)
    {
		SoundData.channels [i].previous16 [0] = (int16) SoundData.channels [i].previous [0];
		SoundData.channels [i].previous16 [1] = (int16) SoundData.channels [i].previous [1];
    }
    int printed = snprintf (buffer, sizeof(buffer), "%s:%04d\n", SNAPSHOT_MAGIC, SNAPSHOT_VERSION);
    if (printed < 0)
        printed = 0;
    else if (printed >= (int) sizeof(buffer))
        printed = sizeof(buffer) - 1;
    stream.write(buffer, printed);
    printed = snprintf (buffer, sizeof(buffer), "NAM:%06d:%s%c", strlen (Memory.ROMFilename) + 1,
		Memory.ROMFilename, 0);
    if (printed < 0)
        printed = 0;
    else if (printed >= (int) sizeof(buffer))
        printed = sizeof(buffer) - 1;
    stream.write(buffer, printed);
    FreezeStruct (stream, "CPU", &CPU, SnapCPU, COUNT (SnapCPU));
    FreezeStruct (stream, "REG", &Registers, SnapRegisters, COUNT (SnapRegisters));
    FreezeStruct (stream, "PPU", &PPU, SnapPPU, COUNT (SnapPPU));
    FreezeStruct (stream, "DMA", DMA, SnapDMA, COUNT (SnapDMA));

	// RAM and VRAM
    FreezeBlock (stream, "VRA", Memory.VRAM, 0x10000);
    FreezeBlock (stream, "RAM", Memory.RAM, 0x20000);
    FreezeBlock (stream, "SRA", ::SRAM, 0x20000);
    FreezeBlock (stream, "FIL", Memory.FillRAM, 0x8000);
    if (Settings.APUEnabled)
    {
		// APU
		FreezeStruct (stream, "APU", &APU, SnapAPU, COUNT (SnapAPU));
		FreezeStruct (stream, "ARE", &APURegisters, SnapAPURegisters,
			COUNT (SnapAPURegisters));
		FreezeBlock (stream, "ARA", IAPU.RAM, 0x10000);
		FreezeStruct (stream, "SOU", &SoundData, SnapSoundData,
			COUNT (SnapSoundData));
    }
    if (Settings.SA1)
    {
		SA1Registers.PC = SA1.PC - SA1.PCBase;
		S9xSA1PackStatus ();
		FreezeStruct (stream, "SA1", &SA1, SnapSA1, COUNT (SnapSA1));
		FreezeStruct (stream, "SAR", &SA1Registers, SnapSA1Registers, 
			COUNT (SnapSA1Registers));
    }
	
	if (Settings.SPC7110)
    {
		S9xSpc7110PreSaveState();
		FreezeStruct (stream, "SP7", &s7r, SnapSPC7110, COUNT (SnapSPC7110));
    }
	if(Settings.SPC7110RTC)
	{
		FreezeStruct (stream, "RTC", &rtc_f9, SnapS7RTC, COUNT (SnapS7RTC));
	}

	S9xSetSoundMute (FALSE);
}

int S9xUnfreezeFromStream (STREAM stream)
{
    char buffer [_MAX_PATH + 1];
    char rom_filename [_MAX_PATH + 1];
    int result;
	
    int version;
    size_t len = strlen (SNAPSHOT_MAGIC) + 1 + 4 + 1;
    if (READ_STREAM (buffer, len, stream) != len)
		return (WRONG_FORMAT);
    if (strncmp (buffer, SNAPSHOT_MAGIC, strlen (SNAPSHOT_MAGIC)) != 0)
		return (WRONG_FORMAT);
    if ((version = atoi (&buffer [strlen (SNAPSHOT_MAGIC) + 1])) > SNAPSHOT_VERSION)
		return (WRONG_VERSION);
	
    if ((result = UnfreezeBlock (stream, "NAM", (uint8 *) rom_filename, _MAX_PATH)) != SUCCESS)
		return (result);
	
    if (strcasecmp (rom_filename, Memory.ROMFilename) != 0 &&
		strcasecmp (S9xBasename (rom_filename), S9xBasename (Memory.ROMFilename)) != 0)
    {
		S9xMessage (S9X_WARNING, S9X_FREEZE_ROM_NAME,
			"Current loaded ROM image doesn't match that required by freeze-game file.");
    }

    // Validate the first data block header (CPU) before committing to reset.
    // Full header shape: "CPU:" + 6 ASCII digits + ":"
    // This catches truncated or corrupt files without losing the current game state.
    {
        long pos = FIND_STREAM(stream);
        char peek[11];
        if (READ_STREAM(peek, 11, stream) != 11)
            return (WRONG_FORMAT);
        REVERT_STREAM(stream, pos, 0);

        if (strncmp(peek, "CPU:", 4) != 0 || peek[10] != ':')
            return (WRONG_FORMAT);

        // Verify the 6-digit size field contains only digits and is > 0
        int blockLen = 0;
        for (int i = 4; i < 10; i++) {
            if (peek[i] < '0' || peek[i] > '9')
                return (WRONG_FORMAT);
            blockLen = blockLen * 10 + (peek[i] - '0');
        }
        if (blockLen == 0)
            return (WRONG_FORMAT);
    }

    uint32 old_flags = CPU.Flags;
    uint32 sa1_old_flags = SA1.Flags;

    S9xReset ();
    S9xSetSoundMute (TRUE);

    // track if APU data was successfully loaded
    bool apuLoaded = false; 

    do
    {
        if ((result = UnfreezeStruct (stream, "CPU", &CPU, SnapCPU, COUNT (SnapCPU))) != SUCCESS) break;
        if ((result = UnfreezeStruct (stream, "REG", &Registers, SnapRegisters, COUNT (SnapRegisters))) != SUCCESS) break;
        if ((result = UnfreezeStruct (stream, "PPU", &PPU, SnapPPU, COUNT (SnapPPU))) != SUCCESS) break;
        if ((result = UnfreezeStruct (stream, "DMA", DMA, SnapDMA, COUNT (SnapDMA))) != SUCCESS) break;
        
        // Load big blocks directly into target memory
        if ((result = UnfreezeBlock (stream, "VRA", Memory.VRAM, 0x10000)) != SUCCESS) break;
        Mode7IdxInvalidate();
        if ((result = UnfreezeBlock (stream, "RAM", Memory.RAM, 0x20000)) != SUCCESS) break;
        if ((result = UnfreezeBlock (stream, "SRA", ::SRAM, 0x20000)) != SUCCESS) break;
        if ((result = UnfreezeBlock (stream, "FIL", Memory.FillRAM, 0x8000)) != SUCCESS) break;

        if (UnfreezeStruct (stream, "APU", &APU, SnapAPU, COUNT (SnapAPU)) == SUCCESS)
        {
            if ((result = UnfreezeStruct (stream, "ARE", &APURegisters, SnapAPURegisters, COUNT (SnapAPURegisters))) != SUCCESS) break;
            if ((result = UnfreezeBlock (stream, "ARA", IAPU.RAM, 0x10000)) != SUCCESS) break;
            if ((result = UnfreezeStruct (stream, "SOU", &SoundData, SnapSoundData, COUNT (SnapSoundData))) != SUCCESS) break;
            
            apuLoaded = true;
        }
        else
        {
            Settings.APUEnabled = FALSE;
            IAPU.APUExecuting = FALSE;
            S9xSetSoundMute (TRUE);
        }

        if ((result = UnfreezeStruct (stream, "SA1", &SA1, SnapSA1, COUNT(SnapSA1))) == SUCCESS)
        {
            if ((result = UnfreezeStruct (stream, "SAR", &SA1Registers, SnapSA1Registers, COUNT (SnapSA1Registers))) != SUCCESS) break;
            S9xFixSA1AfterSnapshotLoad ();
            SA1.Flags |= sa1_old_flags & (TRACE_FLAG);
        }
        
        if ((result = UnfreezeStruct (stream, "SP7", &s7r, SnapSPC7110, COUNT(SnapSPC7110))) != SUCCESS)
        {
            if(Settings.SPC7110) S9xSpc7110PostLoadState(); // Soft fail logic
        }
        
        if ((result = UnfreezeStruct (stream, "RTC", &rtc_f9, SnapS7RTC, COUNT (SnapS7RTC))) != SUCCESS)
        {
            if(Settings.SPC7110RTC) break;
        }

        if (Settings.SPC7110RTC) S9xUpdateRTC();

        result = SUCCESS;

    } while(false);

    // If load failed, we stop here. The system was reset() at the start, so it's in a safe "blank" state.
    if (result != SUCCESS) {
        return result;
    }

    // Post-Load Fixes
    Memory.FixROMSpeed ();
    CPU.Flags |= old_flags & (DEBUG_MODE_FLAG | TRACE_FLAG | SINGLE_STEP_FLAG | FRAME_ADVANCE_FLAG);
    IPPU.ColorsChanged = TRUE;
    IPPU.OBJChanged = TRUE;
    CPU.InDMA = FALSE;
    S9xFixColourBrightness ();
    IPPU.RenderThisFrame = FALSE;

    // fix sound pointers BEFORE enabling APU
    S9xFixSoundAfterSnapshotLoad (1);

    if (apuLoaded) 
    {
        S9xSetSoundMute (FALSE);
        IAPU.PC = IAPU.RAM + APURegisters.PC;
        S9xAPUUnpackStatus ();
        IAPU.DirectPage = APUCheckDirectPage () ? IAPU.RAM + 0x100 : IAPU.RAM;
        Settings.APUEnabled = TRUE;
        IAPU.APUExecuting = TRUE;
    }

    uint8 hdma_byte = Memory.FillRAM[0x420c];
    S9xSetCPU(hdma_byte, 0x420c);

    if(!Memory.FillRAM[0x4213]){
			// most likely an old savestate
        Memory.FillRAM[0x4213]=Memory.FillRAM[0x4201];
			if(!Memory.FillRAM[0x4213])
				Memory.FillRAM[0x4213]=Memory.FillRAM[0x4201]=0xFF;
    }

		// Copy the DSP data to the copy used for reading.
		//
    for (int i = 0; i < 0x80; i++)
        IAPU.DSPCopy[i] = APU.DSP[i];

    ICPU.ShiftedPB = Registers.PB << 16;
    ICPU.ShiftedDB = Registers.DB << 16;
    S9xSetPCBase (ICPU.ShiftedPB + Registers.PC);
    S9xUnpackStatus ();
    S9xFixCycles ();

#ifdef ZSNES_FX
    if (Settings.SuperFX)
        S9xSuperFXPostLoadState ();
#endif
    
    S9xSRTCPostLoadState ();
    if (Settings.SDD1)
        S9xSDD1PostLoadState ();
        
    IAPU.NextAPUTimerPos = CPU.Cycles * 1000L;
    IAPU.NextAPUTimerPosDiv10000 = IAPU.NextAPUTimerPos / 1000;
    IAPU.APUTimerCounter = 0; 

    S9xInitializeVerticalSections();

    return (result);
}

int FreezeSize (int size, int type)
{
    switch (type)
    {
    case uint16_ARRAY_V:
		return (size * 2);
    case uint32_ARRAY_V:
		return (size * 4);
    default:
		return (size);
    }
}

void FreezeStruct (BufferedFileWriter& stream, const char *name, void *base, FreezeData *fields,
				   int num_fields)
{
    // Work out the size of the required block
    int len = 0;
    int i;
    int j;
	
    for (int i = 0; i < num_fields; i++) {
        int l = fields[i].offset + FreezeSize(fields[i].size, fields[i].type);
        if (l > len) len = l;
    }
	
	uint8 stackBuf[2048];
    uint8 *block = stackBuf;
    bool allocated = false;
    
    if (len > (int)sizeof(stackBuf)) {
        block = new uint8[len];
        allocated = true;
    }

    uint8 *ptr = block;
    uint16 word;
    uint32 dword;
    int64  qword;
	
    // Build the block ready to be streamed out
    for (i = 0; i < num_fields; i++)
    {
		switch (fields [i].type)
		{
		case INT_V:
			switch (fields [i].size)
			{
			case 1:
				*ptr++ = *((uint8 *) base + fields [i].offset);
				break;
			case 2:
				word = *((uint16 *) ((uint8 *) base + fields [i].offset));
				*ptr++ = (uint8) (word >> 8);
				*ptr++ = (uint8) word;
				break;
			case 4:
				dword = *((uint32 *) ((uint8 *) base + fields [i].offset));
				*ptr++ = (uint8) (dword >> 24);
				*ptr++ = (uint8) (dword >> 16);
				*ptr++ = (uint8) (dword >> 8);
				*ptr++ = (uint8) dword;
				break;
			case 8:
				qword = *((int64 *) ((uint8 *) base + fields [i].offset));
				*ptr++ = (uint8) (qword >> 56);
				*ptr++ = (uint8) (qword >> 48);
				*ptr++ = (uint8) (qword >> 40);
				*ptr++ = (uint8) (qword >> 32);
				*ptr++ = (uint8) (qword >> 24);
				*ptr++ = (uint8) (qword >> 16);
				*ptr++ = (uint8) (qword >> 8);
				*ptr++ = (uint8) qword;
				break;
			}
			break;
			case uint8_ARRAY_V:
				memcpy (ptr, (uint8 *) base + fields [i].offset, fields [i].size);
				ptr += fields [i].size;
				break;
			case uint16_ARRAY_V:
				for (j = 0; j < fields [i].size; j++)
				{
					word = *((uint16 *) ((uint8 *) base + fields [i].offset + j * 2));
					*ptr++ = (uint8) (word >> 8);
					*ptr++ = (uint8) word;
				}
				break;
			case uint32_ARRAY_V:
				for (j = 0; j < fields [i].size; j++)
				{
					dword = *((uint32 *) ((uint8 *) base + fields [i].offset + j * 4));
					*ptr++ = (uint8) (dword >> 24);
					*ptr++ = (uint8) (dword >> 16);
					*ptr++ = (uint8) (dword >> 8);
					*ptr++ = (uint8) dword;
				}
				break;
		}
    }

	FreezeBlock (stream, name, block, len);
    if (allocated) delete[] block;
}

void FreezeBlock (BufferedFileWriter& stream, const char *name, uint8 *block, int size)
{
    char buffer[16];
    int printed = sprintf(buffer, "%s:%06d:", name, size);
    stream.write(buffer, printed);
    stream.write(block, size);
}

int UnfreezeStruct (STREAM stream, const char *name, void *base, FreezeData *fields,
					int num_fields)
{
    // Work out the size of the required block
    int len = 0;
    int i;
    int j;

	for (int i = 0; i < num_fields; i++) {
        int l = fields[i].offset + FreezeSize(fields[i].size, fields[i].type);
        if (l > len) len = l;
    }

	uint8 stackBuf[2048];
    uint8 *block = stackBuf;
    bool allocated = false;
    
    if (len > (int)sizeof(stackBuf)) {
        block = new uint8[len];
        allocated = true;
    }
	
    uint8 *ptr = block;
    uint16 word;
    uint32 dword;
    int64  qword;
    int result;
	
    if ((result = UnfreezeBlock (stream, name, block, len)) != SUCCESS)
    {
        if (allocated) delete[] block;
        return (result);
    }
	
    // Unpack the block of data into a C structure
    for (i = 0; i < num_fields; i++)
    {
		switch (fields [i].type)
		{
		case INT_V:
			switch (fields [i].size)
			{
			case 1:
				*((uint8 *) base + fields [i].offset) = *ptr++;
				break;
			case 2:
				word  = *ptr++ << 8;
				word |= *ptr++;
				*((uint16 *) ((uint8 *) base + fields [i].offset)) = word;
				break;
			case 4:
				dword  = *ptr++ << 24;
				dword |= *ptr++ << 16;
				dword |= *ptr++ << 8;
				dword |= *ptr++;
				*((uint32 *) ((uint8 *) base + fields [i].offset)) = dword;
				break;
			case 8:
				qword  = (int64) *ptr++ << 56;
				qword |= (int64) *ptr++ << 48;
				qword |= (int64) *ptr++ << 40;
				qword |= (int64) *ptr++ << 32;
				qword |= (int64) *ptr++ << 24;
				qword |= (int64) *ptr++ << 16;
				qword |= (int64) *ptr++ << 8;
				qword |= (int64) *ptr++;
				*((int64 *) ((uint8 *) base + fields [i].offset)) = qword;
				break;
			}
			break;
			case uint8_ARRAY_V:
				memcpy ((uint8 *) base + fields [i].offset, ptr, fields [i].size);
				ptr += fields [i].size;
				break;
			case uint16_ARRAY_V:
				for (j = 0; j < fields [i].size; j++)
				{
					word  = *ptr++ << 8;
					word |= *ptr++;
					*((uint16 *) ((uint8 *) base + fields [i].offset + j * 2)) = word;
				}
				break;
			case uint32_ARRAY_V:
				for (j = 0; j < fields [i].size; j++)
				{
					dword  = *ptr++ << 24;
					dword |= *ptr++ << 16;
					dword |= *ptr++ << 8;
					dword |= *ptr++;
					*((uint32 *) ((uint8 *) base + fields [i].offset + j * 4)) = dword;
				}
				break;
		}
    }

	if (allocated) delete [] block;
    return (result);
}

int UnfreezeBlock (STREAM stream, const char *name, uint8 *block, int size)
{
    char buffer [20];
    int len = 0;
    int rem = 0;
    int rew_len;
    if (READ_STREAM (buffer, 11, stream) != 11 ||
		strncmp (buffer, name, 3) != 0 || buffer [3] != ':' ||
		(len = atoi (&buffer [4])) == 0)
    {
		REVERT_STREAM(stream, FIND_STREAM(stream)-11, 0);
		return (WRONG_FORMAT);
    }

    if (len > size)
    {
        rem = len - size;
        len = size;
    }
    
    if ((rew_len = READ_STREAM (block, len, stream)) != len)
    {
        REVERT_STREAM(stream, FIND_STREAM(stream)-11-rew_len, 0);
        return (WRONG_FORMAT);
    }
    
    if (rem)
    {
        fseek(stream, rem, SEEK_CUR);
    }
    
    return (SUCCESS);
}


extern uint8 spc_dump_dsp[0x100];

bool8 S9xSPCDump (const char *filename)
{
    static uint8 header [] = {
		'S', 'N', 'E', 'S', '-', 'S', 'P', 'C', '7', '0', '0', ' ',
			'S', 'o', 'u', 'n', 'd', ' ', 'F', 'i', 'l', 'e', ' ',
			'D', 'a', 't', 'a', ' ', 'v', '0', '.', '3', '0', 26, 26, 26
    };
    static uint8 version = {
		0x1e
    };
	
    FILE *fs;
	
    S9xSetSoundMute (TRUE);
	
    if (!(fs = file3dsOpen (filename, "wb")))
		return (FALSE);
	
    // The SPC file format:
    // 0000: header:	'SNES-SPC700 Sound File Data v0.30',26,26,26
    // 0036: version:	$1e
    // 0037: SPC700 PC:
    // 0039: SPC700 A:
    // 0040: SPC700 X:
    // 0041: SPC700 Y:
    // 0042: SPC700 P:
    // 0043: SPC700 S:
    // 0044: Reserved: 0, 0, 0, 0
    // 0048: Title of game: 32 bytes
    // 0000: Song name: 32 bytes
    // 0000: Name of dumper: 32 bytes
    // 0000: Comments: 32 bytes
    // 0000: Date of SPC dump: 4 bytes
    // 0000: Fade out time in milliseconds: 4 bytes
    // 0000: Fade out length in milliseconds: 2 bytes
    // 0000: Default channel enables: 1 bytes
    // 0000: Emulator used to dump .SPC files: 1 byte, 1 == ZSNES
    // 0000: Reserved: 36 bytes
    // 0256: SPC700 RAM: 64K
    // ----: DSP Registers: 256 bytes
	
    if (fwrite (header, sizeof (header), 1, fs) != 1 ||
		fputc (version, fs) == EOF ||
		fseek (fs, 37, SEEK_SET) == EOF ||
		fputc (APURegisters.PC & 0xff, fs) == EOF ||
		fputc (APURegisters.PC >> 8, fs) == EOF ||
		fputc (APURegisters.YA.B.A, fs) == EOF ||
		fputc (APURegisters.X, fs) == EOF ||
		fputc (APURegisters.YA.B.Y, fs) == EOF ||
		fputc (APURegisters.P, fs) == EOF ||
		fputc (APURegisters.S, fs) == EOF ||
		fseek (fs, 256, SEEK_SET) == EOF ||
		fwrite (IAPU.RAM, 0x10000, 1, fs) != 1 ||
		fwrite (spc_dump_dsp, 1, 256, fs) != 256 ||
		fwrite (APU.ExtraRAM, 64, 1, fs) != 1 ||
		file3dsClose (fs) < 0)
    {
		S9xSetSoundMute (FALSE);
		return (FALSE);
    }
    S9xSetSoundMute (FALSE);
    return (TRUE);
}
