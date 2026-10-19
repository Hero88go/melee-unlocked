// Native ELF/ABI integration test using the user's own read-only base ISO.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_services.h"
#include <cstdio>
#include <string>

int main(int argc,char** argv) {
  if (argc!=3) { std::fputs("usage: linux_abi_loader_test ISO FIXTURE_SO\n",stderr); return 2; }
  linux_host::Config config;
  config.iso_path=argv[1]; config.game_path=argv[2]; config.headless=true;
  config.pace=false; config.max_frames=32; config.has_time_base=true;
  std::string error;
  if (!linux_host::initialize(config,&error)) { std::fprintf(stderr,"native ABI loader: %s\n",error.c_str()); return 1; }
  const int result=linux_host::run();
  MuStatePod state{};
  const bool passed=result==0 && linux_host::counters().retraces==20 &&
                    linux_host::counters().disc_reads==1 && linux_host::state_snapshot(&state) &&
                    state.rng==0xBADC0DEu && state.match_frame==20;
  linux_host::shutdown();
  std::fprintf(stderr,"native ELF ABI integration: %s\n",passed ? "PASS" : "FAIL");
  return passed ? 0 : 1;
}
