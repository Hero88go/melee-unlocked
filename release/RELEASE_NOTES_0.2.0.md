# Melee Unlocked 0.2.0

Animation, DLAA and the controller overlay. The headline fix is an animation bug that made
airdodges, and anything else that flashes, stutter in time with the flash.

## Install

Extract the zip and run `MeleeUnlockedLauncher.exe`, or drag your Melee NTSC 1.02 ISO onto
`MeleeUnlocked.bat`. Settings, saves and replays carry over.

## Fixed

**Airdodges stuttered in time with the intangibility flash.** While a fighter flashed, the game
added a lighting stage to its material for one frame at a time. The renderer treats a material
change as "this might be a different object" and refused to carry the fighter's animation across
it, so the fighter froze for a whole simulation frame, twice per flash cycle, then snapped to catch
up. That is 20 stalls a second, exactly in step with the flash, which is why airdodging looked both
slow and blinky. The renderer now recognises the fighter across a material change, because the game
itself already tells it which object is which. Measured over an airdodge: 27% of presented frames
were stalling before, 0% after, with the fighter up to a quarter of a character height out of
position on a bad frame. This affects every flashing character, not just airdodges.

**DLAA produced a black screen.** DLAA renders at the same size it outputs, and it was being asked
for a window-sized image that the game's internal resolution can never match, so it rendered
nothing. It now anti-aliases the game at your chosen internal resolution and the result is fitted
to the window as usual. DLAA also no longer disables Internal resolution and Anti-aliasing, since
it renders at full resolution and those settings still apply; the DLSS upscaling modes still
override them, and now say so by greying them out.

**The controller overlay.** It can show up to four ports at once, stacked. If the port it is set to
has nothing plugged into it, it shows a port that does, rather than an empty controller. The face
buttons sit where they sit on a real GameCube controller, the stick knobs are bigger and easier to
read on a stream, and the line from the centre to the knob is gone. Drag it to move it and its
edges to resize it while the settings panel is open, and "Hide border" hides the panel background
and the resize grip.

**Shield drops (UCF).** Gecko code caves can end the function they were injected into by hand and
return past the call, which is how UCF Shield Drop overrides the game's answer. The recompiler was
discarding that, so the override never took effect and this client could disagree with the other
player. This is the only code in the game that does it. Whether it is the cause of the shield-drop
desync reported in Unranked is not yet confirmed, so if you still see one, please say so: the log
now records how often the path is taken.

**Sub-frame animation now defaults to Interpolate**, which does not overshoot. Predict ahead is
still available in the settings panel.

**Pressing X during a match popped up a small window that stayed while the button was held.** Dear
ImGui polls Xbox controllers itself when controller navigation is on, and treats X as its menu
button, which opens its window switcher. Controller navigation is now only active while the settings
panel is open.

**A crash waiting to happen in the rebinding UI.** Opening it with a GameCube adapter connected
wrote three controller states past the end of a single-entry variable on the stack, and gave every
port the first port's baseline. This is a plausible cause of otherwise unexplained crashes around
the controller settings.

## Experimental

**Switch Pro Controller.** Reports from a Switch Pro Controller never reached the game at all,
because it identifies itself as a joystick rather than a gamepad and only gamepads were being
listened to. It is now listened for, initialised over USB (it stays silent until it is asked to
start sending), and selectable in the port list as "Switch Pro (experimental)". It has not been
tested against real hardware: nobody here has one. Please report whether it works. If it does not,
Steam Input or BetterJoy will present the controller as an Xbox pad, which has always worked.

## Known issues

- Fighters still hold their pose for short runs when a joint uses a feature the animation capture
  refuses (billboards, constraints, IK). This is separate from the airdodge bug and is being worked
  on.
- Yoshi's Story still has visual problems.
- Gecko codes cannot be turned on and off by editing `GALE01r2.ini`. The enabled codes are
  translated into the game when it is built, so the file records what was built in rather than
  controlling it. Run-time toggles are possible (Widescreen already is one) and more can be added.

## Thanks

- fern: airdodge stutter
- Asukafriend: X button popup
- Stache: Gecko codes not toggleable
- manassm: Switch Pro (#2)
- testers: shield drop in Unranked

## For developers

Input scripts can now set the analog triggers (`l=` and `r=`), so shield behaviour can be tested
without a controller. The log reports `gecko: N code-cave returns resumed past the call` when a
Gecko cave takes the path described above.
