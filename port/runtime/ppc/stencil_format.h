// Copy-and-patch table produced from our MSVC object, never from a player's compiler.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace ppc::stencil {
constexpr uint32_t kFormatVersion = 3;
// The first seven are the cell and jump holes of a stencil instance. The last three exist only in
// a call-capable stencil: a REL32 to a host symbol, an image-relative (ADDR32NB) reference to a
// host symbol, and a REL32 to a compiler constant copied into the table.
enum class Hole : uint8_t { Destination, Source, Immediate, Next, Source2, Immediate2, Taken,
                            External, ExternalRva, Constant };
constexpr uint32_t kHoleCount = 7;     // Cell and jump holes.
constexpr uint32_t kHoleKindCount = 10; // All relocation kinds.
constexpr uint32_t kCellCount = 5; // Literal cells per stencil instance; Next and Taken are jumps.
namespace holes {
constexpr uint32_t D = 1u << 0, S = 1u << 1, I = 1u << 2, N = 1u << 3, B = 1u << 4, J = 1u << 5, T = 1u << 6;
// Not a hole. C marks a call-capable stencil: it may have a stack frame with unwind data and may
// reference the host symbols on the extractor's list. Without C a stencil must be leaf.
constexpr uint32_t C = 1u << 7;
constexpr uint32_t kHoleMask = C - 1;
}
// One row per stencil: Operation name and the exact hole set its compiled code must reference.
// tools/extract_ppc_stencils.py carries the same list; a test compares the two.
// Rows are only ever appended, so an Operation keeps its number.
#define MU_STENCIL_OPERATIONS(X) \
  X(Add, D|S|I|N) X(Or, D|S|I|N) X(Xor, D|S|I|N) X(AndRecord, D|S|I|N) X(Return, 0) \
  X(AddReg, D|S|B|N) X(Subf, D|S|B|N) X(Mullw, D|S|B|N) X(Mulhw, D|S|B|N) X(Mulhwu, D|S|B|N) \
  X(Divw, D|S|B|N) X(Divwu, D|S|B|N) X(Neg, D|S|N) \
  X(Addc, D|S|B|N) X(Adde, D|S|B|N) X(Addze, D|S|N) X(Addme, D|S|N) \
  X(Subfc, D|S|B|N) X(Subfze, D|S|N) X(Subfme, D|S|N) \
  X(Addic, D|S|I|N) X(Subfic, D|S|I|N) X(Mulli, D|S|I|N) \
  X(AndReg, D|S|B|N) X(OrReg, D|S|B|N) X(XorReg, D|S|B|N) X(Nand, D|S|B|N) X(Nor, D|S|B|N) \
  X(Eqv, D|S|B|N) X(Andc, D|S|B|N) X(Orc, D|S|B|N) \
  X(Extsb, D|S|N) X(Extsh, D|S|N) X(Cntlzw, D|S|N) \
  X(Slw, D|S|B|N) X(Srw, D|S|B|N) X(Sraw, D|S|B|N) X(Srawi, D|S|I|N) \
  X(Rlwinm, D|S|I|J|N) X(Rlwnm, D|S|B|J|N) X(Rlwimi, D|S|I|J|N) X(Record, S|N)   X(Cmpw, D|S|B|N) X(Cmplw, D|S|B|N) X(Cmpwi, D|S|I|N) X(Cmplwi, D|S|I|N)   X(Mfcr, D|N) X(Mtcrf, S|I|N) X(Mcrf, D|S|N)   X(Mflr, D|N) X(Mtlr, S|N) X(Mfctr, D|N) X(Mtctr, S|N) X(Mfxer, D|N) X(Mtxer, S|N) \
  X(Jump, N) X(BranchCrSet, S|I|N|T) X(BranchCrClear, S|I|N|T) \
  X(BranchCtrNonzero, N|T) X(BranchCtrZero, N|T) X(Backedge, N|T) X(Exit, I) \
  /* Third set. Forms that were refused by shape, now call-capable. */ \
  X(Subfe, D|S|B|N|C) \
  X(Crand, D|S|B|N|C) X(Cror, D|S|B|N|C) X(Crxor, D|S|B|N|C) X(Crnand, D|S|B|N|C) \
  X(Crnor, D|S|B|N|C) X(Creqv, D|S|B|N|C) X(Crandc, D|S|B|N|C) X(Crorc, D|S|B|N|C) \
  X(Mcrxr, D|N) \
  /* Loads and stores: D = rD or rS, S = rA, I = displacement, B = rB. */ \
  X(Lbz, D|S|I|N|C) X(Lhz, D|S|I|N|C) X(Lha, D|S|I|N|C) X(Lwz, D|S|I|N|C) \
  X(Lbzu, D|S|I|N|C) X(Lhzu, D|S|I|N|C) X(Lhau, D|S|I|N|C) X(Lwzu, D|S|I|N|C) \
  X(Lbzx, D|S|B|N|C) X(Lhzx, D|S|B|N|C) X(Lhax, D|S|B|N|C) X(Lwzx, D|S|B|N|C) \
  X(Lbzux, D|S|B|N|C) X(Lhzux, D|S|B|N|C) X(Lhaux, D|S|B|N|C) X(Lwzux, D|S|B|N|C) \
  X(Stb, D|S|I|N|C) X(Sth, D|S|I|N|C) X(Stw, D|S|I|N|C) \
  X(Stbu, D|S|I|N|C) X(Sthu, D|S|I|N|C) X(Stwu, D|S|I|N|C) \
  X(Stbx, D|S|B|N|C) X(Sthx, D|S|B|N|C) X(Stwx, D|S|B|N|C) \
  X(Stbux, D|S|B|N|C) X(Sthux, D|S|B|N|C) X(Stwux, D|S|B|N|C) \
  X(Lmw, D|S|I|N|C) X(Stmw, D|S|I|N|C) \
  X(Lwbrx, D|S|B|N|C) X(Lhbrx, D|S|B|N|C) X(Stwbrx, D|S|B|N|C) X(Sthbrx, D|S|B|N|C) \
  /* Float loads and stores: D = fD or fS. */ \
  X(Lfs, D|S|I|N|C) X(Lfsu, D|S|I|N|C) X(Lfsx, D|S|B|N|C) X(Lfsux, D|S|B|N|C) \
  X(Lfd, D|S|I|N|C) X(Lfdu, D|S|I|N|C) X(Lfdx, D|S|B|N|C) X(Lfdux, D|S|B|N|C) \
  X(Stfs, D|S|I|N|C) X(Stfsu, D|S|I|N|C) X(Stfsx, D|S|B|N|C) X(Stfsux, D|S|B|N|C) \
  X(Stfd, D|S|I|N|C) X(Stfdu, D|S|I|N|C) X(Stfdx, D|S|B|N|C) X(Stfdux, D|S|B|N|C) \
  X(Stfiwx, D|S|B|N|C) \
  /* Float register forms: D = fD (or CR field), S = fA, B = fB, I = fC. */ \
  X(Fadd, D|S|B|N) X(Fsub, D|S|B|N) X(Fmul, D|S|I|N) X(Fdiv, D|S|B|N) \
  X(Fmadd, D|S|I|B|N) X(Fmsub, D|S|I|B|N) X(Fnmadd, D|S|I|B|N) X(Fnmsub, D|S|I|B|N) \
  X(Fadds, D|S|B|N) X(Fsubs, D|S|B|N) X(Fmuls, D|S|I|N) X(Fdivs, D|S|B|N) \
  X(Fmadds, D|S|I|B|N) X(Fmsubs, D|S|I|B|N) X(Fnmadds, D|S|I|B|N) X(Fnmsubs, D|S|I|B|N) \
  X(Fres, D|B|N|C) X(Frsqrte, D|B|N|C) X(Frsp, D|B|N) \
  X(Fmr, D|B|N) X(Fneg, D|B|N) X(Fabs, D|B|N) X(Fnabs, D|B|N) X(Fsel, D|S|I|B|N|C) \
  X(Fcmp, D|S|B|N) X(Fctiw, D|B|N|C) X(Fctiwz, D|B|N|C) \
  X(Mffs, D|N) X(Mtfsf, B|I|J|N|C) X(Mtfsb0, I|N|C) X(Mtfsb1, I|N|C) X(Mtfsfi, I|J|N|C) \
  X(Mcrfs, D|I|N) \
  /* Paired singles. J of a quantized load or store is W | (I << 1). */ \
  X(PsqL, D|S|I|J|N|C) X(PsqLu, D|S|I|J|N|C) X(PsqLx, D|S|B|J|N|C) X(PsqLux, D|S|B|J|N|C) \
  X(PsqSt, D|S|I|J|N|C) X(PsqStu, D|S|I|J|N|C) X(PsqStx, D|S|B|J|N|C) X(PsqStux, D|S|B|J|N|C) \
  X(PsAdd, D|S|B|N) X(PsSub, D|S|B|N) X(PsMul, D|S|I|N) X(PsDiv, D|S|B|N) \
  X(PsMuls0, D|S|I|N) X(PsMuls1, D|S|I|N) \
  X(PsMadd, D|S|I|B|N|C) X(PsMsub, D|S|I|B|N|C) X(PsNmadd, D|S|I|B|N|C) X(PsNmsub, D|S|I|B|N|C) \
  X(PsMadds0, D|S|I|B|N) X(PsMadds1, D|S|I|B|N) X(PsSum0, D|S|I|B|N) X(PsSum1, D|S|I|B|N) \
  X(PsRes, D|B|N|C) X(PsRsqrte, D|B|N|C) X(PsSel, D|S|I|B|N|C) \
  X(PsMr, D|B|N) X(PsNeg, D|B|N) X(PsAbs, D|B|N) X(PsNabs, D|B|N) \
  X(PsMerge00, D|S|B|N) X(PsMerge01, D|S|B|N) X(PsMerge10, D|S|B|N) X(PsMerge11, D|S|B|N) \
  X(FcmpPs1, D|S|B|N) \
  /* Calls through the dispatch: I = return address or target, J = target. */ \
  X(Call, I|J|N|C) X(CallCtr, I|N|C) X(TailCall, I|N|C) X(TailCallCtr, N|C)   /* Fourth set. SetLr precedes the Exit of a local call. CallChecked and ResumeTest are a call of      a callee with computed returns: I = return address (ResumeTest: the address to resume at). */   X(SetLr, I|N) X(CallChecked, I|J|N|C) X(ResumeTest, I|N|T|C) \
  /* Runtime guards and explicit instruction-cache invalidation. */ \
  X(CodeGuard, I|N|C) X(InvalidateCode, S|B|N|C)
enum class Operation : uint8_t {
#define MU_STENCIL_ENUM(name, holes) name,
  MU_STENCIL_OPERATIONS(MU_STENCIL_ENUM)
#undef MU_STENCIL_ENUM
};
#define MU_STENCIL_COUNT(name, holes) +1u
constexpr uint32_t kOperationCount = 0u MU_STENCIL_OPERATIONS(MU_STENCIL_COUNT);
#undef MU_STENCIL_COUNT
// The declared row of an operation: its hole set, with holes::C when it is call-capable.
constexpr uint32_t operation_row(Operation operation) {
  using namespace holes;
  switch (operation) {
#define MU_STENCIL_HOLES(name, holes) case Operation::name: return (holes);
    MU_STENCIL_OPERATIONS(MU_STENCIL_HOLES)
#undef MU_STENCIL_HOLES
  }
  return ~0u;
}
constexpr uint32_t operation_holes(Operation operation) {
  const uint32_t row = operation_row(operation);
  return row == ~0u ? row : (row & holes::kHoleMask);
}
constexpr bool operation_may_call(Operation operation) {
  const uint32_t row = operation_row(operation);
  return row != ~0u && (row & holes::C) != 0;
}
constexpr const char* operation_name(Operation operation) {
  switch (operation) {
#define MU_STENCIL_NAME(name, holes) case Operation::name: return #name;
    MU_STENCIL_OPERATIONS(MU_STENCIL_NAME)
#undef MU_STENCIL_NAME
  }
  return "?";
}
// Literal cell of an operand hole, or kCellCount for the two jump holes.
constexpr uint32_t hole_cell(Hole hole) {
  switch (hole) {
    case Hole::Destination: return 0;
    case Hole::Source: return 1;
    case Hole::Immediate: return 2;
    case Hole::Source2: return 3;
    case Hole::Immediate2: return 4;
    default: return kCellCount;
  }
}
struct Relocation {
  uint32_t offset;
  Hole hole;
  int32_t addend;
  uint8_t bias; // AMD64 REL32_N: target - (patch + 4 + N).
  uint16_t index = 0; // External, ExternalRva: Table::externals. Constant: Table::constants.
};
// One unwind record of a call-capable stencil with a stack frame. Usually there is one, covering
// the whole stencil. MSVC writes several when it saves registers in the middle of a function: the
// later ranges are chained (UNW_FLAG_CHAININFO) to an earlier one.
struct UnwindRecord {
  uint32_t begin, end;   // The range of the stencil's code this record covers.
  const uint8_t* info;   // UNWIND_INFO. A chained one ends in a zeroed RUNTIME_FUNCTION, which the
  uint32_t info_size;    // translator fills in for every copy.
  int32_t parent;        // The earlier record a chained one continues, or -1.
};
struct Stencil {
  Operation operation;
  const uint8_t* code;
  size_t size;
  const Relocation* relocations;
  size_t relocation_count;
  // Null for a leaf. Sorted by begin; together the records cover [0, size).
  const UnwindRecord* unwind = nullptr;
  size_t unwind_count = 0;
};
// A host symbol a call-capable stencil references.
enum class ExternalKind : uint8_t {
  Direct,    // A function or object in the host image, reached by REL32 or by ADDR32NB.
  Slot,      // A `__imp_` pointer: the stencil reads an 8-byte slot holding `address`.
  ImageBase, // `__ImageBase`: the base of the host image, found at translation time.
};
struct External {
  const char* name;
  const void* address;
  ExternalKind kind;
};
// A read-only compiler constant (`__real@...`, `__xmm@...`) a stencil reads through REL32.
struct Constant {
  const uint8_t* bytes;
  size_t size;
  uint32_t alignment;
};
struct Table {
  uint32_t version;
  const char* sha256;
  const Stencil* stencils;
  size_t count;
  const External* externals = nullptr;
  size_t external_count = 0;
  const Constant* constants = nullptr;
  size_t constant_count = 0;
};
} // namespace ppc::stencil
