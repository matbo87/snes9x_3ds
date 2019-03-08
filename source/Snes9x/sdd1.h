#include "copyright.h"


#ifndef _SDD1_H_
#define _SDD1_H_

void S9xSetSDD1MemoryMap (uint32 bank, uint32 value);
void S9xResetSDD1 ();
void S9xSDD1PostLoadState ();
void S9xSDD1SaveLoggedData ();
void S9xSDD1LoadLoggedData ();

#endif

