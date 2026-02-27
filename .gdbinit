# SNES9x-3DS Stereoscopic 3D — GDB init
# Usage:
#   1. On 3DS: Rosalina menu (L+Down+Select) → Debugger → Enable debugger
#   2. On 3DS: Note the IP address shown
#   3. On PC:  cd repos/matbo87-snes9x_3ds
#              /opt/devkitpro/devkitARM/bin/arm-none-eabi-gdb -x .gdbinit
#   4. At gdb prompt:  connect <3DS_IP>
#
# Or manually:  target remote <3DS_IP>:4003

file output/matbo87-snes9x_3ds.elf

# Convenience command: connect <ip>
define connect
  target remote $arg0:4003
end
document connect
  Connect to 3DS Rosalina GDB stub. Usage: connect <3DS_IP>
end

# Show all stereo globals at once
define stereo-state
  printf "=== Stereo 3D State ===\n"
  printf "g_stereoEnabled:    %d\n", g_stereoEnabled
  printf "g_stereoEffective:  %f\n", g_stereoEffective
  printf "g_stereoEyeSign:    %d\n", g_stereoEyeSign
  printf "g_stereoCurrentLayer: %d\n", g_stereoCurrentLayer
  printf "g_stereoMainScreenOverride: %p\n", g_stereoMainScreenOverride
  printf "stereo3dsMainScreenTargetL: %p\n", stereo3dsMainScreenTargetL
  printf "stereo3dsMainScreenTargetR: %p\n", stereo3dsMainScreenTargetR
  printf "\n=== Layer Depths ===\n"
  printf "BG0: %f\n", g_stereoLayerDepths[0]
  printf "BG1: %f\n", g_stereoLayerDepths[1]
  printf "BG2: %f\n", g_stereoLayerDepths[2]
  printf "BG3: %f\n", g_stereoLayerDepths[3]
  printf "OBJ: %f\n", g_stereoLayerDepths[4]
end
document stereo-state
  Print all stereo 3D global state (enabled, depths, eye sign, targets).
end

# Break at key stereo points
define stereo-breaks
  break stereo3dsInit
  break stereo3dsUpdateSlider
  break gpu3dsSetParallaxBarrier
  printf "Stereo breakpoints set. Use 'continue' to run.\n"
end
document stereo-breaks
  Set breakpoints at stereo init, slider update, and barrier control.
end

# Break at the double-render entry (gfxhw.cpp stereo path)
define stereo-render-break
  break gfxhw.cpp:4169
  printf "Breakpoint set at stereo double-render entry (gfxhw.cpp:4169).\n"
  printf "WARNING: This fires every frame — use 'continue' carefully.\n"
end
document stereo-render-break
  Set breakpoint at the stereo double-render path in S9xUpdateScreenHardware.
  Fires every frame when stereo is active — use with condition or ignore count.
end

set print pretty on
set pagination off
