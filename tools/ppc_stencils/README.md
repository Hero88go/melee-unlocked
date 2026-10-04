# Copy-and-patch translator prototype (B3)

This isolated prototype translates one bounded PowerPC function into native code by copying
compiler-produced stencils and patching their relocation holes. It covers integer forms, compares,
the condition register, loads and stores, float and paired-single forms, branches and loops with
the recompiler's back-edge poll, calls through the dispatch (bl, bctrl, tail branches, bctr), and
the local calls, inline data and blrl of mod code.

No game dispatch calls this prototype. No Source Port code is translated. No game image is read.
B3 remains a prototype: nothing here is wired into the Static Recomp. What that step needs is
written in `run-source/rel09-b3-third/PROGRESS.md`.

## How it works

- `port/runtime/ppc/leaf_stencils.cpp` holds one small C++ function per instruction form. Each body
  is the statement `port/recomp/emit.py` writes for that instruction, with the same `ppc.h` helpers
  (cr0, carry, sraw, ld32, st32, fmadd, f25, fctiw, call, ...). Register numbers and immediates are
  read from five literal cells (Destination, Source, Immediate, Source2, Immediate2) in place of
  the literals emit.py prints. Every stencil ends in a tail jump to Next.
- `tools/extract_ppc_stencils.py` reads the MSVC COFF object at our build time and writes the table.
  It fails closed (see "Two kinds of stencil").
- `leaf_translation_plan.h` decodes the guest words once into a graph of stencil instances. Only
  canonical encodings are accepted, and every refusal has a reason.
- `leaf_translator.cpp` validates the table again at run time, copies the stencils, writes the
  cells, patches the holes, makes the pages read and execute only and registers the unwind data.
  No instruction is decoded during execution.

The table format is version 3. Stencils return a `uint32_t`: 0 means the guest function returned.

## Two kinds of stencil

A **leaf stencil** has no stack frame, no unwind data and no reference outside its own cells and
jumps. The extractor rejects it for any unwind record, any relocation to a symbol that is not one
of its holes, a nonzero addend, overlapping holes, a hole set other than the declared one, a Next
or Taken reference that is not a jump, or anything after the final jump. 110 of the 197 stencils
have no stack frame.

A **call-capable stencil** is marked `C` in the operation list (`stencil_format.h` and the
extractor carry the same list; a test compares them). It is the same kind of source text, compiled
as an ordinary non-leaf function that ends in its epilogue and a tail jump to Next. Only a
call-capable stencil may have:

- A stack frame with unwind data. The extractor reads the `.pdata` records and the `UNWIND_INFO`
  from `.xdata` and admits only version 1, no handler, no frame register and the plain unwind
  codes (push, stack allocation, saved register, saved xmm). The records must cover the whole
  stencil. MSVC saves registers in the middle of one stencil (stmw) and chains the later ranges to
  the first (`UNW_FLAG_CHAININFO`); chained records are admitted when their parent is an earlier
  record of the same stencil.
- References to the host, from an explicit list in the extractor (`EXTERNALS`): a REL32 to a host
  function (`ppc::mmio_read`, `ppc::call`, `ppc::locked_cache`, ...), the `__ImageBase` plus
  ADDR32NB form MSVC uses for `g_ram_watched` and `g_ram_versions`, and import pointers
  (`__imp_trunc`), which become 8-byte slots in the translation. Any other symbol rejects the
  object. The generated header names each host symbol as a C++ address expression, so the linker
  of the program that includes the table resolves them against the runtime it is linked with.
- REL32 references to read-only compiler constants (`__real@...`), whose bytes are copied into the
  table.

87 stencils have a frame. Where MSVC declines to inline a `ppc.h` accessor (st8 in the four stb
stencils), the stencil calls the host's own copy of that inline function, which is on the list.

At translation time:

- The code is allocated within 2 GB of the host image, so every REL32 reaches without a thunk and
  the ADDR32NB references need no rewriting. If no such range is free the translation is refused.
- Every host symbol must be inside one loaded image (the one `__ImageBase` then stands for).
- Each copy of a framed stencil gets its own `RUNTIME_FUNCTION` entries and `UNWIND_INFO` copies in
  the same allocation; a chained `UNWIND_INFO` is completed with the copy's own parent entry. The
  table is registered with `RtlAddFunctionTable` and removed with `RtlDeleteFunctionTable` when
  the translation is freed. Leaf copies get no entry: Windows treats code without an entry as a
  leaf function whose return address is at RSP, which is true for them.

The tests walk the stack with `RtlLookupFunctionEntry` and `RtlVirtualUnwind` from a host function
called under translated code (two translated functions deep, and from under load, store, stmw and
stfd stencils), and throw a C++ exception (`ppc::GuestLongJmp`) from the host across translated
frames 34,000 times, the chained stmw ranges included.

## Accepted instructions (197 stencils)

- Immediates: addi, addis, addic, addic., subfic, mulli, ori, oris, xori, xoris, andi., andis.
- Register arithmetic, each with its record form: add, subf, mullw, mulhw, mulhwu, divw, divwu,
  addc, adde, subfc, subfe, neg, addze, addme, subfze, subfme.
- Logical, shift and rotate, each with its record form: and, or, xor, nand, nor, eqv, andc, orc,
  slw, srw, sraw, srawi, extsb, extsh, cntlzw, rlwinm, rlwimi, rlwnm.
- Compare and condition register: cmpw, cmplw, cmpwi, cmplwi, mfcr, mtcrf, mcrf, mcrxr, crand,
  cror, crxor, crnand, crnor, creqv, crandc, crorc.
- Special registers emit.py reads and writes in place: mflr, mtlr, mfctr, mtctr, mfxer, mtxer.
- Loads and stores: lbz, lhz, lha, lwz, stb, sth, stw, each with its update, indexed and
  update-indexed form; lmw, stmw; lwbrx, lhbrx, stwbrx, sthbrx.
- Float loads and stores: lfs, lfd, stfs, stfd with their update and indexed forms; stfiwx.
- Float: fadd, fsub, fmul, fdiv, fmadd, fmsub, fnmadd, fnmsub and their single forms, fres,
  frsqrte, fsel, frsp, fmr, fneg, fabs, fnabs, fcmpu, fcmpo, fctiw, fctiwz, mffs, mtfsf, mtfsb0,
  mtfsb1, mtfsfi, mcrfs.
- Paired singles: psq_l, psq_lu, psq_lx, psq_lux, psq_st, psq_stu, psq_stx, psq_stux (the host's
  `ppc::psq_load` and `ppc::psq_store`), ps_add, ps_sub, ps_mul, ps_div, ps_muls0, ps_muls1,
  ps_madd, ps_msub, ps_nmadd, ps_nmsub, ps_madds0, ps_madds1, ps_sum0, ps_sum1, ps_sel, ps_res,
  ps_rsqrte, ps_mr, ps_neg, ps_abs, ps_nabs, ps_merge00, ps_merge01, ps_merge10, ps_merge11,
  ps_cmpu0, ps_cmpo0, ps_cmpu1, ps_cmpo1.
- Instructions emit.py writes nothing for: sync, isync, eieio, dcbst, dcbf, dcbt, dcbtst, dcbi,
  icbi. They plan as nothing.
- Branches inside the function: b, bc with every BO and BI, bclr with every BO and BI.
- Calls and tail calls through `ppc::call`: bl, bla, bcl (every BO and BI) to a target outside the
  function; bctrl and conditional bcctrl; b, ba and bc to a target outside the function (the call,
  then a return); bctr and conditional bcctr.
- Local calls: bl and bcl to a target inside the function, with the local returns of blr; blrl
  and conditional bclrl. See "Local calls, inline data, blrl and computed returns".

## Refused, and why

The planner gives one of these reasons (`leaf_translation_plan.h`); `ppc_leaf_refusal_test.cpp`
reaches every one.

- **CounterBranch**: bcctr that decrements CTR, an invalid form.
- **OverflowEnable**: OE=1 arithmetic. emit.py models no overflow flag.
- **FloatRecord**: Rc=1 on a float or paired-single form. emit.py writes no CR1 update.
- **InvalidUpdate**: an update form with RA=0, or an integer load-update with RA=RD.
- **InvalidMultiple**: lmw with RA in the loaded range.
- **SpecialRegister**: mfspr and mtspr of anything but XER, LR and CTR (GQR, HID0, DEC, the time
  base, ...). emit.py models them; no stencil yet.
- **NoStencil**: sc, rfi, lwarx, stwcx., mfmsr, mtmsr, mftb, mfsr, mfsrin, mtsr, mtsrin, tlbie,
  tlbsync, lswi, stswi, lswx, stswx, dcbz, dcbz_l. emit.py models them; no stencil yet.
- **DroppedByEmit**: tw and twi. emit.py drops traps; the planner does not translate a trap as
  nothing.
- **ReservedBits**: a set reserved field, a compare with L=1, mtocrf, a record bit on a form that
  has none, BH hints on bclr and bcctr.
- **NotDecoded**: a word `gekko.py` does not decode, or decodes and emit.py has no statement for
  (fsqrt).
- **FallsOffEnd**: a path from the entry runs past the last word (a call, a conditional branch or
  an instruction that plans as nothing in last place). Words no path reaches are not planned and
  refuse nothing.
- **InvalidRange**: no code, an unaligned or out-of-RAM address, or more than 16,384 instructions.

## Local calls, inline data, blrl and computed returns

Only words a path from the entry reaches are planned. Data pools after a return, dead code and
inline data are never looked at as instructions.

emit.py keeps state per invocation of a function: `uint32_t lrs[32]; uint32_t lrn = 0;` when the
function makes local calls, and `const uint32_t entry_lr = c.lr;` for blrl. The translation keeps
the same state in the stack frame of its driver, `CompiledLeaf::run`, and the stencils stay
stateless: each statement that uses the state is an Exit of the chain with a kind
(`ExitKind` in `leaf_translation_plan.h`), and the driver does what emit.py writes.

- **Local call.** A `bl` or `bcl` whose target is one of the function's own addresses, as
  analyze.py decides for cave code. emit.py: `c.lr = ret; lrs[lrn++ & 31u] = ret; goto L_target;`.
  Plan: a SetLr stencil, then Exit(LocalCall).
- **Return in a function with local calls.** emit.py:
  `{ uint32_t t = c.lr; if (ppc::local_return(lrs, lrn, t)) { if (t == r) goto L_r; ... } return; }`.
  Plan: Exit(LocalReturn). The driver calls the same `ppc::local_return`.
- **Tail call in a function with local calls.** emit.py reads LR, makes the call, then does the
  local return test on the LR it read. Plan: Exit(TailCall) or Exit(TailCallCtr); the driver makes
  the call.
- **blrl.** emit.py: `{ uint32_t t = c.lr; c.lr = ret; [local return test] if (t == entry_lr)
  return; ppc::call(c, m, t); }`. Plan: Exit(LinkReturn), which resumes at the next instruction.
- **Inline data.** `bl` over data to an mflr gives the code the guest address of its data: LR is
  the address of the first data word, as on the console, and a load through it reads the
  function's own words from guest RAM. The same shape is also a call of a subroutine that saves
  LR first. The planner tells them apart by the words themselves: they are first planned as the
  code a subroutine would return to, and only when one of the words between an unconditional `bl`
  and a target that starts with mflr cannot be planned are they taken as data. Data is never
  planned. emit.py does write statements for those words (a `ppc::fatal` for one that does not
  decode) and a `goto` to them in every return's chain; nothing reaches them in a correct program.
  If a return does go there, the translation calls `ppc::fatal` and stops, where the recompiled
  code would run the data words as instructions.
- **Computed returns.** For a callee that can return to its caller's LR plus K (analyze.py
  `_computed_return_delta`), emit.py writes `++ppc::g_computed_return_checks; c.lr = ret;
  call; if (c.lr == ret + K) { ++ppc::g_resumed_returns; goto L; }` for every K that lands on one
  of the caller's own words. The planner does the same (CallChecked and ResumeTest stencils) when
  it is told the callee's deltas: `translate_leaf` takes a list of callees, and
  `computed_returns()` in `leaf_translation_plan.h` is analyze.py's rule for finding the deltas in
  a callee's words. Without that list a call is the plain call, which is what emit.py writes for
  a callee without computed returns.

## What "exact" means, and its limits

Every stencil is compiled from the statement emit.py writes, and the tests compare it bit for bit
with emit.py's own C++ and with a hand-written reference. Three places need words:

1. **NaN operand order.** For `a + b`, `a * c` and the fused multiply-adds with two or more NaN
   operands, the processor returns the payload of whichever NaN the compiler put first, and C++
   lets the compiler choose. MSVC does choose differently from site to site: the fadd stencil
   loads fB first, the same statement in the reference's switch loads fA first. So the recompiled
   game itself has no single answer here, and neither can a stencil. Everything else about NaNs
   (quieting, payloads through single operand forms, conversions, compares, fsel, subtraction and
   division) is compared exactly. The tests accept either NaN in a float register for a single
   instruction of this kind and set longer functions aside when the reference meets one; the
   counts are printed. A defined order would need `ppc.h` helpers with explicit NaN handling,
   which is the owner's decision.
2. **Calls.** Every call out of the function is `c.lr = ret; ppc::call(c, m, target);`, the
   statement emit.py writes for a target that is not one of the recompiler's own functions. For
   a known function emit.py writes a direct C++ call, which differs only in `c.call_depth` during
   the callee. The resume test for computed returns is exact when the callee's deltas are given
   and absent when they are not; who knows a callee's deltas is an integration question (for
   translated callees `computed_returns()` finds them; for the game's own functions the
   recompiler's analysis has them). emit.py also resolves a bctr or bctrl whose CTR it can prove
   constant, and writes a `switch` for a bctr with a jump table; the planner plans both through
   the dispatch.
3. **Cave rule.** analyze.py treats a `bl` inside a function in two ways: as a local call in cave
   code and as an ordinary call when caller and target are both inside a named function's body
   (and, for the standalone copies it makes of Slippi's caves, as a direct call of the copy that
   starts at the target). Mod code in RAM has no named function, so the planner always uses the
   local call. A function that calls its own entry is therefore a local call too.
4. **emit.py, not hardware.** addme and subfme compute CA as `carry(a, k - 1)`; update forms with
   RA=0 are refused instead of copied. The stencils follow emit.py.

## Branches and the back-edge poll

emit.py writes a branch as `if (cond) { [ppc::backedge(c);] goto L; }`. Each test of the condition
is one stencil with two exits. `ppc::backedge` is `if ((++c.backedges & 0x3FFu) == 0) loop_poll(c);`:
the Backedge stencil increments and tests the counter and on the 1024th time jumps to an Exit
stencil, which returns a nonzero exit number. `CompiledLeaf::run` calls `ppc::loop_poll` and
re-enters the chain at the branch target. This stayed as it was: it works, it is tested, and it is
not a call in the middle of a stencil. With call-capable stencils it could become a direct call.

## The alternative that was not taken

The second set of slices proposed keeping every stencil leaf and leaving the chain for anything
that needs a call. It avoids unwind data, but it needs `ppc.h` split into fast and slow parts or a
second copy of the fast path, and it gives calls, crash reports and guest longjmp no correct
stack. The owner asked for call-capable stencils with registered unwind data instead.

## Checks already run

All of this ran on 2026-10-04 with MSVC 19.44 (toolset 14.44.35207), Release, for the AVX2 baseline
in `build-ppc-stencils` and the SSE2 baseline in `build-ppc-stencils-sse2`. Eleven CTest suites
pass in each. Counts are identical in both.

- `port_ppc_stencil_extract`: 73 synthetic COFF tests. New: unwind data kept, unsupported versions,
  flags, frame registers and codes, records that do not cover the stencil, handler relocations,
  chained records and their parents, listed and unlisted host symbols in call-capable and in leaf
  stencils, image-relative data, import slots, constants and what is not a constant.
- `port_ppc_leaf_translator`: the first slice's test.
- `port_ppc_leaf_integer`: 111 forms (subfe, the CR logical forms, mcrxr and the instructions that
  plan as nothing added). 1,110,000 random full-context comparisons, 6,027,568 boundary
  comparisons, 61 known answers, 32,000 comparisons over 2,000 random functions.
- `port_ppc_leaf_branch`: as before, 197,152 comparisons, 19,067,767 back-edges, 49,445 polls.
- `port_ppc_leaf_memory`: 59 forms in full-size guest RAM. 45 hand-computed known answers (byte
  order, sign extension, update, the four mirrors, RA=0, the locked cache, I/O call counts, the
  last byte of RAM, write generations of watched, unwatched and straddled blocks). 354,000 random
  comparisons, 12,264 at exact boundary addresses, 18,000 over 1,500 random functions. 658,310
  host calls, 26,901 generation bumps and 443,564 written RAM pages compared.
- `port_ppc_leaf_float`: 63 forms. 54 known answers, 378,000 random comparisons, 604,800 over every
  pair of 40 special doubles in 5 host float modes (the four rounding modes, and flush-to-zero with
  denormals-are-zero), 16,375 over 1,500 random functions. Either NaN accepted in 1,104 single
  instruction comparisons, 1,625 function samples set aside (limit 1 above).
- `port_ppc_leaf_call`: plans, table tampering for unwind data and host references, known calls
  (call and continue, recursion 300 deep, tail call, bctrl, bctr, a call in a loop), stack walks,
  34,000 exceptions, 24,576 comparisons over every BO and BI as bcl, tail bc, bcctrl and bcctr,
  9,410 over 600 generated call graphs of four translated functions.
- `port_ppc_leaf_refusal`: 133 refused functions; every one of the 12 reasons is reached and named.
- `port_ppc_leaf_local`: plans (what is reached, what is data, where a local call returns, the
  resume tests), 19 hand-computed known answers (the data idiom and a read of the data, bl +4,
  a return into data stopping the translation, subroutines, nested subroutines, a conditional
  local call, tail calls from a subroutine and from the main body, the three cases of blrl, the
  32-entry ring, a resumed return), and 47,811 comparisons over 4,000 generated functions with
  18,758 local call sites; 27,941 resume tests ran and 12,486 resumed past the call.
- `port_ppc_leaf_emit_corpus`: the reference is not written by hand. `generate_emit_corpus.py` runs
  the unmodified `gekko.py` and `emit.py` over 6,616 generated functions (78,067 guest words: every
  form, 600 straight-line functions, 500 with branches and loops, 300 with calls and tail calls,
  600 with local subroutines, inline data, blrl and callees with computed returns, emitted with
  the `local_returns`, `has_blrl` and callee deltas analyze.py would find). 211,328 three-way
  comparisons of native code, emit.py's C++ and the hand-written reference.
- `port_guest_heap_trace`: unchanged.
- Mutation check: deliberately wrong stencils, driver and planner, one changed place each. See
  `mutation-check.log` and `mutation-check-local-calls.log` in `run-source/rel09-b3-third/`.

In every comparison the whole `ppc::Context` is compared byte for byte, with guest RAM (every page
any executor wrote, found with the system's write watch), the locked cache, the write generations,
every host call with its arguments and the context it saw, the polls, and the MXCSR control bits.

Table SHA-256: AVX2 `8f9c305bc57e94a74b32c17fbd984e30c002303d8bfffd2707f7f517b4734dc1`, SSE2 `62bb3fd9eec03c16c1bf3d92a4ad00d70f3470a44968ce7e7498bab7b8722796`. Evidence, logs, both extracted tables and object
disassemblies are in `run-source/rel09-b3-third/`. Earlier evidence stays in
`run-source/rel09-b3-first/` and `run-source/rel09-b3-second/`.

## Coverage of real mod code

`tools/ppc_stencils/coverage.py` (read-only) takes raw PowerPC blobs, Gecko code lists and Melee
Code Manager library files, splits them into functions, asks the planner (through
`ppc_leaf_plan_probe`, which is `leaf_translation_plan.h` and nothing else) and reports the share
of instructions and of whole functions accepted and the most common refused forms. A raw blob is
split at `bl` targets and prologues. Words no path reaches are tried as functions of their own
(code reached only through a pointer) and otherwise reported as data or unplanned code. The run
of 2026-10-04 is in `run-source/rel09-b3-third/coverage.txt` and `coverage.json`: 22,864 of 23,220
instructions (98.5%) and 468 of 515 functions (90.9%) over the Hack Pack's AI engine and code
library and the Slippi code files in the tree. The run before local calls existed is kept beside
it as `coverage-before-local-calls.txt`.

## Build procedure

Use the same MSVC AMD64 compiler as the host, in a separate build folder. The CPU baseline and
`/fp:precise` match the current host and guest. The stencil object is compiled with `/Ob3`.

```powershell
cmake -S tools/ppc_stencils -B build-ppc-stencils -G "Visual Studio 17 2022" -A x64 -DMELEE_CPU_BASELINE=AVX2
cmake --build build-ppc-stencils --config Release --parallel 2
ctest --test-dir build-ppc-stencils -C Release --output-on-failure
```

Repeat in `build-ppc-stencils-sse2` with `-DMELEE_CPU_BASELINE=SSE2` for the compatible baseline.
The programs use synthetic CPU state only. They create no game window, load no game or disc, and
use no audio or input devices.

## Review findings and limits

- MSVC may change a stencil's shape with a compiler update: a new saved register in a leaf
  stencil, a handler, a frame register, a symbol that is not on the list. Extraction rejects these
  and the build stops. Which accessors are inlined already differs from stencil to stencil.
- The fused multiply-add intrinsics of `ppc.h` compile to FMA instructions on both baselines,
  because that is what `ppc.h` asks for. A host CPU without FMA is outside this prototype's reach
  for the same reason it is outside the recompiled game's.
- A record form is two stencils and a conditional branch is up to four. There is no
  cross-instruction optimization. No speed or latency claim is made, and nothing was measured.
- A wrong translation of a loop does not return. CTest gives each native test 300 seconds.
- Code is copied into writable pages, then changed to executable and read-only, and the
  instruction cache is flushed. Entries are aligned to 16 bytes. This standalone build does not
  enable `/guard:cf`, so it is not a test of a CFG-instrumented host.
- `CompiledLeaf::run` rejects a nonzero `c.entry` and changed guest bytes, and calls `ppc::enter`
  once. The byte comparison per call is a validation measure of the prototype.
- The function tables are registered per translation. With thousands of translations one growable
  table (`RtlAddGrowableFunctionTable`) per code region would be the better fit; the entries and
  unwind data would be laid out the same way.
- The table fingerprint identifies stencil bytes, holes, unwind data, host symbol names and
  constants. No disk cache reads it yet.

The approved larger design and its Jev revision remain in `run-source/rel085-final/X-design.md`.
