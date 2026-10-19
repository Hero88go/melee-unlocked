// Exercises Linux host services without loading proprietary disc content.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_services.h"
#include <cstdio>
#include <string>

int main(int argc,char** argv) {
  if (argc!=2) { std::fprintf(stderr,"usage: linux_host_services_test SCRATCH_DIRECTORY\n"); return 2; }
  std::string error;
  if (!linux_host::service_self_test(argv[1],&error)) {
    std::fprintf(stderr,"native host services: FAIL: %s\n",error.c_str());
    return 1;
  }
  return 0;
}
