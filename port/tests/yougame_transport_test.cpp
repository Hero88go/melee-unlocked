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
  auto start=set.due(50,true,7); CHECK(start[0]["t"]=="game-start"); set.sent(start[0]);
  yougame::ranked::Game game; game.index=1; game.winner=0; set.report_game(game);
  auto report=set.due(100,true,7); CHECK(report.size()==1 && report[0]["t"]=="game-report"); set.sent(report[0]);
  CHECK(r.dropped(101,true));
  CHECK(r.dropped(14000,true) && r.remaining(14000)==1101); // repeats do not extend grace
  auto partial=welcome(); partial["players"].erase(1);
  CHECK(!r.restore(partial,5000) && r.pending());
  CHECK(r.restore(welcome(),14000) && !r.pending());
  set.rejoined();
  auto resumed_start=set.due(14000,true,8); CHECK(resumed_start==start); set.sent(resumed_start[0]);
  auto resumed=set.due(14001,true,8);
  CHECK(resumed==report && set.awaiting_receipt() && set.needs_drain() && !set.closed());
  set.sent(resumed[0]); CHECK(set.due(15000,true,8).empty());
  set.receive({{"t","game-result"},{"matchId","set-1"},{"id","g1-t0"},{"ranking",{"a","b"}}});
  CHECK(set.games_received()==1);
  set.start_game(); start=set.due(15500,true,8); CHECK(start[0]["t"]=="game-start"); set.sent(start[0]);
  game.index=2; set.report_game(game);
  report=set.due(16000,true,8); CHECK(report[0]["t"]=="game-report"); set.sent(report[0]);
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
static void mixed_version_reasons() {
  using yougame::transport::lobby_protocol_mismatch;
  CHECK(lobby_protocol_mismatch({{"reason","the rooms server answered 403: This room uses a different lobby protocol or configuration"}}));
  CHECK(lobby_protocol_mismatch({{"reason","HTTP 403: INCOMPATIBLE LOBBY PROTOCOL; please update"}}));
  CHECK(lobby_protocol_mismatch({{"errorCode","console_mixed_builds"}}));
  CHECK(lobby_protocol_mismatch({{"refused","lobby_mismatch"}}));
  CHECK(!lobby_protocol_mismatch({{"reason","the rooms server answered 403: ticket expired"}}));
  CHECK(!lobby_protocol_mismatch({{"reason","connection lost"}}));
  CHECK(!lobby_protocol_mismatch({{"reason",12}}));
  // Rooms' Teams-mark refusals: a room of another kind (friends room-kind retries), not a lobby_mismatch.
  using yougame::transport::room_kind_refusal;
  CHECK(room_kind_refusal({{"reason","the rooms server answered 403: This room uses another Teams protocol"}}));
  CHECK(room_kind_refusal({{"reason","the rooms server answered 403: This room uses another Doubles profile"}}));
  CHECK(!lobby_protocol_mismatch({{"reason","the rooms server answered 403: This room uses another Teams protocol"}}));
  CHECK(!room_kind_refusal({{"reason","the rooms server answered 410: Your friend isn't in that game any more"}}));
  CHECK(!room_kind_refusal({{"reason","the rooms server answered 409: Your home hold expired. Open a new Teams home."}}));
  CHECK(!room_kind_refusal({{"reason","the rooms server answered 403: This room uses a different lobby protocol or configuration"}}));
  CHECK(!room_kind_refusal({{"reason",12}}));
  // The app names rooms' refusal code as "answered <status> [<slug>]: <error>": the slug decides, read anchored at
  // the start (brackets in the error words never count); without one the words decide.
  using yougame::transport::rooms_refusal_slug;
  CHECK(rooms_refusal_slug("the rooms server answered 403 [teams_protocol]: This room uses another Teams protocol") == "teams_protocol");
  CHECK(rooms_refusal_slug("the rooms server answered 403 [doubles_profile]: This room uses another Doubles profile") == "doubles_profile");
  CHECK(rooms_refusal_slug("the rooms server answered 403: This room uses another Teams protocol").empty());
  CHECK(rooms_refusal_slug("the rooms server answered 403: bad [teams_protocol] words").empty());   // a bracket in the error
  CHECK(rooms_refusal_slug("x the rooms server answered 403 [teams_protocol]: y").empty());          // anchored
  CHECK(rooms_refusal_slug("the rooms server answered 40 [teams_protocol]: y").empty());
  CHECK(rooms_refusal_slug("the rooms server answered 4031 [teams_protocol]: y").empty());
  CHECK(rooms_refusal_slug("the rooms server answered 403 []: y").empty());
  CHECK(rooms_refusal_slug("the rooms server answered 403 [Teams]: y").empty());
  CHECK(rooms_refusal_slug("the rooms server answered 403 [teams-protocol]: y").empty());
  CHECK(rooms_refusal_slug("the rooms server answered 403 [teams_protocol").empty());
  CHECK(rooms_refusal_slug("the rooms server answered 403 [" + std::string(32, 'a') + "]: y") == std::string(32, 'a'));
  CHECK(rooms_refusal_slug("the rooms server answered 403 [" + std::string(33, 'a') + "]: y").empty());
  CHECK(room_kind_refusal({{"reason","the rooms server answered 403 [teams_protocol]: This room uses another Teams protocol"}}));
  CHECK(room_kind_refusal({{"reason","the rooms server answered 403 [doubles_profile]: This room uses another Doubles profile"}}));
  CHECK(room_kind_refusal({{"reason","the rooms server answered 403 [teams_protocol]: words [with] brackets"}}));
  CHECK(room_kind_refusal({{"message","the rooms server answered 403 [doubles_profile]: changed words"}}));
  CHECK(!room_kind_refusal({{"reason","the rooms server answered 410 [host_gone]: Your friend isn't in that game any more"}}));
  // A slug that isn't a kind decides too, whatever the words say; a bracketed word inside the error is no slug.
  CHECK(!room_kind_refusal({{"reason","the rooms server answered 403 [lobby_mismatch]: This room uses another Teams protocol"}}));
  CHECK(room_kind_refusal({{"reason","the rooms server answered 403: [note] This room uses another Doubles profile"}}));
  CHECK(!room_kind_refusal({{"reason","the rooms server answered 403: [teams_protocol] ticket expired"}}));
  using yougame::transport::rooms_busy;
  CHECK(rooms_busy({{"reason","the rooms server answered 503: Please try again shortly."}}));
  CHECK(!rooms_busy({{"reason","the rooms server answered 503: Room metadata unavailable"}}));
  CHECK(!rooms_busy({{"reason","the rooms server answered 401: Bad or expired ticket"}}));
  CHECK(!rooms_busy({{"reason","connection lost"}}));
  CHECK(!rooms_busy({{"reason",12}}));
  using yougame::transport::rooms_full;
  CHECK(rooms_full({{"reason","the rooms server answered 409: Room is full"}}));
  CHECK(rooms_full({{"message","the rooms server answered 409: Room is full"}}));
  CHECK(!rooms_full({{"reason","the rooms server answered 409: Build mismatch"}}));
  CHECK(!rooms_full({{"reason",12}}) && !rooms_full(json::object()));
}
// The app's answer to netOpen can take the whole open timeout (a slow reply, a starved thread): the open
// or refusal it already queued still decides, rather than a timeout that leaves the stream half open.
static void answer_after_deadline() {
  Events events; uint64_t t = 1000; bool refuse = false; int next = 1;
  Stream s(events, [&](const json& q, int) -> json {
    if (q["type"] != "netOpen") return json();
    const int id = next++;
    if (refuse) {
      events.push({{"conn",id},{"event","error"},{"code","refused"},{"message","the rooms server answered 409: Room is full"}});
      events.push({{"conn",id},{"event","close"},{"code",1006},{"reason","the rooms server answered 409: Room is full"}});
    } else events.push({{"conn",id},{"event","open"}});
    t += 500;
    return json{{"conn",id}};
  }, [&] { return t; });
  CHECK(s.open("test-ticket","abcdef",100) && !s.closed());
  s.close();
  refuse = true;
  CHECK(!s.open("test-ticket","abcdef",100) && s.closed() && s.full());
}
// The app's refusal of a full friend room: an error event, then a 1006 close with the same words.
static void full_room_refusal() {
  Events events; int next=1; bool refuse=true;
  Stream s(events,[&](const json& q,int) {
    if(q["type"]!="netOpen") return json();
    const int id=next++;
    if(refuse) {
      events.push({{"type","net"},{"conn",id},{"event","error"},{"code","refused"},{"message","the rooms server answered 409: Room is full"}});
      events.push({{"type","net"},{"conn",id},{"event","close"},{"code",1006},{"reason","the rooms server answered 409: Room is full"}});
    } else events.push({{"conn",id},{"event","open"}});
    return json{{"conn",id}};
  });
  CHECK(!s.open("test-ticket","abcdef",100) && s.closed() && s.close_code()==1006 && s.full() && !s.busy());
  refuse=false;
  CHECK(s.open("test-ticket","abcdef",100) && !s.full());   // a fresh open forgets it
  events.push({{"conn",2},{"event","error"},{"code","refused"},{"message","the rooms server answered 409: Room is full"}});
  events.push({{"conn",2},{"event","close"},{"code",1006},{"reason","connection lost"}});
  json m; CHECK(!s.next(m,100) && s.closed() && s.full());
  // Any other 1006 drop stays recoverable.
  refuse=false; CHECK(s.open("test-ticket","abcdef",100));
  events.push({{"conn",3},{"event","close"},{"code",1006},{"reason","connection lost"}});
  CHECK(!s.next(m,100) && s.closed() && !s.full());
  // Past the upgrade: the losing seat of a race (1008), or a recovery client's final refusal (4004).
  CHECK(s.open("test-ticket","abcdef",100));
  events.push({{"conn",4},{"event","close"},{"code",1008},{"reason","room full"}});
  CHECK(!s.next(m,100) && s.full());
  CHECK(s.open("test-ticket","abcdef",100));
  events.push({{"conn",5},{"event","close"},{"code",4004},{"reason","rejoin-refused:room-full"}});
  CHECK(!s.next(m,100) && s.full());
  CHECK(s.open("test-ticket","abcdef",100));
  events.push({{"conn",6},{"event","close"},{"code",1008},{"reason","too many messages"}});
  CHECK(!s.next(m,100) && !s.full());
}
// A Teams-mark refusal named by its slug (error event and close, whatever the words), or only by the close's
// words (an older app); any other 1006 is not one, and a fresh open forgets it.
static void kind_code_refusal() {
  Events events; int next=1; int how=0;
  Stream s(events,[&](const json& q,int) {
    if(q["type"]!="netOpen") return json();
    const int id=next++;
    if(how==0) {
      events.push({{"type","net"},{"conn",id},{"event","error"},{"code","refused"},{"message","the rooms server answered 403 [doubles_profile]: changed words"}});
      events.push({{"type","net"},{"conn",id},{"event","close"},{"code",1006},{"reason","the rooms server answered 403 [doubles_profile]: changed words"}});
    } else if(how==1) {
      events.push({{"type","net"},{"conn",id},{"event","close"},{"code",1006},{"reason","the rooms server answered 403: This room uses another Teams protocol"}});
    } else if(how==2) {
      events.push({{"type","net"},{"conn",id},{"event","close"},{"code",1006},{"reason","the rooms server answered 410: Your friend isn't in that game any more"}});
    } else events.push({{"conn",id},{"event","open"}});
    return json{{"conn",id}};
  });
  CHECK(!s.open("test-ticket","abcdef",100) && s.closed() && s.kind_refused() && !s.lobby_mismatch());
  how=1; CHECK(!s.open("test-ticket","abcdef",100) && s.kind_refused());
  how=2; CHECK(!s.open("test-ticket","abcdef",100) && !s.kind_refused());
  how=3; CHECK(s.open("test-ticket","abcdef",100) && !s.kind_refused());
}
static void seats_without_links() {
  // Rooms' links:false (a standalone copy) moves the whole match to own UDP; no key means links.
  CHECK(!any_seat_without_links(json::array()));
  CHECK(!any_seat_without_links(json::array({{{"connectionId","a"}},{{"connectionId","b"},{"links",true}}})));
  CHECK(any_seat_without_links(json::array({{{"connectionId","a"}},{{"connectionId","b"},{"links",false}}})));
  CHECK(!any_seat_without_links(json::array({{{"connectionId","a"},{"links","false"}},{{"links",0}}})));
  CHECK(!any_seat_without_links(json::object({{"links",false}})) && !any_seat_without_links(json()));
  CHECK(seat_without_links(json{{"links",false}}) && !seat_without_links(json{{"links",nullptr}}) && !seat_without_links(json(false)));
}
static void links_flag() {
  // Windows copies say they can share a match with a links:false seat, on tickets and room holds; other
  // systems never do, and no other mpFriend (an invite, the picker) carries it.
  json ticket = {{"type","mpTicket"},{"players",2}};
  CHECK(with_links_flag(ticket, true).value("links_flag", 0) == 1 && ticket["links_flag"].is_number_integer());
  json other = {{"type","mpTicket"},{"players",2}};
  CHECK(!with_links_flag(other, false).count("links_flag"));
  json hold = {{"type","mpFriend"},{"hold",true},{"players",4},{"compatibility","c"}};
  CHECK(with_links_flag(hold, true).value("links_flag", 0) == 1 && hold.size() == 5);
  json hold_off = {{"type","mpFriend"},{"hold",true},{"players",2}};
  CHECK(!with_links_flag(hold_off, false).count("links_flag"));
  json invite = {{"type","mpFriend"},{"room","ABCDEFGHJKLM"},{"players",4},{"multi",true}};
  json picker = {{"type","mpFriend"},{"mode","singles"},{"players",2}};
  json not_hold = {{"type","mpFriend"},{"hold",false}}, odd_hold = {{"type","mpFriend"},{"hold",1}};
  CHECK(!with_links_flag(invite, true).count("links_flag") && !with_links_flag(picker, true).count("links_flag"));
  CHECK(!with_links_flag(not_hold, true).count("links_flag") && !with_links_flag(odd_hold, true).count("links_flag"));
  json odd = json::array(); json typeless = {{"type",1}};
  CHECK(with_links_flag(odd, true).is_array() && !with_links_flag(typeless, true).count("links_flag"));
  json here = {{"type","mpTicket"}}, here_hold = {{"type","mpFriend"},{"hold",true}};
#ifdef _WIN32
  CHECK(kLinksFlag && with_links_flag(here).value("links_flag", 0) == 1 && with_links_flag(here_hold).value("links_flag", 0) == 1);
#else
  CHECK(!kLinksFlag && !with_links_flag(here).count("links_flag") && !with_links_flag(here_hold).count("links_flag"));
#endif
}
int main() {
  mixed_version_reasons(); answer_after_deadline(); full_room_refusal(); kind_code_refusal();
  closed_during_event_wait(); closed_state_transitions();
  mode_and_candidates(); broker_ordering(); unranked_recovery(); link_budget(); recovery_and_ranked(); leave_reasons(); lobby_match_links(); seats_without_links(); links_flag();
  std::puts("yougame_transport: mode, candidates, broker ordering, link timeout/retry, leave reasons, lobby/match links, seats without links, seat recovery and ranked resume passed");
}
