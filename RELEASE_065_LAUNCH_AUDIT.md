# Static-recomp release 0.6.5 launch audit

2026-09-20. Report: launcher says it could not start melee_port.exe after updating
from 0.6.4. The affected machine's Windows error code is not available. Root cause
not reproduced and not established. This report concerns the actual public
standard win64 ZIP, not the local source-port game binaries.

## Verified

- Downloaded both public standard release ZIPs; SHA256 matches release metadata.
  0.6.4: 8a647af645ca0c626f156b1df02afbfa1175b9e56e9750b4862f801dc17bfa5a.
  0.6.5: e54dedbbbb79800747514f8bcebacf86f0a4e883145bc5f83b69e7323d8d6f6a.
- Both archives contain 99 files. No additions/removals. All packaged DLLs and
  Sys data are unchanged. Changed files: normal/compatibility/playback executables,
  launcher, README.txt and shadercache/recipes.bin.
- Actual shipped normal and compatibility 0.6.5 executables each complete 60
  retraces from a clean extraction, exit code 0, with explicit package Sys paths.
- Isolated update test: extract 0.6.4, then use the updater's xcopy /e /y /q /r
  copy operation from a fresh 0.6.5 extraction. All 99 destination file hashes
  match the ZIP before execution. Both updated game executables then complete
  600 retraces with threaded rendering, exit code 0 and completion log markers.
- All runs hidden and muted, MELEE_NO_GC_ADAPTER=1, owned child handles and
  independent logs/scratch save directories. Evidence: run-source/release-065-audit.

## Limits and local hardening

The launcher message is a CreateProcess failure, before game initialization.
The only source delta in the local release commit is texture prefetch budgeting
and VERSION. This does not explain that dialog. Binary packaging was checked
separately above. The actual updater download/wait/relaunch UI sequence, DLSS5
archive, affected PC, older CPUs and installed antivirus policy were not tested.
The compatibility binary was run on this machine, not on a pre-AVX2 CPU.

Local commit eb63c5e fixes UTF-8 paths passed into ANSI process creation and adds
Windows error/path diagnostics. It is not published and is not proof of the
remote cause. Shared launch helper tests include Unicode/spaced paths and
missing executable/directory failures. Host ctest 17/17; native ctest 4/4.

Initial direct package runs omitted --sys-dir and failed on missing MxScn.dat.
Those were harness errors corrected before the successful tests, not a release
regression. Do not use those initial logs as evidence of the reported problem.

## Information needed from the affected installation

Ask whether the update used the launcher or manual extraction, and whether it
was standard or experimental. Ask Windows version/CPU, update.log if present,
and the exact error when opening melee_port.exe and melee_port_compat.exe directly.
Direct execution may show a subsequent missing-ISO prompt; that means process
creation succeeded and is distinct from the launcher error.

Ask for a fresh standard ZIP extracted into a NEW local folder, with no copied
old settings, and test the launcher there using the existing ISO. Keep the old
folder/saves intact. If it works, focus on the original path, settings or update
copy. If not, ask for Windows Security Protection History entries for those files
without asking the player to disable protection or restore a quarantined file.
If needed compare the downloaded ZIP hash with the published digest above.

No shipping worktrees modified, no release assets changed and nothing pushed.
