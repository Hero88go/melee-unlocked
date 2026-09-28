// Real WinHTTP + UDP test against a local test service (started by lobby/test_client.py).
#include "../app/launcher_lobby.cpp"
#include <iostream>

int main(int argc, char** argv) {
  using namespace launcher::lobby;
  if (argc != 2) return 2;
  try {
    bool rejected = false;
    try { endpoint("http://example.com"); } catch (...) { rejected = true; }
    if (!rejected) throw std::runtime_error("Plain HTTP accepted outside loopback");
    Json a={{"url",argv[1]},{"name","Alpha"},{"code","TEST#101"},{"location","Phoenix"},
            {"mains",Json::array({2,20,9})},{"build","test:recomp"}};
    Json b=a; b["name"]="Beta"; b["code"]="TEST#102";
    auto ia=api(a,"join",a), ib=api(b,"join",b);
    a["token"]=ia["token"]; b["token"]=ib["token"]; a["id"]=ia["id"]; b["id"]=ib["id"];
    a["udp_token"]=ia["udp_token"]; b["udp_token"]=ib["udp_token"];
    auto request=api(a,"request",{{"target",ib["id"]}});
    WSADATA ws{}; WSAStartup(MAKEWORD(2,2),&ws);
    {
      Probe pa,pb;
      for(int i=0;i<30;++i) {
        pa.poll(a,api(a,"poll",Json::object()),ia["probe"].get<std::string>());
        pb.poll(b,api(b,"poll",Json::object()),ib["probe"].get<std::string>());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      { std::lock_guard<std::mutex> lock(mutex); if(pings.empty()) throw std::runtime_error("No direct UDP RTT measured"); }
    }
    WSACleanup();
    api(a,"chat",{{"text","Hello lobby"}});
    if(api(b,"poll",Json::object())["messages"].empty()) throw std::runtime_error("Chat missing");
    api(a,"friend",{{"target",ib["id"]}});
    api(b,"friend_accept",{{"target",ia["id"]}});
    api(b,"accept",{{"request",request["id"]}});
    if(api(a,"poll",Json::object())["requests"][0]["state"]!="accepted") throw std::runtime_error("Match acceptance missing");
    api(a,"presence",{{"status","In match"},{"stocks",Json::array({3})}});
    auto friend_status=api(b,"poll",Json::object())["friends"][0];
    if(friend_status["status"]!="In match" || friend_status["stocks"][0]!=3) throw std::runtime_error("Stocks missing");
    // Exercise the actual background worker and one-shot launcher dispatch, not just the API.
    set_account("Alpha","TEST#101");
    { std::lock_guard<std::mutex> lock(mutex); config=a; go_online_requested=true; }
    init(nullptr, (std::filesystem::temp_directory_path() / ("mu-lobby-test-"+std::to_string(GetCurrentProcessId()))).u8string());
    HWND ui=CreateWindowExW(0,L"MeleeUnlockedLobby",L"Lobby test",WS_OVERLAPPEDWINDOW,
                           0,0,790,725,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    enqueue("presence");
    Match match; bool delivered=false;
    for(int i=0;i<300 && !delivered;++i) { delivered=take_match(match); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    Match duplicate;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    bool repeated=take_match(duplicate);
    SendMessageW(ui,WM_TIMER,1,0);
    bool controls=ui && GetDlgItem(ui,CHAT) && GetDlgItem(ui,CHARACTER_FIRST+25) &&
        SendMessageW(GetDlgItem(ui,FRIENDS),LB_GETCOUNT,0,0)==1 &&
        SendMessageW(GetDlgItem(ui,PLAYERS),LB_GETCOUNT,0,0)==2;
    shutdown();
    if(!delivered || repeated || !controls || match.code!="TEST#102" || match.character!=2 || match.build!="test:recomp")
      throw std::runtime_error("Worker did not deliver exactly one validated launch: delivered="+std::to_string(delivered)+" repeated="+std::to_string(repeated)+" controls="+std::to_string(controls)+" code="+match.code+" build="+match.build);
    api(b,"offline",Json::object());
    std::cout << "PASS: WinHTTP, peer UDP ping, chat, friends, acceptance, live stocks\n";
    return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
