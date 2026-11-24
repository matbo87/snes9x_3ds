#ifndef _3DSMSU_H_
#define _3DSMSU_H_

// MSU-1 audio on NDSP channel 1.
// A streaming thread reads the .pcm from SD into an audio ring.
// The mixing thread copies from the ring to channel 1.
// The emulation thread reaches both through the hooks installed in the MSU-1 core.
// The .msu data port is read by the core itself.

// Call after loading an MSU-1 game.
// The first call allocates the buffers and starts the streaming thread.
void msu3dsStart();

// Call before snd3dsFinalize while the mixer and NDSP are alive.
void msu3dsFinalize();

// Mixing thread, inside snesAccessLock.
void msu3dsMixerTick();

// Emulation thread, after each emulated frame; keeps the track in sync during turbo.
void msu3dsEmulatedFrame();

void msu3dsApplyOutputVolume();

void msu3dsLogStats();

#endif
