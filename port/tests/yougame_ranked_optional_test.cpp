// Exercise the actual shim readers against old/new hosts; nosp links with NO command provider.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mu_host.h"
#include "yougame_ranked_policy.h"
#include "native_slippi_bridge.h"
#include <cstdlib>
#include <iostream>
#include <cstring>

static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::exit(1); } } while (0)
static_assert(MU_HOST_API_VERSION == 35 && MU_GAME_API_VERSION == 10);   // 35: Triples (5-6 players)

// Only the command declaration from mu_native.h is needed by this portable shim.
#define MU_NATIVE_H
typedef struct MuMatchState MuMatchState;
extern "C" {
int mu_slippi_cmd(unsigned, const void*, unsigned, void*, unsigned, unsigned*);
#include "../../sourceport/game/shim/mu_ranked_set.c"
}

#ifndef MU_NO_SLIPPI
static int status = -1, calls;
static unsigned response_size;
static std::vector<uint8_t> response;
extern "C" int mu_slippi_cmd(unsigned cmd, const void* payload, unsigned size,
                               void* out, unsigned capacity, unsigned* got) {
  ++calls;
  CHECK(cmd == 0xFE && size == 1 && payload);
  CHECK(*static_cast<const uint8_t*>(payload) <= 1);
  *got = response_size;
  if (status == 0) std::memcpy(out, response.data(), std::min(capacity, response_size));
  return status;
}
#endif

int main() {
  MuYgRanks ranks{};
  MuRankedPolicy policy{};
  CHECK(!mu_ranked_read_ranks(nullptr) && !mu_ranked_read_policy(nullptr));
  ranks.self.known = policy.available = 1;
  CHECK(!mu_ranked_read_ranks(&ranks) && !ranks.self.known);
  CHECK(!mu_ranked_read_policy(&policy) && !policy.available);
#ifndef MU_NO_SLIPPI
  CHECK(calls == 2); // an API-22 old host returns unknown; no ABI bump required
  status = 0;
  for (bool rank : {false, true}) {
    response_size = rank ? MU_YG_RANK_RESPONSE_SIZE : MU_YG_POLICY_SIZE;
    response.assign(response_size, 0); response[0] = 1;
    for (int invalid : {0, 1, 2}) {
      if (invalid == 0) --response_size;
      if (invalid == 1) { ++response_size; response[0] = 2; }
      if (invalid == 2) response[0] = 1; // valid unknown rank / unavailable policy
      CHECK(rank ? mu_ranked_read_ranks(&ranks) == (invalid == 2) : !mu_ranked_read_policy(&policy));
      CHECK(!ranks.self.known && !policy.available);
    }
  }
  response = yougame::ranked_policy::encode(yougame::ranked_policy::slippi_v1());
  response_size = response.size();
  CHECK(mu_ranked_read_policy(&policy) && policy.available && policy.starters[0] == 2);
  MuYgRanks expected{}; expected.self.known = 1; expected.self.rating = 12345;
  response.resize(MU_YG_RANK_RESPONSE_SIZE); mu_yg_ranks_encode(response.data(), &expected);
  response_size = response.size();
  CHECK(mu_ranked_read_ranks(&ranks) && ranks.self.known && ranks.self.rating == 12345);
  // Native bridge transports the single-byte selector and rejects malformed payloads.
  uint8_t reply[MU_YG_RANK_RESPONSE_SIZE], sub = MU_YG_SUB_RANK;
  uint32_t got = 0;
  const auto handler = [](uint8_t cmd, const uint8_t* p, uint32_t n, std::vector<uint8_t>& out) {
    CHECK(cmd == MU_YG_COMMAND && n == 1 && p[0] == MU_YG_SUB_RANK);
    out = response; return true;
  };
  using namespace slippi::online;
  CHECK(native_command(CMD_YOUGAME_DATA, &sub, 1, reply, sizeof reply, &got, 0, true, handler) == 0);
  CHECK(got == sizeof reply && mu_yg_ranks_decode(&ranks, reply, got));
  CHECK(native_command(CMD_YOUGAME_DATA, nullptr, 0, reply, sizeof reply, &got, 0, true, handler) == NATIVE_COMMAND_BOUNDS);
#endif
  mu_ranked_set_reset();
  CHECK(mu_ranked_set()->cur_game == 1 && mu_ranked_set()->prev_winner == 0xFF);
  std::cout << "ranked optional commands: " << checks << " checks passed\n";
}
