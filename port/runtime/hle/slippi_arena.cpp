// Arena rooms for Slippi Direct (see slippi_arena.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "slippi_arena.h"
#include "arena_feed.h"
#include "slippi_net.h"
#include "slippi_online.h"
#include "native_practice.h"
#include "host.h"
#include "window.h"
#include "test_env.h"
#include "yougame_link.h"
#include "../gx/render_options.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace slippi::arena {
namespace feed = slippi::arena_feed;
void watch_feed(const uint8_t* data, size_t size);

namespace {

// Players a room takes before it refuses the next: kRoomLimit. Tests only: MELEE_TEST_ARENA_ROOM_LIMIT
// (2 to kRoomLimit) lowers it, so the refusal is proven with real games inside the limit of 8 games.
int room_limit() {
  static const int n = [] {
    const char* v = host::test_env::getenv("MELEE_TEST_ARENA_ROOM_LIMIT");
    const int k = v ? std::atoi(v) : 0;
    return k >= 2 && k <= kRoomLimit ? k : kRoomLimit;
  }();
  return n;
}

// Room message kinds (first byte of an NP_MSG_MU_ARENA payload).
enum Msg : uint8_t { kHello = 1, kWelcome = 2, kRoomState = 3, kAction = 4, kReport = 5, kPings = 6, kFeed = 7,
                     kClosed = 8,     // kClosed: the host left, the room is over
                     kScore = 9,      // the game's stocks and percent (warm-up room strip); older builds ignore it
                     kWarmup = 10,    // member -> host: this member is in Training (1) or back (0)
                     kWarming = 11 }; // host -> everyone: who is warming up, a bit per member id (older builds ignore both)
enum Report : uint8_t { kStarted = 1, kEnded = 2 };
constexpr uint64_t kRetryMs = 1500;

struct Link {
  std::unique_ptr<NetplayClient> np;
  std::unique_ptr<Matchmaking> mm;   // a real room: the Direct-style search that makes this link
  std::string code;            // a real room: the friend at the other end
  uint8_t id = kNone;          // the member at the other end (host side), kNone until its hello
  int test_peer = -1;          // test rooms: the other member's index
  uint64_t retry_at = 0;
  uint64_t ready_at = 0;       // when it came up: a link up this long with no hello answered is reopened
  uint64_t refused_at = 0;     // a full room said no on this link (either way): dropped 3 s later
  bool was_ready = false, hello_sent = false;
  std::string yg;              // under YouGame: the connection at the other end, in the arena's own room
};

template <class T> void drop_later(std::unique_ptr<T>& p) {
  if (p) std::thread([q = std::move(p)]() mutable { q.reset(); }).detach();   // their threads join off this one
}

struct Test { bool on = false; int index = 0, count = 0, base = 0; uint8_t character = 2; } g_test;

// Simulation thread.
bool g_active = false, g_hosting = false;
std::string g_notice;            // a line for the player beside the room tab (a full room), until g_notice_until
uint64_t g_notice_until = 0;
bool g_close_room = false;       // a full room refused this player: leave it at the next tick
bool g_member_closed = false;    // the host said the room is over
const char* g_closed_reason = nullptr;   // a member's room ended (host gone): closed after the links are read
std::string g_code, g_name;
uint8_t g_self = kNone;
Room g_room;
uint32_t g_sent_rev = 0xFFFFFFFFu;
std::vector<Link> g_links;                 // host: one per member; member: the one to the host
std::vector<uint8_t> g_codes;
std::function<void*(const std::string&)> g_launch_viewer;
feed::Sender g_sender;
bool g_sending = false;                    // this game's recording goes out as the feed
feed::Receiver g_receiver;
uint32_t g_receiver_tag = 0;
bool g_viewer_open_for_tag = false;
HANDLE g_viewer = nullptr;
uint32_t g_ended_tag = 0, g_started_tag = 0;   // the host's: reports already applied
uint32_t g_my_tag = 0;                    // this game's own recording
bool g_my_online = false;                 // and it is an online match
std::string g_my_opp;                     // the opponent it is against (the room may move on first)
bool g_my_doubles = false;                // and it was a doubles game
uint8_t g_port_team[4] = {0, 0, 0, 0};    // each port's team in it (Game Start)
std::vector<uint8_t> g_table;             // its command table event, for a sender made at Game Start
bool g_wait_room = false;                 // a member's game ended: wait for the room to say what is next
void* g_yield_to = nullptr;               // this room joins the room at the other end of that link
uint64_t g_next_match_ms = 0, g_next_pings_ms = 0;
std::atomic<bool> g_search_busy{false};
std::atomic<uint64_t> g_room_over_ms{0};   // when this game was last put out of a room (0 never)
std::atomic<uint16_t> g_css_pick{0xFF00};   // character << 8 | costume, kNone character: none
uint64_t g_test_wait_since = 0;
// Under YouGame the room is the arena's own YouGame room (yougame::arena_room) and every link is a
// connection in it; there are no Slippi links, searches or codes. Simulation thread.
bool g_yg = false;
bool g_yg_seen_host = false;      // a member: the host was in the room's roster (gone after that: closed)
bool g_yg_invite = false;         // the host asked to invite while the room was still opening
int g_yg_invite_seen = 0;         // the invite's last state shown
uint64_t g_yg_close_at = 0;       // a room left: closed once the goodbye has gone out
// Warm-up room: this member is in Training; the viewer window waits for the way back (unless the
// setting keeps it open). The game's score from the first fighter (kScore), for the room's strip.
bool g_warming = false;
LiveScore g_score;
uint32_t g_score_tag = 0;
uint16_t g_warming_mask = 0;      // who in the room is warming up (the host's word), a bit per member id
uint16_t g_warming_sent = 0xFFFF; // the host: the mask everyone last got
// The sender's side: the fighters' stocks and percent per recent frame, sent once a frame is final.
struct ScoreFrame { int32_t frame = INT32_MIN; uint8_t present = 0, stocks[4] = {}; float percent[4] = {}; };
ScoreFrame g_score_ring[32];
uint8_t g_score_char[4] = {0xFF, 0xFF, 0xFF, 0xFF}, g_score_costume[4] = {};
std::string g_score_name[4];   // each port's name from the Game Start (display name, else the code's tag)

// A Game Start name field (Shift-JIS, NUL padded) as plain text: ASCII kept, the full-width letters,
// digits and '#' turned into ASCII, anything else left out.
std::string sjis_ascii(const uint8_t* p, size_t n) {
  std::string out;
  for (size_t i = 0; i < n && p[i]; ++i) {
    const uint8_t c = p[i];
    if (c < 0x80) { if (c >= 0x20) out.push_back((char)c); continue; }
    if (i + 1 >= n) break;
    const unsigned w = (unsigned)(c << 8) | p[i + 1];
    ++i;
    if (w >= 0x824F && w <= 0x8258) out.push_back((char)('0' + (w - 0x824F)));
    else if (w >= 0x8260 && w <= 0x8279) out.push_back((char)('A' + (w - 0x8260)));
    else if (w >= 0x8281 && w <= 0x829A) out.push_back((char)('a' + (w - 0x8281)));
    else if (w == 0x8194) out.push_back('#');
    else if (w == 0x8140) out.push_back(' ');
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}
int32_t g_score_sent_frame = INT32_MIN;
ScoreFrame g_score_sent;   // the values last sent: a change goes out at once (stocks or whole percent)

// The room's match window (host::options.spectate): it simulates the game itself, so the stocks and
// percent it shows exist only here. Each frame's are sent to the game that opened it (the warm-up
// panel shows exactly them): at once on a change, else twice a second.
void viewer_score_event(const uint8_t* data, uint32_t size) {
  static uint8_t present = 0, stocks[4] = {};
  static uint16_t percent[4] = {};
  static uint64_t sent = ~0ull;
  static int32_t sent_frame = INT32_MIN;
  const auto get32 = [](const uint8_t* q) { return (uint32_t)q[0] << 24 | (uint32_t)q[1] << 16 | (uint32_t)q[2] << 8 | q[3]; };
  const uint8_t* p = data + 1;
  const uint32_t n = size - 1;
  if (data[0] == 0x36) {
    present = 0;
    for (int i = 0; i < 4; ++i) {
      const size_t at = 4 + 0x60 + 0x24 * (size_t)i;
      if (at + 1 < n && p[at + 1] != 3) present |= (uint8_t)(1u << i);
      stocks[i] = 0;
      percent[i] = 0;
    }
    sent = ~0ull;
    sent_frame = INT32_MIN;
  } else if (data[0] == 0x38 && n >= 0x21) {
    const uint8_t port = p[4];
    if (port > 3 || p[5]) return;   // a follower (Nana) is not on the score
    uint32_t bits = get32(p + 0x15);
    float pct;
    std::memcpy(&pct, &bits, 4);
    stocks[port] = p[0x20];
    percent[port] = (uint16_t)std::clamp((int)std::floor(pct), 0, 999);
  } else if (data[0] == 0x3C && n >= 4) {
    const int32_t frame = (int32_t)get32(p);
    uint64_t packed = 0;
    for (int i = 0; i < 4; ++i) {
      const uint64_t v = (present >> i) & 1 ? ((uint64_t)std::min<uint8_t>(stocks[i], 14) << 10 | percent[i]) : (15ull << 10);
      packed |= v << (14 * i);
    }
    if (packed != sent || frame >= sent_frame + 30 || frame < sent_frame) {
      if (((packed ^ sent) & 0x3C003C003C003C00ull) != 0)   // a stock changed (the 4 stock bits of each port)
        host::log("spectate: stocks shown at frame %d: %u %u %u %u", frame, (unsigned)(packed >> 10 & 15), (unsigned)(packed >> 24 & 15),
                  (unsigned)(packed >> 38 & 15), (unsigned)(packed >> 52 & 15));
      host::window_report_score(packed);
      sent = packed;
      sent_frame = frame;
    }
  }
}
// The game being passed on to the room, and who was in the ring when it began. The room moves on
// at its Game End report, which can arrive before the feed's last messages: those still go to the
// members who were watching that game, not to whoever is watching now.
uint32_t g_relay_tag = 0;
uint8_t g_relay_from = kNone;
std::array<uint8_t, kRingMax> g_relay_ring{kNone, kNone, kNone, kNone};
std::array<uint8_t, kRingMax> g_send_ring{kNone, kNone, kNone, kNone};   // this member's own game, as it began
bool in_ring_of(const std::array<uint8_t, kRingMax>& ring, uint8_t id) {
  for (uint8_t r : ring) if (r == id) return true;
  return false;
}            // test rooms: when this member began waiting for its viewer
std::string g_pair_code;                  // the opponent this game last searched for
uint32_t g_pair_games = 0;                // the room's finished games when that pairing was counted
std::array<int, kMaxMembers> g_ping{};
std::string g_status;
bool g_live = false;

// UI thread hand-off.
std::mutex g_mutex;
View g_view;
struct Pending { Act a; uint8_t arg; std::string text; };
std::deque<Pending> g_pending;
std::deque<std::string> g_events;

uint64_t now_ms() { return GetTickCount64(); }

void event(const char* fmt, ...) {
  char line[300];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof line, fmt, args);
  va_end(args);
  host::log("arena: %s", line);
  std::lock_guard<std::mutex> lk(g_mutex);
  g_events.push_back(line);
  while (g_events.size() > 64) g_events.pop_front();
}

void put_str(std::vector<uint8_t>& o, const std::string& s) {
  const size_t n = std::min<size_t>(s.size(), 64);
  o.push_back((uint8_t)n);
  o.insert(o.end(), s.begin(), s.begin() + n);
}
bool get_str(const std::vector<uint8_t>& m, size_t& at, std::string& out) {
  if (at >= m.size()) return false;
  const size_t n = m[at++];
  if (at + n > m.size()) return false;
  out.assign((const char*)m.data() + at, n);
  at += n;
  return true;
}
void put32(std::vector<uint8_t>& o, uint32_t v) { for (int s = 24; s >= 0; s -= 8) o.push_back((uint8_t)(v >> s)); }
uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

bool send(Link& link, const std::vector<uint8_t>& m, bool bulk = false) {
  if (!link.yg.empty()) {   // the arena's YouGame room carries it (sent in batches within the room's limits)
    yougame::arena_room::send(link.yg, m);
    return true;
  }
  return link.np && link.np->SendArena(m.data(), m.size(), bulk);
}

Link* link_of(uint8_t id) {
  for (Link& l : g_links) if (l.id == id) return &l;
  return nullptr;
}

// ---- links ----
std::unique_ptr<NetplayClient> test_link(int peer) {
  if (!enet_ready()) return nullptr;
  const bool host_side = g_test.index == 0;
  const int member = host_side ? peer : g_test.index;
  const uint16_t mine = (uint16_t)(g_test.base + (host_side ? 100 : 200) + member);
  const uint16_t theirs = (uint16_t)(g_test.base + (host_side ? 200 : 100) + member);
  return std::make_unique<NetplayClient>(std::vector<std::string>{"127.0.0.1"}, std::vector<uint16_t>{theirs}, 1, mine,
                                         host_side, (uint8_t)(host_side ? 0 : 1));
}

void open_links() {
  g_links.clear();
  if (!g_test.on) return;
  if (g_test.index == 0) {
    for (int i = 1; i < g_test.count; ++i) { Link l; l.test_peer = i; g_links.push_back(std::move(l)); }
  } else {
    Link l; l.test_peer = 0; g_links.push_back(std::move(l));
  }
}

// ---- the room (host side) ----
void broadcast_warming() {
  if (!g_hosting) return;
  // Members who left are nobody's business any more.
  uint16_t present = 0;
  for (const Member& mm : g_room.members) if (mm.id < 16) present |= (uint16_t)(1u << mm.id);
  g_warming_mask &= present;
  if (g_warming_mask == g_warming_sent) return;
  if (g_warming_sent != 0xFFFF || g_warming_mask) event("warming up in Training: mask %04X (sent to the room)", g_warming_mask);
  g_warming_sent = g_warming_mask;
  const std::vector<uint8_t> m = {kWarming, (uint8_t)(g_warming_mask >> 8), (uint8_t)g_warming_mask};
  for (Link& l : g_links) if (l.id != kNone) send(l, m);
}

void broadcast_room() {
  if (g_hosting && g_room.rev != g_sent_rev) g_warming_sent = 0xFFFF;   // with every room change (late joiners)
  broadcast_warming();
  if (!g_hosting || g_room.rev == g_sent_rev) return;
  g_sent_rev = g_room.rev;
  std::vector<uint8_t> m = {kRoomState};
  const std::vector<uint8_t> bytes = encode(g_room);
  m.insert(m.end(), bytes.begin(), bytes.end());
  for (Link& l : g_links) if (l.id != kNone) send(l, m);
  if (g_room.doubles)
    event("room rev %u: doubles %s, ring %d %d vs %d %d, line %zu, games %u", g_room.rev,
          g_room.mode == Mode::WinnerStays ? "winner stays" : g_room.mode == Mode::TakeTurns ? "take turns" : "ask",
          (int)g_room.ring[0], (int)g_room.ring[1], (int)g_room.ring[2], (int)g_room.ring[3], g_room.line.size(), g_room.games);
  else
    event("room rev %u: %s, ring %d %d, line %zu, games %u", g_room.rev,
          g_room.mode == Mode::WinnerStays ? "winner stays" : g_room.mode == Mode::TakeTurns ? "take turns" : "ask",
          (int)g_room.ring[0], (int)g_room.ring[1], g_room.line.size(), g_room.games);
}

bool apply_action(uint8_t who, Act a, uint8_t arg, uint8_t extra = 0) {
  switch (a) {
    case Act::SetCharacter: return set_character(g_room, who, arg, extra);
    case Act::ToggleLine: return toggle_line(g_room, who);
    case Act::Ask: return ask(g_room, who);
    case Act::Answer: return answer(g_room, who, (Answer)arg);
    case Act::GiveUp: return give_up(g_room, who, arg != 0);
    case Act::SetMode: return set_mode(g_room, (Mode)arg);
    case Act::ProposeSwitch: return propose_switch(g_room, who);
    case Act::VoteSwitch: return vote_switch(g_room, who, arg != 0);
    case Act::Leave: {
      if (g_room.in_game && ring_slot(g_room, who) >= 0) game_end(g_room, kNone);
      return leave(g_room, who);
    }
    default: return false;
  }
}

void member_gone(uint8_t id) {
  if (!find(g_room, id)) return;
  event("member %u left", id);
  apply_action(id, Act::Leave, 0);
}

// A fighter's report of its game: the first report of each game counts.
void apply_report(uint8_t what, uint32_t tag, const std::string& winner_code) {
  if (what == kStarted) {
    if (tag == g_ended_tag || tag == g_started_tag) return;
    g_started_tag = tag;
    if (game_start(g_room)) event("game %08X started", tag);
    return;
  }
  if (what != kEnded || tag == g_ended_tag) return;
  g_ended_tag = tag;
  if (!g_room.in_game) game_start(g_room);   // the start report was lost or late
  uint8_t team = kNone;
  for (const Member& m : g_room.members)
    if (!winner_code.empty() && m.code == winner_code) {
      const int slot = ring_slot(g_room, m.id);
      if (slot >= 0) team = (uint8_t)team_of_slot(g_room, slot);
    }
  game_end(g_room, team);
  event("game %08X ended, winner %s", tag, winner_code.empty() ? "none" : winner_code.c_str());
}

void say_room_full(bool theirs) {
  g_notice = std::string(theirs ? "That" : "The") + " room is full (" + std::to_string(room_limit()) + " players at most)";
  g_notice_until = now_ms() + 10000;
}

// kScore: tag, frame, then per port: present, character (external), costume, stocks, percent x 10.
void take_score(const std::vector<uint8_t>& m) {
  if (m.size() < 9 + 4 * 6) return;
  LiveScore s;
  s.live = true;
  s.frame = get32(&m[5]);
  for (int i = 0; i < 4; ++i) {
    const size_t at = 9 + 6 * (size_t)i;
    auto& f = s.fighter[i];
    f.present = m[at] != 0;
    f.character = m[at + 1];
    f.costume = m[at + 2];
    f.stocks = m[at + 3];
    f.percent = (float)((m[at + 4] << 8) | m[at + 5]) / 10.0f;
  }
  for (size_t at = 9 + 6 * 4, i = 0; i < 4 && at < m.size(); ++i) {   // names, from builds that send them
    const size_t len = std::min<size_t>(m[at], 15);
    if (at + 1 + len > m.size()) break;
    s.fighter[i].name.assign(m.begin() + (long)at + 1, m.begin() + (long)(at + 1 + len));
    at += 1 + len;
  }
  const uint32_t tag = get32(&m[1]);
  if (tag != g_score_tag) {
    int shown = 0;
    for (const auto& f : s.fighter) shown += f.present;
    event("live score for game %08X: %d fighters, frame %u", tag, shown, s.frame);
  }
  g_score = s;
  g_score_tag = tag;
}

void host_message(Link& from, const std::vector<uint8_t>& m) {
  if (m.empty()) return;
  switch (m[0]) {
    case kHello: {
      size_t at = 1;
      std::string code, name;
      if (!get_str(m, at, code) || !get_str(m, at, name) || from.id != kNone) return;
      const bool they_host = at + 2 <= m.size() && m[at] != 0;
      const size_t their_count = at + 2 <= m.size() ? m[at + 1] : 1;
      bool full = g_room.members.size() >= (size_t)room_limit();
      if (they_host && !g_test.on) {
        // Two rooms met: the bigger one keeps its host; between equals, the lower connect code hosts.
        // Not when both together would pass the limit: then neither moves (a host that joined a full
        // room would leave its own players behind) and the asking room is told it does not fit.
        const size_t mine = g_room.members.size();
        if (mine + their_count > (size_t)room_limit()) full = true;
        else if (their_count > mine || (their_count == mine && code < g_code)) {
          g_yield_to = (void*)&from;   // done once this link's messages are read (the list may not change mid-loop)
          return;
        }
      }
      const uint8_t id = full ? kNone : join(g_room, code, name);
      from.id = id;
      std::vector<uint8_t> w = {kWelcome, id};
      if (full) w.push_back(1);   // why: the room is full (older members ignore the extra byte)
      send(from, w);
      g_sent_rev = 0xFFFFFFFFu;   // everyone gets the room with the new member
      if (full) {
        event("refused %s (the room is full: %d players)", code.c_str(), room_limit());
        from.refused_at = now_ms();
        if (!g_test.on) say_room_full(they_host && their_count > g_room.members.size());   // the smaller side: "That room"
      } else {
        event(id == kNone ? "refused %s (already in the room)" : "%s joined as %u", code.c_str(), id);
      }
      return;
    }
    case kWelcome:
      // This game was hosting a room of its own when it asked to join another (two rooms met), and
      // that room had no place for it: say so; a room of just this player is closed, a bigger one
      // stays as it was.
      if (m.size() >= 3 && m[1] == kNone && m[2] == 1 && !g_test.on) {
        event("the other room has no place for this one (%d players at most): not joining it", room_limit());
        from.refused_at = now_ms();
        if (g_room.members.size() <= 1) { say_room_full(true); g_close_room = true; }
      }
      return;
    case kAction:
      if (from.id != kNone && m.size() >= 3) {
        const uint8_t who = from.id;
        apply_action(who, (Act)m[1], m[2], m.size() >= 4 ? m[3] : 0);
        if ((Act)m[1] == Act::Leave) {
          // Left on purpose: the link closes and that friend is not searched for again (their next
          // Direct search for this player's code is a plain one).
          event("member %u left the room", who);
          from.id = kNone;
          from.refused_at = now_ms();
        }
      }
      return;
    case kReport: {
      // Only the players of the game report it.
      if (m.size() < 6 || from.id == kNone || ring_slot(g_room, from.id) < 0) return;
      size_t at = 6;
      std::string winner;
      get_str(m, at, winner);
      apply_report(m[1], get32(&m[2]), winner);
      return;
    }
    case kFeed: {
      // A game starts from the first player only; the rest of it comes from the same player under
      // the same tag even after the room has moved on. Passed on to everyone who was not playing
      // in it, and kept here when the host was watching.
      if (from.id == kNone || m.size() < 6) return;
      const uint32_t tag = get32(&m[2]);
      if (m[1] == feed::kStart && from.id == g_room.ring[0]) {
        g_relay_tag = tag;
        g_relay_from = from.id;
        std::copy(std::begin(g_room.ring), std::end(g_room.ring), g_relay_ring.begin());
      } else if (from.id != g_relay_from || tag != g_relay_tag) {
        return;
      }
      for (Link& l : g_links)
        if (&l != &from && l.id != kNone && !in_ring_of(g_relay_ring, l.id)) send(l, m, true);
      if (!in_ring_of(g_relay_ring, g_self)) watch_feed(m.data() + 1, m.size() - 1);
      return;
    }
    case kWarmup:
      if (from.id != kNone && from.id < 16 && m.size() >= 2) {
        const uint16_t bit = (uint16_t)(1u << from.id);
        g_warming_mask = m[1] ? (uint16_t)(g_warming_mask | bit) : (uint16_t)(g_warming_mask & ~bit);
      }
      return;
    case kScore: {
      // The first fighter's score of the game it is sending: passed on like the feed.
      if (from.id == kNone || m.size() < 5 || from.id != g_relay_from || get32(&m[1]) != g_relay_tag) return;
      for (Link& l : g_links)
        if (&l != &from && l.id != kNone && !in_ring_of(g_relay_ring, l.id)) send(l, m);
      if (!in_ring_of(g_relay_ring, g_self)) take_score(m);
      return;
    }
    default:
      return;
  }
}

// ---- watching (any member who is not fighting) ----
bool viewer_running() {
  DWORD code = 0;
  return g_viewer && GetExitCodeProcess(g_viewer, &code) && code == STILL_ACTIVE;
}

void close_viewer() {
  if (!g_viewer) return;
  DWORD code = 0;
  if (GetExitCodeProcess(g_viewer, &code) && code == STILL_ACTIVE) TerminateProcess(g_viewer, 0);   // the window we opened
  CloseHandle(g_viewer);
  g_viewer = nullptr;
}

// ---- the warm-up room's two windows (RenderOptions::live_warmup_watch) ----
// Warming up in a room with the room's match on: 1 the match's viewer window small on top of this
// game (its sound off), 2 the viewer filling the area this game had and this game small on top (its
// sound off). Size and corner from the settings (the Practice menu changes them too); the small
// window can be dragged and sized with the mouse as well, which is left alone until the settings
// change again. Both windows go back exactly as they were when the warm-up ends.
struct Pip {
  int applied = 0;
  int size = -1, corner = -1;
  void* viewer = nullptr;   // the viewer's game window the layout is on
  long area[4] = {0, 0, 0, 0};   // this game's window when the layout began (screen: left, top, right, bottom)
  long client[4] = {0, 0, 0, 0}; // its inside (the picture and its bars), on the screen
  uint64_t next_find_ms = 0;
  int viewer_mode = 0;           // what the viewer said it took (1 small, 2 big)
  int asked = 0;                 // what it was last asked, and when
  uint64_t asked_ms = 0;
  float cx = -1, cy = -1, cw = -1;   // the controller's place it was laid out with (live_warmup_pip_*)
  bool adjusting = false;            // picked with the controller (Practice menu, Move and resize)
  bool on_select = false;            // laid out for the room's select (under its side card)
};
Pip g_pip;
std::atomic<int> g_pip_adjust_req{0};   // 1 pick the small window with the controller, 2 let it go
// Watching on the room's character select: the match as the setting says (room_watch: 0 its own
// window, 1 small, 2 big), or as the player asked for this wait (PICTURE IN PICTURE on it, or the
// side bar's button). -1: the setting; the setting again for each new wait.
std::atomic<int> g_watch_layout{-1};
std::atomic<bool> g_watch_open{false};  // the room's match window is up while this player watches
std::atomic<int> g_watch_toggle{0};     // the side bar's button pressed (any thread): 1 toggle
// The right side bar's top card on the select (arena_panel): its bottom as a share of the window's
// height, and when it was said (ms); the small window sits under it there.
std::atomic<float> g_card_bottom{0.0f};
std::atomic<uint64_t> g_card_bottom_ms{0};
std::atomic<bool> g_pip_up{false};      // the two windows are laid out (pip_active)

#ifdef _WIN32
void pip_rect(const long area[4], const long client[4], int size, int corner, long out[4]) {
  // A place picked with the controller: its centre and width as fractions of the area, kept inside it.
  const float cw = gx::RenderOptions::live_warmup_pip_cw().load();
  if (cw > 0.01f) {
    const long aw = area[2] - area[0], ah = area[3] - area[1];
    long w = std::clamp((long)(aw * cw), std::min<long>(120, aw), std::max<long>(120, aw * 9 / 10)), h = w * 3 / 4;
    if (h > ah * 9 / 10) { h = ah * 9 / 10; w = h * 4 / 3; }
    const long x = area[0] + (long)(aw * gx::RenderOptions::live_warmup_pip_cx().load()) - w / 2;
    const long y = area[1] + (long)(ah * gx::RenderOptions::live_warmup_pip_cy().load()) - h / 2;
    out[0] = std::clamp(x, area[0], std::max(area[0], area[2] - w));
    out[1] = std::clamp(y, area[1], std::max(area[1], area[3] - h));
    out[2] = out[0] + w;
    out[3] = out[1] + h;
    return;
  }
  // Corner 4: inside the black bar right of the 4:3 picture, level with the warm-up panel in the bar
  // on the left, so it covers nothing. A bar too narrow for it (true widescreen): the bottom right.
  if (corner == 4) {
    const long cw = client[2] - client[0], ch = client[3] - client[1];
    const long pic = std::min<long>(cw, (long)(ch * std::max(0.5f, host::window_picture_aspect())));
    const long bar = (cw - pic) / 2;
    if (bar >= 110 && ch > 0) {
      const long m = std::max<long>(6, bar / 20);
      long w = bar - 2 * m, h = w * 3 / 4;
      if (h > ch * 6 / 10) { h = ch * 6 / 10; w = h * 4 / 3; }
      out[0] = client[2] - bar + (bar - w) / 2;
      // level with the warm-up panel across the picture: at the top when the panel is flush in its bar;
      // on the room's select (the Arena panel's cards in the bars), under the right bar's top card
      const float card = g_card_bottom.load();
      if (!g_warming && card > 0.0f && now_ms() - g_card_bottom_ms.load() < 1000)
        out[1] = client[1] + (long)(ch * card) + 2 * m;
      else
        out[1] = client[1] + (gx::RenderOptions::live_warmup_panel_style() == 2 ? m : (long)(ch * 0.17f));
      out[2] = out[0] + w;
      out[3] = out[1] + h;
      return;
    }
    corner = 1;
  }
  static const float kWidth[3] = {0.25f, 0.34f, 0.45f};   // of the area's width
  const long aw = area[2] - area[0], ah = area[3] - area[1];
  long w = (long)(aw * kWidth[std::clamp(size, 0, 2)]), h = w * 3 / 4;
  if (h > ah * 6 / 10) { h = ah * 6 / 10; w = h * 4 / 3; }
  const long m = std::max<long>(12, aw / 80);
  const bool left = corner == 2 || corner == 3, bottom = corner == 1 || corner == 2;
  out[0] = left ? area[0] + m : area[2] - m - w;
  out[1] = bottom ? area[3] - m - h : area[1] + m;
  out[2] = out[0] + w;
  out[3] = out[1] + h;
}

void pip_restore(const char* why) {
  if (g_pip.adjusting) {   // no small window to move any more
    host::window_set_selected(false);
    if (g_pip.viewer && IsWindow((HWND)g_pip.viewer)) host::window_send_selected(g_pip.viewer, false);
    g_pip.adjusting = false;
  }
  if (!g_pip.applied) { g_pip.viewer = nullptr; return; }
  if (g_pip.applied == 2) host::window_set_pip(0, false);
  if (g_pip.viewer && IsWindow((HWND)g_pip.viewer)) host::window_send_pip(g_pip.viewer, 0, false);
  event("the two windows are back as they were (%s)", why);
  g_pip_up.store(false);
  g_pip = Pip{};
}

void pip_tick() {
  // The match window's buttons, and the side bar's: warm up (from the big window), small, full size.
  const int action = host::window_spectate_action();
  if (action == 1) {
    event("WARM UP pressed on the match window");
    native_practice::submit_warmup();
  }
  // On the select: the setting, or this wait's own choice. "Full size" there is what the setting calls
  // it: the match's own window (0, the default), or big with this game small (2).
  const int room = std::clamp(gx::RenderOptions::live_room_watch(), 0, 2);
  const int full = g_warming ? 2 : room == 2 ? 2 : 0;
  int on_select_layout = g_watch_layout.load();
  if (on_select_layout < 0) on_select_layout = room;
  if (action == 2 || action == 3 || g_watch_toggle.exchange(0)) {
    const int now = g_warming ? gx::RenderOptions::live_warmup_watch() : on_select_layout;
    const int to = action == 2 ? 1 : action == 3 ? full : now == 1 ? full : 1;
    // Away (warming up, another mode) the setting says how it shows, so the choice is the setting's;
    // on the select it is this wait's own.
    if (g_warming) gx::RenderOptions::live_warmup_watch() = to;
    else { g_watch_layout.store(to); on_select_layout = to; }
    event("the room's match %s (%s)", to == 1 ? "small: picture in picture" : to == 2 ? "full size" : "in its own window",
          g_warming ? "while away" : "on the select");
  }
  // Called for the turn: the windows go back at once, so the countdown shows full size; and the next
  // wait starts big again.
  const int setting = std::clamp(gx::RenderOptions::live_warmup_watch(), 0, 2);
  if (turn_called()) g_watch_layout.store(-1);
  g_watch_open.store(viewer_running() && !turn_called() && setting != 0);
  if (!setting && viewer_running()) {   // turned to "Not shown": the window goes (it opens again when turned back on)
    pip_restore("the match window is off");
    close_viewer();
    g_viewer_open_for_tag = false;
    event("viewer closed: the room's match is not shown");
    return;
  }
  const int want = viewer_running() && !turn_called() && setting ? (g_warming ? setting : on_select_layout) : 0;
  if (!want) {
    pip_restore(!viewer_running() ? "no match window now" : turn_called() ? "this player's turn"
                : setting ? "the match in its own window" : "the match window is off");
    return;
  }
  // The viewer's game window, found by its process (it takes a moment to open). A new one (the
  // room's next game) gets the layout afresh.
  if (!g_pip.viewer || !IsWindow((HWND)g_pip.viewer)) {
    if (now_ms() < g_pip.next_find_ms) return;
    g_pip.next_find_ms = now_ms() + 250;
    void* found = host::window_of_process(GetProcessId(g_viewer));
    if (!found) return;
    if (g_pip.applied == 2) host::window_set_pip(0, false);   // this game full again until it is laid out anew
    if (g_pip.applied) event("a new match window: laying it out again");
    const bool had_area = g_pip.applied != 0;
    long area[4], client[4];
    std::copy(std::begin(g_pip.area), std::end(g_pip.area), area);
    std::copy(std::begin(g_pip.client), std::end(g_pip.client), client);
    g_pip = Pip{};
    if (had_area) {
      std::copy(std::begin(area), std::end(area), g_pip.area);
      std::copy(std::begin(client), std::end(client), g_pip.client);
    }
    g_pip.viewer = found;
  }
  // Test: MELEE_TEST_PIP_ADJUST=<seconds>: that long after the first layout, the controller picks the
  // small window, moves it to the left middle at a third of the width, and puts it down 5 seconds later.
  if (g_pip.applied) {
    static const char* t = host::test_env::getenv("MELEE_TEST_PIP_ADJUST");
    static uint64_t first = 0;
    static int step = 0;
    if (t && step < 3) {
      if (!first) first = now_ms();
      const uint64_t at = first + (uint64_t)std::atoi(t) * 1000;
      if (step == 0 && now_ms() >= at) { g_pip_adjust_req.store(1); step = 1; event("test: pick the small window"); }
      else if (step == 1 && now_ms() >= at + 1500) {
        gx::RenderOptions::live_warmup_pip_cx().store(0.22f);
        gx::RenderOptions::live_warmup_pip_cy().store(0.55f);
        gx::RenderOptions::live_warmup_pip_cw().store(0.33f);
        step = 2;
      } else if (step == 2 && now_ms() >= at + 6000) { g_pip_adjust_req.store(2); step = 3; }
    }
  }
  // The controller picks the small window (Practice menu, Move and resize): where it is now (the mouse
  // may have moved it) becomes the place it is then moved from, and it shows it is picked.
  int req = g_pip_adjust_req.exchange(0);
  if (req == 1 && !g_pip.applied) { g_pip_adjust_req.store(1); req = 0; }   // the small window is not up yet: pick it once it is
  if (req == 1 && g_pip.applied && !g_pip.adjusting) {
    const HWND little = (HWND)(g_pip.applied == 1 ? g_pip.viewer : host::window_native());
    RECT r{};
    const long aw = g_pip.area[2] - g_pip.area[0], ah = g_pip.area[3] - g_pip.area[1];
    if (little && aw > 0 && ah > 0 && GetWindowRect(little, &r)) {
      gx::RenderOptions::live_warmup_pip_cx().store(((r.left + r.right) * 0.5f - g_pip.area[0]) / aw);
      gx::RenderOptions::live_warmup_pip_cy().store(((r.top + r.bottom) * 0.5f - g_pip.area[1]) / ah);
      gx::RenderOptions::live_warmup_pip_cw().store((float)(r.right - r.left) / aw);
    }
    if (g_pip.applied == 1) host::window_send_selected(g_pip.viewer, true);
    else host::window_set_selected(true);
    g_pip.adjusting = true;
    event("the small window is picked with the controller");
  } else if (req == 2 && g_pip.adjusting) {
    if (g_pip.applied == 1) host::window_send_selected(g_pip.viewer, false);
    else host::window_set_selected(false);
    g_pip.adjusting = false;
    event("the small window is put down: %.0f%% wide at %.0f%%, %.0f%% of the area", gx::RenderOptions::live_warmup_pip_cw().load() * 100,
          gx::RenderOptions::live_warmup_pip_cx().load() * 100, gx::RenderOptions::live_warmup_pip_cy().load() * 100);
  }
  const int size = gx::RenderOptions::live_warmup_pip_size(), corner = gx::RenderOptions::live_warmup_pip_corner();
  const float cx = gx::RenderOptions::live_warmup_pip_cx().load(), cy = gx::RenderOptions::live_warmup_pip_cy().load(),
              cw = gx::RenderOptions::live_warmup_pip_cw().load();
  // Where corner 4 puts it moves between the select (under the side card) and away: laid out again then.
  const bool on_select = !g_warming && g_card_bottom.load() > 0.0f && now_ms() - g_card_bottom_ms.load() < 1000;
  if (want == g_pip.applied && size == g_pip.size && corner == g_pip.corner && cx == g_pip.cx && cy == g_pip.cy && cw == g_pip.cw &&
      on_select == g_pip.on_select)
    return;
  g_pip.on_select = on_select;
  const HWND main = (HWND)host::window_native(), viewer = (HWND)g_pip.viewer;
  if (!main) return;
  if (!g_pip.area[2]) {   // where this game's window is now: the area both layouts share
    RECT r{};
    GetWindowRect(main, &r);
    g_pip.area[0] = r.left; g_pip.area[1] = r.top; g_pip.area[2] = r.right; g_pip.area[3] = r.bottom;
    RECT c{};
    GetClientRect(main, &c);
    POINT tl{c.left, c.top}, br{c.right, c.bottom};
    ClientToScreen(main, &tl);
    ClientToScreen(main, &br);
    g_pip.client[0] = tl.x; g_pip.client[1] = tl.y; g_pip.client[2] = br.x; g_pip.client[3] = br.y;
  }
  // First the viewer takes its new state (it saves where it was), answering when done; only then is
  // anything moved. A viewer still loading answers late: asked again each second until it does.
  if (g_pip.viewer_mode != want) {
    if (g_pip.asked != want || now_ms() - g_pip.asked_ms > 1000) {
      host::window_send_pip(viewer, want, want == 1);
      g_pip.asked = want;
      g_pip.asked_ms = now_ms();
      return;
    }
    if (host::window_pip_answer() != want) return;
    g_pip.viewer_mode = want;
  }
  long box[4];
  pip_rect(g_pip.area, g_pip.client, size, corner, box);
  const long* a = g_pip.area;
  if (want == 1) {
    if (g_pip.applied == 2) host::window_set_pip(0, false);
    SetWindowPos(viewer, HWND_TOPMOST, box[0], box[1], box[2] - box[0], box[3] - box[1], SWP_NOACTIVATE);
  } else {
    SetWindowPos(viewer, HWND_NOTOPMOST, a[0], a[1], a[2] - a[0], a[3] - a[1], SWP_NOACTIVATE);
    if (g_pip.applied != 2) host::window_set_pip(1, true);
    SetWindowPos(main, HWND_TOPMOST, box[0], box[1], box[2] - box[0], box[3] - box[1], SWP_NOACTIVATE);
  }
  if (!g_pip.adjusting || want != g_pip.applied)   // not every step while the controller moves it
    event("the room's match %s: small window %ldx%ld at %ld,%ld in %ldx%ld (inside %ldx%ld at %ld,%ld, picture %.3f, %s)",
          want == 1 ? "small over this game" : "big, this game small", box[2] - box[0], box[3] - box[1], box[0] - a[0], box[1] - a[1],
          a[2] - a[0], a[3] - a[1], g_pip.client[2] - g_pip.client[0], g_pip.client[3] - g_pip.client[1], g_pip.client[0] - a[0],
          g_pip.client[1] - a[1], host::window_picture_aspect(), on_select ? "under the side card" : "level with the panel");
  g_pip.applied = want;
  g_pip_up.store(true);
  g_pip.size = size;
  g_pip.corner = corner;
  g_pip.cx = cx;
  g_pip.cy = cy;
  g_pip.cw = cw;
}
// The match window's view (RenderOptions::live_spectate_view): a change here (the settings, the
// Practice menu) goes to the window; a choice made at its corner comes back and is kept here.
void view_tick() {
  static void* window = nullptr;
  static uint64_t next_find_ms = 0;
  static int sent = -1;
  if (!viewer_running()) { window = nullptr; sent = -1; return; }
  if (!window || !IsWindow((HWND)window)) {
    window = nullptr;
    if (now_ms() < next_find_ms) return;
    next_find_ms = now_ms() + 250;
    window = host::window_of_process(GetProcessId(g_viewer));
    sent = gx::RenderOptions::live_spectate_view();   // it started with this (--spectate-view)
    if (!window) return;
  }
  // Test: MELEE_TEST_ARENA_VIEW=<view>@<seconds>: this game switches the match window's view once.
  static uint64_t found_ms = 0;
  if (!found_ms) found_ms = now_ms();
  static bool test_done = false;
  if (!test_done) {
    if (const char* t = host::test_env::getenv("MELEE_TEST_ARENA_VIEW")) {
      int view = 0, secs = 0;
      if (std::sscanf(t, "%d@%d", &view, &secs) == 2 && now_ms() >= found_ms + (uint64_t)secs * 1000) {
        gx::RenderOptions::live_spectate_view() = std::clamp(view, 0, 2);
        test_done = true;
        event("test: match view %d", view);
      }
    } else test_done = true;
  }
  const int chosen = host::window_spectate_choice();
  if (chosen >= 0 && chosen <= 2) {
    gx::RenderOptions::live_spectate_view() = chosen;
    sent = chosen;
    event("match view %d chosen in its window", chosen);
  }
  const int want = std::clamp(gx::RenderOptions::live_spectate_view(), 0, 2);
  if (want != sent) {
    host::window_send_spectate_view(window, want);
    sent = want;
    event("match view %d sent to its window", want);
  }
}
#else
void pip_restore(const char*) {}
void pip_tick() {}
void view_tick() {}
#endif

std::string live_path(uint32_t tag) {
  std::error_code ec;
  std::filesystem::path dir = std::filesystem::u8path(online::config().user_dir) / "arena";
  std::filesystem::create_directories(dir, ec);
  // Old live recordings of this room's earlier games are removed as the next one starts.
  static const bool keep = [] { const char* v = std::getenv("MELEE_ARENA_KEEP_LIVE"); return v && *v == '1'; }();   // tests
  for (const auto& f : std::filesystem::directory_iterator(dir, ec))
    if (!keep && f.path().filename().u8string().rfind("live-", 0) == 0) std::filesystem::remove(f.path(), ec);
  char name[32];
  std::snprintf(name, sizeof name, "live-%08X.slp", tag);
  return (dir / name).u8string();
}

}  // namespace

void watch_feed(const uint8_t* data, size_t size) {
  if (size < 5) return;
  const uint32_t tag = get32(data + 1);
  if (data[0] == feed::kStart && tag != g_receiver_tag) {
    g_receiver.close();
    close_viewer();
    g_receiver_tag = tag;
    g_viewer_open_for_tag = false;
    if (!g_receiver.open(live_path(tag), g_codes, tag)) { event("cannot write the live recording: %s", g_receiver.error().c_str()); return; }
    event("watching game %08X", tag);
  }
  if (tag != g_receiver_tag) return;
  if (!g_receiver.message(0, data, (uint32_t)size)) event("feed message refused: %s", g_receiver.error().c_str());
  // The viewer opens once the recording has its first frames (it reads the players from them).
  // Tests with many members (MELEE_ARENA_NO_VIEWER=1): the live recording only, no viewer window.
  static const bool no_viewer = [] { const char* v = std::getenv("MELEE_ARENA_NO_VIEWER"); return v && *v == '1'; }();
  // The setting "Not shown": no window at all, the warm-up panel's score only.
  const bool shown = gx::RenderOptions::live_warmup_watch() != 0;
  if (!g_viewer_open_for_tag && g_receiver.stats().frames >= 2 && g_launch_viewer && !no_viewer && shown) {
    g_viewer_open_for_tag = true;
    char name[32];
    std::snprintf(name, sizeof name, "live-%08X.slp", tag);
    const std::string path = (std::filesystem::u8path(online::config().user_dir) / "arena" / name).u8string();
    g_viewer = (HANDLE)g_launch_viewer(path);
    event(g_viewer ? "viewer opened on %s" : "viewer did not open on %s", name);
  }
  if (g_receiver.ended()) {
    const auto& s = g_receiver.stats();
    event("game %08X over: %u frames written, %u duplicates, %u gaps", tag, s.frames, s.duplicates, s.gaps);
  }
}

namespace {

void member_message(const std::vector<uint8_t>& m) {
  if (m.empty()) return;
  switch (m[0]) {
    case kWelcome:
      if (m.size() >= 2) {
        g_self = m[1];
        event(g_self == kNone ? "the room refused this member" : "in the room as member %u", g_self);
        if (g_self == kNone && m.size() >= 3 && m[2] == 1 && !g_test.on) {
          // A full room: this player is not in it. Say so beside the room tab for a while.
          say_room_full(true);
          g_close_room = true;
        }
      }
      return;
    case kRoomState: {
      Room r;
      if (!decode(m.data() + 1, m.size() - 1, r)) { event("room message refused"); return; }
      if (r.rev != g_room.rev) {
        g_room = r;
        if (!r.in_game) g_wait_room = false;
        event("room rev %u: ring %d %d, line %zu, games %u%s", r.rev, (int)r.ring[0], (int)r.ring[1], r.line.size(), r.games,
              ring_slot(r, g_self) >= 0 ? " (fighting)" : "");
      }
      return;
    }
    case kPings:
      for (size_t k = 0; m.size() >= 2 && k < m[1] && 2 + 3 * k + 3 <= m.size(); ++k) {
        const size_t at = 2 + 3 * k;
        if (m[at] < kMaxMembers) g_ping[m[at]] = (int16_t)(m[at + 1] << 8 | m[at + 2]);
      }
      return;
    case kFeed:
      watch_feed(m.data() + 1, m.size() - 1);
      return;
    case kScore:
      take_score(m);
      return;
    case kWarming:
      if (m.size() >= 3) {
        const uint16_t mask = (uint16_t)(m[1] << 8 | m[2]);
        if (mask != g_warming_mask) event("warming up in Training: mask %04X", mask);
        g_warming_mask = mask;
      }
      return;
    case kClosed:
      g_member_closed = true;   // handled after the links are read (this closes them)
      return;
    default:
      return;
  }
}

void send_to_host(const std::vector<uint8_t>& m, bool bulk = false) {
  if (!g_hosting && !g_links.empty()) send(g_links[0], m, bulk);
}

void open_room() {
  g_active = true;
  g_hosting = true;
  g_room = Room{};
  g_self = join(g_room, g_code, g_name);
  g_ping.fill(-1);
  event("room opened by %s", g_code.c_str());
}

// Links of a room this game has left: kept up a moment so the goodbye gets out, then closed.
std::vector<std::pair<uint64_t, Link>> g_closing;

void close_links_soon() {
  const uint64_t at = now_ms() + 800;
  for (Link& l : g_links) {
    drop_later(l.mm);
    if (l.np) g_closing.emplace_back(at, std::move(l));
  }
  g_links.clear();
}

// This game is in no room any more: links closed, its viewer window closed, the search the room
// started on the online character select ended (a player who left is free to play anyone).
void room_over_here(const char* why, bool end_search) {
  g_room_over_ms.store(now_ms() | 1);
  close_links_soon();
  close_viewer();
  g_receiver.close();
  g_viewer_open_for_tag = false;
  g_sending = false;
  if (end_search && online::in_online_menus()) {
    const online::ArenaMatchStatus st = online::arena_match_status();
    const bool searching = st.state >= (int)Matchmaking::INITIALIZING && st.state <= (int)Matchmaking::CONNECTION_SUCCESS;
    if (searching && !st.in_match && (st.mode == Matchmaking::DIRECT || st.mode == Matchmaking::TEAMS)) online::native_cleanup_match();
  }
  if (g_yg) {
    g_yg = false; g_yg_seen_host = false; g_yg_invite = false;
    g_yg_close_at = now_ms() + 800;
  }
  g_active = false; g_hosting = false; g_room = Room{}; g_self = kNone; g_status.clear();
  g_wait_room = false; g_pair_code.clear(); g_yield_to = nullptr; g_sent_rev = 0xFFFFFFFFu;
  g_next_match_ms = 0; g_test_wait_since = 0; g_relay_tag = 0; g_relay_from = kNone;
  g_ping.fill(-1);
  event("%s", why);
}

// Rules > Leave the room. A member says goodbye to the host (the host takes it as that member
// leaving); a host tells everyone the room is over. Only between games (the panel is on the
// character select).
void leave_room() {
  if (!g_active) return;
  if (g_hosting) {
    for (Link& l : g_links) if (l.id != kNone) send(l, std::vector<uint8_t>{kClosed});
    room_over_here("left the room (it closes for everyone)", true);
  } else {
    if (!g_links.empty()) send(g_links[0], std::vector<uint8_t>{kAction, (uint8_t)Act::Leave, 0});
    room_over_here("left the room", true);
  }
  g_notice = "You left the room";
  g_notice_until = now_ms() + 5000;
}

// Under YouGame: this player's YouGame id and name stand for them in the room (the room's seats carry
// the same id, which the arena's games are found by).
bool yg_identity() {
  const yougame::Identity id = yougame::identity();
  if (!id.ok || id.player_id.empty()) {
    g_notice = id.error.empty() ? "YouGame is not signed in yet" : id.error;
    g_notice_until = now_ms() + 6000;
    return false;
  }
  g_code = id.player_id;
  g_name = id.name.empty() ? std::string("Player") : id.name;
  return true;
}

void yg_open(const std::string& room) {
  g_yg = true;
  g_yg_seen_host = false;
  g_yg_close_at = 0;
  g_links.clear();
  g_ping.fill(-1);
  if (room.empty()) {
    open_room();
  } else {
    g_active = true; g_hosting = false; g_room = Room{}; g_self = kNone;
    event("joining the arena %s", room.c_str());
  }
  yougame::arena_room::open(room);
}

// The room's own acts under YouGame; true when handled.
bool yg_act(const Pending& p) {
  if (p.a == Act::JoinYouGame) {
    if (p.text.empty() || !yg_identity()) return true;
    if (g_active) leave_room();
    yg_open(p.text);
    return true;
  }
  if (p.a == Act::OpenRoom) {
    if (!g_active && yg_identity()) yg_open({});
    return true;
  }
  if (p.a == Act::AddCode) {
    // Adding a friend is the app's invite for this room: a player not in a room opens one first.
    if (!g_active) {
      if (!yg_identity()) return true;
      yg_open({});
    }
    if (!g_hosting) {
      event("only the host invites");
      g_notice = "Only the room's host can invite";
      g_notice_until = now_ms() + 6000;
      return true;
    }
    g_yg_invite = true;
    return true;
  }
  return false;
}

void do_act(const Pending& p) {
  if (yougame::session() && !g_test.on) {
    if (yg_act(p)) return;
    if (!g_yg) return;   // a Slippi room never runs on a YouGame row (the app, or one picked under the launcher)
  }
  if (p.a == Act::OpenRoom) {
    if (!g_active && !g_code.empty()) open_room();
    return;
  }
  if (p.a == Act::LeaveRoom) {
    leave_room();
    return;
  }
  if (p.a == Act::AddCode) {
    // Adding a friend: a player not in a room opens one. Only the host searches; a member's room
    // grows through the host (friends enter the host's code).
    if (g_code.empty() || p.text.empty() || p.text == g_code || g_test.on) return;
    if (!g_active) open_room();
    if (!g_hosting) { event("only the host adds friends"); return; }
    for (const Link& l : g_links) if (l.code == p.text) return;
    for (const Member& m : g_room.members) if (m.code == p.text) return;
    if (g_room.members.size() >= (size_t)room_limit()) {
      // Still searched for, so that friend's game is told why it was not let in (the hello decides).
      event("adding %s to a full room (%d players): they will be told", p.text.c_str(), room_limit());
      say_room_full(false);
    }
    Link l;
    l.code = p.text;
    g_links.push_back(std::move(l));
    event("adding %s", p.text.c_str());
    return;
  }
  if (!g_active) return;
  if (g_hosting) {
    if (apply_action(g_self, p.a, p.arg, p.text.empty() ? 0 : (uint8_t)p.text[0]) && p.a != Act::SetCharacter)
      event("action %d by the host", (int)p.a);
    return;
  }
  std::vector<uint8_t> m = {kAction, (uint8_t)p.a, p.arg};
  if (p.a == Act::SetCharacter) m.push_back(p.text.empty() ? 0 : (uint8_t)p.text[0]);
  send_to_host(m);
}

// ---- links: keep them up, read them ----
void service_links() {
  const uint64_t now = now_ms();
  // A link a full room refused (either side) has said why: dropped, so no search for it goes on.
  const auto refused_done = [now](const Link& l) { return l.refused_at && now - l.refused_at > 3000; };
  for (Link& l : g_links) if (refused_done(l)) { drop_later(l.np); drop_later(l.mm); }
  g_links.erase(std::remove_if(g_links.begin(), g_links.end(), refused_done), g_links.end());
  for (Link& l : g_links) {
    if (l.refused_at) continue;   // said no, or left on purpose: only waiting to be dropped (never searched again)
    if (!l.np && l.test_peer >= 0) {
      if (now < l.retry_at) continue;
      l.np = test_link(l.test_peer);
      l.was_ready = false;
      l.hello_sent = false;
      l.retry_at = now + kRetryMs;
      continue;
    }
    if (!l.np) {
      // A real room: search for the friend as Slippi Direct does; they enter this player's code too.
      if (!l.mm) {
        if (now < l.retry_at) continue;
        l.mm = online::arena_matchmaking();
        if (!l.mm) { l.retry_at = now + 5000; continue; }
        Matchmaking::MatchSearchSettings search;
        search.mode = Matchmaking::DIRECT;
        search.connect_code = utf8_to_shiftjis(l.code);
        l.mm->SetLocalPort((uint16_t)(51000 + (now * 7919 + 101 * (uint64_t)(&l - g_links.data())) % 9000));   // links of one tick differ
        l.mm->FindMatch(search);
        event("looking for %s", l.code.c_str());
        continue;
      }
      const auto state = l.mm->GetMatchmakeState();
      if (state == Matchmaking::CONNECTION_SUCCESS) {
        l.np = l.mm->GetNetplayClient();
        drop_later(l.mm);
        l.was_ready = false;
        l.hello_sent = false;
      } else if (state == Matchmaking::ERROR_ENCOUNTERED) {
        event("search for %s: %s", l.code.c_str(), l.mm->GetErrorMessage().c_str());
        drop_later(l.mm);
        l.retry_at = now + 3000;
      }
      continue;
    }
    const auto st = l.np->GetSlippiConnectStatus();
    if (st == NetplayClient::ConnectStatus::FAILED || st == NetplayClient::ConnectStatus::DISCONNECTED) {
      if (l.was_ready) {
        event("link to %s lost", g_hosting ? "a member" : "the host");
        if (g_hosting) member_gone(l.id);
        else { g_room = Room{}; g_self = kNone; }
        if (!g_hosting && !g_test.on) {
          // A real room whose host has gone is over for this member (friends can start a new one).
          drop_later(l.np);
          g_closed_reason = "the room's host left: the room is closed here";
          return;
        }
      }
      l.id = g_hosting ? kNone : l.id;
      drop_later(l.np);
      l.retry_at = now + kRetryMs;
      continue;
    }
    if (!l.np->ArenaReady()) continue;
    if (!l.was_ready) {
      l.was_ready = true;
      l.ready_at = now;
      event("link to %s up", g_hosting ? "a member" : "the host");
    }
    // Up, but the hello never got its answer (the other side was reconnecting when this one came
    // up): reopen it rather than wait for the connection to time out.
    const bool unanswered = g_hosting ? l.id == kNone : g_self == kNone;
    if (unanswered && now - l.ready_at > 6000) {
      event("link to %s up with no answer: reopening", g_hosting ? "a member" : "the host");
      drop_later(l.np);
      l.was_ready = false;
      l.retry_at = now + kRetryMs;
      continue;
    }
    if (!l.hello_sent && (!g_hosting || !g_test.on)) {
      std::vector<uint8_t> h = {kHello};
      put_str(h, g_code);
      put_str(h, g_name);
      h.push_back(g_hosting ? 1 : 0);
      h.push_back((uint8_t)g_room.members.size());
      l.hello_sent = send(l, h);
    }
    std::vector<uint8_t> m;
    for (int n = 0; n < 4000 && !g_yield_to && l.np->PopArena(m); ++n) {
      if (g_hosting) host_message(l, m);
      else member_message(m);
    }
    if (g_yield_to) break;
  }
  if (g_yield_to) {
    Link* keep = (Link*)g_yield_to;
    g_yield_to = nullptr;
    event("joining %s's room", keep->code.c_str());
    Link kept = std::move(*keep);
    for (Link& l : g_links) { drop_later(l.np); drop_later(l.mm); }
    g_links.clear();
    kept.hello_sent = false;
    kept.id = kNone;
    g_links.push_back(std::move(kept));
    g_hosting = false;
    g_room = Room{};
    g_self = kNone;
  }
}

// Under YouGame: the arena's room in place of the links. The host keeps a link per member's
// connection (made at its first message); a member keeps one to the host's.
void service_yg_links() {
  namespace ar = yougame::arena_room;
  const uint64_t now = now_ms();
  std::string why;
  const int st = ar::state(&why);
  if (st == 1) { g_status = g_hosting ? "Opening the room" : "Joining the room"; return; }
  if (st != 2) {
    room_over_here(("the arena's room is closed: " + (why.empty() ? std::string("left") : why)).c_str(), true);
    g_notice = why.empty() ? std::string("The room closed") : why;
    g_notice_until = now + 8000;
    return;
  }
  if (g_yg_invite && g_hosting) {
    g_yg_invite = false;
    g_yg_invite_seen = 0;   // this invite's answer shows even when it is the same as the last one's (a second failure)
    ar::invite();
  }
  const int invite = ar::invite_state(&why);
  if (invite != g_yg_invite_seen) {
    g_yg_invite_seen = invite;
    if (invite == -1) { g_notice = why.empty() ? std::string("Could not invite") : why; g_notice_until = now + 8000; }
    else if (invite == 2) { g_notice = "Invite sent"; g_notice_until = now + 4000; }
  }
  const std::string me = ar::me(), host = ar::host();
  if (me.empty() || host.empty()) return;   // the room's welcome is on its way
  const std::vector<ar::Seat> seats = ar::roster();
  const auto seat_of = [&seats](const std::string& conn) -> const ar::Seat* {
    for (const ar::Seat& s : seats) if (s.conn == conn) return &s;
    return nullptr;
  };
  if (g_hosting) {
    // Members who left the room: gone from the arena too.
    for (Link& l : g_links)
      if (!seat_of(l.yg)) { if (l.id != kNone) member_gone(l.id); l.id = kNone; }
    g_links.erase(std::remove_if(g_links.begin(), g_links.end(), [&](const Link& l) { return !seat_of(l.yg); }), g_links.end());
  } else {
    if (!seat_of(host)) {
      if (g_yg_seen_host) g_closed_reason = "the room's host left: the room is closed here";
      return;
    }
    g_yg_seen_host = true;
    if (g_links.empty()) {
      Link l;
      l.yg = host;
      l.was_ready = true;
      g_links.push_back(std::move(l));
      event("in the arena's room: saying hello to the host");
    }
    Link& l = g_links[0];
    if (g_self == kNone && (!l.hello_sent || now - l.ready_at > 6000)) {   // asked again until the host answers
      std::vector<uint8_t> h = {kHello};
      put_str(h, g_code);
      put_str(h, g_name);
      h.push_back(0);
      h.push_back(1);
      l.hello_sent = send(l, h);
      l.ready_at = now;
    }
  }
  std::string from;
  std::vector<uint8_t> m;
  for (int n = 0; n < 4000 && ar::pop(&from, &m); ++n) {
    if (!g_hosting) {
      if (from == host) member_message(m);
      continue;
    }
    const ar::Seat* seat = seat_of(from);
    if (!seat || from == me || m.empty()) continue;
    Link* l = nullptr;
    for (Link& k : g_links) if (k.yg == from) l = &k;
    if (!l) {
      Link k;
      k.yg = from;
      k.was_ready = true;
      g_links.push_back(std::move(k));
      l = &g_links.back();
    }
    if (m[0] == kHello) {
      // A member is the room's seat it says it is, and only joins (never hosts: one host per arena).
      size_t at = 1;
      std::string code;
      if (!get_str(m, at, code) || code != seat->id) { event("a hello from another player's seat: ignored"); continue; }
      std::string name;
      get_str(m, at, name);
      if (at < m.size()) m[at] = 0;
    }
    host_message(*l, m);
    g_yield_to = nullptr;
  }
}

void send_pings() {
  if (!g_hosting || now_ms() < g_next_pings_ms) return;
  g_next_pings_ms = now_ms() + 1000;
  g_ping.fill(-1);
  if (g_self < kMaxMembers) g_ping[g_self] = 0;
  std::vector<uint8_t> m = {kPings, 0};
  for (Link& l : g_links) {
    if (l.id >= kMaxMembers || !l.np) continue;
    const int ms = l.np->PreMatchPingMs();
    g_ping[l.id] = ms > 0 ? ms : -1;
  }
  for (int id = 0; id < kMaxMembers; ++id) {
    if (!find(g_room, (uint8_t)id)) continue;
    m.push_back((uint8_t)id);
    m.push_back((uint8_t)((uint16_t)g_ping[id] >> 8));
    m.push_back((uint8_t)g_ping[id]);
  }
  m[1] = (uint8_t)((m.size() - 2) / 3);
  for (Link& l : g_links) if (l.id != kNone) send(l, m);
}

// ---- the match: start the right Direct search between games ----
const Member* opponent() {
  const int slot = ring_slot(g_room, g_self);
  if (slot < 0 || g_room.doubles) return nullptr;
  return find(g_room, g_room.ring[slot ^ 1]);
}

// What this member's game should be searching for between games: the opponent's code (singles), or
// the room's Teams code with everyone in the ring (doubles). False when it should not play.
struct Target { int mode = 0; std::string code, label; uint8_t team = 0; std::vector<uint8_t> others; };
bool target(Target& t) {
  const int slot = ring_slot(g_room, g_self);
  if (slot < 0) return false;
  if (!g_room.doubles) {
    const Member* opp = opponent();
    if (!opp) return false;
    t.mode = Matchmaking::DIRECT;
    t.code = opp->code;
    t.label = opp->name;
    t.others = {opp->id};
    return true;
  }
  // Doubles: the four search Teams with the host's code; teams by ring slot (1-2 red, 3-4 blue).
  const Member* host = find(g_room, owner(g_room));
  if (!host) return false;
  t.mode = Matchmaking::TEAMS;
  t.code = host->code;
  t.label = "doubles";
  t.team = (uint8_t)(slot / 2);
  for (int s = 0; s < 4; ++s) if (s != slot && g_room.ring[s] != kNone) t.others.push_back(g_room.ring[s]);
  return t.others.size() == 3;
}

// Tests: MELEE_TEST_ARENA_AUTOLOCK=1 locks a real room's connected pair in at once (test rooms do it
// always), with the character picked on the select, so hidden games play their matches.
// Tests: MELEE_TEST_ARENA_PRESS_START=1 keeps a test room from locking its fighters in: each one locks in
// with Start on the select, as players do (the select presses it, shim/mu_slippi_css.c), so the select's own Start path is covered.
bool test_press_start() {
  static const bool on = [] { const char* v = host::test_env::getenv("MELEE_TEST_ARENA_PRESS_START"); return v && *v == '1'; }();
  return on;
}

bool test_autolock() {
  static const bool on = [] { const char* v = host::test_env::getenv("MELEE_TEST_ARENA_AUTOLOCK"); return v && *v == '1'; }();
  return on;
}

void steer_match() {
  if (g_self == kNone || g_room.in_game || g_wait_room || now_ms() < g_next_match_ms) return;
  if (g_room.members.size() < 2 && !g_test.on) {
    // Alone in the room: nothing to steer, and the player's own Direct searches are their own.
    g_status = "Waiting for friends to join";
    return;
  }
  const online::ArenaMatchStatus st = online::arena_match_status();
  if (st.in_match) return;
  // The search's own state: a pair that has just connected is not an "active session" yet.
  const bool searching = st.state >= (int)Matchmaking::INITIALIZING && st.state <= (int)Matchmaking::CONNECTION_SUCCESS;
  const bool ours = st.mode == Matchmaking::DIRECT || st.mode == Matchmaking::TEAMS;
  Target t;
  if (!target(t)) {
    // Out of the ring: the next pair this member plays is a new one, even against the same opponent
    // as before. Kept, a member who sat out and came back against their last opponent took it for a
    // rematch on their side only, and kept the winner and loser of a game long gone (or none at all,
    // after a Start pressed while watching): their Start then did nothing on the select.
    g_pair_code.clear();
    g_status = ring_slot(g_room, g_self) >= 0 ? "Waiting for another player" : "Watching";
    if (searching && ours && online::in_online_menus()) {
      event("out of the ring: leaving the match with %s", st.code.c_str());
      online::native_cleanup_match();
      g_next_match_ms = now_ms() + 1000;
    }
    return;
  }
  if (searching && (st.mode != t.mode || st.code != t.code)) {
    if (!online::in_online_menus()) return;
    event("next match %s: leaving %s", t.code.c_str(), st.code.c_str());
    online::native_cleanup_match();
    g_next_match_ms = now_ms() + 1000;
    return;
  }
  if (st.state == (int)Matchmaking::ERROR_ENCOUNTERED) {
    online::native_cleanup_match();
    g_next_match_ms = now_ms() + 2000;
    g_status = "Could not reach " + t.label + ", trying again";
    return;
  }
  if (searching) {
    g_status = st.state == (int)Matchmaking::CONNECTION_SUCCESS ? "Ready to fight " + t.label : "Connecting to " + t.label;
    if (g_test.on && !test_press_start() && st.state == (int)Matchmaking::CONNECTION_SUCCESS && !st.local_ready &&
        online::arena_lock_in(g_test.character, 0))
      event("locked in for %s", t.code.c_str());
    uint8_t picked = 2, colour = 0;
    if (!g_test.on && test_autolock() && st.state == (int)Matchmaking::CONNECTION_SUCCESS && !st.local_ready) {
      online::local_css_selection(&picked, &colour);
      if (online::arena_lock_in(picked, colour)) event("locked in for %s (test)", t.code.c_str());
    }
    return;
  }
  if (!online::in_online_menus()) { g_status = "Go to Direct to play " + t.label; return; }
  if (g_test.on && viewer_running()) {
    // Test rooms lock in at once. A player picking a character gives their viewer window time to
    // show the end of the game it was playing, so a test member waits for it too (20 s at most).
    if (!g_test_wait_since) g_test_wait_since = now_ms();
    if (now_ms() - g_test_wait_since < 20000) { g_status = "Watching the end of the last game"; return; }
  }
  g_test_wait_since = 0;
  if (g_test.on) {
    // Test rooms: the players find each other on loopback, in ring-slot order (slot 0 decides).
    auto& lp = Matchmaking::local_peer;
    lp.enabled = true;
    lp.local_index = ring_slot(g_room, g_self);
    // A block of ports per game (both sides know the room's game count), so a new pair never reaches
    // the last pair's connection while it is still closing.
    const int block = g_test.base + 300 + 20 * (int)(g_room.games % 10);
    lp.local_port = (uint16_t)(block + g_test.index);
    lp.remotes.clear();
    for (int s = 0; s < ring_size(g_room); ++s) {
      const Member* m = find(g_room, g_room.ring[s]);
      if (!m || m->id == g_self) continue;
      int index = -1;
      std::sscanf(m->code.c_str(), "ARENA#%d", &index);
      lp.remotes.push_back("127.0.0.1:" + std::to_string(block + index));
    }
  }
  std::string pairing = t.code;
  for (uint8_t id : t.others) pairing += "/" + std::to_string(id);
  // A new pairing: other opponents than last time, or the same ones with a fighter who came into the
  // ring since this pairing was counted (tenure 0: no game finished there yet). The room is the same on
  // every member's PC, so both sides of a pair count it alike. A rematch of fighters who stayed keeps
  // its winner and loser.
  bool newcomer = false;
  for (int s = 0; s < ring_size(g_room); ++s) newcomer |= g_room.ring[s] != kNone && g_room.tenure[s] == 0;
  if (pairing != g_pair_code || (newcomer && g_room.games != g_pair_games)) {
    online::arena_note_new_pairing();
    g_pair_code = pairing;
    g_pair_games = g_room.games;
  }
  std::string error;
  const bool locked = g_test.on && !test_press_start();
  bool started;
  if (g_yg) {
    // The arena's room carries the game: its fighters are found by their YouGame ids, in ring order.
    std::vector<std::string> fighters;
    for (int s = 0; s < ring_size(g_room); ++s)
      if (const Member* m = find(g_room, g_room.ring[s])) fighters.push_back(m->code);
    started = online::arena_start_yg_search(t.mode, fighters, t.code, locked, g_test.character, 0, t.team, &error);
  } else {
    started = online::arena_start_search(t.mode, t.code, locked, g_test.character, 0, t.team, &error);
  }
  if (started) {
    event("searching for %s (%s)", t.code.c_str(), t.mode == Matchmaking::TEAMS ? "doubles" : "singles");
    g_status = "Connecting to " + t.label;
  } else {
    event("could not search for %s: %s", t.code.c_str(), error.c_str());
    // A pair that ended without its game being cleaned up (the other side left at the very start of
    // a match) still counts as an online session, and no new search can start until it is closed: on
    // the select, outside a match, it is closed here (one PC with a slow member: it searched forever).
    if (st.active && !st.in_match && online::in_online_menus()) {
      event("an ended pair was still open: closing it");
      online::native_cleanup_match();
    }
  }
  g_next_match_ms = now_ms() + 1000;
}

// Test rooms only: the host plays the members' parts from MELEE_ARENA_TEST_PLAN, so hidden runs
// cover asking, answering and the doubles vote end to end. Steps, each once and in order, between
// games once the room has finished at least N games:
//   mode:<N>:winner|turns|ask      ask:<N>:<member>      answer:<N>:yes|winner|no (every fighter)
//   doubles:<N>                    (proposed by the host; every voter says yes)
struct PlanStep { std::string what, arg; uint32_t after = 0; uint8_t member = kNone; bool done = false; };
std::vector<PlanStep> g_plan;
bool g_plan_read = false;

void run_test_plan() {
  if (!g_test.on || !g_hosting) return;
  if (!g_plan_read) {
    g_plan_read = true;
    const char* env = std::getenv("MELEE_ARENA_TEST_PLAN");
    std::string all = env ? env : "";
    for (size_t at = 0; at < all.size();) {
      const size_t end = std::min(all.find(',', at), all.size());
      const std::string step = all.substr(at, end - at);
      at = end + 1;
      PlanStep p;
      char what[16] = {}, arg[16] = {};
      unsigned after = 0;
      if (std::sscanf(step.c_str(), "%15[a-z]:%u:%15s", what, &after, arg) >= 2) {
        p.what = what; p.after = after; p.arg = arg;
        if (p.what == "ask") p.member = (uint8_t)std::atoi(arg);
        g_plan.push_back(p);
      }
    }
    if (!g_plan.empty()) event("test plan: %zu steps", g_plan.size());
  }
  if (g_room.in_game) return;
  for (PlanStep& p : g_plan) {
    if (p.done) continue;
    if (g_room.games < p.after) return;   // in order: wait for this one
    bool ok = false;
    if (p.what == "mode") ok = set_mode(g_room, p.arg == "turns" ? Mode::TakeTurns : p.arg == "ask" ? Mode::Ask : Mode::WinnerStays);
    else if (p.what == "ask") ok = ask(g_room, p.member);
    else if (p.what == "answer") {
      const Answer a = p.arg == "no" ? Answer::No : p.arg == "winner" ? Answer::YesAndPlayWinner : Answer::Yes;
      for (int s = 0; s < ring_size(g_room) && g_room.asker != kNone; ++s) ok = answer(g_room, g_room.ring[s], a) || ok;
    } else if (p.what == "doubles") {
      ok = propose_switch(g_room, g_self);
      for (uint8_t v : switch_voters(g_room)) vote_switch(g_room, v, true);
    }
    p.done = true;
    event("test plan: %s %s after %u games: %s", p.what.c_str(), p.arg.c_str(), p.after, ok ? "done" : "refused");
  }
}

void publish_view() {
  View v;
  v.active = g_active;
  v.hosting = g_hosting;
  v.linked = g_hosting || (!g_links.empty() && g_links[0].was_ready);
  v.self = g_self;
  v.room = g_room;
  v.ping_ms = g_ping;
  v.watching_live = viewer_running();
  v.status = g_warming ? std::string("Warming up in Training") : g_status;
  v.warming = g_warming_mask;
  if (!g_notice.empty() && now_ms() < g_notice_until) {
    v.notice = g_notice;
    if (g_active) v.status = g_notice;   // in a room the looks show it where the status line is
  }
  std::lock_guard<std::mutex> lk(g_mutex);
  g_view = std::move(v);
}

}  // namespace

// ---------------------------------------------------------------- public
bool set_test(const std::string& spec) {
  int index = -1, count = 0, base = 0, character = 2;
  const int n = std::sscanf(spec.c_str(), "%d/%d/%d/%d", &index, &count, &base, &character);
  if (n < 3 || count < 2 || count > kMaxMembers || index < 0 || index >= count || base < 1024 || base > 60000 ||
      character < 0 || character > 25)
    return false;
  g_test = {true, index, count, base, (uint8_t)character};
  char code[16];
  std::snprintf(code, sizeof code, "ARENA#%d", index);
  g_code = code;
  g_name = "P" + std::to_string(index + 1);
  g_active = true;
  g_hosting = index == 0;
  g_ping.fill(-1);
  if (g_hosting) { g_room = Room{}; g_self = join(g_room, g_code, g_name); }
  open_links();
  return true;
}
bool test_mode() { return g_test.on; }
// Tests: MELEE_TEST_ARENA_AUTOLOCK_AFTER=<frames> holds that lock-in back until the select has been up
// that long, so a test can lock a player in while the other one is warming up.
bool autolock_due() {
  static const int after = [] { const char* v = host::test_env::getenv("MELEE_TEST_ARENA_AUTOLOCK_AFTER"); return v ? std::atoi(v) : 0; }();
  if (after <= 0) return true;
  // Asked only while a select is up: a gap in the asking is a new select (a game came between).
  static uint32_t since = 0, last = 0;
  const uint32_t now = host::retrace_count();
  if (!online::in_online_menus() || online::arena_match_status().in_match || now - last > 30) since = 0;
  last = now;
  if (!online::in_online_menus()) return false;
  if (!since) since = now ? now : 1;
  return now - since >= (uint32_t)after;
}
bool locks_in_itself() { return g_test.on || (test_autolock() && autolock_due()); }
void set_recording_codes(std::vector<uint8_t> codes) { g_codes = std::move(codes); }
void set_viewer_launcher(std::function<void*(const std::string&)> launch) { g_launch_viewer = std::move(launch); }

// This member's pick on the character select goes to the room (twice a second, when it changed),
// so every panel can show each player's portrait.
void report_character() {
  static uint64_t next = 0;
  if (now_ms() < next || g_self == kNone) return;
  next = now_ms() + 500;
  const uint16_t pick = g_css_pick.load();
  uint8_t character = (uint8_t)(pick >> 8), costume = (uint8_t)(pick & 0xFF);
  if (character == kNone && !online::local_css_selection(&character, &costume)) return;
  const Member* me = find(g_room, g_self);
  if (!me || (me->character == character && me->costume == costume)) return;
  do_act({Act::SetCharacter, character, std::string(1, (char)costume)});
}

// The game's own Direct search met a room's link: the host of a room had added this player's code,
// and this player entered the host's code in plain Direct (or the room's next-match search crossed
// a link coming back up). The Direct pair is ended; out of a room this game then joins that room, as
// "Add a friend's code" would.
void stray_room_hello() {
  std::vector<uint8_t> m;
  for (int n = 0; n < 16 && online::arena_pop_from_match(m); ++n) {
    if (m.empty() || m[0] != kHello || g_test.on) continue;
    size_t at = 1;
    std::string code, name;
    if (!get_str(m, at, code) || !get_str(m, at, name)) continue;
    const bool they_host = at + 2 <= m.size() && m[at] != 0;
    const online::ArenaMatchStatus st = online::arena_match_status();
    if (st.in_match || !online::in_online_menus() || (yougame::session() && !g_test.on)) continue;
    online::native_cleanup_match();
    g_next_match_ms = now_ms() + 2000;
    if (g_active || !they_host) { event("the Direct search met a room link (%s): searching again", code.c_str()); return; }
    event("the Direct search met %s's room: joining it", code.c_str());
    g_notice = "Joining " + (name.empty() ? code : name) + "'s room";
    g_notice_until = now_ms() + 6000;
    act(Act::AddCode, 0, code);
    return;
  }
}

// Tests: MELEE_TEST_ARENA_LEAVE=<games> leaves the room as Rules > Leave the room does, once the room
// has finished that many games and this game is back on the online character select.
void test_leave() {
  static const int after = [] { const char* v = host::test_env::getenv("MELEE_TEST_ARENA_LEAVE"); return v && *v ? std::atoi(v) : -1; }();
  static bool done = false;
  if (after < 0 || done || !g_active || g_self == kNone) return;
  if ((int)g_room.games < after || g_room.in_game || !online::in_online_menus()) return;
  if (online::arena_match_status().in_match) return;
  done = true;
  event("test: leaving the room as the panel does");
  act(Act::LeaveRoom);
}

// Tests: MELEE_TEST_ARENA_ADD=<code>[,<code>...][@<seconds>] adds those friends, once, as the panel's
// "Add a friend's code" does, <seconds> (default 5) after the online menus first show.
void test_add_codes() {
  static std::vector<std::string> codes;
  static double after = 5.0;
  static bool parsed = false, done = false;
  static uint64_t menus_since = 0;
  if (done) return;
  if (!parsed) {
    parsed = true;
    const char* v = host::test_env::getenv("MELEE_TEST_ARENA_ADD");
    std::string s = v ? v : "";
    if (const size_t at = s.find('@'); at != std::string::npos) { after = std::atof(s.c_str() + at + 1); s.resize(at); }
    for (size_t i = 0; i < s.size();) {
      const size_t end = std::min(s.find(',', i), s.size());
      if (end > i) codes.push_back(s.substr(i, end - i));
      i = end + 1;
    }
    if (codes.empty()) { done = true; return; }
  }
  if (!online::in_online_menus()) { menus_since = 0; return; }
  if (!menus_since) menus_since = now_ms();
  if (now_ms() - menus_since < (uint64_t)(after * 1000.0)) return;
  done = true;
  for (const std::string& c : codes) {
    event("test: adding %s as the panel does", c.c_str());
    act(Act::AddCode, 0, c);
  }
}

void tick() {
  if (!rooms_enabled()) return;   // tests only: rooms off
  test_add_codes();
  test_leave();
  stray_room_hello();
  {
    std::deque<Pending> todo;
    { std::lock_guard<std::mutex> lk(g_mutex); todo.swap(g_pending); }
    if (g_code.empty() && !todo.empty()) online::local_identity(&g_code, &g_name);   // a real room: the Slippi account
    for (const Pending& p : todo) {
      // The player's next action clears the last line (a failed invite, a room that did not open). Not the room's
      // own character report, nor a code it adds for the player (a Direct search that met a room says "Joining").
      if (p.a != Act::SetCharacter && !(p.a == Act::AddCode && !p.text.empty())) g_notice_until = 0;
      do_act(p);
    }
  }
  {
    static int every = 0;
    if (++every >= 10) {
      every = 0;
      const online::ArenaMatchStatus st = online::arena_match_status();
      g_search_busy.store(st.state >= (int)Matchmaking::INITIALIZING && st.state <= (int)Matchmaking::CONNECTION_SUCCESS);
    }
  }
  {
    const uint64_t now = now_ms();
    for (auto& c : g_closing) if (now >= c.first) drop_later(c.second.np);
    g_closing.erase(std::remove_if(g_closing.begin(), g_closing.end(), [](const auto& c) { return !c.second.np; }), g_closing.end());
  }
  if (g_close_room) {
    g_close_room = false;
    room_over_here("left: the room was full", false);
  }
  if (g_yg_close_at && now_ms() >= g_yg_close_at) {
    g_yg_close_at = 0;
    yougame::arena_room::close();
  }
  if (!g_active) {
    // The room tab's notice (a full room) still reaches the panel while this game is in no room.
    static bool had_notice = false;
    const bool notice = !g_notice.empty() && now_ms() < g_notice_until;
    if (notice || had_notice) publish_view();
    had_notice = notice;
    return;
  }
  if (g_yg) service_yg_links();
  else service_links();
  if (!g_active) { publish_view(); return; }
  if (g_member_closed || g_closed_reason) {
    const bool host_said = g_member_closed;
    const char* why = g_closed_reason;
    g_member_closed = false; g_closed_reason = nullptr;
    room_over_here(host_said ? "the host closed the room" : why, true);
    g_notice = host_said ? "The host closed the room" : "The room's host left";
    g_notice_until = now_ms() + 8000;
    publish_view();
    return;
  }
  send_pings();
  report_character();
  run_test_plan();
  broadcast_room();
  steer_match();
  pip_tick();
  view_tick();
  publish_view();
}

namespace {
// The first fighter's score for the room's warm-up strip: each fighter's stocks and percent as of the
// newest frame that cannot change any more (a rollback never shows), about twice a second.
void send_score(const ScoreFrame& f) {
  std::vector<uint8_t> m = {kScore};
  put32(m, g_my_tag);
  put32(m, (uint32_t)f.frame);
  for (int i = 0; i < 4; ++i) {
    const bool here = ((f.present >> i) & 1) && g_score_char[i] != 0xFF;
    const float pct = std::clamp(f.percent[i] * 10.0f, 0.0f, 65535.0f);
    const uint16_t tenths = (uint16_t)pct;
    m.push_back(here ? 1 : 0);
    m.push_back(g_score_char[i]);
    m.push_back(g_score_costume[i]);
    m.push_back(f.stocks[i]);
    m.push_back((uint8_t)(tenths >> 8));
    m.push_back((uint8_t)tenths);
  }
  // Then each port's name (length, bytes): older builds read the first part only.
  for (int i = 0; i < 4; ++i) {
    m.push_back((uint8_t)g_score_name[i].size());
    m.insert(m.end(), g_score_name[i].begin(), g_score_name[i].end());
  }
  if (g_hosting) {
    for (Link& l : g_links) if (l.id != kNone && !in_ring_of(g_send_ring, l.id)) send(l, m);
  } else {
    send_to_host(m);
  }
}

void score_event(const uint8_t* data, uint32_t size) {
  const uint8_t command = data[0];
  const uint8_t* p = data + 1;
  const uint32_t n = size - 1;
  if (command == 0x36) {   // Game Start: who plays, in which colours (empty slots have player type 3)
    for (int i = 0; i < 4; ++i) {
      const size_t at = 4 + 0x60 + 0x24 * (size_t)i;
      g_score_char[i] = at + 3 < n && p[at + 1] != 3 ? p[at] : 0xFF;
      g_score_costume[i] = at + 3 < n ? p[at + 3] : 0;
      // The name the game shows: display name (Game Start 0x1A5), else the connect code's tag (0x221).
      std::string name = n >= 0x1A4 + 0x1F * 4 ? sjis_ascii(p + 0x1A4 + 0x1F * i, 0x1F) : std::string();
      if (name.empty() && n >= 0x220 + 0x0A * 4) {
        name = sjis_ascii(p + 0x220 + 0x0A * i, 0x0A);
        const size_t hash = name.find('#');
        if (hash != std::string::npos) name.resize(hash);
      }
      g_score_name[i] = name.substr(0, 15);
    }
    for (ScoreFrame& f : g_score_ring) f = ScoreFrame{};
    g_score_sent_frame = INT32_MIN;
  } else if (command == 0x38 && n >= 0x21) {   // post-frame: percent (float) at +0x15, stocks at +0x20
    const int32_t frame = (int32_t)get32(p);
    const uint8_t port = p[4];
    if (port > 3 || p[5]) return;   // a follower (Nana) is not a fighter of the score
    ScoreFrame& f = g_score_ring[(uint32_t)frame & 31u];
    if (f.frame != frame) { f = ScoreFrame{}; f.frame = frame; }
    uint32_t bits = get32(p + 0x15);
    float pct;
    std::memcpy(&pct, &bits, 4);
    f.present |= (uint8_t)(1u << port);
    f.stocks[port] = p[0x20];
    f.percent[port] = pct;
  } else if (command == 0x3C && n >= 8) {   // frame end: frames up to the second word are final
    const int32_t final_frame = (int32_t)get32(p + 4);
    const ScoreFrame& f = g_score_ring[(uint32_t)final_frame & 31u];
    bool changed = false;   // a stock lost or the whole percent moved: out now, not at the next half second
    for (int i = 0; i < 4; ++i)
      changed |= f.stocks[i] != g_score_sent.stocks[i] || (int)f.percent[i] != (int)g_score_sent.percent[i];
    if (f.frame == final_frame && (g_score_sent_frame == INT32_MIN || changed || final_frame >= g_score_sent_frame + 30)) {
      g_score_sent_frame = final_frame;
      g_score_sent = f;
      send_score(f);
    }
  } else if (command == 0x39) {   // Game End: the newest frame as it ended
    const ScoreFrame* newest = nullptr;
    for (const ScoreFrame& f : g_score_ring) if (f.frame != INT32_MIN && (!newest || f.frame > newest->frame)) newest = &f;
    if (newest) send_score(*newest);
  }
}
}  // namespace

void recording_event(const uint8_t* data, uint32_t size) {
  if (host::options.spectate && data && size) viewer_score_event(data, size);   // the room's match window
  if (!g_active || !data || !size) return;
  const uint8_t command = data[0];
  if (command == 0x35) { g_table.assign(data, data + size); return; }
  if (command == 0x36) {
    // Game Start: an online match (the Game Start's scene, or its online match id) in the ring.
    const uint8_t* start = data + 1;
    const uint32_t n = size - 1;
    g_my_online = n >= 0x2F8 && (start[0x1A3] == 0x08 || start[0x2BD] != 0) && ring_slot(g_room, g_self) >= 0;
    g_my_tag = feed::game_tag(start, n);
    if (g_my_online) close_viewer();   // playing now: the last game's viewer window has had its turn
    g_my_doubles = g_room.doubles;
    feed::port_teams(start, n, g_port_team);
    if (!g_my_doubles) { const Member* opp = opponent(); g_my_opp = opp ? opp->code : std::string(); }
    else {
      // Doubles: a player of the other team stands for it in the report.
      const int slot = ring_slot(g_room, g_self);
      const Member* other = slot >= 0 ? find(g_room, g_room.ring[slot < 2 ? 2 : 0]) : nullptr;
      g_my_opp = other ? other->code : std::string();
    }
    // The first fighter sends the game out when anybody is watching.
    const bool watchers = g_room.members.size() > (size_t)ring_size(g_room);
    g_sending = g_my_online && g_room.ring[0] == g_self && watchers;
    if (g_sending) {
      g_sender = feed::Sender{};
      std::copy(std::begin(g_room.ring), std::end(g_room.ring), g_send_ring.begin());
      g_sender.set_out([](const uint8_t* d, uint32_t len) {
        std::vector<uint8_t> m = {kFeed};
        m.insert(m.end(), d, d + len);
        if (g_hosting) {
          for (Link& l : g_links) if (l.id != kNone && !in_ring_of(g_send_ring, l.id)) send(l, m, true);
        } else {
          send_to_host(m, true);
        }
      });
      if (!g_table.empty()) g_sender.event(g_table.data(), (uint32_t)g_table.size());
      event("sending game %08X to the room", g_my_tag);
    }
  }
  if (g_sending) {
    g_sender.event(data, size);
    score_event(data, size);
  }
  // Every fighter reports its game's start and end to the host; the first report of each counts.
  if ((command == 0x36 || command == 0x39) && g_my_online) {
    std::vector<uint8_t> m = {kReport, (uint8_t)(command == 0x36 ? kStarted : kEnded)};
    put32(m, g_my_tag);
    std::string winner;
    // Game End: the method (1 time, 2 game, 3 game in newer recordings; 7 is a no contest), the
    // LRA-Start port, then each port's placement. Whoever placed first wins (in doubles, their team).
    if (command == 0x39) {
      const int result = feed::my_result(data + 1, size - 1, online::local_player_index(), g_port_team, g_my_doubles);
      if (result >= 0) winner = result ? g_code : g_my_opp;
    }
    put_str(m, winner);
    if (g_hosting) apply_report(m[1], g_my_tag, winner);
    else send_to_host(m);
    if (command == 0x39) {
      event("my game %08X ended (winner %s)", g_my_tag, winner.empty() ? "none" : winner.c_str());
      if (!g_hosting && g_room.in_game) g_wait_room = true;   // until the room has this game's end
      g_my_online = false;
    }
  }
  if (command == 0x39 && g_sending) {
    const auto& st = g_sender.stats();
    event("feed sent: %u frames, %llu bytes, %u replaced after rollbacks, %u late", st.frames, (unsigned long long)st.bytes,
          st.superseded, st.late);
    g_sending = false;
  }
}

void shutdown() {
  close_viewer();
  g_receiver.close();
  for (Link& l : g_links) l.np.reset();
  g_links.clear();
  g_closing.clear();
  g_active = false;
}

void act(Act a, uint8_t arg, const std::string& text) {
  std::lock_guard<std::mutex> lk(g_mutex);
  g_pending.push_back({a, arg, text});
}

bool search_busy() { return g_search_busy.load(); }

// ---- warm-up room ----
bool turn_called() {
  if (!g_active || g_self == kNone || g_room.in_game) return false;
  if (ring_slot(g_room, g_self) < 0) return false;
  for (int s = 0; s < ring_size(g_room); ++s) if (g_room.ring[s] == kNone) return false;
  return true;
}

bool warmup_ok(std::string* why) {
  const auto no = [&](const char* reason) { if (why) *why = reason; return false; };
  if (!g_active || g_self == kNone) return no("");
  if (g_warming) return no("Warming up");
  if (!g_hosting && (g_links.empty() || !g_links[0].was_ready)) return no("Joining the room");
  if (turn_called()) return no("Your match is next");
  if (ring_slot(g_room, g_self) >= 0 && g_room.in_game) return no("");
  return true;
}

void pip_adjust(bool on) { g_pip_adjust_req.store(on ? 1 : 2); }
bool pip_active() { return g_pip_up.load(); }
void toggle_watch_layout() { g_watch_toggle.store(1); }
int watch_layout() { return g_pip_up.load() ? g_pip.applied : g_watch_open.load() ? 3 : 0; }
void note_side_card_bottom(float share) {
  g_card_bottom.store(share);
  g_card_bottom_ms.store(now_ms());
}

void set_warming(bool on) {
  if (on == g_warming) return;
  g_warming = on;
  if (g_active && g_self != kNone && g_self < 16) {
    if (g_hosting) {
      const uint16_t bit = (uint16_t)(1u << g_self);
      g_warming_mask = on ? (uint16_t)(g_warming_mask | bit) : (uint16_t)(g_warming_mask & ~bit);
    } else {
      send_to_host({kWarmup, (uint8_t)(on ? 1 : 0)});
    }
  }
  // The match window stays as it is (pip_tick lays it out for where the player is now).
  event(on ? "warming up (Training, then anywhere offline)" : "back from the warm-up");
}

WarmupInfo warmup_info() {
  WarmupInfo w;
  if (!g_active || g_self == kNone) return w;
  const int slot = ring_slot(g_room, g_self);
  if (slot >= 0) {
    w.place = turn_called() ? "Your match is next" : g_room.in_game ? "Fighting" : "Waiting for an opponent";
  } else {
    const int at = line_place(g_room, g_self);
    w.place = at == 0 ? std::string("Next up") : at > 0 ? std::to_string(at) + " ahead of you" : std::string("Watching");
  }
  if (g_room.in_game) {
    const auto label = [](uint8_t id) {
      const Member* m = find(g_room, id);
      if (!m) return std::string("?");
      std::string s = m->name.empty() ? m->code : m->name;
      if (m->name.empty()) { const size_t hash = s.find('#'); if (hash != std::string::npos) s.resize(hash); }
      return s;
    };
    if (g_room.doubles)
      w.versus = label(g_room.ring[0]) + " & " + label(g_room.ring[1]) + " vs " + label(g_room.ring[2]) + " & " + label(g_room.ring[3]);
    else
      w.versus = label(g_room.ring[0]) + " vs " + label(g_room.ring[1]);
    w.score = g_score;
    w.score.live = g_score.live && g_score_tag == g_receiver_tag && g_receiver_tag != 0;
    // The match window open: the panel shows what it shows, frame for frame (it simulates the game).
    uint64_t packed = 0;
    if (viewer_running() && host::window_viewer_score(&packed)) {
      for (int i = 0; i < 4; ++i) {
        const unsigned v = (unsigned)((packed >> (14 * i)) & 0x3FFF), st = v >> 10;
        auto& fighter = w.score.fighter[i];
        if (st == 15) { fighter.present = false; continue; }
        fighter.present = true;
        fighter.stocks = (uint8_t)st;
        fighter.percent = (float)(v & 1023);
      }
      w.score.live = true;
      static uint64_t logged = ~0ull;
      if (((packed ^ logged) & 0x3C003C003C003C00ull) != 0) {   // the panel's stocks changed: as the window shows them
        event("panel stocks from the match window: %u %u %u %u", (unsigned)(packed >> 10 & 15), (unsigned)(packed >> 24 & 15),
              (unsigned)(packed >> 38 & 15), (unsigned)(packed >> 52 & 15));
        logged = packed;
      }
    }
  }
  return w;
}

std::atomic<uint32_t> g_css_hand{0xFFFFFFFFu};
void set_css_hand(int16_t x10, int16_t y10) { g_css_hand.store((uint32_t)(uint16_t)x10 << 16 | (uint16_t)y10); }
bool css_hand(float* x, float* y) {
  const uint32_t v = g_css_hand.load();
  if (v == 0xFFFFFFFFu) return false;
  *x = (int16_t)(v >> 16) / 10.0f;
  *y = (int16_t)(v & 0xFFFF) / 10.0f;
  return true;
}

std::atomic<int> g_css_mode{-1};
void set_css_mode(int mode) { g_css_mode.store(mode); }
bool css_ranked_or_unranked() {
  const int mode = g_css_mode.load();
  return mode == Matchmaking::RANKED || mode == Matchmaking::UNRANKED;
}

void set_css_pick(uint8_t character, uint8_t costume) {
  g_css_pick.store((uint16_t)(character << 8 | costume));
}

bool css_pick(uint8_t* character, uint8_t* costume) {
  const uint16_t v = g_css_pick.load();
  if ((v >> 8) >= 26) return false;
  *character = (uint8_t)(v >> 8);
  *costume = (uint8_t)v;
  return true;
}

bool in_room() {
  std::lock_guard<std::mutex> lk(g_mutex);
  return g_view.active;
}
bool room_just_ended() {
  const uint64_t at = g_room_over_ms.load();
  return at != 0 && GetTickCount64() - at < 5000;
}

namespace {
const char* const kFixtureStates[] = {"alone", "n2", "n3", "n5", "n8", "refused", "asking", "prompt", "watching", "doubles", "linklost"};
constexpr int kFixtureStateCount = 11;
constexpr int kFixtureLooks = 5;
std::string fixture_spec() {
  static const std::string spec = [] { const char* s = host::test_env::getenv("MELEE_TEST_ARENA_FIXTURE"); return std::string(s ? s : ""); }();
  return spec;
}
// "cycle[:long]": every second (60 retraces) the next state, and after all states the next look.
int fixture_index() {
  const std::string spec = fixture_spec();
  if (spec.rfind("cycle", 0) != 0) return -1;
  static uint32_t start = 0;
  const uint32_t now = host::retrace_count();
  if (!start) start = now;
  return (int)(((now - start) / 60) % (kFixtureStateCount * kFixtureLooks));
}
View build_fixture(const std::string& state, bool wide, const std::string& spec) {
  {
    View v;
    if (state == "refused") {   // in no room: a full room said no
      v.notice = "That room is full (" + std::to_string(kRoomLimit) + " players at most)";
      host::log("arena: test fixture %s (%s)", state.c_str(), spec.c_str());
      return v;
    }
    int n = 3;
    if (state.size() > 1 && state[0] == 'n') n = std::atoi(state.c_str() + 1);
    else if (state == "alone") n = 1;
    else if (state == "doubles") n = 5;
    else if (state == "watching" || state == "asking" || state == "prompt" || state == "linklost") n = 4;
    n = std::clamp(n, 1, kMaxMembers);
    static const char* const kNames[] = {"HERO", "WISE", "PEACE", "MOONKEH", "PARTY", "JJASHYA", "KAI", "ROSE",
                                         "AXE", "LUNA", "MILO", "ZED", "NOVA", "IVY", "OTTO", "PIX"};
    static const uint8_t kChars[] = {19, 3, 2, 20, 9, 15, 0, 12, 13, 16, 6, 21, 25, 14, 18, 10};
    Room r;
    for (int i = 0; i < n; ++i) {
      char code[16];
      std::snprintf(code, sizeof code, "TEST#%03d", i + 1);
      const uint8_t id = join(r, code, wide ? "WWWWWWWW" : kNames[i]);
      if (id == kNone) break;
      Member* m = nullptr;
      for (Member& e : r.members) if (e.id == id) m = &e;
      if (!m) continue;
      m->character = kChars[i % 16];
      m->costume = (uint8_t)(i % 4);
      m->wins = (uint16_t)(wide ? 100 + 7 * i : (i * 3) % 7);
      m->losses = (uint16_t)(wide ? 900 - 9 * i : (i * 2) % 5);
      v.ping_ms[id] = i == 0 ? 0 : wide ? 999 : 18 + 11 * i;
    }
    r.games = wide ? 123 : 4;
    if (n >= 2) { r.streak_id = r.ring[0]; r.streak = wide ? 101 : 3; }
    v.active = true; v.hosting = true; v.linked = true;
    v.self = r.members.empty() ? kNone : r.members[0].id;
    v.status = "Ready to fight " + (r.members.size() > 1 ? r.members[1].code : std::string("a friend"));
    if (state == "alone") v.status = "Add a friend's code to start";
    if (state == "watching" && n >= 4) { v.self = r.members[3].id; toggle_line(r, v.self); v.watching_live = true; v.hosting = false; v.status = "Watching"; }
    if (state == "asking" && n >= 3) { v.self = r.members[2].id; ask(r, v.self); v.hosting = false; v.status = "Asked to play next"; }
    if (state == "prompt" && n >= 3) { ask(r, r.members[2].id); }
    if (state == "doubles") { propose_switch(r, r.members[0].id); vote_switch(r, r.members[0].id, true); }
    if (state == "linklost" && n >= 2) { v.self = r.members[2].id; v.hosting = false; v.linked = false; v.status = "Lost the link to the room. Reconnecting"; }
    v.room = r;
    host::log("arena: test fixture %s (%s), %d members, valid %d", state.c_str(), spec.c_str(), (int)r.members.size(), valid(r) ? 1 : 0);
    return v;
  }
}
bool fixture(View& out) {
  const std::string spec = fixture_spec();
  if (spec.empty()) return false;
  const bool wide = spec.find(":long") != std::string::npos;
  static std::map<std::string, View> made;
  const int index = fixture_index();
  const std::string state = index >= 0 ? kFixtureStates[index % kFixtureStateCount] : spec.substr(0, spec.find(':'));
  auto it = made.find(state);
  if (it == made.end()) it = made.emplace(state, build_fixture(state, wide, spec)).first;
  out = it->second;
  return true;
}
}  // namespace

bool test_fixture_active() { return !fixture_spec().empty(); }
bool rooms_enabled() {
  static const bool test_off = [] { const char* v = host::test_env::getenv("MELEE_TEST_ARENA_ROOMS"); return v && *v == '0'; }();
  return !test_off || g_test.on || test_fixture_active();
}
int test_fixture_index() { return fixture_index(); }
const char* test_fixture_state(int index) { return index >= 0 ? kFixtureStates[index % kFixtureStateCount] : ""; }
int test_fixture_look() {
  const int index = fixture_index();
  if (index < 0) return -1;
  static int last = -1;
  if (index != last) {
    last = index;
    host::log("arena: test fixture now %s, look %d", kFixtureStates[index % kFixtureStateCount], index / kFixtureStateCount);
  }
  return index / kFixtureStateCount;
}

View view() {
  View f;
  if (fixture(f)) return f;
  std::lock_guard<std::mutex> lk(g_mutex);
  return g_view;
}

std::vector<std::string> recent_events() {
  std::lock_guard<std::mutex> lk(g_mutex);
  return {g_events.begin(), g_events.end()};
}

}  // namespace slippi::arena
