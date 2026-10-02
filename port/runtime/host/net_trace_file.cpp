// SPDX-License-Identifier: GPL-2.0-or-later
#include "net_trace_file.h"
#include <chrono>
#include <filesystem>
#include <system_error>
#include <process.h>

namespace net_trace {
namespace {

constexpr auto kBatch = std::chrono::milliseconds(500);   // rows wait this long at most for the disk

std::string file_name(const std::string& path) {
  const size_t slash = path.find_last_of("\\/");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

std::string trace_path(const std::string& slp_path) {
  std::string base = slp_path;
  const size_t n = base.size();
  if (n >= 4 && base[n - 4] == '.' && (base[n - 3] | 0x20) == 's' && (base[n - 2] | 0x20) == 'l' && (base[n - 1] | 0x20) == 'p')
    base.resize(n - 4);
  return base + ".trace";
}

Writer::Writer(Log log) : log_(log), thread_([this] { run(); }) {}

Writer::~Writer() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  wake_.notify_one();
  if (thread_.joinable()) thread_.join();
}

void Writer::post(Command&& command, bool wake) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(command));
    if (wake) urgent_ = true;
  }
  if (wake) wake_.notify_one();
}

void Writer::begin(const std::string& replay_dir, const std::string& slp_path) {
  Command c;
  c.kind = Command::Begin; c.a = replay_dir; c.b = slp_path;
  post(std::move(c), true);
}
// Rows do not wake the worker: it collects them every kBatch.
void Writer::add(const Record& record) {
  Command c;
  c.kind = Command::Row; c.record = record;
  post(std::move(c), false);
}
void Writer::end() {
  Command c;
  c.kind = Command::End;
  post(std::move(c), true);
}
void Writer::replay_saved(const std::string& slp_path) {
  Command c;
  c.kind = Command::Saved; c.a = slp_path;
  post(std::move(c), true);
}

void Writer::wait_idle() {
  std::unique_lock<std::mutex> lock(mutex_);
  urgent_ = true;
  wake_.notify_one();
  idle_.wait(lock, [this] { return queue_.empty() && !busy_; });
}

void Writer::run() {
  std::deque<Command> batch;
  for (;;) {
    bool stop = false;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (queue_.empty()) { busy_ = false; idle_.notify_all(); }
      wake_.wait_for(lock, kBatch, [this] { return stop_ || urgent_; });
      urgent_ = false;
      batch.swap(queue_);
      busy_ = !batch.empty();
      stop = stop_;
    }
    for (const Command& c : batch) handle(c);
    batch.clear();
    if (file_) std::fflush(file_);
    if (stop) break;
  }
  close_file();
  drop_pending();
  std::lock_guard<std::mutex> lock(mutex_);
  busy_ = false;
  idle_.notify_all();
}

void Writer::report_saved(const std::string& path, size_t rows) {
  if (!log_) return;
  char line[400];
  std::snprintf(line, sizeof line, "slippi: session trace saved to %s (%zu rows)", file_name(path).c_str(), rows);
  log_(line);
}

// A ".part" file whose match never got a replay: nothing is kept.
void Writer::drop_pending() {
  if (pending_.empty()) return;
  std::remove(pending_.c_str());
  pending_.clear();
  pending_rows_ = 0;
}

void Writer::close_file() {
  if (!file_) return;
  std::fclose(file_);
  file_ = nullptr;
  if (rows_ == 0) {
    std::remove(path_.c_str());
  } else if (path_ == final_) {
    report_saved(final_, rows_);
  } else if (!final_.empty()) {
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(path_), std::filesystem::path(final_), ec);
    if (!ec) report_saved(final_, rows_);
    else if (log_) {
      char line[400];
      std::snprintf(line, sizeof line, "slippi: session trace left at %s (cannot rename it, error %d)", file_name(path_).c_str(), ec.value());
      log_(line);
    }
  } else {
    pending_ = path_;
    pending_rows_ = rows_;
  }
  path_.clear();
  final_.clear();
  rows_ = 0;
}

void Writer::handle(const Command& command) {
  switch (command.kind) {
    case Command::Begin: {
      close_file();
      drop_pending();
      std::error_code ec;
      std::filesystem::create_directories(std::filesystem::path(command.a), ec);
      if (!command.b.empty()) {
        final_ = trace_path(command.b);
        path_ = final_;
      } else {
        // The process id keeps two clients that share a replay folder apart.
        path_ = command.a + "\\Game_unsaved_" + std::to_string(_getpid()) + ".trace.part";
      }
      file_ = std::fopen(path_.c_str(), "wb");
      if (!file_) {
        if (log_) {
          char line[400];
          std::snprintf(line, sizeof line, "slippi: cannot create session trace %s", file_name(path_).c_str());
          log_(line);
        }
        path_.clear(); final_.clear();
        return;
      }
      std::fputs(csv_header(), file_);
      rows_ = 0;
      return;
    }
    case Command::Row: {
      if (!file_) return;
      char line[160];
      const size_t n = csv_row(command.record, line, sizeof line);
      if (n && std::fwrite(line, 1, n, file_) == n) ++rows_;
      return;
    }
    case Command::End:
      close_file();
      return;
    case Command::Saved:
      if (file_) {
        if (final_.empty()) final_ = trace_path(command.a);   // renamed when the match ends
      } else if (!pending_.empty()) {
        const std::string target = trace_path(command.a);
        std::error_code ec;
        std::filesystem::rename(std::filesystem::path(pending_), std::filesystem::path(target), ec);
        if (!ec) { report_saved(target, pending_rows_); pending_.clear(); pending_rows_ = 0; }
        else drop_pending();
      }
      return;
  }
}

}  // namespace net_trace
