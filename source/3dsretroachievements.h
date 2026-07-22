#ifndef _3DSRETROACHIEVEMENTS_H
#define _3DSRETROACHIEVEMENTS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
void ra3dsInitialize(void);
void ra3dsFinalize(void);

// Called after a ROM has been fully loaded into the SNES memory map.
// Computes the RA hash and identifies the game.
void ra3dsLoadGame(void);

// Called before tearing down / replacing the current ROM.
void ra3dsUnloadGame(void);

// Called on SNES reset (S9xReset)
void ra3dsReset(void);

// Called once per emulated frame after the CPU has run.
void ra3dsDoFrame(void);

// True once a user session is active.
bool ra3dsIsLoggedIn(void);

#ifdef __cplusplus
}
#endif

#endif // _3DSRETROACHIEVEMENTS_H
