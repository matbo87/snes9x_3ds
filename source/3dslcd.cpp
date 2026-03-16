#include <3ds.h>
#include <math.h>

#include "3dslcd.h"
#include "3dsgpu.h"

static u32 savedVtotalTop;
static u32 savedVtotalBottom;
static bool vtotalActive;

// Compute the nearest integer VTotal for a target fps.
// Uses round() to pick the closest match (~0.02 Hz error for NTSC).
// For even more precise matching, alternating between
// the two nearest vtotal alues per frame could be used
// (https://github.com/skyfloogle/red-viper/issues/46#issuecomment-1997181635)
static u32 computeVtotal(double targetFps, u32 defaultVtotalBottom) {
    double defaultEff = (double)(defaultVtotalBottom + 1);
    double targetEff = defaultEff * LCD_DEFAULT_HZ / targetFps;
    return (u32)round(targetEff) - 1;
}

static void writeVtotal(u32 vtotal2D) {
    u32 vtop = gpu3dsIs3DEnabled() ? vtotal2D * 2 + 1 : vtotal2D;
    u32 vbot = vtotal2D;
    GSPGPU_WriteHWRegs(PDC_VTOTAL_TOP, &vtop, 4);
    GSPGPU_WriteHWRegs(PDC_VTOTAL_BOTTOM, &vbot, 4);
}

void lcd3dsSetEmulationRate(u32 ticksPerFrame) {
    if (vtotalActive)
        return;

    // store current VTotal so we can restore it properly
    GSPGPU_ReadHWRegs(PDC_VTOTAL_TOP, &savedVtotalTop, 4);
    GSPGPU_ReadHWRegs(PDC_VTOTAL_BOTTOM, &savedVtotalBottom, 4);

    double targetFps = (double)TICKS_PER_SEC / ticksPerFrame;
    u32 vtotal = computeVtotal(targetFps, savedVtotalBottom);

    vtotalActive = true;
    gspWaitForVBlank();
    writeVtotal(vtotal);
}

void lcd3dsRestoreDefaultRate() {
    if (!vtotalActive)
        return;

    vtotalActive = false;
    gspWaitForVBlank();
    GSPGPU_WriteHWRegs(PDC_VTOTAL_TOP, &savedVtotalTop, 4);
    GSPGPU_WriteHWRegs(PDC_VTOTAL_BOTTOM, &savedVtotalBottom, 4);
}
