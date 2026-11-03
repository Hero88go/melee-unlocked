// Real portable room pump + real Stream, against an offline app broker and timed event queue.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_room.h"
#include <cstdio>
#include <cstdlib>
#include <future>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
using namespace yougame::transport;
static json roster() {
  return json::array({{{"id","a"},{"connectionId","a"},{"slot",0},{"localIndex",0},{"user","account-a"}},
                      {{"id","b"},{"connectionId","b"},{"slot",1},{"localIndex",0},{"user","account-b"}}});
}
static json welcome(const std::string& match = "set-1") {
  // Lobby players deliberately have no seat: no dependency on the app's former relabel bug.
  return {{"t","welcome"},{"me","a"},{"room",{{"code","abcdef"}}},{"matchId",match},
          {"participants",roster()},{"players",json::array({{{"id","a"},{"user","account-a"}},{{"id","b"},{"user","account-b"}}})}};
}
struct Broker {
  uint64_t now = 1;
  struct Action { uint64_t at; std::function<void()> run; };
  std::deque<Action> actions;
  Events events{[this](int ms) { wait(ms); }};
  std::vector<json> requests, sent;
  std::vector<int> waits;
  int conn = 0, link_state = 2, disabled = 0;
  bool fail_send = false, fail_renew = false;
  std::function<void(int)> on_open;
  std::function<void(const json&)> on_send;
  std::function<json(int)> on_ticket;
  void wait(int ms) {
    CHECK(ms >= 0); waits.push_back(ms);
    const auto until = now + ms;
    if (!actions.empty() && actions.front().at <= until) {
      auto action = std::move(actions.front()); actions.pop_front();
      now = std::max(now, action.at); action.run();
    } else now = until; // an empty fake wait consumes precisely the caller's budget
  }
  void at(uint64_t time, std::function<void()> action) { actions.push_back({time,std::move(action)}); }
  void push(const json& m) { events.push({{"conn",conn},{"event","message"},{"data",m.dump()}}); }
  void opened(int id) { events.push({{"conn",id},{"event","open"}}); }
  void closed(int code = 1006) { events.push({{"conn",conn},{"event","close"},{"code",code}}); }
  json request(const json& q, int ms) {
    CHECK(ms > 0); requests.push_back(q);
    const auto type = str(q,"type");
    if (type == "netOpen") {
      ++conn;
      if (on_open) on_open(conn); else opened(conn);
      return {{"conn",conn}};
    }
    if (type == "netSend") {
      if (fail_send) throw std::runtime_error("backed_up");
      auto m = json::parse(q["data"].get<std::string>()); sent.push_back(m);
      if (on_send) on_send(m);
    }
    if (type == "mpTicket") {
      if (on_ticket) return on_ticket(ms);
      if (fail_renew) throw std::runtime_error("temporary ticket failure");
      return {{"ticket","ticket-new"},{"room","abcdef"}};
    }
    if (type == "directOpen") return {{"link",257}};
    return json::object();
  }
  int count(const char* type) const {
    return (int)std::count_if(requests.begin(),requests.end(),[&](const json& q){return str(q,"type")==type;});
  }
  int messages(const char* type) const {
    return (int)std::count_if(sent.begin(),sent.end(),[&](const json& q){return str(q,"t")==type;});
  }
};
struct FakeSocket {
  Broker& broker;
  Stream stream;
  bool broker_mode = true;
  explicit FakeSocket(Broker& b) : broker(b), stream(b.events,[&b](const json& q,int ms){return b.request(q,ms);},[&b]{return b.now;}) {}
  bool open_session(const std::string&,const std::string& ticket,const std::string& room,std::string*,int ms=10000) { return stream.open(ticket,room,ms); }
  bool next(json& m,int ms) { return stream.next(m,ms); }
  bool closed() const { return stream.closed(); }
  bool recoverable() const { return broker_mode && stream.close_code()==1006; }
  bool brokered() const { return broker_mode; }
  bool send(const json& m) { return stream.send(m); }
};
struct Fixture {
  Broker b;
  RoomState<FakeSocket> r;
  std::mutex mutex;
  RoomIO io;
  std::vector<std::string> logs;
  Fixture() {
    r.socket=std::make_unique<FakeSocket>(b);
    CHECK(r.socket->stream.open("ticket-old","abcdef",100));
    r.me=r.host_connection="a"; r.code="abcdef"; r.ticket="ticket-old";
    r.ticket_request={{"type","mpTicket"}}; r.port_ids={"a","b"}; r.revision=7;
    r.recovery.configure(r.code,r.me,roster());
    io.request=[this](const json& q,int ms){return b.request(q,ms);};
    io.clock=[this]{return b.now;}; io.pause=[this](int ms){b.wait(ms);}; io.link_state=[this](unsigned){return b.link_state;};
    io.disable_links=[this]{++b.disabled;};
    io.log=[this](const char* message){logs.emplace_back(message);};
  }
  void ranked() {
    r.ranked=true; r.set.configure(0,{"a","b"}); r.set.start_game();
    r.match_id="set-1"; r.recovery.start(r.match_id);
    r.set.receive({{"t","start"},{"matchId","set-1"}});
  }
  void report(int index=1) {
    yougame::ranked::Game g; g.index=index; g.winner=0; r.set.report_game(g);
  }
  void tick() { b.push({{"t","test-tick"}}); }
  void stop_at(uint64_t at) { b.at(at,[this]{r.stop=true;tick();}); }
  void run() { pump_room(&r,mutex,io); CHECK(b.actions.empty()); }
};
static void failed_send_is_loss() {
  for (const char* error : {"timeout","backed_up","rate_messages"}) {
    Events events; int closes=0, sends=0;
    Stream stream(events,[&](const json& q,int)->json {
      if (q["type"]=="netOpen") {events.push({{"conn",1},{"event","open"}}); return {{"conn",1}};}
      if (q["type"]=="netClose") ++closes;
      if (q["type"]=="netSend" && ++sends==1) throw std::runtime_error(error);
      return json::object();
    });
    CHECK(stream.open("ticket-old","abcdef",10));
    CHECK(!stream.send({{"t","begin"}}) && !stream.closed());
    CHECK(stream.open("ticket-old","abcdef",10) && closes==0); // no replacement of a live stream
    CHECK(stream.send({{"t","begin"}}) && sends==2);
  }
  Fixture f; f.ranked(); f.report(); f.b.fail_send=true; f.tick();
  f.b.at(101,[&]{f.b.fail_send=false; f.tick();});
  f.b.at(2201,[&]{CHECK(f.b.messages("game-report")==1 && !f.r.set.closed());f.tick();});
  f.stop_at(2301); f.run();
  CHECK(f.b.count("netOpen")==1 && f.b.count("netClose")==0);
}
static void timeout_and_late_open() {
  Broker b; b.on_open=[](int){};
  {
    Stream s(b.events,[&](const json& q,int ms){return b.request(q,ms);},[&]{return b.now;});
    CHECK(!s.open("ticket-old","abcdef",2000) && b.now==2001 && !s.closed());
    CHECK(!s.open("ticket-old","abcdef",2000) && b.now==4001 && b.count("netOpen")==1);
    b.at(4501,[&]{b.opened(b.conn);});
    CHECK(s.open("ticket-old","abcdef",2000) && b.now==4501);
    CHECK(s.send({{"t","ping"}}) && b.count("netClose")==0);
  }
  CHECK(b.requests.back()["type"]=="netClose" && b.requests.back()["code"]==4000);
  // The real pump sees an unexpected close, then a reopen which times out but opens later.
  Fixture f; f.ranked(); f.report(); f.r.links.ids={257};
  f.b.on_open=[](int){};
  f.b.closed();
  f.b.at(2501,[&]{CHECK(f.b.count("netOpen")==2 && f.b.count("netClose")==0); f.b.opened(f.b.conn);});
  f.b.at(2502,[&]{f.b.push(welcome());});
  f.b.at(2701,[&]{CHECK(!f.r.recovery.pending() && f.b.messages("game-report")==1); f.tick();});
  f.stop_at(2801); f.run();
  CHECK(f.b.count("mpTicket")==0 && f.b.count("netClose")==0 && !f.r.set.closed());
}
static void recovery_closes_and_expiry() {
  for (int close_code : {1000,4003}) {
    Fixture f; f.ranked(); f.report(); f.r.links.ids={257}; f.b.closed(close_code);
    f.run(); CHECK(f.r.set.closed() && f.b.disabled==1 && f.b.messages("game-report")==0);
    CHECK(f.b.count("netOpen")==1 && f.b.count("netClose")==0);
  }
  for (bool wrong_match : {false,true}) {
    Fixture f;f.ranked();f.report();f.r.links.ids={257};f.b.closed();
    f.b.on_open=[&](int id){
      f.b.opened(id);auto m=welcome();
      if(wrong_match) m["matchId"]="other-set";else m["me"]="other-player";
      f.b.push(m);
    };
    f.run();CHECK(f.r.set.closed() && f.b.messages("game-report")==0 && f.b.count("netClose")==0);
  }
  Fixture f; f.ranked(); f.report(); f.r.links.ids={257}; f.b.closed();
  f.b.on_open=[&](int){f.b.closed();};
  f.b.fail_renew=true; f.run();
  CHECK(f.b.now>=15001 && f.b.now<=15101 && f.r.set.closed());
  CHECK(f.b.count("mpTicket")==14 && f.b.count("netOpen")>5 && f.b.count("netClose")==0);
  for (const auto& q : f.b.requests) if(q["type"]=="netOpen") CHECK(q["ticket"]=="ticket-old");
}
static void lobby_start_links() {
  Fixture f;
  CHECK(open_links(f.r.links,f.b.conn,{"b"},f.io.request,[&](unsigned){return f.b.link_state;},f.io.clock,[]{},[]{return false;}));
  f.b.push(welcome("")); f.r.games.start_game();
  f.b.at(2,[&]{auto m=welcome("game-1");m["t"]="start";f.b.push(m);});
  f.b.at(3,[&]{CHECK(f.r.match_id=="game-1" && f.b.disabled==0 && f.r.links.ids==std::vector<unsigned>({257})); f.r.games.report(0);f.tick();});
  f.b.at(4,[&]{CHECK(f.b.messages("finish")==1);f.b.push({{"t","result"},{"matchId","game-1"}});});
  f.b.at(5,[&]{CHECK(f.r.match_id.empty() && f.b.disabled==0);f.r.games.start_game();f.tick();});
  f.b.at(6,[&]{auto m=welcome("game-2");m["t"]="start";f.b.push(m);});
  f.b.at(7,[&]{CHECK(f.r.match_id=="game-2" && f.b.count("directOpen")==1 && f.b.count("directClose")==0);f.b.link_state=3;f.tick();});
  f.b.at(8,[&]{CHECK(f.b.disabled==1 && f.r.links.ids.empty());f.r.games.report(1);f.tick();});
  f.stop_at(9); f.run(); CHECK(f.b.count("directClose")==1);
}
static void departure_keeps_ranked_pump() {
  // Owed game report -> receipt -> claim/final -> result, after definitive departure/link close.
  for (const char* reason : {"left","kicked","link"}) {
    Fixture f; f.ranked(); f.report(); f.r.set.lost(); f.r.links.ids={257};
    if (std::string(reason)=="link") {f.b.link_state=3;f.tick();}
    else f.b.push({{"t","leave"},{"player",{{"id","b"}}},{"reason",reason}});
    f.b.at(101,[&]{CHECK(f.b.disabled==1 && !f.r.set.closed() && f.b.messages("game-report")==1);f.b.push({{"t","game-result"},{"matchId","set-1"},{"id","g1-t0"},{"ranking",{"a","b"}}});});
    f.b.at(201,[&]{CHECK(f.b.messages("finish")==1 && !f.r.set.closed());f.b.push({{"t","result"},{"matchId","set-1"},{"ranking",{"a","b"}}});});
    f.stop_at(301); f.run(); CHECK(f.r.set.settled() && !f.r.set.closed());
  }
  for (const char* reason : {"left","kicked"}) {
    Fixture f;f.ranked();f.report();f.r.set.lost();f.r.links.ids={257};
    f.b.push({{"t","leave"},{"id","b"},{"reason","dropped"}});
    f.b.at(101,[&]{CHECK(f.r.recovery.pending() && f.b.disabled==0);f.b.push({{"t","leave"},{"id","b"},{"reason",reason}});});
    f.b.at(201,[&]{CHECK(!f.r.recovery.pending() && f.b.disabled==1 && f.b.messages("game-report")==1 && !f.r.set.closed());f.tick();});
    f.stop_at(301);f.run();
  }
  Fixture f; f.ranked(); f.r.links.ids={257};
  f.b.push({{"t","leave"},{"player",{{"id","spectator"}}},{"reason","left"}});
  f.b.at(2,[&]{CHECK(f.b.disabled==0 && !f.r.recovery.pending());f.tick();});f.stop_at(3);f.run();
}
static void missed_receipts() {
  for (const char* error : {"That game result is already recorded","That game result is already being recorded"}) {
    Fixture f; f.ranked();f.report();
    CHECK(f.r.set.due(f.b.now,true,7)[0]["t"]=="game-report"); // its receipt is lost
    f.r.set.lost(); f.b.closed();
    f.b.on_open=[&](int id){f.b.opened(id);f.b.push(welcome());};
    f.b.at(2201,[&]{CHECK(f.b.messages("game-report")==1);f.b.push({{"t","command-error"},{"action","game-report"},{"id","g1-t0"},{"error",error}});});
    f.b.at(2301,[&]{CHECK(!f.r.set.awaiting_receipt() && f.r.set.games_received()==1 && f.b.messages("finish")==1);f.b.push({{"t","result"},{"matchId","set-1"}});});
    f.stop_at(2401);f.run();CHECK(f.r.set.settled());
    // Internet/non-restored refusals still wait for their authoritative game-result.
    yougame::ranked::Set legacy;legacy.configure(0,{"a","b"});legacy.start_game();legacy.receive({{"t","start"},{"matchId","set-1"}});
    yougame::ranked::Game g;g.index=1;g.winner=0;legacy.report_game(g);legacy.due(1,true,7);
    legacy.refused({{"action","game-report"},{"id","g1-t0"},{"error",error}});CHECK(legacy.awaiting_receipt());
  }
  for (bool refused : {false,true}) {
    Fixture f;f.r.games.start_game();f.r.games.started("old-game");f.r.games.report(0);
    CHECK(f.r.games.due(1,true,7,f.r.port_ids)[0]["t"]=="finish");
    f.r.games.start_game();f.r.games.report(1); // a later game queued during the outage
    if (refused) f.b.push({{"t","command-error"},{"action","finish"},{"error","No active match"}});
    else {f.b.closed();f.b.on_open=[&](int id){f.b.opened(id);f.b.push(welcome(""));};}
    f.b.at(101,[&]{CHECK(f.b.messages("begin")==1);f.tick();});f.stop_at(201);f.run();
  }
  for (bool refused : {false,true}) {
    Fixture f;f.r.match_id="old-game";f.r.recovery.start("old-game");
    f.r.games.start_game("old-game");f.r.games.report(0);
    CHECK(f.r.games.due(1,true,7,f.r.port_ids)[0]["t"]=="finish");
    if (refused) f.b.push({{"t","command-error"},{"action","finish"},{"error","No active match"}});
    else {f.b.closed();f.b.on_open=[&](int id){f.b.opened(id);f.b.push(welcome(""));};}
    f.b.at(101,[&]{CHECK(f.r.match_id.empty());f.r.games.start_game(f.r.match_id);f.tick();});
    f.b.at(201,[&]{CHECK(f.b.messages("begin")==1 && f.b.messages("finish")==0);f.tick();});
    f.stop_at(301);f.run();
  }

}
static void ticket_allowance() {
  TicketRetry retry;int opens=0,renewals=0;std::string ticket="old";
  for (int i=0;i<20;++i) CHECK(!retry.attempt([&]{++opens;CHECK(ticket==(opens==1?"old":"new"));return false;},[&]{++renewals;ticket="new";}));
  CHECK(opens==20 && renewals==19);
  Fixture f;f.ranked();f.b.closed();
  f.b.on_open=[&](int id){if(id<4) f.b.closed();else {f.b.opened(id);f.b.push(welcome());}};
  f.b.at(2401,[&]{CHECK(f.b.count("mpTicket")==2 && !f.r.recovery.pending());f.b.closed();}); // next outage tries the current ticket first
  f.b.at(2601,[&]{CHECK(f.b.count("mpTicket")==2 && !f.r.recovery.pending());f.tick();});
  f.stop_at(2701);f.run();
  std::vector<std::string> tickets;for(const auto& q:f.b.requests)if(q["type"]=="netOpen")tickets.push_back(q["ticket"]);
  CHECK(tickets==std::vector<std::string>({"ticket-old","ticket-old","ticket-new","ticket-new","ticket-new"}));
}
static void ticket_renewal_recovers() {
  Fixture f; f.ranked(); f.b.closed();
  int renewals=0;
  f.b.on_ticket=[&](int ms)->json {
    CHECK(ms==3000);
    if (++renewals<=2) throw std::runtime_error("temporary ticket failure");
    return {{"ticket","ticket-new"},{"room","abcdef"}};
  };
  f.b.on_open=[&](int id){
    if(id<5) f.b.closed();
    else {f.b.opened(id);f.b.push(welcome());}
  };
  f.stop_at(3201); f.run();
  CHECK(renewals==3 && f.r.ticket=="ticket-new" && !f.r.recovery.pending() && !f.r.set.closed());
  std::vector<std::string> tickets;
  for(const auto& q:f.b.requests) if(q["type"]=="netOpen") tickets.push_back(q["ticket"]);
  CHECK(tickets==std::vector<std::string>({"ticket-old","ticket-old","ticket-old","ticket-old","ticket-new"}));
  CHECK(f.logs==std::vector<std::string>({"yougame: room ticket renewal failed: request failed",
    "yougame: room ticket renewal failed: request failed","yougame: room ticket renewed"}));
}
static void ticket_renewal_deadline() {
  Fixture f; f.ranked(); f.b.closed();
  f.b.on_open=[&](int){f.b.closed();};
  std::vector<int> budgets;
  f.b.on_ticket=[&](int ms)->json {
    CHECK(f.b.now<15001 && ms<=3000 && ms<=15001-f.b.now);
    budgets.push_back(ms); f.b.now+=ms;
    throw std::runtime_error("The YouGame app did not answer");
  };
  f.run();
  CHECK(f.b.now==15001 && f.r.set.closed());
  CHECK(budgets==std::vector<int>({3000,3000,3000,2000}));
  CHECK(f.b.count("mpTicket")==4 && f.b.count("netOpen")==5);
  CHECK(f.logs==std::vector<std::string>(4,"yougame: room ticket renewal failed: request timed out"));
  const auto requests=f.b.requests.size();
  CHECK(!reopen_room(f.r,f.io) && f.b.requests.size()==requests); // no request after expiry
}
static void ticket_renewal_validation() {
  for (const char* named : {"other-room","abcdef",""}) {
    Fixture f;
    CHECK(f.r.recovery.dropped(f.b.now,true));
    f.r.socket->stream.close();
    f.b.on_open=[&](int){f.b.closed();};
    CHECK(!reopen_room(f.r,f.io) && f.b.count("mpTicket")==0);
    f.b.on_ticket=[&](int)->json {return {{"ticket","ticket-new"},{"room",named}};};
    CHECK(!reopen_room(f.r,f.io) && f.b.count("mpTicket")==1);
    const bool mismatch=std::string(named)=="other-room";
    CHECK(f.r.ticket==(mismatch?"ticket-old":"ticket-new"));
    CHECK(f.b.requests.back()["ticket"]==(mismatch?"ticket-old":"ticket-new"));
    CHECK(f.logs==std::vector<std::string>({mismatch?"yougame: room ticket renewal failed: room mismatch":"yougame: room ticket renewed"}));
  }
  Fixture f;
  CHECK(f.r.recovery.dropped(f.b.now,true));
  f.r.socket->stream.close(); f.b.on_open=[&](int){f.b.closed();};
  CHECK(!reopen_room(f.r,f.io));
  f.b.on_ticket=[](int)->json {return {{"room","abcdef"}};};
  CHECK(!reopen_room(f.r,f.io) && f.r.ticket=="ticket-old");
  CHECK(f.logs.back()=="yougame: room ticket renewal failed: missing ticket");
  f.b.on_ticket=[](int)->json {throw std::runtime_error("untrusted response with a credential placeholder");};
  CHECK(!reopen_room(f.r,f.io) && f.r.ticket=="ticket-old");
  CHECK(f.logs.back()=="yougame: room ticket renewal failed: request failed");
  f.r.stop=true;
  const auto requests=f.b.requests.size();
  CHECK(!reopen_room(f.r,f.io) && f.b.requests.size()==requests);
}
static void internet_unchanged() {
  Fixture f;f.r.socket->broker_mode=false;f.r.pending_begin=true;
  f.b.push({{"t","start"},{"matchId","legacy"},{"host","b"},{"revision",99}});
  f.b.at(2,[&]{CHECK(f.r.pending_begin && f.r.host_connection=="a" && f.r.revision==7);f.b.push({{"t","result"},{"matchId","legacy"}});});
  f.b.at(3,[&]{CHECK(f.r.match_id=="legacy");f.r.pending_begin=false;f.r.pending_winner=0;f.r.match_id.clear();f.b.push({{"t","command-error"},{"action","begin"}});});
  f.b.at(4,[&]{CHECK(!f.r.pending_begin);f.b.push({{"t","lobby"},{"host","b"},{"revision",8}});});
  f.b.at(5,[&]{CHECK(f.r.host_connection=="b" && f.r.revision==8);f.r.in_game=true;f.b.push({{"t","command-error"},{"action","begin"}});});
  f.b.at(6,[&]{CHECK(f.r.pending_begin && f.r.begin_at==2005);f.tick();});f.stop_at(7);f.run();
  CHECK(std::all_of(f.b.waits.begin(),f.b.waits.end(),[](int ms){return ms==250;}));
  json leave={{"id","old-id"},{"player",{{"id","broker-id"}}}};
  CHECK(leave_id(leave,false)=="old-id" && leave_id(leave,true)=="broker-id");
}
static void flush_joins_open() {
  // stop_and_flush must wait for a pump blocked inside reopening the real Stream.
  Events events;std::mutex mutex,gate;std::condition_variable cv;
  bool opening=false,release=false;std::atomic<bool> flushing{false};std::vector<json> sent;
  struct Socket { Stream stream; Socket(Events& e,Request q):stream(e,std::move(q)){} bool send(const json& m){return stream.send(m);} };
  RoomState<Socket> r;r.ranked=true;r.set.configure(0,{"a","b"});r.set.start_game();r.set.receive({{"t","start"},{"matchId","set-1"}});r.set.quit();
  r.socket=std::make_unique<Socket>(events,[&](const json& q,int)->json {
    if(q["type"]=="netOpen") {
      std::unique_lock<std::mutex> lock(gate);opening=true;cv.notify_all();cv.wait(lock,[&]{return release;});
      events.push({{"conn",1},{"event","open"}});return {{"conn",1}};
    }
    if(q["type"]=="netSend") {CHECK(release);sent.push_back(json::parse(q["data"].get<std::string>()));}
    return json::object();
  });
  r.pump=std::thread([&]{CHECK(r.socket->stream.open("ticket-old","abcdef",1000));});
  {std::unique_lock<std::mutex> lock(gate);cv.wait(lock,[&]{return opening;});}
  auto flush=std::async(std::launch::async,[&]{flushing=true;stop_and_flush(r,mutex);});
  while(!flushing) std::this_thread::yield();
  CHECK(flush.wait_for(std::chrono::milliseconds(10))==std::future_status::timeout);
  {std::lock_guard<std::mutex> lock(gate);release=true;cv.notify_all();}
  flush.get();CHECK(!r.pump.joinable() && sent.size()==1 && sent[0]["t"]=="finish");
}
int main() {
  failed_send_is_loss();timeout_and_late_open();recovery_closes_and_expiry();lobby_start_links();
  departure_keeps_ranked_pump();missed_receipts();ticket_allowance();internet_unchanged();flush_joins_open();
  ticket_renewal_recovers();ticket_renewal_deadline();ticket_renewal_validation();
  std::puts("yougame_room: real pump send loss, late open, recovery expiry, lobby links, ranked drain, missed receipts, ticket budgets, internet compatibility and flush serialization passed");
}
