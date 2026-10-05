// Translator stencils: one compiled function per instruction form, chained by tail jumps.
// Operand symbols become read-only literal cells; Next becomes the following stencil's entry.
// Each body is the expression port/recomp/emit.py writes for that instruction, with the register
// numbers and immediates read from cells in place of literals, and the same ppc.h helpers.
// A branch stencil has a second exit, Taken. A leaf stencil calls nothing: for the back-edge poll
// the chain returns a nonzero exit number to CompiledLeaf::run. A call-capable stencil (marked C in
// stencil_format.h) is an ordinary non-leaf function: it may call the ppc.h runtime (memory slow
// paths, ppc::call, ...), and the translator registers its unwind data for every copy.
// Compile with /O2 /Ob2 /Gy /GS- /GL-. The extractor rejects a frame, a call or a host reference in
// a leaf stencil, any host symbol that is not on its list, and a non-tail Next everywhere.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc.h"
#include "ram_translator.h"

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
MU_STENCIL(code_guard) {
  if (mu_ram_invalidated ||
      (mu_ram_version0 && mu_ram_version0->load(std::memory_order_relaxed) != mu_ram_expected0) ||
      (mu_ram_version1 && mu_ram_version1->load(std::memory_order_relaxed) != mu_ram_expected1))
    mu_ram_translation_guard(I);
  MU_NEXT;
}
MU_STENCIL(invalidate_code) {
  const uint32_t ra = mu_stencil_source;
  mu_ram_translation_invalidate(((ra ? c.r[ra] : 0u) + B) & ~31u, 32);
  MU_NEXT;
}

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
// Call-capable only because MSVC saves a register for it (push rbx); it calls nothing.
MU_STENCIL(subfe) {
  { uint32_t a = ~S, b = B, k = c.ca; D = a + b + k;
    c.ca = ppc::carry(a, b) || ppc::carry(a + b, k); }
  MU_NEXT;
}
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
// CR logical forms: D = crbD, S = crbA, B = crbB (bit numbers). Call-capable only because MSVC
// saves registers for them; they call nothing.
#define MU_CR_LOGICAL(name, expression) \
  MU_STENCIL(name) { \
    { uint32_t a = ppc::crbit(c, (int)mu_stencil_source), b = ppc::crbit(c, (int)mu_stencil_source2); \
      ppc::crbit_set(c, (int)mu_stencil_destination, expression); } \
    MU_NEXT; \
  }
MU_CR_LOGICAL(crand, a & b) MU_CR_LOGICAL(cror, a | b) MU_CR_LOGICAL(crxor, a ^ b)
MU_CR_LOGICAL(crnand, !(a & b)) MU_CR_LOGICAL(crnor, !(a | b)) MU_CR_LOGICAL(creqv, !(a ^ b))
MU_CR_LOGICAL(crandc, a & !b) MU_CR_LOGICAL(crorc, a | !b)
MU_STENCIL(mcrxr) {
  c.cr[mu_stencil_destination] = (uint8_t)((c.so << 3) | (c.ov << 2) | (c.ca << 1)); c.so = c.ov = c.ca = 0;
  MU_NEXT;
}
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

// ---- loads and stores (call-capable): the accessors of ppc.h, slow paths and all ----
// emit.py's ea_d and ea_x: RA=0 reads zero. The update forms use RA as written.
#define EA_D ((mu_stencil_source ? c.r[mu_stencil_source] : 0u) + I)
#define EA_X ((mu_stencil_source ? c.r[mu_stencil_source] : 0u) + B)
#define MU_LD8(ea) ppc::ld8(c, m, ea)
#define MU_LD16(ea) ppc::ld16(c, m, ea)
#define MU_LD16A(ea) (uint32_t)(int32_t)(int16_t)ppc::ld16(c, m, ea)
#define MU_LD32(ea) ppc::ld32(c, m, ea)
// D = rD, S = rA, I = displacement, B = rB.
#define MU_LOAD_FAMILY(name, load) \
  MU_STENCIL(name) { D = load(EA_D); MU_NEXT; } \
  MU_STENCIL(name##u) { { uint32_t ea = S + I; D = load(ea); S = ea; } MU_NEXT; } \
  MU_STENCIL(name##x) { D = load(EA_X); MU_NEXT; } \
  MU_STENCIL(name##ux) { { uint32_t ea = S + B; D = load(ea); S = ea; } MU_NEXT; }
MU_LOAD_FAMILY(lbz, MU_LD8) MU_LOAD_FAMILY(lhz, MU_LD16) MU_LOAD_FAMILY(lha, MU_LD16A) MU_LOAD_FAMILY(lwz, MU_LD32)
// D = rS, the value stored.
#define MU_STORE_FAMILY(name, store) \
  MU_STENCIL(name) { store(c, m, EA_D, D); MU_NEXT; } \
  MU_STENCIL(name##u) { { uint32_t ea = S + I; store(c, m, ea, D); S = ea; } MU_NEXT; } \
  MU_STENCIL(name##x) { store(c, m, EA_X, D); MU_NEXT; } \
  MU_STENCIL(name##ux) { { uint32_t ea = S + B; store(c, m, ea, D); S = ea; } MU_NEXT; }
MU_STORE_FAMILY(stb, ppc::st8) MU_STORE_FAMILY(sth, ppc::st16) MU_STORE_FAMILY(stw, ppc::st32)
MU_STENCIL(lmw) {
  { uint32_t ea = EA_D; for (int i = (int)mu_stencil_destination; i < 32; ++i, ea += 4) c.r[i] = ppc::ld32(c, m, ea); }
  MU_NEXT;
}
MU_STENCIL(stmw) {
  { uint32_t ea = EA_D; for (int i = (int)mu_stencil_destination; i < 32; ++i, ea += 4) ppc::st32(c, m, ea, c.r[i]); }
  MU_NEXT;
}
MU_STENCIL(lwbrx) { D = ppc::ld32r(c, m, EA_X); MU_NEXT; }
MU_STENCIL(lhbrx) { D = ppc::ld16r(c, m, EA_X); MU_NEXT; }
MU_STENCIL(stwbrx) { ppc::st32r(c, m, EA_X, D); MU_NEXT; }
MU_STENCIL(sthbrx) { ppc::st16r(c, m, EA_X, D); MU_NEXT; }

// ---- float registers: D = fD (or fS of a store), S = fA, B = fB, I = fC ----
#define FD0 c.f[mu_stencil_destination].ps0
#define FD1 c.f[mu_stencil_destination].ps1
#define UD0 c.f[mu_stencil_destination].u0
#define UD1 c.f[mu_stencil_destination].u1
#define FA0 c.f[mu_stencil_source].ps0
#define FA1 c.f[mu_stencil_source].ps1
#define UA0 c.f[mu_stencil_source].u0
#define UA1 c.f[mu_stencil_source].u1
#define FB0 c.f[mu_stencil_source2].ps0
#define FB1 c.f[mu_stencil_source2].ps1
#define UB0 c.f[mu_stencil_source2].u0
#define UB1 c.f[mu_stencil_source2].u1
#define FC0 c.f[mu_stencil_immediate].ps0
#define FC1 c.f[mu_stencil_immediate].ps1

// Float loads and stores. The address is integer: S = rA, I = displacement, B = rB.
MU_STENCIL(lfs) { FD0 = FD1 = ppc::float_bits_to_double(ppc::ld32(c, m, EA_D)); MU_NEXT; }
MU_STENCIL(lfsu) { { uint32_t ea = S + I; FD0 = FD1 = ppc::float_bits_to_double(ppc::ld32(c, m, ea)); S = ea; } MU_NEXT; }
MU_STENCIL(lfsx) { FD0 = FD1 = ppc::float_bits_to_double(ppc::ld32(c, m, EA_X)); MU_NEXT; }
MU_STENCIL(lfsux) { { uint32_t ea = S + B; FD0 = FD1 = ppc::float_bits_to_double(ppc::ld32(c, m, ea)); S = ea; } MU_NEXT; }
MU_STENCIL(lfd) { UD0 = ppc::ld64(c, m, EA_D); MU_NEXT; }
MU_STENCIL(lfdu) { { uint32_t ea = S + I; UD0 = ppc::ld64(c, m, ea); S = ea; } MU_NEXT; }
MU_STENCIL(lfdx) { UD0 = ppc::ld64(c, m, EA_X); MU_NEXT; }
MU_STENCIL(lfdux) { { uint32_t ea = S + B; UD0 = ppc::ld64(c, m, ea); S = ea; } MU_NEXT; }
MU_STENCIL(stfs) { ppc::st32(c, m, EA_D, ppc::double_to_float_bits(FD0)); MU_NEXT; }
MU_STENCIL(stfsu) { { uint32_t ea = S + I; ppc::st32(c, m, ea, ppc::double_to_float_bits(FD0)); S = ea; } MU_NEXT; }
MU_STENCIL(stfsx) { ppc::st32(c, m, EA_X, ppc::double_to_float_bits(FD0)); MU_NEXT; }
MU_STENCIL(stfsux) { { uint32_t ea = S + B; ppc::st32(c, m, ea, ppc::double_to_float_bits(FD0)); S = ea; } MU_NEXT; }
MU_STENCIL(stfd) { ppc::st64(c, m, EA_D, UD0); MU_NEXT; }
MU_STENCIL(stfdu) { { uint32_t ea = S + I; ppc::st64(c, m, ea, UD0); S = ea; } MU_NEXT; }
MU_STENCIL(stfdx) { ppc::st64(c, m, EA_X, UD0); MU_NEXT; }
MU_STENCIL(stfdux) { { uint32_t ea = S + B; ppc::st64(c, m, ea, UD0); S = ea; } MU_NEXT; }
MU_STENCIL(stfiwx) { ppc::st32(c, m, EA_X, (uint32_t)UD0); MU_NEXT; }

// Double precision scalar (ps0 only), then single precision (result duplicated to ps1).
MU_STENCIL(fadd) { FD0 = FA0 + FB0; MU_NEXT; }
MU_STENCIL(fsub) { FD0 = FA0 - FB0; MU_NEXT; }
MU_STENCIL(fmul) { FD0 = FA0 * FC0; MU_NEXT; }
MU_STENCIL(fdiv) { FD0 = FA0 / FB0; MU_NEXT; }
MU_STENCIL(fmadd) { FD0 = ppc::fmadd(FA0, FC0, FB0); MU_NEXT; }
MU_STENCIL(fmsub) { FD0 = ppc::fmsub(FA0, FC0, FB0); MU_NEXT; }
MU_STENCIL(fnmadd) { FD0 = ppc::fnmadd(FA0, FC0, FB0); MU_NEXT; }
MU_STENCIL(fnmsub) { FD0 = ppc::fnmsub(FA0, FC0, FB0); MU_NEXT; }
MU_STENCIL(fadds) { FD0 = FD1 = ppc::fs(FA0 + FB0); MU_NEXT; }
MU_STENCIL(fsubs) { FD0 = FD1 = ppc::fs(FA0 - FB0); MU_NEXT; }
MU_STENCIL(fmuls) { FD0 = FD1 = ppc::fs(FA0 * ppc::f25(FC0)); MU_NEXT; }
MU_STENCIL(fdivs) { FD0 = FD1 = ppc::fs(FA0 / FB0); MU_NEXT; }
MU_STENCIL(fmadds) { FD0 = FD1 = ppc::fs(ppc::fmadd(FA0, ppc::f25(FC0), FB0)); MU_NEXT; }
MU_STENCIL(fmsubs) { FD0 = FD1 = ppc::fs(ppc::fmsub(FA0, ppc::f25(FC0), FB0)); MU_NEXT; }
MU_STENCIL(fnmadds) { FD0 = FD1 = ppc::fs(ppc::fnmadd(FA0, ppc::f25(FC0), FB0)); MU_NEXT; }
MU_STENCIL(fnmsubs) { FD0 = FD1 = ppc::fs(ppc::fnmsub(FA0, ppc::f25(FC0), FB0)); MU_NEXT; }
MU_STENCIL(fres) { FD0 = FD1 = ppc::fres(FB0); MU_NEXT; }
MU_STENCIL(frsqrte) { FD0 = ppc::frsqrte(FB0); MU_NEXT; }
MU_STENCIL(frsp) { FD0 = FD1 = ppc::fs(FB0); MU_NEXT; }
MU_STENCIL(fmr) { UD0 = UB0; MU_NEXT; }
MU_STENCIL(fneg) { UD0 = UB0 ^ 0x8000000000000000ull; MU_NEXT; }
MU_STENCIL(fabs) { UD0 = UB0 & 0x7FFFFFFFFFFFFFFFull; MU_NEXT; }
MU_STENCIL(fnabs) { UD0 = UB0 | 0x8000000000000000ull; MU_NEXT; }
MU_STENCIL(fsel) { FD0 = (FA0 >= -0.0) ? FC0 : FB0; MU_NEXT; }
MU_STENCIL(fcmp) { ppc::fcmp(c, (int)mu_stencil_destination, FA0, FB0); MU_NEXT; } // fcmpu, fcmpo, ps_cmpu0, ps_cmpo0
MU_STENCIL(fctiw) { UD0 = ppc::fctiw(FB0, false); MU_NEXT; }
MU_STENCIL(fctiwz) { UD0 = ppc::fctiw(FB0, true); MU_NEXT; }
MU_STENCIL(mffs) { UD0 = 0xFFF8000000000000ull | c.fpscr; MU_NEXT; }
// I is the mask of the fields kept (emit.py's hexs(~m)) and J the mask of the fields written.
MU_STENCIL(mtfsf) { c.fpscr = (c.fpscr & I) | ((uint32_t)UB0 & J); ppc::update_mxcsr(c); MU_NEXT; }
MU_STENCIL(mtfsb0) { c.fpscr &= ~I; ppc::update_mxcsr(c); MU_NEXT; } // I is 0x80000000 >> crbD.
MU_STENCIL(mtfsb1) { c.fpscr |= I; ppc::update_mxcsr(c); MU_NEXT; }
// I is 0xF << sh and J is imm << sh, with sh = 28 - 4 * crfD.
MU_STENCIL(mtfsfi) { c.fpscr = (c.fpscr & ~I) | J; ppc::update_mxcsr(c); MU_NEXT; }
MU_STENCIL(mcrfs) { c.cr[mu_stencil_destination] = (uint8_t)((c.fpscr >> I) & 15); MU_NEXT; } // I is 28 - 4 * crfS.

// ---- paired singles ----
// Quantized loads and stores are host calls. J is W | (I << 1); the displacement is 12 bits.
#define MU_PSQ_FAMILY(name, call) \
  MU_STENCIL(name) { call(c, m, EA_D, mu_stencil_destination, J & 1u, J >> 1); MU_NEXT; } \
  MU_STENCIL(name##u) { { uint32_t ea = S + I; call(c, m, ea, mu_stencil_destination, J & 1u, J >> 1); S = ea; } MU_NEXT; } \
  MU_STENCIL(name##x) { call(c, m, EA_X, mu_stencil_destination, J & 1u, J >> 1); MU_NEXT; } \
  MU_STENCIL(name##ux) { { uint32_t ea = S + B; call(c, m, ea, mu_stencil_destination, J & 1u, J >> 1); S = ea; } MU_NEXT; }
MU_PSQ_FAMILY(psq_l, ppc::psq_load) MU_PSQ_FAMILY(psq_st, ppc::psq_store)
MU_STENCIL(ps_add) { { double x = FA0 + FB0, y = FA1 + FB1; FD0 = ppc::fs(x); FD1 = ppc::fs(y); } MU_NEXT; }
MU_STENCIL(ps_sub) { { double x = FA0 - FB0, y = FA1 - FB1; FD0 = ppc::fs(x); FD1 = ppc::fs(y); } MU_NEXT; }
MU_STENCIL(ps_mul) { { double x = FA0 * ppc::f25(FC0), y = FA1 * ppc::f25(FC1); FD0 = ppc::fs(x); FD1 = ppc::fs(y); } MU_NEXT; }
MU_STENCIL(ps_div) { { double x = FA0 / FB0, y = FA1 / FB1; FD0 = ppc::fs(x); FD1 = ppc::fs(y); } MU_NEXT; }
MU_STENCIL(ps_muls0) { { double k = ppc::f25(FC0); double x = FA0 * k, y = FA1 * k; FD0 = ppc::fs(x); FD1 = ppc::fs(y); } MU_NEXT; }
MU_STENCIL(ps_muls1) { { double k = ppc::f25(FC1); double x = FA0 * k, y = FA1 * k; FD0 = ppc::fs(x); FD1 = ppc::fs(y); } MU_NEXT; }
#define MU_PS_FMA(name, fn) \
  MU_STENCIL(name) { \
    { double x = fn(FA0, ppc::f25(FC0), FB0), y = fn(FA1, ppc::f25(FC1), FB1); FD0 = ppc::fs(x); FD1 = ppc::fs(y); } \
    MU_NEXT; \
  }
MU_PS_FMA(ps_madd, ppc::fmadd) MU_PS_FMA(ps_msub, ppc::fmsub) MU_PS_FMA(ps_nmadd, ppc::fnmadd) MU_PS_FMA(ps_nmsub, ppc::fnmsub)
MU_STENCIL(ps_madds0) {
  { double k = ppc::f25(FC0); double x = ppc::fmadd(FA0, k, FB0), y = ppc::fmadd(FA1, k, FB1); FD0 = ppc::fs(x); FD1 = ppc::fs(y); }
  MU_NEXT;
}
MU_STENCIL(ps_madds1) {
  { double k = ppc::f25(FC1); double x = ppc::fmadd(FA0, k, FB0), y = ppc::fmadd(FA1, k, FB1); FD0 = ppc::fs(x); FD1 = ppc::fs(y); }
  MU_NEXT;
}
MU_STENCIL(ps_sum0) { { double x = FA0 + FB1, y = FC1; FD0 = ppc::fs(x); FD1 = ppc::fs(y); } MU_NEXT; }
MU_STENCIL(ps_sum1) { { double x = FC0, y = FA0 + FB1; FD0 = ppc::fs(x); FD1 = ppc::fs(y); } MU_NEXT; }
MU_STENCIL(ps_res) { { double x = ppc::fres(FB0), y = ppc::fres(FB1); FD0 = ppc::fs(x); FD1 = ppc::fs(y); } MU_NEXT; }
MU_STENCIL(ps_rsqrte) { { double x = ppc::frsqrte(FB0), y = ppc::frsqrte(FB1); FD0 = ppc::fs(x); FD1 = ppc::fs(y); } MU_NEXT; }
MU_STENCIL(ps_sel) {
  { double x = (FA0 >= -0.0) ? FC0 : FB0, y = (FA1 >= -0.0) ? FC1 : FB1; FD0 = x; FD1 = y; }
  MU_NEXT;
}
MU_STENCIL(ps_mr) { { uint64_t x = UB0, y = UB1; UD0 = x; UD1 = y; } MU_NEXT; }
MU_STENCIL(ps_neg) { { uint64_t x = UB0 ^ 0x8000000000000000ull, y = UB1 ^ 0x8000000000000000ull; UD0 = x; UD1 = y; } MU_NEXT; }
MU_STENCIL(ps_abs) { { uint64_t x = UB0 & 0x7FFFFFFFFFFFFFFFull, y = UB1 & 0x7FFFFFFFFFFFFFFFull; UD0 = x; UD1 = y; } MU_NEXT; }
MU_STENCIL(ps_nabs) { { uint64_t x = UB0 | 0x8000000000000000ull, y = UB1 | 0x8000000000000000ull; UD0 = x; UD1 = y; } MU_NEXT; }
MU_STENCIL(ps_merge00) { { uint64_t x = UA0, y = UB0; UD0 = x; UD1 = y; } MU_NEXT; }
MU_STENCIL(ps_merge01) { { uint64_t x = UA0, y = UB1; UD0 = x; UD1 = y; } MU_NEXT; }
MU_STENCIL(ps_merge10) { { uint64_t x = UA1, y = UB0; UD0 = x; UD1 = y; } MU_NEXT; }
MU_STENCIL(ps_merge11) { { uint64_t x = UA1, y = UB1; UD0 = x; UD1 = y; } MU_NEXT; }
MU_STENCIL(fcmp_ps1) { ppc::fcmp(c, (int)mu_stencil_destination, FA1, FB1); MU_NEXT; } // ps_cmpu1, ps_cmpo1

// ---- calls through the dispatch ----
// emit.py's _call and _tail for a target that is not one of the recompiler's own functions, and
// its bctrl and bctr. The translated frame stays on the stack during the call; its unwind data is
// registered, so a crash report or a guest longjmp (a C++ exception) crosses it correctly.
MU_STENCIL(call) { c.lr = I; ppc::call(c, m, J); MU_NEXT; }             // I = return address, J = target
MU_STENCIL(call_ctr) { { uint32_t t = c.ctr; c.lr = I; ppc::call(c, m, t); } MU_NEXT; }
MU_STENCIL(tail_call) { ppc::call(c, m, I); MU_NEXT; }                   // Next is a Return stencil.
MU_STENCIL(tail_call_ctr) { ppc::call(c, m, c.ctr); MU_NEXT; }
// A local call (emit.py _local_call): `c.lr = ret;` here, then an Exit; the driver remembers the
// return address for this invocation and resumes at the target.
MU_STENCIL(set_lr) { c.lr = I; MU_NEXT; }
// emit.py _call for a callee with computed returns: the counted call, then one test per address
// the callee may have left in LR.
MU_STENCIL(call_checked) { ++ppc::g_computed_return_checks; c.lr = I; ppc::call(c, m, J); MU_NEXT; }
MU_STENCIL(resume_test) { if (c.lr == I) { ++ppc::g_resumed_returns; MU_TAKEN; } MU_NEXT; }
}
