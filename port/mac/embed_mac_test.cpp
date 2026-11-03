// No SDL/AppKit initialization, WindowServer calls, game or sockets.
#include "embed_mac_state.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace mac_embed;
namespace {
int checks = 0;
void check(bool ok, const char* text) {
  ++checks;
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", text); std::exit(1); }
}
void math() {
  struct Case { Rect parent, box, cocoa; double primary, backing_scale; };
  const Case cases[] = {
    {{100, 200, 1000, 700}, {10, 58, 640, 480}, {110, 342, 640, 480}, 1080, 1},
    {{100, 200, 1000, 700}, {10, 58, 640, 480}, {110, 342, 640, 480}, 1080, 2},
    {{-1500, 20, 1000, 700}, {0, 28, 960, 640}, {-1500, 392, 960, 640}, 1080, 2},
    {{1800, -900, 1200, 900}, {25, 0, 800, 600}, {1825, 1380, 800, 600}, 1080, 1},
    {{-50, 1180, 1000, 700}, {10, 28, 640, 480}, {-40, -608, 640, 480}, 1080, 2},
    {{10.5, 20.25, 900, 600}, {3, 28, 800, 500}, {13.5, 351.75, 800, 500}, 900, 2},
  };
  for (const auto& c : cases) {
    auto frame = screen_rect(c.parent, c.box, c.primary);
    check(equal(frame, c.cocoa), "top-left/points/negative-display/titlebar conversion");
    check(frame.w * c.backing_scale == c.box.w * c.backing_scale,
          "Retina backing size changes, placement stays in points");
  }
  // The app's box is in backing pixels: Luis's 2x display, theater and the in-window full screen (10-26 melee.log).
  check(equal(points_from_backing({142, 240, 2326, 1383}, 2), {71, 120, 1163, 691.5}), "theater box: pixels to points at 2x");
  check(equal(points_from_backing({142, 0, 3314, 2122}, 2), {71, 0, 1657, 1061}), "full-window box: pixels to points at 2x");
  check(equal(points_from_backing({10, 58, 640, 480}, 1), {10, 58, 640, 480}), "1x display: unchanged");
  check(equal(points_from_backing({10, 58, 640, 480}, 0), {10, 58, 640, 480}), "unknown scale counts as 1");
  check(equal(screen_rect({0, 33, 1728, 1084}, points_from_backing({142, 240, 2326, 1383}, 2), 1117),
              {71, 272.5, 1163, 691.5}), "2x theater panel sits inside the parent, at the box's size in points");
  check(overlap_area({0, 0, 100, 100}, {50, 50, 100, 100}) == 2500, "overlap area");
  check(overlap_area({0, 0, 100, 100}, {100, 0, 100, 100}) == 0, "touching displays do not overlap");
  check(intersects({-100, 0, 200, 100}, {0, 0, 1000, 1000}), "partial offscreen");
  check(!intersects({-200, 0, 200, 100}, {0, 0, 1000, 1000}), "fully offscreen");
  check(!intersects({0, 0, 0, 100}, {0, 0, 1000, 1000}), "empty box");
}
void commands() {
  check(parent_id("12345") == 12345, "CGWindowID decimal");
  for (auto value : {"", "0", "-1", "+1", "1 ", " 1", "0x42", "4294967296", "1x"})
    check(!parent_id(value), "invalid CGWindowID");
  for (auto line : {"rect 10 28 640 480", "rect -20 -3 960 640", "rect 0 0 0 0",
                    "show", "hide", "focus", "blur", "quit", "scale 2", "scale 1.5", "scale 0.5", "scale 8", "scale 2.000"})
    check(bool(parse_command(line)), "valid command");
  check(parse_command("scale 2")->kind == Kind::Scale && parse_command("scale 2")->scale == 2, "scale value");
  check(parse_command("scale 1.25")->scale == 1.25, "fractional scale");
  for (auto line : {"scale", "scale ", "scale 0.49", "scale 8.01", "scale 0", "scale -2", "scale 2 x", "scale 2x",
                    "scale nan", "scale inf", "scale 1e9", "scales 2", "scale 2\nquit", "embed scale 2"})
    check(!parse_command(line), "malformed scale rejected");
  for (auto line : {"rect 1 2 3", "rect 1 2 3 4 tail", "rect 1 2 -3 4", "rect 1 2 3 -4",
                    "rect 1.5 2 3 4", "rect 2147483648 0 32 32", "rect 0 0 100001 50",
                    "rect 0 0 3 4\nquit", "embed show", "volume 0", "show ", "quitter", ""})
    check(!parse_command(line), "malformed/unsupported command rejected");
  Presentation p;
  Parent parent{true, true, {100, 100, 800, 600}, 123};
  check(!p.visible(parent), "starts hidden");
  p.apply(*parse_command("rect 0 28 640 480"));
  check(!p.visible(parent), "rect without first frame hidden");
  p.first_frame(); check(p.visible(parent), "first frame plus rect shown");
  p.apply(*parse_command("hide")); check(!p.visible(parent), "hide immediate");
  p.apply(*parse_command("show")); check(p.visible(parent), "show restores");
  p.apply(*parse_command("rect 0 0 1 1"));
  p.apply(*parse_command("show")); check(!p.visible(parent), "tiny rect stays hidden through show");
  p.apply(*parse_command("rect 0 0 32 32")); check(p.visible(parent), "minimum rect restores");
  parent.on_screen = false; check(!p.visible(parent), "minimized/other Space hidden");
  parent.on_screen = true;
  p.apply(*parse_command("blur")); check(!p.focused && p.visible(parent), "blur preserves picture");
  p.apply(*parse_command("focus")); check(p.focused, "focus");
  check(p.app_scale == 0, "no app scale until one arrives");
  p.apply(*parse_command("scale 2")); check(p.app_scale == 2 && p.visible(parent), "app scale kept, picture unchanged");
  p.apply(*parse_command("scale 1")); check(p.app_scale == 1, "app scale follows a display change");
  p.apply(*parse_command("quit")); check(p.quit && !p.visible(parent), "quit");
  Presentation frame_first;
  frame_first.first_frame(); check(!frame_first.visible(parent), "first frame without rect hidden");
}
void timing() {
  QueryTimer t(100);
  check(t.due(0), "first query");
  t.request(); check(!t.due(1) && !t.due(19), "20ms forced floor");
  check(t.due(20), "deferred force remains pending");
  check(!t.due(119) && t.due(120), "normal 100ms cache resumes");
  StartupBackoff backoff;
  for (auto delay : {20, 50, 100, 200, 200, 200}) check(backoff.next() == delay, "startup backoff");
  Tracker startup;
  std::vector<Millis> attempts;
  for (Millis now = 0; now <= 2200; ++now)
    startup.tick(now, [&] { attempts.push_back(now); return Parent{}; });
  check(attempts == std::vector<Millis>({0,20,70,170,370,570,770,970,1170,1370,1570,1770,1970,2170}),
        "startup fake clock exact attempts; no per-frame queries");
  check(startup.fallback(), "startup absence falls back after grace plus bounded retry");
  Tracker late_parent;
  for (Millis now = 0; now < 2170; ++now)
    late_parent.tick(now, [&] { return Parent{now >= 1999, now >= 1999, {0,0,900,700}, 88}; });
  check(!late_parent.fallback(), "grace decision cannot use stale cached absence");
  late_parent.tick(2170, [] { return Parent{true,true,{0,0,900,700},88}; });
  check(late_parent.parent().exists && !late_parent.fallback(), "late startup parent survives");

  struct FakeWindowList {
    bool exists = true, on_screen = true;
    int queries = 0;
    Parent query() { ++queries; return {exists, on_screen, {100,200,900,700}, on_screen ? 77 : 0}; }
  } windows;
  Tracker tracker;
  int ordering = 0;
  for (Millis now = 0; now < 1000; ++now) {
    tracker.tick(now, [&] { return windows.query(); });
    if (tracker.parent().on_screen && tracker.ordering_due(now)) ++ordering;
  }
  check(windows.queries == 10 && ordering == 4, "steady 10 parent + 4 ordering queries/second");
  windows.on_screen = false;
  for (Millis now = 1000; now < 5000; ++now) tracker.tick(now, [&] { return windows.query(); });
  check(tracker.parent().exists && !tracker.fallback(), "minimize/other Space never parent loss");
  check(tracker.parent().owner == 77, "hidden parent retains owner for activation refresh");
  windows.on_screen = true;
  tracker.owner_activated(); tracker.tick(5000, [&] { return windows.query(); });
  check(tracker.parent().on_screen && tracker.ordering_due(5000), "owner activation refresh");
  check(!tracker.ordering_due(5001), "activation ordering not repeated each frame");
  windows.exists = false; windows.on_screen = false;
  tracker.tick(5100, [&] { return windows.query(); });
  tracker.tick(6999, [&] { return windows.query(); });
  check(!tracker.fallback(), "parent absent just under grace");
  windows.exists = true; windows.on_screen = true;
  tracker.refresh(); tracker.tick(7019, [&] { return windows.query(); });
  check(!tracker.fallback(), "parent returns at force floor before decision");
  windows.exists = false; windows.on_screen = false;
  tracker.tick(7220, [&] { return windows.query(); });
  tracker.tick(9220, [&] { return windows.query(); });
  check(tracker.fallback(), "permanent loss fallback after grace");

  Tracker forced;
  int queries = 0, above = 0;
  for (Millis now = 0; now < 1000; ++now) {
    forced.refresh();
    forced.tick(now, [&] { ++queries; return Parent{true,true,{0,0,800,600},77}; });
    if (forced.ordering_due(now)) ++above;
  }
  check(queries == 50 && above == 50, "command flood max 50+50 queries/second");
  SettleGate settle;
  Rect bounds{100,200,900,700};
  settle.observe(0, true, bounds);
  check(!settle.ready(499) && settle.ready(500), "500ms settle before order-in");
  bounds.x += 1; settle.observe(501, true, bounds);
  check(!settle.ready(1000) && settle.ready(1001), "moving parent restarts settle");
  settle.observe(1002, false, bounds);
  check(!settle.ready(5000), "hidden parent cannot accrue stable time");
}
} // namespace
int main() {
  math(); commands(); timing();
  std::printf("Mac embed: %d checks passed; steady 14 queries/s; forced <=100 queries/s; no windows\n", checks);
}
