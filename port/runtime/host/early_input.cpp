// Early inputs: the sampler thread (see early_input.h). It reads the controllers about a thousand
// times a second while an online match runs, and when the local player's pad changes in a way the
// game acts on, hands the new pad to the transport at once, stamped with the pad frame that change
// will first appear in. The host knows that frame exactly: the pad polled at the next frame goes out
// as (last sent frame + 1), and every change seen between two polls lands in that pad.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "early_input.h"
#include "host.h"
#include "tick_timing.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace host::early_input {
namespace {

std::atomic<bool> g_setting{true};
std::atomic<bool> g_running{false};
std::thread g_thread;
SendFn g_send = nullptr;
std::mutex g_mutex;                        // g_known, g_stats
uint8_t g_known[kPadBytes] = {};            // the newest pad the other side has, sent or early
std::atomic<int32_t> g_last_sent_frame{0};
std::atomic<int> g_local_port{-1};
std::atomic<bool> g_skipped{false};         // the host answered a poll without sending a pad
// A poll has read the controller but its pad has not been sent (or skipped) yet: a change seen now
// is not in that pad, so it belongs to the frame after it.
std::atomic<bool> g_poll_pending{false};
int32_t g_crossed_target = 0;               // the target of the newest judged change (under g_mutex)
double g_last_follow_up = 0;                // when the newest follow-up went (under g_mutex)
constexpr double kFollowUpSeconds = 0.002;
Stats g_stats;

// MELEE_EARLY_RULES=all|presses: which changes count (early_input_rules.h). Default all.
Rules rules() {
  static const Rules value = [] {
    const char* v = std::getenv("MELEE_EARLY_RULES");
    return v && *v == 'p' ? Rules::kPresses : Rules::kAllEdges;
  }();
  return value;
}

// MELEE_EARLY_GUARD_MS=N (measurement only): a change seen within N ms of when the next poll would
// come at an even 60 Hz is left to that poll. Off by default: online, the poll often comes later
// (the game waits for the other player), and the guard then held back the sends that help most.
double guard_seconds() {
  static const double value = [] {
    const char* v = std::getenv("MELEE_EARLY_GUARD_MS");
    const double ms = v && *v ? std::atof(v) : 0.0;
    return ms > 0.0 && ms < 8.0 ? ms / 1000.0 : 0.0;
  }();
  return value;
}
constexpr double kFrameSeconds = 1.0 / 59.94;

// MELEE_EARLY_TRACE=1 (tests): the first 600 sends, sent pads and skips are logged with their frame
// numbers and times, to match against the receiver's "early wrong" lines.
bool trace_on() {
  static const bool value = [] { const char* v = std::getenv("MELEE_EARLY_TRACE"); return v && *v == '1'; }();
  return value;
}
std::atomic<int> g_trace_lines{0};
bool trace_line() { return trace_on() && g_trace_lines.fetch_add(1, std::memory_order_relaxed) < 600; }

// MELEE_EARLY_INPUTS=0|1 (tests, hidden runs): the setting file is not read in scripted runs, so
// the switch has to come from the environment there. Unset: the setting decides.
int env_override() {
  static const int value = [] {
    const char* v = std::getenv("MELEE_EARLY_INPUTS");
    if (!v || !*v) return -1;
    return *v == '0' ? 0 : 1;
  }();
  return value;
}

// MELEE_TEST_EARLY_LEAD_MS=N (tests only): in a scripted run the controller is the script, and a
// script changes its pad exactly at a poll, so there is never a change between polls to send early.
// With this set, the sampler takes the pad the script will deliver at the next poll and sends it N
// ms before that poll. Deterministic, so two runs (early inputs on and off) feed the game the same
// inputs and only the timing of what the other side learns differs.
double test_lead_seconds() {
  static const double value = [] {
    const char* v = std::getenv("MELEE_TEST_EARLY_LEAD_MS");
    const double ms = v && *v ? std::atof(v) : 0.0;
    return ms > 0.0 && ms < 16.0 ? ms / 1000.0 : 0.0;
  }();
  return value;
}

// The pad frame a change seen now first appears in.
int32_t target_frame() {
  return g_last_sent_frame.load(std::memory_order_acquire) + (g_poll_pending.load(std::memory_order_acquire) ? 2 : 1);
}

// `force`: after a skipped poll, any byte difference from what the other side has is sent again.
// Follow-ups: the game counts a guess wrong on any stick byte, so once a change crossed a line,
// later byte changes before that frame's poll are sent too, at most one every 2 ms; the last one
// before the poll is then the pad the poll takes.
void emit(const uint8_t now[kPadBytes], bool force) {
  bool send = false;
  int32_t target = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const int32_t next = target_frame();
    const bool differs = std::memcmp(now, g_known, kPadBytes) != 0;
    const bool judged = !force && matters(now, g_known, rules());
    const bool follow_up = !force && !judged && differs && g_crossed_target == next &&
                           now_seconds() - g_last_follow_up >= kFollowUpSeconds;
    if ((force && differs) || judged || follow_up) {
      // Too close to the next poll: leave it to that poll (g_known is not moved, so nothing is lost).
      const TickTiming tick = tick_timing();
      if (guard_seconds() > 0.0 && tick.pad > 0.0 && now_seconds() > tick.pad + kFrameSeconds - guard_seconds()) {
        ++g_stats.guarded;
      } else {
        std::memcpy(g_known, now, kPadBytes);
        // Read after the judgement: a pad the game sent in the meantime moves the target on.
        target = target_frame();
        ++g_stats.changes;
        if (force) ++g_stats.resent_after_skip;
        if (judged) g_crossed_target = target;
        if (follow_up) { ++g_stats.follow_ups; g_last_follow_up = now_seconds(); }
        send = true;
      }
    }
  }
  if (!send) return;
  if (trace_line())
    log("early send: target %d last %d pending %d %s %02x%02x %d,%d %d,%d %d %d at %.2f ms after the poll", target,
        g_last_sent_frame.load(), g_poll_pending.load() ? 1 : 0, force ? "after-skip" : "change", now[0], now[1],
        (int8_t)now[2], (int8_t)now[3], (int8_t)now[4], (int8_t)now[5], now[6], now[7],
        (now_seconds() - tick_timing().pad) * 1000.0);
  // MELEE_TEST_EARLY_LIE=1 (tests): every early pad goes out with its first button byte inverted, a
  // peer sending made-up early pads; the receiver must stop using them (mu_online.c, early trust).
  static const bool lie = [] { const char* v = std::getenv("MELEE_TEST_EARLY_LIE"); return v && *v == '1'; }();
  if (lie) {
    uint8_t fake[kPadBytes];
    std::memcpy(fake, now, kPadBytes);
    fake[0] ^= 0x1F;
    if (SendFn fn = g_send) fn(target, fake);
    return;
  }
  if (SendFn fn = g_send) fn(target, now);
}

void loop() {
  const double lead = test_lead_seconds();
  uint32_t handled_retrace = 0;
  while (g_running.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (g_last_sent_frame.load(std::memory_order_acquire) <= 0) continue;   // no pad has gone out yet
    PadState pads[4];
    int port = g_local_port.load(std::memory_order_acquire);
    if (lead > 0.0 && input_script_active()) {
      const uint32_t retrace = retrace_count();
      if (retrace == handled_retrace) continue;
      const TickTiming tick = tick_timing();
      if (tick.pad <= 0.0 || now_seconds() < tick.pad + 1.0 / 60.0 - lead) continue;
      handled_retrace = retrace;
      input_script_peek(retrace + 1, pads);
      if (port < 0) port = 0;
    } else {
      if (port < 0) continue;   // the local player's controller is not known yet
      input_read_devices(pads);
    }
    { std::lock_guard<std::mutex> lock(g_mutex); ++g_stats.samples; }
    if (pads[port].err != 0) continue;
    uint8_t now[kPadBytes];
    pad_to_wire(pads[port], now);
    clamp_rest(now);
    emit(now, g_skipped.exchange(false, std::memory_order_acq_rel));
  }
}

}  // namespace

bool enabled() {
  const int env = env_override();
  return env >= 0 ? env == 1 : g_setting.load(std::memory_order_relaxed);
}
void set_enabled(bool on) { g_setting.store(on, std::memory_order_relaxed); }

void start(SendFn send) {
  if (!enabled() || !send || g_running.load(std::memory_order_acquire)) return;
  stop();
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::memset(g_known, 0, sizeof g_known);
    g_crossed_target = 0;
    g_last_follow_up = 0;
  }
  g_last_sent_frame.store(0, std::memory_order_release);
  g_local_port.store(-1, std::memory_order_release);
  g_skipped.store(false, std::memory_order_release);
  g_poll_pending.store(false, std::memory_order_release);
  g_send = send;
  g_running.store(true, std::memory_order_release);
  g_thread = std::thread(loop);
  log("early inputs: watching the controller, rules %s, guard %.1f ms%s", rules() == Rules::kPresses ? "presses" : "all",
      guard_seconds() * 1000.0, test_lead_seconds() > 0.0 ? " (test: the script's next pad, ahead of its poll)" : "");
}

void stop() {
  g_running.store(false, std::memory_order_release);
  if (g_thread.joinable()) g_thread.join();
  g_send = nullptr;
}

bool running() { return g_running.load(std::memory_order_acquire); }

void note_sent_pad(int32_t frame, const uint8_t pad[kPadBytes]) {
  if (!g_running.load(std::memory_order_acquire)) return;
  uint8_t sent[kPadBytes];
  std::memcpy(sent, pad, kPadBytes);
  clamp_rest(sent);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::memcpy(g_known, sent, kPadBytes);
  }
  g_last_sent_frame.store(frame, std::memory_order_release);
  g_poll_pending.store(false, std::memory_order_release);
  if (trace_line()) log("early sent pad: frame %d %02x%02x %d,%d %d,%d %d %d", frame, sent[0], sent[1], (int8_t)sent[2],
                        (int8_t)sent[3], (int8_t)sent[4], (int8_t)sent[5], sent[6], sent[7]);
  if (g_local_port.load(std::memory_order_acquire) >= 0 || neutral(sent)) return;
  // The controller the local player holds: the one whose last read is this non-neutral pad. Online,
  // the in-game port and the controller port can differ (the match assigns the slot), so it is
  // found from the pads rather than assumed.
  PadState last[4];
  input_last_pads(last);
  int found = -1, matches = 0;
  for (int p = 0; p < 4; ++p) {
    if (last[p].err != 0) continue;
    uint8_t wire[kPadBytes];
    pad_to_wire(last[p], wire);
    clamp_rest(wire);
    if (std::memcmp(wire, sent, kPadBytes) == 0) { found = p; ++matches; }
  }
  if (matches == 1) {
    g_local_port.store(found, std::memory_order_release);
    { std::lock_guard<std::mutex> lock(g_mutex); g_stats.local_port = found; }
    log("early inputs: the local player is on controller port %d", found + 1);
  }
}

void pad_to_wire(const PadState& p, uint8_t out[kPadBytes]) {
  out[0] = (uint8_t)(p.button >> 8);
  out[1] = (uint8_t)p.button;
  out[2] = (uint8_t)p.stick_x;
  out[3] = (uint8_t)p.stick_y;
  out[4] = (uint8_t)p.sub_x;
  out[5] = (uint8_t)p.sub_y;
  out[6] = p.trig_l;
  out[7] = p.trig_r;
}

Stats stats() { std::lock_guard<std::mutex> lock(g_mutex); return g_stats; }
void reset_stats() { std::lock_guard<std::mutex> lock(g_mutex); g_stats = Stats(); g_stats.local_port = g_local_port.load(); }
void note_skipped() {
  g_poll_pending.store(false, std::memory_order_release);
  if (g_running.load(std::memory_order_acquire) && trace_line()) log("early skip: last %d", g_last_sent_frame.load());
  if (g_running.load(std::memory_order_acquire)) g_skipped.store(true, std::memory_order_release);
}
void note_poll() { if (g_running.load(std::memory_order_acquire)) g_poll_pending.store(true, std::memory_order_release); }
void note_packet() { std::lock_guard<std::mutex> lock(g_mutex); ++g_stats.packets; }
void note_received() { std::lock_guard<std::mutex> lock(g_mutex); ++g_stats.received; }

}  // namespace host::early_input
