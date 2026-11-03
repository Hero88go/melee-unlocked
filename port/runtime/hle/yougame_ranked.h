// YouGame Ranked: one best-of-3 set per room, and the player's rank. Kept independent of WinHTTP
// for deterministic socket tests (port/tests/yougame_ranked_test.cpp).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace yougame::ranked {
using json = nlohmann::json;

// A string field, or "" when it is missing or not a string (json::value throws on a wrong type).
inline std::string str(const json& m, const char* key) {
  if (!m.is_object()) return {};
  const auto it = m.find(key);
  return it != m.end() && it->is_string() ? it->get<std::string>() : std::string();
}

inline std::string ticket_error(const json& answer) {
  const std::string code = str(answer, "code"), error = str(answer, "error");
  if (code == "ranked_signed_out") return "Sign in to YouGame to play Ranked.";
  if (code == "ranked_off") return "Ranked is off for this game.";
  return error.empty() ? "YouGame request failed" : error;
}

// Ranked v1 plays Melee Unlocked's Unranked screens (character select, a random legal stage each
// game) and is rated on YouGame's ladder as one best-of-3 set per room. The game sends no set
// result in that flow (there is no GameSetup scene, so no C2): the set is decided here, from the
// games the room settled, which both players receive alike.
constexpr int kSetWins = 2;
// Draws and games the room voids do not count. A set still undecided after this many settled
// games is void (it cannot produce a winner); it only bounds the reports.
constexpr int kMaxGames = 9;
// How many times (every kRetryMs) the host asks the room to start the set's match.
constexpr int kBeginTries = 15;
constexpr uint64_t kRetryMs = 2000;
// A set whose room has not started its match this long after its first game began cannot be
// recorded: the pair breaks up instead of playing unrated games under Ranked.
constexpr uint64_t kStartWaitMs = kBeginTries * kRetryMs + 5000;

// One game's winner (0-based port, -1 for none) from the game's own end record. `end_method` is
// Slippi's Game End method: 1 TIME!, 2 GAME!, 7 a No Contest. Ranked has no LRA-Start (the game
// installs no pause in mode 0), so a No Contest there is the online code ending the game for a
// disconnect or a desync, naming the other player as `lras`; either way the player it names
// quit: they lose the game, and the set (Game::forfeit). `placements` are the record's four
// placement bytes (0 first, 0xFF no player); without them `reported` (Slippi's winnerIdx) is used.
inline int game_winner(int end_method, int lras, int reported, const int* placements) {
  if (end_method == 7) return lras == 0 || lras == 1 ? 1 - lras : -1;
  if (placements && (end_method == 1 || end_method == 2)) {
    int winner = -1;
    for (int port = 0; port < 4; ++port) {
      if (placements[port] != 0) continue;
      if (port > 1 || winner >= 0) return -1;   // a tie, or a placement outside the duel
      winner = port;
    }
    return winner;
  }
  return reported == 0 || reported == 1 ? reported : -1;
}

// Casual/Direct duels use the same placements, but a No Contest never awards a winner.
// Multiplayer uses team_winner below; this single-port result remains duel-only.
inline int casual_winner(int end_method, int lras, int reported, const int* placements, int players) {
  if (players != 2 || end_method == 7) return -1;
  return game_winner(end_method, lras, reported, placements);
}

// A duel's TIME! is Slippi's to decide (SinglesDetermineWinner: the ledge grab limit, then stocks,
// then damage; an exact tie has no winner), and only the game's report carries that: winnerIdx,
// sent once the game can no longer roll back (slippi_online.cpp handle_report_game). The Game End
// record ranks a time-out the way Melee's results screen does (stocks, kills, falls, then port),
// and the Source Port sends it before the last frames are confirmed: on 10-28 two tied ranked
// time-outs (4 stocks and 0% each) went to port 1 from the record while the report said no winner.
// So a duel's TIME! from the record (`from_record`: it has placements) waits for the report.
// Teams has no report (ReportGameQuery has no teams result) and keeps the record.
inline bool time_out_waits_for_report(int end_method, bool from_record, int players) {
  return end_method == 1 && from_record && players == 2;
}

// Teams v1 requires exactly two known players per team. An absent/invalid seat cannot win.
inline bool valid_teams(const int* teams) {
  if (!teams) return false;
  int count[2] = {};
  for (int port = 0; port < 4; ++port) {
    if (teams[port] != 0 && teams[port] != 1) return false;
    ++count[teams[port]];
  }
  return count[0] == 2 && count[1] == 2;
}

// Game End placements are 0-based (0 first, 0xFF absent). Winners may share first place
// within one team, or a surviving player may win after their teammate was eliminated.
// Only TIME/GAME with first place belonging exclusively to one team is conclusive. LRAS
// never awards a casual win; a No Contest, missing placements or a cross-team tie is void.
inline int team_winner(int end_method, int lras, const int* placements, const int* teams) {
  if ((end_method != 1 && end_method != 2) || lras != -1 || !placements || !valid_teams(teams)) return -1;
  int winner = -1;
  for (int port = 0; port < 4; ++port) {
    if (placements[port] < 0 || placements[port] > 3) return -1;
    if (placements[port] != 0) continue;
    if (winner >= 0 && winner != teams[port]) return -1;
    winner = teams[port];
  }
  return winner;
}

// A game report's `detail`: what the room stores beside the outcome and never compares. The room
// takes at most 8 keys of 1-4 lowercase letters, each an integer in [-1, 2^31) or up to four of
// them (rooms/src/game-lobby.ts cleanDetail); anything outside that is sent as -1.
inline json game_detail(int end_method, int lras, int stage, const int characters[2], int64_t frames) {
  const auto fit = [](int64_t v) -> int64_t { return v >= -1 && v < (int64_t(1) << 31) ? v : -1; };
  json detail = {{"end", fit(end_method)}, {"lras", fit(lras)}};
  if (stage >= 0) detail["stg"] = fit(stage);
  if (characters) detail["char"] = {fit(characters[0]), fit(characters[1])};
  if (frames >= 0) detail["frm"] = fit(frames);
  return detail;
}

// A game's report id from what both players' games negotiated for it (the match's rng offset and
// stage), so a game one side failed to count cannot shift every later id. "" when unknown.
inline std::string game_key(int64_t rng_offset, int stage) {
  if (rng_offset < 0 || rng_offset > 0xFFFFFFFFll || stage < 0 || stage > 0xFFFF) return {};
  char key[32];
  std::snprintf(key, sizeof key, "r%llx-s%d", (unsigned long long)rng_offset, stage);
  return key;
}

struct Game {
  std::string key;          // game_key(); without one the id is "g<index>-t<tiebreak>"
  uint32_t index = 0, tiebreak = 0;
  int winner = -1;        // 0-based port; -1 a draw or a game with no winner (reported void)
  bool forfeit = false;   // the loser quit (a No Contest naming them): the set goes to the winner
  json detail = json::object();
};

// One ranked room's set, as one client sees it. The rooms Worker (game-lobby.ts) settles a duel's
// game only when both players report the same outcome, and its final only when both send the
// same `finish`, or by its leave rule: once a player has been gone for its reconnect allowance
// (15 s), the player still there gets the set, unless a pending report is void or names someone
// else. So:
// - the set's winner is the first to kSetWins games the room settled (both clients see the same
//   game-result events, so both send the same finish), or the player a No Contest did not name;
// - a player who quits (leaves the online menus, holds Z, Slippi's status 40) concedes and closes
//   at once, so the leave rule can award the opponent even if the opponent's game says nothing;
// - a player whose connection to the opponent broke claims the set, but only once its own games
//   are settled: the pair may have broken because the opponent already saw the deciding result;
// - exactly one final per set: C2 (a future GameSetup port) never adds a second one.
class Set {
 public:
  void configure(int local, std::vector<std::string> participants) {
    local_ = local;
    participants_ = std::move(participants);
  }
  // A game of the set started (its first frame; the game repeats frame 1 while it waits for the
  // opponent, so only the first call of a game counts). The first game makes the host ask for the
  // match. Both players count the same games, so their reports carry the same ids.
  void start_game() {
    if (final_requested_ || closed_ || in_game_) return;
    started_ = in_game_ = true;
    ++games_started_;
  }
  int games_started() const { return games_started_; }
  // Unique room receipts/restore acknowledgements for our reports (draws/voids count too).
  int games_received() const {
    return (int)std::count_if(order_.begin(), order_.end(), [&](const std::string& id) {
      return (results_.count(id) || games_.at(id).acknowledged) && games_.at(id).received;
    });
  }
  // The game runs Slippi's GameSetup flow and sends C2 itself: C2 alone decides the set.
  void game_setup() { game_setup_ = true; }

  void report_game(const Game& game) {
    in_game_ = false;
    if (final_requested_ || !started_ || closed_) return;
    const std::string id = !game.key.empty() ? game.key : "g" + std::to_string(game.index) + "-t" + std::to_string(game.tiebreak);
    if (games_.count(id)) return;   // one report per game, however many places say it ended
    Pending report;
    report.message = {{"t", "game-report"}, {"id", id}};
    if (valid_port(game.winner)) report.message["winner"] = participants_[game.winner];
    else report.message["void"] = true;
    if (game.detail.is_object() && !game.detail.empty()) report.message["detail"] = game.detail;
    games_.emplace(id, std::move(report));
    order_.push_back(id);
    if (game.forfeit && valid_port(game.winner) && forfeit_ < 0) forfeit_ = game.winner;
    decide();
  }
  // C2 (CMD_REPORT_SET_COMPLETE): the game's own verdict, once its games are settled.
  void complete(uint8_t end_mode) {
    if (!started_ || c2_ >= 0) return;
    c2_ = end_mode;
    decide();
  }
  // The local player left the set (Slippi's status 40, leaving the online menus, holding Z).
  void quit() {
    if (!started_ || quit_ || lost_) return;
    quit_ = true;
    decide();
  }
  // The connection to the opponent broke (their game left, or the link died).
  void lost() {
    if (!started_ || quit_ || lost_) return;
    lost_ = true;
    decide();
  }
  // A quitter whose concession is already decided leaves at once (sends what it owes, then closes),
  // so the room's leave rule can run; anyone else stays until the room's result.
  bool leaves_at_once() const { return conceded_; }

  // What is due now: the host's begin, then each game report in order (the next waits for the
  // previous one's game-result), then the final. Unchanged retries every kRetryMs.
  std::vector<json> due(uint64_t now, bool host, int revision) {
    std::vector<json> out;
    // A concession is flushed exactly once by close_room before closing the socket; the pump
    // must not send it concurrently with that flush.
    if (!started_ || settled_ || closed_ || closing_ || conceded_) return out;
    if (match_id_.empty()) {
      if (!waiting_since_) waiting_since_ = now;
      if (now - waiting_since_ >= kStartWaitMs) {
        orphaned_ = true;
        return out;
      }
      if (host && !begin_dead_ && (begin_tries_ == 0 || now >= begin_at_)) {
        out.push_back({{"t", "begin"}, {"revision", revision}});
        begin_at_ = now + kRetryMs;
        if (++begin_tries_ >= kBeginTries) begin_dead_ = true;
      }
      return out;
    }
    for (const auto& id : order_) {
      auto& p = games_.at(id);
      if (p.received) continue;
      if (!p.sent || now >= p.retry_at) {
        p.message["matchId"] = match_id_;
        out.push_back(p.message);
        p.sent = true;
        p.retry_at = now + kRetryMs;
      }
      return out;
    }
    if (final_requested_ && !final_dead_ && (!final_sent_ || now >= final_at_)) {
      final_["matchId"] = match_id_;
      out.push_back(final_);
      final_sent_ = true;
      final_at_ = now + kRetryMs;
    }
    return out;
  }
  // A quitter's last words before it closes: everything still owed, once, unfenced.
  std::vector<json> flush() {
    std::vector<json> out;
    if (match_id_.empty() || settled_ || closed_ || closing_) return out;
    for (const auto& id : order_) {
      auto& p = games_.at(id);
      if (p.received) continue;
      p.message["matchId"] = match_id_;
      out.push_back(p.message);
      p.sent = true;
    }
    if (final_requested_ && !final_dead_) {
      final_["matchId"] = match_id_;
      out.push_back(final_);
      final_sent_ = true;
    }
    return out;
  }

  // A room message. True only for this set's first final result.
  bool receive(const json& message) {
    const std::string type = str(message, "t");
    if (type == "start" && match_id_.empty()) {
      match_id_ = str(message, "matchId");
      if (!match_id_.empty()) orphaned_ = false;   // late, but before the pair broke up: play on
      return false;
    }
    if (match_id_.empty() || str(message, "matchId") != match_id_) return false;
    if (type == "game-result") {
      const std::string id = str(message, "id");
      if (id.empty()) return false;
      if (!results_.count(id)) results_[id] = settled_winner(message);   // a repeated receipt counts once
      const auto it = games_.find(id);
      if (it != games_.end() && it->second.sent) it->second.received = true;
      decide();
    } else if (type == "result" && !settled_) {
      settled_ = true;
      return true;
    }
    return false;
  }
  // A command-error from the room: drop what it refuses for good instead of resending it forever.
  void refused(const json& message, bool restored = false) {
    const std::string action = str(message, "action"), error = str(message, "error");
    // The room is recording the match's result or already has it: nothing more to send; wait.
    if (error == "The match result is being recorded" || error == "The set is over") {
      closing_ = true;
      return;
    }
    if (action == "begin") {
      // A roster still settling or a player reconnecting may pass; anything else will not.
      if (error.rfind("Waiting for", 0) != 0 && error.rfind("The lobby changed", 0) != 0) begin_dead_ = true;
    } else if (action == "game-report") {
      // Normally wait for the authoritative result. After a restore it may already have been
      // broadcast while we were away, so an acknowledgement must also advance the fence.
      if (error.find("already being recorded") != std::string::npos || error.find("already recorded") != std::string::npos) {
        if (restored) {
          const auto it = games_.find(str(message, "id"));
          if (it != games_.end() && it->second.sent && !it->second.received) {
            // Advance the receipt fence without inventing the room's settled outcome.
            // A delayed game-result remains authoritative for the set's score.
            it->second.received = it->second.acknowledged = true;
            decide();
          }
        }
        return;
      }
      const auto it = games_.find(str(message, "id"));
      if (it != games_.end() && it->second.sent && !it->second.received) {
        it->second.received = it->second.refused = true;   // the fence moves on; it counts nothing
        decide();
      }
    } else if (action == "finish") {
      if (error.rfind("Wait for individual game reports", 0) != 0) final_dead_ = true;
    }
  }
  void room_closed() { closed_ = true; }

  bool started() const { return started_; }
  bool decided() const { return final_requested_; }
  bool settled() const { return settled_; }
  bool closed() const { return closed_; }
  // The game thread breaks the pair: the set has a final, a result, no room any more, or a room
  // that never started its match.
  bool over() const { return final_requested_ || settled_ || closed_ || orphaned_; }
  // A report still waits for its game-result: no next game may start yet.
  bool awaiting_receipt() const {
    if (settled_ || closed_ || match_id_.empty()) return false;
    for (const auto& id : order_) if (!games_.at(id).received) return true;
    return false;
  }
  // The room started a match for this set and has not given its result: leaving now could cost it.
  bool needs_drain() const { return !match_id_.empty() && !settled_ && !closed_; }
  const json& final_message() const { return final_; }
  const std::string& local_participant() const {
    static const std::string empty;
    return valid_port(local_) ? participants_[local_] : empty;
  }

 private:
  bool valid_port(int port) const {
    return participants_.size() == 2 && port >= 0 && port < 2 && !participants_[port].empty();
  }
  int port_of(const std::string& participant) const {
    for (int port = 0; port < 2 && port < (int)participants_.size(); ++port)
      if (!participant.empty() && participants_[port] == participant) return port;
    return -1;
  }
  int settled_winner(const json& m) const {
    if (!m.is_object()) return -1;
    const auto flag = [&](const char* key) { const auto it = m.find(key); return it != m.end() && it->is_boolean() && it->get<bool>(); };
    if (flag("void") || flag("draw")) return -1;
    const auto ranking = m.find("ranking");
    if (ranking == m.end() || !ranking->is_array() || ranking->empty() || !(*ranking)[0].is_string()) return -1;
    return port_of((*ranking)[0].get<std::string>());
  }
  std::array<int, 2> wins() const {
    std::array<int, 2> out{};
    for (const auto& [id, winner] : results_) if (winner == 0 || winner == 1) ++out[winner];
    return out;
  }
  bool fence_clear() const {
    for (const auto& id : order_) if (!games_.at(id).received) return false;
    return true;
  }
  void finish(int port) {
    final_requested_ = true;
    final_ = {{"t", "finish"}};
    if (valid_port(port)) final_["winner"] = participants_[port];
    else final_["void"] = true;
  }
  void decide() {
    if (final_requested_ || !started_) return;
    if (!game_setup_) {
      if (forfeit_ >= 0) return finish(forfeit_);
      const auto w = wins();
      for (int port = 0; port < 2; ++port) if (w[port] >= kSetWins) return finish(port);
      if ((int)results_.size() >= kMaxGames) return finish(-1);
    }
    if (!fence_clear()) return;
    if (c2_ >= 0) {
      const auto w = wins();
      return finish(c2_ == 0 && w[0] != w[1] ? (w[0] > w[1] ? 0 : 1) : -1);
    }
    if (quit_) {
      conceded_ = true;
      return finish(valid_port(local_) ? 1 - local_ : -1);
    }
    if (lost_) return finish(valid_port(local_) ? local_ : -1);
  }
  struct Pending { json message; bool sent = false, received = false, refused = false, acknowledged = false; uint64_t retry_at = 0; };
  int local_ = -1;
  std::vector<std::string> participants_, order_;
  std::map<std::string, Pending> games_;
  std::map<std::string, int> results_;   // game id -> settled winner port, -1 none
  std::string match_id_;
  int games_started_ = 0, forfeit_ = -1, c2_ = -1, begin_tries_ = 0;
  bool started_ = false, in_game_ = false, game_setup_ = false, quit_ = false, lost_ = false, conceded_ = false;
  bool begin_dead_ = false, closing_ = false, final_requested_ = false, final_sent_ = false, final_dead_ = false;
  bool settled_ = false, closed_ = false, orphaned_ = false;
  uint64_t begin_at_ = 0, final_at_ = 0, waiting_since_ = 0;
  json final_;
};

// YouGame's authoritative tier/division compressed to Slippi's three divisions (E3/E5 and the
// match-state rank bytes; nothing in the native game draws them yet).
inline uint8_t rank_enum(const json& rank) {
  if (!rank.is_object() || !rank.count("placed") || !rank["placed"].is_boolean() || !rank["placed"].get<bool>()) return 0;
  const std::string tier = str(rank, "tier");
  if (tier == "challenger") return 19; // Grandmaster
  static const std::array<const char*, 5> tiers = {"bronze", "silver", "gold", "platinum", "diamond"};
  const auto it = std::find(tiers.begin(), tiers.end(), tier);
  if (it == tiers.end() || !rank.count("division") || !rank["division"].is_number_integer()) return 0;
  const int division = rank["division"].get<int>();
  if (division < 1 || division > 4) return 0;
  return (uint8_t)(1 + 3 * (it - tiers.begin()) + (division == 1 ? 2 : division == 2 ? 1 : 0));
}

// The rank's name as YouGame writes it (web/src/lib/ranks.ts rankFor's `label`: "Gold II",
// "Challenger", "Placements 2/5"), kept to plain ASCII the game's text can show. Built from the
// tier and division with ranks.ts's names when the label is missing or unusable.
inline std::string rank_label(const json& rank, bool placed, int64_t matches) {
  std::string label;
  for (char c : str(rank, "label"))
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '/') label += c;
  if (!label.empty() && label.size() <= 20) return label;
  if (!placed) {
    int64_t total = 5;   // ranks.ts PLACEMENT_MATCHES
    if (rank.count("placementTotal") && rank["placementTotal"].is_number_integer()) total = rank["placementTotal"].get<int64_t>();
    if (total < 1 || total > 99) total = 5;
    return "Placements " + std::to_string(std::min(matches, total)) + "/" + std::to_string(total);
  }
  const std::string tier = str(rank, "tier");
  if (tier == "challenger") return "Challenger";
  static const std::array<std::pair<const char*, const char*>, 5> names = {
      {{"bronze", "Bronze"}, {"silver", "Silver"}, {"gold", "Gold"}, {"platinum", "Platinum"}, {"diamond", "Diamond"}}};
  static const char* roman[] = {"", "I", "II", "III", "IV"};
  for (const auto& [id, name] : names) {
    if (tier != id) continue;
    if (!rank.count("division") || !rank["division"].is_number_integer()) return name;
    const int division = rank["division"].get<int>();
    return division >= 1 && division <= 4 ? std::string(name) + " " + roman[division] : std::string(name);
  }
  return {};
}

// The local player's ranked standing for this process: from a ranked ticket, the bridge's mpRank,
// or a set's final result, whichever is newest. Shown in the Ranked character select's header.
class RankCache {
 public:
  // A ranks.ts Rank object ({tier, division, label, placed, matches, rating, ...}) and the rating
  // beside it (the rank's own rating when `rating` is not a number). False when it is unusable, or
  // describes a player with no ranked result yet (0 matches): the header then stays plain.
  bool standing(const json& rank, const json& rating, float change = 0) {
    if (!rank.is_object() || !rank.count("placed") || !rank["placed"].is_boolean()) return false;
    const json& r = rating.is_number() ? rating : rank.count("rating") ? rank["rating"] : json();
    if (!r.is_number() || !rank.count("matches") || !(rank["matches"].is_number_integer() || rank["matches"].is_number_unsigned()))
      return false;
    const double value = r.get<double>();
    const int64_t matches = rank["matches"].get<int64_t>();
    const bool placed = rank["placed"].get<bool>();
    if (!std::isfinite(value) || std::abs(value) > 1000000 || matches < 0 || matches > INT32_MAX) return false;
    if (!placed && matches == 0) return false;
    known_ = true;
    placed_ = placed;
    rating_ = (float)value;
    change_ = change;
    matches_ = (uint32_t)matches;
    rank_ = rank_enum(rank);
    label_ = rank_label(rank, placed, matches);
    ++version_;
    return true;
  }
  // A set's final result: ratings[participant] = {before, after, matches, rank, rankBefore}.
  bool result(const json& event, const std::string& participant) {
    if (str(event, "t") != "result" || str(event, "queue") != "ranked" || !event.count("ratings") ||
        !event["ratings"].is_object()) return false;
    const auto it = event["ratings"].find(participant);
    if (it == event["ratings"].end() || !it->is_object()) return false;
    const auto& r = *it;
    if (!r.count("before") || !r["before"].is_number() || !r.count("after") || !r["after"].is_number() ||
        !r.count("rank") || !r["rank"].is_object()) return false;
    const double before = r["before"].get<double>(), after = r["after"].get<double>();
    if (!std::isfinite(before) || std::abs(before) > 1000000) return false;
    json rank = r["rank"];
    if (r.count("matches") && (r["matches"].is_number_integer() || r["matches"].is_number_unsigned())) rank["matches"] = r["matches"];
    return standing(rank, r["after"], (float)(after - before));
  }
  uint64_t version() const { return version_; }
  bool known() const { return known_; }
  // The Ranked character select's header: "Ranked Mode" before any result, then
  // "Ranked - Placements 2/5" while placing and "Ranked - Gold II - 1534" once placed.
  std::string header() const {
    if (!known_) return "Ranked Mode";
    const std::string rating = std::to_string((long long)std::lround(rating_));
    if (!placed_) return label_.empty() ? "Ranked Mode" : "Ranked - " + label_;
    return label_.empty() ? "Ranked - " + rating : "Ranked - " + label_ + " - " + rating;
  }
  uint8_t visibility() const { return known_ ? 1 : 0; } // only the local rank is known
  uint8_t rank() const { return known_ ? rank_ : 0; }
  std::vector<uint8_t> response() const {
    std::vector<uint8_t> out = {visibility(), 1, rank()};
    append_float(out, rating_);
    append_word(out, matches_);
    append_float(out, change_);
    // RustRankInfo's enum declarations are absent from the sparse reference. Preserve the
    // existing fetch byte (1) and leave the rank-change animation neutral rather than guess.
    out.push_back(0);
    return out;
  }

 private:
  static void append_word(std::vector<uint8_t>& out, uint32_t word) {
    for (int i = 3; i >= 0; --i) out.push_back((uint8_t)(word >> (8 * i)));
  }
  static void append_float(std::vector<uint8_t>& out, float value) {
    uint32_t bits; std::memcpy(&bits, &value, sizeof bits); append_word(out, bits);
  }
  bool known_ = false, placed_ = false;
  uint8_t rank_ = 0;
  uint32_t matches_ = 0;
  float rating_ = 0, change_ = 0;
  std::string label_;
  uint64_t version_ = 0;
};

// The bridge's mpRank answer (docs/MELEE_UNLOCKED.md, "Bridge request: mpRank (proposed)"):
// {mode, rating, matches, wins, place, rank} with rank a ranks.ts Rank, or rank null for a player
// with no ranked row yet. Applied only if nothing newer reached the cache since it was asked
// (`asked` is the cache's version then): a set's result outranks an older read.
inline bool apply_rank_answer(RankCache& cache, const json& result, uint64_t asked) {
  if (!result.is_object() || cache.version() != asked || !result.count("rank") || !result["rank"].is_object()) return false;
  json rank = result["rank"];
  if (!rank.count("matches") && result.count("matches")) rank["matches"] = result["matches"];
  return cache.standing(rank, result.count("rating") ? result["rating"] : json());
}
// A bridge that does not know a request answers "Unknown request" (the page, Player.tsx) or
// "Unknown request <type>" (tools/melee-unlocked/dev-bridge.mjs): never ask it again.
inline bool unknown_request(const std::string& error) { return error.rfind("Unknown request", 0) == 0; }
} // namespace yougame::ranked
