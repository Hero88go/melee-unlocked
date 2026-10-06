# Current-release hotfix replies

These replies describe hotfix 0.8.76. Features identified below as future work are not included
in this release.

## Two frames of delay in Training

> The hotfix adds **Also use offline** beside **Frame delay** under **F1 > Game > Online**.
> Set Frame delay to **2** and check Also use offline. It applies to Training, VS and other offline
> gameplay on both engines. Menus keep their normal controls. It delays inputs and keeps the game's
> simulation speed unchanged.

## Modded ISOs in online Teams

> It is technically possible, but modded-ISO Teams are not currently supported. Mod support in Direct
> does not automatically give Teams support: all four players need compatible game code and content,
> and the matchmaking and compatibility checks must support that. I cannot promise a release date.

## Repeated character-unlock and trophy messages

> The hotfix corrects how Unlock everything interacts with the mod's own unlock code. Everything
> remains unlocked by default. Turning the option off preserves the mod's own settings and restores
> its instructions instead of replacing them with retail instructions. The patch also fixes an
> after-match hang found while testing ACE and Akaneia.

## Official red and green L-cancel feedback

> Open **F1 > Game > L-cancel > L-cancel flash**. Choose **MU: success (green)** for successful
> L-cancels, or **MU: both (red / green)** for green successes and red misses. **MU: missed (red)**
> gives just red misses. These MU choices work on both engines without enabling or installing 20XX.
> They are visual feedback; they do not automatically L-cancel for you.

## Why some Gecko codes were refused

> A Gecko code can either change data or install PowerPC instructions into the GameCube program.
> The older Static Recomp importer supported only a limited selection and rejected instruction
> injections, even though that engine has a PowerPC execution path. The hotfix runs the console
> Gecko handler there, including C0 and C2 instruction codes and the standard handler's conditions,
> pointer operations, loops and searches. Imported user codes are limited to offline play and are
> suspended during online play and replay playback.
>
> The Source Port runs rebuilt native game code. A PowerPC branch into the console program does
> not point into that native program, so arbitrary instruction-injecting codes need native feature
> implementations there. Some approved data codes already work. Providing the Static handler
> does not make every code compatible with every game revision, mod or other enabled code, and
> emulator-specific codes can have additional requirements.

## Actionable green flash and hitbox display

> Both features are possible. On Static Recomp, compatible Gecko versions can use the new offline
> handler; the particular actionable-flash and hitbox code files still need individual testing.
> On Source Port, there are existing native 20XX TE **Color overlays** and **Collision bubbles**
> options under **F1 > Mods > 20XX TE** after importing a supported TE save. These are offline
> training options. Making those two displays independent native features needs additional work.
> The native green successful-L-cancel flash is already included in this hotfix and needs no TE save;
> that is a separate display from green when actionable.

## Updating fails

> The hotfix fixes update replacement failures when a game from the same installation is still
> running, and handles Unicode installation paths. The updater waits for those processes to close.
> Other installations are left alone. Without the original error text, I cannot confirm which
> updater failure this report encountered.

## Precursor Battlefield from Nucleus

> I downloaded and tested the actual Precursor Battlefield ZIP and DAT. The replacement loads
> offline. The hotfix also selects newly imported stage variants automatically, so the importer
> does not leave Vanilla or a previous skin selected. This stage changes more than texture bytes,
> so the current online check falls back to the standard Battlefield.

## Custom stage DAT without a ZIP

> A ZIP is not required. In the hotfix, open **F1 > Mods > Cosmetic mods**, click **Add stage DAT...**,
> choose your DAT, choose **Stage to replace**, then click **Import stage**. The imported variant
> becomes selected automatically. Restart the game to load the changed disc profile. The stage
> picker also handles files with custom names that do not identify their replacement slot.

## More than the vanilla number of costumes

> You can already store more than ten skins per character and choose them under
> **F1 > Mods > Cosmetic mods**. They are alternatives for existing costume slots. For supported
> retail profiles, L and R on character select cycle the installed skins for that costume slot.
> Online choices must preserve the fighter skeleton; Teams keeps its team-color rules.

## Random stage skins and model replacements online

> Randomly choosing a saved stage skin at match start would need a new feature. The current online
> stage check is conservative: it allows texture changes but rejects model and file-layout changes.
> That can reject harmless visual replacements that work in Dolphin. A broader check needs to
> verify that collision, platforms and gameplay scripts remain unchanged. New visual models alone
> should not disqualify a skin, but that broader verification is not included in this hotfix.

## Startup archive crash in 0.8.75

> This report stops while loading **LbRb.dat**, the rumble-data archive, before the menus start.
> Its expected length is 1,045 bytes, but its header is zero. Three patched menu-file hashes also
> exactly match patches applied to all-zero source files. That indicates missing or zero-filled
> assets being read from the game image. Select a fresh, working, uncompressed Melee NTSC 1.02 ISO;
> if using a modified image, rebuild it from a working base. The hotfix detects the damaged startup
> archive early and names it in the error instead of reaching the generic archive assertion.
