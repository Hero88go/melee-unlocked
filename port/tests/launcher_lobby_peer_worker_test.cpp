// Exercise the real launcher worker and one-shot handoff without a lobby service.
#include "../app/launcher_lobby.cpp"
#include <iostream>

int main(int argc,char** argv) {
  if(argc!=4) return 2;
  using namespace launcher::lobby;
  try {
    init(nullptr,argv[1]);
    Json cfg={{"mode","peer"},{"url",argv[2]},{"peer_port",std::stoi(argv[3])},
      {"name","Alpha"},{"code","TEST#101"},{"location","Phoenix"},
      {"mains",Json::array({2,20,9})},{"build","peer-smoke:recomp"},{"ready",true}};
    enqueue("join",cfg);
    bool sent=false,received=false,chat=false,friendship=false,stocks=false;
    Match match;
    auto began=std::chrono::steady_clock::now();
    while(std::chrono::steady_clock::now()-began<std::chrono::seconds(30)) {
      Json state; std::string failure;
      { std::lock_guard<std::mutex> lock(mutex); state=snapshot; failure=notice; }
      if(!sent && state.value("players",Json::array()).size()) {
        auto target=state["players"][0]["id"].get<std::string>();
        enqueue("chat",{{"text","Hello from Alpha"}});
        enqueue("friend",{{"target",target}});
        enqueue("request",{{"target",target}});
        sent=true;
      }
      if(take_match(match)) received=true;
      for(const auto& message:state.value("messages",Json::array()))
        if(message.value("text",std::string())=="Hello from Beta") chat=true;
      for(const auto& friend_entry:state.value("friends",Json::array())) {
        friendship=true;
        if(friend_entry.value("status",std::string())=="In match" &&
           friend_entry.value("stocks",Json::array())==Json::array({3})) stocks=true;
      }
      if(received && chat && friendship && stocks) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    Match duplicate; bool repeated=take_match(duplicate);
    shutdown();
    // The game appends one result per game. Verify multiple games in one process
    // and that an unreported exit stays outside the W/L totals.
    game_running(true);
    const auto results_path=std::filesystem::u8path(std::string(argv[1])+"/lobby-game-status.json.results");
    { std::ofstream result(results_path,std::ios::app);
      result<<"{\"result\":\"win\",\"winner\":0,\"end_method\":2}\n"; }
    consume_results();
    bool saved_win=history.size()==1 && history[0].value("result",std::string())=="win" &&
                   history[0].value("opponent",std::string())=="Beta";
    { std::ifstream file(std::filesystem::u8path(std::string(argv[1])+"/lobby-history.json"));
      const auto persisted=Json::parse(file); saved_win=saved_win && persisted.size()==1 &&
          persisted[0].value("id",std::string())==match.id+"/1"; }
    { std::ofstream result(results_path,std::ios::app);
      result<<"{\"result\":\"loss\",\"winner\":1,\"end_method\":2}\n"; }
    consume_results();
    bool saved_loss=history.size()==2 && history[0].value("result",std::string())=="loss" && result_events==2;
    game_running(false);
    saved_loss=saved_loss && history.size()==2;
    pending_match=Match{"unreported","TEST#102",2,"peer-smoke:recomp","Beta"};
    game_running(true);
    game_running(false);
    bool incomplete=history.size()==3 && history[0].value("result",std::string())=="incomplete";
    HWND parent=CreateWindowExW(0,L"STATIC",L"test parent",WS_OVERLAPPEDWINDOW,
                                0,0,980,860,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    owner=parent; go_online_requested=false; joined=false; commands.clear();
    set_account("Alpha","TEST#101");
    open("peer-smoke:recomp",true);
    HWND checkbox=GetDlgItem(window,GO_ONLINE);
    wchar_t hint[256]{}; GetWindowTextW(GetDlgItem(window,ONLINE_HINT),hint,256);
    bool embedded=parent && window && GetParent(window)==parent &&
                  (GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD) && checkbox &&
                  !go_online_requested &&
                  std::wstring(hint).find(L"match requests while the launcher is open")!=std::wstring::npos &&
                  commands.empty();
    label(NAME,"Alpha"); label(CODE,"TEST#101");
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(GO_ONLINE,BN_CLICKED),(LPARAM)checkbox);
    embedded=embedded && go_online_requested && !commands.empty() && commands.back().action=="join";

    SendMessageW(window,WM_COMMAND,MAKEWPARAM(GO_ONLINE,BN_CLICKED),(LPARAM)checkbox);
    embedded=embedded && !go_online_requested && commands.back().action=="leave";
    wchar_t request_label[64]{}; GetWindowTextW(GetDlgItem(window,REQUEST),request_label,64);
    embedded=embedded && std::wstring(request_label)==L"Send Match Request";
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(TAB_FRIENDS,BN_CLICKED),0);
    embedded=embedded && lobby_tab==1 && (GetWindowLongPtrW(GetDlgItem(window,FRIENDS),GWL_STYLE)&WS_VISIBLE) && !(GetWindowLongPtrW(GetDlgItem(window,CHAT),GWL_STYLE)&WS_VISIBLE);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(TAB_PROFILE,BN_CLICKED),0);
    embedded=embedded && lobby_tab==3 && (GetWindowLongPtrW(GetDlgItem(window,NAME),GWL_STYLE)&WS_VISIBLE);
    const auto shown=[&](int id){ return (GetWindowLongPtrW(GetDlgItem(window,id),GWL_STYLE)&WS_VISIBLE)!=0; };
    embedded=embedded && (GetWindowLongPtrW(GetDlgItem(window,CODE),GWL_STYLE)&ES_READONLY);
    label(CODE,"FORGED#999"); embedded=embedded && profile_config()["code"]=="TEST#101";
    label(CODE,account_code);
    selected_mains={2,20,9};
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(CHARACTER_FIRST+2,BN_CLICKED),0);
    embedded=embedded && selected_mains.size()==2;
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(CHARACTER_FIRST+3,BN_CLICKED),0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(CHARACTER_FIRST+4,BN_CLICKED),0);
    embedded=embedded && selected_mains.size()==3 && std::find(selected_mains.begin(),selected_mains.end(),4)==selected_mains.end();
    joined=true; snapshot["friends"]=Json::array({{{"id","friend"},{"name","Beta"},{"status","Online"}}});
    snapshot["messages"]=Json::array({{{"name","Beta"},{"text","hello"}}});
    for(int repeat=0;repeat<3;++repeat) for(int tab:{0,1,2,3}) {
      SendMessageW(window,WM_COMMAND,MAKEWPARAM(TAB_CHAT+tab,BN_CLICKED),0);
      // A changing hidden list must not become visible when WM_SETREDRAW is restored.
      snapshot["friends"][0]["status"]=repeat%2?"Online":"In match";
      for(int tick=0;tick<3;++tick) refresh();
      embedded=embedded && lobby_tab==tab && shown(CHAT)==(tab==0) && shown(FRIENDS)==(tab==1) && shown(HISTORY)==(tab==2) && shown(NAME)==(tab==3) && shown(TAB_CHAT)==(tab!=0);
    }
    embedded=embedded && !rows.empty() && rows[0].value("is_self",false);
    SendMessageW(GetDlgItem(window,PLAYERS),LB_SETCURSEL,0,0); refresh();
    embedded=embedded && !IsWindowEnabled(GetDlgItem(window,REQUEST)) && IsWindowEnabled(GetDlgItem(window,ADD_FRIEND));
    label(CHAT,"Hello \xF0\x9F\x91\x8B");
    commands.clear(); SendMessageW(GetDlgItem(window,CHAT),WM_KEYDOWN,VK_RETURN,0);
    embedded=embedded && !commands.empty() && commands.back().action=="chat" && commands.back().data["text"]=="Hello \xF0\x9F\x91\x8B" && text(CHAT).empty();
    commands.clear(); SendMessageW(window,WM_COMMAND,MAKEWPARAM(ADD_FRIEND,BN_CLICKED),0);
    embedded=embedded && commands.empty() && adding_friend && lobby_tab==1 && shown(FRIEND_CODE);
    auto_reject=true;
    Json invite={{"players",Json::array({{{"id","friend"},{"name","Beta"},{"location","Seattle"},{"mains",Json::array({2})}}})},
                 {"requests",Json::array({{{"id","pending-test"},{"from","friend"},{"to",self},{"state","pending"}}})}};
    process_invites(invite,{},self,false);
    embedded=embedded && prompted_requests.count("pending-test") && !commands.empty() &&
             commands.back().action=="cancel" && commands.back().data.value("request",std::string())=="pending-test";
    auto_reject=false;
    if(window) DestroyWindow(window); window=nullptr;
    if(parent) DestroyWindow(parent);
    if(!received || repeated || !chat || !friendship || !stocks ||
       match.code!="TEST#102" || match.character!=2 || match.build!="peer-smoke:recomp" ||
       !saved_win || !incomplete || !saved_loss || !embedded) {
      std::cerr<<"Peer worker exchange incomplete: sent="<<sent<<" received="<<received
               <<" chat="<<chat<<" friend="<<friendship<<" stocks="<<stocks
               <<" saved_win="<<saved_win<<" incomplete="<<incomplete<<" saved_loss="<<saved_loss<<" embedded="<<embedded<<" history="<<history.dump()
               <<" match_opponent="<<match.opponent<<" notice="<<notice<<"\n"; return 1;
    }
    std::cout<<"PASS launcher peer worker and one-shot Slippi Direct handoff\n";
    return 0;
  } catch(const std::exception& ex) { std::cerr<<ex.what()<<"\n"; shutdown(); return 1; }
}
