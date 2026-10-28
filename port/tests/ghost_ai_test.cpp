// Model-file integrity and recurrent memory boundaries for offline pro practice.
#include "../runtime/hle/ghost_ai.cpp"
#include <filesystem>
#include <fstream>
#include <limits>

namespace host {
void log(const char*, ...) {}
double now_seconds() { return 0; }
}

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

int main(int argc, char** argv) {
  using namespace ghost_ai;
  if (argc == 3) {
    // Python generates independent PyTorch recurrent outputs from real replay observations.
    // Compare those with the actual shipped C++ inference implementation, not another port of it.
    CHECK(load_file(argv[1], g_model));
    if (failures) return 1;
    g_loaded = 1; g_enabled = true;
    std::ifstream f(argv[2], std::ios::binary);
    uint32_t count = 0;
    f.read(reinterpret_cast<char*>(&count), 4);
    CHECK(count > 0 && count <= 1000);
    float max_error = 0;
    for (uint32_t i = 0; i < count && f; ++i) {
      MuGhostObs obs{};
      float previous[2][10];
      std::vector<float> expected(g_model.h_rnn);
      f.read(reinterpret_cast<char*>(&obs), sizeof obs);
      f.read(reinterpret_cast<char*>(previous), sizeof previous);
      f.read(reinterpret_cast<char*>(expected.data()), expected.size() * sizeof(float));
      CHECK(bool(f));
      if (!f) break;
      CHECK(obs.slot < 4);
      if (obs.slot >= 4) break;
      // The first replay observation has real history while a new live match starts neutral.
      // Prime only this history; recurrent memory still starts at zero and advances normally.
      auto& slot = g_slots[obs.slot];
      if (!slot.live) {
        slot.live = true; slot.pro = obs.pro; slot.last_frame = obs.frame - 1;
        slot.h.assign(g_model.h_rnn, 0.f);
      }
      std::memcpy(slot.prev, previous, sizeof previous);
      slot.prev_valid = false;
      MuGhostAct output{};
      CHECK(act(obs, output));
      for (size_t j = 0; j < expected.size(); ++j)
        max_error = std::max(max_error, std::fabs(slot.h[j] - expected[j]));
    }
    CHECK(max_error < 0.001f);
    std::printf("PyTorch/C++ parity: %u frames, maximum hidden-state error %.8f\n", count, max_error);
    return failures ? 1 : 0;
  }
  const auto path = std::filesystem::temp_directory_path() /
      ("mu-ghost-test-" + std::to_string(GetCurrentProcessId()) + ".bin");
  // Tiny complete network, with a distinct row for each of four pro identities.
  std::vector<uint32_t> dims = {55, 1, 1, 1, 1, 1, 1, 4, 1, 2, 2, 2};
  constexpr size_t numbers = 1 + 1 + 1 + 4 + 2 * 141 + 2 + 3 * 2 * 2 +
      3 * 2 * 2 + 6 * 2 + 2 * 4 + 2 + 91 * 3 + 162;
  std::vector<float> weights(numbers, 0.f);
  auto write = [&] {
    std::ofstream f(path, std::ios::binary);
    const uint32_t version = MU_GHOST_VERSION;
    f.write("MUGA", 4);
    f.write(reinterpret_cast<const char*>(&version), 4);
    f.write(reinterpret_cast<const char*>(dims.data()), 48);
    f.write(reinterpret_cast<const char*>(weights.data()), weights.size() * sizeof(float));
  };
  write();
  Model model;
  CHECK(load_file(path.string(), model));
  dims[7] = 5; write(); CHECK(!load_file(path.string(), model)); // shifted layers
  weights.insert(weights.begin() + 7, 0.f);
  write(); CHECK(load_file(path.string(), model)); // a complete fifth embedding is supported
  weights.erase(weights.begin() + 7);
  dims[7] = 0; write(); CHECK(!load_file(path.string(), model)); // empty embeddings
  dims[7] = 0xFFFFFFFFu; write(); CHECK(!load_file(path.string(), model));
  dims[7] = 4;
  weights[0] = std::numeric_limits<float>::quiet_NaN();
  write(); CHECK(!load_file(path.string(), model));
  weights[0] = 0;
  weights.back() = 2.f; write(); CHECK(!load_file(path.string(), model)); // invalid controller mean
  weights.back() = 0;
  weights.pop_back(); write(); CHECK(!load_file(path.string(), model)); // truncated
  weights.push_back(0); weights.push_back(0); write(); CHECK(!load_file(path.string(), model)); // trailing data
  weights.pop_back(); write(); CHECK(load_file(path.string(), model));

  g_model = model; g_loaded = 1; g_enabled = true;
  MuGhostObs obs{};
  obs.version = MU_GHOST_VERSION; obs.frame = 100;
  obs.self.motion = obs.opp.motion = 14;
  MuGhostAct a{}, b{};
  CHECK(act(obs, a));
  const auto h = g_slots[0].h;
  CHECK(act(obs, b));
  CHECK(std::memcmp(&a, &b, sizeof a) == 0 && g_slots[0].h == h);
  obs.frame += 2; obs.applied.lx = 1;
  CHECK(act(obs, a));
  CHECK(g_slots[0].prev[1][0] == 0); // a gap must not reintroduce pre-gap input
  g_slots[0].prev[0][0] = 1;
  obs.pro = 1;
  CHECK(act(obs, a)); // same-frame pro switch must clear memory and bypass cached answer
  CHECK(g_slots[0].pro == 1 && g_slots[0].prev[1][0] == 0);
  obs.frame++; obs.reset = 1;
  CHECK(act(obs, a)); CHECK(g_slots[0].prev[1][0] == 0);
  obs.reset = 0; obs.pro = 4; CHECK(!act(obs, a));
  obs.pro = 0; obs.slot = 4; CHECK(!act(obs, a));
  obs.slot = 0; obs.self.num[0] = std::numeric_limits<float>::infinity(); CHECK(!act(obs, a));
  obs.self.num[0] = 0;
  g_has_p1 = true; g_model_p1 = model; g_model_p1.n_pros = 1;
  obs.pro = 1; CHECK(!act(obs, a)); // validate against the selected model, not P2's
  std::filesystem::remove(path);
  std::printf("ghost AI: %d failures\n", failures);
  return failures ? 1 : 0;
}
