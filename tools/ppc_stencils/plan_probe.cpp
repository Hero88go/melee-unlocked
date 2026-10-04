// The translator's planner as a command line filter, for tools/ppc_stencils/coverage.py.
// It reads functions from standard input and says, for each, whether the planner accepts it and
// which instructions it refuses and why. It translates nothing and runs nothing.
//
// Input, one function per line:   <hex address> <hex word> <hex word> ...
// Output, one line per function:  ok <stencils> <unreached words> [u<first>-<last> ...]
//                            or:  no <unreached words> [u<first>-<last> ...] <index>:<Reason> ...
// An index equal to the word count is a refusal of the whole function. Unreached words are the
// ones no path from the entry reaches (inline data, data pools, dead code); they are not planned.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "leaf_translation_plan.h"
#include <cstdio>
#include <iostream>
#include <sstream>

int main() {
  std::string line;
  ppc::stencil::Plan plan;
  std::vector<ppc::stencil::Refused> refused;
  while (std::getline(std::cin, line)) {
    std::istringstream fields(line);
    uint32_t address = 0, word = 0;
    if (!(fields >> std::hex >> address)) continue;
    std::vector<uint8_t> code;
    while (fields >> std::hex >> word)
      for (int shift : {24, 16, 8, 0}) code.push_back(uint8_t(word >> shift));
    std::string error;
    if (ppc::stencil::plan_function(code.data(), code.size(), address, plan, error, &refused)) {
      std::printf("ok %zu %u", plan.stencils.size(), plan.unreachable);
      for (const auto& run : plan.unreachable_runs) std::printf(" u%u-%u", run.first, run.second);
      std::printf("\n");
      continue;
    }
    std::printf("no %u", plan.unreachable);
    for (const auto& run : plan.unreachable_runs) std::printf(" u%u-%u", run.first, run.second);
    for (const auto& item : refused) std::printf(" %u:%s", item.index, ppc::stencil::refusal_name(item.reason));
    std::printf("\n");
  }
  return 0;
}
