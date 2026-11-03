// App-owned standard-layout controllers. No platform or SDL dependencies.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace yougame::relay_pads {
struct Pad {
  bool present = false;
  std::string id, name, kind;
  uint16_t buttons = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
  uint8_t lt = 0, rt = 0;
};
struct Frame {
  uint32_t seq = 0;
  double t = 0;
  std::array<Pad, 4> pads{};
};

inline bool parse(const std::string& line, Frame& out, std::string& reason) {
  using json = nlohmann::json;
  auto fail = [&](const char* why) { reason = why; return false; };
  if (line.rfind("yg-pads ", 0) != 0) return fail("bad prefix");
  // Four full pads with 64-character names stay well under this; a longer line is never parsed.
  if (line.size() > 2048) return fail("line longer than 2048 bytes");
  const auto j = json::parse(line.substr(8), nullptr, false);
  if (!j.is_object()) return fail("expected object");
  auto integer = [](const json& obj, const char* key, int64_t lo, uint64_t hi) {
    const auto i = obj.find(key);
    if (i == obj.end() || !i->is_number_integer()) return false;
    if (i->is_number_unsigned()) return i->get<uint64_t>() <= hi;
    const int64_t n = i->get<int64_t>();
    return n >= lo && (n < 0 || static_cast<uint64_t>(n) <= hi);
  };
  Frame next;
  if (!integer(j, "seq", 0, UINT32_MAX)) return fail("seq must be uint32");
  const auto time = j.find("t");
  if (time == j.end() || !time->is_number() || !std::isfinite(time->get<double>()) || time->get<double>() < 0)
    return fail("t must be finite nonnegative milliseconds");
  next.seq = j["seq"].get<uint32_t>(); next.t = time->get<double>();
  const auto pads = j.find("pads");
  if (pads == j.end() || !pads->is_array() || pads->size() > 4) return fail("pads must be an array of at most 4");
  for (const auto& p : *pads) {
    if (!p.is_object()) return fail("pad must be an object");
    if (!integer(p, "slot", 0, 3)) return fail("slot must be 0..3");
    auto& pad = next.pads[p["slot"].get<unsigned>()];
    if (pad.present) return fail("duplicate slot");
    auto string = [&](const char* key) { const auto i = p.find(key); return i != p.end() && i->is_string(); };
    // The id is the app's opaque per-run handle ("p1", "virtual") and the only pad identity the log shows, so it
    // stays short and plain; the pad's name never reaches the log (pack-platform).
    if (!string("id")) return fail("id must be 1..32 of A-Z a-z 0-9 _ -");
    const auto& id = p["id"].get_ref<const std::string&>();
    if (id.empty() || id.size() > 32 || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
          return std::isalnum(c) || c == '_' || c == '-'; }))
      return fail("id must be 1..32 of A-Z a-z 0-9 _ -");
    if (!string("name")) return fail("name must be a string");
    pad.name = p["name"].get<std::string>();
    // JSON is UTF-8: count code points, not bytes, for the 64-character limit.
    if (std::count_if(pad.name.begin(), pad.name.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; }) > 64)
      return fail("name exceeds 64 characters");
    if (!string("kind")) return fail("kind must be ps|xbox|switch|other");
    pad.kind = p["kind"].get<std::string>();
    if (pad.kind != "ps" && pad.kind != "xbox" && pad.kind != "switch" && pad.kind != "other")
      return fail("kind must be ps|xbox|switch|other");
    if (!integer(p, "b", 0, 65535)) return fail("b must be 0..65535");
    for (const char* axis : {"lx", "ly", "rx", "ry"})
      if (!integer(p, axis, -32768, 32767)) return fail("sticks must be int16");
    for (const char* trigger : {"lt", "rt"})
      if (!integer(p, trigger, 0, 255)) return fail("triggers must be 0..255");
    pad.present = true; pad.id = p["id"].get<std::string>();
    pad.buttons = p["b"].get<uint16_t>();
    pad.lx = p["lx"].get<int16_t>(); pad.ly = p["ly"].get<int16_t>();
    pad.rx = p["rx"].get<int16_t>(); pad.ry = p["ry"].get<int16_t>();
    pad.lt = p["lt"].get<uint8_t>(); pad.rt = p["rt"].get<uint8_t>();
  }
  out = std::move(next); reason.clear(); return true;
}

class State {
 public:
  using Log = std::function<void(const std::string&)>;
  explicit State(Log log, bool logging_ready = true) : log_(std::move(log)), logging_ready_(logging_ready) {}
  // stdin starts before argv parsing: emitting host::log then would open the default
  // log file before --log-file is known. Buffer until the host has selected its path.
  void logging_ready() {
    std::lock_guard<std::mutex> lock(mutex_);
    logging_ready_ = true;
    for (const auto& line : pending_logs_) log_(line);
    pending_logs_.clear();
  }
  bool accept(const std::string& line, uint64_t received_ms) {
    Frame next; std::string reason;
    const bool valid = parse(line, next, reason);
    std::unique_lock<std::shared_mutex> native_lock(native_mutex_);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!valid) {
      if (!rejected_) { rejected_ = true; emit_locked("pads: rejected relay line: " + reason); }
      return false;
    }
    if (!active_) {
      active_ = true; first_local_ = received_ms; first_app_ = next.t;
      emit_locked("pads: relay mode (app): non-adapter pads come from the YouGame app");
    }
    // Connects and disconnects by the app's opaque id and kind only, never the pad's name (pack-platform).
    for (unsigned slot = 0; slot < next.pads.size(); ++slot) {
      const auto& before = latest_.pads[slot];
      const auto& now = next.pads[slot];
      if (before.present && (!now.present || now.id != before.id))
        emit_locked("pads: relay disconnect slot " + std::to_string(slot) + " " + before.id);
      if (now.present && (!before.present || now.id != before.id))
        emit_locked("pads: relay connect slot " + std::to_string(slot) + " " + now.id + " " + now.kind);
    }
    // Signed delay relative to the first arrival: jitter, NOT absolute app-to-game latency.
    // Subtract in floating point to avoid unsigned wrap when an app timestamp moves backwards.
    lag_.push_back((double(received_ms) - double(first_local_)) - (double(next.t) - double(first_app_)));
    latest_ = std::move(next); // stdin arrival order wins, including repeated or wrapped seq.
    if (lag_.size() == 600) report_locked();
    return true;
  }
  bool active() const { std::lock_guard<std::mutex> lock(mutex_); return active_; }
  template<class XState> bool read(unsigned slot, XState* out) const {
    std::lock_guard<std::mutex> lock(mutex_); return read_locked(slot, out);
  }
  // Native calls share a gate with the irreversible latch. Native reads/probes/rumble
  // run concurrently, preserving the host's off-thread slow-driver isolation. Activation
  // waits for in-flight calls; no native call can start after activation.
  template<class XState, class Native> bool poll(unsigned slot, XState* out, Native native) const {
    std::shared_lock<std::shared_mutex> native_lock(native_mutex_);
    { std::lock_guard<std::mutex> lock(mutex_); if (active_) return read_locked(slot, out); }
    return native();
  }
  template<class Native> void rumble(Native native) {
    std::shared_lock<std::shared_mutex> native_lock(native_mutex_);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (active_) {
        if (!rumble_reported_) { rumble_reported_ = true; emit_locked("pads: relayed pads have no rumble yet"); }
        return;
      }
    }
    native();
  }
  void flush() { std::lock_guard<std::mutex> lock(mutex_); if (!lag_.empty()) report_locked(); }
  void reset_on_eof() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) return;
    for (unsigned slot = 0; slot < latest_.pads.size(); ++slot) {
      const auto& pad = latest_.pads[slot];
      if (pad.present)
        emit_locked("pads: relay disconnect slot " + std::to_string(slot) + " " + pad.id);
    }
    latest_ = {}; // Keep the relay latch: closed stdin must never restore native pads.
    if (!eof_reported_) {
      eof_reported_ = true;
      emit_locked("pads: relay ended (stdin closed): all relayed pads released");
    }
  }
 private:
  void emit_locked(const std::string& line) {
    if (logging_ready_) log_(line); else pending_logs_.push_back(line);
  }
  template<class XState> bool read_locked(unsigned slot, XState* out) const {
    if (!out) return false;
    *out = {};
    if (!active_ || slot >= 4 || !latest_.pads[slot].present) return false;
    const auto& p = latest_.pads[slot];
    out->dwPacketNumber = latest_.seq;
    auto& g = out->Gamepad;
    g.wButtons = p.buttons; g.bLeftTrigger = p.lt; g.bRightTrigger = p.rt;
    g.sThumbLX = p.lx; g.sThumbLY = p.ly; g.sThumbRX = p.rx; g.sThumbRY = p.ry;
    return true;
  }
  void report_locked() {
    std::sort(lag_.begin(), lag_.end());
    const auto percentile = [&](double p) {
      char ms[384]; // fixed notation also handles large, valid app timestamps without integer overflow
      std::snprintf(ms, sizeof ms, "%.0f", lag_[size_t(std::ceil(p * lag_.size())) - 1]);
      return std::string(ms);
    };
    emit_locked("pads: relay lag p50 " + percentile(.50) + " ms p95 " +
         percentile(.95) + " ms over " + std::to_string(lag_.size()) + " lines");
    lag_.clear();
  }
  Log log_;
  bool logging_ready_;
  std::vector<std::string> pending_logs_;
  mutable std::mutex mutex_;
  mutable std::shared_mutex native_mutex_;
  bool active_ = false, rejected_ = false, rumble_reported_ = false, eof_reported_ = false;
  Frame latest_;
  uint64_t first_local_ = 0;
  double first_app_ = 0;
  std::vector<double> lag_;
};
} // namespace yougame::relay_pads
