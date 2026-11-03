// Host-only YouGame presentation and overlay protocol; no platform or game state.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <mutex>
#include <cstdint>
#ifdef YOUGAME_AGENT_TEST_HOOK
#include <chrono>
#include <limits>
#endif

namespace yougame::parity {
inline constexpr const char* kMenuHint = "Escape / YouGame menu (D-pad + Start)";
#if defined(_WIN32) || defined(__linux__)
inline constexpr const char* kCaps = "yg {\"caps\":[\"ready\",\"overlay\",\"join\",\"notice\",\"pads\",\"view\",\"volume\"]}";
#else
inline constexpr const char* kCaps = "yg {\"caps\":[\"ready\",\"overlay\",\"join\",\"notice\",\"pads\"]}";
#endif
inline constexpr const char* kReady = "yg {\"ready\":true}";
inline constexpr const char* kDown = "yg {\"overlay\":\"down\"}";
inline constexpr const char* kUp = "yg {\"overlay\":\"up\"}";
inline constexpr const char* kCancel = "yg {\"overlay\":\"cancel\"}";

class Signals {
 public:
  using Writer = void (*)(const char*);
  // Startup only. Publish the writer after caps, so ready can never overtake caps.
  void start(bool stdio, Writer writer) {
    if (!stdio || !writer) return;
    std::call_once(caps_, [&] { writer(kCaps); writer_.store(writer); });
  }
  void emit(const char* line) const {
    if (line) if (auto writer = writer_.load()) writer(line);
  }
#ifdef YOUGAME_AGENT_TEST_HOOK
  using Clock = std::chrono::steady_clock;
  bool first_present(Clock::time_point& at) const {
    const auto ticks = first_present_.load();
    if (ticks == kNotPresented) return false;
    at = Clock::time_point(Clock::duration(ticks));
    return true;
  }
#endif
  // Called after a successful swap of a visible game frame, never a loading panel.
  void presented(bool eligible
#ifdef YOUGAME_AGENT_TEST_HOOK
                 , Clock::time_point now = Clock::now()
#endif
                 ) {
    auto writer = writer_.load();
    if (eligible && writer && !ready_.exchange(true)) {
      writer(kReady);
#ifdef YOUGAME_AGENT_TEST_HOOK
      first_present_.store(now.time_since_epoch().count());
#endif
    }
  }
 private:
#ifdef YOUGAME_AGENT_TEST_HOOK
  static constexpr auto kNotPresented = std::numeric_limits<Clock::duration::rep>::min();
  std::atomic<Clock::duration::rep> first_present_{kNotPresented};
#endif
  std::once_flag caps_;
  std::atomic<Writer> writer_{nullptr};
  std::atomic<bool> ready_{false};
};

// The window thread owns this state. Keep swallowing a cancelled press until release:
// a repeat after focus returns must not open the game's practice menu.
class OverlayKey {
 public:
  struct Result { bool swallowed; const char* line; };
  Result tab(bool stdio, bool down, bool shift, bool repeat) {
    if (!stdio) return {false, nullptr};
    if (down) {
      if (!repeat) {
        reserved_ = shift;
        if (shift && !down_) { down_ = true; return {true, kDown}; }
      }
      return {reserved_, nullptr};
    }
#ifdef YOUGAME_AGENT_TEST_HOOK
    // A real Tab release cannot release a scripted tap early. Real presses during
    // the tap coalesce with it; their eventual release is still swallowed.
    if (scripted_) {
      const bool swallowed = reserved_;
      reserved_ = false;
      return {swallowed, nullptr};
    }
#endif
    const Result result{reserved_, down_ ? kUp : nullptr};
    down_ = reserved_ = false;
    return result;
  }
#ifdef YOUGAME_AGENT_TEST_HOOK
  const char* script_down() {
    if (reserved_ || down_) return nullptr;  // Includes a cancelled real hold.
    const auto event = tab(true, true, true, false);
    scripted_ = true;
    reserved_ = false;  // Only a real Tab press owns the real release.
    return event.line;
  }
  const char* script_up() {
    if (!scripted_) return nullptr;  // Focus loss already cancelled this tap.
    scripted_ = false;
    const bool was_down = down_;
    down_ = false;
    return was_down ? kUp : nullptr;
  }
#endif
  const char* cancel() {
#ifdef YOUGAME_AGENT_TEST_HOOK
    scripted_ = false;
#endif
    const bool was_down = down_;
    down_ = false;
    return was_down ? kCancel : nullptr;
  }
 private:
  bool down_ = false, reserved_ = false;
#ifdef YOUGAME_AGENT_TEST_HOOK
  bool scripted_ = false;
#endif
};

// Simulation-thread controller shortcut. Use the same down/up protocol as Shift+Tab,
// with a fixed tap duration independent of how long Start remains physically held.
class OverlayPad {
 public:
  const char* tick(uint64_t now_ms) {
    if (!tapping_ || now_ms < release_at_) return nullptr;
    tapping_ = false;
    return key_.tab(true, false, false, false).line;
  }
  const char* poll(bool stdio, int port, bool connected, uint16_t& buttons,
                   uint64_t now_ms, bool legacy_settings = false) {
    constexpr uint16_t start = 0x1000, down = 0x0004, z = 0x0010;
    const bool held = connected && (buttons & start);
    const bool pressed = held && !was_start_[port];
    was_start_[port] = held;
    if (!stdio) return nullptr;
    if (!held) claimed_[port] = false;
    const bool chord = pressed && (buttons & down) && !(buttons & z) && !legacy_settings;
    if (chord) claimed_[port] = true;
    // Keep Start suppressed until its release, including when Down is released first.
    if (claimed_[port]) buttons &= static_cast<uint16_t>(~start);
    if (!chord || tapping_) return nullptr;
    tapping_ = true;
    release_at_ = now_ms + 100;
    return key_.tab(true, true, true, false).line;
  }
 private:
  OverlayKey key_;
  bool was_start_[4]{}, claimed_[4]{};
  bool tapping_ = false;
  uint64_t release_at_ = 0;
};

// Header-only so renderer-only tools need no bridge/network dependency. In standalone
// builds the writer stays null. The stdio link installs its existing serialized writer.
inline Signals signals;
}  // namespace yougame::parity
