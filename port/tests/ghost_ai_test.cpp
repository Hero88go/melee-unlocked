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
  // This test injects its own model; installed specialist files must not change its evidence.
  std::fill(std::begin(g_specialist_status), std::end(g_specialist_status), -1);
  if (argc == 2) {
    Model model;
    return load_file(argv[1], model) ? 0 : 1;
  }
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
  uint32_t file_version = 1;
  float first_scale = 1.f;
  auto write = [&] {
    std::ofstream f(path, std::ios::binary);
    f.write("MUGA", 4);
    f.write(reinterpret_cast<const char*>(&file_version), 4);
    f.write(reinterpret_cast<const char*>(dims.data()), 48);
    if (file_version == 1) {
      f.write(reinterpret_cast<const char*>(weights.data()), weights.size() * sizeof(float));
    } else {
      const uint32_t layout[][3] = {
          {1,1,1}, {1,1,1}, {1,1,1}, {dims[7],1,1}, {2,141,1}, {1,2,0},
          {6,2,1}, {6,2,1}, {1,6,0}, {1,6,0}, {2,4,1}, {1,2,0},
          {81,2,1}, {1,81,0}, {5,2,1}, {1,5,0}, {5,2,1}, {1,5,0}, {1,162,0}};
      size_t offset = 0;
      for (const auto& a : layout) {
        for (uint32_t row = 0; row < a[0]; ++row) {
          if (a[2]) {
            const float scale = offset == 0 ? first_scale : 1.f;
            f.write(reinterpret_cast<const char*>(&scale), 4);
            for (uint32_t col = 0; col < a[1]; ++col) {
              const int8_t value = offset == 0 ? 1 : 0;
              f.write(reinterpret_cast<const char*>(&value), 1);
              ++offset;
            }
          } else {
            f.write(reinterpret_cast<const char*>(weights.data() + offset), a[1] * sizeof(float));
            offset += a[1];
          }
        }
      }
    }
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

  file_version = 2; first_scale = 0.25f;
  write(); CHECK(load_file(path.string(), model)); CHECK(model.state[0] == 0.25f);
  first_scale = 0.f; write(); CHECK(!load_file(path.string(), model));
  first_scale = -1.f; write(); CHECK(!load_file(path.string(), model));
  first_scale = std::numeric_limits<float>::infinity(); write(); CHECK(!load_file(path.string(), model));
  first_scale = std::numeric_limits<float>::quiet_NaN(); write(); CHECK(!load_file(path.string(), model));
  first_scale = 1.f; write();
  std::filesystem::resize_file(path, std::filesystem::file_size(path) - 1);
  CHECK(!load_file(path.string(), model));
  write(); { std::ofstream f(path, std::ios::binary | std::ios::app); f.put(0); }
  CHECK(!load_file(path.string(), model));
  // Compact files retain dynamic player-count support.
  dims[7] = 5; weights.insert(weights.begin() + 7, 0.f);
  write(); CHECK(load_file(path.string(), model));
  dims[7] = 1; weights.erase(weights.begin() + 4, weights.begin() + 8);
  write(); CHECK(load_file(path.string(), model));
  dims[7] = 4; weights.insert(weights.begin() + 4, 3, 0.f);
  write(); CHECK(load_file(path.string(), model));

  g_model = model; g_loaded = 1; g_enabled = true;
  CHECK(available(0) && !specialist_available(0)); // shared fallback is not a named-player release ghost
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
  g_specialists[4] = model; g_specialists[4].n_pros = 1; g_specialists[4].pro.resize(1);
  g_specialist_status[4] = 1;
  CHECK(available(4)); CHECK(specialist_available(4)); CHECK(!available(5));
  uint32_t row = 999;
  CHECK(selected_model(4, 1, row) == &g_specialists[4] && row == 0);
  obs.slot = 1; obs.pro = 4;
  CHECK(act(obs, a)); CHECK(g_slots[1].pro == 4);
  g_has_p1 = true; g_model_p1 = model; g_model_p1.n_pros = 1;
  obs.slot = 0; obs.pro = 1; CHECK(act(obs, a)); // explicit one-policy comparison files use row zero
  g_model_p1.n_pros = 2; obs.pro = 3;
  CHECK(!act(obs, a)); // validate against the selected model, not P2's
  std::filesystem::remove(path);
  std::printf("ghost AI: %d failures\n", failures);
  return failures ? 1 : 0;
}
