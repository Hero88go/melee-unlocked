// Metadata and Slippi-style match statistics for .slp files.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace launcher::replay {
// Opening types, as the Slippi Launcher names them.
enum class Opening { Neutral, CounterHit, Trade };
// One punish: from the first hit on the opponent until the opponent is free again (or loses the stock).
struct Conversion {
  int start_frame=0, end_frame=0;          // game frames, first playable frame is 0
  float start_percent=0, end_percent=0;    // the opponent's percent
  int moves=0;                             // hits in the punish
  bool killed=false;                       // it ended in the opponent losing a stock
  int last_move=-1;                        // Slippi move id of its last hit (move_name)
  Opening opening=Opening::Neutral;
};
// One of this player's stocks.
struct Stock {
  int start_frame=0, end_frame=-1;         // end -1: still alive at the end of the match
  float percent=0;                         // percent when it ended (or at the end of the match)
  int kill_move=-1;                        // the opponent's move that took it, -1 self destruct
  int direction=-1;                        // 0 down, 1 left, 2 right, 3 up, -1 not lost
};
struct Player {
  int port=0, character=-1, costume=0, start_stocks=0, stocks=-1;
  int l_success=0, l_fail=0, rolls=0, spot_dodges=0, air_dodges=0, ledge_grabs=0;
  int wavedashes=0, wavelands=0, dash_dances=0;
  int kills=0;                             // opponent stocks taken
  float damage_done=0;
  int openings=0, successful_conversions=0;  // openings = conversions started; successful = more than one hit
  int neutral_wins=0, counter_hits=0, trades=0, beneficial_trades=0;
  int inputs=0, digital_inputs=0;          // Slippi's input counts (per minute = count / minutes played)
  std::vector<Conversion> conversions;     // this player's punishes on the opponent
  std::vector<Stock> stock_list;           // this player's own stocks
  std::string name, code;
};
struct Info {
  std::filesystem::path path;
  std::string start_at, stage, version, played_on;  // played_on: "Dolphin", "Nintendont", "Console" or ""
  int stage_id=-1;
  int last_frame=-123;
  int winner=-1;                           // index into players, -1 unknown
  bool valid=false, stats_loaded=false, l_cancel_available=false;
  std::vector<Player> players;
};
Info inspect(const std::filesystem::path& path,bool with_stats=false);
std::string display_date(const Info& info);
const char* character_name(int id);
const char* move_name(int move_id);        // "Forward Air", "Self Destruct" for -1, ...
}
