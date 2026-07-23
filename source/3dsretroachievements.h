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

// Called while paused/in menus to keep rc_client housekeeping alive.
void ra3dsIdle(void);

bool ra3dsIsLoggedIn(void);

typedef enum {
    RA_LOGIN_CANCELLED = 0, // user backed out of the keyboard
    RA_LOGIN_OK,
    RA_LOGIN_FAILED,        // see ra3dsGetLastError()
} RaLoginResult;

RaLoginResult ra3dsPromptLogin(void);

void ra3dsLogout(void);

const char *ra3dsGetUsername(void);

const char *ra3dsGetLastError(void);

#ifdef __cplusplus
}
#endif

#endif // _3DSRETROACHIEVEMENTS_H
