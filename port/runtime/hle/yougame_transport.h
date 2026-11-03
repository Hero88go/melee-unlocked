// Portable broker ordering and room recovery policy, shared with the fake-broker tests.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "yougame_ranked.h"
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <climits>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <stdexcept>

namespace yougame::transport {
using ranked::json;
using ranked::str;
inline uint64_t now_ms() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct Mode {
  bool broker = false, direct = false, unsupported = false;
  static Mode hello(const json& h) {
    Mode m;
    const auto n = h.find("netBroker");
    if (n == h.end() || !n->is_object() || n->value("version", 0) != 1) return m;
    m.broker = str(*n, "mode") == "yougame";
    m.direct = m.broker && n->count("direct") && (*n)["direct"].is_object() &&
               (*n)["direct"].value("version", 0) == 1 && (*n)["direct"].value("maxDatagram", 0) == 1200;
    m.unsupported = m.broker && n->count("direct") && !m.direct;
    return m;
  }
};
enum class DatagramRoute { Standalone, Ring, Refuse };
// Unix broker launches must never fall back to native UDP, including an unknown
// broker version. Windows retains Mode::hello's existing selection behavior.
inline DatagramRoute posix_datagram_route(const json& hello) {
  const auto n = hello.find("netBroker");
  const bool broker = n != hello.end() && n->is_object() && str(*n, "mode") == "yougame";
  if (!broker) return DatagramRoute::Standalone;
  const auto integer_is = [](const json& object, const char* key, uint64_t expected) {
    const auto value = object.find(key);
    return value != object.end() && value->is_number_integer() &&
           *value == json(expected);
  };
  const auto direct = n->find("direct");
  return integer_is(*n, "version", 1) && direct != n->end() && direct->is_object() &&
         integer_is(*direct, "version", 1) && integer_is(*direct, "maxDatagram", 1200)
      ? DatagramRoute::Ring : DatagramRoute::Refuse;
}
struct PosixAttachment { int section = -1, wake = -1, wait = -1; size_t size = 0; };
inline bool posix_attachment(const json& answer, PosixAttachment& out) {
  const auto p = answer.find("posix");
  if (p == answer.end() || !p->is_object()) return false;
  const auto number = [&](const char* key, uint64_t max, uint64_t& value) {
    const auto n = p->find(key);
    if (n == p->end() || !n->is_number_integer() ||
        (!n->is_number_unsigned() && n->get<int64_t>() < 0)) return false;
    value = n->get<uint64_t>();
    return value <= max;
  };
  uint64_t version, section, wake, wait, size;
  if (!number("version", 1, version) || version != 1 ||
      !number("section", INT_MAX, section) || !number("wake", INT_MAX, wake) ||
      !number("wait", INT_MAX, wait) || !number("size", SIZE_MAX, size)) return false;
  out = {(int)section, (int)wake, (int)wait, (size_t)size};
  return true; // fd kinds, sizes and the mapped header are checked by the ring core
}
inline json candidate(bool direct, const std::string& compat, const std::string& code, const std::string& match,
                      const std::string& lan, const std::string& ext, const std::string& token) {
  json d = {{"kind", "mu-cand"}, {"v", 2}, {"compat", compat}, {"code", code}, {"match", match}};
  if (!direct) { d["lan"] = lan; d["ext"] = ext; d["token"] = token; }
  return d;
}
// The reader may receive open/message/close before netOpen's answer. Keep them by id, in order.
// No bridge requests run on that reader thread. Closed connections are explicitly retired.
class Events {
 public:
  explicit Events(std::function<void(int)> wait = {}) : wait_(std::move(wait)) {}
  void push(const json& p) {
    const int id = p.value("conn", 0);
    std::lock_guard<std::mutex> lock(mutex_);
    if (id <= 0 || retired_.count(id)) return;
    auto& q = queues_[id];
    if (q.size() >= 1024) { q.clear(); q.push_back({{"event", "close"}, {"code", 1009}}); }
    else q.push_back(p);
    cv_.notify_all();
  }
  bool next(int id, json& p, int ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (wait_) {
      if (queues_[id].empty()) { lock.unlock(); wait_(ms); lock.lock(); }
    } else cv_.wait_for(lock, std::chrono::milliseconds(ms), [&] { return !queues_[id].empty(); });
    auto& q = queues_[id];
    if (q.empty()) return false;
    p = std::move(q.front()); q.pop_front(); return true;
  }
  void retire(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    queues_.erase(id); retired_.insert(id);
  }
 private:
  std::function<void(int)> wait_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::map<int, std::deque<json>> queues_;
  std::set<int> retired_;
};
using Request = std::function<json(const json&, int)>; // returns bridge.result, throws on refusal
class Stream {
 public:
  Stream(Events& events, Request request, std::function<uint64_t()> clock = now_ms)
      : events_(events), request_(std::move(request)), clock_(std::move(clock)) {}
  ~Stream() { close(1000, 4000); }
  bool open(const std::string& ticket, const std::string& room, int ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t until = clock_() + ms;
    // An open timeout is not a close. Keep waiting for this connection's app event;
    // replacing it could deliberately evict a seat which connected after the timeout.
    if (conn_ && !closed_) return ready_ || wait_open(until);
    if (conn_) events_.retire(conn_);
    conn_ = 0; pending_.clear(); ready_ = false; code_ = 1006;
    json q = {{"type", "netOpen"}, {"ticket", ticket}};
    if (!room.empty()) q["room"] = room;
#ifdef YOUGAME_AGENT_TEST_HOOK
    if (!first_open_recorded_) { first_open_recorded_ = true; first_open_at_ = clock_(); }
#endif
    conn_ = request_(q, std::max(1, ms)).value("conn", 0);
    closed_ = conn_ <= 0;
    return !closed_ && wait_open(until);
  }
  bool send(const json& m) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || !ready_ || conn_ <= 0) return false;
    try { request_({{"type", "netSend"}, {"conn", conn_}, {"data", m.dump()}}, 1000); return true; }
    catch (...) { return false; } // timeout/backpressure/rate limiting loses a message, not the stream
  }
  bool next(json& m, int ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t until = clock_() + ms;
    do {
      json p;
      if (!pending_.empty() && ready_) { p = std::move(pending_.front()); pending_.pop_front(); }
      else if (!conn_ || closed_ || !events_.next(conn_, p, (int)std::max<int64_t>(0, (int64_t)until - clock_()))) return false;
      const auto event = str(p, "event");
      if (event == "open") ready_ = true;
      if (event == "close") { closed_ = true; ready_ = false; code_ = p.value("code", 1006); return false; }
      if (event == "message" && !ready_) { pending_.push_back(std::move(p)); continue; }
      if (event == "message" && p.count("data") && p["data"].is_string()) {
        m = json::parse(p["data"].get<std::string>(), nullptr, false);
        if (m.is_object()) return true;
      }
    } while (clock_() < until || (!pending_.empty() && ready_));
    return false;
  }
  void close(int timeout = 1000, int code = 1000) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int id = conn_; conn_ = 0; ready_ = false; pending_.clear();
    const bool reported_closed = closed_; closed_ = true;
    if (!id) return;
    events_.retire(id);
    if (!reported_closed) {
      try { request_({{"type", "netClose"}, {"conn", id}, {"code", code}}, timeout); } catch (...) {}
    }
  }
  // Input polling must not wait for next()'s blocking event read under mutex_.
  bool closed() const { return closed_.load(); }
  bool ready() const { std::lock_guard<std::mutex> lock(mutex_); return ready_; }
  int close_code() const { std::lock_guard<std::mutex> lock(mutex_); return code_; }
  int conn() const { std::lock_guard<std::mutex> lock(mutex_); return conn_; }
#ifdef YOUGAME_AGENT_TEST_HOOK
  uint64_t agent_test_first_open_at() const {
    std::lock_guard<std::mutex> lock(mutex_); return first_open_at_;
  }
  void agent_test_drop_room() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || !conn_) return;
    const int id = conn_; conn_ = 0; ready_ = false; pending_.clear();
    code_ = 1006; closed_ = true;
    events_.retire(id);
    // 1006 is local-only (reserved on the wire). Use the app's existing teardown request.
    try { request_({{"type", "netClose"}, {"conn", id}, {"code", 4000}}, 1000); } catch (...) {}
  }
#endif
 private:
  bool wait_open(uint64_t until) {
    while (clock_() < until) {
      json p;
      if (!events_.next(conn_, p, (int)std::max<int64_t>(0, (int64_t)until - clock_()))) break;
      const auto event = str(p, "event");
      if (event == "open") { ready_ = true; return true; }
      if (event == "message") pending_.push_back(std::move(p));
      if (event == "close") { code_ = p.value("code", 1006); closed_ = true; return false; }
    }
    return false;
  }
  Events& events_;
  Request request_;
  std::function<uint64_t()> clock_;
  mutable std::mutex mutex_;
  int conn_ = 0, code_ = 1006;
  std::atomic<bool> closed_{true};
  bool ready_ = false;
  std::deque<json> pending_;
#ifdef YOUGAME_AGENT_TEST_HOOK
  bool first_open_recorded_ = false;
  uint64_t first_open_at_ = 0;
#endif
};

// Each outage tries its existing ticket first, then renews before every retry.
// Callers bound requests and pace attempts within their existing recovery deadline.
class TicketRetry {
 public:
  template<class Open, class Renew> bool attempt(Open open, Renew renew) {
    if (tried_) renew();
    tried_ = true;
    return open();
  }
 private:
  bool tried_ = false;
};

// Share validation and credential-safe diagnostics with pre-match recovery. Never log
// response fields or arbitrary exception text: either may contain the ticket.
template<class RequestTicket, class Log>
bool renew_room_ticket(std::string& current, const std::string& room, RequestTicket request, Log log) {
  const char* reason = "request failed";
  try {
    const auto fresh = request();
    const auto named = str(fresh, "room"), ticket = str(fresh, "ticket");
    if (!named.empty() && named != room) reason = "room mismatch";
    else if (ticket.empty()) reason = "missing ticket";
    else {
      current = ticket;
      log("yougame: room ticket renewed");
      return true;
    }
  } catch (const std::exception& e) {
    const std::string error = e.what();
    if (error == "The YouGame app did not answer" || error == "timeout") reason = "request timed out";
    else if (error == "The YouGame app is not answering") reason = "app unavailable";
    else if (error == "The YouGame app sent an invalid answer") reason = "invalid answer";
    else if (error == "Cancelled") reason = "cancelled";
  } catch (...) {}
  log((std::string("yougame: room ticket renewal failed: ") + reason).c_str());
  return false;
}

// The fork bounds both local reconnect and provisional peer loss to the rooms' 15 s allowance.
// The app alone authenticates a reopened socket and rebinds links; the game never requests rebind.
class Recovery {
 public:
  void configure(std::string code, std::string me, const json& participants) {
    code_ = std::move(code); me_ = std::move(me); roster_ = seats(participants);
  }
  void start(const std::string& match) { if (!match.empty()) match_ = match; }
  void result() { match_.clear(); }
  bool dropped(uint64_t now, bool unexpected) {
    if (ended_) return false;
    if (!unexpected) { end(); return false; }
    if (!until_) until_ = now + 15000;
    return !expired(now);
  }
  bool leave(const json& m, uint64_t now, bool active) {
    const auto who = m.count("player") ? str(m["player"], "id") : str(m, "id");
    if (std::none_of(roster_.begin(), roster_.end(), [&](const json& p) { return str(p, "connectionId") == who; })) return true;
    const auto reason = str(m, "reason");
    // A deliberate departure cancels even an earlier drop: it has no recovery grace.
    if (reason == "left" || reason == "kicked") { end(); return false; }
    if (active && (reason.empty() || reason == "dropped" || reason == "transport" || reason == "disconnected"))
      return dropped(now, true);
    end(); return false;
  }
  bool restore(const json& m, uint64_t now, bool require_players = true, bool same_match = true) {
    if (!until_ || expired(now)) return false;
    if (str(m, "t") == "welcome") {
      if (str(m, "me") != me_ || !m.count("room") || str(m["room"], "code") != code_) { end(); return false; }
    }
    if (same_match && !match_.empty() && str(m, "matchId") != match_) { end(); return false; }
    if (!require_players) { until_ = 0; return true; }
    const auto p = m.find("participants");
    if (p == m.end() || !p->is_array() || p->size() < roster_.size()) return false;
    if (seats(*p) != roster_) { end(); return false; }
    // Seat records may be retained for absent players; require the live player roster too.
    if (!m.count("players") || !m["players"].is_array()) return false;
    std::set<std::string> present;
    for (const auto& player : m["players"]) if (!player.value("away", false)) {
      const auto id = str(player, "id");
      for (const auto& seat : roster_) if (str(seat, "connectionId") == id &&
          seat["user"] != player.value("user", json())) { end(); return false; }
      present.insert(id);
    }
    for (const auto& seat : roster_) if (!present.count(str(seat, "connectionId"))) return false;
    until_ = 0; return true;
  }
  bool expired(uint64_t now) const { return ended_ || (until_ && now >= until_); }
  bool pending() const { return until_ != 0; }
  uint64_t remaining(uint64_t now) const { return until_ > now ? until_ - now : 0; }
  void end() { ended_ = true; until_ = 0; }
  void cancel_wait() { ended_ = false; until_ = 0; }
 private:
  static json seats(const json& participants) {
    json out = json::array();
    if (!participants.is_array()) return out;
    for (const auto& p : participants) out.push_back({{"id", str(p,"id")}, {"connectionId", str(p,"connectionId")},
      {"user", p.value("user", json())}, {"slot", p.value("slot", -1)}, {"localIndex", p.value("localIndex", -1)}});
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) { return a["slot"] < b["slot"]; });
    return out;
  }
  std::string code_, me_, match_;
  json roster_ = json::array();
  uint64_t until_ = 0;
  bool ended_ = false;
};

// Stable room-slot order within each team, captured when the game ends so retries cannot
// pick up a later roster. A team report never designates a single player as the winner.
inline json team_outcome(int winner, const std::vector<std::string>& ids, const int* teams) {
  if ((winner != 0 && winner != 1) || ids.size() != 4 || !ranked::valid_teams(teams)) return {{"void", true}};
  std::set<std::string> unique(ids.begin(), ids.end());
  if (unique.size() != 4 || unique.count("")) return {{"void", true}};
  json ranking = json::array();
  for (int team : {winner, 1 - winner})
    for (int port = 0; port < 4; ++port) if (teams[port] == team) ranking.push_back(ids[port]);
  return {{"ranking", ranking}, {"detail", {{"tw", winner}}}};
}

// Unranked may finish a fight and start another while signaling recovers. Keep each begin/finish
// together, retain its winner through retries, and retire it only on its own room receipt.
class Games {
 public:
  void start_game(const std::string& active_match = {}) {
    if (in_game_) return;
    in_game_ = true;
    Game g;
    if (games_.empty()) g.match = active_match;
    games_.push_back(std::move(g));
  }
  void report(int winner) { in_game_ = false; if (!games_.empty()) games_.back().winner = winner; }
  void report_team(int winner, const std::vector<std::string>& ids, const int* teams) {
    in_game_ = false;
    if (!games_.empty()) games_.back().team_result = team_outcome(winner, ids, teams);
  }
  void started(const std::string& match) {
    if (!games_.empty() && games_.front().match.empty()) { games_.front().match = match; games_.front().next = 0; }
  }
  void receive(const json& m) {
    if (!games_.empty() && str(m, "t") == "welcome" && !games_.front().match.empty() &&
        str(m, "matchId") != games_.front().match) games_.pop_front();
    if (!games_.empty() && str(m, "t") == "result" && str(m, "matchId") == games_.front().match) games_.pop_front();
  }
  bool refused(const json& m) {
    if (games_.empty() || str(m, "action") != "finish" || !games_.front().finish_sent) return false;
    games_.pop_front();
    return true;
  }
  std::vector<json> due(uint64_t now, bool host, int revision, const std::vector<std::string>& ids) {
    if (games_.empty()) return {};
    auto& g = games_.front();
    if (now < g.next) return {};
    if (g.match.empty()) {
      if (!host) return {};
      g.next = now + 2000;
      return {{{"t", "begin"}, {"revision", revision}}};
    }
    if (g.winner == -2 && g.team_result.is_null()) return {};
    json finish = {{"t", "finish"}, {"matchId", g.match}};
    if (!g.team_result.is_null()) finish.update(g.team_result);
    else if (g.winner >= 0 && g.winner < (int)ids.size()) finish["winner"] = ids[g.winner];
    else finish["void"] = true;
    g.next = now + 2000; g.finish_sent = true;
    return {finish};
  }
#ifdef YOUGAME_AGENT_TEST_HOOK
  bool agent_test_pending() const { return !games_.empty(); }
#endif
 private:
  struct Game { std::string match; json team_result; int winner = -2; uint64_t next = 0; bool finish_sent = false; };
  std::deque<Game> games_;
  bool in_game_ = false;
};

// Link establishment's single budget includes bridge answers, not 12 seconds per peer.
struct Links {
  std::vector<unsigned> ids;
  uint64_t until = 0;
  void begin(uint64_t now) { until = now + 12000; }
  int budget(uint64_t now) const { return until > now ? (int)(until - now) : 0; }
  template<class State> bool up(uint64_t now, State state) const {
    return budget(now) && !ids.empty() && std::all_of(ids.begin(), ids.end(), [&](unsigned id) { return state(id) == 2; });
  }
  template<class State> bool failed(uint64_t now, State state) const {
    return !budget(now) || std::any_of(ids.begin(), ids.end(), [&](unsigned id) { return state(id) == 3; });
  }
};
inline void close_links(Links& links, Request request) {
  const auto ids = std::move(links.ids);
  links.ids.clear();
  for (unsigned id : ids) { try { request({{"type", "directClose"}, {"link", id}}, 1000); } catch (...) {} }
}
inline bool retry_link_failure(int mode) { return mode != 0 && mode != 2; }
template<class State, class Clock, class Pause, class Cancel>
bool open_links(Links& links, int conn, const std::vector<std::string>& peers, Request request,
                State state, Clock clock, Pause pause, Cancel cancelled) {
  links.begin(clock());
  try {
    for (const auto& peer : peers) {
      const int budget = links.budget(clock());
      if (!budget || cancelled()) return false;
      const unsigned id = request({{"type", "directOpen"}, {"conn", conn}, {"peer", peer}}, budget).value("link", 0u);
      if (!id || id > 65534 || std::find(links.ids.begin(), links.ids.end(), id) != links.ids.end()) return false;
      links.ids.push_back(id);
    }
    while (!links.up(clock(), state)) {
      if (cancelled() || links.failed(clock(), state)) return false;
      pause();
    }
    return true;
  } catch (...) { return false; }
}
} // namespace yougame::transport
