# Copy-and-patch translator prototype (B3)

This isolated prototype translates one bounded PowerPC function into native code by copying
compiler-produced stencils and patching their relocation holes. It now covers integer register
and immediate forms, compares, condition register moves, the XER/LR/CTR moves, and branches that
stay inside the function, including loops with the recompiler's back-edge poll.

No game dispatch calls this prototype. No Source Port code is translated. No game image is read.
B3 remains incomplete: there are no memory instructions, calls or floating point yet.

## How it works

- `port/runtime/ppc/leaf_stencils.cpp` holds one small C++ function per instruction form. Each body
  is the statement `port/recomp/emit.py` writes for that instruction, with the same `ppc.h` helpers
  (cr0, cr_set_s, cr_set_u, carry, sraw, srawi, cntlzw, divw, divwu, mfcr, mtcrf). Register numbers
  and immediates are read from five literal cells (Destination, Source, Immediate, Source2,
  Immediate2) in place of the literals emit.py prints. Every stencil ends in a tail jump to Next.
- `tools/extract_ppc_stencils.py` reads the MSVC COFF object at our build time and writes the table.
  It fails closed. A stencil is rejected for unwind data (any saved register or stack frame), any
  relocation to a symbol that is not one of the seven holes (so any helper call or data reference),
  a nonzero addend, overlapping holes, a hole set that differs from the one declared for that
  stencil, a Next or Taken reference that is not a jump, or anything after the final jump.
- `leaf_translation_plan.h` decodes the guest words once into a graph of stencil instances.
  Only canonical encodings are accepted. A record form (Rc=1) is its operation followed by a
  Record stencil, which is the order emit.py writes (`rd = ...; ppc::cr0(c, rd);`).
- `leaf_translator.cpp` validates the table again at run time, copies the stencils, writes the
  cells, patches the holes and makes the pages read and execute only. No instruction is decoded
  during execution.

The table format is version 2. Stencils return a `uint32_t`: 0 means the guest function returned.

## Accepted instructions (62 stencils)

- Immediates: addi, addis (li, lis), addic, addic., subfic, mulli, ori, oris, xori, xoris, andi.,
  andis.
- Register arithmetic, each with its record form: add, subf, mullw, mulhw, mulhwu, divw, divwu,
  addc, adde, subfc, neg, addze, addme, subfze, subfme.
- Logical, shift and rotate, each with its record form: and, or (mr), xor, nand, nor, eqv, andc,
  orc, slw, srw, sraw, srawi, extsb, extsh, cntlzw, rlwinm, rlwimi, rlwnm.
- Compare and condition register: cmpw, cmplw, cmpwi, cmplwi into any CR field, mfcr, mtcrf, mcrf.
- Special registers emit.py reads and writes in place: mflr, mtlr, mfctr, mtctr, mfxer, mtxer.
- Branches inside the function: b, bc with every BO and BI (CR bit, CTR, both; bdnz, bdz and the
  combined forms), bclr with every BO and BI (blr, beqlr, bdnzlr, ...).

## Rejected, and why

- subfe, subfe.: every source form of emit.py's statement compiles with `push rbx`, which needs
  unwind data. Nine forms were tried; none is leaf. The stencil was dropped, not patched around.
- crand, cror, crxor, crnand, crnor, creqv, crandc, crorc: with the three bit numbers read from
  cells, `ppc::crbit` and `ppc::crbit_set` compile with rbx, rsi and rdi saved. Dropped likewise.
- OE=1 arithmetic (addo, subfo, mullwo, nego, ...): emit.py ignores OE and models no overflow
  update, so there is no faithful form to copy. The planner refuses the word.
- Loads and stores: see the next section.
- bl, bla, ba, bcl, bclrl, bcctr, bcctrl and any branch whose target is outside the function:
  calls, tail calls and computed branches are later slices.
- Noncanonical words: set reserved fields, compares with L=1, mtocrf, SPRs other than XER, LR and
  CTR. The recompiler's decoder ignores some of these bits; the planner refuses them instead.
- A function whose last instruction can fall through, a function over 1024 instructions, and any
  entry other than the first instruction.

No instruction in the requested integer list was skipped for calling a helper: all of the helpers
above are inline expressions in ppc.h.

## Branches and the back-edge poll

emit.py writes a branch as `if (cond) { [ppc::backedge(c);] goto L; }`, where `cond` decrements
and tests CTR first, then tests the CR bit, short-circuit, and where a target at or before the
branch is a loop back-edge. `ppc::backedge` is
`if ((++c.backedges & 0x3FFu) == 0) loop_poll(c);`.

- Each test is one stencil with two exits: Taken (the next test or the destination) and Next (the
  fall-through). MSVC compiles these as `jcc Taken; jmp Next`. The extractor admits a conditional
  jump only in a stencil that declares Taken, and the last instruction must be a direct jump.
- `loop_poll` is a call, and a stencil may not call. The Backedge stencil therefore increments and
  tests the counter exactly as ppc.h does, and on the 1024th time jumps to an Exit stencil, which
  returns a nonzero exit number. `CompiledLeaf::run`, ordinary compiled C++ with normal unwind
  data, calls `ppc::loop_poll` and re-enters the chain at the branch target. The counter, the poll
  count and the whole context at every poll are compared with the recompiler's in the tests.
- Because the chain has already returned when the poll runs, no translated frame is ever on the
  stack during a host call. Nothing can unwind or longjmp through copied code.
- A conditional `bclr` jumps to a Return stencil of its own.

## Loads and stores: stopped, with the proposed shape

The memory slice was stopped as planned. A probe compiled emit.py's statements for lwz, stw and lbz
through `ppc::ld32`, `ppc::st32` and `ppc::ld8` (logs `slice3-memory-probe-*.log` in
`run-source/rel09-b3-second/`). The extractor rejected them, and the object shows why:

- The accessors are not leaf. The slow path calls `ppc::locked_cache`, `ppc::mmio_read` and
  `ppc::mmio_write`; the compiled stencil allocates shadow space and saves rbx, rsi, rdi (r14 too
  for stw), so it carries unwind data.
- A store also runs `ppc::mark_ram_write`, which reads `g_ram_watched` and bumps `g_ram_versions`.
  MSVC addresses both through `__ImageBase` with ADDR32NB relocations. Copied code is not at a
  fixed distance from the host image, so these cannot be patched as REL32 holes.

Smallest safe shape, not implemented: keep every stencil leaf and reuse the exit that the
back-edge poll already uses.

1. The stencil holds only the inline RAM fast path. A load computes the effective address in a
   register, tests `ppc::fast(m, ea)`, reads and byte-swaps, writes the destination register and
   tail-jumps to Next. It changes no guest state before the access has succeeded.
2. On a miss it jumps to an Exit stencil whose number names the guest instruction. `run` executes
   that one instruction through the ordinary accessor, in host code compiled from the same
   statement emit.py writes, and re-enters the chain at the following stencil. The slow path
   re-does the whole instruction, so its result does not depend on the stencil's test.
3. Stores: first version, every store exits to the driver, which calls `ppc::st32` and so marks
   write generations exactly as the game does. Measure it. Second version: one new 8-byte pointer
   cell for `g_ram_watched`, so a store to an unwatched RAM block stays inline and a watched block,
   a block-straddling write, the locked cache and MMIO exit.
4. Update forms write `ra = ea` in the stencil on the fast path and in the driver on the slow path.
5. This needs one owner decision outside the prototype: the fast path must be the same source text
   in the stencil and in the recompiled game. That means splitting each accessor in ppc.h into an
   inline RAM part and the rest (for example `ld32_ram` used by `ld32`). Rewriting the fast path a
   second time inside the stencil file would work but could drift from ppc.h.
6. Tests for it: a synthetic guest RAM compared before and after, both address mirrors, the last
   bytes of RAM, the locked cache range, an MMIO stub that records every call, watched and
   unwatched blocks with generation counts compared against the recompiler's statements.

Rejected alternative: true non-leaf stencils. They need unwind data extracted and registered per
copied instance (`RtlAddFunctionTable`), helper calls reached through placement within 2 GB of the
host image or through call thunks, and three more relocation kinds. They also put translated
frames under host calls, so a guest longjmp would unwind through copied code. The exit shape needs
none of that.

## Checks already run

All of this ran on 2026-10-01 with MSVC 19.44.35227, Release, for the AVX2 baseline in
`build-ppc-stencils` and the SSE2 baseline in `build-ppc-stencils-sse2`. Six CTest suites pass in
each. Counts are identical in both.

- `port_ppc_stencil_extract`: 48 synthetic COFF tests. Besides the first slice's cases: every
  declared hole is required, an extra hole is refused, a helper call is refused by name, duplicated
  direct tail jumps are admitted only when the stencil still ends in one, branch stencil layouts,
  Taken through a call or data reference, padding after the final jump, the return and exit
  shapes, and the operation list compared against `stencil_format.h`.
- `port_ppc_leaf_translator`: the first slice's test, adapted to the new return shape.
- `port_ppc_leaf_integer`: 91 instruction forms. 910,000 random full-context comparisons (10,000
  per form over 200 random encodings, all GPRs, CR, XER, CTR, LR and the rest of the context
  random), 5,710,048 boundary comparisons (carry in and out, overflow, shift counts 0, 31, 32, 33,
  63 and 64, every srawi count, wrapped rotate masks, sign extension, repeated registers), 61
  hand-computed known answers plus CR and SPR ones, 34 rejected words, negative controls, and
  32,000 comparisons over 2,000 random straight-line functions.
- `port_ppc_leaf_branch`: the planner's stencil graph for representative branches, 18 rejected
  functions, table tampering for branch and exit stencils, 9 loops with hand-computed results,
  back-edge counts and poll counts, 49,152 comparisons over every BO and BI as a forward bc, a
  backward bc and a bclr, 100,000 over 10 branch instructions (10,000 random contexts each), and
  48,000 over 3,000 generated functions with nested bounded loops. 18,958,407 back-edges were
  taken and 49,559 polls matched in count and in the context each poll saw.
- `port_ppc_leaf_emit_corpus`: the reference is not written by hand. At build time
  `generate_emit_corpus.py` runs the unmodified `port/recomp/gekko.py` and `emit.py` over 2,984
  generated functions (35,925 guest words: 24 encodings of every form, 300 straight-line
  functions, 500 functions with branches and loops) and writes the C++ the recompiler would. The
  test compares translated native code, that C++ and the hand-written reference three ways:
  143,232 full-context comparisons, 17,998 polls.
- Mutation check: 8 deliberately wrong stencils (poll interval, missing counter increment, a carry
  operand, an inverted CR test, bdnz without the decrement, rlwimi mask, signed cmplwi, record of
  the wrong value). Each was caught by at least two of the three native differential tests.
  See `mutation-check.log`.

In every comparison the whole `ppc::Context` is compared byte for byte and guest RAM is unchanged.

Table SHA-256: AVX2 `6ab0b053c11be06a9512c7e50ad275c4108f437c906a16e626d9a202c7ebb8a9`, SSE2
`3c84a297ca53517e5dfb65967bca406deb2ec7ba8bd8fc6fee50d1a72e15a535`. The 62 stencils total 3,111
bytes (AVX2) and 3,138 bytes (SSE2); the largest is mtcrf at 176 bytes. The return stencil is
exactly `33 C0 C3` and the exit stencil `8B 05 rel32 C3`. Evidence, logs, both extracted tables and
object disassemblies are in `run-source/rel09-b3-second/`. The first slice's evidence stays in
`run-source/rel09-b3-first/`.

## Build procedure

Use the same MSVC AMD64 compiler as the host, in a separate build folder. The CPU baseline and
`/fp:precise` match the current host and guest.

```powershell
cmake -S tools/ppc_stencils -B build-ppc-stencils -G "Visual Studio 17 2022" -A x64 -DMELEE_CPU_BASELINE=AVX2
cmake --build build-ppc-stencils --config Release --parallel 4
ctest --test-dir build-ppc-stencils -C Release --output-on-failure
```

Repeat in `build-ppc-stencils-sse2` with `-DMELEE_CPU_BASELINE=SSE2` for the compatible baseline.
The programs use synthetic CPU state only. They create no game window, load no game or disc, and
use no audio or input devices.

## Review findings and limits

- MSVC may emit unwind metadata, an unexpected helper, a nonzero relocation addend, a non-tail
  call, or padding after the final jump. Extraction rejects these forms. The tested compiler
  produces the required shape for the 62 stencils on both baselines; a different compiler, flag or
  baseline must pass the same extraction checks, and may lose a stencil the way subfe was lost.
- MSVC duplicates the tail of a stencil whose last statement branches (adde, subfc, subfic have two
  `jmp Next`). Several direct jumps to Next are admitted; a conditional jump to Next is admitted
  only in a branch stencil. Both rules are checked in the extractor and again at run time.
- The semantics are the recompiler's, not hardware's. One difference was noticed and left alone:
  emit.py's addme and subfme compute CA as `carry(a, k - 1)`, which is 0 when CA was 1 on entry,
  where hardware gives 1. The stencils reproduce emit.py bit for bit.
- A record form is two stencils and a conditional branch is up to four. There is no
  cross-instruction optimization and no fall-through elision. No speed or latency claim is made.
- A back-edge poll leaves the chain and re-enters it. That costs one return and one call per 1024
  back-edges; it is not measured.
- A wrong translation of a loop does not return. CTest gives each native test 300 seconds.
- Tail jumps preserve the original caller's return address and reuse the Windows AMD64 argument
  registers. No stencil may allocate stack space or have unwind metadata. See Microsoft's
  [x64 exception handling](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170).
- Code is copied into writable pages, then changed to executable/read-only, and the instruction
  cache is flushed. Entries are aligned to 16 bytes. Windows defaults newly executable pages to
  valid CFG targets; no custom invalid-target protection flag is used. This behavior is documented
  by Microsoft in [Control Flow Guard](https://learn.microsoft.com/en-us/windows/win32/secbp/control-flow-guard).
  This standalone build does not enable `/guard:cf`, so it is not a separate test of a
  CFG-instrumented host.
- The run method rejects nonzero c.entry and changed guest bytes, and calls ppc::enter exactly
  once. The per-call source comparison is a validation measure. Runtime discovery, worker
  scheduling, page generations, disk cache, jump tables, caves, computed returns, calls, memory
  instructions, FP/paired singles, hot-patching and all B3 replay gates remain open.
- The table fingerprint identifies stencil bytes and holes. No disk cache reads it yet. Supporting
  cached code later also requires the runtime format version and build/ABI identity.
- The analysis the recompiler applies to whole functions (labels, local subroutines, entries,
  jump tables) is not reused here. The planner derives branch targets from the words alone, which
  is the same result only for the simple functions this prototype accepts.

The approved larger design and its Jev revision remain in `run-source/rel085-final/X-design.md`.
