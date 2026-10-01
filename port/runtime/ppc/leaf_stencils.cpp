// Translator stencils: one compiled function per instruction form, chained by tail jumps.
// Operand symbols become read-only literal cells; Next becomes the following stencil's entry.
// Each body is the expression port/recomp/emit.py writes for that instruction, with the register
// numbers and immediates read from cells in place of literals, and the same ppc.h helpers.
// A branch stencil has a second exit, Taken. No stencil calls anything: where the recompiled code
// would call (the back-edge poll), the chain returns a nonzero exit number to CompiledLeaf::run.
// Compile with /O2 /Ob2 /Gy /GS- /GL-. The extractor rejects calls, unwind records and non-tail Next.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc.h"

extern "C" {
extern const uint32_t mu_stencil_destination;
extern const uint32_t mu_stencil_source;
extern const uint32_t mu_stencil_immediate;
extern const uint32_t mu_stencil_source2;
extern const uint32_t mu_stencil_immediate2;
uint32_t mu_stencil_next(ppc::Context&, uint8_t*);
uint32_t mu_stencil_taken(ppc::Context&, uint8_t*);

// The result is 0 for a guest return and an exit number otherwise; the value passes through
// every tail jump untouched.
#define MU_STENCIL(name) __declspec(noinline) uint32_t mu_stencil_##name(ppc::Context& c, uint8_t* m)
#define MU_NEXT return mu_stencil_next(c, m)
#define MU_TAKEN return mu_stencil_taken(c, m)
#define D c.r[mu_stencil_destination]
#define S c.r[mu_stencil_source]
#define B c.r[mu_stencil_source2]
#define I mu_stencil_immediate
#define J mu_stencil_immediate2

// ---- integer immediates (first slice) ----
MU_STENCIL(add) { // addi, addis, li, lis: RA=0 reads zero.
  const uint32_t source = mu_stencil_source;
  const uint32_t value = (source ? c.r[source] : 0u) + mu_stencil_immediate;
  c.r[mu_stencil_destination] = value;
  MU_NEXT;
}
// Spelled out: `or` and `xor` are operator tokens, which must not meet the paste in MU_STENCIL.
__declspec(noinline) uint32_t mu_stencil_or(ppc::Context& c, uint8_t* m) { D = S | I; MU_NEXT; }
__declspec(noinline) uint32_t mu_stencil_xor(ppc::Context& c, uint8_t* m) { D = S ^ I; MU_NEXT; }
MU_STENCIL(and_record) {
  const uint32_t value = S & I;
  D = value;
  ppc::cr0(c, value); // The recompiler's exact CR0 and XER.SO semantics.
  MU_NEXT;
}
MU_STENCIL(return) { (void)c; (void)m; return 0; }

// ---- integer register forms: D = rD, S = rA, B = rB ----
MU_STENCIL(add_reg) { D = S + B; MU_NEXT; }
MU_STENCIL(subf) { D = B - S; MU_NEXT; }
MU_STENCIL(mullw) { D = (uint32_t)((int32_t)S * (int32_t)B); MU_NEXT; }
MU_STENCIL(mulhw) { D = (uint32_t)(((int64_t)(int32_t)S * (int64_t)(int32_t)B) >> 32); MU_NEXT; }
MU_STENCIL(mulhwu) { D = (uint32_t)(((uint64_t)S * (uint64_t)B) >> 32); MU_NEXT; }
MU_STENCIL(divw) { D = ppc::divw((int32_t)S, (int32_t)B); MU_NEXT; }
MU_STENCIL(divwu) { D = ppc::divwu(S, B); MU_NEXT; }
MU_STENCIL(neg) { D = 0u - S; MU_NEXT; }
MU_STENCIL(addc) { { uint32_t a = S, b = B; D = a + b; c.ca = ppc::carry(a, b); } MU_NEXT; }
MU_STENCIL(adde) {
  { uint32_t a = S, b = B, k = c.ca; D = a + b + k;
    c.ca = ppc::carry(a, b) || (k && ppc::carry(a + b, k)); }
  MU_NEXT;
}
MU_STENCIL(addze) { { uint32_t a = S, k = c.ca; D = a + k; c.ca = ppc::carry(a, k); } MU_NEXT; }
MU_STENCIL(addme) { { uint32_t a = S, k = c.ca; D = a + k - 1u; c.ca = ppc::carry(a, k - 1u); } MU_NEXT; }
MU_STENCIL(subfc) {
  { uint32_t a = S, b = B; D = b - a; c.ca = (a == 0) || ppc::carry(b, 0u - a); }
  MU_NEXT;
}
// subfe has no stencil: every form of emit.py's statement compiles with a saved register
// (push rbx), so it needs unwind data and the extractor rejects it. The planner refuses it.
MU_STENCIL(subfze) { { uint32_t a = ~S, k = c.ca; D = a + k; c.ca = ppc::carry(a, k); } MU_NEXT; }
MU_STENCIL(subfme) { { uint32_t a = ~S, k = c.ca; D = a + k - 1u; c.ca = ppc::carry(a, k - 1u); } MU_NEXT; }
// I is the sign-extended immediate, as emit.py's hexs(simm) literal.
MU_STENCIL(addic) { { uint32_t a = S; D = a + I; c.ca = ppc::carry(a, I); } MU_NEXT; }
MU_STENCIL(subfic) { { uint32_t a = S; D = I - a; c.ca = (a == 0) || ppc::carry(0u - a, I); } MU_NEXT; }
MU_STENCIL(mulli) { D = (uint32_t)((int32_t)S * (int32_t)I); MU_NEXT; }

// ---- logical, shift and rotate forms: D = rA, S = rS, B = rB ----
MU_STENCIL(and_reg) { D = S & B; MU_NEXT; }
MU_STENCIL(or_reg) { D = S | B; MU_NEXT; } // Also mr.
MU_STENCIL(xor_reg) { D = S ^ B; MU_NEXT; }
MU_STENCIL(nand) { D = ~(S & B); MU_NEXT; }
MU_STENCIL(nor) { D = ~(S | B); MU_NEXT; }
MU_STENCIL(eqv) { D = ~(S ^ B); MU_NEXT; }
MU_STENCIL(andc) { D = S & ~B; MU_NEXT; }
MU_STENCIL(orc) { D = S | ~B; MU_NEXT; }
MU_STENCIL(extsb) { D = (uint32_t)(int32_t)(int8_t)S; MU_NEXT; }
MU_STENCIL(extsh) { D = (uint32_t)(int32_t)(int16_t)S; MU_NEXT; }
MU_STENCIL(cntlzw) { D = ppc::cntlzw(S); MU_NEXT; }
MU_STENCIL(slw) { D = (B & 0x20) ? 0u : (S << (B & 31)); MU_NEXT; }
MU_STENCIL(srw) { D = (B & 0x20) ? 0u : (S >> (B & 31)); MU_NEXT; }
MU_STENCIL(sraw) { D = ppc::sraw(c, S, B); MU_NEXT; }
MU_STENCIL(srawi) { D = ppc::srawi(c, S, (int)I); MU_NEXT; }
// I is SH and J is the mask emit.py computes from MB and ME at translation time.
MU_STENCIL(rlwinm) { D = _rotl(S, (int)I) & J; MU_NEXT; }
MU_STENCIL(rlwnm) { D = _rotl(S, B & 31) & J; MU_NEXT; }
MU_STENCIL(rlwimi) { D = (D & ~J) | (_rotl(S, (int)I) & J); MU_NEXT; }
// Rc=1 forms: emit.py appends ppc::cr0(c, <destination register>) after the operation.
MU_STENCIL(record) { ppc::cr0(c, S); MU_NEXT; }

// ---- compare and condition register: D = CR field or bit, S = rA, source field or bit ----
MU_STENCIL(cmpw) { ppc::cr_set_s(c, (int)mu_stencil_destination, (int32_t)S, (int32_t)B); MU_NEXT; }
MU_STENCIL(cmplw) { ppc::cr_set_u(c, (int)mu_stencil_destination, S, B); MU_NEXT; }
MU_STENCIL(cmpwi) { ppc::cr_set_s(c, (int)mu_stencil_destination, (int32_t)S, (int32_t)I); MU_NEXT; }
MU_STENCIL(cmplwi) { ppc::cr_set_u(c, (int)mu_stencil_destination, S, I); MU_NEXT; }
MU_STENCIL(mfcr) { D = ppc::mfcr(c); MU_NEXT; }
MU_STENCIL(mtcrf) { ppc::mtcrf(c, I, S); MU_NEXT; } // I is CRM.
MU_STENCIL(mcrf) { c.cr[mu_stencil_destination] = c.cr[mu_stencil_source]; MU_NEXT; }
// crand, cror, crxor, crnand, crnor, creqv, crandc and crorc have no stencil: with the three bit
// numbers read from cells, ppc::crbit and ppc::crbit_set compile to code that saves rbx, rsi and
// rdi, so it needs unwind data and the extractor rejects it. The planner refuses them.
// mfspr and mtspr of LR, CTR and XER, the three SPRs emit.py reads and writes in place.
MU_STENCIL(mflr) { D = c.lr; MU_NEXT; }
MU_STENCIL(mtlr) { c.lr = S; MU_NEXT; }
MU_STENCIL(mfctr) { D = c.ctr; MU_NEXT; }
MU_STENCIL(mtctr) { c.ctr = S; MU_NEXT; }
MU_STENCIL(mfxer) { D = ((c.so << 31) | (c.ov << 30) | (c.ca << 29)); MU_NEXT; }
MU_STENCIL(mtxer) { c.so = (S >> 31) & 1; c.ov = (S >> 30) & 1; c.ca = (S >> 29) & 1; MU_NEXT; }

// ---- branches inside one function ----
// emit.py's cond_expr tests CTR first, then the CR bit, and stops at the first failure. Each
// test is one stencil: Taken is the next test or the branch destination, Next the fall-through.
MU_STENCIL(jump) { MU_NEXT; }
MU_STENCIL(branch_cr_set) { if (c.cr[mu_stencil_source] & I) MU_TAKEN; MU_NEXT; } // I is 8 >> (BI & 3).
MU_STENCIL(branch_cr_clear) { if (!(c.cr[mu_stencil_source] & I)) MU_TAKEN; MU_NEXT; }
MU_STENCIL(branch_ctr_nonzero) { if (--c.ctr != 0) MU_TAKEN; MU_NEXT; }
MU_STENCIL(branch_ctr_zero) { if (--c.ctr == 0) MU_TAKEN; MU_NEXT; }
// ppc::backedge split at its call. Next is the loop target. Taken is an Exit stencil: the chain
// returns its number, run() calls ppc::loop_poll in ordinary compiled code and re-enters at the
// target. The counter and its 1024 interval are the ones in ppc.h; the tests compare them.
MU_STENCIL(backedge) { if ((++c.backedges & 0x3FFu) == 0) MU_TAKEN; MU_NEXT; }
MU_STENCIL(exit) { (void)c; (void)m; return I; }
}
