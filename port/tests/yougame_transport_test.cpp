// Offline: clang++ -std=c++20 -Iport/runtime/hle -Iport/third_party ...
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_transport.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
using namespace yougame::transport;

static json participants() {
  return json::array({{{"id","a"},{"connectionId","a"},{"slot",0},{"localIndex",0},{"user","account-a"}},
                      {{"id","b"},{"connectionId","b"},{"slot",1},{"localIndex",0},{"user","account-b"}}});
}
static json welcome() {
  return {{"t","welcome"},{"me","a"},{"room",{{"code","abcdef"}}},{"matchId","set-1"},
          {"participants",participants()},{"players",json::array({{{"id","a"},{"user","account-a"}},{{"id","b"},{"user","account-b"}}})}};
}
static void mode_and_candidates() {
  CHECK(!Mode::hello(json::object()).broker);
  CHECK(posix_datagram_route(json::object())==DatagramRoute::Standalone);
  json h={{"netBroker",{{"version",1},{"mode","internet"}}}};
  CHECK(!Mode::hello(h).broker && !Mode::hello(h).direct);
  CHECK(posix_datagram_route(h)==DatagramRoute::Standalone);
  h["netBroker"]["mode"]="yougame";
  CHECK(Mode::hello(h).broker && !Mode::hello(h).direct);
  CHECK(posix_datagram_route(h)==DatagramRoute::Refuse);
  h["netBroker"]["direct"]={{"version",1},{"maxDatagram",1200}};
  CHECK(Mode::hello(h).direct);
  CHECK(posix_datagram_route(h)==DatagramRoute::Ring);
  for (const char* key : {"version", "maxDatagram"}) {
    json bad=h; bad["netBroker"]["direct"][key]=key==std::string("version")?1.5:1200.5;
    CHECK(posix_datagram_route(bad)==DatagramRoute::Refuse);
    bad=h; bad["netBroker"]["direct"][key]=true;
    CHECK(posix_datagram_route(bad)==DatagramRoute::Refuse);
    bad=h; bad["netBroker"]["direct"][key]=uint64_t(1)<<63;
    CHECK(posix_datagram_route(bad)==DatagramRoute::Refuse);
  }
  h["netBroker"]["direct"]["maxDatagram"]=1400;
  CHECK(!Mode::hello(h).direct && Mode::hello(h).unsupported);
  CHECK(posix_datagram_route(h)==DatagramRoute::Refuse);
  h["netBroker"]["version"]=2;
  CHECK(posix_datagram_route(h)==DatagramRoute::Refuse);
  h["netBroker"]["version"]="bad";
  CHECK(posix_datagram_route(h)==DatagramRoute::Refuse);
  PosixAttachment p;
  const json valid={{"posix",{{"version",1},{"section",3},{"wake",4},{"wait",5},{"size",655744}}}};
  CHECK(posix_attachment(valid,p) && p.section==3 && p.wake==4 && p.wait==5 && p.size==655744);
  CHECK(!posix_attachment(json::object(),p));
  for (const char* key : {"version","section","wake","wait","size"}) {
    json bad=valid; bad["posix"].erase(key); CHECK(!posix_attachment(bad,p));
    bad=valid; bad["posix"][key]=-1; CHECK(!posix_attachment(bad,p));
    bad=valid; bad["posix"][key]=3.5; CHECK(!posix_attachment(bad,p));
    bad=valid; bad["posix"][key]="3"; CHECK(!posix_attachment(bad,p));
  }
  for (const char* key : {"section","wake","wait"}) {
    json bad=valid; bad["posix"][key]=uint64_t(INT_MAX)+4; CHECK(!posix_attachment(bad,p));
  }
  json bad=valid; bad["posix"]["version"]=2; CHECK(!posix_attachment(bad,p));
  const auto d=candidate(true,"mu-test","TAG#1","yg-match","legacy-lan","legacy-ext","test-only");
  CHECK(d==json({{"kind","mu-cand"},{"v",2},{"compat","mu-test"},{"code","TAG#1"},{"match","yg-match"}}));
  const auto legacy=candidate(false,"mu-test","TAG#1","yg-match","legacy-lan","legacy-ext","test-only");
  CHECK(legacy.count("lan") && legacy.count("ext") && legacy.count("token"));
}
static void broker_ordering() {
  Events events; std::vector<json> sent; int next=1;
  Stream s(events,[&](const json& q,int) {
    sent.push_back(q);
    if(q["type"]=="netOpen") {
      const int id=next++;
      // All pushes arrive before netOpen's answer, as on a fast broker socket.
      events.push({{"conn",id},{"event","open"}});
      events.push({{"conn",id},{"event","message"},{"data",json({{"t","welcome"}}).dump()}});
      events.push({{"conn",id},{"event","message"},{"data",json({{"t","matched"},{"room","abcdef"}}).dump()}});
      return json{{"conn",id}};
    }
    return json();
  });
  CHECK(s.open("test-ticket","",100));
  CHECK(sent[0]==json({{"type","netOpen"},{"ticket","test-ticket"}}));
  json m; CHECK(s.next(m,0) && m["t"]=="welcome");
  CHECK(s.next(m,0) && m["t"]=="matched");
  CHECK(s.send({{"t","ping"},{"at",1}}));
  CHECK(sent.back()==json({{"type","netSend"},{"conn",1},{"data","{\"at\":1,\"t\":\"ping\"}"}}));
  events.push({{"conn",1},{"event","close"},{"code",1006}});
  CHECK(!s.next(m,0) && s.closed() && s.close_code()==1006);
  CHECK(s.open("fresh-test-ticket","abcdef",100));
  CHECK(std::none_of(sent.begin(),sent.end(),[](const json& q){return q["type"]=="netClose";}));
  CHECK(sent.back()==json({{"type","netOpen"},{"ticket","fresh-test-ticket"},{"room","abcdef"}}));
  CHECK(s.next(m,0) && m["t"]=="welcome");
  s.close();
  // No send while opening: it must wait for the open push, even after the response arrived.
  Stream slow(events,[&](const json& q,int) { sent.push_back(q); return json{{"conn",3}}; });
  CHECK(!slow.open("test-ticket","abcdef",1));
  CHECK(!slow.send({{"t","begin"}}));
  CHECK(sent.back()["type"]=="netOpen");
  CHECK(!slow.closed()); // still opening: a late app open remains the same connection
  events.push({{"conn",3},{"event","open"}});
  CHECK(slow.open("test-ticket","abcdef",100));
  CHECK(slow.send({{"t","begin"}}));

}
static void closed_during_event_wait() {
  std::mutex wait_mutex;
  std::condition_variable wait_cv;
  bool waiting = false, release = false;
  Events events([&](int ms) {
    std::unique_lock<std::mutex> lock(wait_mutex);
    waiting = true;
    wait_cv.notify_one();
    wait_cv.wait_for(lock, std::chrono::milliseconds(ms), [&] { return release; });
  });
  Stream s(events, [&](const json& q, int) -> json {
    if (q["type"] == "netOpen") {
      events.push({{"conn", 1}, {"event", "open"}});
      return {{"conn", 1}};
    }
    return nullptr;
  });
  CHECK(s.open("test-ticket", "abcdef", 100));
  std::thread reader([&] { json m; CHECK(!s.next(m, 300)); });
  {
    std::unique_lock<std::mutex> lock(wait_mutex);
    CHECK(wait_cv.wait_for(lock, std::chrono::seconds(2), [&] { return waiting; }));
  }
  const auto start = std::chrono::steady_clock::now();
  const bool closed = s.closed();
  const auto elapsed = std::chrono::steady_clock::now() - start;
  {
    std::lock_guard<std::mutex> lock(wait_mutex);
    release = true;
  }
  wait_cv.notify_one();
  reader.join();
  CHECK(!closed);
  CHECK(elapsed < std::chrono::milliseconds(20));
}
static void closed_state_transitions() {
  Events events; int next = 1;
  Stream s(events, [&](const json& q, int) -> json {
    if (q["type"] == "netOpen") {
      const int id = next++;
      events.push({{"conn", id}, {"event", "open"}});
      return {{"conn", id}};
    }
    return nullptr;
  });
  CHECK(s.closed());
  CHECK(s.open("test-ticket", "abcdef", 100) && !s.closed());
  events.push({{"conn", 1}, {"event", "close"}, {"code", 1006}});
  json m;
  CHECK(!s.next(m, 0));
  CHECK(s.closed() && !s.ready() && s.close_code() == 1006);
  CHECK(!s.send({{"t", "ping"}}));
  CHECK(s.open("test-ticket", "abcdef", 100) && !s.closed());
  s.close();
  CHECK(s.closed() && !s.ready());
  CHECK(!s.send({{"t", "ping"}}) && !s.next(m, 0));
}
static void unranked_recovery() {
  Games games;
  const std::vector<std::string> ids={"a","b"};
  games.start_game(); games.start_game(); // repeated frame 1 counts once
  CHECK(games.due(1,true,7,ids)[0]==json({{"t","begin"},{"revision",7}}));
  games.started("game-1"); games.report(0);
  auto finish=games.due(2,true,7,ids);
  CHECK(finish[0]==json({{"t","finish"},{"matchId","game-1"},{"winner","a"}}));
  // Both games finish during the outage. An older finish and its receipt retain their identity.
  games.start_game(); games.report(1);
  CHECK(games.due(14000,true,8,ids)==finish);
  games.receive({{"t","result"},{"matchId","wrong"}});
  CHECK(games.due(16000,true,8,ids)==finish);
  games.receive({{"t","result"},{"matchId","game-1"}});
  CHECK(games.due(16001,true,8,ids)[0]["t"]=="begin");
  games.started("game-2");
  CHECK(games.due(16002,true,8,ids)[0]==json({{"t","finish"},{"matchId","game-2"},{"winner","b"}}));
  games.receive({{"t","result"},{"matchId","game-1"}}); // duplicate old receipt does not retire game 2
  CHECK(games.due(18002,true,8,ids)[0]["matchId"]=="game-2");
  games.receive({{"t","result"},{"matchId","game-2"}});
  CHECK(games.due(18003,true,8,ids).empty());
  Games nonhost; nonhost.started("early"); nonhost.start_game("early"); nonhost.report(-1);
  CHECK(nonhost.due(1,false,8,ids)[0]==json({{"t","finish"},{"matchId","early"},{"void",true}}));
}
static void link_budget() {
  uint64_t clock=10; std::vector<json> sent; unsigned next=1;
  const auto request=[&](const json& q,int budget) { CHECK(budget>0 && budget<=12000); sent.push_back(q); return json{{"link",next++}}; };
  Links links;
  CHECK(open_links(links,7,{"b","c","d"},request,[](unsigned){return 2;},[&]{return clock;},[&]{clock+=10;},[]{return false;}));
  CHECK(links.ids==std::vector<unsigned>({1,2,3}));
  CHECK(sent[0]==json({{"type","directOpen"},{"conn",7},{"peer","b"}}));
  CHECK(sent.size()==3);
  links={}; clock=10;
  CHECK(!open_links(links,7,{"b"},request,[](unsigned){return 1;},[&]{return clock;},[&]{clock+=10;},[]{return false;}));
  CHECK(clock==12010); // one 12 s budget, no per-peer extension
  links={}; clock=10;
  CHECK(!open_links(links,7,{"b"},request,[](unsigned){return 3;},[&]{return clock;},[&]{clock+=10;},[]{return false;}));
  CHECK(clock==10);
  CHECK(retry_link_failure(1) && retry_link_failure(3));
  CHECK(!retry_link_failure(0) && !retry_link_failure(2));
  links={};
  CHECK(!open_links(links,7,{"b"},[](const json&,int)->json{throw std::runtime_error("refused");},[](unsigned){return 1;},[&]{return clock;},[]{},[]{return false;}));
}
static Recovery recovery() { Recovery r; r.configure("abcdef","a",participants()); r.start("set-1"); return r; }
static void recovery_and_ranked() {
  auto r=recovery();
  yougame::ranked::Set set; set.configure(0,{"a","b"}); set.start_game();
  CHECK(set.due(1,true,7)[0]["t"]=="begin");
  set.receive({{"t","start"},{"matchId","set-1"}});
  yougame::ranked::Game game; game.index=1; game.winner=0; set.report_game(game);
  auto report=set.due(100,true,7); CHECK(report.size()==1 && report[0]["t"]=="game-report");
  CHECK(r.dropped(101,true));
  CHECK(r.dropped(14000,true) && r.remaining(14000)==1101); // repeats do not extend grace
  auto partial=welcome(); partial["players"].erase(1);
  CHECK(!r.restore(partial,5000) && r.pending());
  CHECK(r.restore(welcome(),14000) && !r.pending());
  auto resumed=set.due(14000,true,8);
  CHECK(resumed.size()==1 && resumed[0]==report[0] && set.needs_drain() && !set.closed());
  set.receive({{"t","game-result"},{"matchId","set-1"},{"id","g1-t0"},{"ranking",{"a","b"}}});
  CHECK(set.games_received()==1);
  set.start_game(); game.index=2; set.report_game(game);
  CHECK(set.due(16000,true,8)[0]["t"]=="game-report");
  set.receive({{"t","game-result"},{"matchId","set-1"},{"id","g2-t0"},{"ranking",{"a","b"}}});
  CHECK(set.decided() && set.due(18000,true,8)[0]["t"]=="finish");
  set.receive({{"t","result"},{"matchId","set-1"},{"ranking",{"a","b"}}});
  CHECK(set.settled() && !set.needs_drain());
  for(int kind=0;kind<6;kind++) {
    r=recovery(); CHECK(r.dropped(1,true)); auto bad=welcome();
    if(kind==0) bad["room"]["code"]="other";
    if(kind==1) bad["me"]="other";
    if(kind==2) bad["participants"][1]["slot"]=2;
    if(kind==3) bad["participants"][1]["user"]="other-account";
    if(kind==4) bad["matchId"]="another-set";
    if(kind==5) bad["players"][1]["user"]="other-account";
    CHECK(!r.restore(bad,10) && r.expired(10));
  }
  r=recovery(); CHECK(!r.leave({{"t","leave"},{"id","b"}},1,false));
  r=recovery(); CHECK(!r.dropped(1,false));
  r=recovery(); CHECK(r.dropped(1,true) && !r.restore(welcome(),15001));
}
static void leave_reasons() {
  for (const auto* reason : {"left", "kicked"}) for (bool pending : {false, true}) {
    auto r=recovery();
    Links links; links.ids={1,257}; std::vector<json> closed;
    const auto record=[&](const json& q,int)->json { closed.push_back(q); return nullptr; };
    if (pending) CHECK(r.leave({{"t","leave"},{"id","b"},{"reason","dropped"}},1,true));
    CHECK(!r.leave({{"t","leave"},{"id","b"},{"reason",reason}},5000,true));
    CHECK(r.expired(5000) && !r.pending() && r.remaining(5000)==0);
    // Definitive roster departures cancel peer recovery and close links immediately.
    if (r.expired(5000)) close_links(links,record);
    CHECK(links.ids.empty() && closed==std::vector<json>({{{"type","directClose"},{"link",1}},{{"type","directClose"},{"link",257}}}));
    close_links(links,record); CHECK(closed.size()==2);
    CHECK(!r.restore(welcome(),5000) && !r.dropped(5000,true));
    CHECK(!r.pending() && r.remaining(5000)==0);
  }
  for (bool reason : {false, true}) {
    auto r=recovery(); Links links; links.ids={1,257};
    json leave={{"t","leave"},{"id","b"}}; if (reason) leave["reason"]="dropped";
    CHECK(r.leave(leave,1,true) && r.pending());
    CHECK(r.leave(leave,5000,true) && r.remaining(5000)==10001);
    CHECK(!r.expired(15000) && r.remaining(15000)==1);
    CHECK(links.ids==std::vector<unsigned>({1,257}));
    CHECK(!r.leave(leave,15001,true) && r.expired(15001));
    CHECK(!r.restore(welcome(),15001));
    int closes=0; close_links(links,[&](const json&,int)->json { ++closes; return nullptr; });
    CHECK(links.ids.empty() && closes==2);
    r=recovery(); CHECK(r.leave(leave,1,true));
    CHECK(r.restore(welcome(),15000) && !r.pending() && !r.expired(15001));
  }
}

// Model the room pump with real broker frames and the shared lifecycle policies. Links are
// opened in the lobby, then keep their ids through start/result/rematch with an unchanged roster.
static void lobby_match_links() {
  Events events; std::vector<json> requests;
  const auto request=[&](const json& q,int)->json {
    requests.push_back(q);
    if(q["type"]=="netOpen") { events.push({{"conn",7},{"event","open"}}); return {{"conn",7}}; }
    if(q["type"]=="directOpen") return {{"link",257}};
    return nullptr;
  };
  Stream socket(events,request); CHECK(socket.open("test-ticket","abcdef",100));
  Links links;
  CHECK(open_links(links,7,{"b"},request,[](unsigned){return 2;},[]{return uint64_t(1);},[]{},[]{return false;}));
  Recovery r; r.configure("abcdef","a",participants()); Games games;
  for(const auto* match : {"game-1","game-2"}) {
    games.start_game(); CHECK(games.due(1,true,7,{"a","b"})[0]["t"]=="begin");
    json start={{"t","start"},{"matchId",match},{"participants",participants()},{"players",welcome()["players"]}};
    events.push({{"conn",7},{"event","message"},{"data",start.dump()}});
    json m; CHECK(socket.next(m,0) && m==start);
    r.start(yougame::ranked::str(m,"matchId")); games.started(yougame::ranked::str(m,"matchId"));
    CHECK(!r.pending() && !r.expired(1) && links.ids==std::vector<unsigned>({257}));
    games.report(0); CHECK(games.due(2,true,7,{"a","b"})[0]["matchId"]==match);
    games.receive({{"t","result"},{"matchId",match}}); r.result();
    CHECK(!r.expired(2) && links.ids==std::vector<unsigned>({257}));
  }
  CHECK(requests.size()==2 && requests[0]["type"]=="netOpen" && requests[1]["type"]=="directOpen");
  close_links(links,request); CHECK(requests.size()==3 && requests.back()["type"]=="directClose");
}
int main() {
  closed_during_event_wait(); closed_state_transitions();
  mode_and_candidates(); broker_ordering(); unranked_recovery(); link_budget(); recovery_and_ranked(); leave_reasons(); lobby_match_links();
  std::puts("yougame_transport: mode, candidates, broker ordering, link timeout/retry, leave reasons, lobby/match links, seat recovery and ranked resume passed");
}
