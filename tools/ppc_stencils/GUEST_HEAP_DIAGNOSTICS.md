# Static guest allocation failure diagnostics

`MELEE_TRACE_GUEST_HEAP=1` installs entry observers in hidden automated runs. With the variable absent
or any other value, no observer is installed and no heap metadata is read. The code changes no
allocator, ARAM, heap size, guest register or guest memory behavior. The trace enables the existing
function-entry diagnostic path, so its runs are not performance measurements.

The failure observer accepts exactly `__assert` at 0x80388220, caller LR 0x80015098, line 233 and the
literal filename `lbmemory.c`. The compiled `__assert` calls ppc::enter before its prologue overwrites
r30. That entry mechanism reaches direct compiled callers; observing OSPanic instead would lose the
allocator's register values. An entirely RAM-local branch that bypasses enter cannot be observed.

At the accepted site, r24 holds the heap handle and r30 holds the rounded request. The allocator's
caller LR is saved at r1+60. The allocation entry observer also remembers the original requested
size and caller, but they are reported as available only when the handle, rounded size and stack
match. Missing entry observations are explicitly unavailable. The heap walk checks every read,
address ordering, overlap, size overflow, cycles and the retail 131-descriptor bound. Partial or
corrupt lists have `complete=0` and an error; their partial gap counts are not a total.

Files changed: port/runtime/host/host.cpp and host.h. New parser and tests:
port/runtime/host/guest_heap_trace.h and port/tests/guest_heap_trace_test.cpp. The synthetic C++ test
is included in the standalone tools/ppc_stencils CMake project. The MSVC AVX2 Release test passed on
2026-10-01 (`run-source/rel09-b3-first/ctest-fixed.log`), including bounds, gaps, cycles, overflow and
read-only checks. The diagnostic host itself and an ACE reproduction remain separate work.

## Existing evidence

`run-source/rel085-final/wp7-20260930/ace/vs-0/port.log` shows menu major 01 at frame 341 (line 885),
title major 00 at frame 387 (line 890), and the failing allocator assertion (lines 893 to 896).
The digest ends at frame 506; no match started. The same folder's results.json records a successful
2,400-frame boot and a failing VS script. No requested size or failing heap list was captured.

This assertion means no sufficiently large contiguous gap was selected. It does not establish
host process memory exhaustion or an interpreter bug. The existing overflow report in native
lbmemory.c is under MU_NATIVE and does not run on the Static Recomp.

## Next authorized measurement

After building the diagnostic host in an isolated 0.9 folder, run the existing
`run-source/rel085-final/x-akaneia-static/vs.txt` on ACE with a new copy of the same settings/card.
Keep `--hidden --volume 0 --no-music`, explicit scratch settings, card, replay, user and log paths,
and `MELEE_TRACE_GUEST_HEAP=1`. No visible window, focus change or key injection is needed.

Read the `[guest-heap]` lines before the fatal report. If the heap is consistent, compare the rounded
request against total free and largest gap to distinguish depletion from fragmentation. If it is
inconsistent, follow the first invalid descriptor or bounds. The caller address identifies the
resource load. Only then measure its allocation/free lifecycle through the title transition.

One separate candidate remains unconnected: hle_stubs.cpp's ARInit resets its stack metadata every
call while the SDK ar.c returns immediately once initialized. ARAlloc also omits the SDK's alignment,
capacity and free-descriptor checks. Existing logs do not show repeated ARInit or a corrupted ARAM
stack, so no ARAM initialization or allocation behavior was changed.
