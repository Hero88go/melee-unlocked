// Test-only startup defaults for app-launched native games. Read agent-test.json once, only
// with an active YouGame bridge and nonempty YOUGAME_DATA. Missing files are silent. Optional
// keys: mute:true, online:"unranked"|"ranked"|"direct"|"teams", character:0..25 (with online),
// code:1..12 ASCII [A-Z0-9#], timer:5..600 seconds, quitAfter:1..3, padBack:10..900 seconds, fight:true. Invalid/unknown keys
// are ignored together in one log line. Prepend defaults so the existing CLI parser handles
// modes, character defaults and volume, and later explicit flags win. No file watching.
// Hook builds also accept script (a bounded file in the data dir; Source permits subdirectories),
// rng_seed:uint32 and time_base:uint64 (0 keeps the CLI's wall-clock default).
// Source-only hook key: match:"<stage>:<kind>[/c<level>][/x<costume>][:...]" (1..6 players,
// at most 128 ASCII [a-z0-9_:/] characters, numeric IDs; refused whenever online is present).
// Both engines: frames:600..36000, stopAt:60..36000 (host::die at that frame: exit 3 and a last stderr
// line "crash: agent test: stop at frame N", for the app's "stopped" notice). Static-only:
// captureEvery:30..600 (agent-captures/cap.ppm).
// Source-only hook keys: frame_times, sim_times, replay_dir, record_native, slippi_menus,
// no_gc_adapter, early_rng_seed; netDelayMs:0..250, netJitterMs:0..100 (positive delay, Direct only).
// Output paths stay inside the data directory.
// keys:"overlay@20,escape@30" emits stdio protocol taps after the first presented frame.
// Hook-only, both online engines: dropRoomAfter:330..900 seconds (agent-only rooms, never ranked).
// Source renderer hook: present_log:"<name>", present_frames:"<directory-name>:<1..600>".
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_paths.h"
#include "yougame_agent_keys.h"
#include "yougame_agent_present.h"
#include "yougame_net_test.h"
#endif
#include <algorithm>
#include <array>
#include <cstdint>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace yougame::agent_test {
enum class Engine { Source, Static };

struct Settings {
  std::vector<std::string> arguments;
  std::vector<std::pair<std::string, std::string>> environment;
  std::string log;
  std::string unsupported_online;
  bool static_online_test = false;
  int pad_back_seconds = 0;
  bool fight = false;
#ifdef YOUGAME_AGENT_TEST_HOOK
  std::vector<ScriptedKey> key_script;
  bool agent_script = false;
  net_test::Config net;
  agent_present::Config present;
  int stop_at_frame = 0;
#endif

  std::vector<std::string> prepend(int argc, char* const* argv) const {
    std::vector<std::string> result;
    result.emplace_back(argv[0]);
    result.insert(result.end(), arguments.begin(), arguments.end());
    for (int i = 1; i < argc; ++i) result.emplace_back(argv[i]);
    return result;
  }
};

inline bool integer_in(const nlohmann::json& value, int minimum, int maximum) {
  if (value.is_number_unsigned()) {
    const auto n = value.get<uint64_t>();
    return n >= static_cast<uint64_t>(minimum) && n <= static_cast<uint64_t>(maximum);
  }
  if (!value.is_number_integer()) return false;
  const auto n = value.get<int64_t>();
  return n >= minimum && n <= maximum;
}

#ifdef YOUGAME_AGENT_TEST_HOOK
inline bool script_name_valid(const std::string& name) {
  return !name.empty() && name.size() <= 64 && name.find("..") == std::string::npos &&
      name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") == std::string::npos;
}

inline bool source_script_path_valid(const std::string& name) {
  return agent_paths::relative_valid(name);
}

inline bool source_match_valid(const std::string& text) {
  if (text.empty() || text.size() > 128 ||
      text.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_:/") != std::string::npos) return false;
  // Dry parse source_port::set_match's base-0 numbers, six player slots and options in
  // either order. Calling set_match itself would change the process's match override.
  const char* next = text.c_str();
  auto number = [&]() {
    char* end = nullptr;
    errno = 0;
    const auto value = std::strtol(next, &end, 0);
    if (end == next || errno == ERANGE || value < 0 || value > INT32_MAX) return false;
    next = end;
    return true;
  };
  if (!number()) return false;
  size_t players = 0;
  while (*next == ':') {
    ++next;
    if (++players > 6 || !number()) return false;
    while (*next == '/') {
      if (next[1] != 'c' && next[1] != 'x') return false;
      next += 2;
      if (!number()) return false;
    }
  }
  return *next == '\0' && players != 0;
}

inline bool early_rng_seed_valid(const std::string& text) {
  // Match both engines' base-0 strtoul grammar, but reject truncation, signs and overflow.
  const char* next = text.c_str();
  for (int field = 0; field < 2; ++field) {
    if (*next < '0' || *next > '9') return false;
    char* end = nullptr;
    errno = 0;
    const auto number = std::strtoull(next, &end, 0);
    if (errno == ERANGE || number > UINT32_MAX || (field == 1 && number == 0)) return false;
    if (field == 0) { if (*end != '@') return false; next = end + 1; }
    else if (end != text.c_str() + text.size()) return false;
  }
  return true;
}

inline bool source_output_path(const std::string& name, const std::filesystem::path& data_dir,
                               bool directory, std::filesystem::path& path,
                               const agent_paths::Root& capability) {
  (void)data_dir;
  const auto& root = capability.path;
  if (!source_script_path_valid(name) || root.empty()) return false;
  auto normalized = name;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  const auto relative = std::filesystem::u8path(normalized);
  if (relative.filename().empty() || relative.filename() == ".") return false;
  std::error_code ec;
  const auto base = std::filesystem::symlink_status(root, ec);
  if (ec || !std::filesystem::is_directory(base)) return false;
  path = root;
  for (const auto& component : relative) {
    path /= component;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) return false;
    ec.clear();
    if (std::filesystem::is_symlink(status)) return false;
    if (std::filesystem::exists(status) &&
        !(path == root / relative && !directory ? std::filesystem::is_regular_file(status) :
                                                    std::filesystem::is_directory(status))) return false;
  }
  if (directory) return agent_paths::directory(root, normalized, true, capability);
  else if (!std::filesystem::is_directory(path.parent_path(), ec)) return false;
  return !ec;
}

inline bool unsigned_in(const nlohmann::json& value, uint64_t maximum) {
  if (value.is_number_unsigned()) return value.get<uint64_t>() <= maximum;
  return value.is_number_integer() && value.get<int64_t>() >= 0 &&
      static_cast<uint64_t>(value.get<int64_t>()) <= maximum;
}
#endif

inline Settings map_keys(const nlohmann::json& object, Engine engine = Engine::Source,
                         const std::filesystem::path& data_dir = {}, bool receive_injector_set = false
#ifdef YOUGAME_AGENT_TEST_HOOK
                         , const agent_paths::Root* capability = nullptr
#endif
                         ) {
  Settings out;
  std::string keys, online;
#ifdef YOUGAME_AGENT_TEST_HOOK
  // Direct mapping has no load/session prerequisite. Keep its root local; load()
  // instead supplies the capability retained before agent-test.json was read.
  agent_paths::Root mapping_root;
  if (!capability) {
    if (!data_dir.empty()) mapping_root.load(data_dir);
    capability = &mapping_root;
  }
  std::string diagnostics;
  auto diagnostic = [&](const std::string& text) {
    diagnostics += "\nagent test: " + text + "; ignored";
  };
#else
  (void)data_dir;
  (void)receive_injector_set;
#endif
  int character = -1;
  bool ignored = false;
  auto applied = [&](const char* key) {
    if (!keys.empty()) keys += ", ";
    keys += key;
  };
  for (auto it = object.begin(); it != object.end(); ++it) {
    const auto& key = it.key();
    const auto& value = it.value();
#ifdef YOUGAME_AGENT_TEST_HOOK
    if (key == "present_log" || key == "present_frames") {
      auto invalid = [&](const char* reason) { diagnostics += "\nagent-test: " + key + " " + reason + "; ignored"; };
      std::string name;
      unsigned every = 0;
      const bool frames = key == "present_frames";
      if (engine != Engine::Source) invalid("not supported on the Static Recomp");
      else if (!value.is_string() ||
          !(frames ? agent_present::frame_spec(value.get<std::string>(), name, every) :
                     agent_present::plain_name(name = value.get<std::string>()))) invalid("invalid name or interval");
      else {
        std::filesystem::path path;
        if (!source_output_path(name, data_dir, frames, path, *capability)) invalid("invalid output path");
        else {
          if (frames) { out.present.directory = name; out.present.every = every; }
          else out.present.log = name;
          applied(key.c_str());
        }
      }
      continue;
    }
    if (key == "netDelayMs" || key == "netJitterMs") {
      const bool delay = key == "netDelayMs";
      auto invalid = [&](const char* reason) { diagnostics += "\nagent-test: " + key + " " + reason + "; ignored"; };
      if (engine != Engine::Source) invalid("not supported on the Static Recomp");
      else if (!integer_in(value, 0, delay ? 250 : 100)) invalid("invalid value");
      else if (receive_injector_set) {
        // One refusal for the pair, even if both keys are present.
        if (diagnostics.find("receive-side injector") == std::string::npos)
          diagnostics += "\nagent-test: net delay keys conflict with receive-side injector; ignored";
      } else if (!delay && out.net.delay_ms == 0) {
        invalid("requires netDelayMs > 0");
      } else {
        if (delay) out.net.delay_ms = value.get<int>();
        else out.net.jitter_ms = value.get<int>();
        applied(key.c_str());
      }
      continue;
    }
    if (key == "frame_times" || key == "sim_times" || key == "replay_dir" ||
        key == "record_native" || key == "slippi_menus" || key == "no_gc_adapter" || key == "early_rng_seed") {
      auto invalid = [&](const char* reason) { diagnostics += "\nagent-test: " + key + " " + reason + "; ignored"; };
      if (engine != Engine::Source) invalid("not supported on the Static Recomp");
      else if (key == "frame_times" || key == "sim_times" || key == "replay_dir") {
        std::filesystem::path path;
        if (!value.is_string() || !source_output_path(value.get<std::string>(), data_dir, key == "replay_dir", path, *capability))
          invalid("invalid output path");
        else {
          if (key == "sim_times") out.environment.emplace_back("MELEE_SIM_TIMES", path.u8string());
          else out.arguments.insert(out.arguments.end(), {key == "frame_times" ? "--frame-times" : "--replay-dir", path.u8string()});
          applied(key.c_str());
        }
      } else if (key == "early_rng_seed") {
        if (!value.is_string() || !early_rng_seed_valid(value.get<std::string>())) invalid("invalid value");
        else { out.environment.emplace_back("MELEE_TEST_EARLY_RNG_SEED", value.get<std::string>()); applied(key.c_str()); }
      } else if (!value.is_boolean()) invalid("invalid value");
      else if (key == "slippi_menus") {
        out.arguments.insert(out.arguments.end(), {"--slippi-menus", value.get<bool>() ? "on" : "off"});
        applied(key.c_str());
      } else if (value.get<bool>()) {
        if (key == "record_native") out.arguments.emplace_back("--record-native");
        else out.environment.emplace_back("MELEE_NO_GC_ADAPTER", "1");
        applied(key.c_str());
      }
      continue;
    }
    if (key == "keys") {
      std::string why;
      if (!value.is_string()) why = "expected a string";
      else out.key_script = parse_key_script(value.get<std::string>(), why);
      if (!why.empty()) diagnostics += "\nagent-test: keys ignored: " + why;
      else applied("keys");
      continue;
    }
    if (key == "dropRoomAfter") {
      if (!integer_in(value, 330, 900)) ignored = true;
      else {
        out.environment.emplace_back("MELEE_ONLINE_TEST_DROP_ROOM_AFTER", std::to_string(value.get<int>()));
        applied("dropRoomAfter");
      }
      continue;
    }
    if (key == "rng_seed" || key == "time_base") {
      const bool seed = key == "rng_seed";
      if (!unsigned_in(value, seed ? UINT32_MAX : UINT64_MAX)) {
        diagnostics += "\nagent-test: " + key + " invalid value; ignored";
      } else {
        out.arguments.insert(out.arguments.end(), {seed ? "--rng-seed" : "--time-base",
                                                   std::to_string(value.get<uint64_t>())});
        applied(seed ? "rng_seed" : "time_base");
      }
      continue;
    }
    if (key == "match") {
      auto invalid = [&](const char* reason) { diagnostics += std::string("\nagent-test: match ") + reason + "; ignored"; };
      if (engine != Engine::Source) invalid("not supported on the Static Recomp");
      else if (object.count("online")) invalid("conflicts with online (offline only)");
      else if (!value.is_string() || !source_match_valid(value.get<std::string>())) invalid("invalid value");
      else {
        out.arguments.insert(out.arguments.end(), {"--match", value.get<std::string>()});
        applied("match");
      }
      continue;
    }
    if (key == "script") {
      auto script_diagnostic = [&](const char* text) {
        diagnostics += std::string("\nagent-test: script ") + text + "; ignored";
      };
      if (!value.is_string() || !(engine == Engine::Static ? script_name_valid(value.get<std::string>()) :
                                                           source_script_path_valid(value.get<std::string>()))) {
        script_diagnostic("invalid name");
      } else if (data_dir.empty()) {
        script_diagnostic("missing data directory");
      } else {
        auto name = value.get<std::string>();
        std::replace(name.begin(), name.end(), '\\', '/');
        const auto& root = capability->path;
        const auto path = root / std::filesystem::u8path(name);
        std::string snapshot;
        if (!agent_paths::read(root, name, agent_paths::script_limit, snapshot, *capability)) {
          script_diagnostic(errno == EFBIG ? "exceeds 64 KiB" : "missing or not a regular file");
        } else {
          out.arguments.insert(out.arguments.end(), {"--script", path.u8string()});
          out.agent_script = true;
          applied("script");
        }
      }
      continue;
    }
    if (key == "stopAt") {
      if (!integer_in(value, 60, 36000)) diagnostic("stopAt invalid value");
      else { out.stop_at_frame = value.get<int>(); applied("stopAt"); }
      continue;
    }
    if (key == "captureEvery" || key == "frames") {
      const bool capture = key == "captureEvery";
#if defined(MELEE_LINUX) && !defined(__APPLE__)
      if (capture && engine == Engine::Source) {
        diagnostics += "\nagent-test: captureEvery not supported on Linux; ignored";
        continue;
      }
#endif
      if ((capture && engine != Engine::Static) || !integer_in(value, capture ? 30 : 600, capture ? 600 : 36000)) {
        if (!capture && engine == Engine::Source) diagnostics += "\nagent-test: frames invalid value; ignored";
        else diagnostic(key + (engine != Engine::Static ? " not supported on the Source Port" : " invalid value"));
      } else if (capture) {
        std::filesystem::path directory;
        if (!source_output_path("agent-captures", data_dir, true, directory, *capability)) diagnostic("captureEvery cannot create directory");
        else {
          out.arguments.insert(out.arguments.end(), {"--capture", (directory / "cap.ppm").u8string(),
                                                     "--capture-every", std::to_string(value.get<int>())});
          applied("captureEvery");
        }
      } else {
        out.arguments.insert(out.arguments.end(), {"--frames", std::to_string(value.get<int>())});
        applied("frames");
      }
      continue;
    }
#endif
    if (key == "mute" && value.is_boolean() && value.get<bool>()) {
      out.arguments.insert(out.arguments.end(), {"--volume", "0"});
      applied("mute");
    } else if (key == "online" && value.is_string() &&
               (engine == Engine::Static || value == "unranked" || value == "ranked" || value == "direct" || value == "teams")) {
      online = value.get<std::string>();
    } else if (key == "character" && integer_in(value, 0, 25)) {
      character = value.get<int>();
    } else if (key == "code" && value.is_string() && engine == Engine::Source) {
      const auto code = value.get<std::string>();
      if (code.empty() || code.size() > 12 || code.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#") != std::string::npos) {
        ignored = true;
        continue;
      }
      out.environment.emplace_back("MELEE_ONLINE_TEST_CODE", code);
      applied("code");
    } else if (key == "fight" && value.is_boolean()) {
      out.fight = value.get<bool>();
      if (out.fight) applied("fight");
    } else if (key == "padBack" && integer_in(value, 10, 900)) {
      out.pad_back_seconds = value.get<int>();
      applied("padBack");
    } else if ((key == "timer" && integer_in(value, 5, 600)) ||
               (key == "quitAfter" && integer_in(value, 1, 3))) {
      out.environment.emplace_back(key == "timer" ? "MELEE_ONLINE_TEST_TIMER" : "MELEE_ONLINE_TEST_QUIT_AFTER",
                                   std::to_string(value.get<int>()));
      applied(key == "timer" ? "timer" : "quitAfter");
    } else {
      ignored = true;
    }
  }
  if (engine == Engine::Static) {
    if (online == "unranked" || online == "teams") {
      out.environment.emplace_back("MELEE_PRACTICE_AUTOSTART", online);
      out.static_online_test = true;
      applied("online");
    } else if (object.count("online") && object["online"].is_string()) {
      out.unsupported_online = "agent test: online " + online + " not supported on the Static Recomp";
    }
    if (character >= 0) ignored = true;
  } else if (!online.empty()) {
    applied("online");
    if (character >= 0) {
      online += ":" + std::to_string(character);
      applied("character");
    }
    out.arguments.insert(out.arguments.end(), {"--online-test", online});
  } else if (character >= 0) {
    ignored = true;  // A character alone does not opt into a match or modify an explicit CLI spec.
  }
  if (!keys.empty()) out.log = "agent-test.json applied default keys: " + keys;
  if (ignored) {
    if (out.log.empty()) out.log = "agent-test.json";
    out.log += "; invalid/unknown or inapplicable keys ignored";
  }
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (!diagnostics.empty()) out.log += diagnostics.substr(out.log.empty() ? 1 : 0);
#endif
  return out;
}
inline Settings load(bool active, const char* data_dir, Engine engine = Engine::Source) {
  Settings out;
#ifdef YOUGAME_AGENT_TEST_HOOK
  agent_present::recorder().stop();
  agent_paths::policy = {};
  agent_paths::session.reset();
  // All early returns and exceptions release the root unless mapping activated a session.
  struct LoadScope {
    bool active = false;
    ~LoadScope() { if (!active) { agent_paths::policy = {}; agent_paths::session.reset(); } }
  } scope;
#endif
  if (!active || !data_dir || !*data_dir) return out;
  // Bound the actual read BEFORE parsing; the same handle is checked and read.
  std::array<char, 4097> bytes{};
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (!agent_paths::session.load(std::filesystem::u8path(data_dir))) return out;
  const auto& root = agent_paths::session.path;
  FILE* file = agent_paths::open(root, "agent-test.json", agent_paths::Mode::Read);
  if (!file) return out;
  const auto size = std::fread(bytes.data(), 1, bytes.size(), file);
  const bool failed = std::ferror(file) != 0;
  std::fclose(file);
#else
  std::ifstream file(std::filesystem::u8path(data_dir) / "agent-test.json", std::ios::binary);
  if (!file) return out;
  file.read(bytes.data(), bytes.size());
  const auto size = file.gcount();
  const bool failed = file.bad();
#endif
  if (size > 4096) {
    out.log = "agent-test.json ignored: exceeds 4 KiB";
    return out;
  }
  if (failed) {
    out.log = "agent-test.json ignored: read failed";
    return out;
  }
  // The JSON lexer treats a raw NUL as EOF; reject it rather than accepting a truncated file.
  if (std::find(bytes.data(), bytes.data() + size, '\0') != bytes.data() + size) {
    out.log = "agent-test.json ignored: invalid JSON object";
    return out;
  }
  const auto object = nlohmann::json::parse(bytes.data(), bytes.data() + size, nullptr, false);
  if (object.is_discarded() || !object.is_object()) {
    out.log = "agent-test.json ignored: invalid JSON object";
    return out;
  }
  out = map_keys(object, engine, std::filesystem::u8path(data_dir),
      std::getenv("MELEE_NET_LAG_MS") != nullptr || std::getenv("MELEE_NET_JITTER_MS") != nullptr
#ifdef YOUGAME_AGENT_TEST_HOOK
      , &agent_paths::session
#endif
      );
#ifdef YOUGAME_AGENT_TEST_HOOK
  const bool active_session = !out.arguments.empty() || !out.environment.empty() || !out.key_script.empty() ||
      out.static_online_test || out.pad_back_seconds > 0 || out.fight || out.net.delay_ms > 0 || out.present.enabled();
  if (!active_session) return out;
  // Consumers must reopen through the capability, not through a checked pathname.
  agent_paths::policy = {};
  agent_paths::policy.root = root;
  for (size_t i = 0; i < out.arguments.size(); ++i) {
    const auto& arg = out.arguments[i];
    if (i + 1 == out.arguments.size()) continue;
    if (arg == "--replay-dir") agent_paths::policy.replay_directory = out.arguments[i + 1];
    if (arg == "--script") agent_paths::policy.script = out.arguments[i + 1];
    if (arg == "--frame-times") agent_paths::policy.frame_times = out.arguments[i + 1];
    if (arg == "--capture") agent_paths::policy.capture_directory =
        std::filesystem::u8path(out.arguments[i + 1]).parent_path().u8string();
  }
  for (const auto& item : out.environment)
    if (item.first == "MELEE_SIM_TIMES") agent_paths::policy.sim_times = item.second;
  if (!agent_present::recorder().arm(out.present, agent_paths::session)) {
    if (!out.log.empty()) out.log += '\n';
    out.log += "agent-test: present output capability refused; ignored";
  }
  scope.active = true;
#endif
  return out;
}
}  // namespace yougame::agent_test
