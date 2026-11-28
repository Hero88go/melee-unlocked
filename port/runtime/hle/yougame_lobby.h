// The 4-player Friends lobby's state model: one session per room socket, no transport and no clock of
// its own (the caller feeds room messages and the time, and sends what due() returns). It keeps the
// room's latest state (Rooms owns it: a refusal's state replaces ours as any broadcast does), turns the
// local player's and the host's intents into v1 frames, and says what the screen may offer.
//
// Rules it follows (Rooms rooms/src/game-lobby.ts at b82e31058 and the coordinator's later shapes):
//  - rev covers shared state only (rules, seat, start, CPU picks). A shared frame carries the rev the
//    player saw; a `revision` refusal hands back the new state, and the session sends rules, seat and
//    CPU picks again on the new rev (at most kMaxResends times). Start is never resent on a new rev:
//    the roster changed under the host's press, so the host presses again.
//  - hover: only on change, at most one per 100 ms, the newest one flushed when the 100 ms are up.
//    Rooms keeps no hover across a reconnect: a reconnect brings picks and rules, not hovers.
//  - hello {settingsDigest, cardDigest} once when this player takes a seat (again on another seat, and
//    after a digest_missing refusal). A `shape` refusal of hello means Rooms has no hello yet: stop.
//  - start {inputDelay} only when asked for; a `shape` refusal of it sends the same start once more
//    without it, if the roster did not change.
//  - a keepalive at least every 5 s: Rooms evicts a lobby socket silent for over 15 s.
//  - Teams on with every seat on one team: Start is greyed out (Rooms refuses it, teams_required).
//  - Seats show `ready: true` once their game sent hello: Start waits for every human (digest_missing).
//    player_away (someone inside their 5 s lobby grace) is carried through like any refusal.
//  - Start needs two humans (Rooms refuses fewer, humans_required): greyed out with one human and CPUs.
//
// The Doubles home (Profile::Doubles, G2) is the same session on a 2-seat lobby: seat 0 owns the
// partner seat (CPU 1-9 or open: "find me a partner"; set_rules is refused, the rules are fixed), any
// seated player presses Start (alone too: Rooms then searches for a partner and opponents together),
// the start frame carries only the rev, and `cancel` stops the search. While Rooms says `searching`
// the select is locked (Rooms refuses hover, pick and seat with `searching`). A digest_mismatch that
// ends a search is the other team's version, not this home's: it reads as `digest_mismatch_teams`.
#pragma once
#include "yougame_lobby_wire.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace yougame::lobby {

inline constexpr uint64_t kHoverMs = 100;
inline constexpr uint64_t kKeepaliveMs = 5000;   // under Rooms' 15 s eviction with two misses to spare
inline constexpr int kMaxResends = 3;

// Why Start is not offered, in the order the screen explains them.
// NotReady: a seated human's game has not sent its hello (Rooms: digest_missing), shown only once Rooms
// shows `ready` on any seat (a room before hello never does).
enum class StartBlock { None, NotSeated, NotHost, MatchActive, PickRequired, PlayersRequired, NotReady, TeamsRequired,
                        HumansRequired, Searching, SquadTwoPlayers, SquadCapability, SquadBudget, RankedHumans };

// The cross-team digest_mismatch (Rooms' teams-search-error, broadcast to the home after its search
// found opponents on another version): the session's own name for it, so the screen tells it apart from
// a mismatch inside this home.
inline constexpr const char* kTeamsMismatch = "digest_mismatch_teams";

// Rooms' refusal codes the screen explains (phase 2 maps each to a hint); unknown codes pass through.
inline bool known_refusal(const std::string& e) {
  for (const char* k : {"shape", "not_seated", "match_active", "host_only", "revision", "seat_owner", "pick_required",
                        "players_required", "seat_occupied", "seat_taken", "unsupported", "digest_missing", "digest_mismatch",
                        "teams_required", "player_away", "humans_required", "searching", "fixed_rules",
                        "unranked_only", "search_cancelled", "stage_select_open", "squad_rules", "squad_two_players",
                        "squad_capability", "squad_budget", "squad_unavailable", kTeamsMismatch})
    if (e == k) return true;
  return false;
}

struct Seated { int seat; std::string id; };

class Session {
 public:
  struct Options {
    std::string me;                     // this player's room id (welcome/participants id)
    Profile profile = Profile::Friends4;
    std::string settings_digest, card_digest;   // hello's; empty = no hello
    int input_delay = 0;                // start's inputDelay, 0 = never sent
    bool ranked = false;                // Doubles: the Online menu's queue choice (the host's; Rooms' state wins)
    // A call-off carried from this room's earlier socket (call_off() of the session it replaces): the
    // host's room connection closed after a setup that never began, so this session treats that frozen
    // setup as stale and calls it off once this player is the host.
    std::string call_off_id;
    json call_off_setup;                // that setup as the old state showed it (null: any frozen setup)
    std::string connection_id;          // owning socket, distinct from the participant id
  };
  explicit Session(Options o) : opt_(std::move(o)) {
    pending_call_off_ = opt_.call_off_id;
    pending_frozen_ = pending_call_off_.empty() ? json() : opt_.call_off_setup;
  }

  // Every room message goes through here. True when it was a lobby frame (welcome.muLobby included).
  bool receive(const json& m) {
    if (!m.is_object()) return false;
    if (m.value("t", json()) == "welcome" && m.count("muLobby")) return take(decode(m["muLobby"], opt_.profile));
    return take(decode(m, opt_.profile));
  }

  // The frames to send now, in order (keepalive included). The caller sends each as one room message.
  std::vector<json> due(uint64_t now) {
    std::vector<json> out;
    if (hover_pending_ && now >= hover_sent_at_ + kHoverMs && my_seat() >= 0 && lobby_open()) {
      out.push_back(hover_frame(my_seat(), hover_));
      hover_sent_ = hover_; hover_has_sent_ = true; hover_pending_ = false; hover_sent_at_ = now;
    }
    if (local_hover_pending_ && owns_local() && lobby_open() && now >= local_hover_sent_at_ + kHoverMs) {
      out.push_back(hover_frame(1, local_hover_));
      local_hover_sent_ = local_hover_; local_hover_has_sent_ = true;
      local_hover_pending_ = false; local_hover_sent_at_ = now;
    }
    for (auto& f : queue_) out.push_back(std::move(f));
    queue_.clear();
    if (!out.empty()) last_sent_ = now;
    else if (now - last_sent_ >= kKeepaliveMs) {
      out.push_back({{"t", "ping"}, {"at", now}});
      last_sent_ = now;
    }
    return out;
  }
  // Something else went out on the socket (the room's own ping): it counts as the keepalive.
  void note_sent(uint64_t now) { last_sent_ = now; }

  // ---- the local player's intents (false: not possible now, nothing queued) ----
  bool hover(const Pick& p) {
    if (my_seat() < 0 || !lobby_open() || !pick_ok(p)) return false;
    hover_ = p;
    hover_pending_ = !(hover_has_sent_ && p == hover_sent_);
    return true;
  }
  bool pick(const Pick& p) {
    if (state_.rules.squad) return false;
    if (my_seat() < 0 || !lobby_open() || !pick_ok(p)) return false;
    return push(pick_frame(my_seat(), p, nullptr));
  }
  bool unpick() {
    if (state_.rules.squad) return false;
    if (my_seat() < 0 || !lobby_open()) return false;
    return push(unpick_frame(my_seat(), nullptr));
  }
  static constexpr bool squad_enabled() {
#if MU_SQUAD
    return true;
#else
    return false;
#endif
  }
  // Only local admission supplies this digest; a room advertisement alone never grants capability.
  void squad_admission(const std::string& budget) {
    if (doubles() || !squad_enabled() || !squad_budget_ok(budget) || squad_budget_ == budget) return;
    squad_budget_ = budget; squad_hello_ = false;
    if (my_seat() >= 0) send_hello(my_seat());
  }
  bool squad_available() const {
#if MU_SQUAD
    return !doubles() && state_.squad_protocol && !squad_budget_.empty();
#else
    return false;
#endif
  }
  const std::string& squad_budget() const { return squad_budget_; }
  bool lineup(const std::vector<Pick>& members) {
    if (state_.rules.squad && !squad_available()) {
      refusal_ = "squad_unavailable"; refusal_action_ = "lineup"; ++refusal_count_;
      return false;
    }
    if (!state_.rules.squad || my_seat() < 0 || !lobby_open()) return false;
    if (!read_lineup(lineup_json(members), nullptr)) return false;
    lineup_resends_ = 0;
    last_lineup_ = lineup_frame(my_seat(), members, state_.rev);
    return push(last_lineup_);
  }
  bool cycle_mode() {
    if (doubles() || !host_can()) return false;
    Rules r = state_.rules;
    if (r.squad) {
      r = saved_rules_ok_ ? saved_rules_ : Rules{};
      r.teams = false; r.squad = false;
    } else if (r.teams) {
      if (!squad_available()) {
        refusal_ = "squad_unavailable"; refusal_action_ = "rules"; ++refusal_count_;
        r.teams = false;
      } else {
        saved_rules_ = r; saved_rules_ok_ = true;
        r.squad = true; r.teams = false; r.time = false; r.stocks = 4;
        int filled = 0, humans = 0;
        for (const auto& seat : state_.seats) if (seat.kind != Kind::Open) {
          ++filled; if (seat.kind == Kind::Human) ++humans;
        }
        if (filled != 2 || humans != 2) {
          refusal_ = "squad_two_players"; refusal_action_ = "rules"; ++refusal_count_;
        }
      }
    } else r.teams = true;
    return set_rules(r);
  }
  bool owns_local() const {
    return doubles() && my_seat() == 0 && state_.seats[1].kind == Kind::Local &&
           state_.seats[1].connection_id == (opt_.connection_id.empty() ? opt_.me : opt_.connection_id);
  }
  bool local_seat(bool claim) {
    if (!host_can() || !doubles() || (claim ? state_.seats[1].kind != Kind::Open : !owns_local())) return false;
    return shared(claim ? local_seat_frame(state_.rev) : open_seat_frame(1, state_.rev));
  }
  bool local_hover(const Pick& p) {
    if (!owns_local() || !lobby_open() || !pick_ok(p)) return false;
    local_hover_ = p; local_hover_pending_ = !local_hover_has_sent_ || p != local_hover_sent_;
    return true;
  }
  bool local_pick(const Pick& p) {
    return owns_local() && lobby_open() && pick_ok(p) && push(pick_frame(1, p, nullptr));
  }
  bool local_unpick() { return owns_local() && lobby_open() && push(unpick_frame(1, nullptr)); }
  // ---- the host's (shared state, carries rev) ----
  bool cpu_pick(int seat, const Pick& p) {
    if (!host_can() || !cpu_seat(seat) || !pick_ok(p)) return false;
    return shared(pick_frame(seat, p, &state_.rev));
  }
  bool cpu_unpick(int seat) {
    if (!host_can() || !cpu_seat(seat)) return false;
    return shared(unpick_frame(seat, &state_.rev));
  }
  bool set_rules(const Rules& r) {
    if (doubles() || !host_can() || !rules_ok(r) || (r.squad && !squad_available())) return false;   // Doubles: fixed (Rooms: fixed_rules)
    return shared(rules_frame(r, state_.rev));
  }
  bool set_cpu(int seat, int level) {
    if (!host_can() || !seat_ok(seat) || (state_.seats[seat].kind != Kind::Open && state_.seats[seat].kind != Kind::Cpu)) return false;
    return shared(cpu_seat_frame(seat, level, state_.rev));
  }
  bool open_seat(int seat) {
    if (!host_can() || !seat_ok(seat) || state_.seats[seat].kind != Kind::Cpu) return false;
    return shared(open_seat_frame(seat, state_.rev));
  }
  // stage 0 = random. Only when can_start() says None. Doubles: the stage is ignored (Rooms draws it)
  // and the frame carries the rev alone.
  bool start(int stage) {
    if (doubles()) {
      if (can_start() != StartBlock::None) return false;
      start_stage_ = 0; start_rev_ = state_.rev; start_delay_sent_ = 0;
      start_ranked_ = ranked();
      return push(doubles_start_frame(state_.rev, start_ranked_));
    }
    if (can_start() != StartBlock::None || (stage != 0 && !stage_ok(stage))) {
      // Nothing goes out and no refusal comes back, so the guests would stay on the stage select.
      if (state_.stage_select) stage_select(false);
      return false;
    }
    start_squad_ = state_.rules.squad;
    start_stage_ = stage;
    start_rev_ = state_.rev;
    start_delay_sent_ = opt_.input_delay && input_delay_supported_ ? opt_.input_delay : 0;
    return push(start_frame(stage, state_.rev, start_delay_sent_));
  }
  // Doubles: stop the search (any seated player). Rooms refuses it with match_active once the match is
  // placed, and answers an idle cancel with nothing.
  bool cancel() {
    if (!doubles() || my_seat() < 0 || !state_.searching || left_) return false;
    return push(cancel_frame());
  }
  // Friends: the host is going to the stage select (or left it), so the guests follow it there and back.
  bool stage_select(bool open) {
    if (doubles() || !host_can()) return false;
    return shared(stage_select_frame(open, state_.rev));
  }
  // An explicit leave frees the seat at once (a dropped socket holds it 5 s): the lobby's own leave,
  // then the room's.
  void leave() {
    push(leave_frame());
    queue_.push_back({{"t", "leave"}});
    left_ = true;
  }

  // ---- views ----
  bool known() const { return known_; }
  const State& state() const { return state_; }
  int64_t rev() const { return state_.rev; }
  // The room's seats: 4, or 6 once Rooms seats six (2 in Doubles).
  int size() const { return state_.size; }
  int my_seat() const {
    for (const auto& s : state_.seats) if (s.kind == Kind::Human && s.id == opt_.me) return s.seat;
    return -1;
  }
  // Doubles: seat 0 owns the partner seat, whoever opened the home.
  bool is_host() const { return my_seat() >= 0 && (doubles() ? my_seat() == 0 : state_.host == my_seat()); }
  bool doubles() const { return opt_.profile == Profile::Doubles; }
  Profile profile() const { return opt_.profile; }
  bool searching() const { return doubles() && state_.searching; }
  // Doubles: the ranked queue. Rooms' word when its state names the queue, else this player's own choice.
  bool ranked() const { return doubles() && (state_.ranked_shown ? state_.ranked : opt_.ranked); }
  // Rooms refused the ranked ask (it has no ranked Doubles yet): START is not offered again on this home.
  bool ranked_refused() const { return ranked_refused_; }
  bool lobby_open() const { return known_ && state_.setup.is_null() && !has_setup_ && !left_ && !searching(); }
  StartBlock can_start() const {
    if (my_seat() < 0) return StartBlock::NotSeated;
    if (doubles()) {
      // Any seated player starts, alone too. Rooms checks player_away, digest_missing, digest_mismatch,
      // then pick_required, and refuses a ranked queue (unranked_only).
      if (searching()) return StartBlock::Searching;
      if (!lobby_open()) return StartBlock::MatchActive;
      // Ranked is people only: a CPU partner or a second controller can't earn or lose rating.
      if (ranked())
        for (int i = 0; i < kDoublesSeats; ++i)
          if (state_.seats[i].kind == Kind::Cpu || state_.seats[i].kind == Kind::Local) return StartBlock::RankedHumans;
      for (int i = 0; i < kDoublesSeats; ++i)
        if (state_.seats[i].kind != Kind::Open && !state_.seats[i].picked) return StartBlock::PickRequired;
      if (ready_seen_)
        for (int i = 0; i < kDoublesSeats; ++i)
          if ((state_.seats[i].kind == Kind::Human || state_.seats[i].kind == Kind::Local) && !state_.seats[i].ready) return StartBlock::NotReady;
      return StartBlock::None;
    }
    if (!is_host()) return StartBlock::NotHost;
    if (!lobby_open()) return StartBlock::MatchActive;
    if (state_.rules.squad) {
      int humans = 0, filled = 0;
      for (const auto& s : state_.seats) if (s.kind != Kind::Open) {
        ++filled; if (s.kind == Kind::Human) ++humans;
      }
      if (filled != 2 || humans != 2) return StartBlock::SquadTwoPlayers;
      if (!squad_available()) return StartBlock::SquadCapability;
      // Rooms checks the peer's hello capability and budget; state seats never echo them.
      for (const auto& s : state_.seats) if (s.kind == Kind::Human && s.lineup.size() != 4) return StartBlock::PickRequired;
    }
    int filled = 0, humans = 0, first_team = -1;
    bool one_team = true;
    for (const auto& s : state_.seats) {
      if (s.kind == Kind::Open) continue;
      if (!s.picked) return StartBlock::PickRequired;
      ++filled;
      if (s.kind == Kind::Human) ++humans;
      if (first_team < 0) first_team = s.pick.team;
      else if (s.pick.team != first_team) one_team = false;
    }
    if (filled < 2) return StartBlock::PlayersRequired;
    if (humans < 2) return StartBlock::HumansRequired;   // Rooms: humans_required (CPUs alone aren't a match)
    if (ready_seen_)
      for (const auto& s : state_.seats) if (s.kind == Kind::Human && !s.ready) return StartBlock::NotReady;
    if (state_.rules.teams && one_team) return StartBlock::TeamsRequired;
    return StartBlock::None;
  }
  // The last refusal Rooms sent this player (error, action), until the next one; refusal_count() goes up
  // by one with each, so the screen shows each refusal's hint once (yougame_lobby_screen.h).
  const std::string& refusal() const { return refusal_; }
  uint32_t refusal_count() const { return refusal_count_; }
  const std::string& refusal_action() const { return refusal_action_; }
  bool hello_supported() const { return hello_supported_; }
  bool unsupported() const { return unsupported_; }   // the room has no Friends lobby
  bool input_delay_supported() const { return input_delay_supported_; }
  // The started match: set once, by the setup broadcast (or a state carrying the frozen setup).
  bool has_setup() const { return has_setup_; }
  const Setup& setup() const { return setup_; }
  // The game reached the splash on this setup (yougame_link.cpp game_started). Until then a state with
  // no setup means Rooms dropped the match before it began, so the setup is let go and the select is back.
  void match_began() { if (has_setup_) began_ = true; }
  bool began() const { return began_; }
  // This game refused the frozen setup before frame 1 (Squad Strike fighters it can't load). A guest
  // asks Rooms to end that match now (Rooms takes it from a seated guest before any final, within 60 s
  // of the session start, and sends sessionEnded setup_refused), so the host is back at the select
  // without waiting for the link to drop. The host calls the setup off itself (next_match).
  void refuse_setup() {
    if (!has_setup_ || began_ || setup_.match_id.empty() || is_host() || refused_ == setup_.match_id) return;
    refused_ = setup_.match_id;
    call_off_setup(setup_.match_id);
  }
  // Other humans who took a seat since the last call: the game opens a direct link to each (Rooms
  // keeps lobby links across roster changes but never opens one itself).
  std::vector<Seated> take_seated() { return std::exchange(seated_, {}); }
  // A lobby frame the game could not use: logged by the caller, then cleared.
  const char* take_bad_frame() { return std::exchange(bad_, nullptr); }
  // The match is over and everyone is back on the select: the room stays, with its seats, picks and
  // rules, and the next Start freezes a fresh setup. The old match's setup is never taken again (a late
  // setup broadcast with its matchId, or a state that still carries its frozen setup until Rooms clears
  // it); the next match's arrives as a new setup, or a frozen setup after a state without one.
  // A setup that never reached the splash (a link dropped or the setup hash disagreed before frame 1) is
  // still Rooms' active match, which refuses every pick, rule, seat and Start with match_active: it stays a
  // pending call-off until a state without a setup arrives. The host calls it off now; a guest who becomes
  // the host while the room still carries that frozen setup calls it off then (adopt). Everyone waits for
  // the state without a setup. (A setup without its own matchId never got this far: plan() refuses it.)
  void next_match() {
    if (has_setup_ && !began_ && !setup_.match_id.empty()) {
      pending_call_off_ = setup_.match_id;
      pending_frozen_ = state_.setup;
      call_off_sent_ = false;
      if (is_host()) send_call_off();
    }
    if (has_setup_ && !setup_.match_id.empty()) done_match_ = setup_.match_id;
    stale_frozen_ = !state_.setup.is_null();
    has_setup_ = false;
    began_ = false;
    setup_ = Setup{};
    last_shared_ = json();
    resends_ = 0;
  }
  // What a session replacing this one on a new socket must still call off (Options::call_off_id): the
  // pending call-off, or a setup that never began. Empty id: nothing.
  struct CallOff { std::string id; json setup; };
  CallOff call_off() const {
    if (!pending_call_off_.empty()) return {pending_call_off_, pending_frozen_};
    if (has_setup_ && !began_ && !setup_.match_id.empty()) return {setup_.match_id, state_.setup};
    return {};
  }
  const std::string& pending_call_off() const { return pending_call_off_; }

 private:
  // The one place the call-off's shape lives (a room frame, not a lobby op, so it skips frame_text).
  void call_off_setup(const std::string& match_id) {
    queue_.push_back({{"t", "end-session"}, {"sessionId", match_id}});
  }
  void send_call_off() {
    if (pending_call_off_.empty() || call_off_sent_) return;
    call_off_setup(pending_call_off_);
    call_off_sent_ = true;
  }
  bool take(const Incoming& in) {
    if (in.op == Op::None) {
      if (in.why) bad_ = in.why;
      return in.why != nullptr;
    }
    if (in.op == Op::Setup) {
      if (!has_setup_ && (done_match_.empty() || in.setup.match_id != done_match_) &&
          (pending_call_off_.empty() || in.setup.match_id != pending_call_off_)) { setup_ = in.setup; has_setup_ = true; }
      return true;
    }
    const State& s = in.state;
    const bool refusal = !s.error.empty();
    // Rooms' teams-search-error: the search this home was in ended on the other team's version.
    const std::string error = doubles() && s.error == "digest_mismatch" && state_.searching && !s.searching ? kTeamsMismatch : s.error;
    if (refusal && !s.action.empty()) { refusal_ = error; refusal_action_ = s.action; }
    else if (refusal) { refusal_ = error; refusal_action_.clear(); }
    if (refusal) ++refusal_count_;
    if (s.bare) {   // a room without the lobby (`unsupported`): nothing to adopt
      if (s.error == "unsupported") unsupported_ = true;
      return true;
    }
    adopt(s);
    if (refusal) react(s);
    return true;
  }
  void adopt(const State& s) {
    const int before = my_seat();
    std::array<std::string, kSeatsMax> old_ids;
    for (int i = 0; i < kSeatsMax; ++i) old_ids[i] = known_ && state_.seats[i].kind == Kind::Human ? state_.seats[i].id : "";
    if (state_.seats[1].kind != s.seats[1].kind || state_.seats[1].connection_id != s.seats[1].connection_id) {
      local_hover_has_sent_ = local_hover_pending_ = false;
    }
    if (state_.rules.squad != s.rules.squad) { last_lineup_ = json(); lineup_resends_ = 0; }
    if (state_.squad_protocol != s.squad_protocol) squad_hello_ = false;
    state_ = s;
    state_.error.clear(); state_.action.clear(); state_.has_refused_seat = false;
    known_ = true;
    for (const auto& seat : state_.seats) if (seat.ready_shown) ready_seen_ = true;
    for (const auto& seat : state_.seats)
      if (seat.kind == Kind::Human && seat.id != opt_.me && old_ids[seat.seat] != seat.id) seated_.push_back({seat.seat, seat.id});
    const int now_seat = my_seat();
    if (now_seat < 0) { hello_seat_ = -1; hover_has_sent_ = false; hover_pending_ = false; }
    else if (now_seat != before || hello_seat_ != now_seat || (squad_available() && !squad_hello_)) {
      if (now_seat != before) { hover_has_sent_ = false; }
      send_hello(now_seat);
    }
    if (state_.setup.is_null()) {
      stale_frozen_ = false;
      pending_call_off_.clear(); pending_frozen_ = json(); call_off_sent_ = false;   // the room let it go
      if (has_setup_ && !began_) { has_setup_ = false; setup_ = Setup{}; }   // dropped before the splash
    } else if (!pending_call_off_.empty() && !has_setup_ &&
               (pending_frozen_.is_null() || state_.setup == pending_frozen_)) {
      // The called-off match's frozen setup, still in the room: stale (never planned), and called off the
      // first time this player is the host (a host who moved before frame 1, or a re-joined socket).
      stale_frozen_ = true;
      if (pending_frozen_.is_null()) pending_frozen_ = state_.setup;
      if (is_host()) send_call_off();
    }
    if (!state_.setup.is_null() && !has_setup_ && !stale_frozen_) {
      Setup frozen;
      if (!read_setup_object(state_.setup, &frozen)) { setup_ = frozen; has_setup_ = true; }
    }
  }
  void react(const State& s) {
    const std::string& e = s.error;
    const std::string& a = s.action;
    if (a == "hello" && e == "shape") {
      if (squad_hello_) {
        squad_hello_supported_ = false; squad_hello_ = false; hello_seat_ = -1;
        if (my_seat() >= 0) send_hello(my_seat());
      } else hello_supported_ = false;
      return;
    }
    // The ranked ask, refused: by name (unranked_only), or as a key Rooms does not know yet (shape,
    // fixed_rules). Never sent again as unranked by itself: the player chose ranked.
    if (a == "start" && doubles() && start_ranked_ && (e == "unranked_only" || e == "shape" || e == "fixed_rules")) {
      start_ranked_ = false; ranked_refused_ = true;
      refusal_ = "unranked_only";
      return;
    }
    if (a == "start" && e == "shape" && start_delay_sent_ && !start_squad_) {
      input_delay_supported_ = false;
      start_delay_sent_ = 0;
      if (s.rev == start_rev_ && can_start() == StartBlock::None) push(start_frame(start_stage_, s.rev, 0));
      return;
    }
    // The host is back on the select after a refused Start: the watching guests must not wait on it.
    if (a == "start" && state_.stage_select) stage_select(false);
    if (a == "start" && e == "digest_missing" && my_seat() >= 0) { hello_seat_ = -1; send_hello(my_seat()); return; }
    if (a == "lineup" && e == "revision" && !last_lineup_.is_null() && squad_available() &&
        state_.rules.squad && lobby_open() && last_lineup_.value("seat", -1) == my_seat() && lineup_resends_ < kMaxResends) {
      last_lineup_["rev"] = state_.rev; ++lineup_resends_; push(last_lineup_); return;
    }
    if (e != "revision" || last_shared_.is_null() || last_shared_.value("op", "") != a) return;
    if (a == "start" || resends_ >= kMaxResends || !is_host()) return;
    // Send the same change on the rev Rooms just handed back, if it still applies.
    json again = last_shared_;
    again["rev"] = s.rev;
    if (a == "seat") {
      const auto kind = state_.seats[again.value("seat", 0)].kind;
      if (again.value("kind", "") == "local" && kind != Kind::Open) return;
      if (again.value("kind", "") == "cpu" && kind != Kind::Open && kind != Kind::Cpu) return;
      if (again.value("kind", "") == "open" && kind != Kind::Cpu && !owns_local()) return;
    }
    if ((a == "pick" || a == "unpick") && !cpu_seat(again.value("seat", -1))) return;
    ++resends_;
    push_shared(std::move(again));
  }
  void send_hello(int seat) {
    const bool squad = squad_available() && squad_hello_supported_;
    if (!hello_supported_ || opt_.settings_digest.empty() || opt_.card_digest.empty() ||
        (hello_seat_ == seat && (!squad || squad_hello_))) return;
    json hello = hello_frame(opt_.settings_digest, opt_.card_digest);
    if (squad) { hello["squadVersion"] = 3; hello["squadBudget"] = squad_budget_; }
    if (push(hello)) { hello_seat_ = seat; squad_hello_ = squad; }
  }
  bool host_can() const { return is_host() && lobby_open(); }
  // A seat the host may change: any of the room's (four, or six), or in Doubles only the partner's (seat 1).
  bool seat_ok(int seat) const { return doubles() ? seat == 1 : seat >= 0 && seat < state_.size; }
  bool cpu_seat(int seat) const { return seat_ok(seat) && state_.seats[seat].kind == Kind::Cpu; }
  static bool pick_ok(const Pick& p) {
    return p.ch >= 0 && p.ch <= kCharMax && p.color >= 0 && p.color <= kColorMax && p.team >= 0 && p.team <= kTeamMax;
  }
  bool push(json f) {
    if (frame_text(f).empty()) return false;
    queue_.push_back(std::move(f));
    return true;
  }
  bool shared(json f) { resends_ = 0; return push_shared(std::move(f)); }
  bool push_shared(json f) {
    last_shared_ = f;
    return push(std::move(f));
  }

  std::string squad_budget_;
  bool squad_hello_supported_ = true;
  bool squad_hello_ = false, start_squad_ = false, saved_rules_ok_ = false;
  Rules saved_rules_;
  json last_lineup_;
  int lineup_resends_ = 0;
  Options opt_;
  State state_;
  bool known_ = false, has_setup_ = false, began_ = false, left_ = false, unsupported_ = false;
  Setup setup_;
  std::vector<json> queue_;
  std::vector<Seated> seated_;
  const char* bad_ = nullptr;
  std::string refusal_, refusal_action_;
  uint32_t refusal_count_ = 0;
  // next_match: the finished match's id, and whether the state still shows its frozen setup
  std::string done_match_;
  bool stale_frozen_ = false;
  // a setup that never began, until a state without a setup: its matchId, its frozen setup as the state
  // showed it (null: not seen yet), and whether this player's end-session went out
  std::string pending_call_off_;
  json pending_frozen_;
  bool call_off_sent_ = false;
  std::string refused_;   // the setup this guest refused (refuse_setup), sent once
  // hover
  Pick local_hover_, local_hover_sent_;
  bool local_hover_pending_ = false, local_hover_has_sent_ = false;
  uint64_t local_hover_sent_at_ = 0;
  Pick hover_, hover_sent_;
  bool hover_pending_ = false, hover_has_sent_ = false;
  uint64_t hover_sent_at_ = 0;
  uint64_t last_sent_ = 0;
  // hello, start, resends
  int hello_seat_ = -1;
  bool hello_supported_ = true, input_delay_supported_ = true, ready_seen_ = false;
  int start_stage_ = 0, start_delay_sent_ = 0;
  bool start_ranked_ = false, ranked_refused_ = false;
  int64_t start_rev_ = -1;
  json last_shared_;
  int resends_ = 0;
};
// A seat's direct link as the desktop app reports it: route "direct", "lan", "relay" or "unknown" once the
// link is up (empty before), and the newest round trip in ms (-1 none yet).
struct LinkInfo {
  std::string route;
  int rtt = -1;
  bool operator==(const LinkInfo& o) const { return route == o.route && rtt == o.rtt; }
};
// The screen's handle on the session, which lives on the search thread (find_match's lobby route, or the
// parked room's pump between matches): the screen posts intents from the game thread, the session's
// thread runs them each turn and publishes what the screen draws. The 4-door select screen is its only
// caller. One Control lasts as long as the lobby does: across matches, and across a reconnect.
class Control {
 public:
  using Intent = std::function<void(Session&)>;
  // The view carries the profile from the start: before the session first runs, a Doubles select drew
  // the 4-door Friends layout ("Opening the lobby"), then snapped to 2 doors (staging report e22413bd).
  explicit Control(Profile p = Profile::Friends4, bool ranked = false) : profile_(p), ranked_(ranked && p == Profile::Doubles) {
    view_.profile = p; view_.ranked = ranked_;
  }
  // Doubles: the queue the Online menu's slider chose when this home was opened.
  bool ranked_choice() const { return ranked_; }
  Profile profile() const { return profile_; }
  // Doubles: matchmaking placed the home in a match room (the setup arrives there, not in the home),
  // so the select shows Starting until the match ends and set_matched(false) brings it back.
  void set_matched(bool on) { std::lock_guard<std::mutex> lock(mutex_); matched_ = on; view_.started = view_.started || on; }
  bool matched() const { std::lock_guard<std::mutex> lock(mutex_); return matched_; }
  struct View {
    bool known = false, host = false, started = false, unsupported = false;
    bool owns_local = false;
    bool squad_available = false;
    std::string squad_budget;
    bool searching = false;                // Doubles: Rooms is looking for opponents (and a partner)
    bool ranked = false;                   // Doubles: the ranked queue
    Profile profile = Profile::Friends4;
    State state;
    int my_seat = -1;
    StartBlock start = StartBlock::NotSeated;
    std::string refusal, refusal_action;   // Rooms' last refusal of this player's frame
    uint32_t refusal_count = 0;
    std::array<LinkInfo, kSeatsMax> links{};  // by seat; empty for this player's own seat and open seats
    std::string room;                      // the room's code once known (the invite picker's room)
  };
  void post(Intent f) { std::lock_guard<std::mutex> lock(mutex_); intents_.push_back(std::move(f)); }
  View view() const { std::lock_guard<std::mutex> lock(mutex_); return view_; }
  // The session's thread, each turn: the posted intents in order, then the new view. links and room are
  // what the caller knows of the room right now.
  void run(Session& s, const std::array<LinkInfo, kSeatsMax>& links = {}, const std::string& room = {}) {
    std::vector<Intent> todo;
    { std::lock_guard<std::mutex> lock(mutex_); todo.swap(intents_); }
    for (auto& f : todo) f(s);
    View v;
    v.known = s.known(); v.host = s.is_host(); v.unsupported = s.unsupported();
    v.squad_available = s.squad_available(); v.squad_budget = s.squad_budget();
    v.owns_local = s.owns_local(); v.searching = s.searching(); v.profile = profile_;
    v.ranked = s.ranked();
    v.state = s.state(); v.my_seat = s.my_seat(); v.start = s.can_start();
    v.refusal = s.refusal(); v.refusal_action = s.refusal_action(); v.refusal_count = s.refusal_count();
    v.links = links;
    v.room = room;
    std::lock_guard<std::mutex> lock(mutex_);
    v.started = s.has_setup() || matched_;
    view_ = std::move(v);
  }
 private:
  Profile profile_;
  bool ranked_ = false;
  bool matched_ = false;
  mutable std::mutex mutex_;
  std::vector<Intent> intents_;
  View view_;
};

// Direct links opened as players take seats (Rooms' desktop keeps lobby links across roster changes,
// but only the game opens one: directOpen {conn, peer}). The match then plays on the links its humans
// already have, opening any that are missing. `request` is the bridge call; it may throw. A failed open
// is just "no link", except an exception of type Fatal (the forced update), which is passed on.
struct NeverFatal {};
struct EarlyLinks {
  std::map<std::string, unsigned> by_peer;   // the peer's connection id -> link id
  template <class Fatal = NeverFatal, class Request>
  bool open(int conn, const std::string& peer, Request request, int ms = 3000) {
    if (peer.empty() || by_peer.count(peer)) return by_peer.count(peer) != 0;
    try {
      const json answer = request(json{{"type", "directOpen"}, {"conn", conn}, {"peer", peer}}, ms);
      const unsigned id = answer.is_object() && integer_at(answer, "link", 1, 65534) ? (unsigned)answer["link"].get<int64_t>() : 0u;
      if (!id || id > 65534) return false;
      for (const auto& kv : by_peer) if (kv.second == id) return false;
      by_peer[peer] = id;
      return true;
    } catch (const Fatal&) {
      throw;
    } catch (...) { return false; }
  }
  // A peer that left: its link is torn down (a rejoin gets a new link id and generation).
  template <class Request>
  void forget(const std::string& peer, Request request) {
    const auto it = by_peer.find(peer);
    if (it == by_peer.end()) return;
    const unsigned id = it->second;
    by_peer.erase(it);
    try { request(json{{"type", "directClose"}, {"link", id}}, 1000); } catch (...) {}
  }
  template <class Request>
  void forget_all(Request request) {
    while (!by_peer.empty()) forget(by_peer.begin()->first, request);
  }
  // Links that died while the lobby waited (the peer never answered) go, so the next open makes a new one.
  template <class State, class Request>
  void forget_dead(State state, Request request) {
    std::vector<std::string> dead;
    for (const auto& kv : by_peer) if (state(kv.second) == 3) dead.push_back(kv.first);
    for (const auto& peer : dead) forget(peer, request);
  }
  // The links for these peers, in their order, opening the missing ones; empty when one can't open.
  template <class Fatal = NeverFatal, class Request>
  std::vector<unsigned> for_peers(int conn, const std::vector<std::string>& peers, Request request) {
    std::vector<unsigned> ids;
    for (const auto& peer : peers) {
      if (!open<Fatal>(conn, peer, request)) return {};
      ids.push_back(by_peer[peer]);
    }
    return ids;
  }
  // Match start: links to anyone not in the match are closed, the match's are opened where missing and
  // handed over (no longer held here: the room closes them). When one can't open, nothing is handed
  // over and every link stays here to be closed.
  template <class Fatal = NeverFatal, class Request, class Close>
  std::vector<unsigned> hand_over(int conn, const std::vector<std::string>& peers, Request request, Close close) {
    std::vector<std::string> extra;
    for (const auto& kv : by_peer)
      if (std::find(peers.begin(), peers.end(), kv.first) == peers.end()) extra.push_back(kv.first);
    for (const auto& peer : extra) forget(peer, close);
    std::vector<unsigned> ids = for_peers<Fatal>(conn, peers, request);
    if (ids.size() == peers.size()) by_peer.clear();
    return ids;
  }
};
}  // namespace yougame::lobby
