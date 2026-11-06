// SPDX-License-Identifier: GPL-2.0-or-later
// Runs Phillip's published ONNX policies (slippi-ai, Vlad Firoiu, MIT; models from Hugging Face
// vladfi/phillip-models, MIT) for the "Fight a pro" fighter. The packing of inputs and outputs, the
// action feedback and the controller decoding follow slippi-ai's onnx_policies.py (format version 1).
//
// What the game sends (mu_ghost_ai.h) is what the policy was trained on: the libmelee game state of
// both players after the last frame (positions, percent, action, character, jumps, shield, facing,
// ground, invulnerability) and the stage. A step's answer is used `delay` frames later (18 for these
// policies), exactly as Phillip plays on Slippi Dolphin, so the step runs on a worker thread per
// fighter and the game thread waits only when an answer is not ready by the frame it is due.
#include "phillip_ai.h"

#include "host.h"
#include "updater.h"
#include "phillip_randall.h"

#include <nlohmann/json.hpp>
#include <onnxruntime_c_api.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <set>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace phillip_ai {
namespace {

using json = nlohmann::json;
bool has(const json& j, const char* key) { return j.is_object() && j.find(key) != j.end(); }
constexpr unsigned kPros = 5;   // ghost_ai's order: mang0, zain, slot 2 (none), cody, amsa
constexpr unsigned kSlots = 4;

// ---- onnxruntime, loaded from beside the game only when a pro is played -------------------------
const OrtApi* g_ort = nullptr;
OrtEnv* g_env = nullptr;
int g_runtime = 0;   // 0 not tried, 1 ready, -1 missing

std::string exe_dir() {
#ifdef _WIN32
  char exe[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string dir(exe);
#else
  char exe[PATH_MAX] = {};
  const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
  std::string dir(n > 0 ? std::string(exe, (size_t)n) : std::string("./x"));
#endif
  dir.resize(dir.find_last_of("\\/") + 1);
  return dir;
}

std::string pro_dir() {
  if (const char* v = std::getenv("MELEE_PRO_AI_DIR")) {
    std::string d = v;
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d;
  }
  return exe_dir() + "ProAI/";
}

// A step of a 21 MB policy takes 3 to 8 ms with AVX. Without it onnxruntime falls far behind the
// 60 frames a second the game needs, so those processors (the compatibility build's) go without.
bool cpu_has_avx() {
#ifdef _WIN32
  return IsProcessorFeaturePresent(PF_AVX_INSTRUCTIONS_AVAILABLE) != FALSE;
#else
  return __builtin_cpu_supports("avx");
#endif
}

bool runtime_ready() {
  if (g_runtime) return g_runtime == 1;
  g_runtime = -1;
  if (!cpu_has_avx()) { host::log("pro ai: this processor has no AVX; Fight a pro is off"); return false; }
#ifdef _WIN32
  // The full path: Windows has its own, older onnxruntime.dll in System32 for Windows ML.
  const std::string path = exe_dir() + "onnxruntime.dll";
  HMODULE dll = LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  using GetBase = const OrtApiBase*(ORT_API_CALL*)();
  GetBase get_base = dll ? reinterpret_cast<GetBase>(GetProcAddress(dll, "OrtGetApiBase")) : nullptr;
#else
  // The Linux package keeps its libraries in lib/ beside the game.
  std::string path = exe_dir() + "lib/libonnxruntime.so";
  void* dll = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!dll) dll = dlopen((path = exe_dir() + "libonnxruntime.so").c_str(), RTLD_NOW | RTLD_LOCAL);
  using GetBase = const OrtApiBase* (*)();
  GetBase get_base = dll ? reinterpret_cast<GetBase>(dlsym(dll, "OrtGetApiBase")) : nullptr;
#endif
  if (!get_base) { host::log("pro ai: %s not found", path.c_str()); return false; }
  const OrtApiBase* base = get_base();
  g_ort = base ? base->GetApi(ORT_API_VERSION) : nullptr;
  if (!g_ort) { host::log("pro ai: onnxruntime %s cannot serve API %d", base ? base->GetVersionString() : "?", ORT_API_VERSION); return false; }
  if (OrtStatus* st = g_ort->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "melee-pro-ai", &g_env)) {
    host::log("pro ai: onnxruntime: %s", g_ort->GetErrorMessage(st));
    g_ort->ReleaseStatus(st);
    return false;
  }
  host::log("pro ai: onnxruntime %s ready", base->GetVersionString());
  g_runtime = 1;
  return true;
}

bool ok(OrtStatus* st, const char* what) {
  if (!st) return true;
  host::log("pro ai: %s: %s", what, g_ort->GetErrorMessage(st));
  g_ort->ReleaseStatus(st);
  return false;
}

// ---- the catalog: which policy plays which pro in which matchup (ProAI/catalog.json) -------------
struct Entry {
  std::string file, kind, sha256, source;
  int character = -1, delay = 0;
  int priority = 0;             // higher plays first (catalog; phillip_catalog.py ranks the generations)
  bool heavy = false;           // a large policy (about 12 ms a step): skipped on a PC too slow for it
  std::vector<int> opponents;   // empty: every opponent (imitation policies)
  int name[kPros];              // the pro's name code in this policy, -1 absent
  // Like the pro first (owner, 2026-11-05): a self-play policy trained as other players never beats one
  // trained as this pro, whatever its generation (catalog "pro_trained", from the policy's own
  // agent_config.name), unless the likeness measurements cleared it ("likeness_ok",
  // tools/ghost_ai/phillip_likeness.py). Imitation policies count as trained as every pro they know.
  bool pro_trained[kPros];
  bool likeness_ok = false;
  float temperature = 1.f;      // the sampling temperature it is stepped with (catalog; 1.0 as published)
  uint64_t size = 0;            // bytes, for the download question (0 in an older catalog)
  std::string trained_as;       // the players its self-play ran as, for the log
  Entry() {
    for (int& n : name) n = -1;
    for (bool& t : pro_trained) t = true;
  }
};
std::vector<Entry> g_catalog;
int g_catalog_state = 0;   // 0 not read, 1 read, -1 missing/bad

void read_catalog() {
  if (g_catalog_state) return;
  g_catalog_state = -1;
  std::ifstream f(pro_dir() + "catalog.json", std::ios::binary);
  if (!f) { host::log("pro ai: no %scatalog.json", pro_dir().c_str()); return; }
  std::stringstream ss;
  ss << f.rdbuf();
  const json doc = json::parse(ss.str(), nullptr, false);
  if (!doc.is_object() || doc.value("version", 0) != 1 || !has(doc, "models") || !doc["models"].is_array()) {
    host::log("pro ai: catalog.json unreadable");
    return;
  }
  for (const json& m : doc["models"]) {
    Entry e;
    e.file = m.value("file", "");
    e.kind = m.value("kind", "");
    e.sha256 = m.value("sha256", "");
    e.source = m.value("source", "");
    e.character = m.value("character", -1);
    e.delay = m.value("delay", 0);
    e.priority = m.value("priority", e.kind == "imitation" ? 1 : 2);
    e.heavy = m.value("heavy", false);
    e.likeness_ok = m.value("likeness_ok", false);
    e.temperature = (float)std::clamp(m.value("temperature", 1.0), 0.05, 2.0);
    e.size = m.value("size", (uint64_t)0);
    if (has(m, "pro_trained") && m["pro_trained"].is_object())
      for (auto it = m["pro_trained"].begin(); it != m["pro_trained"].end(); ++it) {
        const int pro = std::atoi(it.key().c_str());
        if (pro >= 0 && pro < (int)kPros && it.value().is_boolean()) e.pro_trained[pro] = it.value().get<bool>();
      }
    if (has(m, "trained_as") && m["trained_as"].is_array())
      for (const json& n : m["trained_as"])
        if (n.is_string()) e.trained_as += (e.trained_as.empty() ? "" : ", ") + n.get<std::string>();
    if (has(m, "opponents") && m["opponents"].is_array())
      for (const json& o : m["opponents"]) if (o.is_number_integer()) e.opponents.push_back(o.get<int>());
    if (has(m, "names") && m["names"].is_object())
      for (auto it = m["names"].begin(); it != m["names"].end(); ++it) {
        const int pro = std::atoi(it.key().c_str());
        if (pro >= 0 && pro < (int)kPros && it.value().is_number_integer()) e.name[pro] = it.value().get<int>();
      }
    // A file name is a plain name inside the folder, never a path.
    if (e.file.empty() || e.file.find_first_of("/\\:") != std::string::npos || e.file.find("..") != std::string::npos ||
        e.character < 0 || e.delay <= 0 || e.delay > 60)
      continue;
    g_catalog.push_back(std::move(e));
  }
  g_catalog_state = g_catalog.empty() ? -1 : 1;
  host::log("pro ai: catalog has %zu policies", g_catalog.size());
}

bool present(const Entry& e) {
  std::error_code ec;
  return std::filesystem::is_regular_file(std::filesystem::u8path(pro_dir() + e.file), ec);
}

// The game ships each pro's imitation policy; a matchup's own policy (21 MB) comes from Phillip's
// published models the first time that matchup is played: the pinned Hugging Face commit in the
// catalog, refused unless its SHA-256 matches, and used from the next match on. One at a time;
// a failure is not retried until the game starts again. MELEE_PRO_AI_NO_DOWNLOAD (tests) turns it off.
// Requests wait in a queue and are fetched one after another, so "download every stronger policy"
// gets them all.
std::mutex g_fetch_mutex;
std::thread g_fetch_thread;
std::atomic<bool> g_fetching{false};
std::set<std::string> g_fetch_tried;
std::set<std::string> g_fetch_pending;
struct Fetch { std::string url, sha, file, dest; };
std::deque<Fetch> g_fetch_queue;

void fetch_worker() {
  for (;;) {
    Fetch f;
    {
      std::lock_guard<std::mutex> lk(g_fetch_mutex);
      if (g_fetch_queue.empty()) { g_fetching = false; return; }
      f = g_fetch_queue.front();
      g_fetch_queue.pop_front();
    }
    host::log("pro ai: downloading %s", f.file.c_str());
    std::string why;
    if (host::updater::fetch_verified(f.url, f.sha, f.dest, &why)) host::log("pro ai: %s installed; it plays from the next match", f.file.c_str());
    else host::log("pro ai: %s not downloaded (%s); the character's own policy plays", f.file.c_str(), why.c_str());
    std::lock_guard<std::mutex> lk(g_fetch_mutex);
    g_fetch_pending.erase(f.file);
  }
}

void fetch_later(const Entry& e, bool retry = false) {
  static const bool off = std::getenv("MELEE_PRO_AI_NO_DOWNLOAD") != nullptr;
  if (off || e.sha256.size() != 64 || e.source.rfind("https://huggingface.co/vladfi/phillip-models/resolve/", 0) != 0) return;
  std::lock_guard<std::mutex> lk(g_fetch_mutex);
  if (g_fetch_pending.count(e.file) || (!retry && g_fetch_tried.count(e.file))) return;
  g_fetch_tried.insert(e.file);
  g_fetch_pending.insert(e.file);
  g_fetch_queue.push_back({e.source, e.sha256, e.file, pro_dir() + e.file});
  if (g_fetching) return;
  if (g_fetch_thread.joinable()) g_fetch_thread.join();
  g_fetching = true;
  g_fetch_thread = std::thread(fetch_worker);
}

std::atomic<int> g_stronger{kStrongerAsk};

// Phillip's newest policies (90 MB) need about 10 ms a step: a PC plays them only once a background
// check shows it steps one fast enough (0 not known, 1 fast enough, -1 too slow). Until then, and on
// a slower PC, the lighter policies play. MELEE_PRO_AI_LARGE=1/0 (tests) skips the check.
std::atomic<int> g_large_ok{0};
std::atomic<bool> g_checking{false};
std::thread g_check_thread;
void check_speed_later(const Entry& e);

// The policy for this pro on this character against that opponent: first one trained as this pro (or
// cleared by the likeness table), then the highest priority (Phillip's newest self-play, then older
// matchup self-play, then imitation). Only installed files are used; a missing better one is fetched
// for next time. MELEE_PRO_AI_FORCE_P<n>=<file> (tests) plays that file for port n.
const Entry* choose(unsigned pro, unsigned slot, int character, int opponent) {
  read_catalog();
  char force_key[32];
  std::snprintf(force_key, sizeof force_key, "MELEE_PRO_AI_FORCE_P%u", slot + 1);
  if (const char* forced = std::getenv(force_key)) {
    for (const Entry& e : g_catalog) if (e.file == forced && present(e)) return &e;
    host::log("pro ai: forced %s is not installed", forced);
    return nullptr;
  }
  const Entry* best = nullptr;
  const Entry* wanted = nullptr;   // the best one this player may download now
  const Entry* stronger = nullptr; // the best stronger one already here, still waiting for the speed check
  int best_rank = -1, wanted_rank = -1, stronger_rank = -1;
  static const int forced_large = [] { const char* v = std::getenv("MELEE_PRO_AI_LARGE"); return v ? (std::atoi(v) ? 1 : -1) : 0; }();
  if (forced_large) g_large_ok = forced_large;
  const int large = g_large_ok.load(), choice = g_stronger.load();
  for (const Entry& e : g_catalog) {
    if (e.character != character || e.name[pro] < 0) continue;
    if (!e.opponents.empty() && std::find(e.opponents.begin(), e.opponents.end(), opponent) == e.opponents.end()) continue;
    if (e.heavy && (large < 0 || choice == kStrongerNever)) continue;
    // Like the pro first: trained as this pro (or cleared) outranks every generation of one that was not.
    const int rank = e.priority + ((e.pro_trained[pro] || e.likeness_ok) ? 100 : 0);
    const bool here = present(e);
    if ((!e.heavy || choice == kStrongerAlways || here) && rank > wanted_rank) { wanted_rank = rank; wanted = &e; }
    if (e.heavy && here && large == 0 && rank > stronger_rank) { stronger_rank = rank; stronger = &e; }
    if (rank > best_rank && here && (!e.heavy || large > 0)) { best_rank = rank; best = &e; }
  }
  if (wanted && wanted_rank > best_rank && !present(*wanted)) fetch_later(*wanted);
  if (stronger && stronger_rank > best_rank) check_speed_later(*stronger);
  return best_rank >= 0 ? best : nullptr;
}

// ---- one loaded policy ---------------------------------------------------------------------------
struct Leaf { int pack = -1; size_t offset = 0, size = 0; };
struct PackSpec { std::string name; ONNXTensorElementDataType type{}; size_t width = 0, elem = 0; bool fed = false; };

// slippi-ai's custom_v1 stick buckets (action_space/custom_v1.py): radius 0 plus log-spaced radii in
// raw [-80, 80] units, each radius with its own number of evenly spaced angles from -pi.
struct Polar {
  std::vector<float> radius, angle;   // by label
  int labels = 0;
  bool build(const json& cfg) {
    const int n_radius = cfg.at("n_radius_buckets").get<int>();
    std::vector<int> n_angle = {1};
    for (const json& a : cfg.at("n_angle_buckets")) n_angle.push_back(a.get<int>());
    if (n_radius <= 0 || (int)n_angle.size() != n_radius + 1) return false;
    std::vector<float> radii = {0.f};
    const double lo = std::log(23.0), hi = std::log(80.0);
    for (int i = 0; i < n_radius; ++i)
      radii.push_back((float)std::exp(n_radius == 1 ? lo : lo + (hi - lo) * i / (n_radius - 1)));
    for (size_t r = 0; r < n_angle.size(); ++r)
      for (int k = 0; k < n_angle[r]; ++k) {
        radius.push_back(radii[r]);
        angle.push_back((float)((double)k / n_angle[r] * 2.0 * 3.14159265358979323846 - 3.14159265358979323846));
      }
    labels = (int)radius.size();
    return labels > 0 && labels < 4096;
  }
  // The stick in the game's [-1, 1] (raw / 80).
  void decode(int label, float& x, float& y) const {
    label = std::clamp(label, 0, labels - 1);
    const float rx = radius[(size_t)label] * std::cos(angle[(size_t)label]);
    const float ry = radius[(size_t)label] * std::sin(angle[(size_t)label]);
    x = std::clamp(((rx + 80.f) / 160.f) * 2.f - 1.f, -1.f, 1.f);
    y = std::clamp(((ry + 80.f) / 160.f) * 2.f - 1.f, -1.f, 1.f);
  }
};

struct Policy {
  OrtSession* session = nullptr;
  int delay = 0, axis = 32, shoulder = 10;
  bool custom = false;          // custom_v1 controller (Phillip's newest policies)
  Polar main_stick, c_stick;
  int mask_window = 0;          // the opponent's tech starts look alike for this many frames
  bool heavy = false;
  double rating = 0;
  std::vector<PackSpec> in, out;
  std::map<std::string, Leaf> in_leaf, out_leaf;
  std::vector<std::pair<std::string, std::vector<double>>> initial;   // prev_state.* starting values
  std::vector<std::string> noise;                                      // noise.* inputs
  std::vector<std::pair<std::string, std::string>> feedback;          // output leaf -> input leaf
};

std::mutex g_policy_mutex;
std::map<std::string, std::unique_ptr<Policy>> g_policies;   // by file; never unloaded while running

ONNXTensorElementDataType dtype_of(const std::string& pack, size_t& elem) {
  const std::string t = pack.substr(pack.find('.') + 1);
  if (t == "float32") { elem = 4; return ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT; }
  if (t == "int32") { elem = 4; return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32; }
  if (t == "bool") { elem = 1; return ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL; }
  if (t == "uint8") { elem = 1; return ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8; }
  if (t == "int64") { elem = 8; return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64; }
  elem = 0;
  return ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
}

bool read_layout(const json& layout, std::vector<PackSpec>& packs, std::map<std::string, Leaf>& leaves) {
  if (!layout.is_object()) return false;
  for (auto it = layout.begin(); it != layout.end(); ++it) {
    PackSpec p;
    p.name = it.key();
    p.type = dtype_of(p.name, p.elem);
    if (!p.elem || !it.value().is_array()) return false;
    for (const json& l : it.value()) {
      Leaf leaf;
      leaf.pack = (int)packs.size();
      leaf.offset = l.value("offset", (size_t)0);
      leaf.size = l.value("size", (size_t)0);
      p.width = std::max(p.width, leaf.offset + leaf.size);
      leaves[l.value("name", std::string())] = leaf;
    }
    if (!p.width || p.width > (1u << 20)) return false;
    packs.push_back(p);
  }
  return true;
}

void flatten(const json& v, std::vector<double>& out) {
  if (v.is_array()) for (const json& x : v) flatten(x, out);
  else if (v.is_number()) out.push_back(v.get<double>());
  else if (v.is_boolean()) out.push_back(v.get<bool>() ? 1.0 : 0.0);
}

// Only what this runtime builds is accepted: a policy that needs items, Randall, the Fountain's
// platforms, Nana, speeds, controller history, masked animations or frame skipping is refused rather
// than fed zeros it never saw in training.
bool supported(const json& m, std::string& why) {
  try {
    if (m.at("format_version") != 1) { why = "format"; return false; }
    if (m.at("batch_size") != 1) { why = "batch size"; return false; }
    const std::string controller = m.at("controller").at("type").get<std::string>();
    if (controller != "default" && controller != "custom_v1") { why = "controller encoding"; return false; }
    const json& cfg = m.at("config");
    const json& e = cfg.at("embed");
    const std::string items = e.at("items").value("type", std::string("skip"));
    if (items != "skip" && items != "flat") { why = "items"; return false; }
    const json& p = e.at("player");
    if (p.value("with_speeds", false) || p.value("with_controller", false)) { why = "player fields"; return false; }
    if (has(cfg, "observation")) {
      const json& o = cfg["observation"];
      if (has(o, "frame_skip") && o["frame_skip"].value("skip", 0) != 0) { why = "observation skip"; return false; }
    }
    if (cfg.at("policy").value("frame_skip", 1) != 1) { why = "frame skip"; return false; }
    return true;
  } catch (const json::exception&) {
    why = "metadata";
    return false;
  }
}

const Policy* load_policy(const std::string& file) {
  std::lock_guard<std::mutex> lk(g_policy_mutex);
  auto found = g_policies.find(file);
  if (found != g_policies.end()) return found->second.get();
  std::unique_ptr<Policy> pol;
  const double t0 = host::now_seconds();
  const std::string path = pro_dir() + file;
  OrtSessionOptions* opt = nullptr;
  OrtSession* session = nullptr;
  // A large policy gets a second thread: 13 ms a step on one became 10 ms on two.
  std::error_code size_ec;
  const auto bytes = std::filesystem::file_size(std::filesystem::u8path(path), size_ec);
  const bool large = !size_ec && bytes > 50u * 1024u * 1024u;
  const int large_threads = (int)std::clamp(std::thread::hardware_concurrency() / 4u, 2u, 4u);
  bool good = ok(g_ort->CreateSessionOptions(&opt), "session options") &&
              ok(g_ort->SetIntraOpNumThreads(opt, large ? large_threads : 1), "threads") && ok(g_ort->SetInterOpNumThreads(opt, 1), "threads") &&
              ok(g_ort->SetSessionGraphOptimizationLevel(opt, ORT_ENABLE_ALL), "optimization");
#ifdef _WIN32
  std::wstring wpath(path.size() + 1, L'\0');
  wpath.resize((size_t)MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), (int)wpath.size()) - 1);
  good = good && ok(g_ort->CreateSession(g_env, wpath.c_str(), opt, &session), path.c_str());
#else
  good = good && ok(g_ort->CreateSession(g_env, path.c_str(), opt, &session), path.c_str());
#endif
  if (opt) g_ort->ReleaseSessionOptions(opt);
  OrtAllocator* alloc = nullptr;
  json meta;
  if (good) {
    OrtModelMetadata* md = nullptr;
    char* text = nullptr;
    good = ok(g_ort->GetAllocatorWithDefaultOptions(&alloc), "allocator") &&
           ok(g_ort->SessionGetModelMetadata(session, &md), "metadata") &&
           ok(g_ort->ModelMetadataLookupCustomMetadataMap(md, alloc, "slippi_ai", &text), "metadata");
    if (good && text) meta = json::parse(text, nullptr, false);
    if (text) alloc->Free(alloc, text);
    if (md) g_ort->ReleaseModelMetadata(md);
  }
  std::string why;
  if (good && !supported(meta, why)) {
    host::log("pro ai: %s is not supported (%s)", file.c_str(), why.c_str());
    good = false;
  }
  if (good) {
    pol = std::make_unique<Policy>();
    pol->session = session;
    try {
      pol->delay = meta.at("config").at("policy").at("delay").get<int>();
      pol->axis = meta.at("controller").at("default").at("axis_spacing").get<int>();
      pol->shoulder = meta.at("controller").at("default").at("shoulder_spacing").get<int>();
      pol->heavy = large;
      pol->custom = meta.at("controller").at("type") == "custom_v1";
      if (pol->custom)
        good = pol->main_stick.build(meta.at("controller").at("custom_v1").at("main_stick_config")) &&
               pol->c_stick.build(meta.at("controller").at("custom_v1").at("c_stick_config"));
      const json& cfg = meta.at("config");
      if (has(cfg, "observation") && has(cfg["observation"], "animation") && cfg["observation"]["animation"].value("mask", false))
        pol->mask_window = cfg["observation"]["animation"].value("tech_mask_window", 7);
      if (meta.at("agent_config").is_object()) pol->rating = meta["agent_config"].value("rating", 0.0);
      good = read_layout(meta.at("inputs"), pol->in, pol->in_leaf) && read_layout(meta.at("outputs"), pol->out, pol->out_leaf);
      for (auto it = meta.at("initial_state").begin(); good && it != meta.at("initial_state").end(); ++it) {
        std::vector<double> v;
        flatten(it.value().at("value"), v);
        pol->initial.emplace_back(it.key(), std::move(v));
      }
    } catch (const json::exception&) {
      good = false;
    }
    good = good && pol->delay > 0 && pol->delay <= 60 && pol->axis > 0 && pol->shoulder > 0;
    for (const auto& [name, leaf] : pol->in_leaf) if (name.rfind("noise.", 0) == 0) pol->noise.push_back(name);
    for (const auto& [name, leaf] : pol->out_leaf)
      if (name.rfind("state.", 0) == 0 || name.rfind("actions.", 0) == 0) {
        const std::string target = "prev_" + name;
        auto in = pol->in_leaf.find(target);
        if (in != pol->in_leaf.end() && in->second.size == leaf.size) pol->feedback.emplace_back(name, target);
      }
    // The graph may have pruned dead packed inputs: feed only the ones it still has.
    size_t count = 0;
    good = good && ok(g_ort->SessionGetInputCount(session, &count), "inputs");
    for (size_t i = 0; good && i < count; ++i) {
      char* name = nullptr;
      if (!ok(g_ort->SessionGetInputName(session, i, alloc, &name), "input name")) { good = false; break; }
      for (PackSpec& p : pol->in) if (p.name == name) p.fed = true;
      alloc->Free(alloc, name);
    }
    static const char* const kDefault[] = {"actions.0.main_stick.x", "actions.0.main_stick.y", "actions.0.c_stick.x",
                                           "actions.0.c_stick.y", "actions.0.shoulder", "actions.0.buttons.A"};
    static const char* const kCustom[] = {"actions.0.buttons", "actions.0.main_stick"};
    if (good && pol->custom) {
      for (const char* need : kCustom)
        if (!pol->out_leaf.count(need)) { host::log("pro ai: %s has no %s", file.c_str(), need); good = false; }
    } else if (good) {
      for (const char* need : kDefault)
        if (!pol->out_leaf.count(need)) { host::log("pro ai: %s has no %s", file.c_str(), need); good = false; }
    }
  }
  if (!good) {
    if (session) g_ort->ReleaseSession(session);
    g_policies[file] = nullptr;   // do not retry every frame
    host::log("pro ai: %s could not be loaded", file.c_str());
    return nullptr;
  }
  host::log("pro ai: %s loaded in %.2f s (delay %d frames%s)", file.c_str(), host::now_seconds() - t0, pol->delay,
            pol->custom ? ", items, Randall, Fountain, Nana" : "");
  return (g_policies[file] = std::move(pol)).get();
}

// ---- the step: what the game saw -> the policy -> the controller it holds `delay` frames later ----
struct Buffers { std::vector<std::vector<uint8_t>> in, out; };

void put(const Policy& p, Buffers& b, const std::string& name, double v, size_t index = 0) {
  auto it = p.in_leaf.find(name);
  if (it == p.in_leaf.end() || index >= it->second.size) return;
  const PackSpec& pack = p.in[(size_t)it->second.pack];
  uint8_t* at = b.in[(size_t)it->second.pack].data() + (it->second.offset + index) * pack.elem;
  switch (pack.type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: { const float f = (float)v; std::memcpy(at, &f, 4); break; }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: { const int32_t i = (int32_t)std::lround(v); std::memcpy(at, &i, 4); break; }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: { const int64_t i = (int64_t)std::llround(v); std::memcpy(at, &i, 8); break; }
    default: *at = (uint8_t)(v != 0 ? 1 : 0); break;   // bool, uint8
  }
}

double get(const Policy& p, const Buffers& b, const std::string& name) {
  auto it = p.out_leaf.find(name);
  if (it == p.out_leaf.end()) return 0;
  const PackSpec& pack = p.out[(size_t)it->second.pack];
  const uint8_t* at = b.out[(size_t)it->second.pack].data() + it->second.offset * pack.elem;
  switch (pack.type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: { float f; std::memcpy(&f, at, 4); return f; }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: { int32_t i; std::memcpy(&i, at, 4); return i; }
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: { int64_t i; std::memcpy(&i, at, 8); return (double)i; }
    default: return *at;
  }
}

void start(const Policy& p, Buffers& b) {
  b.in.assign(p.in.size(), {});
  b.out.assign(p.out.size(), {});
  for (size_t i = 0; i < p.in.size(); ++i) b.in[i].assign(p.in[i].width * p.in[i].elem, 0);
  for (size_t i = 0; i < p.out.size(); ++i) b.out[i].assign(p.out[i].width * p.out[i].elem, 0);
  for (const auto& [name, values] : p.initial)
    for (size_t k = 0; k < values.size(); ++k) put(p, b, name, values[k], k);
}

// The Slippi stage id the game sends -> libmelee's Stage enum, which the policies saw.
int libmelee_stage(int slippi) {
  switch (slippi) {
    case 2: return 0x08;    // Fountain of Dreams
    case 3: return 0x12;    // Pokemon Stadium
    case 8: return 0x06;    // Yoshi's Story
    case 28: return 0x1A;   // Dream Land
    case 31: return 0x18;   // Battlefield
    case 32: return 0x19;   // Final Destination
    default: return 0;
  }
}

// What a fighter's own observation keeps between steps: the item slots (slippi-ai's ItemAssigner: an
// item keeps its slot while it lives, a freed slot is the next one used) and the opponent's tech
// animation mask (observations.py AnimationFilter, fast mode).
struct Memory {
  std::map<uint32_t, int> item_slot;
  std::vector<int> free_slots;
  int prev_action = 0, same_frames = 0;
  void reset() {
    item_slot.clear();
    free_slots.clear();
    for (int i = MU_GHOST_ITEMS - 1; i >= 0; --i) free_slots.push_back(i);
    prev_action = 0;
    same_frames = 0;
  }
};

void put_fighter(const Policy& p, Buffers& b, const std::string& pre, const MuGhostFighter& f, int action) {
  put(p, b, pre + "x", f.num[0]);
  put(p, b, pre + "y", f.num[1]);
  put(p, b, pre + "facing", f.num[2] > 0 ? 1 : 0);
  put(p, b, pre + "percent", std::max(0.0, std::floor((double)f.num[3])));
  put(p, b, pre + "shield_strength", f.num[4]);
  put(p, b, pre + "on_ground", f.num[8] != 0 ? 0 : 1);
  put(p, b, pre + "jumps_left", std::max(0.0f, f.num[9]));
  put(p, b, pre + "action", action);
  put(p, b, pre + "character", f.kind);
  put(p, b, pre + "invulnerable", f.hurtbox != 0 ? 1 : 0);
}

void observe(const Policy& p, Buffers& b, const MuGhostObs& o, int name_code, bool reset, std::mt19937& rng, Memory& mem,
             float temperature = 1.f) {
  if (reset) mem.reset();
  // The opponent's neutral, forward and backward tech start the same: for its first frames the policy
  // sees every one of them as a neutral tech, as in its training.
  int opp_action = o.opp.motion;
  if (p.mask_window > 0) {
    if (opp_action == mem.prev_action) ++mem.same_frames;
    else { mem.same_frames = 0; mem.prev_action = opp_action; }
    if (opp_action >= 0xC7 && opp_action <= 0xC9 && mem.same_frames < p.mask_window) opp_action = 0xC7;
  }
  put_fighter(p, b, "game.p0.", o.self, o.self.motion);
  put_fighter(p, b, "game.p1.", o.opp, opp_action);
  const struct { const char* pre; bool on; const MuGhostFighter* f; } nanas[2] = {
      {"game.p0.nana.", o.self_nana != 0, &o.self_nana_f}, {"game.p1.nana.", o.opp_nana != 0, &o.opp_nana_f}};
  for (const auto& n : nanas) {
    static const MuGhostFighter zero{};
    put_fighter(p, b, n.pre, n.on ? *n.f : zero, n.on ? n.f->motion : 0);
    put(p, b, std::string(n.pre) + "exists", n.on ? 1 : 0);
    if (!n.on) { put(p, b, std::string(n.pre) + "facing", 0); put(p, b, std::string(n.pre) + "on_ground", 0); }
  }
  const int stage = libmelee_stage(o.stage);
  put(p, b, "game.stage", stage);
  // Randall (Yoshi's Story) where libmelee puts it on this Slippi frame; the Fountain's platforms.
  const int cycle = ((o.match_frame % 1200) + 1200) % 1200;
  put(p, b, "game.randall.x", stage == 0x06 ? kRandall[cycle][0] : 0.0);
  put(p, b, "game.randall.y", stage == 0x06 ? kRandall[cycle][1] : 0.0);
  put(p, b, "game.fod_platforms.left", stage == 0x08 ? o.fod_left : 0.0);
  put(p, b, "game.fod_platforms.right", stage == 0x08 ? o.fod_right : 0.0);
  // Items, each in the slot it took when it appeared.
  {
    const int count = std::min<int>(o.item_count, MU_GHOST_ITEMS);
    std::set<uint32_t> alive;
    for (int i = 0; i < count; ++i) alive.insert(o.items[i].spawn);
    for (auto it = mem.item_slot.begin(); it != mem.item_slot.end();) {
      if (!alive.count(it->first)) { mem.free_slots.push_back(it->second); it = mem.item_slot.erase(it); }
      else ++it;
    }
    bool used[MU_GHOST_ITEMS] = {};
    for (int i = 0; i < count; ++i) {
      const MuGhostItem& item = o.items[i];
      auto found = mem.item_slot.find(item.spawn);
      int slot;
      if (found != mem.item_slot.end()) slot = found->second;
      else if (!mem.free_slots.empty()) { slot = mem.free_slots.back(); mem.free_slots.pop_back(); mem.item_slot[item.spawn] = slot; }
      else continue;
      used[slot] = true;
      const std::string pre = "game.items.item_" + std::to_string(slot) + ".";
      put(p, b, pre + "exists", 1);
      put(p, b, pre + "type", item.kind);
      put(p, b, pre + "state", item.state);
      put(p, b, pre + "x", item.x);
      put(p, b, pre + "y", item.y);
    }
    for (int slot = 0; slot < MU_GHOST_ITEMS; ++slot) {
      if (used[slot]) continue;
      const std::string pre = "game.items.item_" + std::to_string(slot) + ".";
      for (const char* f : {"exists", "type", "state", "x", "y"}) put(p, b, pre + f, 0);
    }
  }
  put(p, b, "name", name_code);
  put(p, b, "needs_reset", reset ? 1 : 0);
  // The catalog's temperature for this policy; MELEE_PRO_AI_TEMP (tests) steps every policy at one value.
  static const char* forced_temp = std::getenv("MELEE_PRO_AI_TEMP");
  put(p, b, "temperature", forced_temp ? std::atof(forced_temp) : (double)temperature);
  put(p, b, "rating", p.rating);
  // The policy samples its controller from these uniform numbers. MELEE_PRO_AI_FIXED_NOISE (tests: the
  // parity check against slippi-ai's Python) holds them at one value.
  static const char* fixed = std::getenv("MELEE_PRO_AI_FIXED_NOISE");
  std::uniform_real_distribution<float> u(0.f, 1.f);
  for (const std::string& n : p.noise) {
    const Leaf& l = p.in_leaf.at(n);
    for (size_t k = 0; k < l.size; ++k) put(p, b, n, fixed ? std::atof(fixed) : u(rng), k);
  }
}

bool run(const Policy& p, Buffers& b) {
  OrtMemoryInfo* mem = nullptr;
  if (!ok(g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem), "memory")) return false;
  std::vector<OrtValue*> ins, outs(p.out.size(), nullptr);
  std::vector<const char*> in_names, out_names;
  bool good = true;
  for (size_t i = 0; i < p.in.size() && good; ++i) {
    if (!p.in[i].fed) continue;
    const int64_t shape[2] = {1, (int64_t)p.in[i].width};
    OrtValue* v = nullptr;
    good = ok(g_ort->CreateTensorWithDataAsOrtValue(mem, b.in[i].data(), b.in[i].size(), shape, 2, p.in[i].type, &v), "input");
    if (good) { ins.push_back(v); in_names.push_back(p.in[i].name.c_str()); }
  }
  for (const PackSpec& s : p.out) out_names.push_back(s.name.c_str());
  good = good && ok(g_ort->Run(p.session, nullptr, in_names.data(), ins.data(), ins.size(), out_names.data(), outs.size(), outs.data()), "step");
  for (size_t i = 0; i < outs.size() && good; ++i) {
    void* data = nullptr;
    good = ok(g_ort->GetTensorMutableData(outs[i], &data), "output");
    if (good) std::memcpy(b.out[i].data(), data, b.out[i].size());
  }
  for (OrtValue* v : ins) g_ort->ReleaseValue(v);
  for (OrtValue* v : outs) if (v) g_ort->ReleaseValue(v);
  g_ort->ReleaseMemoryInfo(mem);
  if (!good) return false;
  // Its own sampled controller and recurrent state go back in as the next step's history.
  for (const auto& [from, to] : p.feedback) {
    const Leaf& src = p.out_leaf.at(from);
    for (size_t k = 0; k < src.size; ++k) {
      const PackSpec& pk = p.out[(size_t)src.pack];
      const uint8_t* at = b.out[(size_t)src.pack].data() + (src.offset + k) * pk.elem;
      double v;
      switch (pk.type) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: { float f; std::memcpy(&f, at, 4); v = f; break; }
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: { int32_t i; std::memcpy(&i, at, 4); v = i; break; }
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: { int64_t i; std::memcpy(&i, at, 8); v = (double)i; break; }
        default: v = *at; break;
      }
      put(p, b, to, v, k);
    }
  }
  return true;
}

MuGhostAct decode(const Policy& p, const Buffers& b) {
  MuGhostAct a{};
  if (p.custom) {
    // custom_v1: the buttons label is b, x/y, l/r, z-a-shoulder (7) and the c-stick, most significant
    // first; the main stick has its own label.
    p.main_stick.decode((int)get(p, b, "actions.0.main_stick"), a.lx, a.ly);
    int label = (int)get(p, b, "actions.0.buttons");
    const int c = label % p.c_stick.labels; label /= p.c_stick.labels;
    const int zas = label % 7; label /= 7;
    const bool lr = (label % 2) != 0; label /= 2;
    const bool xy = (label % 2) != 0; label /= 2;
    const bool bb = (label % 2) != 0;
    p.c_stick.decode(c, a.cx, a.cy);
    static const float kShoulder[3] = {0.f, 0.35f, 1.f};
    const bool z = zas == 6;
    const bool aa = z || zas / 3 != 0;
    a.l = z ? kShoulder[1] : kShoulder[zas % 3];
    if (aa) a.buttons |= 0x0100;
    if (bb) a.buttons |= 0x0200;
    if (xy) a.buttons |= 0x0800;
    if (z) a.buttons |= 0x0010;
    if (lr) { a.buttons |= 0x0040; a.l = 1.f; }
    return a;
  }
  const auto axis = [&](const char* n) {
    return (float)std::clamp(get(p, b, n) / p.axis * 2.0 - 1.0, -1.0, 1.0);
  };
  a.lx = axis("actions.0.main_stick.x");
  a.ly = axis("actions.0.main_stick.y");
  a.cx = axis("actions.0.c_stick.x");
  a.cy = axis("actions.0.c_stick.y");
  // libmelee presses the analog shoulder on L; the digital L and R are their own buttons.
  a.l = (float)std::clamp(get(p, b, "actions.0.shoulder") / p.shoulder, 0.0, 1.0);
  static const struct { const char* name; unsigned bit; } kButtons[] = {
      {"actions.0.buttons.A", 0x0100}, {"actions.0.buttons.B", 0x0200}, {"actions.0.buttons.X", 0x0400},
      {"actions.0.buttons.Y", 0x0800}, {"actions.0.buttons.Z", 0x0010}, {"actions.0.buttons.L", 0x0040},
      {"actions.0.buttons.R", 0x0020}, {"actions.0.buttons.D_UP", 0x0008}};
  for (const auto& k : kButtons) if (get(p, b, k.name) != 0) a.buttons |= k.bit;
  if (a.buttons & 0x0040) a.l = 1.f;
  if (a.buttons & 0x0020) a.r = 1.f;
  return a;
}

// ---- one fighter: a worker that steps its policy in frame order ----------------------------------
struct Job { uint64_t generation; uint32_t frame; bool reset; MuGhostObs obs; const Entry* entry; int name; };

struct Slot {
  std::mutex m;
  std::condition_variable cv;
  std::deque<Job> jobs;
  std::map<uint32_t, MuGhostAct> answers;
  std::thread worker;
  bool stop = false;
  uint64_t generation = 0;
  // game thread only
  bool live = false;
  unsigned pro = 0xFF;
  int self_kind = -1, opp_kind = -1;
  const Entry* entry = nullptr;
  uint32_t first = 0, last = 0;
  MuGhostAct last_out{};
  // worker only
  const Policy* policy = nullptr;
  Buffers buffers;
  Memory memory;
  std::mt19937 rng{0x9E3779B9u};
  double spent = 0;
  int steps = 0;
  double heavy_spent = 0;
  int heavy_steps = 0;
};

Slot g_slot[kSlots];
std::mutex g_mutex;

void work(Slot* s) {
#ifdef _WIN32
  // Above normal, as the renderer and the sub-frame solver: the game's own thread runs at the highest
  // priority, and a starved step would make the fighter's answer late and hold the frame up.
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lk(s->m);
      s->cv.wait(lk, [&] { return s->stop || !s->jobs.empty(); });
      if (s->stop) return;
      job = s->jobs.front();
      s->jobs.pop_front();
      if (job.generation != s->generation) continue;
    }
    if (job.reset || !s->policy) {
      s->policy = load_policy(job.entry->file);
      if (s->policy) start(*s->policy, s->buffers);
      s->heavy_spent = 0;
      s->heavy_steps = 0;
      s->rng.seed(0x9E3779B9u ^ (job.frame * 2654435761u));
    }
    MuGhostAct a{};
    bool stepped = false;
    if (s->policy) {
      const double t0 = host::now_seconds();
      observe(*s->policy, s->buffers, job.obs, job.name, job.reset, s->rng, s->memory, job.entry->temperature);
      stepped = run(*s->policy, s->buffers);
      if (stepped) a = decode(*s->policy, s->buffers);
      const double took = host::now_seconds() - t0;
      s->spent += took;
      // A large policy must average under a frame (16.7 ms) or the fighter's answers come late.
      // Measured over its first 600 steps (after 60 to warm up); too slow, the lighter policies play
      // from the next match. MELEE_PRO_AI_SLOW_MS (tests) sets the limit.
      if (s->policy->heavy && g_large_ok.load() >= 0) {
        static const double limit = [] { const char* v = std::getenv("MELEE_PRO_AI_SLOW_MS"); return v ? std::atof(v) : 15.0; }();
        if (++s->heavy_steps > 60) s->heavy_spent += took;
        if (s->heavy_steps == 660) {
          const double ms = s->heavy_spent / 600 * 1000.0;
          if (ms > limit) {
            g_large_ok = -1;
            host::log("pro ai: %.1f ms a step is too slow for the large policies here; lighter ones play from the next match", ms);
          }
        }
      }
      if (++s->steps == 3600) {
        host::log("pro ai: P%u %.2f ms a step", (unsigned)job.obs.slot + 1, s->spent / s->steps * 1000.0);
        s->spent = 0;
        s->steps = 0;
      }
    }
    {
      std::lock_guard<std::mutex> lk(s->m);
      if (job.generation == s->generation) {
        // A failed step answers with the neutral controller: the frame is not held up.
        s->answers[job.frame] = a;
        (void)stepped;
      }
    }
    s->cv.notify_all();
  }
}

void check_speed_later(const Entry& e) {
  bool expected = false;
  if (!g_checking.compare_exchange_strong(expected, true)) return;
  if (g_check_thread.joinable()) g_check_thread.join();
  const std::string file = e.file;
  g_check_thread = std::thread([file] {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);   // as a match's worker runs
#endif
    const Policy* pol = load_policy(file);
    double ms = 1e9;
    if (pol) {
      Buffers b;
      Memory mem;
      std::mt19937 rng(7);
      start(*pol, b);
      MuGhostObs o{};
      o.version = MU_GHOST_VERSION;
      o.stage = 31;
      o.self.kind = 18; o.opp.kind = 1;
      o.self.num[0] = -30; o.opp.num[0] = 30; o.self.num[4] = o.opp.num[4] = 60; o.self.num[9] = o.opp.num[9] = 2;
      double spent = 0;
      for (int i = 0; i < 140; ++i) {
        const double t0 = host::now_seconds();
        observe(*pol, b, o, 0, i == 0, rng, mem);
        if (!run(*pol, b)) { spent = 1e9; break; }
        if (i >= 20) spent += host::now_seconds() - t0;
        o.match_frame = i;
      }
      ms = spent / 120 * 1000.0;
    }
    // An answer is due 21 frames after its step, so a step only has to average under a frame (16.7 ms):
    // this check and the 15 ms in-match limit keep a margin under that.
    static const double limit = [] { const char* v = std::getenv("MELEE_PRO_AI_CHECK_MS"); return v ? std::atof(v) : 14.0; }();
    g_large_ok = ms <= limit ? 1 : -1;
    host::log("pro ai: %s steps in %.1f ms here: Phillip's large policies %s", file.c_str(), ms,
              ms <= limit ? "play from the next match" : "are too slow for this PC; the lighter ones play");
    g_checking = false;
  });
}

struct Shutdown {
  ~Shutdown() {
    if (g_fetch_thread.joinable()) g_fetch_thread.join();
    if (g_check_thread.joinable()) g_check_thread.join();
    for (Slot& s : g_slot) {
      { std::lock_guard<std::mutex> lk(s.m); s.stop = true; }
      s.cv.notify_all();
      if (s.worker.joinable()) s.worker.join();
    }
  }
} g_shutdown;

}  // namespace

bool available(unsigned pro) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (pro >= kPros) return false;
  read_catalog();
  bool any = false;
  for (const Entry& e : g_catalog) any |= e.name[pro] >= 0 && e.kind == "imitation" && present(e);
  return any && runtime_ready();
}

bool act(const MuGhostObs& o, MuGhostAct& out) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (o.version != MU_GHOST_VERSION || o.slot >= kSlots || o.pro >= kPros) return false;
  for (const MuGhostFighter* f : {&o.self, &o.opp})
    for (float v : f->num) if (!std::isfinite(v)) return false;
  Slot& s = g_slot[o.slot];
  // Asked twice in one frame: the same answer, and the policy does not step again.
  if (s.live && s.pro == o.pro && !o.reset && o.frame == s.last) {
    out = s.last_out;
    return true;
  }
  const bool restart = o.reset || !s.live || s.pro != o.pro || o.frame != s.last + 1 ||
                       s.self_kind != o.self.kind || s.opp_kind != o.opp.kind;
  if (restart) {
    if (!runtime_ready()) return false;
    const Entry* e = choose(o.pro, o.slot, o.self.kind, o.opp.kind);
    if (!e) {
      if (s.live || s.pro != o.pro) host::log("pro ai: no policy for pro %u as fighter %u against %u", o.pro, o.self.kind, o.opp.kind);
      s.live = false;
      s.pro = o.pro;
      return false;
    }
    if (!s.worker.joinable()) s.worker = std::thread(work, &s);
    {
      std::lock_guard<std::mutex> lk2(s.m);
      ++s.generation;
      s.jobs.clear();
      s.answers.clear();
    }
    host::log("pro ai: P%u plays pro %u with %s (%s%s%s, %d frames of delay, temperature %.2f) from frame %u",
              (unsigned)o.slot + 1, o.pro, e->file.c_str(), e->kind.c_str(), e->trained_as.empty() ? "" : ", trained as ",
              e->trained_as.c_str(), e->delay, e->temperature, o.frame);
    s.entry = e;
    s.first = o.frame;
    s.live = true;
    s.pro = o.pro;
    s.self_kind = o.self.kind;
    s.opp_kind = o.opp.kind;
  }
  s.last = o.frame;
  {
    std::lock_guard<std::mutex> lk2(s.m);
    s.jobs.push_back(Job{s.generation, o.frame, restart, o, s.entry, s.entry->name[o.pro]});
  }
  s.cv.notify_all();
  // The answer due now was asked for `delay` frames ago; before that the fighter holds still, as
  // Phillip does on the first frames of a game.
  MuGhostAct a{};
  const uint32_t delay = (uint32_t)s.entry->delay;
  if (o.frame - s.first >= delay) {
    const uint32_t due = o.frame - delay;
    std::unique_lock<std::mutex> lk2(s.m);
    const bool got = s.cv.wait_for(lk2, std::chrono::seconds(5), [&] { return s.answers.count(due) != 0; });
    if (got) a = s.answers[due];
    else host::log("pro ai: P%u answer for frame %u is late", (unsigned)o.slot + 1, due);
    s.answers.erase(s.answers.begin(), s.answers.upper_bound(due));
  }
  // Phillip plays every frame itself, recovery included: no practice CPU safety net for it.
  a.buttons |= MU_GHOST_ACT_OWN_RECOVERY;
  s.last_out = a;
  out = a;
  return true;
}

void set_stronger_choice(int choice) { g_stronger = std::clamp(choice, 0, 2); }
int stronger_choice() { return g_stronger.load(); }

StrongerStatus stronger_status(int pro) {
  std::lock_guard<std::mutex> catalog_lock(g_mutex);
  read_catalog();
  StrongerStatus s;
  for (const Entry& e : g_catalog) {
    if (!e.heavy) continue;
    bool mine = pro < 0;
    if (!mine && pro < (int)kPros) mine = e.name[pro] >= 0;
    if (!mine) continue;
    ++s.total;
    if (!present(e)) { ++s.missing; s.missing_mb += e.size ? e.size / 1e6 : 90.0; }   // catalog size; 90 MB when an old catalog has none
  }
  std::lock_guard<std::mutex> lk(g_fetch_mutex);
  s.downloading = g_fetching.load();
  return s;
}

void download_stronger(int pro) {
  std::lock_guard<std::mutex> catalog_lock(g_mutex);
  read_catalog();
  for (const Entry& e : g_catalog) {
    if (!e.heavy || present(e)) continue;
    if (pro >= 0 && (pro >= (int)kPros || e.name[pro] < 0)) continue;
    // An explicit request retries failures without duplicating work already in the queue.
    fetch_later(e, true);
  }
}

}  // namespace phillip_ai
