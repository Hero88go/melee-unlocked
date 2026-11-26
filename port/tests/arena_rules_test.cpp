// Arena room rules: every rotation mode, the ask flow, giving up a spot, the doubles vote, rooms of
// up to 16, the live feed's routing, the wire form, and a random-operations test that checks the
// room never breaks its own rules.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "arena_rules.h"
#include "slippi_stage_pool.h"
#include "../runtime/gx/arena_hand.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace slippi::arena;

namespace {
int g_failed = 0, g_checks = 0;
const char* g_test = "";
#define CHECK(x) do { ++g_checks; if (!(x)) { std::printf("  FAIL %s line %d: %s\n", g_test, __LINE__, #x); ++g_failed; } } while (0)

struct Case { const char* name; std::function<void()> body; };
std::vector<Case>& cases() { static std::vector<Case> c; return c; }
struct Add { Add(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); } };
#define TEST(name) static void name(); static Add add_##name(#name, name); static void name()

Room room_of(int n) {
  Room r;
  const char* names[] = {"HERO", "WISE", "PEACE", "NOVA", "ECHO", "FOXY", "ZAP", "MOSS",
                         "RUNE", "BLAZE", "KITE", "LUX", "ONYX", "PIXEL", "QUILL", "TANGO"};
  for (int i = 0; i < n; ++i) {
    char code[16];
    std::snprintf(code, sizeof code, "%s#%d", names[i], 100 + i);
    join(r, code, names[i]);
  }
  return r;
}
// Plays a game won by the fighter (singles) or team (doubles) whose first slot holds `id`.
void win(Room& r, uint8_t id) {
  CHECK(game_start(r));
  const int s = ring_slot(r, id);
  CHECK(s >= 0);
  CHECK(game_end(r, (uint8_t)team_of_slot(r, s)));
}
std::vector<uint8_t> ring_ids(const Room& r) {
  std::vector<uint8_t> v;
  for (int s = 0; s < ring_size(r); ++s) v.push_back(r.ring[s]);
  return v;
}
bool same(const Room& a, const Room& b) { return encode(a) == encode(b); }
void dump(const Room& r) {
  std::printf("    rev %u mode %d kind %d ffa_allowed %d in_game %d members", r.rev, (int)r.mode, (int)r.kind, r.ffa_allowed, r.in_game);
  for (const Member& m : r.members) std::printf(" %u", m.id);
  std::printf("\n    ring %d %d %d %d line", r.ring[0], r.ring[1], r.ring[2], r.ring[3]);
  for (uint8_t id : r.line) std::printf(" %u", id);
  std::printf("\n    asker %d refused %d declined %d leaving %d incoming %d plays_winner %d next %d switch %d votes %04X streak %d\n",
              r.asker, r.refused, r.declined, r.leaving_slot, r.incoming, r.leaver_plays_winner, r.play_winner_next,
              r.switch_pending, r.switch_votes, r.streak_id);
  std::printf("    switch_to %d\n", (int)r.switch_to);
}
}  // namespace

// ---------------------------------------------------------------- joining and leaving
TEST(join_fills_ring_then_line) {
  Room r = room_of(4);
  CHECK(r.members.size() == 4);
  CHECK(r.ring[0] == 0 && r.ring[1] == 1);
  CHECK((r.line == std::vector<uint8_t>{2, 3}));
  CHECK(role(r, 0) == Role::Fighter && role(r, 2) == Role::InLine && role(r, 9) == Role::Absent);
  CHECK(valid(r));
}
TEST(join_refuses_same_code_any_case) {
  Room r = room_of(2);
  CHECK(join(r, "hero#100", "Someone") == kNone);
  CHECK(join(r, " HERO#100", "Someone") != kNone);   // a different code (leading space) is a different player
  CHECK(r.members.size() == 3);
}
TEST(join_full_at_sixteen) {
  Room r = room_of(16);
  CHECK(r.members.size() == 16 && valid(r));
  CHECK(join(r, "LATE#999", "LATE") == kNone);
  CHECK(r.line.size() == 14);
}
TEST(join_cleans_code_and_name) {
  Room r;
  const uint8_t id = join(r, "ab\x01" "cd#12345678", std::string("Name\n") + std::string(60, 'x'));
  const Member* m = find(r, id);
  CHECK(m && m->code == "ABCD#12345" && m->code.size() == kMaxCode);
  CHECK(m && m->name.size() == kMaxName && m->name.find('\n') == std::string::npos);
  CHECK(join(r, "\x02\x03", "x") == kNone);   // nothing left of the code
}
TEST(ids_reuse_the_lowest_free) {
  Room r = room_of(4);
  CHECK(leave(r, 1));
  CHECK(join(r, "NEW#1", "NEW") == 1);
  CHECK(owner(r) == 0);
  CHECK(leave(r, 0));
  CHECK(owner(r) == 1);
}
TEST(leave_fighter_refills_from_line) {
  Room r = room_of(4);
  CHECK(leave(r, 1));
  CHECK(r.ring[0] == 0 && r.ring[1] == 2);
  CHECK((r.line == std::vector<uint8_t>{3}));
  CHECK(!leave(r, 1));
  CHECK(valid(r));
}
TEST(last_member_leaving_empties_room) {
  Room r = room_of(1);
  CHECK(r.ring[0] == 0 && r.ring[1] == kNone);
  const uint32_t rev = r.rev;
  CHECK(leave(r, 0));
  CHECK(r.members.empty() && r.ring[0] == kNone && r.rev > rev && valid(r));
}

// ---------------------------------------------------------------- Winner stays
TEST(winner_stays_loser_to_back_front_in) {
  Room r = room_of(4);   // 0 v 1, line 2 3
  win(r, 0);
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 2}));
  CHECK((r.line == std::vector<uint8_t>{3, 1}));
  win(r, 2);
  CHECK((ring_ids(r) == std::vector<uint8_t>{3, 2}));
  CHECK((r.line == std::vector<uint8_t>{1, 0}));
}
TEST(winner_stays_with_nobody_waiting) {
  Room r = room_of(2);
  win(r, 1);
  win(r, 0);
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1}));
  CHECK(r.line.empty() && r.games == 2);
}
TEST(streak_counts_and_moves) {
  Room r = room_of(4);
  win(r, 0); win(r, 0);
  CHECK(r.streak_id == 0 && r.streak == 2);
  win(r, 3);   // 0 v 3 (2 lost the second game); 3 wins
  CHECK(r.streak_id == 3 && r.streak == 1);
}
TEST(record_wins_and_losses) {
  Room r = room_of(3);
  win(r, 0); win(r, 0); win(r, 1);
  CHECK(find(r, 0)->wins == 2 && find(r, 0)->losses == 1);
  CHECK(find(r, 2)->losses == 1 && find(r, 1)->wins == 1 && find(r, 1)->losses == 1);
}
TEST(eight_member_rotation_everyone_plays) {
  Room r = room_of(8);
  std::set<uint8_t> played;
  for (int g = 0; g < 7; ++g) { for (uint8_t id : ring_ids(r)) played.insert(id); win(r, r.ring[g % 2]); }
  for (uint8_t id : ring_ids(r)) played.insert(id);
  CHECK(played.size() == 8);
  CHECK(r.games == 7 && valid(r));
}

// ---------------------------------------------------------------- Take turns
TEST(take_turns_first_game_loser_out) {
  Room r = room_of(3);
  CHECK(set_mode(r, Mode::TakeTurns));
  win(r, 0);
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 2}));
  CHECK((r.line == std::vector<uint8_t>{1}));
}
TEST(take_turns_veteran_out_even_after_winning) {
  Room r = room_of(3);
  set_mode(r, Mode::TakeTurns);
  win(r, 0);   // 1 out
  win(r, 0);   // 0 has two games in, 2 one: 0 steps out although 0 won
  CHECK((ring_ids(r) == std::vector<uint8_t>{1, 2}));
  CHECK((r.line == std::vector<uint8_t>{0}));
}
TEST(take_turns_is_fair_over_many_games) {
  Room r = room_of(3);
  set_mode(r, Mode::TakeTurns);
  std::map<uint8_t, int> games;
  for (int g = 0; g < 30; ++g) { for (uint8_t id : ring_ids(r)) ++games[id]; win(r, r.ring[0]); }
  CHECK(games[0] >= 19 && games[0] <= 21);
  CHECK(games[1] >= 19 && games[1] <= 21);
  CHECK(games[2] >= 19 && games[2] <= 21);
}

// ---------------------------------------------------------------- Ask
TEST(ask_mode_nothing_moves_on_its_own) {
  Room r = room_of(3);
  set_mode(r, Mode::Ask);
  win(r, 1); win(r, 0);
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1}));
  CHECK((r.line == std::vector<uint8_t>{2}));
}
TEST(ask_yes_takes_that_fighters_spot) {
  Room r = room_of(3);
  set_mode(r, Mode::Ask);
  CHECK(ask(r, 2));
  CHECK(next_up(r) == 2);
  CHECK(answer(r, 0, Answer::Yes));   // between games: at once, for the next game
  CHECK(r.leaving_slot == kNone && r.asker == kNone);
  CHECK((ring_ids(r) == std::vector<uint8_t>{2, 1}));
  CHECK((r.line == std::vector<uint8_t>{0}));
}
TEST(ask_answered_during_a_game_waits_for_its_end) {
  Room r = room_of(3);
  set_mode(r, Mode::Ask);
  CHECK(game_start(r));
  CHECK(ask(r, 2));
  CHECK(answer(r, 0, Answer::Yes));
  CHECK(r.leaving_slot == 0 && r.incoming == 2);
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1}));   // the game in progress keeps its players
  CHECK(game_end(r, 0));                                 // the result does not matter: the spot was given
  CHECK((ring_ids(r) == std::vector<uint8_t>{2, 1}));
  CHECK((r.line == std::vector<uint8_t>{0}));
}
TEST(ask_yes_and_i_play_the_winner) {
  Room r = room_of(4);
  set_mode(r, Mode::Ask);
  CHECK(ask(r, 3));
  CHECK(answer(r, 1, Answer::YesAndPlayWinner));   // 1 lets 3 in now and goes to the front of the line
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 3}));
  CHECK((r.line == std::vector<uint8_t>{1, 2}) && r.play_winner_next);
  win(r, 3);   // 1 plays the winner (3); 0 lost and goes to the back, even in Ask mode
  CHECK((ring_ids(r) == std::vector<uint8_t>{1, 3}));
  CHECK((r.line == std::vector<uint8_t>{2, 0}) && !r.play_winner_next);
}
TEST(ask_declined_by_both) {
  Room r = room_of(3);
  CHECK(ask(r, 2));
  CHECK(answer(r, 0, Answer::No));
  CHECK(r.asker == 2 && r.declined == kNone);
  CHECK(!answer(r, 0, Answer::No));   // one answer each
  CHECK(answer(r, 1, Answer::No));
  CHECK(r.asker == kNone && r.declined == 2);
  CHECK(game_start(r) && game_end(r, 0) && r.declined == kNone);
}
TEST(ask_one_no_then_other_yes) {
  Room r = room_of(3);
  set_mode(r, Mode::Ask);
  ask(r, 2);
  CHECK(answer(r, 0, Answer::No));
  CHECK(answer(r, 1, Answer::Yes));
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 2}) && (r.line == std::vector<uint8_t>{1}));
}
TEST(ask_refused_cases) {
  Room r = room_of(4);
  CHECK(!ask(r, 0));        // a fighter cannot ask
  CHECK(!ask(r, 9));        // not in the room
  CHECK(ask(r, 3));
  CHECK(!ask(r, 2));        // one ask at a time
  CHECK(!answer(r, 2, Answer::Yes));   // only fighters answer
}
TEST(ask_from_a_watcher_jumps_the_line) {
  Room r = room_of(4);
  set_mode(r, Mode::Ask);
  CHECK(toggle_line(r, 3));   // 3 just watches
  CHECK(role(r, 3) == Role::Watching);
  CHECK(ask(r, 3) && answer(r, 1, Answer::Yes));
  CHECK(r.ring[1] == 3 && (r.line == std::vector<uint8_t>{2, 1}));
}

TEST(an_asker_rotated_in_is_no_longer_asking) {
  Room r = room_of(3);
  set_mode(r, Mode::TakeTurns);
  CHECK(ask(r, 2));
  win(r, 0);   // Take turns brings 2 in for 1 anyway
  CHECK(r.ring[1] == 2 && r.asker == kNone && valid(r));
}

// ---------------------------------------------------------------- giving up a spot
TEST(give_up_and_take_it_back) {
  Room r = room_of(3);
  CHECK(game_start(r));       // during a game the change waits for its end, and can be taken back
  CHECK(give_up(r, 1));
  CHECK(r.leaving_slot == 1 && next_up(r) == 2);
  CHECK(give_up(r, 1));       // again: taken back
  CHECK(r.leaving_slot == kNone);
  CHECK(give_up(r, 0));
  CHECK(!give_up(r, 1));      // one change per game
}
TEST(give_up_between_games_is_at_once) {
  Room r = room_of(3);
  CHECK(give_up(r, 1));
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 2}) && (r.line == std::vector<uint8_t>{1}));
  CHECK(r.leaving_slot == kNone);
}
TEST(give_up_needs_someone_to_come_in) {
  Room r = room_of(2);
  CHECK(!give_up(r, 0));
  Room r3 = room_of(3);
  toggle_line(r3, 2);         // the only other member just watches
  CHECK(!give_up(r3, 0));
}
TEST(give_up_replaces_the_mode_rotation) {
  Room r = room_of(4);        // Winner stays
  CHECK(game_start(r));
  CHECK(give_up(r, 0));       // during the game, 0 lets the front of the line in
  CHECK(game_end(r, 0));      // 0 even wins: the voluntary change happens instead of 1 going out
  CHECK((ring_ids(r) == std::vector<uint8_t>{2, 1}));
  CHECK((r.line == std::vector<uint8_t>{3, 0}));
  CHECK(r.streak_id == kNone);   // the streak left with 0
}
TEST(give_up_uses_a_waiting_ask) {
  Room r = room_of(4);
  ask(r, 3);
  CHECK(give_up(r, 1));
  CHECK(r.ring[1] == 3 && r.asker == kNone);
}

// ---------------------------------------------------------------- watching and the line
TEST(toggle_line_watch_and_back) {
  Room r = room_of(4);
  CHECK(toggle_line(r, 2));
  CHECK(role(r, 2) == Role::Watching && (r.line == std::vector<uint8_t>{3}));
  CHECK(toggle_line(r, 2));
  CHECK((r.line == std::vector<uint8_t>{3, 2}));
  CHECK(!toggle_line(r, 0));   // a fighter does not leave the ring this way
  CHECK(!toggle_line(r, 7));
}
TEST(watchers_are_skipped_by_rotation) {
  Room r = room_of(4);
  toggle_line(r, 2);
  win(r, 0);
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 3}) && role(r, 2) == Role::Watching);
}

// ---------------------------------------------------------------- games
TEST(no_contest_keeps_the_lineup) {
  Room r = room_of(4);
  CHECK(game_start(r));
  give_up(r, 1);
  CHECK(game_end(r, kNone));
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1}) && r.games == 0);
  CHECK(r.leaving_slot == 1);   // still decided for the next game
  CHECK(find(r, 0)->wins == 0 && find(r, 1)->losses == 0);
  CHECK(game_start(r) && game_end(r, 0));
  CHECK(r.ring[1] == 2);
}
TEST(game_start_and_end_order) {
  Room r = room_of(1);
  CHECK(!game_start(r));       // nobody to fight
  CHECK(!game_end(r, 0));
  Room r2 = room_of(2);
  CHECK(game_start(r2) && !game_start(r2));
  CHECK(!game_end(r2, 2));     // no team 2
  CHECK(game_end(r2, 1) && !game_end(r2, 1));
}
TEST(rev_counts_only_changes) {
  Room r = room_of(2);
  const uint32_t rev = r.rev;
  CHECK(!set_mode(r, Mode::WinnerStays) && r.rev == rev);
  CHECK(set_mode(r, Mode::TakeTurns) && r.rev == rev + 1);
  CHECK(!set_mode(r, (Mode)7) && r.rev == rev + 1);
}

// ---------------------------------------------------------------- doubles
TEST(doubles_needs_four) {
  Room r = room_of(3);
  CHECK(switch_voters(r, Ring::Doubles).empty());
  CHECK(!propose_switch(r, 0));
}
TEST(doubles_everyone_says_yes) {
  Room r = room_of(5);    // 0 v 1, line 2 3 4
  CHECK((switch_voters(r, Ring::Doubles) == std::vector<uint8_t>{0, 1, 2, 3}));
  CHECK(propose_switch(r, 2));
  CHECK(r.switch_pending && !doubles(r));
  CHECK(!vote_switch(r, 4, true));   // 4 would not play
  CHECK(vote_switch(r, 0, true) && vote_switch(r, 1, true));
  CHECK(!doubles(r));
  CHECK(vote_switch(r, 3, true));
  CHECK(doubles(r) && !r.switch_pending && valid(r));
  // Teams: 0 with 2, 1 with 3.
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 2, 1, 3}));
  CHECK((r.line == std::vector<uint8_t>{4}));
}
TEST(doubles_one_no_cancels) {
  Room r = room_of(4);
  propose_switch(r, 0);
  CHECK(vote_switch(r, 1, true));
  CHECK(vote_switch(r, 3, false));
  CHECK(!r.switch_pending && r.switch_votes == 0 && !doubles(r));
}
TEST(doubles_vote_during_a_game_waits_for_its_end) {
  Room r = room_of(4);
  CHECK(game_start(r));
  propose_switch(r, 0); vote_switch(r, 1, true); vote_switch(r, 2, true); vote_switch(r, 3, true);
  CHECK(!doubles(r) && r.switch_pending);
  CHECK(game_end(r, 0));
  CHECK(doubles(r) && find(r, 0)->wins == 1 && find(r, 1)->losses == 1);
}
TEST(doubles_winner_stays_swaps_the_losing_team) {
  Room r = room_of(6);
  propose_switch(r, 0); vote_switch(r, 1, true); vote_switch(r, 2, true); vote_switch(r, 3, true);
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 2, 1, 3}));   // line 4 5
  win(r, 0);   // team 0+2 wins; 1 and 3 go out, 4 and 5 come in
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 2, 4, 5}));
  CHECK((r.line == std::vector<uint8_t>{1, 3}));
  CHECK(find(r, 2)->wins == 1 && find(r, 3)->losses == 1);
  CHECK(r.streak_id == 0 && r.streak == 1);
}
TEST(doubles_back_to_singles) {
  Room r = room_of(4);
  propose_switch(r, 0); vote_switch(r, 1, true); vote_switch(r, 2, true); vote_switch(r, 3, true);
  CHECK(doubles(r));
  CHECK((switch_voters(r, Ring::Singles) == std::vector<uint8_t>{0, 2, 1, 3}));
  propose_switch(r, 3); vote_switch(r, 0, true); vote_switch(r, 1, true); vote_switch(r, 2, true);
  CHECK(!doubles(r) && valid(r));
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1}));   // one from each team stays
  CHECK((r.line == std::vector<uint8_t>{2, 3}));
}
TEST(doubles_falls_back_when_a_player_leaves) {
  Room r = room_of(4);
  propose_switch(r, 0); vote_switch(r, 1, true); vote_switch(r, 2, true); vote_switch(r, 3, true);
  CHECK(leave(r, 2));   // nobody waiting to take the spot
  CHECK(!doubles(r) && valid(r));
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1}) && (r.line == std::vector<uint8_t>{3}));
}
TEST(doubles_refills_from_the_line) {
  Room r = room_of(5);
  propose_switch(r, 0); vote_switch(r, 1, true); vote_switch(r, 2, true); vote_switch(r, 3, true);
  CHECK(leave(r, 3));
  CHECK(doubles(r) && r.ring[3] == 4 && r.line.empty());
}

TEST(doubles_losing_team_never_takes_its_own_slots_back) {
  Room r = room_of(5);   // one waiting: only one of the losing team can go out
  propose_switch(r, 0); vote_switch(r, 1, true); vote_switch(r, 2, true); vote_switch(r, 3, true);
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 2, 1, 3}) && (r.line == std::vector<uint8_t>{4}));
  win(r, 0);   // team 1+3 loses: 1 goes out for 4, 3 stays (nobody else was waiting)
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 2, 4, 3}));
  CHECK((r.line == std::vector<uint8_t>{1}) && valid(r));
}

// ---------------------------------------------------------------- free-for-all
Room ffa_room(int n) {
  Room r = room_of(n);
  CHECK(set_ffa_allowed(r, true));
  CHECK(propose_switch(r, 0, Ring::Ffa));
  for (uint8_t v : switch_voters(r)) vote_switch(r, v, true);
  CHECK(ffa(r) && !r.switch_pending && valid(r));
  return r;
}
TEST(ffa_needs_three_and_the_hosts_word) {
  Room r = room_of(2);
  std::string why;
  CHECK(!switch_offered(r, Ring::Ffa, &why) && why == "Free-for-all needs 3 players");
  CHECK(!propose_switch(r, 0, Ring::Ffa));
  Room r3 = room_of(3);
  CHECK(!switch_offered(r3, Ring::Ffa, &why) && why == "Everyone needs the latest Melee Unlocked");
  CHECK(!propose_switch(r3, 0, Ring::Ffa));
  CHECK(set_ffa_allowed(r3, true) && !set_ffa_allowed(r3, true));
  CHECK(switch_offered(r3, Ring::Ffa, &why) && why.empty());
  CHECK(!switch_offered(r3, Ring::Doubles, &why) && why == "Doubles needs 4 players");
  CHECK(!switch_offered(r3, Ring::Singles, &why));   // already singles
  CHECK((switch_voters(r3, Ring::Ffa) == std::vector<uint8_t>{0, 1, 2}));
}
TEST(ffa_everyone_says_yes) {
  Room r = room_of(5);   // 0 v 1, line 2 3 4
  set_ffa_allowed(r, true);
  CHECK((switch_voters(r, Ring::Ffa) == std::vector<uint8_t>{0, 1, 2, 3}));
  CHECK(propose_switch(r, 4, Ring::Ffa));   // anyone may propose; 4 would not play
  CHECK(r.switch_pending && r.switch_to == Ring::Ffa && !ffa(r));
  CHECK(!vote_switch(r, 4, true));
  CHECK(vote_switch(r, 0, true) && vote_switch(r, 1, true) && vote_switch(r, 2, true));
  CHECK(!ffa(r));
  CHECK(vote_switch(r, 3, true));
  CHECK(ffa(r) && !r.switch_pending && valid(r));
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1, 2, 3}));
  CHECK((r.line == std::vector<uint8_t>{4}) && ring_size(r) == 4 && fighters(r) == 4 && ring_ready(r));
  CHECK(team_of_slot(r, 3) == 3);
}
TEST(ffa_leaves_a_watcher_alone_when_three_want_to_play) {
  Room r = room_of(4);   // 0 v 1, line 2 3
  set_ffa_allowed(r, true);
  CHECK(toggle_line(r, 3));   // 3 just watches
  CHECK((switch_voters(r, Ring::Ffa) == std::vector<uint8_t>{0, 1, 2}));
  CHECK(propose_switch(r, 0, Ring::Ffa));
  for (uint8_t v : switch_voters(r)) vote_switch(r, v, true);
  CHECK(ffa(r) && (ring_ids(r) == std::vector<uint8_t>{0, 1, 2, kNone}) && role(r, 3) == Role::Watching);
  // Doubles needs four: the watcher is asked then, as before.
  Room d = room_of(4);
  toggle_line(d, 3);
  CHECK((switch_voters(d, Ring::Doubles) == std::vector<uint8_t>{0, 1, 2, 3}));
}
TEST(ffa_three_fighters_play_with_an_open_slot) {
  Room r = ffa_room(3);
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1, 2, kNone}) && fighters(r) == 3 && ring_ready(r));
  win(r, 2);   // nobody waiting: everyone stays
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1, 2, kNone}) && r.games == 1);
  CHECK(find(r, 2)->wins == 1 && find(r, 0)->losses == 1 && find(r, 1)->losses == 1);
  CHECK(r.streak_id == 2 && r.streak == 1);
  CHECK(!game_end(r, 3));   // no such fighter
  CHECK(game_start(r) && !game_end(r, 3) && game_end(r, 0));
  CHECK(r.streak_id == 0);
}
TEST(ffa_winner_stays_waiting_players_come_in) {
  Room r = ffa_room(6);   // ring 0 1 2 3, line 4 5
  win(r, 2);   // 0 and 1 go out for 4 and 5; 3 stays (nobody else waiting); losers to the back
  CHECK((ring_ids(r) == std::vector<uint8_t>{4, 5, 2, 3}));
  CHECK((r.line == std::vector<uint8_t>{0, 1}) && valid(r));
  win(r, 3);   // 4 and 5 go out for 0 and 1; 2 stays
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1, 2, 3}));
  CHECK((r.line == std::vector<uint8_t>{4, 5}));
}
TEST(ffa_take_turns_one_steps_out) {
  Room r = ffa_room(5);   // ring 0 1 2 3, line 4
  set_mode(r, Mode::TakeTurns);
  win(r, 0);   // a fresh ring: a loser steps out (the lowest slot), the winner stays
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 4, 2, 3}) && (r.line == std::vector<uint8_t>{1}));
  win(r, 0);   // 0, 2 and 3 have two games in, 4 one: 2 steps out (the winner last among equals)
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 4, 1, 3}) && (r.line == std::vector<uint8_t>{2}));
  win(r, 1);   // 0 and 3 have three in: 0 steps out
  CHECK((ring_ids(r) == std::vector<uint8_t>{2, 4, 1, 3}) && (r.line == std::vector<uint8_t>{0}) && valid(r));
}
TEST(ffa_ask_mode_and_give_up) {
  Room r = ffa_room(5);
  set_mode(r, Mode::Ask);
  win(r, 1);
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1, 2, 3}));
  CHECK(ask(r, 4) && answer(r, 2, Answer::Yes));
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1, 4, 3}) && (r.line == std::vector<uint8_t>{2}));
  CHECK(game_start(r) && give_up(r, 3) && game_end(r, 0));
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1, 4, 2}) && (r.line == std::vector<uint8_t>{3}) && valid(r));
}
TEST(ffa_back_to_singles_and_to_doubles) {
  Room r = ffa_room(6);   // ring 0 1 2 3, line 4 5
  CHECK((switch_voters(r, Ring::Singles) == std::vector<uint8_t>{0, 1, 2, 3}));
  CHECK(propose_switch(r, 5, Ring::Singles));
  for (uint8_t v : switch_voters(r)) vote_switch(r, v, true);
  CHECK(r.kind == Ring::Singles && valid(r));
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1}) && (r.line == std::vector<uint8_t>{2, 3, 4, 5}));
  Room d = ffa_room(6);
  CHECK((switch_voters(d, Ring::Doubles) == std::vector<uint8_t>{0, 1, 2, 3}));
  CHECK(propose_switch(d, 0, Ring::Doubles));
  for (uint8_t v : switch_voters(d)) vote_switch(d, v, true);
  CHECK(doubles(d) && valid(d));
  CHECK((ring_ids(d) == std::vector<uint8_t>{0, 2, 1, 3}) && (d.line == std::vector<uint8_t>{4, 5}));
  // And doubles to a free-for-all keeps the four fighters.
  CHECK(propose_switch(d, 1, Ring::Ffa));
  for (uint8_t v : switch_voters(d)) vote_switch(d, v, true);
  CHECK(ffa(d) && (ring_ids(d) == std::vector<uint8_t>{0, 2, 1, 3}) && valid(d));
}
TEST(ffa_three_from_doubles_takes_one_from_the_line) {
  Room r = ffa_room(3);   // ring 0 1 2
  join(r, "NEW#4", "NEW");   // comes straight into the open slot
  CHECK((ring_ids(r) == std::vector<uint8_t>{0, 1, 2, 3}) && r.line.empty());
  CHECK(propose_switch(r, 0, Ring::Doubles));
  for (uint8_t v : switch_voters(r)) vote_switch(r, v, true);
  CHECK(doubles(r) && (ring_ids(r) == std::vector<uint8_t>{0, 2, 1, 3}));
}
TEST(ffa_vote_during_a_game_waits_for_its_end) {
  Room r = room_of(4);
  set_ffa_allowed(r, true);
  CHECK(game_start(r));
  propose_switch(r, 0, Ring::Ffa); vote_switch(r, 1, true); vote_switch(r, 2, true); vote_switch(r, 3, true);
  CHECK(!ffa(r) && r.switch_pending);
  CHECK(game_end(r, 1));
  CHECK(ffa(r) && find(r, 1)->wins == 1 && find(r, 0)->losses == 1 && (ring_ids(r) == std::vector<uint8_t>{0, 1, 2, 3}));
}
TEST(ffa_one_no_cancels) {
  Room r = room_of(4);
  set_ffa_allowed(r, true);
  propose_switch(r, 0, Ring::Ffa);
  CHECK(vote_switch(r, 3, false));
  CHECK(!r.switch_pending && r.switch_to == Ring::Singles && !ffa(r));
}
TEST(ffa_falls_back_to_singles_under_three) {
  Room r = ffa_room(3);
  CHECK(leave(r, 2));
  CHECK(r.kind == Ring::Singles && (ring_ids(r) == std::vector<uint8_t>{0, 1}) && valid(r));
  Room w = ffa_room(4);
  CHECK(leave(w, 1));   // three remain: still a free-for-all, the ring closes up
  CHECK(ffa(w) && (ring_ids(w) == std::vector<uint8_t>{0, 2, 3, kNone}) && valid(w));
}
TEST(doubles_falls_back_to_ffa_when_everyone_plays_it) {
  Room r = room_of(4);
  set_ffa_allowed(r, true);
  propose_switch(r, 0); vote_switch(r, 1, true); vote_switch(r, 2, true); vote_switch(r, 3, true);
  CHECK(doubles(r) && (ring_ids(r) == std::vector<uint8_t>{0, 2, 1, 3}));
  CHECK(leave(r, 2));
  CHECK(ffa(r) && (ring_ids(r) == std::vector<uint8_t>{0, 1, 3, kNone}) && valid(r));
}
TEST(ffa_word_taken_back_goes_to_singles) {
  Room r = ffa_room(5);
  CHECK(set_ffa_allowed(r, false));
  CHECK(r.kind == Ring::Singles && (ring_ids(r) == std::vector<uint8_t>{0, 1}) && valid(r));
  CHECK((r.line == std::vector<uint8_t>{2, 3, 4}));
  Room p = room_of(4);
  set_ffa_allowed(p, true);
  propose_switch(p, 0, Ring::Ffa);
  CHECK(set_ffa_allowed(p, false) && !p.switch_pending);
}
TEST(ffa_newcomer_during_a_game_waits_for_its_end) {
  Room r = ffa_room(3);
  CHECK(game_start(r));
  const uint8_t id = join(r, "LATE#4", "LATE");
  CHECK(id == 3 && role(r, 3) == Role::InLine && fighters(r) == 3);   // the game keeps its three
  CHECK(game_end(r, 0));
  CHECK(fighters(r) == 4 && ring_slot(r, 3) >= 0 && r.line.empty() && valid(r));
}
TEST(ffa_links_connect_every_fighter) {
  Room r = ffa_room(5);
  const auto links = wanted_links(r);
  int fighter_pairs = 0;
  for (const auto& l : links) if (ring_slot(r, l.first) >= 0 && ring_slot(r, l.second) >= 0) ++fighter_pairs;
  CHECK(fighter_pairs == 6);
  Room t = ffa_room(3);
  CHECK(feed_tree(t).empty() && wanted_links(t).size() == 3);
}
TEST(bot_levels_and_20xx_cpus_on_wire_4) {
  // Owner 11-24: a bot's level and a 20XX CPU travel on wire 4; wire 3 carries neither (decode refuses a 20XX CPU on it).
  Room r;
  join(r, "AAAA#1", "A"); join(r, "BBBB#2", "B");
  set_ffa_allowed(r, true);
  const uint8_t pro = add_bot(r, 0, 2, 3, 4), cpu = add_bot(r, 1, 20, kPro20xx, 7);
  CHECK(pro != kNone && cpu != kNone);
  CHECK(find(r, pro)->level == 4 && !is_cpu_bot(*find(r, pro)));
  CHECK(find(r, cpu)->level == 7 && is_cpu_bot(*find(r, cpu)) && has_cpu_bot(r));
  std::vector<uint8_t> bytes = encode(r);
  CHECK(bytes[0] == kWireVersion);
  Room back;
  CHECK(decode(bytes.data(), bytes.size(), back));
  CHECK(find(back, cpu)->level == 7 && is_cpu_bot(*find(back, cpu)) && find(back, pro)->level == 4);
  CHECK(encode(back) == bytes);
  bytes = encode(r, kWireFfa);
  CHECK(bytes[0] == kWireFfa && !decode(bytes.data(), bytes.size(), back));   // a 20XX CPU never on wire 3
  remove_bot(r, 1);
  bytes = encode(r, kWireFfa);
  CHECK(decode(bytes.data(), bytes.size(), back) && find(back, pro)->level == kBotLevelMax);   // wire 3: no levels
  // A level out of range is the default.
  CHECK(find(r, add_bot(r, 1, 9, kPro20xx, 0))->level == kBotLevelMax);
}

TEST(ffa_wire_round_trip_and_legacy_wire) {
  Room r = ffa_room(5);
  set_mode(r, Mode::Ask);
  ask(r, 4);
  std::vector<uint8_t> bytes = encode(r);
  CHECK(bytes[0] == kWireVersion);
  Room back;
  CHECK(decode(bytes.data(), bytes.size(), back) && ffa(back) && back.ffa_allowed && same(r, back));
  // A pending proposal travels with its kind.
  Room p = room_of(4);
  set_ffa_allowed(p, true);
  propose_switch(p, 0, Ring::Ffa);
  bytes = encode(p);
  CHECK(decode(bytes.data(), bytes.size(), back) && back.switch_pending && back.switch_to == Ring::Ffa && same(p, back));
  // The old wire: a doubles room reads as before, with its toggle's meaning; a free-for-all does not fit it.
  Room d = room_of(4);
  propose_switch(d, 0); vote_switch(d, 1, true); vote_switch(d, 2, true); vote_switch(d, 3, true);
  propose_switch(d, 0);   // back to singles
  bytes = encode(d, kWireLegacy);
  CHECK(bytes[0] == kWireLegacy);
  CHECK(decode(bytes.data(), bytes.size(), back) && doubles(back) && back.switch_pending && back.switch_to == Ring::Singles);
  CHECK(!back.ffa_allowed && encode(back, kWireLegacy) == bytes);
  bytes = encode(r, kWireLegacy);
  CHECK(!decode(bytes.data(), bytes.size(), back));
  // A kind byte that disagrees with the doubles flag is refused.
  bytes = encode(d);
  bytes[7] = (uint8_t)Ring::Ffa;
  CHECK(!decode(bytes.data(), bytes.size(), back));
}
TEST(ring_names) {
  CHECK(std::string(ring_name(Ring::Singles)) == "Singles" && std::string(ring_name(Ring::Doubles)) == "Doubles" &&
        std::string(ring_name(Ring::Ffa)) == "Free-for-all");
}

// ---------------------------------------------------------------- wire form
TEST(wire_round_trip) {
  Room r = room_of(9);
  win(r, 0); win(r, 2);   // Winner stays: 2 came in for 1
  set_mode(r, Mode::Ask);
  toggle_line(r, 6);
  ask(r, 6); answer(r, 0, Answer::No);
  std::vector<uint8_t> bytes = encode(r);
  Room back;
  CHECK(decode(bytes.data(), bytes.size(), back));
  CHECK(same(r, back) && back.members[3].name == r.members[3].name);
  Room d = room_of(6);
  propose_switch(d, 0); vote_switch(d, 1, true); vote_switch(d, 2, true); vote_switch(d, 3, true);
  bytes = encode(d);
  CHECK(decode(bytes.data(), bytes.size(), back) && doubles(back) && same(d, back));
}
TEST(character_picks_travel_with_the_room) {
  Room r = room_of(3);
  CHECK(r.members[1].character == kNone);
  const uint32_t rev = r.rev;
  CHECK(set_character(r, 1, 8, 2));              // Mario, third costume
  CHECK(r.rev == rev + 1);
  CHECK(!set_character(r, 1, 8, 2));             // the same pick again changes nothing
  CHECK(!set_character(r, 1, 200, 0));           // no such character
  CHECK(!set_character(r, 9, 8, 0));             // no such member
  CHECK(set_character(r, 1, kNone, 0));          // picked nothing again
  set_character(r, 2, 25, 4);
  const std::vector<uint8_t> bytes = encode(r);
  Room back;
  CHECK(decode(bytes.data(), bytes.size(), back));
  CHECK(back.members[2].character == 25 && back.members[2].costume == 4 && back.members[1].character == kNone);
}
TEST(wire_refuses_every_truncation_and_garbage) {
  Room r = room_of(5);
  const std::vector<uint8_t> bytes = encode(r);
  Room out;
  for (size_t n = 0; n < bytes.size(); ++n) CHECK(!decode(bytes.data(), n, out));
  std::vector<uint8_t> longer = bytes;
  longer.push_back(0);
  CHECK(!decode(longer.data(), longer.size(), out));
  std::vector<uint8_t> version = bytes;
  version[0] = 9;
  CHECK(!decode(version.data(), version.size(), out));
  CHECK(!decode(nullptr, 10, out));
}
TEST(wire_refuses_an_impossible_room) {
  Room r = room_of(4);
  std::vector<uint8_t> bytes = encode(r);
  // The line's last id becomes a fighter's: the same member twice.
  bytes.back() = r.ring[0];
  Room out;
  CHECK(!decode(bytes.data(), bytes.size(), out));
  Room bad = room_of(3);
  bad.asker = bad.ring[0];   // a fighter asking
  bytes = encode(bad);
  CHECK(!decode(bytes.data(), bytes.size(), out));
}

// ---------------------------------------------------------------- live feed and links
TEST(feed_small_room) {
  Room r = room_of(3);
  const auto tree = feed_tree(r);
  CHECK(tree.size() == 1 && tree[0].first == 2 && tree[0].second == 0);
  CHECK(feed_hops(r, 2) == 1 && feed_hops(r, 0) == 0 && feed_hops(r, 7) == -1);
}
TEST(feed_sixteen_fanout_and_hops) {
  Room r = room_of(16);
  toggle_line(r, 9);   // one watcher among the line
  const auto tree = feed_tree(r, 3);
  CHECK(tree.size() == 14);
  std::map<uint8_t, int> load;
  for (const auto& e : tree) ++load[e.second];
  for (const auto& l : load) CHECK(l.second <= 3);
  int worst = 0;
  for (const Member& m : r.members) if (role(r, m.id) != Role::Fighter) worst = std::max(worst, feed_hops(r, m.id, 3));
  CHECK(worst <= 3);
  CHECK(feed_hops(r, r.line.front(), 3) == 1);   // next up gets it straight from a fighter
  // Fanout 1 still reaches everyone, through a chain.
  CHECK(feed_tree(r, 1).size() == 14);
}
TEST(links_join_up_the_whole_room) {
  for (int n : {2, 3, 4, 8, 16}) {
    Room r = room_of(n);
    const auto links = wanted_links(r);
    CHECK(std::is_sorted(links.begin(), links.end()));
    std::set<uint8_t> reached = {r.ring[0]};
    for (bool grew = true; grew;) {
      grew = false;
      for (const auto& l : links) {
        const bool a = reached.count(l.first), b = reached.count(l.second);
        if (a != b) { reached.insert(a ? l.second : l.first); grew = true; }
      }
    }
    CHECK((int)reached.size() == n);
    CHECK(std::find(links.begin(), links.end(), std::make_pair(std::min(r.ring[0], r.ring[1]), std::max(r.ring[0], r.ring[1]))) != links.end());
    CHECK((int)links.size() <= n);   // a tree plus the fighters' own link
  }
}
TEST(links_in_doubles_connect_all_four_players) {
  Room r = room_of(6);
  propose_switch(r, 0); vote_switch(r, 1, true); vote_switch(r, 2, true); vote_switch(r, 3, true);
  const auto links = wanted_links(r);
  int fighter_pairs = 0;
  for (const auto& l : links) if (ring_slot(r, l.first) >= 0 && ring_slot(r, l.second) >= 0) ++fighter_pairs;
  CHECK(fighter_pairs == 6);   // every pair of the four plays over its own link, as in Teams
}

// ---------------------------------------------------------------- Pro AIs
// The invariants every room keeps between games: a Pro AI in the ring has its owner in it, never in
// singles, and a ring with a Pro AI has two people in it.
bool bots_seated_right(const Room& r) {
  if (r.in_game) return true;
  int people_in = 0, bots_in = 0;
  for (int s = 0; s < ring_size(r); ++s) {
    const Member* m = find(r, r.ring[s]);
    if (!m) continue;
    if (!is_bot(*m)) { ++people_in; continue; }
    ++bots_in;
    if (r.kind == Ring::Singles || ring_slot(r, m->bot_of) < 0) return false;
  }
  return bots_in == 0 || people_in >= kMinPeople;
}
Room two_people_ffa_ok() {
  Room r = room_of(2);
  set_ffa_allowed(r, true);
  return r;
}

// daretodair 11-25: two people, a 20XX bot each, then doubles: the people are one team, the bots the other.
TEST(two_people_with_a_bot_each_play_doubles_together) {
  Room r = two_people_ffa_ok();
  const uint8_t a = add_bot(r, 0, 20, kPro20xx, 9), b = add_bot(r, 1, 20, kPro20xx, 9);
  CHECK(a != kNone && b != kNone);
  std::string why;
  CHECK(switch_offered(r, Ring::Doubles, &why));
  CHECK(propose_switch(r, 0, Ring::Doubles));
  CHECK(vote_switch(r, 1, true));
  std::printf("    ring %d %d %d %d\n", r.ring[0], r.ring[1], r.ring[2], r.ring[3]);
  CHECK(doubles(r) && fighters(r) == 4);
  CHECK(team_of_slot(r, ring_slot(r, 0)) == team_of_slot(r, ring_slot(r, 1)));
  CHECK(team_of_slot(r, ring_slot(r, a)) == team_of_slot(r, ring_slot(r, b)));
  for (int g = 0; g < 4; ++g) {
    win(r, r.ring[g % 2 ? 2 : 0]);
    std::printf("    after game %d: ring %d %d %d %d\n", g + 1, r.ring[0], r.ring[1], r.ring[2], r.ring[3]);
    CHECK(team_of_slot(r, ring_slot(r, 0)) == team_of_slot(r, ring_slot(r, 1)));
  }
}

TEST(pro_ai_makes_a_free_for_all_of_three) {
  Room r = two_people_ffa_ok();
  const uint8_t bot = add_bot(r, 0, 2, 3);
  CHECK(bot == 2);
  CHECK(r.members.size() == 3 && people(r) == 2 && bot_of(r, 0) == 2);
  CHECK(r.kind == Ring::Singles && ring_slot(r, bot) < 0);   // singles never seats a Pro AI
  CHECK(owner(r) == 0);
  std::string why;
  CHECK(switch_offered(r, Ring::Ffa, &why));
  CHECK(propose_switch(r, 0, Ring::Ffa));
  CHECK((switch_voters(r) == std::vector<uint8_t>{0, 1}));   // the Pro AI never votes
  CHECK(vote_switch(r, 1, true));
  CHECK(r.kind == Ring::Ffa && fighters(r) == 3 && ring_slot(r, bot) >= 0 && ring_ready(r));
  CHECK(valid(r) && bots_seated_right(r));
  for (int g = 0; g < 6; ++g) {
    win(r, r.ring[g % 3] == kNone ? r.ring[0] : r.ring[g % 3]);
    CHECK(valid(r) && bots_seated_right(r) && fighters(r) == 3);
  }
}

TEST(pro_ai_one_each_and_goes_with_its_owner) {
  Room r = two_people_ffa_ok();
  CHECK(add_bot(r, 0, 2, 0) != kNone);
  CHECK(add_bot(r, 0, 9, 1) == kNone);                     // one each
  CHECK(add_bot(r, bot_of(r, 0), 2, 0) == kNone);         // a Pro AI adds none
  CHECK(add_bot(r, 1, 20, 0) != kNone);
  CHECK(people(r) == 2 && r.members.size() == 4);
  CHECK(leave(r, 0));
  CHECK(r.members.size() == 2 && bot_of(r, 0) == kNone && people(r) == 1);
  CHECK(valid(r));
  CHECK(remove_bot(r, 1));
  CHECK(r.members.size() == 1);
}

TEST(pro_ai_doubles_two_people_two_ais) {
  Room r = two_people_ffa_ok();
  add_bot(r, 0, 2, 0);
  add_bot(r, 1, 20, 1);
  CHECK(switch_offered(r, Ring::Doubles));
  CHECK(propose_switch(r, 0, Ring::Doubles));
  CHECK(vote_switch(r, 1, true));
  CHECK(r.kind == Ring::Doubles && fighters(r) == 4 && ring_ready(r) && valid(r) && bots_seated_right(r));
  for (int g = 0; g < 8; ++g) {
    win(r, r.ring[(g % 2) * 2]);
    CHECK(valid(r) && bots_seated_right(r) && fighters(r) == 4);
  }
}

TEST(pro_ai_sits_out_with_its_owner) {
  // Three people and one Pro AI in a free-for-all: when its owner steps out for someone waiting, the
  // Pro AI leaves the ring too, and a person (never a Pro AI without its owner) takes its slot.
  Room r = room_of(4);
  set_ffa_allowed(r, true);
  const uint8_t bot = add_bot(r, 2, 2, 0);
  CHECK(propose_switch(r, 0, Ring::Ffa));
  for (uint8_t v : switch_voters(r)) vote_switch(r, v, true);
  CHECK(r.kind == Ring::Ffa);
  CHECK(bots_seated_right(r) && valid(r));
  for (int g = 0; g < 12; ++g) {
    const uint8_t w = r.ring[0];
    win(r, w);
    CHECK(valid(r) && bots_seated_right(r));
    if (ring_slot(r, 2) < 0) CHECK(ring_slot(r, bot) < 0);
  }
}

TEST(pro_ais_leave_when_an_older_build_joins) {
  Room r = two_people_ffa_ok();
  add_bot(r, 0, 2, 0);
  CHECK(set_ffa_allowed(r, false));
  CHECK(r.members.size() == 2 && bot_of(r, 0) == kNone && valid(r));
}

TEST(pro_ai_wire_round_trip) {
  Room r = two_people_ffa_ok();
  add_bot(r, 1, 20, 3);
  const std::vector<uint8_t> bytes = encode(r);
  Room back;
  CHECK(decode(bytes.data(), bytes.size(), back) && same(r, back));
  const Member* b = find(back, bot_of(back, 1));
  CHECK(b && b->bot_of == 1 && b->pro == 3 && b->character == 20);
  // No machine of its own: no links, no feed.
  for (const auto& l : wanted_links(back)) CHECK(l.first != b->id && l.second != b->id);
}

// ---------------------------------------------------------------- random operations
TEST(random_operations_keep_the_room_valid) {
  const int failed_before = g_failed;
  for (uint32_t seed = 1; seed <= 40; ++seed) {
    std::mt19937 rng(seed);
    Room r;
    int next_code = 0;
    for (int step = 0; step < 600; ++step) {
      const uint32_t before = r.rev;
      const std::vector<uint8_t> was = encode(r);
      const uint8_t id = (uint8_t)(rng() % 18);
      bool changed = false;
      const uint32_t op = rng() % 16;
      Room prior = r;
      switch (op) {
        case 0: { char code[16]; std::snprintf(code, sizeof code, "P%d#1", next_code++); changed = join(r, code, "P") != kNone; break; }
        case 1: changed = (rng() % 4 == 0) && leave(r, id); break;
        case 2: changed = set_mode(r, (Mode)(rng() % 3)); break;
        case 3: changed = toggle_line(r, id); break;
        case 4: changed = ask(r, id); break;
        case 5: changed = answer(r, id, (Answer)(rng() % 3)); break;
        case 6: changed = give_up(r, id, rng() % 2); break;
        case 7: changed = rng() % 2 ? propose_switch(r, id) : propose_switch(r, id, (Ring)(rng() % 3)); break;
        case 8: changed = vote_switch(r, id, rng() % 5 != 0); break;
        case 9: case 10: changed = game_start(r); break;
        case 11: changed = (rng() % 8 == 0) && set_ffa_allowed(r, rng() % 4 != 0); break;
        case 14: changed = add_bot(r, id, (uint8_t)(rng() % 26), (uint8_t)(rng() % 5)) != kNone; break;
        case 15: changed = (rng() % 3 == 0) && remove_bot(r, id); break;
        default: changed = game_end(r, (uint8_t)(rng() % 3 == 0 ? kNone : rng() % 4)); break;
      }
      CHECK(valid(r));
      CHECK(bots_seated_right(r));
      CHECK(changed ? r.rev > before : (r.rev == before && encode(r) == was));
      const std::vector<uint8_t> bytes = encode(r);
      Room back;
      CHECK(decode(bytes.data(), bytes.size(), back) && encode(back) == bytes);
      if (g_failed != failed_before) {
        std::printf("  seed %u step %d op %u id %u\n  before:\n", seed, step, op, id);
        dump(prior);
        std::printf("  after:\n");
        dump(r);
        return;
      }
    }
  }
}

// A room that goes from singles to doubles keeps drawing from the same random stage bag: doubles
// must never draw Fountain of Dreams, whether the bag is fresh or a singles game left it half drawn.
TEST(doubles_never_draws_fountain) {
  namespace sp = slippi::stage_pool;
  const std::vector<uint16_t> six = {0x2, 0x3, 0x8, 0x1C, 0x1F, 0x20};
  std::vector<uint16_t> bag = {0x2, 0x1F};            // what a singles game left
  sp::prepare(bag, six, true);
  CHECK(bag == std::vector<uint16_t>{0x1F});
  bag = {0x2};                                        // only Fountain left: refilled without it
  sp::prepare(bag, six, true);
  CHECK(bag.size() == 5);
  CHECK(std::find(bag.begin(), bag.end(), sp::kFountain) == bag.end());
  bag.clear();
  sp::prepare(bag, six, false);                       // singles keeps all six
  CHECK(bag == six);
  bag = {0x2};
  sp::prepare(bag, six, false);
  CHECK(bag == std::vector<uint16_t>{0x2});
  bag.clear();
  sp::prepare(bag, {0x2}, true);                      // a list with nothing else is used as it is
  CHECK(bag == std::vector<uint16_t>{0x2});
  bool fountain = false, all_singles = false;
  for (uint32_t n = 0; n < 60; ++n) {
    if (sp::agreed_stage(n, true) == sp::kFountain) fountain = true;
    if (sp::agreed_stage(n, false) == sp::kFountain) all_singles = true;
  }
  CHECK(!fountain);
  CHECK(all_singles);
}

// The online select's hand and the Arena card (gx/arena_hand.h). The card out of a room sits at 0.42 of
// the picture from its top: 4.58 of the select's units above the middle, about 6 units tall (1280x720).
TEST(yougame_player_ids_stay_whole) {
  // 11-24 production: "fb9a5f9a-ef4f-4beb-b984-011b74147a08" became "FB9A5F9A-E", so no game found its fighters.
  Room r;
  const std::string a = "fb9a5f9a-ef4f-4beb-b984-011b74147a08", b = "99f11b49-6ae3-42ea-b023-c34ec8b08dd1";
  const uint8_t ia = join(r, a, "RetroKoala"), ib = join(r, b, "ShadowMango");
  CHECK(find(r, ia) && find(r, ia)->code == a);
  CHECK(find(r, ib) && find(r, ib)->code == b);
  CHECK(join(r, a, "again") == kNone);
  CHECK(join(r, "abcd#123", "Slippi")!= kNone && find(r, 2) && find(r, 2)->code == "ABCD#123");   // Slippi codes as before
  r.ffa_allowed = true;
  CHECK(add_bot(r, ia, 2, 0) != kNone);
  const std::vector<uint8_t> wire = encode(r);
  Room back;
  CHECK(decode(wire.data(), wire.size(), back));
  CHECK(find(back, ia) && find(back, ia)->code == a && find(back, ib) && find(back, ib)->code == b);
  CHECK(join(r, std::string(60, 'f'), "long") != kNone && r.members.back().code.size() == kMaxId);
}

TEST(yougame_by_code_room_names) {
  const std::string a = arena_by_code_room("SHAD#775"), b = arena_by_code_room("RETR#81");
  CHECK(a.size() == 12 && b.size() == 12 && a.rfind("mu", 0) == 0);
  for (char c : a + b) CHECK((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'));
  CHECK(a != b);
  CHECK(arena_by_code_room("shad#775") == a);   // case ignored, as typed codes are
  CHECK(arena_by_code_room("SHAD#775") == a);   // the same every time, on every machine
  CHECK(arena_by_code_room("SHAD#776") != a);
}

TEST(hand_reaches_the_card) {
  using namespace arena_hand;
  const float top = 4.58f, bottom = -1.5f;
  // A push at the edge level with the card, as before.
  CHECK(pushing(2, 26.0f, 127) && level(2.0f, top, bottom) == 2);
  // A gentle or diagonal push (45 to 60 to the side) is a push now; 50 and under used to miss.
  CHECK(pushing(2, 26.0f, 45));
  CHECK(pushing(2, 26.0f, 31));
  CHECK(!pushing(2, 26.0f, 30) && !pushing(2, 25.0f, 127));
  CHECK(pushing(1, -35.0f, -45) && !pushing(1, -34.0f, -127));
  // Resting beside the card with the stick let go: at the limit, short of it, the card's lower part.
  CHECK(beside(2, 26.0f, 2.0f, top, bottom));
  CHECK(beside(2, 24.8f, 2.0f, top, bottom));
  CHECK(beside(2, 26.0f, -2.0f, top, bottom));
  CHECK(beside(2, 29.0f, 0.0f, top, bottom));          // m-ex's limit
  // Not beside it: on Young Link (his row ends at 6), on Roy or the last column, under the card.
  CHECK(!beside(2, 26.0f, 6.0f, top, bottom));
  CHECK(!beside(2, 26.0f, 9.0f, top, bottom));
  CHECK(!beside(2, 24.0f, 2.0f, top, bottom));
  CHECK(!beside(2, 26.0f, -3.0f, top, bottom));
  // The WARM UP card on the left: the same, from the left limit to just short of Dr. Mario's column.
  CHECK(beside(1, -35.0f, 2.0f, top, bottom) && beside(1, -31.0f, 2.0f, top, bottom));
  CHECK(!beside(1, -30.0f, 2.0f, top, bottom));
  // The glove's lower part over the card from Young Link (point at 9): the longer push still lets it in,
  // a rest there never does (picking Young Link).
  CHECK(level(9.0f, top, bottom) == 1 && !beside(2, 26.0f, 9.0f, top, bottom));
  CHECK(level(12.0f, top, bottom) == 0);
  // In a panel: out of a room a hand let in short of the edge stays in; in a room the edge rules.
  CHECK(at_side(2, 24.3f, false) && !at_side(2, 24.0f, false));
  CHECK(at_side(2, 25.4f, true) && !at_side(2, 24.8f, true));
  CHECK(at_side(1, -30.6f, false) && !at_side(1, -30.6f, true));
}

int main() {
  int passed = 0;
  for (const Case& c : cases()) {
    g_test = c.name;
    const int before = g_failed;
    c.body();
    if (g_failed == before) ++passed;
    std::printf("%s %s\n", g_failed == before ? "ok  " : "FAIL", c.name);
  }
  std::printf("arena_rules_test: %d of %zu tests passed (%d checks)\n", passed, cases().size(), g_checks);
  return g_failed ? 1 : 0;
}
