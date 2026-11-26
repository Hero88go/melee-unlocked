// Arena room rules (see arena_rules.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "arena_rules.h"
#include <algorithm>
#include <cctype>

namespace slippi::arena {
namespace {

std::string clean(const std::string& s, size_t max, bool upper) {
  std::string out;
  for (char c : s) {
    const unsigned char u = (unsigned char)c;
    if (u < 0x20 || u == 0x7F) continue;
    out += upper ? (char)std::toupper(u) : c;
    if (out.size() >= max) break;
  }
  return out;
}

bool is_fighting(const Room& r, uint8_t id) { return ring_slot(r, id) >= 0; }

void remove_from_line(Room& r, uint8_t id) {
  r.line.erase(std::remove(r.line.begin(), r.line.end(), id), r.line.end());
}

uint8_t pop_front(Room& r) {
  if (r.line.empty()) return kNone;
  const uint8_t id = r.line.front();
  r.line.erase(r.line.begin());
  return id;
}

// Whether `id` may take a ring slot now: a person always; a Pro AI only in a doubles or free-for-all ring
// its owner is already in.
bool may_fight(const Room& r, uint8_t id) {
  const Member* m = find(r, id);
  if (!m) return false;
  if (!is_bot(*m)) return true;
  return r.kind != Ring::Singles && ring_slot(r, m->bot_of) >= 0;
}
int people_in_ring(const Room& r) {
  int n = 0;
  for (int s = 0; s < ring_size(r); ++s)
    if (const Member* m = find(r, r.ring[s])) n += is_bot(*m) ? 0 : 1;
  return n;
}

void clear_choices(Room& r) {
  r.asker = kNone; r.refused = 0;
  r.leaving_slot = kNone; r.incoming = kNone; r.leaver_plays_winner = false;
}

void clear_switch(Room& r) { r.switch_pending = false; r.switch_votes = 0; r.switch_to = Ring::Singles; }

void reset_tenure(Room& r) { for (uint16_t& t : r.tenure) t = 0; }

// A free-for-all keeps its fighters in the first slots (a slot freed in the middle closes up), so
// slot order stays join order and the panel's corners read the same everywhere.
void compact_ring(Room& r) {
  if (!ffa(r)) return;
  int to = 0;
  for (int s = 0; s < kRingMax; ++s) {
    if (r.ring[s] == kNone) continue;
    if (s != to) { r.ring[to] = r.ring[s]; r.tenure[to] = r.tenure[s]; r.ring[s] = kNone; r.tenure[s] = 0; }
    ++to;
  }
}

// The ring shrinks to singles: the first two fighters in slot order stay, the others go to the front
// of the line in order (a doubles ring keeps one of each team, the first of each).
void shrink_to_singles(Room& r) {
  std::vector<uint8_t> in;
  if (doubles(r)) {
    // One player from each team stays; their partners go to the front of the line.
    in = {r.ring[0], r.ring[2], r.ring[1], r.ring[3]};
  } else {
    for (int s = 0; s < kRingMax; ++s) in.push_back(r.ring[s]);
  }
  in.erase(std::remove(in.begin(), in.end(), kNone), in.end());
  r.kind = Ring::Singles;
  for (int s = 0; s < kRingMax; ++s) { r.ring[s] = kNone; r.tenure[s] = 0; }
  for (size_t i = 0; i < in.size(); ++i) {
    if (i < 2) r.ring[i] = in[i];
    else r.line.insert(r.line.begin() + (std::ptrdiff_t)(i - 2), in[i]);
  }
  clear_switch(r);
  clear_choices(r);
  r.streak_id = kNone; r.streak = 0;
}

// Keeps the ring full when it can be: between games an empty slot takes the front of the line (during
// a game the ring is the game's). A doubles ring that cannot fill four slots goes on as a free-for-all
// when three remain and every build plays one, else as singles; a free-for-all under kFfaMin goes to
// singles.
void seat_bots(Room& r);
void normalize(Room& r) {
  const int n = ring_size(r);
  if (!r.in_game) {
    seat_bots(r);
    for (int s = 0; s < n; ++s)
      if (r.ring[s] == kNone && !r.line.empty() && may_fight(r, r.line.front())) { r.ring[s] = pop_front(r); r.tenure[s] = 0; }
    seat_bots(r);
  }
  if (doubles(r) && fighters(r) < 4 && !r.in_game) {
    if (r.ffa_allowed && fighters(r) >= kFfaMin) {
      r.kind = Ring::Ffa;
      compact_ring(r);
      clear_switch(r);
      clear_choices(r);
      r.streak_id = kNone; r.streak = 0;
    } else {
      shrink_to_singles(r);
    }
  } else if (ffa(r) && !r.in_game) {
    compact_ring(r);
    if (fighters(r) < kFfaMin) shrink_to_singles(r);
  }
  // Doubles with two people and their two bots: the people are one team, the bots the other (slots 0-1
  // and 2-3). Seated as they came in, each person had their own bot as partner, and the players had no
  // way to team up against the bots (daretodair, 11-25).
  if (doubles(r) && !r.in_game && fighters(r) == 4 && people_in_ring(r) == 2) {
    uint8_t ring[4]; uint16_t tenure[4];
    int k = 0;
    for (int pass = 0; pass < 2; ++pass)
      for (int s = 0; s < 4; ++s) {
        const Member* m = find(r, r.ring[s]);
        if (m && is_bot(*m) == (pass == 1)) { ring[k] = r.ring[s]; tenure[k] = r.tenure[s]; ++k; }
      }
    if (k == 4) for (int s = 0; s < 4; ++s) { r.ring[s] = ring[s]; r.tenure[s] = tenure[s]; }
  }
  // Someone who asked, or was chosen to come in, and got into the ring anyway: that is settled.
  if (r.asker != kNone && ring_slot(r, r.asker) >= 0) { r.asker = kNone; r.refused = 0; }
  if (r.incoming != kNone && ring_slot(r, r.incoming) >= 0) r.incoming = kNone;
  if (r.leaving_slot != kNone && (r.leaving_slot >= ring_size(r) || r.ring[r.leaving_slot] == kNone)) {
    r.leaving_slot = kNone; r.incoming = kNone; r.leaver_plays_winner = false;
  }
  if (r.switch_pending && switch_voters(r).empty()) clear_switch(r);
}

// Pro AIs between games: one whose owner is out of the ring (or in a singles ring) goes to the front of the
// line; a free slot takes the first in line who may fight (people, and Pro AIs whose owner is in); a ring
// with fewer than two people trades its last Pro AI for the first person waiting.
void seat_bots(Room& r) {
  if (r.in_game) return;
  const int n = ring_size(r);
  for (int s = 0; s < n; ++s) {
    if (r.ring[s] == kNone || may_fight(r, r.ring[s])) continue;
    r.line.insert(r.line.begin(), r.ring[s]);
    r.ring[s] = kNone; r.tenure[s] = 0;
  }
  for (int pass = 0; pass < 2; ++pass)   // a person seated in pass 0 lets their Pro AI in, in pass 1
    for (int s = 0; s < n; ++s) {
      if (r.ring[s] != kNone) continue;
      for (size_t i = 0; i < r.line.size(); ++i)
        if (may_fight(r, r.line[i])) {
          r.ring[s] = r.line[i]; r.tenure[s] = 0;
          r.line.erase(r.line.begin() + (std::ptrdiff_t)i);
          break;
        }
    }
  if (people_in_ring(r) < kMinPeople && fighters(r) > people_in_ring(r)) {
    for (size_t i = 0; i < r.line.size(); ++i) {
      const Member* m = find(r, r.line[i]);
      if (!m || is_bot(*m)) continue;
      for (int s = n - 1; s >= 0; --s) {
        const Member* b = find(r, r.ring[s]);
        if (!b || !is_bot(*b)) continue;
        const uint8_t person = r.line[i];
        r.line.erase(r.line.begin() + (std::ptrdiff_t)i);
        r.line.insert(r.line.begin(), r.ring[s]);
        r.ring[s] = person; r.tenure[s] = 0;
        break;
      }
      break;
    }
  }
  compact_ring(r);
}

bool switch_complete(const Room& r) {
  if (!r.switch_pending) return false;
  const std::vector<uint8_t> voters = switch_voters(r);
  if (voters.empty()) return false;
  for (uint8_t v : voters) if (!(r.switch_votes & (1u << v))) return false;
  return true;
}

void apply_switch(Room& r) {
  const Ring to = r.switch_to;
  const std::vector<uint8_t> v = lineup(r, to);
  clear_switch(r);
  if (v.empty() || to == r.kind) return;
  clear_choices(r);
  r.play_winner_next = false;
  r.streak_id = kNone; r.streak = 0;
  if (to == Ring::Singles) {
    shrink_to_singles(r);
  } else if (to == Ring::Doubles) {
    if (v.size() != 4) return;
    // The first two fighters lead the teams; the next two join them in order.
    for (uint8_t id : v) remove_from_line(r, id);
    r.ring[0] = v[0]; r.ring[1] = v[2]; r.ring[2] = v[1]; r.ring[3] = v[3];
    r.kind = Ring::Doubles;
  } else {
    if (v.size() < (size_t)kFfaMin || v.size() > (size_t)kRingMax) return;
    for (uint8_t id : v) remove_from_line(r, id);
    for (int s = 0; s < kRingMax; ++s) r.ring[s] = s < (int)v.size() ? v[s] : kNone;
    r.kind = Ring::Ffa;
  }
  reset_tenure(r);
}

// The losers of `winner_team` go out and the front of the line comes in, one slot at a time; a slot
// stays when nobody is waiting. Only members waiting before the game ended come in: a loser who just
// went to the back of the line never takes another loser's slot.
void rotate_losers(Room& r, uint8_t winner_team) {
  const int n = ring_size(r);
  size_t waiting = r.line.size();
  for (int s = 0; s < n && waiting > 0; ++s) {
    if (r.ring[s] == kNone || team_of_slot(r, s) == (int)winner_team) continue;
    const uint8_t in = pop_front(r);
    --waiting;
    r.line.push_back(r.ring[s]);
    r.ring[s] = in;
    r.tenure[s] = 0;
  }
}

// Take turns: whoever has been in longest steps out (a team in doubles); in a tie, the losers. In a
// free-for-all one fighter steps out for the front of the line: the longest in, in a tie a loser (the
// winner last), in a tie still the lower slot.
void rotate_longest(Room& r, uint8_t winner_team) {
  if (ffa(r)) {
    if (r.line.empty()) return;
    int out = -1;
    for (int s = 0; s < ring_size(r); ++s) {
      if (r.ring[s] == kNone) continue;
      if (out < 0 || r.tenure[s] > r.tenure[out] || (r.tenure[s] == r.tenure[out] && out == (int)winner_team && s != (int)winner_team))
        out = s;
    }
    if (out < 0) return;
    const uint8_t in = pop_front(r);
    r.line.push_back(r.ring[out]);
    r.ring[out] = in;
    r.tenure[out] = 0;
    return;
  }
  uint16_t longest[2] = {0, 0};
  for (int s = 0; s < ring_size(r); ++s)
    longest[team_of_slot(r, s)] = std::max(longest[team_of_slot(r, s)], r.tenure[s]);
  const int out = longest[0] > longest[1] ? 0 : longest[1] > longest[0] ? 1 : (winner_team == 0 ? 1 : 0);
  rotate_losers(r, (uint8_t)(out == 0 ? 1 : 0));   // "the winner" here is the team that stays
}

void bump(Room& r) { ++r.rev; }

// A fighter's spot changes hands: the one asking (or the front of the line) comes in, the fighter
// goes to the back of the line (the front, and plays the winner next, when they asked to).
void apply_leaving(Room& r) {
  const uint8_t s = r.leaving_slot;
  if (s == kNone || s >= ring_size(r)) { r.leaving_slot = kNone; return; }
  const uint8_t leaver = r.ring[s];
  uint8_t in = r.incoming;
  if (in != kNone && role(r, in) == Role::Fighter) in = kNone;
  if (in == kNone) in = pop_front(r); else remove_from_line(r, in);
  if (in != kNone) {
    r.ring[s] = in;
    r.tenure[s] = 0;
    if (r.streak_id == leaver) { r.streak_id = kNone; r.streak = 0; }
    if (r.leaver_plays_winner) { r.line.insert(r.line.begin(), leaver); r.play_winner_next = true; }
    else r.line.push_back(leaver);
  }
  r.leaving_slot = kNone; r.incoming = kNone; r.leaver_plays_winner = false;
}

void put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back((uint8_t)(v >> 8)); o.push_back((uint8_t)v); }
void put32(std::vector<uint8_t>& o, uint32_t v) { for (int s = 24; s >= 0; s -= 8) o.push_back((uint8_t)(v >> s)); }
void put_str(std::vector<uint8_t>& o, const std::string& s) { o.push_back((uint8_t)s.size()); o.insert(o.end(), s.begin(), s.end()); }

struct Reader {
  const uint8_t* p; size_t n, at = 0; bool ok = true;
  uint8_t u8() { if (at + 1 > n) { ok = false; return 0; } return p[at++]; }
  uint16_t u16() { if (at + 2 > n) { ok = false; return 0; } uint16_t v = (uint16_t)(p[at] << 8 | p[at + 1]); at += 2; return v; }
  uint32_t u32() { if (at + 4 > n) { ok = false; return 0; }
    uint32_t v = (uint32_t)p[at] << 24 | (uint32_t)p[at + 1] << 16 | (uint32_t)p[at + 2] << 8 | p[at + 3]; at += 4; return v; }
  std::string str(size_t max) { const uint8_t len = u8(); if (!ok || len > max || at + len > n) { ok = false; return {}; }
    std::string s((const char*)p + at, len); at += len; return s; }
};

}  // namespace

// ---------------------------------------------------------------- reading
const char* ring_name(Ring kind) {
  switch (kind) {
    case Ring::Doubles: return "Doubles";
    case Ring::Ffa: return "Free-for-all";
    default: return "Singles";
  }
}

bool set_character(Room& r, uint8_t id, uint8_t character, uint8_t costume) {
  if (character != kNone && character >= 33) return false;   // the external ids (a mod disc has a few more)
  if (costume > 15) return false;
  for (Member& m : r.members) {
    if (m.id != id) continue;
    if (m.character == character && m.costume == costume) return false;
    m.character = character;
    m.costume = costume;
    bump(r);
    return true;
  }
  return false;
}

const Member* find(const Room& r, uint8_t id) {
  for (const Member& m : r.members) if (m.id == id) return &m;
  return nullptr;
}
int ring_size(const Room& r) { return r.kind == Ring::Singles ? 2 : kRingMax; }
int fighters(const Room& r) {
  int n = 0;
  for (int s = 0; s < ring_size(r); ++s) if (r.ring[s] != kNone) ++n;
  return n;
}
bool ring_ready(const Room& r) {
  const int n = fighters(r);
  if (people_in_ring(r) < kMinPeople) return false;   // one machine is no match
  if (ffa(r)) {
    // Full, or nobody waiting who may come in (a Pro AI whose owner sits out waits for its owner).
    bool waiting = false;
    for (uint8_t id : r.line) waiting = waiting || may_fight(r, id);
    return n >= kFfaMin && (n == kRingMax || !waiting);
  }
  return n == ring_size(r);
}
int ring_slot(const Room& r, uint8_t id) {
  if (id == kNone) return -1;
  for (int s = 0; s < ring_size(r); ++s) if (r.ring[s] == id) return s;
  return -1;
}
int team_of_slot(const Room& r, int slot) { return doubles(r) ? slot / 2 : slot; }
int line_place(const Room& r, uint8_t id) {
  for (size_t i = 0; i < r.line.size(); ++i) if (r.line[i] == id) return (int)i;
  return -1;
}
Role role(const Room& r, uint8_t id) {
  if (!find(r, id)) return Role::Absent;
  if (ring_slot(r, id) >= 0) return Role::Fighter;
  if (line_place(r, id) >= 0) return Role::InLine;
  return Role::Watching;
}
uint8_t owner(const Room& r) {
  uint8_t best = kNone;
  for (const Member& m : r.members) if (!is_bot(m) && m.id < best) best = m.id;
  return best;
}
uint8_t bot_of(const Room& r, uint8_t owner_id) {
  for (const Member& m : r.members) if (is_bot(m) && m.bot_of == owner_id) return m.id;
  return kNone;
}
int people(const Room& r) {
  int n = 0;
  for (const Member& m : r.members) n += is_bot(m) ? 0 : 1;
  return n;
}
uint8_t next_up(const Room& r) {
  if (r.incoming != kNone) return r.incoming;
  if (r.asker != kNone) return r.asker;
  return r.line.empty() ? kNone : r.line.front();
}

bool switch_offered(const Room& r, Ring kind, std::string* why) {
  const auto no = [&](const char* reason) { if (why) *why = reason; return false; };
  if ((uint8_t)kind >= kRingKinds) return no("");
  if (kind == r.kind) return no("");
  if (kind == Ring::Doubles && r.members.size() < 4) return no("Doubles needs 4 players");
  if (kind == Ring::Ffa) {
    if (r.members.size() < (size_t)kFfaMin) return no("Free-for-all needs 3 players");
    if (!r.ffa_allowed) return no("Everyone needs the latest Melee Unlocked");
  }
  if (switch_voters(r, kind).empty()) return no(kind == Ring::Doubles ? "Doubles needs 4 players" : "Free-for-all needs 3 players");
  if (why) why->clear();
  return true;
}

// Who would be in a new ring of `kind`: the fighters, then the line, up to its size; someone just
// watching only when the ring would be short without them. A Pro AI only with its owner in it, never in
// singles. Empty when the ring would be short, or have fewer than two people.
std::vector<uint8_t> lineup(const Room& r, Ring kind) {
  std::vector<uint8_t> v;
  if ((uint8_t)kind >= kRingKinds || kind == r.kind) return v;
  if (kind == Ring::Ffa && !r.ffa_allowed) return v;
  const size_t want = kind == Ring::Singles ? 2 : kind == Ring::Doubles ? 4 : (size_t)kRingMax;
  const size_t least = kind == Ring::Singles ? 2 : kind == Ring::Doubles ? 4 : (size_t)kFfaMin;
  const auto person = [&](uint8_t id) { const Member* m = find(r, id); return m && !is_bot(*m); };
  // People: the fighters, then the line, up to the ring's size; someone just watching only while the
  // ring would be short (counting the Pro AIs that will come in with their owners).
  if (kind == Ring::Singles) {
    // Back to singles: everyone fighting now has a say (the ring shrinks to its first two).
    for (int s = 0; s < ring_size(r); ++s) if (person(r.ring[s])) v.push_back(r.ring[s]);
    return (int)v.size() >= kMinPeople ? v : std::vector<uint8_t>{};
  }
  for (int s = 0; s < ring_size(r); ++s) if (person(r.ring[s]) && v.size() < want) v.push_back(r.ring[s]);
  {
    for (uint8_t id : r.line) if (person(id) && v.size() < want) v.push_back(id);
  }
  const auto bots_with_owners = [&](const std::vector<uint8_t>& people_in) {
    size_t n = 0;
    if (kind == Ring::Singles) return n;
    for (const Member& m : r.members)
      if (is_bot(m) && std::find(people_in.begin(), people_in.end(), m.bot_of) != people_in.end()) ++n;
    return n;
  };
  for (const Member& m : r.members) {
    if (is_bot(m) || std::find(v.begin(), v.end(), m.id) != v.end()) continue;
    if (v.size() + bots_with_owners(v) >= least || v.size() >= want) break;
    v.push_back(m.id);
  }
  // Then each Pro AI whose owner is in, in the order they wait (the ring's, then the line's), while there is room.
  if (kind != Ring::Singles) {
    std::vector<uint8_t> order;
    for (int s = 0; s < ring_size(r); ++s) order.push_back(r.ring[s]);
    for (uint8_t id : r.line) order.push_back(id);
    for (const Member& m : r.members) order.push_back(m.id);
    for (uint8_t id : order) {
      const Member* m = find(r, id);
      if (!m || !is_bot(*m) || v.size() >= want || std::find(v.begin(), v.end(), id) != v.end()) continue;
      if (std::find(v.begin(), v.end(), m->bot_of) != v.end()) v.push_back(id);
    }
  }
  int people_in = 0;
  for (uint8_t id : v) people_in += person(id) ? 1 : 0;
  if (v.size() < least || people_in < kMinPeople) return {};
  return v;
}
std::vector<uint8_t> switch_voters(const Room& r, Ring kind) {
  std::vector<uint8_t> v = lineup(r, kind);
  v.erase(std::remove_if(v.begin(), v.end(), [&](uint8_t id) { const Member* m = find(r, id); return !m || is_bot(*m); }), v.end());
  return v;
}
std::vector<uint8_t> switch_voters(const Room& r) {
  return r.switch_pending ? switch_voters(r, r.switch_to) : std::vector<uint8_t>{};
}

bool valid(const Room& r) {
  if (r.members.size() > (size_t)kMaxMembers || (uint8_t)r.mode > 2 || (uint8_t)r.kind >= kRingKinds || (uint8_t)r.switch_to >= kRingKinds)
    return false;
  uint32_t seen = 0;
  for (size_t i = 0; i < r.members.size(); ++i) {
    const Member& m = r.members[i];
    if (m.id >= kMaxMembers || (seen & (1u << m.id))) return false;
    seen |= 1u << m.id;
    if (m.code.empty() || m.code.size() > kMaxId || m.name.size() > kMaxName) return false;
    for (size_t j = 0; j < i; ++j) if (r.members[j].code == m.code) return false;
  }
  for (const Member& m : r.members) {
    if (!is_bot(m)) continue;
    const Member* o = find(r, m.bot_of);
    if (!o || is_bot(*o) || bot_of(r, m.bot_of) != m.id || !r.ffa_allowed) return false;
  }
  uint32_t placed = 0;
  auto place = [&](uint8_t id) {
    if (id >= kMaxMembers || !(seen & (1u << id)) || (placed & (1u << id))) return false;
    placed |= 1u << id;
    return true;
  };
  const int n = ring_size(r);
  for (int s = 0; s < kRingMax; ++s) {
    if (r.ring[s] == kNone) continue;
    if (s >= n || !place(r.ring[s])) return false;
  }
  for (uint8_t id : r.line) if (!place(id)) return false;
  if (doubles(r)) {
    if (r.members.size() < 4) return false;
    for (int s = 0; s < 4; ++s) if (r.ring[s] == kNone) return false;
  }
  if (ffa(r)) {
    if (!r.ffa_allowed || r.members.size() < (size_t)kFfaMin || fighters(r) < kFfaMin) return false;
  }
  auto member_or_none = [&](uint8_t id) { return id == kNone || (id < kMaxMembers && (seen & (1u << id))); };
  if (!member_or_none(r.asker) || !member_or_none(r.incoming) || !member_or_none(r.streak_id) || !member_or_none(r.declined))
    return false;
  if (r.asker != kNone && is_fighting(r, r.asker)) return false;
  if (r.incoming != kNone && is_fighting(r, r.incoming)) return false;
  if (r.refused >> n) return false;
  if (r.leaving_slot != kNone && (r.leaving_slot >= n || r.ring[r.leaving_slot] == kNone)) return false;
  if (r.switch_votes & ~seen) return false;
  if (!r.switch_pending && r.switch_votes) return false;
  if (r.switch_pending && (r.switch_to == r.kind || (r.switch_to == Ring::Ffa && !r.ffa_allowed))) return false;
  return true;
}

// ---------------------------------------------------------------- changes
uint8_t join(Room& r, const std::string& code_in, const std::string& name_in) {
  if (r.members.size() >= (size_t)kMaxMembers) return kNone;
  const bool slippi = code_in.find('#') != std::string::npos;   // a connect code; else a YouGame player id
  const std::string code = clean(code_in, slippi ? kMaxCode : kMaxId, slippi), name = clean(name_in, kMaxName, false);
  if (code.empty()) return kNone;
  for (const Member& m : r.members) if (m.code == code) return kNone;
  uint8_t id = 0;
  while (find(r, id)) ++id;
  Member m;
  m.id = id; m.code = code; m.name = name.empty() ? code : name;
  r.members.push_back(m);
  r.line.push_back(id);
  normalize(r);
  bump(r);
  return id;
}

bool leave(Room& r, uint8_t id) {
  if (!find(r, id)) return false;
  if (const uint8_t bot = bot_of(r, id); bot != kNone) leave(r, bot);   // a person's Pro AI goes with them
  r.members.erase(std::remove_if(r.members.begin(), r.members.end(), [&](const Member& m) { return m.id == id; }), r.members.end());
  const int s = ring_slot(r, id);
  if (s >= 0) { r.ring[s] = kNone; r.tenure[s] = 0; }
  remove_from_line(r, id);
  if (r.asker == id) { r.asker = kNone; r.refused = 0; }
  if (r.incoming == id) r.incoming = kNone;
  if (r.declined == id) r.declined = kNone;
  if (r.streak_id == id) { r.streak_id = kNone; r.streak = 0; }
  r.switch_votes &= (uint16_t)~(1u << id);
  if (r.switch_pending) clear_switch(r);   // the people voting changed
  if (r.members.empty()) { r = Room{r.rev}; r.mode = Mode::WinnerStays; }
  normalize(r);
  bump(r);
  return true;
}

bool has_cpu_bot(const Room& r) {
  for (const Member& m : r.members) if (is_cpu_bot(m)) return true;
  return false;
}
uint8_t add_bot(Room& r, uint8_t owner_id, uint8_t character, uint8_t pro, uint8_t level) {
  const Member* o = find(r, owner_id);
  if (!o || is_bot(*o) || !r.ffa_allowed || bot_of(r, owner_id) != kNone || pro > 0x7F) return kNone;
  if (r.members.size() >= (size_t)kMaxMembers) return kNone;
  uint8_t id = 0;
  while (find(r, id)) ++id;
  Member m;
  m.id = id;
  m.code = "PROAI#" + std::to_string(owner_id);   // never a Slippi code (digits only after the #)
  m.name = clean(o->name, kMaxName - 6, false) + " (AI)";
  m.bot_of = owner_id;
  m.pro = pro;
  m.level = level < kBotLevelMin || level > kBotLevelMax ? kBotLevelMax : level;
  m.character = character;
  r.members.push_back(m);
  r.line.push_back(id);
  normalize(r);
  bump(r);
  return id;
}
bool remove_bot(Room& r, uint8_t owner_id) {
  const uint8_t bot = bot_of(r, owner_id);
  return bot != kNone && leave(r, bot);
}

bool set_mode(Room& r, Mode mode) {
  if ((uint8_t)mode > 2 || r.mode == mode) return false;
  r.mode = mode;
  bump(r);
  return true;
}

bool set_ffa_allowed(Room& r, bool allowed) {
  if (r.ffa_allowed == allowed) return false;
  r.ffa_allowed = allowed;
  if (!allowed) {
    // A build before Pro AIs came in: they leave (it could not see them).
    std::vector<uint8_t> bots;
    for (const Member& m : r.members) if (is_bot(m)) bots.push_back(m.id);
    for (uint8_t id : bots) leave(r, id);
    if (r.switch_pending && r.switch_to == Ring::Ffa) clear_switch(r);
    if (ffa(r) && !r.in_game) shrink_to_singles(r);
    normalize(r);
  }
  bump(r);
  return true;
}

bool toggle_line(Room& r, uint8_t id) {
  const Role now = role(r, id);
  if (now == Role::Absent || now == Role::Fighter) return false;
  if (now == Role::InLine) {
    remove_from_line(r, id);
    if (r.incoming == id) r.incoming = kNone;
    if (r.asker == id) { r.asker = kNone; r.refused = 0; }
  } else {
    r.line.push_back(id);
  }
  normalize(r);
  bump(r);
  return true;
}

bool ask(Room& r, uint8_t id) {
  const Role now = role(r, id);
  if (now == Role::Absent || now == Role::Fighter) return false;
  if (r.asker != kNone || r.leaving_slot != kNone) return false;   // one change per game
  r.asker = id;
  r.refused = 0;
  if (r.declined == id) r.declined = kNone;
  bump(r);
  return true;
}

bool answer(Room& r, uint8_t fighter, Answer a) {
  const int s = ring_slot(r, fighter);
  if (s < 0 || r.asker == kNone || (r.refused & (1u << s)) || (uint8_t)a > 2) return false;
  if (a == Answer::No) {
    r.refused |= (uint8_t)(1u << s);
    bool all = true;
    for (int k = 0; k < ring_size(r); ++k) if (r.ring[k] != kNone && !(r.refused & (1u << k))) all = false;
    if (all) { r.declined = r.asker; r.asker = kNone; r.refused = 0; }
  } else {
    r.leaving_slot = (uint8_t)s;
    r.incoming = r.asker;
    r.leaver_plays_winner = a == Answer::YesAndPlayWinner;
    r.asker = kNone;
    r.refused = 0;
    if (!r.in_game) { apply_leaving(r); normalize(r); }   // between games: at once, for the next game
  }
  bump(r);
  return true;
}

bool give_up(Room& r, uint8_t fighter, bool play_winner) {
  const int s = ring_slot(r, fighter);
  if (s < 0) return false;
  if (r.leaving_slot == s) {   // again: take it back
    r.leaving_slot = kNone; r.incoming = kNone; r.leaver_plays_winner = false;
    bump(r);
    return true;
  }
  if (r.leaving_slot != kNone) return false;
  uint8_t in = r.asker;
  if (in == kNone && r.line.empty()) return false;   // nobody to take the spot
  r.leaving_slot = (uint8_t)s;
  r.incoming = in;
  r.leaver_plays_winner = play_winner;
  r.asker = kNone; r.refused = 0;
  if (!r.in_game) { apply_leaving(r); normalize(r); }   // between games: at once, for the next game
  bump(r);
  return true;
}

bool propose_switch(Room& r, uint8_t id, Ring kind) {
  if (!find(r, id) || r.switch_pending) return false;
  if (!switch_offered(r, kind)) return false;
  const std::vector<uint8_t> voters = switch_voters(r, kind);
  if (voters.empty()) return false;
  r.switch_pending = true;
  r.switch_to = kind;
  r.switch_votes = std::find(voters.begin(), voters.end(), id) != voters.end() ? (uint16_t)(1u << id) : 0;
  if (switch_complete(r) && !r.in_game) { apply_switch(r); normalize(r); }
  bump(r);
  return true;
}

bool propose_switch(Room& r, uint8_t id) {
  return propose_switch(r, id, doubles(r) ? Ring::Singles : Ring::Doubles);
}

bool vote_switch(Room& r, uint8_t id, bool yes) {
  if (!r.switch_pending) return false;
  const std::vector<uint8_t> voters = switch_voters(r);
  if (std::find(voters.begin(), voters.end(), id) == voters.end()) return false;
  if (!yes) {
    clear_switch(r);
  } else {
    if (r.switch_votes & (1u << id)) return false;
    r.switch_votes |= (uint16_t)(1u << id);
    if (switch_complete(r) && !r.in_game) { apply_switch(r); normalize(r); }
  }
  bump(r);
  return true;
}

bool game_start(Room& r) {
  if (r.in_game) return false;
  if (!ring_ready(r)) return false;
  r.in_game = true;
  bump(r);
  return true;
}

bool game_end(Room& r, uint8_t winner_team) {
  if (!r.in_game) return false;
  if (winner_team != kNone) {
    if (doubles(r)) { if (winner_team > 1) return false; }
    else if (winner_team >= (uint8_t)ring_size(r) || r.ring[winner_team] == kNone) return false;
  }
  r.in_game = false;
  r.declined = kNone;
  if (switch_complete(r)) {   // everyone agreed during the game
    if (winner_team != kNone) {
      ++r.games;
      for (int s = 0; s < ring_size(r); ++s) {
        Member* m = const_cast<Member*>(find(r, r.ring[s]));
        if (m) (team_of_slot(r, s) == winner_team ? m->wins : m->losses)++;
      }
    }
    apply_switch(r);
    normalize(r);
    bump(r);
    return true;
  }
  if (winner_team == kNone) { normalize(r); bump(r); return true; }   // no contest: the lineup stays as it was
  ++r.games;
  const int n = ring_size(r);
  for (int s = 0; s < n; ++s) {
    if (r.ring[s] == kNone) continue;
    Member* m = const_cast<Member*>(find(r, r.ring[s]));
    if (m) (team_of_slot(r, s) == winner_team ? m->wins : m->losses)++;
    ++r.tenure[s];
  }
  const uint8_t leader = r.ring[doubles(r) ? winner_team * 2 : winner_team];
  if (r.streak_id == leader) ++r.streak; else { r.streak_id = leader; r.streak = 1; }

  if (r.leaving_slot != kNone) {
    // A fighter gave up their spot during the game: that change replaces the mode's own rotation.
    apply_leaving(r);
  } else if (r.play_winner_next) {
    r.play_winner_next = false;
    rotate_losers(r, winner_team);
  } else if (r.mode == Mode::WinnerStays) {
    rotate_losers(r, winner_team);
  } else if (r.mode == Mode::TakeTurns) {
    rotate_longest(r, winner_team);
  }
  // A streak belongs to whoever is still in the ring.
  if (r.streak_id != kNone && ring_slot(r, r.streak_id) < 0) { r.streak_id = kNone; r.streak = 0; }
  normalize(r);
  bump(r);
  return true;
}

// ---------------------------------------------------------------- wire form
std::vector<uint8_t> encode(const Room& r, uint8_t version) {
  const bool legacy = version <= kWireLegacy;
  const bool levels = version >= kWireVersion;   // wire 4: each member's bot level
  std::vector<uint8_t> o;
  o.push_back(legacy ? kWireLegacy : levels ? kWireVersion : kWireFfa);
  put32(o, r.rev);
  o.push_back((uint8_t)r.mode);
  o.push_back((uint8_t)((doubles(r) ? 1 : 0) | (r.in_game ? 2 : 0) | (r.leaver_plays_winner ? 4 : 0) |
                        (r.switch_pending ? 8 : 0) | (r.play_winner_next ? 16 : 0) | (!legacy && r.ffa_allowed ? 32 : 0)));
  if (!legacy) { o.push_back((uint8_t)r.kind); o.push_back((uint8_t)r.switch_to); }
  put32(o, r.games);
  for (int s = 0; s < kRingMax; ++s) { o.push_back(r.ring[s]); put16(o, r.tenure[s]); }
  o.push_back(r.streak_id); put16(o, r.streak);
  o.push_back(r.asker); o.push_back(r.refused); o.push_back(r.declined);
  o.push_back(r.leaving_slot); o.push_back(r.incoming);
  put16(o, r.switch_votes);
  o.push_back((uint8_t)r.members.size());
  for (const Member& m : r.members) {
    o.push_back(m.id); put16(o, m.wins); put16(o, m.losses); o.push_back(m.character); o.push_back(m.costume);
    put_str(o, m.code); put_str(o, m.name);
    if (!legacy) { o.push_back(m.bot_of); o.push_back(m.pro); }
    if (levels) o.push_back(m.level);
  }
  o.push_back((uint8_t)r.line.size());
  o.insert(o.end(), r.line.begin(), r.line.end());
  return o;
}

bool decode(const uint8_t* data, size_t size, Room& out) {
  if (!data) return false;
  Reader in{data, size};
  const uint8_t version = in.u8();
  if (version != kWireLegacy && version != kWireFfa && version != kWireVersion) return false;
  const bool legacy = version == kWireLegacy, levels = version == kWireVersion;
  Room r;
  r.rev = in.u32();
  r.mode = (Mode)in.u8();
  const uint8_t flags = in.u8();
  if (flags >> (legacy ? 5 : 6)) return false;
  r.in_game = flags & 2; r.leaver_plays_winner = flags & 4;
  r.switch_pending = flags & 8; r.play_winner_next = flags & 16;
  if (legacy) {
    r.kind = (flags & 1) ? Ring::Doubles : Ring::Singles;
    r.switch_to = r.switch_pending ? (doubles(r) ? Ring::Singles : Ring::Doubles) : Ring::Singles;
    r.ffa_allowed = false;
  } else {
    r.ffa_allowed = flags & 32;
    r.kind = (Ring)in.u8();
    r.switch_to = (Ring)in.u8();
    if (((flags & 1) != 0) != (r.kind == Ring::Doubles)) return false;
  }
  r.games = in.u32();
  for (int s = 0; s < kRingMax; ++s) { r.ring[s] = in.u8(); r.tenure[s] = in.u16(); }
  r.streak_id = in.u8(); r.streak = in.u16();
  r.asker = in.u8(); r.refused = in.u8(); r.declined = in.u8();
  r.leaving_slot = in.u8(); r.incoming = in.u8();
  r.switch_votes = in.u16();
  const uint8_t count = in.u8();
  if (!in.ok || count > kMaxMembers) return false;
  for (uint8_t i = 0; i < count; ++i) {
    Member m;
    m.id = in.u8(); m.wins = in.u16(); m.losses = in.u16(); m.character = in.u8(); m.costume = in.u8();
    m.code = in.str(kMaxId); m.name = in.str(kMaxName);
    if (!legacy) { m.bot_of = in.u8(); m.pro = in.u8(); }
    if (levels) m.level = in.u8();
    if (m.level < kBotLevelMin || m.level > kBotLevelMax) return false;
    if (!levels && m.pro == kPro20xx) return false;   // an older wire never carries a 20XX CPU
    if (!in.ok) return false;
    r.members.push_back(std::move(m));
  }
  const uint8_t waiting = in.u8();
  if (!in.ok || waiting > kMaxMembers || in.at + waiting != size) return false;
  r.line.assign(data + in.at, data + in.at + waiting);
  if (!valid(r)) return false;
  out = std::move(r);
  return true;
}

// ---------------------------------------------------------------- feed and links
std::vector<std::pair<uint8_t, uint8_t>> feed_tree(const Room& r, int fanout) {
  if (fanout < 1) fanout = 1;
  std::vector<std::pair<uint8_t, uint8_t>> edges;
  // Senders by level: the fighters first, then the watchers who already have the feed.
  std::vector<std::vector<uint8_t>> levels(1);
  std::vector<int> load(kMaxMembers, 0);
  const auto person = [&](uint8_t id) { const Member* m = find(r, id); return m && !is_bot(*m); };
  for (int s = 0; s < ring_size(r); ++s) if (person(r.ring[s])) levels[0].push_back(r.ring[s]);
  if (levels[0].empty()) return edges;
  // Receivers: the line first (the next to play gets the shortest path), then the watchers by join order.
  // Pro AIs have no machine: they watch nothing.
  std::vector<uint8_t> receivers;
  for (uint8_t id : r.line) if (person(id)) receivers.push_back(id);
  for (const Member& m : r.members)
    if (!is_bot(m) && role(r, m.id) == Role::Watching) receivers.push_back(m.id);
  for (uint8_t who : receivers) {
    for (size_t level = 0; level < levels.size(); ++level) {
      uint8_t best = kNone;
      for (uint8_t sender : levels[level])
        if (load[sender] < fanout && (best == kNone || load[sender] < load[best])) best = sender;
      if (best == kNone) continue;
      ++load[best];
      edges.push_back({who, best});
      if (level + 1 >= levels.size()) levels.emplace_back();
      levels[level + 1].push_back(who);
      break;
    }
  }
  return edges;
}

int feed_hops(const Room& r, uint8_t id, int fanout) {
  if (!find(r, id)) return -1;
  if (ring_slot(r, id) >= 0) return 0;
  const auto edges = feed_tree(r, fanout);
  int hops = 0;
  for (uint8_t at = id; hops <= kMaxMembers;) {
    auto it = std::find_if(edges.begin(), edges.end(), [&](const std::pair<uint8_t, uint8_t>& e) { return e.first == at; });
    if (it == edges.end()) return ring_slot(r, at) >= 0 ? hops : -1;
    ++hops;
    at = it->second;
    if (ring_slot(r, at) >= 0) return hops;
  }
  return -1;
}

std::vector<std::pair<uint8_t, uint8_t>> wanted_links(const Room& r, int fanout) {
  std::vector<std::pair<uint8_t, uint8_t>> links;
  auto add = [&](uint8_t a, uint8_t b) {
    if (a == kNone || b == kNone || a == b) return;
    links.push_back({std::min(a, b), std::max(a, b)});
  };
  const auto person = [&](uint8_t id) { const Member* m = find(r, id); return m && !is_bot(*m); };
  for (int a = 0; a < ring_size(r); ++a)
    for (int b = a + 1; b < ring_size(r); ++b)
      if (person(r.ring[a]) && person(r.ring[b])) add(r.ring[a], r.ring[b]);
  for (const auto& e : feed_tree(r, fanout)) add(e.first, e.second);
  std::sort(links.begin(), links.end());
  links.erase(std::unique(links.begin(), links.end()), links.end());
  return links;
}

std::string arena_by_code_room(const std::string& connect_code) {
  uint64_t h = 1469598103934665603ull;   // FNV-1a 64
  for (char c : connect_code) {
    h ^= (uint8_t)std::toupper((unsigned char)c);
    h *= 1099511628211ull;
  }
  static const char kDigits[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::string room = "mu";
  for (int i = 0; i < 10; ++i) { room += kDigits[h % 36]; h /= 36; }
  return room;
}

}  // namespace slippi::arena
