// Copy-and-patch table produced from our MSVC object, never from a player's compiler.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace ppc::stencil {
constexpr uint32_t kFormatVersion = 2;
enum class Hole : uint8_t { Destination, Source, Immediate, Next, Source2, Immediate2, Taken };
constexpr uint32_t kHoleCount = 7;
constexpr uint32_t kCellCount = 5; // Literal cells per stencil instance; Next and Taken are jumps.
namespace holes {
constexpr uint32_t D = 1u << 0, S = 1u << 1, I = 1u << 2, N = 1u << 3, B = 1u << 4, J = 1u << 5, T = 1u << 6;
}
// One row per stencil: Operation name and the exact hole set its compiled code must reference.
// tools/extract_ppc_stencils.py carries the same list; a test compares the two.
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
  X(BranchCtrNonzero, N|T) X(BranchCtrZero, N|T) X(Backedge, N|T) X(Exit, I)
enum class Operation : uint8_t {
#define MU_STENCIL_ENUM(name, holes) name,
  MU_STENCIL_OPERATIONS(MU_STENCIL_ENUM)
#undef MU_STENCIL_ENUM
};
#define MU_STENCIL_COUNT(name, holes) +1u
constexpr uint32_t kOperationCount = 0u MU_STENCIL_OPERATIONS(MU_STENCIL_COUNT);
#undef MU_STENCIL_COUNT
constexpr uint32_t operation_holes(Operation operation) {
  using namespace holes;
  switch (operation) {
#define MU_STENCIL_HOLES(name, holes) case Operation::name: return (holes);
    MU_STENCIL_OPERATIONS(MU_STENCIL_HOLES)
#undef MU_STENCIL_HOLES
  }
  return ~0u;
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
};
struct Stencil {
  Operation operation;
  const uint8_t* code;
  size_t size;
  const Relocation* relocations;
  size_t relocation_count;
};
struct Table {
  uint32_t version;
  const char* sha256;
  const Stencil* stencils;
  size_t count;
};
} // namespace ppc::stencil
