#include "replay_revisions.h"
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using replay_revisions::Schedule;
using replay_revisions::Span;

static int g_failed = 0;
static const char* g_case = "";

// Every check runs, so one run names all that is wrong.
#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("FAILED [%s] line %d: %s\n", g_case, __LINE__, #cond); \
      ++g_failed; \
    } \
  } while (0)

// Frames a..b, both ends included.
static void run(std::vector<int32_t>& out, int32_t a, int32_t b) {
  for (int32_t f = a; f <= b; ++f) out.push_back(f);
}

// A valid sequence against the rollbacks it should hold; `trailing` is the hidden entries left at
// the file's end.
static void good(const char* name, const std::vector<int32_t>& frames, const std::vector<Span>& want,
                 size_t trailing = 0, int max_rollback = 7) {
  g_case = name;
  const Schedule s = replay_revisions::build(frames, max_rollback);
  CHECK(s.valid);
  CHECK(s.error.empty());
  if (!s.valid) {
    std::printf("  error: %s\n", s.error.c_str());
    return;
  }
  CHECK(s.entries.size() == frames.size());
  if (s.entries.size() != frames.size()) return;

  // Worked out again here, the plain way.
  const std::set<int32_t> distinct(frames.begin(), frames.end());
  size_t hidden = 0, hidden_sum = 0, shown = 0;
  int32_t top = frames[0];
  for (size_t i = 0; i < frames.size(); ++i) {
    const bool is_shown = i == 0 || frames[i] > top;
    if (is_shown) top = frames[i];
    CHECK(s.entries[i].frame == frames[i]);
    CHECK(s.entries[i].shown == is_shown);
    if (!s.entries[i].shown) CHECK(s.entries[i].hidden_before == 0);
    shown += s.entries[i].shown;
    hidden += !s.entries[i].shown;
    hidden_sum += s.entries[i].hidden_before;
  }
  CHECK(s.shown_count == distinct.size());
  CHECK(s.shown_count == shown);
  CHECK(s.trailing_hidden == trailing);
  CHECK(hidden_sum + s.trailing_hidden == hidden);
  CHECK(s.first_frame == frames[0]);
  CHECK(s.last_frame == *distinct.rbegin());
  CHECK((size_t)((int64_t)s.last_frame - s.first_frame) + 1 == s.shown_count);

  CHECK(s.rollbacks.size() == want.size());
  for (size_t i = 0; i < want.size() && i < s.rollbacks.size(); ++i) {
    const Span& got = s.rollbacks[i];
    CHECK(got.first_frame == want[i].first_frame);
    CHECK(got.last_frame == want[i].last_frame);
    CHECK(got.depth == want[i].depth);
    CHECK(got.position == want[i].position);
    CHECK(got.depth == got.last_frame - got.first_frame + 1);
    if (got.position < s.entries.size()) {
      CHECK(s.entries[got.position].shown);
      CHECK(s.entries[got.position].hidden_before == got.depth);
      CHECK(s.entries[got.position].frame == got.last_frame + 1);
    }
  }

  // The lookups, for every frame and just outside the range.
  for (int32_t f : distinct) {
    size_t last = Schedule::npos, first_shown = Schedule::npos;
    for (size_t i = 0; i < frames.size(); ++i) {
      if (frames[i] != f) continue;
      last = i;
      if (first_shown == Schedule::npos) first_shown = i;
    }
    CHECK(s.final_position(f) == last);
    CHECK(s.first_shown_position(f) == first_shown);
  }
  CHECK(s.final_position(s.first_frame - 1) == Schedule::npos);
  CHECK(s.final_position(s.last_frame + 1) == Schedule::npos);
  CHECK(s.first_shown_position(s.first_frame - 1) == Schedule::npos);
  CHECK(s.first_shown_position(s.last_frame + 1) == Schedule::npos);

  // The walk a viewer makes: every entry once, one restore per rollback.
  size_t position = 0, visited = 1, restores = 0, presented = 1;
  bool in_rollback = false;
  for (;;) {
    int32_t target = INT32_MIN;
    const Schedule::Next kind = s.next_kind(position, &target);
    if (kind == Schedule::Next::End) break;
    if (position + 1 >= s.entries.size()) {
      CHECK(!"next_kind went past the last entry");
      break;
    }
    const auto& now = s.entries[position];
    const auto& next = s.entries[position + 1];
    if (kind == Schedule::Next::Normal) {
      CHECK(now.shown && next.shown && !in_rollback);
      CHECK(next.frame == now.frame + 1 && next.hidden_before == 0);
      CHECK(target == INT32_MIN);   // written for Rollback only
    } else if (kind == Schedule::Next::Rollback) {
      CHECK(now.shown && !next.shown && !in_rollback);
      CHECK(next.frame <= now.frame);
      CHECK(target == next.frame);
      if (restores < want.size()) CHECK(target == want[restores].first_frame);
      ++restores;
      in_rollback = true;
    } else {
      CHECK(kind == Schedule::Next::Hidden);
      CHECK(!now.shown && in_rollback);
      CHECK(next.frame == now.frame + 1);
      if (next.shown) {
        // The shown frame that ends the rollback the last restore began.
        CHECK(restores > 0 && restores <= want.size() && position + 1 == want[restores - 1].position);
        in_rollback = false;
      }
    }
    presented += next.shown;
    ++position;
    ++visited;
  }
  CHECK(position + 1 == s.entries.size());
  CHECK(visited == s.entries.size());
  CHECK(presented == s.shown_count);
  CHECK(restores == want.size() + (trailing ? 1 : 0));
  CHECK(s.next_kind(s.entries.size() - 1) == Schedule::Next::End);
  CHECK(s.next_kind(s.entries.size()) == Schedule::Next::End);
  CHECK(s.next_kind(Schedule::npos) == Schedule::Next::End);
}

static void bad(const char* name, const std::vector<int32_t>& frames, int max_rollback = 7) {
  g_case = name;
  const Schedule s = replay_revisions::build(frames, max_rollback);
  CHECK(!s.valid);
  CHECK(!s.error.empty());
  // Nothing a caller could play by mistake.
  CHECK(s.entries.empty() && s.rollbacks.empty() && s.shown_count == 0);
  CHECK(s.final_position(frames.empty() ? 0 : frames[0]) == Schedule::npos);
  CHECK(s.first_shown_position(frames.empty() ? 0 : frames[0]) == Schedule::npos);
  CHECK(s.next_kind(0) == Schedule::Next::End);
}

int main() {
  std::vector<int32_t> f;

  // An offline replay: no frame twice.
  run(f, -123, 200);
  good("no rollback", f, {});
  good("one frame", {-123}, {});
  good("start is not fixed", {40, 41, 42}, {});

  // Depth 1: frame 9 again before frame 10 (position 11).
  good("depth 1", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 10, 11}, {{9, 9, 1, 11}});

  // Depth 7, the deepest the game does: 3..9 again, then 10 at position 10 + 7.
  f.clear();
  run(f, 0, 9);
  run(f, 3, 9);
  run(f, 10, 12);
  good("depth 7", f, {{3, 9, 7, 17}});

  // Back to back: hidden run, one shown frame, hidden run again.
  //                   0   1   2   3   4   5   6   7   8   9  10  11  12
  good("back to back", {10, 11, 12, 11, 12, 13, 12, 13, 14, 15, 15, 16, 17},
       {{11, 12, 2, 5}, {12, 13, 2, 8}, {15, 15, 1, 11}});

  // At the start: the first frames again, back to the first frame itself.
  good("at the start", {-123, -123, -122, -121, -123, -122, -121, -120, -119},
       {{-123, -123, 1, 2}, {-123, -121, 3, 7}});

  // A larger limit lets a deeper run through; the depth is still reported as it was.
  f.clear();
  run(f, 0, 20);
  run(f, 11, 20);
  f.push_back(21);
  good("depth 10 of 10", f, {{11, 20, 10, 31}}, 0, 10);

  // The game closed inside a rollback: accepted, and the cut run is counted apart.
  good("ends inside a rollback", {0, 1, 2, 3, 4, 2, 3}, {}, 2);

  bad("empty", {});
  bad("shown skips a frame", {0, 1, 2, 4, 5});
  bad("shown skips after a rollback", {0, 1, 2, 3, 2, 3, 5});
  f.clear();
  run(f, 0, 9);
  run(f, 2, 9);   // depth 8
  f.push_back(10);
  bad("deeper than max", f);
  bad("deeper than a small max", {0, 1, 2, 3, 2, 3, 4}, 1);
  bad("hidden run skips a frame", {0, 1, 2, 3, 4, 5, 2, 4, 5, 6});
  bad("hidden run stops early", {0, 1, 2, 3, 4, 5, 2, 3, 6});
  bad("hidden run restarts", {0, 1, 2, 3, 4, 5, 3, 4, 3, 4, 5, 6});
  bad("before the first frame", {5, 6, 4, 5, 6, 7});

  if (g_failed) {
    std::printf("replay_revisions_test: %d check(s) failed\n", g_failed);
    return 1;
  }
  return 0;
}
