// Replay coach: the game's frame data events in, readouts and advice out.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_coach.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
      ++g_failures;                                                    \
    }                                                                  \
  } while (0)

static bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// The 22 bytes mu_framedata.c sends.
static void event_bytes(uint8_t b[22], int frame, int kind, int actor, int target, int adv, int move, int lcancel,
                        int to_land, int tilt, int late = 0xFF, int free_at = 0xFF) {
  std::memset(b, 0, 22);
  b[0] = (uint8_t)((uint32_t)frame >> 24);
  b[1] = (uint8_t)((uint32_t)frame >> 16);
  b[2] = (uint8_t)((uint32_t)frame >> 8);
  b[3] = (uint8_t)frame;
  b[4] = (uint8_t)kind;
  b[5] = (uint8_t)actor;
  b[6] = (uint8_t)(target < 0 ? 0xFF : target);
  b[7] = (uint8_t)(int8_t)adv;
  b[8] = (uint8_t)(move >> 8);
  b[9] = (uint8_t)move;
  b[10] = (uint8_t)lcancel;
  b[11] = (uint8_t)(to_land < 0 ? 0xFF : to_land);
  b[12] = (uint8_t)(int8_t)tilt;
  b[18] = 22;   // Falco
  b[19] = 1;    // Fox
  b[20] = (uint8_t)late;
  b[21] = (uint8_t)free_at;
}

int main() {
  using namespace replay_coach;
  uint8_t b[22];
  Event e;

  // A Falco dair on shield, L-cancelled, 4 frames before landing, -3: hit lower for up to 0.
  event_bytes(b, 812, 2, 0, 1, -3, 69, 1, 4, 0, 10);
  CHECK(parse(b, 22, e));
  CHECK(e.frame == 812 && e.kind == Kind::Shield && e.actor == 0 && e.target == 1 && e.advantage == -3);
  CHECK(e.move == 69 && e.lcancel == 1 && e.to_land == 4 && e.actor_character == 22);
  CHECK(!parse(b, 20, e));
  Readout r = readout(e);
  CHECK(r.head == "Dair on shield");
  CHECK(r.value == "-3" && r.sign == -1);
  CHECK(contains(r.detail, "L-cancelled.") && contains(r.detail, "4 frames before landing"));
  CHECK(contains(advice(e), "Hit lower on the shield") && contains(advice(e), "up to 0."));

  // The hitbox was out only 2 more frames in the air: at best -1, still unsafe, so space it.
  event_bytes(b, 812, 2, 0, 1, -3, 69, 1, 4, 0, 2);
  CHECK(parse(b, 22, e) && e.late == 2);
  CHECK(contains(advice(e), "space it"));
  // A shield reading 102 frames behind is not a shield hit: not kept.
  clear();
  event_bytes(b, 500, 2, 0, 1, -102, 69, 2, 1, 0);
  CHECK(parse(b, 22, e));
  add(e);
  CHECK(events().empty());
  // Out for none of them: hitting lower is not possible, space it instead.
  event_bytes(b, 812, 2, 0, 1, -3, 69, 1, 4, 0, 0);
  CHECK(parse(b, 22, e));
  CHECK(contains(advice(e), "space it"));
  // Read from the file (hitbox time unknown): no number promised.
  event_bytes(b, 812, 2, 0, 1, -3, 69, 1, 4, 0);
  CHECK(parse(b, 22, e) && e.late == -1);
  CHECK(contains(advice(e), "each frame later is a frame better") && !contains(advice(e), "up to"));

  // The same with the shield tilted up says so.
  e.tilt = 60;
  CHECK(contains(advice(e), "tilted up"));

  // A missed L-cancel is the first thing to fix.
  event_bytes(b, 900, 2, 0, 1, -9, 69, 2, 3, 0);
  CHECK(parse(b, 22, e));
  CHECK(contains(readout(e).detail, "L-cancel missed."));
  CHECK(contains(advice(e), "L-cancel it"));

  // Hit on the last frame of the fall and still minus: space it.
  event_bytes(b, 950, 2, 0, 1, -2, 66, 1, 1, 0);
  CHECK(parse(b, 22, e));
  CHECK(contains(advice(e), "space it"));

  // Plus on shield and a plus hit.
  event_bytes(b, 960, 2, 0, 1, 2, 69, 1, 1, 0);
  CHECK(parse(b, 22, e));
  CHECK(readout(e).value == "+2" && readout(e).sign == 1);
  CHECK(contains(advice(e), "Safe on shield"));
  event_bytes(b, 970, 1, 1, 0, 14, 58, 0, -1, 0);
  CHECK(parse(b, 22, e));
  CHECK(readout(e).head == "Forward smash hit" && readout(e).value == "+14");
  CHECK(advice(e).empty());

  // A grounded move on shield: punishable by anything faster.
  event_bytes(b, 980, 2, 1, 0, -12, 58, 0, -1, 0);
  CHECK(parse(b, 22, e));
  CHECK(contains(advice(e), "faster than 12 frames"));

  // Techs and landings.
  event_bytes(b, 990, 4, 1, -1, 0, 183, 0, -1, 0);
  CHECK(parse(b, 22, e));
  CHECK(readout(e).head == "Missed tech" && e.target == -1);
  CHECK(contains(advice(e), "just before you hit the ground"));
  event_bytes(b, 995, 3, 0, -1, 0, 66, 2, -1, 0);
  CHECK(parse(b, 22, e));
  CHECK(readout(e).head == "Fair landing" && contains(readout(e).detail, "missed"));

  // The store: the same event after a jump back is kept once; the newest hit is the readout.
  clear();
  Event a;
  event_bytes(b, 812, 2, 0, 1, -3, 69, 1, 4, 0);
  parse(b, 22, a);
  add(a);
  add(a);
  event_bytes(b, 700, 4, 1, -1, 1, 199, 0, -1, 0);
  parse(b, 22, e);
  add(e);
  const auto all = events();
  CHECK(all.size() == 2);
  CHECK(all.size() == 2 && all[0].frame == 700 && all[1].frame == 812);
  Event last;
  double age = 99;
  CHECK(latest(last, age) && last.frame == 812 && age < 5.0);
  clear();
  CHECK(events().empty() && !latest(last, age));

  // Read from the file at load, then measured by the game a frame later: one event, the game's.
  clear();
  {
    Event file_e, game_e;
    event_bytes(b, 700, 2, 0, 1, -6, 69, 1, 3, 0);
    parse(b, 22, file_e);
    add_scanned({file_e});
    event_bytes(b, 701, 2, 0, 1, -5, 69, 1, 3, 0, 1);
    parse(b, 22, game_e);
    add(game_e);
    const auto merged = events();
    CHECK(merged.size() == 1 && merged[0].advantage == -5 && merged[0].late == 1 && !merged[0].from_file);
    // A retry from frame 650 drops the game's reading and brings the file's back.
    forget_from(650);
    const auto again = events();
    CHECK(again.size() == 1 && again[0].advantage == -6 && again[0].from_file);
  }
  clear();

  // Moments for P1: the dair on shield that got grabbed, a plus one (not a moment), a missed
  // L-cancel far from any shield hit, a missed tech; P2's own events are not P1's moments.
  {
    std::vector<Event> list;
    auto push = [&](int frame, int kind, int actor, int target, int adv, int move, int lc, int land) {
      event_bytes(b, frame, kind, actor, target, adv, move, lc, land, 0);
      Event x;
      parse(b, 22, x);
      list.push_back(x);
    };
    push(600, 2, 0, 1, -4, 69, 1, 3);    // dair on shield, -4
    push(612, 5, 1, 0, 0, 212, 0, -1);   // P2 grabs P1
    push(900, 2, 0, 1, 1, 69, 1, 1);     // +1: fine
    push(1500, 3, 0, -1, 0, 66, 2, -1);  // missed L-cancel on a fair
    push(1800, 4, 0, -1, 0, 183, 0, -1); // missed tech
    push(2000, 2, 1, 0, -6, 58, 0, -1);  // P2's own unsafe move
    const auto ms = moments(list, 0);
    CHECK(ms.size() == 3);
    if (ms.size() == 3) {
      CHECK(ms[0].frame == 600 && ms[0].punished && ms[0].what == "Dair on shield, punished");
      CHECK(contains(ms[0].advice, "Punished by Grab."));
      CHECK(ms[0].value == "-4");
      CHECK(ms[1].what == "No L-cancel: Fair");
      CHECK(ms[2].what == "Missed tech");
    }
    CHECK(moments(list, 1).size() == 1);
    // A +20 up smash with 30 frames before they act: followed by a fair at +12, so no moment; the
    // second +20 hit has nothing after it, so it is a dropped combo.
    std::vector<Event> combo;
    auto hit = [&](int frame, int actor, int target, int adv, int move, int free_at) {
      event_bytes(b, frame, 1, actor, target, adv, move, 0, -1, 0, 0xFF, free_at);
      Event x;
      parse(b, 22, x);
      combo.push_back(x);
    };
    hit(3000, 0, 1, 20, 63, 30);
    hit(3012, 0, 1, 8, 66, 20);
    hit(4000, 0, 1, 20, 63, 30);
    const auto cm = moments(combo, 0);
    CHECK(cm.size() == 1);
    if (cm.size() == 1) CHECK(cm[0].frame == 4000 && cm[0].what == "Dropped: Up smash" && cm[0].value == "+20" && contains(cm[0].advice, "20 frames"));
    // A laser well ahead is not a combo to follow up.
    combo.clear();
    hit(5000, 0, 1, 31, 341, 40);
    CHECK(moments(combo, 0).empty());
    CHECK(clock(83 * 60) == "1:23" && clock(-50) == "0:00");
  }

  CHECK(move_name(341) == "Special" && move_name(212) == "Grab");
  CHECK(character_name(22) == "Falco" && character_name(99).empty());

  if (g_failures) return 1;
  std::printf("replay coach: all checks passed\n");
  return 0;
}
