## Install

Download `MeleeUnlocked-0.8.76-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

This hotfix fixes mod unlock handling and after-match hangs, adds native green L-cancel feedback and optional offline input delay, and expands offline Gecko support on Static Recomp.

## New

- Both engines, L-cancel flash: **MU: success (green)** and **MU: both (red / green)** are available under **F1 > Game > L-cancel**. No 20XX save or feature toggle is required. These choices give visual feedback without changing the landing lag or pressing a trigger for you.
- Both engines, Frame delay: **Also use offline** sits beside the slider under **F1 > Game > Online**. When checked, the chosen delay applies to offline gameplay, including VS, Training and 1P modes. Menus and replay playback keep their normal controls. Game speed is unchanged.
- Static Recomp, Gecko Codes: imported offline codes run through the console Gecko handler, including C0/C2 PowerPC instruction codes, conditions, pointer operations, loops and searches. Restart after adding codes so their memory can be reserved. Imported user codes are suspended during online play and replay playback.
- Both engines, stage imports: **Add stage DAT...** under **F1 > Mods > Cosmetic mods** lets you choose the stage a raw DAT replaces, including files with custom names. A ZIP is not required.

## Fixes

- Static Recomp, Unlock everything: preserve the mod's own unlock instructions and checks, including when turning the option off. Everything stays unlocked by default. This corrects the repeated unlock challenges and trophy messages reported with ACE and Akaneia.
- Static Recomp, after-match hangs: the offline results hooks no longer read an uninitialized stack value when a mod replaces their companion hook. ACE and Akaneia return to a valid scene after a match.
- Stage imports: newly imported stage variants become selected automatically instead of leaving Vanilla or a previous skin selected. Generic Nucleus ZIP names use the stage DAT's name. Precursor Battlefield was checked with the actual Nucleus download and loaded offline.
- Updates: wait for running programs from the installation being replaced, including the Source Port, and handle Unicode installation paths. Programs in other installations are left alone.
- Source Port, startup: detect a damaged or zero-filled `LbRb.dat` before the game enters its archive parser. The error names the file and asks for a fresh working ISO instead of showing the generic `lbarchive.c` assertion.

## Notes

- All 77 automated checks passed for both the normal and compatibility builds. ACE and Akaneia completed offline matches with Unlock everything both on and off; Akaneia also completed a second match in the same run. Native red and green L-cancel feedback was exercised on both engines without 20XX, and the actual Precursor stage loaded in Source matches.
- Gecko compatibility still depends on the code's game revision, the loaded mod and other enabled codes. Emulator-specific facilities can require additional support. The individual actionable-flash and hitbox code files from the reports were not supplied for testing. Source Port still needs native equivalents for PowerPC instruction-injecting codes.
- Online stage validation still permits texture changes and falls back to the standard stage for unverified model or gameplay-data changes. Random stage skins and modded-ISO online Teams are not part of this hotfix.
- The startup check cannot reconstruct missing ISO assets. A working, uncompressed Melee NTSC 1.02 image is still required.
