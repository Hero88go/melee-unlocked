#include "net_trace.h"
#include "net_trace_file.h"
#include "net_trace_read.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <process.h>

static void expect(bool value) { if (!value) std::abort(); }

// The writer logs on its worker thread; every read here follows a wait_idle or the writer's end.
static std::vector<std::string> g_log;
static void keep_log(const char* line) { g_log.push_back(line); }

static std::vector<std::string> lines(const std::filesystem::path& path) {
  std::vector<std::string> out;
  std::ifstream in(path, std::ios::binary);
  for (std::string line; std::getline(in, line);) out.push_back(line);
  return out;
}

static size_t count_ext(const std::filesystem::path& dir, const char* ext) {
  size_t n = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) n += entry.path().extension() == ext;
  return n;
}

using net_trace::Record;
using net_trace::Ring;
using net_trace::kCapacity;

static size_t commas(const char* text) {
  size_t n = 0;
  for (; *text; ++text) n += *text == ',';
  return n;
}

int main() {
  // The ring is large; keep it off the stack.
  auto ring = std::make_unique<Ring>();
  auto out = std::make_unique<Record[]>(kCapacity + 8);
  expect(kCapacity >= 600);   // the overlay shows 10 seconds
  expect(ring->size() == 0);
  expect(ring->copy(out.get(), 16) == 0);

  // Partly filled: oldest first, and a copy takes the newest ones.
  for (int i = 0; i < 10; ++i) { Record r; r.frame = i; ring->push(r); }
  expect(ring->size() == 10);
  expect(ring->at(0).frame == 0 && ring->at(9).frame == 9);
  expect(ring->copy(out.get(), 4) == 4);
  expect(out[0].frame == 6 && out[3].frame == 9);
  expect(ring->copy(out.get(), kCapacity + 8) == 10);
  expect(out[0].frame == 0 && out[9].frame == 9);

  // Past the capacity the oldest records fall off and the order holds across the wrap.
  ring->clear();
  const int total = (int)kCapacity * 2 + 37;
  for (int i = 0; i < total; ++i) { Record r; r.frame = i; ring->push(r); }
  expect(ring->size() == kCapacity);
  expect(ring->at(0).frame == total - (int)kCapacity);
  expect(ring->at(kCapacity - 1).frame == total - 1);
  expect(ring->copy(out.get(), kCapacity + 8) == kCapacity);
  for (size_t i = 0; i < kCapacity; ++i) expect(out[i].frame == total - (int)kCapacity + (int)i);
  expect(ring->copy(out.get(), 600) == 600);
  expect(out[0].frame == total - 600 && out[599].frame == total - 1);

  ring->clear();
  expect(ring->size() == 0);
  { Record r; r.frame = 7; ring->push(r); }
  expect(ring->size() == 1 && ring->at(0).frame == 7);

  // One CSV line per record, with the header's columns.
  Record r;
  r.frame = 1234; r.wall = 5021.25; r.sim_ms = 3.5f; r.offset_us = -12000;
  r.wait_frames = 0; r.ping_ms = 48; r.presents = 2;
  r.rollbacks = 1; r.rollback_depth = 3; r.flags = net_trace::kAdvance;
  const uint8_t pad[8] = {0x01, 0x20, 0x7F, 0x80, 0xFF, 0x00, 0x8C, 0x00};   // A + R, stick 127/-128, C-stick -1/0, L 140
  std::memcpy(r.pad, pad, sizeof pad);
  char line[256];
  size_t n = net_trace::csv_row(r, line, sizeof line);
  expect(n == std::strlen(line));
  expect(std::strcmp(line, "1234,5021.2500,3.50,0,1,3,-12000,1,48,0120,127,-128,-1,0,140,0,2,0,0,0\n") == 0);
  expect(commas(line) == commas(net_trace::csv_header()));
  expect(net_trace::csv_header()[std::strlen(net_trace::csv_header()) - 1] == '\n');

  // A wait, and a frame shed for time sync.
  Record w;
  w.frame = -39; w.wall = 0.5; w.wait_frames = 12; w.flags = net_trace::kWait; w.ping_ms = 210;
  n = net_trace::csv_row(w, line, sizeof line);
  expect(n > 0 && std::strcmp(line, "-39,0.5000,0.00,12,0,0,0,0,210,0000,0,0,0,0,0,0,0,0,0,0\n") == 0);
  w.flags = net_trace::kShed; w.wait_frames = 0;
  n = net_trace::csv_row(w, line, sizeof line);
  expect(n > 0 && std::strcmp(line, "-39,0.5000,0.00,0,0,0,0,-1,210,0000,0,0,0,0,0,0,0,0,0,0\n") == 0);
  // A tick the player marked (F8) reads back marked.
  w.flags = net_trace::kMark;
  n = net_trace::csv_row(w, line, sizeof line);
  expect(n > 0 && std::strcmp(line, "-39,0.5000,0.00,0,0,0,0,0,210,0000,0,0,0,0,0,0,0,1,8,0\n") == 0);
  {
    const std::string marked = std::string(net_trace::csv_header()) + line;
    net_trace::Trace trace;
    expect(net_trace::parse_trace(marked.data(), marked.size(), &trace, nullptr) && trace.records.size() == 1 &&
           trace.records[0].flags == net_trace::kMark);
  }

  // Simultaneous marks survive the file format; old readers still see the legacy mark.
  {
    for (unsigned combination = 0; combination < 16; ++combination) {
      w.flags = (uint8_t)(combination << 3);
      expect(net_trace::csv_row(w, line, sizeof line) > 0);
      const std::string marked = std::string(net_trace::csv_header()) + line;
      net_trace::Trace trace;
      expect(net_trace::parse_trace(marked.data(), marked.size(), &trace, nullptr));
      expect(trace.records.size() == 1 && trace.records[0].flags == w.flags);
    }
    const std::string legacy = "frame,wall_s,mark\n1,1,2\n2,2,3\n3,3,4\n";
    net_trace::Trace trace;
    expect(net_trace::parse_trace(legacy.data(), legacy.size(), &trace, nullptr));
    expect(trace.records.size() == 3 && trace.records[0].flags == net_trace::kMarkVisual &&
           trace.records[1].flags == net_trace::kMarkInput && trace.records[2].flags == net_trace::kMarkAudio);
    const std::string future = "frame,wall_s,mark_flags\n1,1,255\n";
    expect(net_trace::parse_trace(future.data(), future.size(), &trace, nullptr));
    expect(trace.records.size() == 1 && trace.records[0].flags == 120);
  }

  // A buffer that cannot hold the line reports nothing written.
  char tiny[8];
  expect(net_trace::csv_row(r, tiny, sizeof tiny) == 0);

  // The trace file takes the replay's name.
  expect(net_trace::trace_path("replays\\Game_20260101T120000.slp") == "replays\\Game_20260101T120000.trace");
  expect(net_trace::trace_path("a.b\\Game_1.SLP") == "a.b\\Game_1.trace");
  expect(net_trace::trace_path("Game_2") == "Game_2.trace");

  // Reading a trace back: the rows the writer makes, then a row cut off by a closing game.
  {
    std::string text = net_trace::csv_header();
    auto add = [&](int frame, double wall, int wait) {
      Record row = r;
      row.frame = frame; row.wall = wall; row.wait_frames = (uint16_t)wait;
      row.flags = wait ? net_trace::kWait : net_trace::kAdvance;
      if (wait) { row.rollbacks = 0; row.rollback_depth = 0; }
      char out_line[256];
      expect(net_trace::csv_row(row, out_line, sizeof out_line) > 0);
      text += out_line;
    };
    add(1, 10.0000, 0);
    add(2, 10.0167, 0);
    add(3, 10.0334, 1); add(3, 10.0501, 2); add(3, 10.0668, 3);   // waited three ticks for the other player
    add(3, 10.0835, 0);
    add(4, 10.1002, 0);
    add(5, 15.1002, 0);                                           // a five second freeze
    add(6, 15.1169, 0);
    text += "7,15.13";                                            // no line end: dropped
    net_trace::Trace trace;
    std::string error;
    expect(net_trace::parse_trace(text.data(), text.size(), &trace, &error));
    expect(trace.records.size() == 9 && trace.skipped == 1);
    const Record& first = trace.records[0];
    expect(first.frame == 1 && first.wall == 10.0 && first.sim_ms == 3.5f && first.offset_us == -12000);
    expect(first.ping_ms == 48 && first.presents == 2 && first.rollbacks == 1 && first.rollback_depth == 3);
    expect(first.flags == net_trace::kAdvance && std::memcmp(first.pad, pad, sizeof pad) == 0);
    expect(trace.records[2].flags == net_trace::kWait && trace.records[2].wait_frames == 1);
    expect(trace.records[5].flags == net_trace::kAdvance && trace.records[5].frame == 3);

    // The schedule by replay frame: online frame 1 is replay frame -123.
    net_trace::Schedule schedule;
    schedule.build(trace);
    expect(schedule.size() == 9 && schedule.first_frame() == -123 && schedule.last_frame() == -118);
    size_t at = 0, count = 0;
    expect(schedule.frame_records(-121, &at, &count) && at == 2 && count == 4);
    expect(schedule.frame_records(-119, &at, &count) && at == 7 && count == 1);
    expect(!schedule.frame_records(-117, &at, &count) && !schedule.frame_records(-124, &at, &count));
    expect(schedule.gap(7) == 2.0 && schedule.true_gap(7) > 4.99 && schedule.true_gap(7) < 5.01);   // clamped for viewing

    // Holds: none for frames that ran on time, three ticks for the wait, two seconds for the freeze.
    net_trace::Pacer pacer(&schedule);
    std::vector<net_trace::Pacer::Step> steps;
    double real = -1.0;
    expect(pacer.next(-123, &steps, &real) == 0.0 && steps.size() == 1 && steps[0].record == 0 && real == 0.0);
    expect(pacer.next(-122, &steps) == 0.0 && steps.size() == 1 && steps[0].wait == 0.0);
    double hold = pacer.next(-121, &steps, &real);
    expect(hold > 0.045 && hold < 0.055 && steps.size() == 4 && real > 0.045 && real < 0.055);
    double waits = 0.0;
    for (const auto& step : steps) { expect(step.wait > 0.0); waits += step.wait; }
    expect(waits > hold - 1e-9 && waits < hold + 1e-9 && steps[3].record == 5);
    expect(pacer.next(-120, &steps) == 0.0);
    hold = pacer.next(-119, &steps, &real);
    expect(hold > 1.97 && hold < 2.0 && real > 4.97 && real < 5.0 && steps.size() == 1);
    expect(pacer.next(-118, &steps) == 0.0);
    expect(pacer.next(500, &steps) == 0.0 && steps.empty());   // a frame the trace does not have

    // Columns are found by name: another order and columns this build does not know.
    const std::string other = "wall_s,later_column,frame,ping_ms\r\n1.5,x,7,33\r\n2.5,y\r\n";
    expect(net_trace::parse_trace(other.data(), other.size(), &trace, &error));
    expect(trace.records.size() == 1 && trace.skipped == 1);
    expect(trace.records[0].frame == 7 && trace.records[0].wall == 1.5 && trace.records[0].ping_ms == 33);
    // Not a trace.
    const std::string wrong = "a,b\n1,2\n";
    expect(!net_trace::parse_trace(wrong.data(), wrong.size(), &trace, &error) && !error.empty());
    expect(!net_trace::parse_trace("", 0, &trace, &error));
    expect(!net_trace::read_trace("no such folder\\no such file.trace", &trace, &error));
  }

  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path base = fs::temp_directory_path(ec);
  if (ec) base = fs::current_path();   // a machine whose temp folder is gone
  const fs::path dir = base / ("mu_net_trace_test_" + std::to_string(_getpid()));
  fs::remove_all(dir, ec);
  const std::string d = dir.string();
  {
    net_trace::Writer writer(keep_log);
    Record m = r;   // a row the player marked: only a marked match keeps its trace
    m.flags |= net_trace::kMarkVisual;

    // Replay named at the match's start: rows land under the final name, header first.
    writer.begin(d, d + "\\Game_A.slp");
    for (int i = 0; i < 3; ++i) { Record row = i == 2 ? m : r; row.frame = 10 + i; writer.add(row); }
    writer.wait_idle();
    expect(lines(dir / "Game_A.trace").size() == 4);   // on disk before the match ends
    expect(g_log.empty());
    writer.end();
    writer.wait_idle();
    const std::vector<std::string> a = lines(dir / "Game_A.trace");
    expect(a.size() == 4);
    expect(a[0] + "\n" == net_trace::csv_header());
    expect(a[1] == "10,5021.2500,3.50,0,1,3,-12000,1,48,0120,127,-128,-1,0,140,0,2,0,0,0");
    expect(a[3].rfind("12,", 0) == 0);
    expect(g_log.size() == 1 && g_log[0] == "slippi: session trace saved to Game_A.trace (3 rows)");
    writer.add(r);   // after the end: nowhere to go
    writer.wait_idle();
    expect(lines(dir / "Game_A.trace").size() == 4);

    // Replay named after the match ended: nothing called .trace until then.
    writer.begin(d, "");
    writer.add(r); writer.add(m);
    writer.end();
    writer.wait_idle();
    expect(count_ext(dir, ".trace") == 1 && count_ext(dir, ".part") == 1);
    writer.replay_saved(d + "\\Game_B.slp");
    writer.wait_idle();
    expect(lines(dir / "Game_B.trace").size() == 3);
    expect(count_ext(dir, ".part") == 0);
    expect(g_log.size() == 2 && g_log[1] == "slippi: session trace saved to Game_B.trace (2 rows)");

    // Replay named before the match's last rows: renamed when the match ends.
    writer.begin(d, "");
    writer.add(r);
    writer.replay_saved(d + "\\Game_C.slp");
    writer.add(r); writer.add(m);
    writer.wait_idle();
    expect(!fs::exists(dir / "Game_C.trace"));
    writer.end();
    writer.wait_idle();
    expect(lines(dir / "Game_C.trace").size() == 4);
    expect(g_log.size() == 3 && g_log[2] == "slippi: session trace saved to Game_C.trace (3 rows)");

    // Nobody marked anything: the match leaves no trace, named early or late.
    writer.begin(d, d + "\\Game_U1.slp");
    writer.add(r); writer.add(r);
    writer.end();
    writer.begin(d, "");
    writer.add(r);
    writer.end();
    writer.replay_saved(d + "\\Game_U2.slp");
    writer.wait_idle();
    expect(!fs::exists(dir / "Game_U1.trace") && !fs::exists(dir / "Game_U2.trace") && count_ext(dir, ".part") == 0);
    expect(g_log.size() == 3);

    // A replay written with no trace waiting changes nothing.
    writer.replay_saved(d + "\\Game_D.slp");
    writer.wait_idle();
    expect(!fs::exists(dir / "Game_D.trace"));

    // No replay for the match: the next match drops its rows, and so does shutdown.
    writer.begin(d, "");
    writer.add(r);
    writer.end();
    writer.begin(d, "");
    writer.add(r);
    writer.wait_idle();
    expect(count_ext(dir, ".part") == 1);

    // A match with no rows leaves no file.
    writer.begin(d, d + "\\Game_E.slp");
    writer.end();
    writer.wait_idle();
    expect(!fs::exists(dir / "Game_E.trace"));
    writer.begin(d, "");
    writer.add(r);
  }
  expect(count_ext(dir, ".part") == 0);
  expect(count_ext(dir, ".trace") == 3);
  expect(g_log.size() == 3);
  fs::remove_all(dir, ec);
  return 0;
}
