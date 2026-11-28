// The Friends lobby's room frames (Rooms rooms/src/mu-lobby.ts, frozen v1), for the 4-player Friends
// lobby and the 2-seat Doubles home (Profile below): the game's side of the codec. Outgoing frames are checked exactly as Rooms checks them (validMuFrame: the envelope, each
// op's own keys and nothing else, the ranges), and never go out over 1,024 characters. Incoming frames
// are read strictly where the game relies on a field, and tolerantly elsewhere: Rooms may add keys
// (settingsDigest, cardDigest, inputDelay) before or after this build ships, and a key the game does
// not know never refuses a state or a setup. The frozen setup is kept as Rooms sent it (every key but
// the envelope and matchId), so game-report and finish echo it exactly, future keys included.
//
// Optional, landing in either order with Rooms (each off until Rooms accepts it, never required):
//   hello {settingsDigest, cardDigest}  each [A-Za-z0-9_-]{1,64}, own seat, no rev
//   start {inputDelay 1-9}              frozen into setup (default 2 when absent)
//   setup seats[].settingsDigest/cardDigest, setup.inputDelay
//
// Six seats (Triples, also optional): the room hold and the guest's ticket ask for lobby.seats 6
// (yougame_friends.h). A Rooms that seats six answers with a state of six seats; one that does not
// keeps four, exactly as before. Every Friends state is read as 4 or 6 seats; seats and ports 0-5.
#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace yougame::lobby {
using json = nlohmann::json;

inline constexpr const char* kType = "yg.mu.lobby";
inline constexpr int kVersion = 1;
inline constexpr size_t kFrameMax = 1024;   // Rooms refuses a longer frame as `shape`
inline constexpr int kSeats = 4;      // a v1 Friends room's seats (and the Doubles match room's)
inline constexpr int kSeatsMax = 6;   // a six-seat Friends room's (Triples)
inline constexpr int kCharMax = 25, kColorMax = 5, kTeamMax = 2, kLevelMin = 1, kLevelMax = 9;
inline constexpr int kRuleMin = 1, kRuleMax = 99;   // stocks and minutes
// Stock's minutes may be 0: no timer (Rooms validMuRules, 11-06). Time mode keeps 1-99.
inline constexpr int kStockMinutesNone = 0;
inline constexpr int kInputDelayMin = 1, kInputDelayMax = 9, kInputDelayDefault = 2;
inline constexpr size_t kDigestMax = 64;
// Random draws from these (Rooms LEGAL_STAGES); an explicit stage is 2-32 except 21 and 26.
inline constexpr std::array<int, 6> kRandomStages = {{2, 3, 8, 28, 31, 32}};
// Doubles' own rotation: Pokemon Stadium, Yoshi's Story, Dream Land, Battlefield, Final Destination.
inline constexpr std::array<int, 5> kDoublesStages = {{3, 8, 28, 31, 32}};

// One lobby runtime, two shapes (the same rooms/src/mu-lobby.ts on both sides): the 4-seat Friends
// lobby, and the 2-seat Doubles home (G2: me and my partner; opponents join only when matchmaking
// places the match). Doubles' rules are fixed (stock 4, 8 minutes, teams on, friendly fire on, pause
// off), its start carries no stage (Rooms draws kDoublesStages[seed % 5]) and no input delay (2), and
// it adds `cancel` (stop the search) and the state's `searching`.
enum class Profile { Friends4, Doubles };
inline constexpr int kDoublesSeats = 2;
inline constexpr int seats_of(Profile p) { return p == Profile::Doubles ? kDoublesSeats : kSeats; }

struct Pick {
  int ch = 0, color = 0, team = 0;
  bool operator==(const Pick& o) const { return ch == o.ch && color == o.color && team == o.team; }
  bool operator!=(const Pick& o) const { return !(*this == o); }
};
struct Rules {
  bool teams = false, friendly_fire = false, time = false;   // time: mode "time", else "stock"
  int stocks = 4, minutes = 8;
  bool pause = false;
  bool squad = false;
  bool operator==(const Rules& o) const {
    return teams == o.teams && friendly_fire == o.friendly_fire && time == o.time && stocks == o.stocks &&
           minutes == o.minutes && pause == o.pause && squad == o.squad;
  }
  bool operator!=(const Rules& o) const { return !(*this == o); }
};
enum class Kind { Open, Human, Cpu, Local };

inline bool integer(const json& v, int64_t lo, int64_t hi) {
  if (!v.is_number_integer()) return false;
  if (v.is_number_unsigned() && v.get<uint64_t>() > (uint64_t)INT64_MAX) return false;
  const int64_t n = v.get<int64_t>();
  return n >= lo && n <= hi;
}
inline bool integer_at(const json& o, const char* key, int64_t lo, int64_t hi) {
  const auto it = o.find(key);
  return it != o.end() && integer(*it, lo, hi);
}
inline bool only_keys(const json& o, std::initializer_list<const char*> allowed) {
  for (auto it = o.begin(); it != o.end(); ++it) {
    bool ok = false;
    for (const char* k : allowed) if (it.key() == k) { ok = true; break; }
    if (!ok) return false;
  }
  return true;
}
inline bool stage_ok(int64_t stage) { return stage >= 2 && stage <= 32 && stage != 21 && stage != 26; }
inline bool digest_ok(const std::string& s) {
  if (s.empty() || s.size() > kDigestMax) return false;
  for (unsigned char c : s)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  return true;
}
inline bool digest_at(const json& o, const char* key) {
  const auto it = o.find(key);
  return it != o.end() && it->is_string() && digest_ok(it->get_ref<const std::string&>());
}

// Squad capability and lineups have one codec for state, setup and outgoing frames.
inline bool squad_budget_ok(const json& v) {
  if (!v.is_string()) return false;
  const auto& s = v.get_ref<const std::string&>();
  if (s.size() != 64) return false;
  for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}
inline bool read_lineup(const json& v, std::vector<Pick>* out) {
  if (!v.is_array() || v.size() > 4) return false;
  unsigned seen = 0;
  std::vector<Pick> line;
  for (const auto& x : v) {
    if (!x.is_object() || !only_keys(x, {"char", "color"}) ||
        !integer_at(x, "char", 0, kCharMax) || !integer_at(x, "color", 0, kColorMax)) return false;
    const int ch = x["char"].get<int>();
    if (seen & (1u << ch)) return false;
    seen |= 1u << ch;
    line.push_back({ch, x["color"].get<int>(), 0});
  }
  if (out) *out = std::move(line);
  return true;
}
inline json lineup_json(const std::vector<Pick>& line) {
  json out = json::array();
  for (const auto& p : line) out.push_back({{"char", p.ch}, {"color", p.color}});
  return out;
}
inline bool read_squad_capability(const json& v, int* version, std::string* budget) {
  if (!v.count("squadVersion") && !v.count("squadBudget")) return true;
  if (!integer_at(v, "squadVersion", 3, 3) || !v.count("squadBudget") || !squad_budget_ok(v["squadBudget"])) return false;
  *version = 3; *budget = v["squadBudget"].get<std::string>();
  return true;
}

inline json rules_json(const Rules& r) {
  json out = {{"teams", r.teams}, {"friendlyFire", r.friendly_fire}, {"mode", r.time ? "time" : "stock"},
          {"stocks", r.stocks}, {"minutes", r.minutes}, {"pause", r.pause}};
  if (r.squad) out["squad"] = true;
  return out;
}
// Rooms validMuRules. `exact`: no other key (outgoing, as Rooms checks it); incoming tolerates more.
inline bool read_rules(const json& v, Rules* out, bool exact) {
  if (!v.is_object()) return false;
  if (exact && !only_keys(v, {"teams", "friendlyFire", "mode", "stocks", "minutes", "pause", "squad"})) return false;
  for (const char* k : {"teams", "friendlyFire", "pause"})
    if (!v.count(k) || !v[k].is_boolean()) return false;
  if (!v.count("mode") || !v["mode"].is_string()) return false;
  const auto mode = v["mode"].get<std::string>();
  if (mode != "stock" && mode != "time") return false;
  if (!integer_at(v, "stocks", kRuleMin, kRuleMax) ||
      !integer_at(v, "minutes", mode == "time" ? kRuleMin : kStockMinutesNone, kRuleMax)) return false;
  if (v.count("squad") && !v["squad"].is_boolean()) return false;
  const bool squad = v.value("squad", false);
  if (squad && (v["teams"].get<bool>() || mode != "stock" || v["stocks"] != 4)) return false;
  if (out) {
    out->squad = squad;
    out->teams = v["teams"].get<bool>();
    out->friendly_fire = v["friendlyFire"].get<bool>();
    out->pause = v["pause"].get<bool>();
    out->time = mode == "time";
    out->stocks = (int)v["stocks"].get<int64_t>();
    out->minutes = (int)v["minutes"].get<int64_t>();
  }
  return true;
}
inline bool rules_ok(const Rules& r) { return read_rules(rules_json(r), nullptr, true); }
// Doubles' fixed rules (Rooms freshMuLobby(true)): the select shows them and never changes them.
inline Rules doubles_rules() {
  Rules r;
  r.teams = true; r.friendly_fire = true; r.time = false; r.stocks = 4; r.minutes = 8; r.pause = false;
  return r;
}
// What Rooms sends until it moves to the rules and stages above: friendly fire off, kRandomStages.
// Remove once Rooms has moved and no live build depends on the old pair.
inline Rules doubles_rules_old() {
  Rules r = doubles_rules();
  r.friendly_fire = false;
  return r;
}

// Rooms validMuFrame, plus the optional hello op and start's inputDelay (both refused as `shape` by a
// Rooms that does not have them yet; the session then stops sending them).
inline bool valid_outgoing(const json& v) {
  if (!v.is_object() || v.value("t", json()) != kType || !integer_at(v, "v", kVersion, kVersion) ||
      !v.count("op") || !v["op"].is_string()) return false;
  if (v.count("rev") && !integer(v["rev"], 0, 9007199254740991LL)) return false;   // Number.MAX_SAFE_INTEGER
  const std::string op = v["op"].get<std::string>();
  if (op == "stageSelect") return only_keys(v, {"t", "v", "op", "rev", "open"}) && v.count("rev") && v.count("open") && v["open"].is_boolean();
  if (op == "leave" || op == "cancel") return only_keys(v, {"t", "v", "op", "rev"});
  if (op == "rules") return only_keys(v, {"t", "v", "op", "rev", "rules"}) && v.count("rules") && read_rules(v["rules"], nullptr, true);
  if (op == "start") {
    // Rooms lets stage be absent (Doubles' start: the stage is drawn); a Friends start always sends one.
    if (!only_keys(v, {"t", "v", "op", "rev", "stage", "inputDelay", "queue"})) return false;
    // Doubles only, and only the ranked ask: an unranked start is the frame it always was.
    if (v.count("queue") && (v["queue"] != "ranked" || v.count("stage") || v.count("inputDelay"))) return false;
    if (v.count("inputDelay") && !integer(v["inputDelay"], kInputDelayMin, kInputDelayMax)) return false;
    if (!v.count("stage")) return true;
    const json& s = v["stage"];
    return (s.is_string() && s.get<std::string>() == "random") || (integer(s, 2, 32) && stage_ok(s.get<int64_t>()));
  }
  if (op == "hello") {
    int version = 0; std::string budget;
    return only_keys(v, {"t", "v", "op", "settingsDigest", "cardDigest", "squadVersion", "squadBudget"}) &&
        digest_at(v, "settingsDigest") && digest_at(v, "cardDigest") && read_squad_capability(v, &version, &budget);
  }
  if (!integer_at(v, "seat", 0, kSeatsMax - 1)) return false;
  if (op == "lineup") return only_keys(v, {"t", "v", "op", "rev", "seat", "members"}) && v.count("rev") &&
      v.count("members") && read_lineup(v["members"], nullptr);
  if (op == "seat") {
    if (!only_keys(v, {"t", "v", "op", "rev", "seat", "kind", "level"}) || !v.count("kind") || !v["kind"].is_string()) return false;
    const std::string kind = v["kind"].get<std::string>();
    return kind == "cpu" ? integer_at(v, "level", kLevelMin, kLevelMax) : (kind == "open" || (kind == "local" && v["seat"] == 1 && v.count("rev"))) && !v.count("level");
  }
  if (op == "unpick") return only_keys(v, {"t", "v", "op", "rev", "seat"});
  return (op == "hover" || op == "pick") && only_keys(v, {"t", "v", "op", "rev", "seat", "char", "color", "team"}) &&
         integer_at(v, "char", 0, kCharMax) && integer_at(v, "color", 0, kColorMax) &&
         (!v.count("team") || integer(v["team"], 0, kTeamMax));
}
// The frame's text, or empty when Rooms would refuse it (shape) or it is over 1,024 characters.
inline std::string frame_text(const json& v) {
  if (!valid_outgoing(v)) return {};
  std::string s = v.dump();
  return s.size() <= kFrameMax ? s : std::string();
}

inline json envelope(const char* op) { return {{"t", kType}, {"v", kVersion}, {"op", op}}; }
inline json hover_frame(int seat, const Pick& p) {
  json f = envelope("hover");
  f["seat"] = seat; f["char"] = p.ch; f["color"] = p.color; f["team"] = p.team;
  return f;
}
// A player's own pick and unpick carry no rev (Rooms b82e31058: they never bump it); a CPU seat's do,
// since CPU picks are shared state the host owns.
inline json pick_frame(int seat, const Pick& p, const int64_t* rev) {
  json f = envelope("pick");
  f["seat"] = seat; f["char"] = p.ch; f["color"] = p.color; f["team"] = p.team;
  if (rev) f["rev"] = *rev;
  return f;
}
inline json unpick_frame(int seat, const int64_t* rev) {
  json f = envelope("unpick");
  f["seat"] = seat;
  if (rev) f["rev"] = *rev;
  return f;
}
inline json lineup_frame(int seat, const std::vector<Pick>& line, int64_t rev) {
  json f = envelope("lineup"); f["rev"] = rev; f["seat"] = seat; f["members"] = lineup_json(line); return f;
}
inline json rules_frame(const Rules& r, int64_t rev) { json f = envelope("rules"); f["rev"] = rev; f["rules"] = rules_json(r); return f; }
inline json cpu_seat_frame(int seat, int level, int64_t rev) {
  json f = envelope("seat"); f["rev"] = rev; f["seat"] = seat; f["kind"] = "cpu"; f["level"] = level; return f;
}
inline json open_seat_frame(int seat, int64_t rev) { json f = envelope("seat"); f["rev"] = rev; f["seat"] = seat; f["kind"] = "open"; return f; }
inline json local_seat_frame(int64_t rev) { json f = open_seat_frame(1, rev); f["kind"] = "local"; return f; }
// stage: 0 = "random", else the stage id. input_delay 0 = not sent.
inline json start_frame(int stage, int64_t rev, int input_delay) {
  json f = envelope("start");
  f["rev"] = rev;
  if (stage == 0) f["stage"] = "random"; else f["stage"] = stage;
  if (input_delay) f["inputDelay"] = input_delay;
  return f;
}
// Doubles: Start carries the rev only (no stage, no inputDelay: Rooms refuses them as fixed_rules).
// A ranked search adds queue "ranked"; a Rooms without ranked Doubles refuses it (unranked_only, or
// shape before it knew the key) and the select says so.
inline json doubles_start_frame(int64_t rev, bool ranked = false) {
  json f = envelope("start"); f["rev"] = rev;
  if (ranked) f["queue"] = "ranked";
  return f;
}
// Doubles: stop the search. Any seated player may; Rooms refuses it with match_active once placed.
inline json cancel_frame() { return envelope("cancel"); }
// Friends: the host went to the stage select, or backed out of it. Rooms keeps the guests watching meanwhile.
inline json stage_select_frame(bool open, int64_t rev) {
  json f = envelope("stageSelect"); f["rev"] = rev; f["open"] = open; return f;
}
inline json leave_frame() { return envelope("leave"); }
inline json hello_frame(const std::string& settings_digest, const std::string& card_digest) {
  json f = envelope("hello"); f["settingsDigest"] = settings_digest; f["cardDigest"] = card_digest; return f;
}

// ---- incoming ----
struct Seat {
  int seat = 0;
  Kind kind = Kind::Open;
  std::string id, name, connection_id;
  int local_index = 0;
  int level = 0;                        // CPU only, 1-9
  bool hovering = false, picked = false;
  Pick hover, pick;
  int squad_version = 0;
  std::string squad_budget;
  std::vector<Pick> lineup;
  std::string settings_digest, card_digest;   // optional: a seat's hello, when Rooms shows it
  // `ready: true` once the seat's game sent its hello (Rooms freezes the digests into the setup only).
  // ready_shown: the entry carried the key at all.
  bool ready = false, ready_shown = false;
};
struct State {
  bool squad_protocol = false;
  int64_t rev = 0;
  int host = -1;                        // the host's seat, -1 when Rooms names none
  int size = kSeats;                    // the room's seats: kSeats, or kSeatsMax in a six-seat room (Doubles: 2)
  std::array<Seat, kSeatsMax> seats{};
  Rules rules;
  json setup;                           // the frozen setup during a match, else null
  // A refusal (sent only to the sender): error, action and the refused seat when there is one.
  std::string error, action;
  bool has_refused_seat = false;
  Seat refused_seat;
  bool bare = false;                    // a refusal with no lobby state (Rooms' `unsupported`)
  // Doubles: the home is looking for opponents (and a partner when seat 1 is open). Rooms sends it on
  // every Doubles state; searching_shown: the key was there.
  bool searching = false, searching_shown = false;
  // Doubles: the queue the home searches, when Rooms says (state.queue "ranked" or "casual"). A guest
  // learns the host's choice from it.
  bool ranked = false, ranked_shown = false;
  bool stage_select = false;   // Friends: the host is on the stage select (absent = no)
};
struct SetupSeat {
  int seat = 0, port = 0;
  Kind kind = Kind::Human;
  std::string id, name, connection_id;
  int local_index = 0;
  Pick pick;
  int level = 0;                        // CPU only
  int pro = -1;                         // a Pro AI seat: local_index 1, the model plays it on its owner's machine
  int squad_version = 0;
  std::string squad_budget;
  std::vector<Pick> lineup;
  std::string settings_digest, card_digest;   // optional (human seats, once Rooms freezes them)
};
struct Setup {
  int squad_version = 0;
  std::string squad_budget;
  std::string match_id;                 // the setup frame's; a state's frozen setup carries none
  int64_t rev = -1;
  uint32_t seed = 0;
  std::vector<SetupSeat> seats;
  Rules rules;
  int stage = 0;
  int input_delay = 0;                  // 0 when Rooms froze none (the default, 2, applies)
  json frozen;                          // exactly what game-report and finish echo
};

inline bool read_pick(const json& v, Pick* out) {
  if (!v.is_object() || !integer_at(v, "char", 0, kCharMax) || !integer_at(v, "color", 0, kColorMax)) return false;
  if (v.count("team") && !integer(v["team"], 0, kTeamMax)) return false;
  out->ch = (int)v["char"].get<int64_t>();
  out->color = (int)v["color"].get<int64_t>();
  out->team = v.count("team") ? (int)v["team"].get<int64_t>() : 0;
  return true;
}
inline bool read_text(const json& o, const char* key, size_t max, std::string* out) {
  const auto it = o.find(key);
  if (it == o.end() || !it->is_string()) return false;
  const auto& s = it->get_ref<const std::string&>();
  if (s.empty() || s.size() > max) return false;
  *out = s;
  return true;
}
inline void read_digests(const json& o, std::string* settings, std::string* card) {
  if (digest_at(o, "settingsDigest")) *settings = o["settingsDigest"].get<std::string>();
  if (digest_at(o, "cardDigest")) *card = o["cardDigest"].get<std::string>();
}
inline const char* read_seat(const json& v, Seat* out) {
  if (!v.is_object() || !integer_at(v, "seat", 0, kSeatsMax - 1) || !v.count("kind") || !v["kind"].is_string()) return "seat";
  Seat s;
  s.seat = (int)v["seat"].get<int64_t>();
  const std::string kind = v["kind"].get<std::string>();
  if (kind == "open") s.kind = Kind::Open;
  else if (kind == "human") {
    s.kind = Kind::Human;
    if (!read_text(v, "id", 128, &s.id)) return "human seat id";
    if (!read_text(v, "name", 256, &s.name)) s.name.clear();
  } else if (kind == "local") {
    s.kind = Kind::Local;
    if (s.seat != 1 || !integer_at(v, "localIndex", 1, 1) || !read_text(v, "owner", 128, &s.connection_id)) return "local owner";
    s.local_index = 1;
    read_text(v, "name", 256, &s.name);
  } else if (kind == "cpu") {
    s.kind = Kind::Cpu;
    if (!integer_at(v, "level", kLevelMin, kLevelMax)) return "cpu level";
    s.level = (int)v["level"].get<int64_t>();
  } else return "seat kind";
  if (s.kind != Kind::Open) {
    if (v.count("pick") && !(s.picked = read_pick(v["pick"], &s.pick))) return "pick";
    if (v.count("hover") && !(s.hovering = read_pick(v["hover"], &s.hover))) return "hover";
  }
  if (!read_squad_capability(v, &s.squad_version, &s.squad_budget)) return "squad capability";
  if (v.count("lineup") && !read_lineup(v["lineup"], &s.lineup)) return "lineup";
  read_digests(v, &s.settings_digest, &s.card_digest);
  if (v.count("ready")) {
    if (!v["ready"].is_boolean()) return "ready";
    s.ready_shown = true;
    s.ready = v["ready"].get<bool>();
  }
  *out = s;
  return nullptr;
}
// The frozen setup object ({seed, seats, rules, stage}, plus what Rooms adds later). nullptr when usable.
inline const char* read_setup_object(const json& o, Setup* out) {
  if (!o.is_object()) return "setup";
  Setup s;
  if (!integer_at(o, "seed", 0, 0xFFFFFFFFLL)) return "seed";
  s.seed = (uint32_t)o["seed"].get<int64_t>();
  if (!o.count("rules") || !read_rules(o["rules"], &s.rules, false)) return "rules";
  if (!integer_at(o, "stage", 2, 32) || !stage_ok(o["stage"].get<int64_t>())) return "stage";
  s.stage = (int)o["stage"].get<int64_t>();
  if (o.count("inputDelay")) {
    if (!integer(o["inputDelay"], kInputDelayMin, kInputDelayMax)) return "inputDelay";
    s.input_delay = (int)o["inputDelay"].get<int64_t>();
  }
  if (!o.count("seats") || !o["seats"].is_array() || o["seats"].size() < 1 || o["seats"].size() > (size_t)kSeatsMax) return "seats";
  for (const auto& v : o["seats"]) {
    SetupSeat x;
    if (!v.is_object() || !integer_at(v, "seat", 0, kSeatsMax - 1) || !integer_at(v, "port", 0, kSeatsMax - 1)) return "setup seat";
    x.seat = (int)v["seat"].get<int64_t>();
    x.port = (int)v["port"].get<int64_t>();
    const std::string kind = v.value("kind", json()).is_string() ? v["kind"].get<std::string>() : std::string();
    if (kind == "human") x.kind = Kind::Human;
    else if (kind == "cpu") x.kind = Kind::Cpu;
    else return "setup seat kind";
    if (!read_text(v, "id", 128, &x.id)) return "setup seat id";
    if (!read_text(v, "name", 256, &x.name)) return "setup seat name";
    if (!read_pick(v, &x.pick) || !v.count("team")) return "setup seat pick";
    if (x.kind == Kind::Cpu) {
      if (!integer_at(v, "level", kLevelMin, kLevelMax)) return "setup cpu level";
      x.level = (int)v["level"].get<int64_t>();
    } else if (v.count("level")) return "setup human level";
    if (v.count("localIndex")) {
      if (!integer_at(v, "localIndex", 0, 1)) return "setup local index";
      x.local_index = v["localIndex"].get<int>();
    }
    if (v.count("connectionId") && !read_text(v, "connectionId", 128, &x.connection_id)) return "setup connection";
    if (v.count("lineup") && !read_lineup(v["lineup"], &x.lineup)) return "lineup";
    if (!read_squad_capability(v, &x.squad_version, &x.squad_budget)) return "squad capability";
    read_digests(v, &x.settings_digest, &x.card_digest);
    s.seats.push_back(std::move(x));
  }
  if (!read_squad_capability(o, &s.squad_version, &s.squad_budget)) return "squad capability";
  s.frozen = o;
  for (const char* k : {"t", "v", "op", "rev", "matchId"}) s.frozen.erase(k);
  *out = std::move(s);
  return nullptr;
}

enum class Op { None, State, Setup };
struct Incoming {
  Op op = Op::None;
  State state;
  Setup setup;
  const char* why = nullptr;            // a lobby frame the game cannot use (logged, ignored)
};
// welcome.muLobby or a broadcast. Anything that is not a v1 lobby frame is Op::None, why == nullptr.
// A Doubles home's state has 2 seats: the rest of State::seats stays Open (each with its own index).
// A Friends state has 4 seats, or 6 from a Rooms that seats six (State::size says which).
inline Incoming decode(const json& m, Profile profile = Profile::Friends4) {
  int n = seats_of(profile);
  if (profile == Profile::Friends4 && m.is_object() && m.count("seats") && m["seats"].is_array() &&
      m["seats"].size() == (size_t)kSeatsMax)
    n = kSeatsMax;
  Incoming in;
  if (!m.is_object() || m.value("t", json()) != kType) return in;
  if (!integer_at(m, "v", kVersion, kVersion) || !m.count("op") || !m["op"].is_string()) { in.why = "envelope"; return in; }
  const std::string op = m["op"].get<std::string>();
  if (op == "state") {
    State s;
    if (m.count("rev") && !integer(m["rev"], 0, 9007199254740991LL)) { in.why = "rev"; return in; }
    s.rev = m.count("rev") ? m["rev"].get<int64_t>() : 0;
    if (m.count("error") && m["error"].is_string()) s.error = m["error"].get<std::string>().substr(0, 64);
    if (m.count("action") && m["action"].is_string()) s.action = m["action"].get<std::string>().substr(0, 16);
    // Rooms' bare refusal for a room without the lobby: {t, v, op:'state', error:'unsupported'}.
    if (!s.error.empty() && !m.count("seats")) { s.bare = true; in.op = Op::State; in.state = s; return in; }
    if (m.count("host") && !m["host"].is_null()) {
      if (!integer(m["host"], 0, n - 1)) { in.why = "host"; return in; }
      s.host = (int)m["host"].get<int64_t>();
    }
    if (!m.count("seats") || !m["seats"].is_array() || m["seats"].size() != (size_t)n) { in.why = "seats"; return in; }
    s.size = n;
    for (int i = n; i < kSeatsMax; ++i) s.seats[i].seat = i;
    for (size_t i = 0; i < (size_t)n; ++i) {
      if (const char* why = read_seat(m["seats"][i], &s.seats[i])) { in.why = why; return in; }
      if (s.seats[i].kind == Kind::Local && profile != Profile::Doubles) { in.why = "local outside doubles"; return in; }
      if (s.seats[i].seat != (int)i) { in.why = "seat order"; return in; }
    }
    if (!m.count("rules") || !read_rules(m["rules"], &s.rules, false)) { in.why = "rules"; return in; }
    if (m.count("squadProtocol")) {
      if (!integer_at(m, "squadProtocol", 1, 1)) { in.why = "squadProtocol"; return in; }
      s.squad_protocol = profile == Profile::Friends4;
    }
    if (profile == Profile::Doubles && s.rules.squad) { in.why = "squad outside friends"; return in; }
    if (s.rules.squad) for (auto& seat : s.seats) {
      seat.picked = seat.lineup.size() == 4;
      if (seat.picked) seat.pick = seat.lineup.front();
    }
    if (m.count("searching")) {
      if (!m["searching"].is_boolean()) { in.why = "searching"; return in; }
      s.searching_shown = true;
      s.searching = m["searching"].get<bool>();
    }
    if (profile == Profile::Doubles && m.count("queue") && m["queue"].is_string()) {
      const auto& q = m["queue"].get_ref<const std::string&>();
      if (q == "ranked" || q == "casual") { s.ranked_shown = true; s.ranked = q == "ranked"; }
    }
    if (m.count("stageSelect")) {
      if (!m["stageSelect"].is_boolean()) { in.why = "stageSelect"; return in; }
      s.stage_select = m["stageSelect"].get<bool>();
    }
    if (m.count("setup") && !m["setup"].is_null()) {
      Setup check;
      if (const char* why = read_setup_object(m["setup"], &check)) { in.why = why; return in; }
      if (profile == Profile::Doubles && check.rules.squad) { in.why = "squad outside friends"; return in; }
      s.setup = check.frozen;
    }
    if (m.count("seat") && m["seat"].is_object()) s.has_refused_seat = read_seat(m["seat"], &s.refused_seat) == nullptr;
    in.op = Op::State;
    in.state = std::move(s);
    return in;
  }
  if (op == "setup") {
    Setup s;
    if (const char* why = read_setup_object(m, &s)) { in.why = why; return in; }
    if (profile == Profile::Doubles && s.rules.squad) { in.why = "squad outside friends"; return in; }
    if (!read_text(m, "matchId", 128, &s.match_id)) { in.why = "matchId"; return in; }
    s.rev = m.count("rev") && integer(m["rev"], 0, 9007199254740991LL) ? m["rev"].get<int64_t>() : -1;
    in.op = Op::Setup;
    in.setup = std::move(s);
    return in;
  }
  in.why = "op";
  return in;
}
}  // namespace yougame::lobby
