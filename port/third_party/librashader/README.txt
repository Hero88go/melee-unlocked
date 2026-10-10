librashader v0.12.0 (https://github.com/SnowflakePowered/librashader), Mozilla Public License 2.0.

Runs RetroArch slang shader presets (.slangp). Used for the player-chosen shader in the present pass.
Unmodified upstream files:
  librashader.h, librashader_ld.h   C headers from include/ at tag librashader-v0.12.0
  bin/librashader.dll               from librashader-x86_64-windows-v0.12.0-optimized.zip (release assets)
  LICENSE.md                        the licence text

The program loads the DLL at run time through librashader_ld.h and works without it (the setting
is then unavailable). Source for the DLL: the tag above.
