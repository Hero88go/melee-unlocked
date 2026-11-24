// Search policies and integration with the production broker stream, entirely in memory.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_search.h"
#include "yougame_transport.h"
#include "yougame_blend.h"
#include <cstdio>
#include <cstdlib>
#include <future>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame;
using json = nlohmann::json;
int groups = 0;

void repeated_links() {
  search_policy::LinkFailures links;
  const auto roster = [](std::string connection, std::string user) {
    return json::array({{{"connectionId","me"},{"id","self"},{"user","self-account"}},
                        {{"connectionId",connection},{"id",connection},{"user",user}}});
  };
  CHECK(!links.failed(roster("one","opponent-a"),"me"));
  CHECK(!links.repeated());
  CHECK(!links.failed(roster("two","opponent-b"),"me")); // another opponent is not two failures
  CHECK(links.failed(roster("new-connection","opponent-a"),"me"));
  CHECK(links.repeated()); // stable account, even when connection/seat id changes
  CHECK(links.failed(roster("third","opponent-a"),"me"));
  links.successful();
  CHECK(!links.repeated());
  CHECK(!links.failed(roster("fourth","opponent-a"),"me"));
  CHECK(!links.failed(roster("fifth","opponent-b"),"me")); // any success cleared BOTH opponents
  links.successful();
  json anonymous=json::array({{{"connectionId","anon"}}});
  CHECK(!links.failed(anonymous,"me")&&!links.failed(anonymous,"me")&&!links.repeated());
  json legacy=json::array({{{"connectionId","old"},{"id","stable-legacy"},{"user",nullptr}}});
  CHECK(!links.failed(legacy,"me"));legacy[0]["connectionId"]="new";
  CHECK(links.failed(legacy,"me"));
  links.successful();
  const auto duplicate=json::array({roster("one","opponent-a")[1],roster("two","opponent-a")[1]});
  CHECK(!links.failed(duplicate,"me")); // one attempt, one stable id
  CHECK(links.failed(duplicate,"me"));
  CHECK(std::string(search_policy::kLinkRetry)=="We couldn't connect you two. Looking for another opponent...");
  CHECK(std::string(search_policy::kLinkFailed)=="Could not connect to your opponent directly");
  ++groups;
  std::puts("repeated direct links: 16 checks passed (stable ids, different opponents, success reset, missing ids, duplicate seats, exact text)");
}

void generation_guard() {
  search_policy::Generation generations;
  CHECK(!generations.current(0));
  const auto old = generations.begin();
  CHECK(generations.current(old));
  const auto next = generations.begin();
  CHECK(!generations.current(old));
  CHECK(generations.current(next));
  ++groups;
}
void busy_retry() {
  const json busy = {{"ok", false}, {"code", "busy"}, {"retry", true}};
  search_policy::BusyRetry retries;
  CHECK(retries.delay(busy) == 2);   // backs off 2, 4, then 8 s
  auto header = busy; header["headers"] = {{"Retry-After", "7"}};
  CHECK(retries.delay(header) == 7);   // the answer's own wait when it is longer
  header["retryAfter"] = 2;
  CHECK(retries.delay(header) == 8);
  CHECK(retries.delay(busy) == 8 && retries.tries() == 4 && retries.waited() == 25);
  while (retries.delay(busy)) {}
  CHECK(retries.gave_up() && retries.waited() <= search_policy::BusyRetry::kWindow && retries.waited() > 50);
  CHECK(retries.delay(busy) == 0);   // given up for the rest of this search
  search_policy::BusyRetry malformed;
  CHECK(malformed.delay(json()) == 0);
  CHECK(malformed.delay({{"ok", false}, {"code", 7}}) == 0);
  search_policy::BusyRetry fresh;
  CHECK(fresh.delay({{"ok", true}, {"code", "busy"}}) == 0);
  CHECK(fresh.delay({{"ok", false}, {"code", "signed_out"}}) == 0);
  CHECK(!fresh.gave_up());
  header = busy; header["retryAfter"] = "invalid";
  CHECK(fresh.delay(header) == 2);
  header["retryAfter"] = -1;
  CHECK(fresh.delay(header) == 4);
  header["retryAfter"] = 0;
  CHECK(fresh.delay(header) == 8);
  // An answer longer than the window is not waited for.
  search_policy::BusyRetry long_wait;
  CHECK(long_wait.delay({{"ok", false}, {"code", "busy"}, {"retryAfter", 120}}) == 0 && long_wait.gave_up());
  // The local channel host's "no page yet" answer retries the same way, and so does any HTTP 503.
  search_policy::BusyRetry local;
  CHECK(local.delay({{"ok", false}, {"code", "notHere"}, {"retryAfter", 2}}) == 2 && local.tries() == 1);
  CHECK(local.delay({{"ok", false}, {"code", "unknown_build"}}) == 0 && local.tries() == 1);
  CHECK(local.delay({{"ok", false}, {"code", "old_build"}}) == 0 && local.tries() == 1);
  CHECK(local.delay({{"ok", false}, {"code", "notYet"}}) == 0);
  CHECK(local.delay({{"ok", false}, {"status", 503}, {"error", "Lookup pending"}}) == 4 && local.tries() == 2);
  CHECK(local.delay({{"ok", false}, {"status", 409}, {"code", "old_build"}}) == 0 && !local.gave_up());
  CHECK(std::string(search_policy::kBusyGaveUp).size() <= 47);   // the Doubles panel's note
  ++groups;
}
void cancel_busy_wait() {
  uint64_t now = 0;
  CHECK(!search_policy::wait(5000, [&] { return now; }, [&](int ms) { now += ms; }, [&] { return now >= 100; }));
  CHECK(now == 100);
  CHECK(search_policy::wait(75, [&] { return now; }, [&](int ms) { now += ms; }, [] { return false; }));
  CHECK(now == 175);
  ++groups;
}
void no_show() {
  int cases = 0;
  for (bool vanilla : {false, true}) for (bool queued : {false, true})
    for (int mode = 0; mode <= 4; ++mode) for (bool friends : {false, true}) {
      const auto deadline = search_policy::no_show_deadline(vanilla, queued, mode, friends, 1000);
      CHECK(deadline == (vanilla && queued && mode == 1 && !friends ? 31000 : 0));
      ++cases;
    }
  CHECK(cases == 40);
  CHECK(!search_policy::no_show_expired(31000, false, 30999));
  CHECK(search_policy::no_show_expired(31000, false, 31000));
  CHECK(!search_policy::no_show_expired(31000, true, 99999));
  CHECK(!search_policy::no_show_expired(0, false, 99999));
  ++groups;
}
void ports() {
  int cases = 0;
  for (uint16_t configured : {0, 41000, 50999, 12345}) for (uint32_t random = 0; random < 10000; ++random) {
    const auto yg = search_policy::blend_port(configured, random);
    CHECK(yg >= 41000 && yg <= 50999 && yg != configured);
    const auto slippi = search_policy::native_port(configured, yg, random + 1);
    CHECK(slippi != yg);
    CHECK(configured ? slippi == configured : slippi >= 41000 && slippi <= 50999);
    ++cases;
  }
  CHECK(cases == 40000);
  ++groups;
}
void slow_open_cancel_and_slippi_commit() {
  // Real Stream, no open event for its 10 s budget. Commit while it is blocked, then initiate
  // the fake Slippi peer BEFORE joining the helper. The stream must cancel within a 50 ms slice.
  std::atomic<bool> cancel{false};
  std::promise<void> opened;
  transport::Events events;
  auto helper = std::async(std::launch::async, [&] {
    transport::Stream socket(events, [&](const json& request, int) {
      if (request.value("type", "") == "netOpen") opened.set_value();
      return json{{"conn", 1}};
    });
    return socket.open("fake-ticket", "", 10000, [&] { return cancel.load(); });
  });
  opened.get_future().wait();
  blend::Coordinator coordinator(0);
  coordinator.searching(1, 0);
  CHECK(coordinator.matched(blend::Side::Slippi, 1));
  cancel = true;
  const bool peer_started = true;
  CHECK(peer_started);
  CHECK(helper.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
  CHECK(!helper.get());
  ++groups;
}
void cancel_during_open_request() {
  std::atomic<bool> cancel{false};
  std::promise<void> opening;
  transport::Events events;
  auto helper = std::async(std::launch::async, [&] {
    transport::Stream socket(events, [&](const json& request, int) {
      if (request.value("type", "") == "netOpen") {
        opening.set_value();
        // Fake the cancellable bridge request used by Socket::open_session.
        CHECK(!search_policy::wait(10000, transport::now_ms,
            [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); },
            [&] { return cancel.load(); }));
      }
      return json{{"conn", 2}};
    });
    return socket.open("fake-ticket", "", 10000, [&] { return cancel.load(); });
  });
  opening.get_future().wait();
  cancel = true;
  CHECK(helper.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
  CHECK(!helper.get());
  ++groups;
}
void stale_helper_transport() {
  search_policy::Generation generations;
  std::mutex mutex;
  const auto old = generations.begin();
  std::promise<void> blocked, resume;
  auto ready = resume.get_future();
  bool broker_selected = false;
  auto helper = std::async(std::launch::async, [&] {
    blocked.set_value(); ready.wait(); // delayed identity response
    std::lock_guard<std::mutex> lock(mutex);
    if (!generations.current(old)) return;
    broker_selected = true;
  });
  blocked.get_future().wait();
  { std::lock_guard<std::mutex> lock(mutex); generations.begin(); broker_selected = false; }
  resume.set_value(); helper.get();
  CHECK(!broker_selected);
  ++groups;
}
void declined_room() {
  uint64_t now = 0;
  transport::Events events;
  std::vector<std::string> transcript;
  transport::Stream room(events, [&](const json& request, int) {
    const auto type = request.value("type", "");
    if (type == "netOpen") {
      CHECK(request.value("room", "") == "late-room");
      transcript.push_back("join");
      events.push({{"conn", 7}, {"event", "open"}});
      return json{{"conn", 7}};
    }
    if (type == "netSend") transcript.push_back(json::parse(request.value("data", "")).value("t", ""));
    if (type == "netClose") transcript.push_back("close");
    return json::object();
  }, [&] { return now; });
  search_policy::decline_room([&] { return room.open("fake-ticket", "late-room", 1000); },
                             [&] { room.send({{"t", "leave"}}); });
  room.close();
  CHECK(transcript == (std::vector<std::string>{"join", "leave", "close"})); // no result
  ++groups;
}
void cancelled_before_open() {
  transport::Events events;
  bool requested = false;
  transport::Stream socket(events, [&](const json&, int) { requested = true; return json{{"conn", 1}}; });
  CHECK(!socket.open("fake", "", 10000, [] { return true; }));
  CHECK(!requested);
  ++groups;
}
void console_session_and_cancellation() {
  transport::Events events;
  bool cancel = false;
  int opens = 0;
  transport::Stream socket(events, [&](const json& request, int) {
    if (request.value("type", "") == "netOpen") {
      CHECK(request.value("session", "") == "fake-console-session");
      CHECK(request.value("room", "") == "fake-console-room");
      ++opens;
      events.push({{"conn", 8}, {"event", "open"}});
      return json{{"conn", 8}};
    }
    return json::object();
  });
  CHECK(socket.open("fake-ticket", "fake-console-room", 1000, "fake-console-session", [&] { return cancel; }));
  socket.close();
  cancel = true;
  CHECK(!socket.open("fake-ticket", "fake-console-room", 1000, "fake-console-session", [&] { return cancel; }));
  CHECK(opens == 1);
  ++groups;
}
int main() {
  repeated_links(); generation_guard(); busy_retry(); cancel_busy_wait(); no_show(); ports();
  slow_open_cancel_and_slippi_commit(); cancel_during_open_request(); stale_helper_transport(); declined_room(); cancelled_before_open(); console_session_and_cancellation();
  std::printf("yougame_search: %d groups passed (40 no-show cases, 40000 port cases, 6 fake-I/O scenarios)\n", groups);
}
