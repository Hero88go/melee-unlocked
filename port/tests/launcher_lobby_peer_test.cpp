// Two processes run this executable together; see lobby/test_peer_client.py.
#include <winsock2.h>
#include "../app/launcher_lobby_p2p.h"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc,char** argv) {
  if(argc!=5 && argc!=6 && argc!=7 && !(argc==3 && std::string(argv[1])=="inspect")) return 2;
  WSADATA ws{}; if(WSAStartup(MAKEWORD(2,2),&ws)) return 2;
  try {
    if(argc==3) {
      launcher::lobby::PeerLobby peer(argv[2],"127.0.0.1:9");
      std::cout<<peer.state().dump()<<"\n";
      WSACleanup(); return 0;
    }
    if(argc==6 && std::string(argv[1])=="resume") {
      const bool alpha=std::string(argv[5])=="alpha";
      launcher::lobby::PeerLobby peer(argv[2],argv[3],std::stoi(argv[4]));
      auto began=std::chrono::steady_clock::now();
      bool observed=false; auto observed_at=began;
      while(std::chrono::steady_clock::now()-began<std::chrono::seconds(15)) {
        peer.presence(alpha?nlohmann::json{{"status","Online"},{"stocks",nlohmann::json::array()}}:
                            nlohmann::json{{"status","In match"},{"stocks",{3}}});
        peer.tick(); auto state=peer.state();
        if(state["players"].empty() && state["friends"].size()==1 &&
           state["friends"][0].value("status",std::string())==(alpha?"In match":"Online") &&
           state["friends"][0].value("stocks",nlohmann::json::array())==
             (alpha?nlohmann::json::array({3}):nlohmann::json::array())) {
          if(!observed) { observed=true; observed_at=std::chrono::steady_clock::now(); }
          if(std::chrono::steady_clock::now()-observed_at>std::chrono::seconds(1)) {
            std::cout<<"PASS friends reconnect outside public lobby after restart\n";
            WSACleanup(); return 0;
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
      }
      std::cerr<<"Friend presence did not resume: "<<peer.state().dump()<<"\n";
      WSACleanup(); return 1;
    }
    const bool alpha=std::string(argv[1])=="alpha";
    launcher::lobby::PeerLobby peer(argv[2],argv[3],std::stoi(argv[4]));
    const int timeout=argc>=6?std::stoi(argv[5]):30;
    const bool outside=argc==7 && std::string(argv[6])=="outside";
    // mismatch: the two are on different Game Builds; the sender is told which.
    // clash: both send a request at once; each receiver answers with why it cannot take it.
    const bool mismatch=argc==7 && std::string(argv[6])=="mismatch";
    const bool clash=argc==7 && std::string(argv[6])=="clash";
    nlohmann::json profile={{"name",alpha?"Alpha":"Beta"},{"code",alpha?"TEST#101":"TEST#102"},
      {"location",alpha?"Phoenix":""},{"mains",alpha?nlohmann::json::array({2}):nlohmann::json::array({2,20,9})},
      {"build",mismatch&&!alpha?"peer-smoke:source":"peer-smoke:recomp"},{"ready",true}};
    peer.join(profile);
    if(mismatch || clash) {
      bool requested=false;
      auto began=std::chrono::steady_clock::now();
      while(std::chrono::steady_clock::now()-began<std::chrono::seconds(timeout)) {
        peer.presence(nlohmann::json{{"status","Online"},{"stocks",nlohmann::json::array()}});
        peer.tick(); auto state=peer.state();
        if(!requested && !state["players"].empty() && (clash || alpha)) {
          if(clash) std::this_thread::sleep_for(std::chrono::milliseconds(300));   // both have seen each other
          try { peer.command("request",{{"target",state["players"][0]["id"]}}); }
          catch(const std::exception& ex) {
            std::string what=ex.what();
            if(mismatch && what.find("Static Recomp")!=std::string::npos && what.find("Source Port")!=std::string::npos) {
              std::cout<<"PASS mismatch named: "<<what<<"\n"; WSACleanup(); return 0;
            }
            if(clash && what.find("pending")!=std::string::npos) { requested=true; continue; }   // the other's request arrived first
            std::cerr<<"unexpected: "<<what<<"\n"; WSACleanup(); return 1;
          }
          requested=true;
        }
        const auto notice=state.value("notice",std::string());
        if(clash && notice.find("not delivered")!=std::string::npos) {
          std::cout<<"PASS clash answered: "<<notice<<"\n"; WSACleanup(); return 0;
        }
        if(clash && requested && !state["requests"].empty() &&
           state["requests"][0].value("from",std::string())!=peer.id()) {
          std::cout<<"PASS clash: the other request arrived first and is pending here\n"; WSACleanup(); return 0;
        }
        if(mismatch && !alpha && std::chrono::steady_clock::now()-began>std::chrono::seconds(6)) { WSACleanup(); return 0; }
        if(clash && requested && !state["requests"].empty() && state["requests"][0].value("from",std::string())==peer.id() &&
           state["requests"][0].value("state",std::string())=="pending" && std::chrono::steady_clock::now()-began>std::chrono::seconds(4)) {
          std::cout<<"PASS clash: own request stands for the other player to accept"<<std::endl; WSACleanup(); return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
      }
      std::cerr<<"no answer: "<<peer.state().dump()<<"\n"; WSACleanup(); return 1;
    }
    bool sent=false,replied=false,friend_accepted=false,match_accepted=false,finished=false,left=false,measured=false;
    auto began=std::chrono::steady_clock::now(),completed=began;
    auto elapsed=[&] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count(); };
    while(std::chrono::steady_clock::now()-began<std::chrono::seconds(timeout)) {
      peer.presence(match_accepted?nlohmann::json{{"status","In match"},{"stocks",{3}}}:
                                   nlohmann::json{{"status","Online"},{"stocks",nlohmann::json::array()}});
      peer.tick(); auto state=peer.state(); measured=measured || !peer.pings().empty();
      if(alpha && !sent && !state["players"].empty()) {
        auto target=state["players"][0]["id"].get<std::string>();
        peer.command("chat",{{"text","Hello from Alpha"}});
        peer.command("friend",{{"target",target}});
        peer.command("request",{{"target",target}});
        sent=true; std::cout<<"Alpha discovered peer at "<<elapsed()<<" ms\n"<<std::flush;
      }
      if(!alpha) {
        for(const auto& message:state["messages"]) if(message.value("text",std::string())=="Hello from Alpha" && !replied) {
          peer.command("chat",{{"text","Hello from Beta"}}); replied=true;
        }
        if(!state["friend_requests"].empty() && !friend_accepted) {
          peer.command("friend_accept",{{"target",state["friend_requests"][0]["id"]}}); friend_accepted=true;
          std::cout<<"Beta accepted friend at "<<elapsed()<<" ms\n"<<std::flush;
        }
        if(!state["requests"].empty() && !match_accepted && state["requests"][0].value("state",std::string())=="pending") {
          peer.command("accept",{{"request",state["requests"][0]["id"]}}); match_accepted=true;
          std::cout<<"Beta accepted match at "<<elapsed()<<" ms\n"<<std::flush;
        }
      }
      if(alpha && sent && (!outside || state["players"].empty()) && !state["friends"].empty() && !state["requests"].empty() &&
         state["requests"][0].value("state",std::string())=="accepted" &&
         state["friends"][0].value("status",std::string())=="In match" &&
         state["friends"][0]["stocks"]==nlohmann::json::array({3}) && measured) {
        for(const auto& message:state["messages"]) if(message.value("text",std::string())=="Hello from Beta") {
          std::cout<<"PASS peer chat, friendship, request, match, stocks, RTT at "<<elapsed()<<" ms\n"<<std::flush; finished=true; break;
        }
        if(finished) break;
      }
      if(!alpha && replied && friend_accepted && match_accepted && !state["requests"].empty() &&
         state["requests"][0].value("confirmed",false)) {
        if(!finished) { finished=true; completed=std::chrono::steady_clock::now(); }
        if(outside && !left && std::chrono::steady_clock::now()-completed>std::chrono::seconds(1)) {
          peer.command("leave"); left=true;
          std::cout<<"Beta left public lobby at "<<elapsed()<<" ms\n"<<std::flush;
        }
        if(std::chrono::steady_clock::now()-completed>std::chrono::seconds(outside?8:4)) {
          std::cout<<"PASS peer receiving chat, friend and match acceptance at "<<elapsed()<<" ms\n"<<std::flush; break;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    WSACleanup();
    if(!finished) { std::cerr<<"Peer lobby exchange incomplete: "<<peer.state().dump()<<"\n"; return 1; }
    return 0;
  } catch(const std::exception& ex) { std::cerr<<ex.what()<<"\n"; WSACleanup(); return 1; }
}
