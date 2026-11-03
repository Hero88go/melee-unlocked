// Window-free validation, evidence formatting, box filtering and asynchronous output.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_agent_present.h"
#include <cstdio>
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_test.h"
#include <cstdlib>
#include <fstream>
#include <iterator>

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

static std::string read(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  CHECK(file.good());
  return std::string(std::istreambuf_iterator<char>(file), {});
}
static void validation(const fs::path& directory) {
  using agent_test::Engine;
  auto parse = [&](const nlohmann::json& object) { return agent_test::map_keys(object, Engine::Source, directory); };
  const auto good = parse({{"present_log", "presents.txt"}, {"present_frames", "strip:600"}});
  CHECK(good.present.log == "presents.txt");
  CHECK(good.present.directory == "strip" && good.present.every == 600);
  CHECK(good.environment.empty() && good.arguments.empty());
  CHECK(good.log == "agent-test.json applied default keys: present_frames, present_log");
  for (const auto* name : {"log", "log.txt", "log-file_1.txt", "frame strip"}) {
    CHECK(parse({{"present_log", name}}).present.log == name);
    CHECK(parse({{"present_frames", std::string(name) + ":1"}}).present.every == 1);
  }
  for (const auto& name : {std::string(), std::string("."), std::string(".."), std::string("a..b"),
       std::string("../x"), std::string("/absolute"), std::string("C:\\absolute"), std::string("a/b"),
       std::string("a\\b"), std::string("a:b"), std::string("x\ny"), std::string("x\0y", 3)}) {
    for (const auto* key : {"present_log", "present_frames"}) {
      const auto s = parse({{key, name + (std::string(key) == "present_frames" ? ":30" : "")}, {"mute", true}});
      CHECK(!s.present.enabled());
      CHECK(s.log == std::string("agent-test.json applied default keys: mute\nagent-test: ") +
          key + " invalid name or interval; ignored"); // Exactly one refusal; other keys survive.
    }
  }
  for (const auto* spec : {"strip:0", "strip:601", "strip:-1", "strip:+1", "strip:1.5", "strip:",
                           "strip", "strip:1:2", "strip:99999999999999999999", "strip: 1"}) {
    CHECK(!parse({{"present_frames", spec}}).present.enabled());
  }
  for (const auto& value : {nlohmann::json(nullptr), nlohmann::json(1), nlohmann::json(true), nlohmann::json::array()}) {
    CHECK(!parse({{"present_log", value}, {"present_frames", value}}).present.enabled());
  }
  CHECK(!agent_test::map_keys({{"present_log", "p.txt"}}, Engine::Source).present.enabled());
  CHECK(!agent_test::map_keys({{"present_log", "p.txt"}}, Engine::Static, directory).present.enabled());
  // Checked output paths refuse symlinks, even when the leaf would stay within the root.
  { std::ofstream file(directory / "target"); file << "untouched"; }
  fs::create_symlink(directory / "target", directory / "linked-log");
  fs::create_directory_symlink(directory / "strip", directory / "linked-frames");
  CHECK(!parse({{"present_log", "linked-log"}}).present.enabled());
  CHECK(!parse({{"present_frames", "linked-frames:1"}}).present.enabled());
  CHECK(read(directory / "target") == "untouched");
  { std::ofstream file(directory / "agent-test.json");
    file << R"({"present_log":"loaded.txt","present_frames":"loaded:30"})"; }
  CHECK(!agent_test::load(false, directory.u8string().c_str()).present.enabled());
  CHECK(!agent_present::recorder().enabled());
  const auto loaded = agent_test::load(true, directory.u8string().c_str());
  CHECK(loaded.present.enabled() && agent_present::recorder().enabled());
  agent_present::recorder().stop();
  CHECK(!fs::exists(directory / "loaded.txt")); // Opening waits for the first present.
  CHECK(!agent_test::load(true, nullptr).present.enabled());
}
static void formatting() {
  CHECK(agent_present::header(123456789) ==
      "present-log v1 wall_ms=123456789 fields=present us sim scene backbuffer window scale\n");
  CHECK(agent_present::line(1, 0, 77, 2, 10, 1920, 1080, 960, 540) ==
      "1 0 77 02:0A 1920x1080 960x540 2.000\n");
  CHECK(agent_present::line(60, 999999, 100, 0x1c, 2, 1000, 500, 800, 400) ==
      "60 999999 100 1C:02 1000x500 800x400 1.250\n");
  CHECK(agent_present::frame_name("strip", 60, 999999) == "strip/60-999999.ppm");
}
static void filtering() {
  const uint8_t pixels[] = {0,10,20, 20,30,40, 100,110,120, 120,130,140,
                            40,50,60, 60,70,80, 140,150,160, 160,170,180};
  const auto image = agent_present::downscale(pixels, 4, 2, 2);
  CHECK(image.width == 2 && image.height == 1);
  CHECK(image.pixels == (std::vector<uint8_t>{30,40,50, 130,140,150}));
  CHECK(image.ppm_header() == "P6\n2 1\n255\n");
  const auto flipped = agent_present::downscale(pixels, 4, 2, 4);
  CHECK(flipped.width == 4 && flipped.height == 2);
  CHECK(flipped.pixels == (std::vector<uint8_t>{40,50,60, 60,70,80, 140,150,160, 160,170,180,
                                              0,10,20, 20,30,40, 100,110,120, 120,130,140}));
  CHECK(agent_present::downscale(pixels, 4, 2, 4, false).pixels == std::vector<uint8_t>(pixels, pixels + 24));
  const uint8_t fractional[] = {0,0,0, 60,60,60, 180,180,180};
  const auto narrow = agent_present::downscale(fractional, 3, 1, 2);
  CHECK(narrow.width == 2 && narrow.height == 1);
  CHECK(narrow.pixels == (std::vector<uint8_t>{20,20,20, 140,140,140}));
  const uint8_t one[] = {1,2,3};
  CHECK(agent_present::downscale(one, 1, 1).pixels == (std::vector<uint8_t>{1,2,3}));
  CHECK(agent_present::downscale(nullptr, 4, 2).pixels.empty());
  std::vector<uint8_t> wide(960 * 4 * 3, 73);
  const auto half = agent_present::downscale(wide.data(), 960, 4);
  CHECK(half.width == 480 && half.height == 2);
  CHECK(half.pixels == std::vector<uint8_t>(480 * 2 * 3, 73));
}
static void limits() {
  agent_present::Budget count;
  CHECK(count.reserve(18000, 100) == nullptr && !count.stopped);
  CHECK(std::string(count.reserve(18001, 0)) == "18000 presents" && count.stopped);
  CHECK(count.reserve(18002, 0) == nullptr); // One notice.
  agent_present::Budget bytes;
  CHECK(bytes.reserve(1, bytes.byte_limit - 1) == nullptr);
  CHECK(std::string(bytes.reserve(2, 2)) == "2 GiB limit");
  CHECK(bytes.reserved == bytes.byte_limit - 1);
  CHECK(bytes.reserve(3, 2) == nullptr);
}
static void output(const fs::path& directory) {
  agent_paths::Root root;
  CHECK(root.load(directory));
  agent_present::Recorder recorder;
  CHECK(recorder.arm({"output.txt", "output-frames", 2}, root));
  const auto epoch = agent_present::Recorder::Clock::time_point(100s);
  const auto wall = std::chrono::system_clock::time_point(123456789ms);
  std::string expected = agent_present::header(123456789);
  for (uint64_t n = 1; n <= 61; ++n) {
    const auto us = (n - 1) * 16667;
    const bool wanted = recorder.wants_frame(n);
    CHECK(wanted == (n % 2 == 0));
    agent_present::Image image;
    if (wanted) { image.width = image.height = 1; image.pixels = {uint8_t(n), 2, 3}; }
    recorder.after_swap(n, 100 + n, 2, 2, 960, 540, 480, 270, std::move(image), epoch + std::chrono::microseconds(us), wall);
    expected += agent_present::line(n, us, 100 + n, 2, 2, 960, 540, 480, 270);
  }
  recorder.stop(); // Drains queued work and flushes the final partial batch.
  CHECK(read(directory / "output.txt") == expected);
  CHECK(std::distance(fs::directory_iterator(directory / "output-frames"), fs::directory_iterator{}) == 30);
  auto ppm = std::string("P6\n1 1\n255\n"); ppm.append("\x02\x02\x03", 3);
  CHECK(read(directory / "output-frames/2-16667.ppm") == ppm);
  CHECK(!fs::exists(directory / "output-frames/1-0.ppm"));
  CHECK(recorder.arm({"", "limit-frames", 600}, root));
  CHECK(recorder.wants_frame(18000));
  CHECK(!recorder.wants_frame(18001));
  CHECK(!recorder.wants_frame(18600));
  recorder.stop();
}
int main(int argc, char** argv) {
  CHECK(argc == 2);
  const auto directory = fs::absolute(fs::u8path(argv[1]));
  fs::remove_all(directory); fs::create_directories(directory);
  validation(directory); formatting(); filtering(); limits(); output(directory);
  fs::remove_all(directory);
  std::printf("yougame_agent_present_test: %u/%u passed\n", checks, checks);
}
#else
int main() { std::puts("presentation evidence disabled"); }
#endif
