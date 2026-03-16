#ifndef _3DSLCD_H_
#define _3DSLCD_H_

#include <3ds.h>

// VTotal register addresses for top and bottom LCD
#define PDC_VTOTAL_TOP    0x400424
#define PDC_VTOTAL_BOTTOM 0x400524

// Default LCD refresh rate in Hz at VTotal=413
// VClock = PClock / (HTotal + 1) / (VTotal + 1)
//        = (268111856 / 24) / (450 + 1) / (413 + 1)
// https://www.3dbrew.org/wiki/GPU/External_Registers#LCD_Source_Framebuffer_Setup
#define LCD_DEFAULT_HZ     59.831

// 3DS CPU tick rate
#define TICKS_PER_SEC (268123480)

// SNES refresh rates:
// refresh rate = masterClock / (cyclesPerScanline × scanlinesPerFrame)
// NTSC: 21477272 / (1364 * 262) = 60.098814 Hz
// PAL:  21281370 / (1364 * 312) = 50.006978 Hz
#define TICKS_PER_FRAME_SNES_NTSC (4462088)
#define TICKS_PER_FRAME_SNES_PAL  (5361734)

void lcd3dsSetEmulationRate(u32 ticksPerFrame);
void lcd3dsRestoreDefaultRate();

#endif
