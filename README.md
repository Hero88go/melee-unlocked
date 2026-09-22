
# Smash ACE Unlocked

**[Smash ACE Build v2.0.0](https://github.com/Chri222k/ACE-BUILD-PUBLIC-) running natively on
Windows**, with an unlocked display frame rate — no emulator.

This is a fork of [Hero88go/melee-unlocked](https://github.com/Hero88go/melee-unlocked), which
statically recompiles Melee's PowerPC code into C++ and runs it against a native D3D12/D3D11
renderer. That project targets retail Melee NTSC 1.02. This fork targets the ACE build instead:
its executable, and the 77 KB Gecko code table the build ships on its disc, are translated
ahead of time along with the game.

Nothing from Melee or from the ACE build is included here. You supply your own ACE-patched ISO.

Not affiliated with the ACE team, the m-ex project, the Slippi team, Nintendo or HAL Laboratory.

## What you need

1. A clean **Melee NTSC 1.02** ISO (MD5 `0e63d4223b01d9aba596259dc155a174`).
2. The **ACE build patch** applied to it, from the
   [ACE build releases](https://github.com/Chri222k/ACE-BUILD-PUBLIC-/releases). Their
   instructions are in the release.
3. Windows 10/11. Git, Python, CMake and the Visual Studio 2022 Build Tools are installed for
   you by `build.bat` if they are missing.

## Play

Drop your ACE ISO onto **`play.bat`**, or put it next to `play.bat` named `ace.iso` and
double-click. The first run builds everything (20 to 40 minutes, mostly compiling the translated
game), then launches. Later runs start straight away.

F1, or Z + Start on a pad, opens the PC settings panel for controls, video and frame pacing.

What `build.bat` does, if you would rather run the steps yourself:

```powershell
git clone --depth 1 https://github.com/doldecomp/melee.git melee          # decomp source, no game data
python tools\extract_dol.py "<your ACE ISO>" build\ace.dol --any
python tools\iso_file.py --iso "<your ACE ISO>" --extract codes.gct --out build\codes.gct
python port\recomp\recomp.py --dol build\ace.dol --modded-dol --no-slippi ^
    --mod-gct build\codes.gct --mod-gct-base 0x8065CC80
cmake -S . -B build-ace -G "Visual Studio 17 2022" -A x64 -DMELEE_BUILD_EXPERIMENTAL_PORT=ON
cmake --build build-ace --config Release --target melee_port --parallel
build-ace\port\Release\melee_port.exe --iso "<your ACE ISO>" --threaded-renderer
```

The build accepts **only** the exact DOL it was translated from. Re-export the ISO from
MexManager and you must re-run the recompile step; the game will say so rather than misbehave.

## Why a separate build is needed at all

Melee Unlocked is not an emulator. `port/recomp/recomp.py` translates one exact DOL image into
C++ ahead of time, so a disc carrying a different executable cannot run against it — the stock
build refuses an ACE disc outright.

Translating ACE's DOL is most of the work, but not all of it. ACE changes only 42 functions of
the retail executable and adds no code to it: the mod itself is a Gecko code list, `codes.gct`,
which m-ex loads into RAM at boot and applies with its own handler. Recompiled code does not
execute guest RAM, so all 770 of those hooks would land in a void. They are baked into the
translation instead, which is what `--mod-gct` does.

[`MODDED_BUILDS.md`](MODDED_BUILDS.md) documents the whole route, including the three failures
that stood between "it boots" and "it plays", how each was diagnosed, and what still does not
work. It also covers using this on other m-ex builds.

## What does not work

- **No online play.** Slippi's code table is left out (it patches addresses ACE has already
  rewritten), so this build has no netplay. Do not take a modded build onto Slippi matchmaking.
- **29 of ACE's hooks patch code that only exists at run time** — fighter and stage files m-ex
  loads. Nothing translated ahead of time can reach those.
- **The Melee Unlocked launcher rejects the disc**, since it checks for a vanilla header. Use
  `play.bat` or `melee_port.exe` directly.
- `fsqrt`, which ACE's code uses and the real Gekko does not have, runs interpreted. Harmless.

## Credits

- **Hero88go** — [Melee Unlocked](https://github.com/Hero88go/melee-unlocked), the recompiler,
  runtime and renderer this is built on.
- **The Smash ACE build team** — [ACE Build](https://github.com/Chri222k/ACE-BUILD-PUBLIC-),
  and the **ACE team** whose mod it extends.
- **Akaneia / Ploaj** — [m-ex](https://github.com/akaneia/m-ex), the framework ACE is built with.
- **The Melee decompilation project** — [doldecomp/melee](https://github.com/doldecomp/melee).
- **The Slippi team**, whose GPL code the runtime borrows from, and **Dolphin**.

## License

GPL-2.0-or-later, as upstream. Super Smash Bros. Melee is the property of Nintendo and HAL
Laboratory; this repository contains none of its data, and none of the ACE build's.
