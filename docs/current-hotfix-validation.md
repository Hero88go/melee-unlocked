# Current release hotfix validation

Base: release/v0.8.75 at 45e6297. Changes are isolated on fix/current-bug-reports-20261005.
The development 0.9 tree and live installations were not modified.

## Implemented changes

- Preserve mod-owned unlock instructions, including when turning Unlock everything off. Defaults
  stay on. Avoid using a compiled retail equivalent for switchable mod unlock queries.
- Correct the offline results hooks when a mod replaces the companion save hook. Preserve the
  next scene in a nonvolatile register instead of relying on an uninitialized stack slot.
- Native green-success and combined red/green L-cancel feedback on both engines, independent of TE.
- An Also use offline checkbox beside Frame delay, covering offline gameplay on both engines.
- Run the shipped console Gecko handler for Static user codes offline. Source retains native
  approved-data handling and does not link a PowerPC interpreter.
- Wait for processes from the installation being updated and preserve Unicode paths.
- Import a raw stage DAT with an explicit replacement-stage picker; automatically select a newly
  imported stage. Give generic Nucleus ZIP imports a descriptive stage name.
- Reject damaged startup rumble archives with a named, actionable ISO error before entering the
  native game library.

## Passed checks

- Complete production host/launcher builds, AVX2 and SSE2, Release, with optional DLSS 5 support,
  at idle priority with four jobs.
- Exact released native sources and release patch built as a matching DLL/DBG/SNAPEXCL trio.
- Combined production CTest suite: **77/77 passed** on AVX2. All **77 compatibility checks passed**;
  two tests initially failed when the disk filled and passed after temporary test copies were removed.
- Source link map contains none of static_gecko.obj, interp.obj, ram_translator.obj or
  leaf_translator.obj.
- Production console handler tested against actual guest RAM: writes, blocks, serial writes,
  conditions, pointers, registers, loops, searches, C0 bodies and C2 hooks; restoration and bounded
  non-returning code; unchanged CPU context and retrace count.
- Static ACE boot and match with imported C0 and C2 codes enabled; normal exit after the requested
  retraces, without a handler suspension.
- ACE and Akaneia completed an offline match with Unlock everything on and with it off. Both
  returned to valid character select after the end callback. Longer on runs entered a second match.
- Native red misses and green successes during actual aerial landings on both Source and Static,
  without TE. Repeated on the production 0.8.76 binaries; Static also had C0/C2 codes enabled.
- Source gameplay with the offline delay checkbox off versus on at 2: **2,702 compared samples,
  zero two-frame mismatches, zero menu mismatches**, with 62 samples changed by the requested delay.
- Actual Nucleus Precursor Battlefield ZIP imported, selected and served through the production
  filesystem overlay. Source loaded the actual replacement in a match.
- The same real stage renamed to an arbitrary DAT filename imported through the explicit stage
  picker and loaded in a Source match.
- Clean retail Source boot with Slippi menus on and D3D12 passed after the archive guard.
- Own sparse ISO fixture with valid boot metadata and zero-filled assets rejected before native
  startup with: LbRb.dat archive header length is 0, expected 1,045 bytes.
- Final packaged SSE2 executable completed an ACE match with C0/C2 codes enabled and returned to
  valid character select. The packaged Source executable passed clean D3D12 boot and rejected
  the damaged-ISO fixture with the named archive error.
- Final packaged retail Static match completed and reached the normal results scene (02:04),
  exercising the upgraded results hooks from the older embedded table.

## Practical limits

The release archive includes the production AVX2 and SSE2 executables, Source host, matching native
DLL/DBG/SNAPEXCL trio, and the optional rendering runtime files. Its integrity, compiled versions,
and equality with the built binaries were checked. Normal simulation timing was not accelerated.

The reported updater's exact error was unavailable. Its known process-lock and Unicode failure
paths are covered by production updater tests, including same-name processes in another install.

The full console handler does not guarantee that a code for another revision, a conflicting mod,
or an emulator-specific facility will work. The particular community actionable-flash and hitbox
code files were not provided and were not individually validated. Static feedback and delays have
production unit coverage and game smoke coverage; the measured input comparison above is Source.

Random stage selection, a broader online stage-model verifier, independently native actionable
flash and hitbox displays, and modded-ISO online Teams are not included.

The startup guard cannot reconstruct missing ISO assets. A working image is still required.
