// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_parity.h"
#include "../linux/embed_visibility.h"
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

namespace {
std::mutex lines_mutex;
std::vector<std::string> lines;
void write_line(const char* line) {
  std::lock_guard<std::mutex> lock(lines_mutex);
  lines.emplace_back(line);
}
void expect(yougame::parity::OverlayKey::Result event, bool swallowed, const char* line = nullptr) {
  CHECK(event.swallowed == swallowed);
  CHECK((event.line == nullptr) == (line == nullptr));
  if (line) CHECK(std::string(event.line) == line);
}

void check_embed_visibility() {
  linux_window::EmbedVisibility visibility;
  yougame::parity::Signals signals;
  signals.start(true, write_line);
  // Attachment alone and show-before-rect must not emit ready, even though the
  // helper's size query already returns the original drawable dimensions.
  CHECK(!visibility.visible());
  visibility.show(true);
  signals.presented(visibility.visible());
  CHECK(lines.size() == 1);
  visibility.rect(31, 480);
  CHECK(!visibility.visible());
  visibility.rect(640, 31);
  CHECK(!visibility.visible());
  visibility.rect(32, 32);
  CHECK(visibility.visible());
  visibility.show(false);
  visibility.rect(640, 480);
  CHECK(!visibility.visible());  // Rect updates never undo an explicit hide.
  signals.presented(visibility.visible());
  CHECK(lines.size() == 1);
  visibility.show(true);
  CHECK(visibility.visible());
  visibility.rect(0, 480);
  visibility.show(true);
  CHECK(!visibility.visible());  // Show cannot override a too-small rect.
  visibility.rect(640, -1);
  CHECK(!visibility.visible());
  signals.presented(visibility.visible());
  CHECK(lines.size() == 1);
  visibility.rect(640, 480);
  CHECK(visibility.visible());
  signals.presented(visibility.visible());
  CHECK(lines.size() == 2 && lines.back() == yougame::parity::kReady);
  visibility.show(false);
  visibility.show(true);
  signals.presented(visibility.visible());
  CHECK(lines.size() == 2);  // Re-show never sends a second ready.
  visibility = {};
  CHECK(!visibility.visible());  // A new attachment needs its own page rect.
  visibility.rect(32, 32);
  CHECK(visibility.visible());
  lines.clear();
}
}

int main() {
  check_embed_visibility();
  using namespace yougame::parity;
  Signals signals;
  signals.start(false, write_line);  // Standalone and the legacy HTTP link.
  signals.presented(true);
  signals.emit(kDown);
  CHECK(lines.empty());
  signals.start(true, nullptr);
  CHECK(lines.empty());

  // Ineligible/early presents must not consume the ready flag. Concurrent callers
  // stand in for startup and presentation threads; caps must precede a single ready.
  signals.presented(true);
  std::thread a([&] { signals.start(true, write_line); });
  std::thread b([&] { signals.start(true, write_line); });
  a.join(); b.join();
  signals.start(true, write_line);
  signals.presented(false);
  CHECK(lines == std::vector<std::string>{kCaps});
  std::thread c([&] { for (int i = 0; i < 100; ++i) signals.presented(true); });
  std::thread d([&] { for (int i = 0; i < 100; ++i) signals.presented(true); });
  c.join(); d.join();
  CHECK(lines.size() == 2);
  CHECK(lines[1] == "yg {\"ready\":true}");

  OverlayKey key;
  expect(key.tab(false, true, true, false), false);
  expect(key.tab(false, false, true, false), false);
  CHECK(key.cancel() == nullptr);
  expect(key.tab(true, true, false, false), false);  // Plain Tab stays the practice key.
  expect(key.tab(true, true, true, true), false);   // Shift added during repeat is not a press.
  expect(key.tab(true, false, true, false), false);
  expect(key.tab(true, true, true, true), false);   // Stray repeat must not generate down.
  expect(key.tab(true, false, false, false), false);

  auto event = key.tab(true, true, true, false);
  expect(event, true, "yg {\"overlay\":\"down\"}"); signals.emit(event.line);
  expect(key.tab(true, true, true, true), true);
  expect(key.tab(true, true, false, true), true);   // Shift released before Tab.
  event = key.tab(true, false, false, false);
  expect(event, true, "yg {\"overlay\":\"up\"}"); signals.emit(event.line);
  expect(key.tab(true, false, false, false), false); // Duplicate release.
  CHECK(key.cancel() == nullptr);

  event = key.tab(true, true, true, false);
  expect(event, true, kDown); signals.emit(event.line);
  const char* cancelled = key.cancel();  // Focus loss, deactivate, close and destroy share this.
  CHECK(cancelled && std::string(cancelled) == "yg {\"overlay\":\"cancel\"}");
  signals.emit(cancelled);
  CHECK(key.cancel() == nullptr);        // Multiple focus/close notifications emit once.
  expect(key.tab(true, true, false, true), true);
  expect(key.tab(true, false, false, false), true); // Cancelled release never sends up.
  expect(key.tab(true, true, false, false), false); // Plain Tab works again.
  expect(key.tab(true, false, false, false), false);

  // A lost key-up does not stop a fresh chord after returning to the window.
  expect(key.tab(true, true, true, false), true, kDown);
  CHECK(key.cancel() != nullptr);
  expect(key.tab(true, true, true, false), true, kDown);
  expect(key.tab(true, false, true, false), true, kUp);
  CHECK(key.cancel() == nullptr);
  CHECK(lines == (std::vector<std::string>{
    kCaps, "yg {\"ready\":true}",
    "yg {\"overlay\":\"down\"}", "yg {\"overlay\":\"up\"}",
    "yg {\"overlay\":\"down\"}", "yg {\"overlay\":\"cancel\"}"}));
  std::puts("YouGame host parity passed");
}
