# Training mods in v0.8.5

Open **Mods**, just above Build in the launcher sidebar, to download the five catalog packs or add
your own finished mod files. Dropping multiple files adds an entry for each detected pack. Playable
new packs start On; unsupported packs stay Off with an explanation. Custom entries use their own
mod name and warn that different builds can desync online. **F1 > Mods** (controller: Start + Down + Z)
edits the same On/Off choices. Disc changes take effect after a restart.
The public download includes native implementations, not the packs' disc images or saves.

| Pack | Supplied file | In-game entry | Purpose |
| --- | --- | --- | --- |
| 20XX Tournament Edition (TE) | Supported 20XX TE `.gci` save | VS Mode > Tournament Melee | VS rules, tournament conveniences and training switches |
| UnclePunch Training Mode Community Edition (TM-CE) | Supported 1.4d1 disc image | 1P Mode > Event Match | Event selection, Training Lab and individual exercises; newer unported versions stay Off |
| 20XX Training Hack Pack | Patched Hack Pack disc | Not supported yet (not a native pack, and it stops at startup on the Static Recomp) | A separate mod with its own training menus and features |

20XX TE and the 20XX Training Hack Pack are different mods. Adding a Hack Pack disc as an
asset overlay does not execute its PowerPC patches or recreate its menu.

## Two ways to change 20XX TE

The game's Tournament Melee entry opens a native **20XX TE Settings** menu. Use the stick
to select a row, Left/Right to change Off/On, and B to leave. Leaving applies the changes
and saves them to the same `port-settings.ini` used by **F1 > Mods**. The overlay can also
change those settings; reopening the in-game menu reads the current values.

This menu uses Melee's own debug-menu renderer and exposes the ported switches. It is not
a visual copy of the original TE screen. The original TE guide documents the Tournament
Melee entry: [20XX TE guide](https://www.20xx.me/guide.html).

| Setting group | Where it applies | Interaction with TM-CE |
| --- | --- | --- |
| Tournament Mode, frozen stages, v1.00 rules, stage striking, reset rules, hand-warmers | Offline VS; Tournament Mode permits only its allowed subset | Stored choices remain available for VS |
| Hold Start, handicap stocks, star KOs, play after GAME, taunt cancel, shields, fixed camera, CPU Zelda/Sheik | Offline VS, subject to Tournament Mode restrictions | Suspended in TM-CE exercises |
| Shield colors, screen rumble, L-cancel flash, CPU DI, collision bubbles, input display, color overlays | Offline play, subject to Tournament Mode restrictions | Suspended in TM-CE exercises |
| Menu music | Chosen in Sound Test and saved by the host | A shared menu preference; does not replace event menus |
| Lock settings | Both TE settings interfaces | Unlock first to change TE options |

20XX's stage controls use VS's stage-selection screen. UnclePunch's event definitions choose
their own character list, stage-selection route, CPU behavior, camera and pause controls.
Applying both sets of rules to an exercise could change its conditions or interfere with its
pause menu, so TM-CE owns those controls inside Event Match. TE remains available in VS.
The TM-CE version label shows only inside Event Match exercises. The main menu and VS stay
clean while TM-CE is loaded.

Both imports can share a profile. Profiles layer file replacements in order, with later
replacements winning; the Mods page reports file conflicts. TE's save normally does not
replace TM-CE's disc files. Adding two different mod discs is not a supported way to combine
their executable patches.

## Differences from the console mods

- Native recordings and their playback replace the original TE memory-card replay workflow.
  Original 20XX replay-save browsing/copying is not implemented here.
- Eight-character tags and the console tag/save-format extensions remain unported. Keep the
  normal Melee save format; importing a TE save is not a promise that every extended field works.
- TE's L-cancel training wheels are not exposed. The port's Auto L-cancel provides that helper.
  The shared **L-cancel flash** selector offers Off, MU missed (red), TE missed (red), TE success,
  and TE both. TE success can be Off, White or Green. MU and TE retain their different effect styles;
  choosing one prevents double flashes. Selecting a TE effect enables its feature master.
- Widescreen is controlled under Video, not by importing a console widescreen patch.
- Native TE match switches are disabled in online play. Native replay playback uses the
  feature flags stored with the recording, independent of today's overlay settings.
- Akaneia uses the Static Recomp, including when Source Port was selected in the launcher. Its
  online sessions are Direct only and require the same complete mod build at both ends. It cannot
  enter Unranked and does not silently substitute vanilla. Launch the vanilla disc for Unranked.
- Supported Source Port file overlays use the retail view for Unranked and other vanilla online
  queues. Direct uses matching mods only when Mods in Direct is enabled. Preflight keeps the
  training exercise loaded during search and switches the content view at the online handoff.
- TM-CE's Event Select L-button shortcut to its global on-screen-display toggle screen remains
  disabled. The port does not yet provide that console screen; individual exercises retain
  their own menus.

The event menu, exercise entry and pause-menu crash reproduction have been tested. That is
not exhaustive validation of every exercise condition, score-saving path or TE switch.

## Audio controls

Graphics preparation now finishes before simulation and audio start: startup evaluates the
selected DLAA/DLSS path, including DLSS 5 when enabled, and waits for GPU completion. Disc
file-cache warming and shader preparation also complete there. A live graphics-mode or
resolution change can still require new resources. Startup preparation removes cold-start work;
it does not make a GPU-heavy neural setting meet every chosen display frame rate.

**F1 > Audio** offers Auto (Windows shared), Low latency (Windows shared), WASAPI exclusive,
and optional ASIO for audio interfaces. Auto adjusts buffering and remembers it per device.
Low latency fixes the software buffer at your selection; its name does not guarantee the fastest
end-to-end result. Both request the minimum shared period supported by the selected driver.
Exclusive takes over the selected Windows output. ASIO uses its selected driver and outputs 1
and 2, with a driver-default or explicit sample buffer; if it cannot start, Auto is used.

The slider controls the software queue. The device queue and processing period are reported
separately; their sum is not a measurement of speaker, HDMI or Bluetooth latency. Lower the
buffer while watching the gap counter, and raise it if gameplay develops gaps. Changing mode
requires a restart to negotiate a different device period. Buffer changes apply immediately.

The native game still supplies 32 kHz stereo AI samples. Output clock correction resamples them;
underruns fade towards silence and crossfade on recovery to avoid a hard discontinuity. This
does not make playback bit-exact to a GameCube. Digital loopback comparisons measure key press
to captured output on the tested PC; they exclude headphone and speaker delay and do not establish
a physical GameCube comparison or a universal fastest mode. Windows supports driver-dependent minimum shared-mode periods:
[Microsoft low-latency audio](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio).
