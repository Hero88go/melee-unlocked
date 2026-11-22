// SPDX-License-Identifier: GPL-2.0-or-later
// The 4-door Friends select's view (yougame_lobby_screen.h): a hint for every Start block and every
// Rooms refusal code, doors from the lobby state, links shown only once up, and the byte view the
// screen reads (abi/mu_lobby_wire.h) round-tripping through encode and decode.
#include "yougame_lobby_screen.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

using namespace yougame::lobby;

static json seat_open(int s) { return {{"seat", s}, {"kind", "open"}}; }
static json seat_human(int s, const std::string& id, bool picked = false, int team = 0) {
  json j = {{"seat", s}, {"kind", "human"}, {"id", id}, {"name", id}};
  if (picked) j["pick"] = {{"char", 2}, {"color", 0}, {"team", team}};
  return j;
}
static json seat_cpu(int s, int level, bool picked = false, int team = 0) {
  json j = {{"seat", s}, {"kind", "cpu"}, {"level", level}};
  if (picked) j["pick"] = {{"char", 9}, {"color", 1}, {"team", team}};
  return j;
}
static json state(int64_t rev, int host, std::vector<json> seats, bool teams = false) {
  return {{"t", "yg.mu.lobby"}, {"v", 1}, {"op", "state"}, {"rev", rev}, {"host", host < 0 ? json() : json(host)},
          {"seats", seats},
          {"rules", {{"teams", teams}, {"friendlyFire", false}, {"mode", "stock"}, {"stocks", 4}, {"minutes", 8}, {"pause", false}}},
          {"setup", nullptr}};
}
static Session::Options opts(const std::string& me) { Session::Options o; o.me = me; return o; }
static Control::View view_of(Session& s, const std::array<LinkInfo, kSeats>& links = {}) {
  Control c;
  c.run(s, links, "ROOM1");
  return c.view();
}

int main() {
  // ---- a Doubles Control draws 2 doors from its first frame, before the session ever runs (e22413bd) ----
  {
    Control doubles(Profile::Doubles);
    CHECK(doubles.view().profile == Profile::Doubles);
    CHECK(doubles.view().my_seat == -1);
    CHECK(Control().view().profile == Profile::Friends4);
  }
  // ---- every Start block has its hint; the host sees why, a guest what it waits for ----
  {
    Control::View v;
    CHECK(block_hint(v, false) == MU_LOBBY_HINT_NONE);       // not a lobby screen
    CHECK(block_hint(v, true) == MU_LOBBY_HINT_OPENING);     // armed, no state yet
    v.unsupported = true;
    CHECK(block_hint(v, true) == MU_LOBBY_HINT_UNSUPPORTED);
    v = {};
    v.known = true; v.my_seat = 0; v.host = true;
    const struct { StartBlock b; unsigned hint; } table[] = {
        {StartBlock::None, MU_LOBBY_HINT_PRESS_START},     {StartBlock::NotSeated, MU_LOBBY_HINT_OPENING},
        {StartBlock::NotHost, MU_LOBBY_HINT_WAIT_HOST},    {StartBlock::MatchActive, MU_LOBBY_HINT_IN_MATCH},
        {StartBlock::PickRequired, MU_LOBBY_HINT_PICK},    {StartBlock::PlayersRequired, MU_LOBBY_HINT_PLAYERS},
        {StartBlock::NotReady, MU_LOBBY_HINT_NOT_READY},   {StartBlock::TeamsRequired, MU_LOBBY_HINT_TEAMS},
        {StartBlock::HumansRequired, MU_LOBBY_HINT_HUMANS},
    };
    for (const auto& row : table) { v.start = row.b; CHECK(block_hint(v, true) == row.hint); }
    v.started = true;
    CHECK(block_hint(v, true) == MU_LOBBY_HINT_STARTING);
    v.started = false; v.host = false; v.start = StartBlock::NotHost;
    CHECK(block_hint(v, true) == MU_LOBBY_HINT_PICK);        // a guest without a fighter
    v.state.seats[0].picked = true;
    CHECK(block_hint(v, true) == MU_LOBBY_HINT_WAIT_HOST);
    for (unsigned h = 0; h < MU_LOBBY_HINT_COUNT; ++h) CHECK(h == MU_LOBBY_HINT_NONE || *mu_lobby_hint_text(h));
  }

  // ---- every refusal code the spec names, plus the ones the session absorbs ----
  {
    const struct { const char* error; unsigned hint; } table[] = {
        {"players_required", MU_LOBBY_HINT_PLAYERS}, {"humans_required", MU_LOBBY_HINT_HUMANS},
        {"teams_required", MU_LOBBY_HINT_TEAMS},     {"digest_missing", MU_LOBBY_HINT_NOT_READY},
        {"digest_mismatch", MU_LOBBY_HINT_MISMATCH}, {"player_away", MU_LOBBY_HINT_AWAY},
        {"pick_required", MU_LOBBY_HINT_PICK},       {"host_only", MU_LOBBY_HINT_HOST_ONLY},
        {"seat_occupied", MU_LOBBY_HINT_TAKEN},      {"seat_owner", MU_LOBBY_HINT_TAKEN},
        {"match_active", MU_LOBBY_HINT_IN_MATCH},    {"unsupported", MU_LOBBY_HINT_UNSUPPORTED},
        {"not_seated", MU_LOBBY_HINT_REFUSED},       {"something_new", MU_LOBBY_HINT_REFUSED},
        {"", MU_LOBBY_HINT_NONE},                    {"shape", MU_LOBBY_HINT_NONE},
    };
    for (const auto& row : table) CHECK(refusal_hint(row.error, "start") == row.hint);
    CHECK(refusal_hint("revision", "start") == MU_LOBBY_HINT_CHANGED);
    CHECK(refusal_hint("revision", "rules") == MU_LOBBY_HINT_NONE);   // resent on the new rev by itself
    CHECK(std::string(mu_lobby_hint_text(MU_LOBBY_HINT_MISMATCH)) == "Someone has a different version");
  }

  // ---- routes and names ----
  {
    CHECK(route_code("") == MU_LOBBY_ROUTE_NONE && route_code("direct") == MU_LOBBY_ROUTE_DIRECT);
    CHECK(route_code("lan") == MU_LOBBY_ROUTE_LAN && route_code("relay") == MU_LOBBY_ROUTE_RELAY);
    CHECK(route_code("turn-v6") == MU_LOBBY_ROUTE_UNKNOWN);
    char name[MU_LOBBY_NAME];
    screen_name("Ana\xC3\xA9z\tB", name);
    CHECK(std::string(name) == "Ana?z?B");
    screen_name("a-very-long-player-name", name);
    CHECK(std::strlen(name) == MU_LOBBY_NAME - 1 && name[MU_LOBBY_NAME - 1] == 0);
  }

  // ---- the doors from a real session: kinds, picks, hover, CPU level, links only for others ----
  {
    Session s(opts("a"));
    json st = state(5, 0, {seat_human(0, "a", true), seat_cpu(1, 7, true, 1), seat_human(2, "c"), seat_open(3)}, true);
    st["seats"][2]["hover"] = {{"char", 12}, {"color", 2}, {"team", 1}};
    CHECK(s.receive(st));
    std::array<LinkInfo, kSeats> links{};
    links[0] = {"direct", 3};      // my own seat: never drawn
    links[1] = {"direct", 3};      // a CPU: never drawn
    links[2] = {"relay", 48};
    const Control::View v = view_of(s, links);
    CHECK(v.room == "ROOM1");
    ScreenFlags f; f.armed = true;
    const MuLobbyView out = screen_view(v, f);
    CHECK(out.flags == (MU_LOBBY_F_ARMED | MU_LOBBY_F_KNOWN | MU_LOBBY_F_HOST));
    CHECK(out.my_seat == 0 && out.host_seat == 0 && out.block_hint == MU_LOBBY_HINT_PICK);   // seat 2 hasn't picked
    CHECK(out.rules.teams == 1 && out.rules.stocks == 4 && out.rules.minutes == 8 && out.rules.time == 0);
    CHECK(out.seats[0].kind == MU_LOBBY_KIND_HUMAN && out.seats[0].pick_ch == 2 && out.seats[0].route == MU_LOBBY_ROUTE_NONE);
    CHECK(std::string(out.seats[0].name) == "a");
    CHECK(out.seats[1].kind == MU_LOBBY_KIND_CPU && out.seats[1].level == 7 && out.seats[1].pick_ch == 9 &&
          out.seats[1].pick_team == 1 && out.seats[1].route == MU_LOBBY_ROUTE_NONE && out.seats[1].name[0] == 0);
    CHECK(out.seats[2].flags == MU_LOBBY_S_HOVERING && out.seats[2].hover_ch == 12 && out.seats[2].hover_team == 1 &&
          out.seats[2].pick_ch == MU_LOBBY_NONE);
    CHECK(out.seats[2].route == MU_LOBBY_ROUTE_RELAY && out.seats[2].rtt_ms == 48);
    CHECK(out.seats[3].kind == MU_LOBBY_KIND_OPEN && out.seats[3].pick_ch == MU_LOBBY_NONE && out.seats[3].hover_ch == MU_LOBBY_NONE);
    // Before a link is up: nothing on the door.
    const MuLobbyView bare = screen_view(view_of(s), f);
    CHECK(bare.seats[2].route == MU_LOBBY_ROUTE_NONE && bare.seats[2].rtt_ms == 0);
    // A huge RTT is clamped for the 4-digit label.
    links[2] = {"direct", 123456};
    CHECK(screen_view(view_of(s, links), f).seats[2].rtt_ms == 9999);

    // The byte view round-trips exactly.
    unsigned char bytes[MU_LOBBY_VIEW_SIZE];
    CHECK(mu_lobby_view_encode(&out, bytes, sizeof bytes) == MU_LOBBY_VIEW_SIZE);
    MuLobbyView back;
    CHECK(mu_lobby_view_decode(bytes, sizeof bytes, &back));
    CHECK(std::memcmp(&back, &out, sizeof out) == 0);
    bytes[0] = MU_LOBBY_VERSION + 1;
    CHECK(!mu_lobby_view_decode(bytes, sizeof bytes, &back));
  }

  // ---- a guest; a refusal bumps the sequence once, so the screen shows it once ----
  {
    Session s(opts("c"));
    s.receive(state(5, 0, {seat_human(0, "a", true), seat_open(1), seat_human(2, "c", true), seat_open(3)}));
    ScreenFlags f; f.armed = true; f.guest = true; f.inviting = true;
    MuLobbyView out = screen_view(view_of(s), f);
    CHECK(out.flags == (MU_LOBBY_F_ARMED | MU_LOBBY_F_KNOWN | MU_LOBBY_F_GUEST | MU_LOBBY_F_INVITING));
    CHECK(out.my_seat == 2 && out.host_seat == 0 && out.block_hint == MU_LOBBY_HINT_WAIT_HOST);
    CHECK(out.refusal_hint == MU_LOBBY_HINT_NONE && out.refusal_seq == 0);
    json r = state(5, 0, {seat_human(0, "a", true), seat_open(1), seat_human(2, "c", true), seat_open(3)});
    r["error"] = "host_only"; r["action"] = "rules";
    s.receive(r);
    out = screen_view(view_of(s), f);
    CHECK(out.refusal_hint == MU_LOBBY_HINT_HOST_ONLY && out.refusal_seq == 1);
    s.receive(state(6, 0, {seat_human(0, "a", true), seat_open(1), seat_human(2, "c", true), seat_open(3)}));
    CHECK(screen_view(view_of(s), f).refusal_seq == 1);   // a plain state: no new refusal
  }

  // ---- YouGame's Leave (the app's lobby overlay): reported only while a lobby is armed ----
  {
    Session s(opts("c"));
    s.receive(state(5, 0, {seat_human(0, "a", true), seat_open(1), seat_human(2, "c", true), seat_open(3)}));
    ScreenFlags f; f.armed = true;
    CHECK(!(screen_view(view_of(s), f).flags & MU_LOBBY_F_LEAVE));
    f.leave = true;
    CHECK(screen_view(view_of(s), f).flags & MU_LOBBY_F_LEAVE);
    f.armed = false;
    CHECK(!(screen_view(view_of(s), f).flags & MU_LOBBY_F_LEAVE));
  }

  // ---- a match started: STARTING for everyone ----
  {
    Session s(opts("a"));
    s.receive(state(4, 0, {seat_human(0, "a", true), seat_human(1, "b", true), seat_open(2), seat_open(3)}));
    json setup = {{"t", "yg.mu.lobby"}, {"v", 1}, {"op", "setup"}, {"rev", 4}, {"matchId", "m1"}, {"seed", 7},
                  {"seats", {{{"seat", 0}, {"port", 0}, {"kind", "human"}, {"id", "a"}, {"name", "a"}, {"char", 2}, {"color", 0}, {"team", 0}},
                             {{"seat", 1}, {"port", 1}, {"kind", "human"}, {"id", "b"}, {"name", "b"}, {"char", 2}, {"color", 0}, {"team", 0}}}},
                  {"rules", {{"teams", false}, {"friendlyFire", false}, {"mode", "stock"}, {"stocks", 4}, {"minutes", 8}, {"pause", false}}},
                  {"stage", 31}};
    s.receive(setup);
    ScreenFlags f; f.armed = true;
    const MuLobbyView out = screen_view(view_of(s), f);
    CHECK((out.flags & MU_LOBBY_F_STARTED) && out.block_hint == MU_LOBBY_HINT_STARTING);
    // The room closed under it: only B is left, whatever else the view says.
    f.closed = true;
    CHECK(screen_view(view_of(s), f).block_hint == MU_LOBBY_HINT_CLOSED);
    ScreenFlags off; off.closed = true;   // not armed: no hint at all
    CHECK(screen_view(view_of(s), off).block_hint == MU_LOBBY_HINT_NONE);
  }
  // ---- a closed room's last state can't send a guest back to the stage select ----
  {
    Control::View v;
    v.state.stage_select = true;
    ScreenFlags f; f.armed = true;
    CHECK(screen_view(v, f).mode & MU_LOBBY_M_STAGE_SELECT);
    f.closed = true;
    CHECK(!(screen_view(v, f).mode & MU_LOBBY_M_STAGE_SELECT));
    ScreenFlags off;
    CHECK(!(screen_view(v, off).mode & MU_LOBBY_M_STAGE_SELECT));
  }
  // ---- nor keep a closed Doubles home searching: its pad (and B out) would stay taken ----
  {
    Control::View v;
    v.profile = Profile::Doubles; v.searching = true;
    ScreenFlags f; f.armed = true;
    CHECK(screen_view(v, f).mode & MU_LOBBY_M_SEARCHING);
    f.closed = true;
    CHECK(!(screen_view(v, f).mode & MU_LOBBY_M_SEARCHING) && (screen_view(v, f).mode & MU_LOBBY_M_DOUBLES));
    CHECK(screen_view(v, f).block_hint == MU_LOBBY_HINT_CLOSED);
  }
  // ---- the arming and the note: a select from another arming never leaves; why a setup fell through ----
  {
    Control::View v;
    v.known = true; v.my_seat = 0;
    ScreenFlags f;
    f.armed = true; f.arm = 7; f.note = "Match cancelled: connection lost."; f.note_seq = 3;
    MuLobbyView out = screen_view(v, f);
    CHECK(out.arm == 7 && out.note_seq == 3 && std::string(out.note) == "Match cancelled: connection lost.");
    unsigned char bytes[MU_LOBBY_VIEW_SIZE];
    MuLobbyView back;
    CHECK(mu_lobby_view_encode(&out, bytes, sizeof bytes) == MU_LOBBY_VIEW_SIZE);
    CHECK(bytes[8] == 7 && bytes[9] == 3);               // arm, then the drop count (after the mode byte)
    CHECK(mu_lobby_view_decode(bytes, sizeof bytes, &back) && back.arm == 7 && back.note_seq == 3 &&
          std::memcmp(&back, &out, sizeof out) == 0);
    v.started = true;                                    // the next match started: the note is gone
    CHECK(screen_view(v, f).note[0] == 0);
    v.started = false;
    f.armed = false;                                     // not a lobby: no note, the arm still says which
    CHECK(screen_view(v, f).note[0] == 0 && screen_view(v, f).arm == 7);
    f.armed = true;
    f.note = std::string("caf\xC3\xA9 ") + std::string(80, 'x');   // the menu font, one line
    out = screen_view(v, f);
    CHECK(std::strlen(out.note) == MU_LOBBY_NOTE - 1 && std::string(out.note).rfind("caf? x", 0) == 0);
    CHECK(MU_LOBBY_VERSION == 6 && MU_LOBBY_VIEW_SIZE == 10 + 6 + MU_LOBBY_NOTE + 4 * MU_LOBBY_SEAT_SIZE);
  }
  // ---- the rules bytes: Stock's minutes may be 0 (no timer), Time's never ----
  {
    const unsigned char stock0[MU_LOBBY_RULES_SIZE] = {0, 0, 0, 4, 0, 0}, time0[MU_LOBBY_RULES_SIZE] = {0, 0, 1, 4, 0, 0};
    const unsigned char time1[MU_LOBBY_RULES_SIZE] = {0, 0, 1, 4, 1, 0}, stock100[MU_LOBBY_RULES_SIZE] = {0, 0, 0, 4, 100, 0};
    CHECK(mu_lobby_rules_ok(stock0) && !mu_lobby_rules_ok(time0) && mu_lobby_rules_ok(time1) && !mu_lobby_rules_ok(stock100));
    const unsigned char rules_req[1 + MU_LOBBY_RULES_SIZE] = {MU_LOBBY_OP_RULES, 0, 0, 0, 4, 0, 0};
    CHECK(mu_lobby_request_ok(rules_req, sizeof rules_req));
    const unsigned char timed_req[1 + MU_LOBBY_RULES_SIZE] = {MU_LOBBY_OP_RULES, 0, 0, 1, 4, 0, 0};
    CHECK(!mu_lobby_request_ok(timed_req, sizeof timed_req));
  }
  // Lineup state adds nine bytes per seat; new commands have exact lengths and ranges.
  {
    CHECK(MU_LOBBY_VIEW_SIZE == 212);
    const unsigned char claim[] = {MU_LOBBY_OP_LOCAL_SEAT,1}, release[] = {MU_LOBBY_OP_LOCAL_SEAT,0};
    const unsigned char bad[] = {MU_LOBBY_OP_LOCAL_SEAT,2};
    const unsigned char hover[] = {MU_LOBBY_OP_LOCAL_HOVER,2,0,0};
    const unsigned char pick[] = {MU_LOBBY_OP_LOCAL_PICK,2,0,0};
    const unsigned char unpick[] = {MU_LOBBY_OP_LOCAL_UNPICK};
    CHECK(mu_lobby_request_ok(claim,2) && mu_lobby_request_ok(release,2) && !mu_lobby_request_ok(bad,2));
    CHECK(mu_lobby_request_ok(hover,4) && mu_lobby_request_ok(pick,4) && mu_lobby_request_ok(unpick,1));
    CHECK(!mu_lobby_request_ok(hover,3) && !mu_lobby_request_ok(unpick,2));
  }
  // ---- COSTUMES: this build's count for every fighter, 1-6 each; Melee's own table otherwise ----
  {
    unsigned char req[1 + MU_LOBBY_CHARS];
    req[0] = MU_LOBBY_OP_COSTUMES;
    for (unsigned ch = 0; ch < MU_LOBBY_CHARS; ++ch) req[1 + ch] = (unsigned char)mu_lobby_costumes_melee(ch);
    {
      const unsigned char open_req[] = {MU_LOBBY_OP_STAGE_SELECT, 1}, wrong[] = {MU_LOBBY_OP_STAGE_SELECT, 2};
      CHECK(mu_lobby_request_ok(open_req, 2) && !mu_lobby_request_ok(wrong, 2) && !mu_lobby_request_ok(open_req, 1));
    }
    CHECK(MU_LOBBY_CHARS == 26 && MU_LOBBY_OP_COUNT == 22 && mu_lobby_request_ok(req, sizeof req));
    // PEEK: READ's view with no side effects, one byte like READ.
    const unsigned char peek[1] = {MU_LOBBY_OP_PEEK}, peek_long[2] = {MU_LOBBY_OP_PEEK, 0};
    CHECK(mu_lobby_request_size(MU_LOBBY_OP_PEEK) == 1 && mu_lobby_request_ok(peek, 1) && !mu_lobby_request_ok(peek_long, 2));
    CHECK(!mu_lobby_request_ok(req, sizeof req - 1));
    req[5] = 0; CHECK(!mu_lobby_request_ok(req, sizeof req));
    req[5] = 7; CHECK(!mu_lobby_request_ok(req, sizeof req));
    req[5] = 6; CHECK(mu_lobby_request_ok(req, sizeof req));
    // gm_1601.c lbl_803D51A0 ncolors: Captain Falcon 6, DK 5, Fox 4, ... Ganondorf 5; none past Sheik.
    const unsigned melee[26] = {6, 5, 4, 4, 6, 4, 5, 4, 5, 5, 4, 4, 5, 4, 4, 5, 5, 6, 5, 5, 4, 5, 5, 5, 4, 5};
    for (unsigned ch = 0; ch < 26; ++ch) CHECK(mu_lobby_costumes_melee(ch) == melee[ch]);
    CHECK(mu_lobby_costumes_melee(26) == 0);
  }
  // Squad lineups round-trip in the shared view, with no duplicates or out-of-range members.
  {
    MuLobbyView v{}, round{};
    v.my_seat = v.host_seat = 0;
    v.rules = {0, 0, 0, 4, 8, 0};
    v.mode = MU_LOBBY_M_SQUAD | MU_LOBBY_M_SQUAD_AVAILABLE;
    v.seats[0].kind = MU_LOBBY_KIND_HUMAN;
    v.seats[0].lineup_count = 2;
    v.seats[0].lineup[0][0] = 0; v.seats[0].lineup[0][1] = 5;
    v.seats[0].lineup[1][0] = 25; v.seats[0].lineup[1][1] = 4;
    unsigned char bytes[MU_LOBBY_VIEW_SIZE];
    CHECK(mu_lobby_view_encode(&v, bytes, sizeof bytes) == sizeof bytes);
    CHECK(mu_lobby_view_decode(bytes, sizeof bytes, &round));
    CHECK(round.mode == v.mode && round.seats[0].lineup_count == 2 && round.seats[0].lineup[1][0] == 25);
    v.seats[0].lineup[1][0] = 0;
    mu_lobby_view_encode(&v, bytes, sizeof bytes); CHECK(!mu_lobby_view_decode(bytes, sizeof bytes, &round));
    unsigned char req[10] = {MU_LOBBY_OP_LINEUP, 2, 0, 5, 25, 4};
    CHECK(mu_lobby_request_ok(req, sizeof req));
    req[4] = 0; CHECK(!mu_lobby_request_ok(req, sizeof req));
    req[4] = 26; CHECK(!mu_lobby_request_ok(req, sizeof req));
    req[4] = 25; req[5] = 6; CHECK(!mu_lobby_request_ok(req, sizeof req));
    req[5] = 4; req[1] = 5; CHECK(!mu_lobby_request_ok(req, sizeof req));
    req[1] = 2; req[9] = 1; CHECK(!mu_lobby_request_ok(req, sizeof req));
    req[9] = 0; CHECK(!mu_lobby_request_ok(req, sizeof req - 1));
  }
  // Every hint has a line, and only NONE is blank.
  for (unsigned h = 0; h < MU_LOBBY_HINT_COUNT; ++h) CHECK((h == MU_LOBBY_HINT_NONE) == (mu_lobby_hint_text(h)[0] == 0));
  // Every hint and route fits Melee's label font (letters, digits, spaces, periods) and one line.
  const auto font_ok = [](const char* t) {
    size_t n = 0;
    for (; t[n]; ++n) {
      const char c = t[n];
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == '\'')) return false;
    }
    return n <= 64;
  };
  for (unsigned h = 0; h < MU_LOBBY_HINT_COUNT; ++h) CHECK(font_ok(mu_lobby_hint_text(h)));
  for (unsigned r = 0; r <= MU_LOBBY_ROUTE_UNKNOWN; ++r) CHECK(font_ok(mu_lobby_route_text(r)));
  std::printf("ok\n");
  return 0;
}
