## Install

Download `MeleeUnlocked-0.8.97-win64.zip` from this release. Close the game and launcher, then extract it over your existing Melee Unlocked folder. Settings, saves, mods and replays stay in place. Use your own Melee NTSC 1.02 ISO.

## Fixes

- Source Port, Big Blue: a crash during matches on Big Blue (Classic and VS). A car's model slot shared its bits with the car's state, so a car changing state could point past the end of the car table.
- Source Port, Big Blue: five of the cars' state changes existed only as console assembly and never happened on the Source Port. They now run as C.
- Source Port, Green Greens: Whispy blew about two seconds into every match instead of on his own timer, which desynced online matches against the Slippi client. His timer now starts where his update reads it.
- Source Port, Fountain of Dreams: the "Fountain of Dreams reflections" setting did nothing; the reflection, water ripples and platform jets were always off. With the setting on they now show in offline matches. Online matches keep them off, as on Slippi.
- Audio: the game now opts out of Windows' communication ducking, for players whose other apps got quieter while the game ran. Windows treats a stream on the default communications device as a call and lowers everything else. Not yet confirmed by that player.
- Display, D3D12: a refused window resize (seen on a laptop in exclusive fullscreen) no longer stops the game; it keeps drawing at the old size until the next size change.
- Mods: changing a costume on the character select failed every time on some PCs ("The cosmetic catalog could not be committed atomically") when another program had the catalog file open for a moment. The save now asks again for up to half a second.
- Display: the log now records frame pacing every five seconds (how evenly frames reach the screen, the slowest frames, and on D3D12 how many refreshes repeated a frame), each long frame as it happens, and when the game loses focus, is minimized or the desktop display mode changes. For reports of choppy motion.
