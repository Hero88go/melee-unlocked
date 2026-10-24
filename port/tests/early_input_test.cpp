// Early inputs: every threshold the judge acts on, both ways; the rest clamp; the receiver's buffer
// (order, replacement, the cap, what prediction picks); the wire encoding.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "early_input_rules.h"
#include <cstdio>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

using namespace host::early_input;

static void pad(uint8_t out[kPadBytes], uint16_t buttons = 0, int sx = 0, int sy = 0, int cx = 0, int cy = 0, int l = 0, int r = 0) {
  out[0] = (uint8_t)(buttons >> 8); out[1] = (uint8_t)buttons;
  out[2] = (uint8_t)(int8_t)sx; out[3] = (uint8_t)(int8_t)sy; out[4] = (uint8_t)(int8_t)cx; out[5] = (uint8_t)(int8_t)cy;
  out[6] = (uint8_t)l; out[7] = (uint8_t)r;
}

int main() {
  uint8_t a[kPadBytes], b[kPadBytes];

  // Buttons. All edges: A B Start and X Y Z R L both ways. Presses: A B Start pressed only, X Y Z R L
  // both ways. The D-pad and unread bits never count.
  pad(a); pad(b);
  CHECK(!matters(a, b)); CHECK(!matters(a, b, Rules::kPresses));
  for (uint16_t bit : {0x0100, 0x0200, 0x1000}) {
    pad(a, bit); pad(b, 0);
    CHECK(matters(a, b)); CHECK(matters(b, a));
    CHECK(matters(a, b, Rules::kPresses)); CHECK(!matters(b, a, Rules::kPresses));
  }
  for (uint16_t bit : {0x0400, 0x0800, 0x0010, 0x0020, 0x0040}) {
    pad(a, bit); pad(b, 0);
    CHECK(matters(a, b)); CHECK(matters(b, a));
    CHECK(matters(a, b, Rules::kPresses)); CHECK(matters(b, a, Rules::kPresses));
  }
  for (uint16_t bit : {0x0001, 0x0002, 0x0004, 0x0008, 0x2000}) {
    pad(a, bit); pad(b, 0);
    CHECK(!matters(a, b)); CHECK(!matters(b, a)); CHECK(!matters(a, b, Rules::kPresses));
  }

  // Stick lines. `out` = the move from `from` to `to` goes outward across a line (counts in both
  // rule sets); a crossing back inward counts only with all edges.
  struct Line { int from, to; bool crosses, out; };
  auto check_axis = [&](const Line* lines, size_t n, int field) {
    for (size_t i = 0; i < n; ++i) {
      const Line& l = lines[i];
      int v[4] = {0, 0, 0, 0};
      v[field] = l.from; pad(a, 0, v[0], v[1], v[2], v[3]);
      v[field] = l.to;   pad(b, 0, v[0], v[1], v[2], v[3]);
      if (matters(b, a) != l.crosses) return (int)i + 1;
      if (matters(b, a, Rules::kPresses) != (l.crosses && l.out)) return (int)i + 101;
      if (matters(a, b) != l.crosses) return (int)i + 201;
    }
    return 0;
  };
  // Main stick X: walk at 23, dash at 64.
  const Line x_lines[] = {{0, 22, false, false}, {0, 23, true, true}, {23, 0, true, false}, {23, 63, false, false},
                          {23, 64, true, true}, {64, 23, true, false}, {64, 127, false, false}, {0, -22, false, false},
                          {0, -23, true, true}, {-23, 0, true, false}, {-23, -64, true, true}, {-64, -128, false, false},
                          {22, -22, false, false}, {0, 64, true, true}};
  CHECK(check_axis(x_lines, sizeof x_lines / sizeof *x_lines, 0) == 0);
  // Main stick Y: tilt at 23, tap jump at 53, crouch at -56; -53 only while shielding (below).
  const Line y_lines[] = {{0, 22, false, false}, {0, 23, true, true}, {23, 52, false, false}, {23, 53, true, true},
                          {53, 23, true, false}, {0, -23, true, true}, {-23, -53, false, false}, {-23, -55, false, false},
                          {-23, -56, true, true}, {-56, -23, true, false}, {-56, -128, false, false}};
  CHECK(check_axis(y_lines, sizeof y_lines / sizeof *y_lines, 1) == 0);
  // C-stick X: 23 and 64. C-stick Y: 23 and 53 both signs.
  const Line cx_lines[] = {{0, 22, false, false}, {0, 23, true, true}, {23, 63, false, false}, {23, 64, true, true},
                           {0, -23, true, true}, {-23, -64, true, true}, {-23, -63, false, false}, {-64, 0, true, false}};
  CHECK(check_axis(cx_lines, sizeof cx_lines / sizeof *cx_lines, 2) == 0);
  const Line cy_lines[] = {{0, 22, false, false}, {0, 23, true, true}, {23, 52, false, false}, {23, 53, true, true},
                           {0, -23, true, true}, {-23, -52, false, false}, {-23, -53, true, true}, {-53, -23, true, false}};
  CHECK(check_axis(cy_lines, sizeof cy_lines / sizeof *cy_lines, 3) == 0);
  // Shield drop: main Y down across -53 counts while L, R, Z or a trigger at the shield line is held.
  pad(a, 0x0040, 0, -23); pad(b, 0x0040, 0, -53);
  CHECK(matters(b, a)); CHECK(matters(b, a, Rules::kPresses)); CHECK(!matters(a, b, Rules::kPresses)); CHECK(matters(a, b));
  pad(a, 0, 0, -23, 0, 0, 43); pad(b, 0, 0, -53, 0, 0, 43);
  CHECK(matters(b, a, Rules::kPresses));
  pad(a, 0, 0, -23, 0, 0, 42); pad(b, 0, 0, -53, 0, 0, 42);
  CHECK(!matters(b, a)); CHECK(!matters(b, a, Rules::kPresses));
  // Triggers: the light-shield line at 43; values on one side of it are the same press.
  pad(a, 0, 0, 0, 0, 0, 42); pad(b, 0, 0, 0, 0, 0, 43); CHECK(matters(a, b)); CHECK(matters(b, a));
  pad(a, 0, 0, 0, 0, 0, 0); pad(b, 0, 0, 0, 0, 0, 42); CHECK(!matters(a, b));
  pad(a, 0, 0, 0, 0, 0, 43); pad(b, 0, 0, 0, 0, 0, 255); CHECK(!matters(a, b));
  pad(a, 0, 0, 0, 0, 0, 0, 42); pad(b, 0, 0, 0, 0, 0, 0, 43); CHECK(matters(a, b));

  // Rest clamp: within two of neutral on both axes of a stick is zero; one axis out keeps both.
  pad(a, 0, 2, -2, 1, 2); clamp_rest(a); CHECK(a[2] == 0 && a[3] == 0 && a[4] == 0 && a[5] == 0);
  pad(a, 0, 3, -2, 0, 3); clamp_rest(a); CHECK((int8_t)a[2] == 3 && (int8_t)a[3] == -2 && a[4] == 0 && (int8_t)a[5] == 3);
  pad(a); CHECK(neutral(a));
  pad(a, 0x0100); CHECK(!neutral(a));
  pad(a, 0, 0, 0, 0, 0, 43); CHECK(!neutral(a));
  pad(a, 0, 0, 0, 0, 0, 42); CHECK(neutral(a));

  // Wire form round trip.
  uint8_t msg[kMessageBytes], out[kPadBytes]; int32_t target = 0; uint8_t port = 0;
  pad(a, 0x0110, -64, 53, 0, 0, 43, 0);
  encode(123456, 3, a, msg);
  decode(msg, target, port, out);
  CHECK(target == 123456 && port == 3 && std::memcmp(out, a, kPadBytes) == 0);
  encode(-5, 0, a, msg); decode(msg, target, port, out); CHECK(target == -5);

  // The receiver's buffer.
  Buffer buf;
  uint8_t p1[kPadBytes], p2[kPadBytes], p3[kPadBytes];
  pad(p1, 0x0100); pad(p2, 0x0200); pad(p3, 0x0400);
  buf.add(12, p1);
  buf.add(10, p2);
  buf.add(11, p3);
  CHECK(buf.entries().size() == 3 && buf.entries()[0].target == 10 && buf.entries()[1].target == 11 && buf.entries()[2].target == 12);
  // Prediction: the newest entry after `latest` and not after `frame`.
  CHECK(buf.best(9, 8) == nullptr);                                   // nothing for frame 9
  CHECK(buf.best(10, 9) && buf.best(10, 9)->target == 10);
  CHECK(buf.best(11, 9) && buf.best(11, 9)->target == 11);
  CHECK(buf.best(20, 9) && buf.best(20, 9)->target == 12);
  CHECK(buf.best(20, 11) && buf.best(20, 11)->target == 12);           // frames 10 and 11 are confirmed
  CHECK(buf.best(20, 12) == nullptr);                                  // everything confirmed
  CHECK(buf.best(11, 11) == nullptr);
  // The newest word about a target replaces the older one.
  buf.add(11, p1);
  CHECK(buf.entries().size() == 3 && std::memcmp(buf.entries()[1].pad, p1, kPadBytes) == 0);
  // Confirmed through 11: 10 and 11 are spent.
  buf.drop_through(11);
  CHECK(buf.entries().size() == 1 && buf.entries()[0].target == 12);
  // The cap: the oldest goes for a newer target; a target older than everything held is ignored.
  buf.clear();
  for (int t = 1; t <= kMaxEntries; ++t) buf.add(t * 2, p1);
  CHECK((int)buf.entries().size() == kMaxEntries && buf.entries().front().target == 2);
  buf.add(100, p2);
  CHECK((int)buf.entries().size() == kMaxEntries && buf.entries().front().target == 4 && buf.entries().back().target == 100);
  buf.add(1, p3);
  CHECK((int)buf.entries().size() == kMaxEntries && buf.entries().front().target == 4);
  buf.add(5, p3);                                                      // in the middle: the oldest (4) goes
  CHECK((int)buf.entries().size() == kMaxEntries && buf.entries().front().target == 5 && buf.entries()[1].target == 6);

  std::puts("early input rules: buttons, stick lines both rule sets, shield drop, triggers, rest clamp, wire form, buffer passed");
  return 0;
}
