#include "copyright.h"


#ifndef _SDD1_H_
#define _SDD1_H_

void S9xSetSDD1MemoryMap (uint32 bank, uint32 value);
void S9xResetSDD1 ();
void S9xSDD1PostLoadState ();
void S9xSDD1SaveLoggedData ();
void S9xSDD1LoadLoggedData ();

// Returns the decompressed block, cached by (source, length).
// Decodes into fallback for blocks that cannot be cached.
uint8 *S9xSDD1Decompress (uint8 *in, uint32 len, uint8 *fallback);

#endif

