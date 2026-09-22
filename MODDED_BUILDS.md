# Running a custom Melee build (m-ex / ACE, 20XX, ...) on this port

Melee Unlocked is not an emulator. Before it runs anything, `port/recomp/recomp.py` translates
one exact DOL image into C++, and the runtime refuses to boot a disc whose DOL is not that image
— the translated code and the RAM the game reads would otherwise describe two different games.
That is why a stock build rejects an ACE disc with:

    ISO DOL does not match vanilla Melee NTSC 1.02; recompiled code cannot run this image

So there is no way to "load" a mod into an existing build. The only route is to translate the
mod's own DOL and compile a second build around it. These changes make that possible:

| change | what it does |
|---|---|
| `recomp.py --modded-dol` | translates a DOL that is not vanilla 1.02, and records its SHA-1 in the generated code |
| `recomp.py --discover` | finds the mod's own functions, which no symbol map describes, so they are compiled instead of interpreted |
| `recomp.py --skip-unemittable` | a function the emitter cannot translate is dropped (the interpreter takes it) instead of failing the run |
| `host.cpp` | the boot check compares the disc's DOL against the image this build was recompiled from, whatever that is, and the DOL size is read from the image instead of hard-coded |
| `host.cpp` | "is this address code" now comes from the loaded DOL's text sections, so a handler in the mod's own section is not reported as corruption |
| `tools/extract_dol.py --any` | extracts the DOL from a disc whose header is not vanilla |
| `emit.py` fall-through | a function whose code runs past its last instruction continues into the next one instead of returning; hand-written mod code ignores the symbol map's boundaries |
| `recomp.py --mod-gct` | bakes the mod's own Gecko table into the translation (m-ex ships its content as `codes.gct` on the disc) |
| `--watch ADDR` | runtime diagnostic: reports what changes a word of guest memory, and between which two calls |
| `tools/mod_report.py` | a pre-flight report on what the mod does to this port's assumptions |
| `tools/iso_file.py`, `tools/gct_report.py`, `tools/changed_functions.py`, `tools/disasm.py` | pull a file off the disc, decode a code list, list what a mod patched, read its code |

Everything the stock build does is unchanged: recompiled from the vanilla DOL, `gs::image`
carries the vanilla SHA-1 and the boot check is exactly as strict as before.

## Before you start

Use a **separate checkout**. `port/generated/` holds the translation of one DOL, so recompiling
for ACE in your working clone replaces your vanilla build's generated code.

    git clone https://github.com/hero88go/melee-unlocked.git melee-unlocked-ace
    cd melee-unlocked-ace
    git apply ..\modded-dol-support.patch

You need the ACE-patched ISO, and (worth having) your clean vanilla 1.02 ISO for the comparison
step.

## 1. Extract both DOLs

    python tools\extract_dol.py "D:\Melee\ACE.iso" build\ace.dol --any
    python tools\extract_dol.py "D:\Melee\melee.iso" build\vanilla.dol

Each prints the DOL's SHA-1. Keep the ACE one: the build you are about to make will accept that
image and nothing else. Re-exporting the ISO from MexManager produces a different DOL, and the
build then has to be regenerated.

## 2. Pre-flight report

    python tools\mod_report.py --dol build\ace.dol --vanilla build\vanilla.dol

Read it in this order:

- **"N of M retail functions have different bytes"** — how much of the retail game m-ex rewrote.
  Expected; those functions are translated from their patched form.
- **"!! ... are replaced by host code in this port"** — the real blocker to look for. This port
  does not run the game's OS, DVD, PAD, SI, EXI, AI, DSP, AR or CARD code; it replaces those
  functions with host implementations (`port/recomp/hle_list.txt`). If m-ex modified one of them,
  its change cannot take effect, and whatever it was for will not work. A hit here needs a
  hand-written fix in `port/runtime/hle/` before the mod can behave correctly.
- **"discover: N functions, X bytes"** — the mod's own code, now compiled.
- **"Y left for the interpreter"** — text nothing reaches statically. Data and padding count
  here too, so a non-zero number is normal; a large one means more work falls to `interp.cpp`.
- **"hooks from retail code into the mod"** — where m-ex branches into itself. A hook listed as
  landing inside an HLE'd function will not run, same as above.

## 2b. The mod's own Gecko table

An m-ex build keeps very little in the DOL. ACE 2.0 changes 42 retail functions and adds no code
at all; the mod itself ships as `codes.gct` on the disc, which a DOL hook in `HSD_OSInit` loads
into RAM at boot and applies with m-ex's own handler.

That does nothing here. Recompiled code is not read from guest RAM, so writes the handler makes
land in a void and a C2 cave in the table is never entered — the branch that would enter it only
exists in RAM. Symptom: the game panics early with `assertion "adr" failed in memory.c on line 52`
(`HSD_MemAlloc` with heap handle `-1`), because the cave that sets the heap up never ran.

The fix is the one the port already uses for Slippi's codes: bake the table into the translation.

    python tools\iso_file.py --iso "D:\Melee\ACE.iso" --extract codes.gct --out build\codes.gct
    python tools\gct_report.py --gct build\codes.gct --base 8065CC80 --dol build\ace.dol

`--base` is the address the build loads the table at, which the port prints when it happens:
run once and look for `DVDReadAsyncPrio ... addr=8065CC80` right after `lbFileGetSize`.

The report's **unsupported lines** count is what decides feasibility: those are code types
`gecko.py` cannot express as ahead-of-time patches. ACE 2.0 has none — 391 writes, 778 C2 hooks,
17,026 instructions of cave code, all bakeable. **Hooks into run-time code** (the report calls
them "not text") are the ones that cannot be: they patch code loaded from files, which no
translation can see. ACE has 29 of those out of 778.

## 3. Recompile

    python port\recomp\recomp.py --dol build\ace.dol --modded-dol --no-slippi ^
        --mod-gct build\codes.gct --mod-gct-base 0x8065CC80

Leave out the `--mod-gct` pair for a build that ships no code table of its own.

`--modded-dol` implies `--discover` and `--skip-unemittable`. `--no-slippi` leaves out Slippi's
Gecko code table, which is not optional here: those codes patch vanilla addresses that m-ex has
already rewritten, so baking them in would corrupt the mod. It also means this build has no
working online play — do not take a modded build onto Slippi's matchmaking.

Watch the output for `warning: N functions left untranslated` — those run in the interpreter, and
each line names the address and the instruction the emitter could not handle.

## 4. Build

    cmake -S . -B build-ace -G "Visual Studio 17 2022" -A x64 -DMELEE_BUILD_EXPERIMENTAL_PORT=ON -DMELEE_RAM_MB=32
    cmake --build build-ace --config Release --target melee_port --parallel

`MELEE_RAM_MB=32` is the default and is what lets four players into a match; see
[The heap is too small for four players](#the-heap-is-too-small-for-four-players). Pass
`-DMELEE_RAM_MB=24` for a machine that behaves like a console.

## 5. Run

    build-ace\port\Release\melee_port.exe --iso "D:\Melee\ACE.iso" --fps unlocked --scale auto

Use `melee_port.exe` directly, not the launcher: the launcher's disc check only accepts a vanilla
NTSC 1.02 header. For a quick first test without a window:

    build-ace\port\Release\melee_port.exe --iso "D:\Melee\ACE.iso" --headless --frames 3600

## When it breaks

`melee_port.log` next to the executable is the whole story. The messages you are most likely to
meet, and what each one means:

- **`ISO DOL (SHA-1 ...) is not the image this build was recompiled from`** — the ISO you ran is
  not the one you recompiled from. Re-run steps 1 and 3.
- **`undecodable instruction` at some address** — a word in the mod's code that the Gekko decoder
  in `port/recomp/gekko.py` does not know. Check the address in the report: inside discovered
  code it is usually data that discovery swallowed (harmless unless executed); in the middle of
  real code it is an opcode the decoder needs adding.
- **`interpreter jump outside RAM` / `guest call depth exceeded`** — control went somewhere wrong.
  Usually a discovered function whose extent was guessed badly. Re-run recomp with
  `--discover --no-pointer-scan` (fewer false starts) and compare.
- **`bad function entry`** — a dispatch thunk entered a function at an address it does not have a
  label for; same cause.
- **`alarm queue corrupt`** — with these changes this should only fire on real corruption; if it
  fires at a handler address inside the mod's own text section, that check needs widening again.
- **A fatal while loading a custom character or stage** — the expected first wall. m-ex executes
  code stored in fighter and stage files; that code never reaches the translator, so it runs in
  `interp.cpp`, which exists for a handful of Slippi routines and has not carried a whole
  framework before. The interpreter line at the end of the log (`interpreter: N calls into
  RAM-resident code, M instructions`) tells you how much of the game is going through it.

## Status: ACE 2.0 runs

Smash ACE Build v2.0.0 boots, renders and plays on this port, at roughly 0.8 ms/frame of
simulation with the mod's code compiled rather than interpreted (2 interpreter calls in a
600-frame run). Getting there needed four fixes, in this order, each hiding the next:

1. **The image check.** The runtime only accepted vanilla 1.02. Now it accepts the DOL the build
   was recompiled from, whatever that is.
2. **The mod's code table.** ACE changes 42 retail functions and adds no code; its content is
   77 KB of Gecko codes in `codes.gct`, loaded at boot into RAM, where recompiled code cannot
   execute it. Baked in ahead of time with `--mod-gct`. Symptom before the fix:
   `assertion "adr" failed in memory.c on line 52`, HSD's heap never created.
3. **Fall-through between functions.** m-ex's loader sits in the dead HIO/MCC debug block and
   runs straight through those old function boundaries — its `addis`/`ori` pair is split across
   the end of `HIOWriteAsync` and the start of `HIOReadStatus`. The emitter ended the translated
   function there, so the guest took a return it never asked for and lost the `lmw` restoring
   `r28` (`HSD_OSInit`'s cached `arena_hi`). Symptom: a heap created with its end below its
   start, then a jump to a garbage address.
4. **Nothing else.** The remaining `fsqrt` gap (below) the interpreter covers.

Four players then needed a fifth fix, which is not a port bug at all — see the next section.

## The heap is too small for four players

A 4-player match on ACE dies partway in with the same message as an unbuilt heap:

    assertion "adr" failed in memory.c on line 52

This one is real out of memory, and it happens on Dolphin too: it is the build's limit, not the
port's. The port can fix it anyway, which an emulator structurally cannot, because emulating a
console means emulating its memory.

What the heap report at the panic shows:

    heap 3* (at panic): 5.43 MB total, free 0.00 MB in 1 blocks, allocated 5.43 MB in 17999 blocks

5.43 MB, not a byte free, ~18,000 live objects. Where 5.43 comes from, traced with
`--trace-func HSD_CreateMainHeap`:

    frame 58  HSD_CreateMainHeap(807212C0, 817F2760)   17.0 MB   <- boot scene
    frame 63  HSD_CreateMainHeap(80C397E0, 811A7360)    5.43 MB  <- everything after

`817F2760` is the arena high exactly (the port puts the FST there, just under the top of RAM).
The match heap's top is that minus a fixed `0x64B400` — 6.6 MB that m-ex keeps at the top of RAM
for its extended content, which on a 24 MB machine leaves the game a third of what retail Melee
gets. Four players and ACE's extra content do not fit in the remainder.

Two details make this cheap to fix:

- **The bottom is pinned, the top follows the arena.** Rebuild at 32 MB and the call becomes
  `HSD_CreateMainHeap(80C397E0, 819A7360)` — bottom unchanged, top up by exactly the 8 MB added.
  The match heap goes 5.43 MB → 13.43 MB, m-ex's block rides up on top of it untouched, and no
  patching of the mod is involved.
- **The obvious larger numbers do not work.** 48 MB is read by Melee as a devkit
  (`OSGetConsoleSimulatedMemSize() == 0x03000000`) and half of it is deliberately thrown away at
  `main+0x4C`. Past 32 the game starts producing pointers that do not survive its own packing —
  a 40 MB build dies on `interpreter jump outside RAM (841C49F8)`.

So 32 MB is the default. It is off-spec: `OSGetPhysicalMemSize` reports something no GameCube
ever had, and the boot banner says `Arena Size 27 MB`. Anything that must match console behaviour
— replay verification, netplay — wants `-DMELEE_RAM_MB=24` instead. This fork has no netplay, so
nothing here depends on it.

A dead end worth recording, because it looks right for a long time: `lbHeap_80015900` computes
those bounds by walking four 28-byte entries at `0x80431FE8` (`start` at `+8`, `size` at `+0xC`,
`kind` at `+0x10`, an enable flag at `+0x14`; kind 1 raises the bottom, kind 2 lowers the top).
Dumping that table at the call shows nothing but zeros — because ACE's C2 hook lands on
`0x800159B4`, the loop's first instruction, and replaces the whole walk with 48 instructions of
its own. The vanilla table is dead code in an m-ex build. `tools/xref.py --addr 80431FA0` is what
showed that only the initialiser ever writes there.

### Known limits

- **29 of the mod's hooks patch code that only exists at run time** (fighter and stage files
  m-ex loads). Nothing ahead-of-time can reach those; whatever they do, does not happen.
- **`fsqrt` is not in the emitter**, so ~20 functions carrying it (including `HSD_OSInit`, which
  has a cave spliced into it) run interpreted. Harmless in practice. Worth noting that `fsqrt`
  does not exist on the real Gekko — that code would fault on a console, so it relies on
  Dolphin implementing it.
- **No online play.** `--no-slippi` is required, and a modded build must not be taken onto
  Slippi matchmaking.
- **The launcher rejects the disc** (it checks for a vanilla header). Run `melee_port.exe`
  directly.

## Going back to vanilla

Nothing here touches your vanilla install. In this checkout,
`python port\recomp\recomp.py --dol build\vanilla.dol --gct-base 0x8065CC80` restores a stock
translation, verified by the same SHA-1 check as before.
