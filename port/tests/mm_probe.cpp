// Manual probe of the Slippi matchmaking handshake: creates a direct-mode ticket for a code that
// nobody owns, so the server validates our credentials and version without pairing anyone, then
// leaves the queue. Usage: port_mm_probe <user_dir> [seconds]
// SPDX-License-Identifier: GPL-2.0-or-later
#include "slippi_net.h"
#include "host.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace host { void log(const char* fmt, ...); }

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: port_mm_probe <user_dir> [seconds]\n"); return 2; }
  int seconds = argc > 2 ? std::atoi(argv[2]) : 12;
  slippi::User user(argv[1]);
  if (!user.IsLoggedIn()) { std::fprintf(stderr, "no user.json in %s\n", argv[1]); return 1; }
  if (!slippi::enet_ready()) return 1;
  slippi::Matchmaking mm(&user);
  slippi::Matchmaking::MatchSearchSettings s;
  s.mode = slippi::Matchmaking::DIRECT;
  s.connect_code = "ZZZZ#999";
  mm.FindMatch(s);
  auto t0 = std::chrono::steady_clock::now();
  slippi::Matchmaking::ProcessState last = slippi::Matchmaking::IDLE;
  while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(seconds)) {
    auto st = mm.GetMatchmakeState();
    if (st != last) { std::printf("state %d %s\n", (int)st, mm.GetErrorMessage().c_str()); std::fflush(stdout); last = st; }
    if (st == slippi::Matchmaking::ERROR_ENCOUNTERED || st == slippi::Matchmaking::CONNECTION_SUCCESS) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  std::printf("final state %d (%s)\n", (int)mm.GetMatchmakeState(), mm.GetErrorMessage().c_str());
  return 0;
}
