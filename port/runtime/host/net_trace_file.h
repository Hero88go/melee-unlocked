// Session trace file: the online tick records (net_trace.h) of one match as CSV beside the match's
// replay, "<replay name>.trace". The simulation thread only queues; a worker thread owns the file
// and writes the queued rows in batches.
//
// The Static Recomp names its replay when the match starts, so its trace is written under the final
// name from the first row. The Source Port names its replay when it writes it (at the match's end),
// so its rows go to a ".trace.part" file that takes the replay's name once the recorder reports it;
// a match whose replay is never written leaves no file, and neither does a match nobody marked:
// a trace is kept only when the player pressed a mark button during it.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "net_trace.h"
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace net_trace {

// "replays\Game_20260101T120000.slp" gives "replays\Game_20260101T120000.trace".
std::string trace_path(const std::string& slp_path);

class Writer {
 public:
  using Log = void (*)(const char* line);   // called on the worker thread
  explicit Writer(Log log);
  ~Writer();   // writes what is queued, closes the file, removes a trace no replay claimed
  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;

  // A match starts. `slp_path` is the replay being recorded, or empty when its name is not known
  // yet (replay_saved gives it later).
  void begin(const std::string& replay_dir, const std::string& slp_path);
  void add(const Record& record);
  // The match is over: the rest is written and the file closed.
  void end();
  // The recorder wrote the match's replay to `slp_path`.
  void replay_saved(const std::string& slp_path);
  // Blocks until everything queued so far is on disk (tests).
  void wait_idle();

 private:
  struct Command {
    enum Kind : uint8_t { Begin, Row, End, Saved } kind = Row;
    Record record;
    std::string a, b;
  };
  void post(Command&& command, bool wake);
  void run();
  void handle(const Command& command);
  void close_file();
  void drop_pending();
  void report_saved(const std::string& path, size_t rows);

  Log log_;
  std::mutex mutex_;
  std::condition_variable wake_, idle_;
  std::deque<Command> queue_;
  bool stop_ = false, urgent_ = false, busy_ = false;
  // Worker thread only.
  FILE* file_ = nullptr;
  std::string path_;      // the file being written
  std::string final_;     // the name it must end up with; empty while the replay's name is unknown
  std::string pending_;   // a closed ".part" file still waiting for its replay's name
  size_t rows_ = 0, pending_rows_ = 0;
  bool marked_ = false;   // a player marked a moment (D-pad Left/Right/Down or F8): only then is the file kept
  std::thread thread_;
};

}  // namespace net_trace
