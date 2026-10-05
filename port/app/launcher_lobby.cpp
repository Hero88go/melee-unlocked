// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <winhttp.h>
#include <commctrl.h>
#include <commdlg.h>
#include <nlohmann/json.hpp>
#include "launcher_lobby.h"
#include "launcher_lobby_p2p.h"
#include "launcher_theme.h"
#include "launcher_lang.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mmsystem.h>
#include <array>
#include <cmath>
#include <ctime>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

namespace launcher::lobby {
void refresh_theme();
namespace {
using Json = nlohmann::json;
const char* characters[] = {"Captain Falcon", "Donkey Kong", "Fox", "Mr. Game & Watch", "Kirby",
  "Bowser", "Link", "Luigi", "Mario", "Marth", "Mewtwo", "Ness", "Peach", "Pikachu",
  "Ice Climbers", "Jigglypuff", "Samus", "Yoshi", "Zelda", "Sheik", "Falco", "Young Link",
  "Dr. Mario", "Roy", "Pichu", "Ganondorf"};
enum { URL=500, NAME, CODE, LOCATION, MAIN1, MAIN2, MAIN3, JOIN, LEAVE, PLAYERS, REQUEST,
       ADD_FRIEND, REQUESTS, ACCEPT, DECLINE, CHATLOG, CHAT, SEND, FRIENDS, FRIEND_ACCEPT,
       FRIEND_DECLINE, REMOVE_FRIEND, STATUS, MODE, URL_LABEL, HISTORY, RECORD, GO_ONLINE, ONLINE_HINT,
       TAB_CHAT, TAB_FRIENDS, TAB_HISTORY, TAB_PROFILE, SAVE_PROFILE, PROFILE_NAME, PROFILE_CODE,
       PROFILE_LOCATION, PROFILE_MAINS, PROFILE_MODE, PLAYER_HEADING, EMPTY_PLAYERS, EMPTY_FRIENDS, EMPTY_CHAT, ADVANCED, FRIEND_CODE, FRIEND_SEND, FRIEND_HINT, EMOJI, AUTO_REJECT, REQUEST_SOUND, VOLUME_LABEL, OPEN_TO,
       PLAYER_FILTER, INVITE_FRIEND, COPY_CODE, PM_PLAYER, PM_FRIEND,
       // Peer-to-peer matches (the build without the Slippi layer; never created otherwise).
       INVITE_EDIT, INVITE_CONNECT, INVITE_COPY, FIND_MATCH, BLOCK_PLAYER, P2P_FIGHTER,
       // P2P Direct as a test control in the launcher that also starts the other kind of match (never
       // created in the build above): the request button and its own character button, both in the
       // page header, which launcher_lobby_ui.inl's layout() leaves alone (layout_all places them).
       P2P_REQUEST, P2P_FIGHTER_TEST };
HWND owner{}, window{};
std::string directory, build;
std::string account_name, account_code;
// The launcher with both kinds of match: the code of the account its hosted matches need, "" while
// signed out. account_code is then the code made from the name and the identity key, so the player
// can still go online and play peer to peer (announced_code).
std::string hosted_code;
// Peer-to-peer matches: there is no account. The name is the one typed on the Profile page and the
// code is made from it and this identity key (lobby-peer-identity.json), read once in init().
std::string identity;
std::string invite_selected;                    // the last answered invite whose player was selected in the list
std::vector<int> selected_mains{2,20,9};
// Peer-to-peer matches: the character and color they are played with ("p2p_ch" and "p2p_col" in
// lobby-profile.json). -1 is the first main, which is also what a profile without the keys means.
int p2p_character=-1, p2p_color=0;
const int CHARACTER_FIRST=700;
bool can_play=false;
std::mutex mutex;
std::condition_variable wake;
std::thread worker;
bool stopping=false, running=false, joined=false;
bool go_online_requested=false;
// A peer-to-peer match on the lobby's UDP port (stop_for_match): hold_wanted is the window thread
// asking, held is the worker saying its socket is closed. Guarded by `mutex`.
bool hold_wanted=false, held=false;
std::condition_variable hold_changed;
bool test_match_taken=false;                    // test runs: one match per run
std::filesystem::file_time_type game_started{};
Json config=Json::object(), snapshot=Json::object();
std::string notice="Ready when you are. Go online to meet other players.", self;
struct Command { std::string action; Json data; };
std::deque<Command> commands;
std::deque<Match> matches;
std::optional<Match> pending_match;
bool pending_started=false;
bool result_mailbox_ready=false;
std::uintmax_t result_offset=0;
int result_events=0;
Json history=Json::array(), displayed_history;
std::set<std::string> launched;
std::map<std::string, int> pings, displayed_pings;
Json rows=Json::array(), requests=Json::array(), friends=Json::array();
Json chat_messages=Json::array();
HWND lobby_tips{};
std::wstring hover_code;
int lobby_tab=0;
bool adding_friend=false;
std::set<std::string> prompted_requests;
std::map<std::string,ULONGLONG> incoming_since;
std::string displayed_status;
bool sound_enabled=true, auto_reject=false;
int sound_volume=65;
int slider_left=40, slider_right=390, slider_y=582;
bool slider_drag=false;
// Mods and the versions a player takes requests for. The Mods folder scan says which mods this PC
// has; the player picks what they are open to. Only mod names, versions and a short content hash of
// a custom ISO leave this PC, never a path.
Json mod_has=Json::object();
std::map<std::string,std::string> mod_paths;   // "akaneia" / "ace" -> that mod's disc on this PC
std::string mods_seen;                          // the scan file's contents as last read
bool custom_launch_ready = false;
Prefs current_prefs;
std::string custom_hash;                        // of current_prefs.custom_iso, 16 hex digits
std::function<void()> prefs_changed;
std::string setup_hint_text;
// The Players list shows everyone, or only the players who take requests for one version: "all",
// "vanilla", "akaneia", "ace" or "custom". For this session; the player's own card always shows.
std::string roster_filter="all";
const char* const roster_filters[5]={"all","vanilla","akaneia","ace","custom"};   // the Show menu's order
Json roster_all=Json::array();                  // everyone online, whatever the filter shows
// "Can't reach X. Use Slippi Direct with their code": the code, while that line is the notice (the
// Copy code button next to it). Guarded by `mutex`; copy_button_code is the window thread's copy.
std::string copy_code, copy_code_notice, copy_button_code;
// Private chat, as the window thread sees it (the rooms themselves live in launcher_lobby_p2p.cpp).
Json private_rooms=Json::array();               // open and closed rooms, oldest first: one tab each
std::string active_room;                        // the room in front on the Chat page; "" is the public lobby chat
std::map<std::string,std::string> room_seen;    // room -> the last message the player has had in front of them
std::set<std::string> rooms_known;              // rooms that already have a tab
std::set<std::string> rooms_closing;            // closed here, until the lobby worker has dropped them
std::set<std::string> private_prompted;         // incoming requests with a banner up
std::vector<RECT> room_tab_hits, room_close_hits;   // the tabs as last painted, for the mouse
std::vector<std::string> room_tab_ids;
bool chat_switched=false;                       // another room came to the front: refill the message list
// The hidden test launcher (MELEE_LAUNCHER_TEST) never takes the foreground, flashes or chimes.
const bool test_mode=std::getenv("MELEE_LAUNCHER_TEST")!=nullptr;
void layout();
std::wstring wide(const std::string& s) {
  int n=MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n); return w;
}
std::string utf8(const std::wstring& s) {
  int n=WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
  std::string a(n, 0); WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), a.data(), n, nullptr, nullptr); return a;
}
std::string text(int id) {
  HWND h=GetDlgItem(window,id); std::wstring s(GetWindowTextLengthW(h)+1,0);
  s.resize(GetWindowTextW(h,s.data(),(int)s.size())); return utf8(s);
}
void label(int id,const std::string& english) {
  const std::string s=launcher::lang::tx(english);
  if(text(id)!=s) SetWindowTextW(GetDlgItem(window,id),wide(s).c_str());
}
void enqueue(std::string action,Json data=Json::object()) {
  { std::lock_guard<std::mutex> lock(mutex); if(commands.size()<32) commands.push_back({action,data}); }
  wake.notify_one();
}
// The versions the player is open to, from the saved choice ("vanilla,akaneia"). Never empty.
std::vector<std::string> open_list() {
  std::vector<std::string> out;
  const std::string& s=current_prefs.open_to;
  for(size_t at=0;at<=s.size();) {
    size_t comma=s.find(',',at); if(comma==std::string::npos) comma=s.size();
    std::string item=s.substr(at,comma-at);
    item.erase(std::remove_if(item.begin(),item.end(),[](unsigned char c){ return std::isspace(c)!=0; }),item.end());
    if((item=="vanilla"||item=="akaneia"||item=="ace"||item=="custom") && std::find(out.begin(),out.end(),item)==out.end()) out.push_back(item);
    at=comma+1;
  }
  if(out.empty()) out.push_back("vanilla");
  return out;
}
std::string join_list(const std::vector<std::string>& items) {
  std::string s; for(const auto& item:items) { if(!s.empty()) s+=","; s+=item; } return s;
}
// A version as the player reads it: "Vanilla", "Akaneia 1.0.1", "ACE", or the custom ISO's name.
std::string mode_title(const std::string& mode,const Json& mine,const Json& them) {
  if(mode=="vanilla") return launcher::lang::tx("Vanilla");
  if(mode=="akaneia" || mode=="ace") {
    std::string title=mode=="akaneia"?"Akaneia":"ACE";
    for(const Json* p:{&mine,&them}) {
      const Json has=p->value("has",Json::object());
      if(has.is_object() && has.count(mode) && has[mode].is_string() && !has[mode].get<std::string>().empty())
        return title+" "+has[mode].get<std::string>();
    }
    return title;
  }
  if(mode.rfind("custom:",0)==0) {
    const Json iso=mine.value("iso",Json::object());
    if(iso.is_object() && iso.value("h",std::string())==mode.substr(7)) return iso.value("n",launcher::lang::tx("Custom ISO"));
    return launcher::lang::tx("Custom ISO");
  }
  return mode;
}
// A short list for the Open to button: "Vanilla, Akaneia".
std::string open_summary() {
  std::string s;
  for(const auto& item:open_list()) {
    if(!s.empty()) s+=", ";
    s+=item=="vanilla"?launcher::lang::tx("Vanilla"):item=="akaneia"?"Akaneia":item=="ace"?"ACE":
       (current_prefs.custom_name.empty()?launcher::lang::tx("Custom ISO"):current_prefs.custom_name);
  }
  return s;
}
Json profile_config();
bool peer_mode(const Json& cfg);
// The code this player is announced under. The launcher where every match is peer to peer makes it
// in profile_config. The other one uses the account's code, and without an account the code made
// from the name and the identity key: enough to go online and to play peer to peer.
std::string announced_code() {
  if(p2p_matches) return account_code;
  if(!hosted_code.empty() || !p2p_test_shown()) return hosted_code;
  std::string name=window?text(NAME):std::string();
  if(name.empty()) { std::lock_guard<std::mutex> lock(mutex); name=config.value("name",std::string()); }
  return identity.empty() || name.empty()?std::string():derived_code(name,identity);
}
// profile_config with that code, and whether a hosted match can be started here ("hosted_ok": read
// by the peer lobby with the match character and color, never sent with the profile).
Json own_profile() {
  if(!p2p_matches) {
    account_code=announced_code();
    if(window) label(CODE,account_code);
  }
  Json cfg=profile_config();
  if(!p2p_matches) cfg["hosted_ok"]=!hosted_code.empty();
  return cfg;
}
// Re-announce the profile (Game Build, ready, mods, open to) to other players at once, public lobby
// or not. The hosted service has no profile action; it gets the change at the next join.
void announce() {
  Json cfg=own_profile();
  bool peer;
  { std::lock_guard<std::mutex> lock(mutex); peer=peer_mode(config); cfg["mode"]=config.value("mode",std::string("peer")); config=cfg; }
  if(peer && !cfg.value("name",std::string()).empty() && !cfg.value("code",std::string()).empty()) enqueue("profile",cfg);
}
// A custom ISO's identity for "Same ISO" matching: 64-bit FNV-1a over the size, the first 4 MiB and
// a 64 KiB sample every 64 MiB. Not a security check (the match itself is Slippi Direct), only a way
// to tell two copies of the same disc from two different discs under the same name.
std::string content_hash16(const std::string& path) {
  std::ifstream in(std::filesystem::u8path(path),std::ios::binary);
  if(!in) return {};
  in.seekg(0,std::ios::end); const auto size=(unsigned long long)in.tellg(); in.seekg(0);
  unsigned long long h=1469598103934665603ull;
  auto mix=[&](const unsigned char* p,size_t n){ for(size_t i=0;i<n;++i) { h^=p[i]; h*=1099511628211ull; } };
  mix((const unsigned char*)&size,sizeof size);
  std::vector<unsigned char> block(4u<<20);
  in.read((char*)block.data(),(std::streamsize)block.size()); mix(block.data(),(size_t)in.gcount());
  block.resize(64u<<10);
  for(unsigned long long at=64ull<<20;at<size;at+=64ull<<20) {
    in.clear(); in.seekg((std::streamoff)at); in.read((char*)block.data(),(std::streamsize)block.size());
    mix(block.data(),(size_t)in.gcount());
  }
  char out[17]{}; std::snprintf(out,sizeof out,"%016llx",h); return out;
}
struct Internet {
  HINTERNET h{}; ~Internet(){ if(h) WinHttpCloseHandle(h); }
  operator HINTERNET() const { return h; }
};
struct Endpoint { std::wstring host; INTERNET_PORT port{}; bool secure{}; };
Endpoint endpoint(const std::string& url) {
  const auto w=wide(url); URL_COMPONENTS c{}; c.dwStructSize=sizeof c;
  c.dwHostNameLength=c.dwUrlPathLength=c.dwExtraInfoLength=(DWORD)-1;
  if(!WinHttpCrackUrl(w.c_str(),0,0,&c) || c.dwExtraInfoLength || (c.dwUrlPathLength && std::wstring(c.lpszUrlPath,c.dwUrlPathLength)!=L"/"))
    throw std::runtime_error("Use a service origin such as https://lobby.example.com");
  Endpoint e{std::wstring(c.lpszHostName,c.dwHostNameLength),c.nPort,c.nScheme==INTERNET_SCHEME_HTTPS};
  if(!e.secure && !(c.nScheme==INTERNET_SCHEME_HTTP && (e.host==L"127.0.0.1" || e.host==L"localhost")))
    throw std::runtime_error("Internet lobby connections require HTTPS.");
  return e;
}
Json api(const Json& cfg,const std::string& action,const Json& data) {
  auto e=endpoint(cfg.value("url",std::string()));
  Internet session{WinHttpOpen(L"MeleeUnlockedLobby/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0)};
  if(!session.h) throw std::runtime_error("Cannot open network session");
  WinHttpSetTimeouts(session,2500,2500,2500,2500);
  Internet connection{WinHttpConnect(session,e.host.c_str(),e.port,0)};
  Internet request{WinHttpOpenRequest(connection,L"POST",wide("/v1/"+action).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,e.secure?WINHTTP_FLAG_SECURE:0)};
  DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof redirect);
  std::wstring headers=L"Content-Type: application/json\r\n";
  const std::string token=cfg.value("token",std::string());
  if(!token.empty()) headers+=L"Authorization: Bearer "+wide(token)+L"\r\n";
  std::string body=data.dump();
  if(!WinHttpSendRequest(request,headers.c_str(),(DWORD)-1,body.data(),(DWORD)body.size(),(DWORD)body.size(),0) || !WinHttpReceiveResponse(request,nullptr))
    throw std::runtime_error("Lobby unavailable. Retrying; presence expires automatically.");
  DWORD status=0,size=sizeof status;
  WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr);
  std::string response; char buffer[8192]; DWORD got=0;
  do {
    if(!WinHttpReadData(request,buffer,sizeof buffer,&got)) throw std::runtime_error("Lobby response interrupted");
    response.append(buffer,got);
    if(response.size()>512*1024) throw std::runtime_error("Lobby response too large");
  } while(got);
  auto result=Json::parse(response);
  if(status!=200) throw std::runtime_error(result.value("error",std::string("Lobby request failed")));
  return result;
}
void save(const Json& cfg) {
  const auto path=std::filesystem::u8path(directory+"/lobby-profile.json");
  std::ofstream f(path); f<<cfg.dump(2);
}
void save_history() {
  const auto path=std::filesystem::u8path(directory+"/lobby-history.json");
  const auto temp=std::filesystem::u8path(directory+"/lobby-history.json.tmp");
  { std::ofstream f(temp); if(!f) throw std::runtime_error("Could not save match history"); f<<history.dump(2); }
  if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING))
    throw std::runtime_error("Could not replace match history");
}
Json profile_config();
// What the game's own result file says about a peer-to-peer match: {"body":{...},"identity",
// "signature"} (mu_net's result_outbox). Only a game that ended with an outcome, without a desync
// or a lost connection, counts as a win or a loss. A session of several games leaves one file per
// game (game_running reads each).
std::string p2p_outcome(const std::string& result_file) {
  if(result_file.empty()) return "incomplete";
  std::ifstream f(std::filesystem::u8path(result_file),std::ios::binary);
  if(!f) return "incomplete";
  const Json file=Json::parse(f,nullptr,false);
  if(!file.is_object() || !file.count("body") || !file["body"].is_object()) return "incomplete";
  const Json& body=file["body"];
  auto flag=[&](const char* key){ return body.count(key) && body[key].is_boolean() && body[key].get<bool>(); };
  auto number=[](const Json& from,const char* key){ return from.count(key) && from[key].is_number_integer()?from[key].get<int>():-1; };
  if(!flag("has_outcome") || flag("desync") || flag("disconnected")) return "incomplete";
  const int slot=number(body,"local_slot"), winner=number(body,"winner");
  if(slot<0 || slot>1) return "incomplete";
  if(winner==0 || winner==1) return winner==slot?"win":"loss";
  // No winner named (a timeout the game left undecided): the stocks left decide, when they differ.
  if(body.count("players") && body["players"].is_array() && body["players"].size()>=2 &&
     body["players"][0].is_object() && body["players"][1].is_object()) {
    const int mine=number(body["players"][slot],"stocks"), theirs=number(body["players"][1-slot],"stocks");
    if(mine>=0 && theirs>=0 && mine!=theirs) return mine>theirs?"win":"loss";
  }
  return "incomplete";
}
void record_match(const Match& match,const std::string& result,int game_number) {
  SYSTEMTIME now{}; GetLocalTime(&now);
  char when[32]{};
  std::snprintf(when,sizeof when,"%04u-%02u-%02u %02u:%02u",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute);
  history.insert(history.begin(),Json{{"id",match.id+"/"+std::to_string(game_number)},
                                      {"opponent",match.opponent},{"code",match.code},{"build",match.build},
                                      {"result",result},{"when",when},{"game",game_number}});
  try { save_history(); } catch(...) { history.erase(history.begin()); throw; }
  bool announce=false;{std::lock_guard<std::mutex> lock(mutex);announce=go_online_requested;}
  if(announce) enqueue("join",own_profile());
}
void consume_results() {
  if(!pending_started || !pending_match || !result_mailbox_ready) return;
  std::ifstream file(std::filesystem::u8path(directory+"/lobby-game-status.json.results"),std::ios::binary);
  if(!file) return;
  file.seekg((std::streamoff)result_offset);
  if(!file) return;
  const std::string data((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
  size_t consumed=0;
  for(;;) {
    const auto newline=data.find('\n',consumed);
    if(newline==std::string::npos) break;
    auto report=Json::parse(data.substr(consumed,newline-consumed),nullptr,false);
    if(report.is_object()) {
      const auto result=report.value("result",std::string());
      if(result=="win" || result=="loss" || result=="incomplete") {
        record_match(*pending_match,result,result_events+1);
        ++result_events;
      }
    }
    result_offset+=newline+1-consumed;
    consumed=newline+1;
  }
}
bool peer_mode(const Json& cfg) {
  return cfg.value("mode",cfg.value("url",std::string()).empty()?std::string("peer"):std::string("service"))=="peer";
}
std::string peer_text(const Json& p,const std::map<std::string,int>& ping) {
  std::string s=p.value("name",std::string("?"))+" | "+p.value("location",launcher::lang::tx("Unknown"));
  auto it=ping.find(p.value("id",std::string()));
  s+=" | "+(it==ping.end()?launcher::lang::tx("Ping unavailable"):std::to_string(it->second)+" ms");
  s+=" | ";
  for(auto c:p.value("mains",Json::array())) { int i=c.get<int>(); if(i>=0 && i<26) s+=std::string(characters[i])+" / "; }
  return s+" | "+(p.value("ready",true)?launcher::lang::tx(p.value("status",std::string("Online"))):launcher::lang::tr("lobby.card.finish_setup"));
}

// The UDP socket stays open for the entire launcher session. Registering it with the service
// discovers NAT's public mapping; peers probe each other directly. No server RTT is shown as ping.
class Probe {
  SOCKET socket_=INVALID_SOCKET;
  struct Pending { std::string id; sockaddr_in address; ULONGLONG start; };
  std::map<std::string,Pending> pending;
  std::mutex state_mutex;
  Json config_, state_;
  std::string cookie_;
  std::atomic<bool> done{false};
  std::thread thread;
  std::map<std::string,std::pair<int,ULONGLONG>> readings;
public:
  Probe() {
    socket_=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    sockaddr_in a{}; a.sin_family=AF_INET; bind(socket_,(sockaddr*)&a,sizeof a);
    u_long nonblock=1; ioctlsocket(socket_,FIONBIO,&nonblock);
    thread=std::thread([this] {
      ULONGLONG sent=0;
      while(!done) {
        Json cfg,state; std::string cookie;
        { std::lock_guard<std::mutex> lock(state_mutex); cfg=config_; state=state_; cookie=cookie_; }
        if(!cfg.is_object()) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
        try {
          if(GetTickCount64()-sent>=1500) { send(cfg,state); sent=GetTickCount64(); }
          receive(cookie);
        } catch(...) {} // HTTP still works when UDP/DNS is unavailable.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    });
  }
  ~Probe(){ done=true; if(thread.joinable()) thread.join(); if(socket_!=INVALID_SOCKET) closesocket(socket_); }
  void poll(const Json& cfg,const Json& state,const std::string& cookie) {
    std::lock_guard<std::mutex> lock(state_mutex); config_=cfg; state_=state; cookie_=cookie;
  }
private:
  void send(const Json& cfg,const Json& state) {
    auto e=endpoint(cfg.value("url",std::string()));
    addrinfo hint{},*result=nullptr; hint.ai_family=AF_INET; hint.ai_socktype=SOCK_DGRAM;
    std::string port=std::to_string(cfg.value("udp_port",(int)e.port));
    if(getaddrinfo(utf8(e.host).c_str(),port.c_str(),&hint,&result)==0) {
      std::string token=cfg.value("udp_token",std::string());
      sendto(socket_,token.data(),(int)token.size(),0,result->ai_addr,(int)result->ai_addrlen); freeaddrinfo(result);
    }
    auto now=GetTickCount64();
    for(auto it=pending.begin();it!=pending.end();) {
      if(now-it->second.start>4000) it=pending.erase(it); else ++it;
    }
    const auto me=state.value("self",Json::object()).value("id",cfg.value("id",std::string()));
    for(auto& p:state.value("players",Json::array())) {
      bool invited=false;
      for(const auto& request:state.value("requests",Json::array())) if(request.value("state",std::string())=="pending" &&
        ((request.value("from",std::string())==me && request.value("to",std::string())==p.value("id",std::string())) ||
         (request.value("to",std::string())==me && request.value("from",std::string())==p.value("id",std::string())))) invited=true;
      if(!invited || !p.count("endpoint")) continue;
      auto a=p["endpoint"]; if(!a.is_array() || a.size()!=2) continue;
      sockaddr_in target{}; target.sin_family=AF_INET; target.sin_port=htons((u_short)a[1].get<int>());
      if(inet_pton(AF_INET,a[0].get<std::string>().c_str(),&target.sin_addr)!=1) continue;
      const auto nonce=std::to_string(now)+"-"+p["id"].get<std::string>();
      const auto packet="MU1 "+p["probe"].get<std::string>()+" "+nonce;
      pending[nonce]={p["id"].get<std::string>(),target,GetTickCount64()};
      sendto(socket_,packet.data(),(int)packet.size(),0,(sockaddr*)&target,sizeof target);
    }
  }
  void receive(const std::string& cookie) {
    // A continuously serviced socket avoids counting the UI/HTTP poll interval as network RTT.
    for(int packets=0;packets<128;++packets) {
      char buffer[256]; sockaddr_in from{}; int len=sizeof from;
      int n=recvfrom(socket_,buffer,sizeof buffer,0,(sockaddr*)&from,&len);
      if(n<0) break;
      std::string packet(buffer,n),prefix="MU1 "+cookie+" ";
      if(!cookie.empty() && packet.rfind(prefix,0)==0) {
        std::string reply="MU2 "+packet.substr(prefix.size());
        sendto(socket_,reply.data(),(int)reply.size(),0,(sockaddr*)&from,len);
      } else if(packet.rfind("MU2 ",0)==0) {
        auto it=pending.find(packet.substr(4));
        if(it!=pending.end() && from.sin_addr.s_addr==it->second.address.sin_addr.s_addr && from.sin_port==it->second.address.sin_port) {
          const auto now=GetTickCount64(); readings[it->second.id]={(int)(now-it->second.start),now}; pending.erase(it);
        }
      }
    }
    // Expire old values rather than showing a stale low ping after the peer disappears.
    std::lock_guard<std::mutex> lock(mutex);
    pings.clear();
    for(auto it=readings.begin();it!=readings.end();) {
      if(GetTickCount64()-it->second.second>10000) it=readings.erase(it);
      else { pings[it->first]=it->second.first; ++it; }
    }
  }
};
// Test runs only (MELEE_LAUNCHER_TEST): two hidden launchers on one PC meet without the public DHT
// and without a click (tools/p2p_launcher_pair.py).
const char* test_env(const char* name) { return test_mode?std::getenv(name):nullptr; }
// The peer lobby for a profile. `port`: the UDP port to bind instead of the saved one.
std::unique_ptr<PeerLobby> make_peer(const Json& cfg,int port=0) {
  std::string url=cfg.value("url",std::string());
  if(!port) port=cfg.value("peer_port",0);
  PeerTestOptions options;
  if(const char* v=test_env("MELEE_LAUNCHER_TEST_LOBBY_PEER")) url=v;
  if(const char* v=test_env("MELEE_LAUNCHER_TEST_LOBBY_PORT")) { if(!port) port=std::atoi(v); }
  options.no_dht=test_env("MELEE_LAUNCHER_TEST_NO_DHT")!=nullptr;
  return std::make_unique<PeerLobby>(directory,url,port,options);
}
void work() {
  WSADATA ws{}; WSAStartup(MAKEWORD(2,2),&ws);
  {
    Probe probe;
    Json cfg;
    { std::lock_guard<std::mutex> lock(mutex); cfg=config; }
    std::unique_ptr<PeerLobby> peer;
    if(peer_mode(cfg) && cfg.count("name")) {
      try { peer=make_peer(cfg); }
      catch(const std::exception& ex) { std::lock_guard<std::mutex> lock(mutex); notice=ex.what(); }
    }
    if(peer) probe.poll(Json(),Json(),"");
    std::string cookie;
    int held_port=0;   // the lobby's port while it is closed for a match; 0: there was no lobby to close
    for(;;) {
      std::deque<Command> batch; bool playing; std::filesystem::file_time_type started;
      bool close_lobby=false,open_lobby=false,online=false;
      {
        std::unique_lock<std::mutex> lock(mutex);
        // While the lobby is closed for a match nothing is done and commands wait in their queue.
        wake.wait_for(lock,std::chrono::milliseconds(peer?50:(joined?800:5000)),
                      []{ return stopping || hold_wanted!=held || (!held && !commands.empty()); });
        if(stopping) break;
        close_lobby=hold_wanted && !held; open_lobby=!hold_wanted && held; online=go_online_requested;
        if(hold_wanted && held) continue;
        if(!close_lobby && !open_lobby) batch.swap(commands);
        playing=running; started=game_started;
      }
      if(close_lobby) {
        // The game is about to bind this port. The other launcher resends its accept every half
        // second until it has this side's half of the setup, so the lobby answers a little longer
        // (one resend at least), tells everyone "In game", and only then lets the socket go.
        held_port=0;
        if(peer) {
          held_port=peer->port();
          try {
            peer->presence({{"status","In game"},{"stocks",Json::array()}}); peer->presence_now();
            for(const ULONGLONG until=GetTickCount64()+600;GetTickCount64()<until;Sleep(20)) peer->tick();
          } catch(...) {}
          peer.reset();
          lobby_log(directory,"lobby closed for a match: the game takes UDP port "+std::to_string(held_port));
        }
        { std::lock_guard<std::mutex> lock(mutex); held=true; snapshot=Json::object(); pings.clear(); }
        hold_changed.notify_all();
        continue;
      }
      if(open_lobby) {
        // The game is gone: the same port again (the game may take a moment to let go of it), so
        // the router mapping other players already use still leads here, then the player as before.
        if(held_port) {
          std::string problem;
          for(int attempt=0;attempt<10 && !peer;++attempt) {
            try { peer=make_peer(cfg,held_port); }
            catch(const std::exception& ex) { problem=ex.what(); Sleep(200); }
          }
          if(!peer) { try { peer=make_peer(cfg); } catch(const std::exception& ex) { problem=ex.what(); } }
          try {
            if(peer) { if(online) peer->join(cfg); else peer->update_profile(cfg); }
            if(peer) lobby_log(directory,"lobby open again on UDP port "+std::to_string(peer->port()));
          } catch(const std::exception& ex) { problem=ex.what(); }
          if(!peer || (online && !peer->visible())) { std::lock_guard<std::mutex> lock(mutex); notice=problem; }
        }
        { std::lock_guard<std::mutex> lock(mutex); held=false; }
        hold_changed.notify_all();
        continue;
      }
      try {
        for(const auto& c:batch) {
          try {
            if(c.action=="join") {
              Json next=c.data;
              if(peer_mode(next)) {
                if(!peer || !peer_mode(cfg) || cfg.value("url",std::string())!=next.value("url",std::string())) {
                  peer.reset();
                  peer=make_peer(next);
                }
                probe.poll(Json(),Json(),"");
                peer->join(next); cfg=next; save(cfg);
                std::lock_guard<std::mutex> lock(mutex); config=cfg; self=peer->id(); joined=true; go_online_requested=true;
              } else {
                peer.reset();
                next["token"]=(!peer_mode(cfg) && cfg.value("url",std::string())==next.value("url",std::string()))?cfg.value("token",std::string()):"";
                auto r=api(next,"join",next);
                next["token"]=r["token"]; cfg=next; cookie=r["probe"].get<std::string>(); save(cfg);
                cfg["udp_token"]=r["udp_token"];
                std::lock_guard<std::mutex> lock(mutex); config=cfg; self=r["id"].get<std::string>(); joined=true; go_online_requested=true;
              }
            } else {
              if(peer_mode(cfg)) { if(!peer) continue; peer->command(c.action,c.data); }
              else { if(cfg.value("token",std::string()).empty()) continue; api(cfg,c.action,c.data); }
              // The match character and color were changed on the Profile page: the lobby that
              // opens again after a match is given `cfg`, so they are kept there too.
              if(c.action=="profile" && c.data.is_object()) {
                for(const char* key:{"p2p_ch","p2p_col","hosted_ok"}) { if(c.data.count(key)) cfg[key]=c.data[key]; else cfg.erase(key); }
              }
              std::lock_guard<std::mutex> lock(mutex);
              if(c.action=="leave") { joined=false; go_online_requested=false; }
            }
            std::lock_guard<std::mutex> lock(mutex);
            // A private chat action leaves the status line alone (its own messages arrive as notices).
            if(c.action.rfind("pm",0)!=0)
#ifdef MELEE_NO_SLIPPI
              notice=peer_mode(cfg)?"You're online. Select a player to send a match request.":
                                     "Connected. Location and player code are player supplied.";
#else
              notice=!peer_mode(cfg)?"Connected. Location and Slippi code are player supplied.":
                     // Online without the account: say what works, once, when the player goes online.
                     c.action=="join" && !cfg.value("hosted_ok",true)?launcher::lang::tr("lobby.p2p.test_online"):
                     "You're online. Select a player to send a match request.";
#endif
          } catch(const std::exception& ex) {
            std::lock_guard<std::mutex> lock(mutex); notice=ex.what();
            if(c.action=="join") go_online_requested=joined;
          }
        }
        Json presence={{"status",playing?"In game":"Online"},{"stocks",Json::array()}};
        if(playing) {
          const auto path=std::filesystem::u8path(directory+"/lobby-game-status.json");
          std::error_code ec; auto modified=std::filesystem::last_write_time(path,ec);
          if(!ec && modified>=started && decltype(modified)::clock::now()-modified<std::chrono::seconds(5)) {
            std::ifstream f(path); auto live=Json::parse(f,nullptr,false);
            if(live.is_object()) presence=live;
          }
        }
        Json state,identity;
        if(peer_mode(cfg)) {
          if(!peer) continue;
          peer->presence(presence); peer->tick(); state=peer->state(); identity=state["self"];
        } else {
          if(cfg.value("token",std::string()).empty()) continue;
          state=api(cfg,"sync",presence); identity=state["self"];
          cookie=identity["probe"].get<std::string>(); cfg["udp_token"]=identity["udp_token"];
          probe.poll(cfg,state,cookie);
        }
        {
          std::lock_guard<std::mutex> lock(mutex); self=identity["id"].get<std::string>(); snapshot=state;
          // The peer lobby's own messages (a refused or unanswered match request), each shown once.
          static unsigned shown_notice=0;
          if(state.value("notice_seq",0u)!=shown_notice) {
            shown_notice=state.value("notice_seq",0u);
            if(!state.value("notice",std::string()).empty()) notice=state.value("notice",std::string());
            // "Can't reach X. Use Slippi Direct with their code": keep the code for the Copy code
            // button, which shows while that line is the notice.
            const Json notices=state.value("notices",Json::array());
            const Json last=notices.is_array() && !notices.empty()?notices.back():Json::object();
            const Json args=last.is_object()?last.value("args",Json::object()):Json::object();
            const std::string code=args.is_object() && args.count("code") && args["code"].is_string()?
                                   normalize_code(args["code"].get<std::string>()):std::string();
            if(last.is_object() && last.value("kind",std::string())=="unreachable" && valid_code(code)) {
              copy_code=code; copy_code_notice=notice;
            } else copy_code.clear();
          }
          if(peer) pings=peer->pings();
          joined=identity.value("visible",false);
          if(joined && !go_online_requested) commands.push_back({"leave",Json::object()});
          if(peer) {
            // The peer lobby hands over each agreed match once, with the version both chose. Friends
            // can play while out of the public lobby, so this does not require `joined`.
            Json launch;
            while(peer->take_launch(launch)) {
              const std::string id=launch.value("request",std::string());
              if(id.empty() || launched.count(id)) continue;
              launched.insert(id);
              if(playing) continue;
              Match m;
              m.id=id; m.code=launch.value("code",std::string()); m.character=launch.value("character",2);
              m.build=launch.value("build",std::string()); m.opponent=launch.value("name",std::string());
              m.mode=launch.value("mode",std::string("vanilla"));
              // A mod match runs the disc this PC's Mods folder scan found (or the custom ISO both
              // players chose); without it the player is told here instead of a failed launch.
              const std::string missing=find_match_disc(m.mode,mod_paths,custom_hash,current_prefs.custom_iso,
                                                        current_prefs.custom_name,m.mod_path,m.mod_name);
              if(!missing.empty()) { notice=missing; continue; }
              // A peer-to-peer match: every match of the build without the other kind, and here a
              // request that asked for it (the lobby then hands over a "p2p" setup).
              if(p2p_matches || launch.count("p2p")) {
                // The request id names the result file and half of the setup came from the other
                // player: both are checked before anything reaches a path or a command line. A match
                // that cannot start frees this player again instead of leaving them "in a match".
                const bool plain_id=id.size()==32 && std::all_of(id.begin(),id.end(),[](char c){ return (c>='0'&&c<='9')||(c>='a'&&c<='f'); });
                m.p2p_result=directory+"\\p2p-results\\"+id+".json";
                if(plain_id) m.p2p_args=p2p_arguments(launch.value("p2p",Json::object()),directory+"\\lobby-peer-identity.json",m.p2p_result);
                if(m.p2p_args.empty()) {
                  notice=launcher::lang::tr("lobby.p2p.setup_failed");
                  commands.push_back({"available",Json::object()});
                  continue;
                }
                m.p2p_lobby_port=launch["p2p"].value("port",0)==peer->port();
                matches.push_back(m);
                notice=launcher::lang::tr("lobby.p2p.starting",{{"name",m.opponent}});
                continue;
              }
#ifndef MELEE_NO_SLIPPI   // in the build without that layer every match took the peer-to-peer branch above
              // The code goes onto the game's command line as the opponent: never one this launcher made
              // from a name, which is no account. (The lobby refuses these requests before they get here.)
              if(!cfg.value("hosted_ok",true)) { notice=launcher::lang::tr("lobby.error.self_no_account"); commands.push_back({"available",Json::object()}); continue; }
              matches.push_back(m);
              notice=m.mode=="vanilla"?"Match accepted. Starting Slippi Direct...":
                     launcher::lang::tr("lobby.starting_mod",{{"mode",m.mod_name}});
#endif
            }
          }
#ifndef MELEE_NO_SLIPPI   // the hosted service's matches start Direct, which the build without that layer does not have
          else for(auto& r:state.value("requests",Json::array())) {
            if(r.value("state",std::string())!="accepted" || !r.value("confirmed",true) || playing || !joined) continue;
            std::string id=r["id"], peer_id=r["from"]==self?r["to"].get<std::string>():r["from"].get<std::string>();
            if(launched.count(id)) continue;
            for(auto& p:state["players"]) if(p["id"]==peer_id && r.value("transport",std::string())==launcher::lobby::kHostedTransport) {
              launched.insert(id); matches.push_back({id,p["code"].get<std::string>(),cfg["mains"][0].get<int>(),cfg.value("build",std::string()),p.value("name",std::string())});
              notice="Match accepted. Starting Slippi Direct...";
            }
          }
#endif
        }
      } catch(const std::exception& ex) { std::lock_guard<std::mutex> lock(mutex); notice=ex.what(); snapshot=Json::object(); pings.clear(); }
    }
    if(!peer_mode(cfg) && !cfg.value("token",std::string()).empty()) { try { api(cfg,"offline",Json::object()); } catch(...) {} }
  }
  WSACleanup();
}
#include "launcher_lobby_ui.inl"
// layout(), then the two P2P Direct test controls of the launcher with both kinds of match: in the
// page header, left of Go Online, where the other build has Find match. layout() does not know
// them, so nothing is placed twice.
void layout_all() {
  layout();
  if(p2p_matches || !window) return;
  RECT r{}; GetClientRect(window,&r);
  const int width=MulDiv(r.right,96,owner?GetDpiForWindow(owner):96);
  place(P2P_REQUEST,width-132-158,24,150,30,p2p_test_shown());
  place(P2P_FIGHTER_TEST,width-132-158-204,24,196,30,p2p_test_shown());
}
std::string selected(HWND h,const Json& list) {
  int i=(int)SendMessageW(h,LB_GETCURSEL,0,0);
  return i>=0 && i<(int)list.size()?list[i].value("id",std::string()):"";
}
void request_chime(int volume) {
  if(volume<=0) return;
  std::thread([volume] {
    constexpr int sample_rate=22050, frames=sample_rate*3/10;
    std::array<short,frames> samples{};
    for(int i=0;i<frames;++i) {
      const double time=double(i)/sample_rate;
      const double frequency=i<frames/2?660.0:880.0;
      const double envelope=std::min(1.0,time*35.0)*std::min(1.0,(frames-i)/1800.0);
      samples[i]=(short)(std::sin(time*frequency*6.283185307179586)*envelope*7500*volume/100);
    }
    WAVEFORMATEX format{}; format.wFormatTag=WAVE_FORMAT_PCM; format.nChannels=1;
    format.nSamplesPerSec=sample_rate; format.wBitsPerSample=16; format.nBlockAlign=2;
    format.nAvgBytesPerSec=sample_rate*2;
    HWAVEOUT output{}; if(waveOutOpen(&output,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR) return;
    WAVEHDR header{}; header.lpData=(LPSTR)samples.data(); header.dwBufferLength=sizeof samples;
    if(waveOutPrepareHeader(output,&header,sizeof header)==MMSYSERR_NOERROR) {
      if(waveOutWrite(output,&header,sizeof header)==MMSYSERR_NOERROR) {
        while(!(header.dwFlags&WHDR_DONE)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      waveOutUnprepareHeader(output,&header,sizeof header);
    }
    waveOutClose(output);
  }).detach();
}
// ---- banners: one strip across the top of the launcher, over whichever page is showing ----
struct Banner {
  std::string id,title,detail; std::vector<BannerButton> buttons; std::function<void(int)> click;
  int priority=0; bool keep_open=false; ULONGLONG shown=0;
};
std::vector<Banner> banners;
HWND banner_window{};
HFONT banner_title_font{}, banner_text_font{};
std::vector<RECT> banner_hits;
int banner_hover=-1, banner_press=-1;
const Banner* banner_top() {
  const Banner* top=nullptr;
  for(const auto& b:banners) if(!top || b.priority>top->priority || (b.priority==top->priority && b.shown>top->shown)) top=&b;
  return top;
}
int BU(int n) { return MulDiv(n,owner?GetDpiForWindow(owner):96,96); }
void banner_place() {
  if(!banner_window) return;
  if(banners.empty()) { ShowWindow(banner_window,SW_HIDE); return; }
  RECT client{}; GetClientRect(owner,&client);
  SetWindowPos(banner_window,HWND_TOP,BU(12),BU(8),client.right-BU(24),BU(72),SWP_SHOWWINDOW|SWP_NOACTIVATE);
  InvalidateRect(banner_window,nullptr,FALSE);
}
void banner_click(int index) {
  const Banner* top=banner_top(); if(!top || index<0 || index>=(int)top->buttons.size()) return;
  auto click=top->click; const std::string id=top->id; const bool keep=top->keep_open;
  if(!keep) { banners.erase(std::remove_if(banners.begin(),banners.end(),[&](const Banner& b){ return b.id==id; }),banners.end()); banner_place(); }
  if(click) click(index);
}
LRESULT CALLBACK banner_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
  if(msg==WM_ERASEBKGND) return 1;
  // WM_PRINTCLIENT: the launcher's test captures (WM_APP+41) print the banner off screen too.
  if(msg==WM_PAINT || msg==WM_PRINTCLIENT) {
    PAINTSTRUCT ps{}; HDC target=msg==WM_PAINT?BeginPaint(w,&ps):(HDC)wp; RECT r{}; GetClientRect(w,&r);
    HDC dc=CreateCompatibleDC(target); HBITMAP bmp=CreateCompatibleBitmap(target,r.right,r.bottom); auto old=SelectObject(dc,bmp);
    HBRUSH back=CreateSolidBrush(RGB(14,19,31)); FillRect(dc,&r,back); DeleteObject(back);
    HBRUSH panel=CreateSolidBrush(RGB(26,34,54)); HPEN edge=CreatePen(PS_SOLID,1,launcher::theme::accent);
    auto ob=SelectObject(dc,panel); auto op=SelectObject(dc,edge);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,BU(12),BU(12)); SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(panel); DeleteObject(edge);
    RECT bar{r.left+BU(1),r.top+BU(10),r.left+BU(5),r.bottom-BU(10)}; HBRUSH accent=CreateSolidBrush(launcher::theme::accent); FillRect(dc,&bar,accent); DeleteObject(accent);
    banner_hits.clear();
    if(const Banner* b=banner_top()) {
      SetBkMode(dc,TRANSPARENT);
      int right=r.right-BU(14);
      std::vector<RECT> hits(b->buttons.size());
      for(int i=(int)b->buttons.size()-1;i>=0;--i) {
        const auto label=wide(launcher::lang::tx(b->buttons[i].label));
        SelectObject(dc,banner_text_font); SIZE s{}; GetTextExtentPoint32W(dc,label.c_str(),(int)label.size(),&s);
        RECT button{right-s.cx-BU(28),r.top+BU(20),right,r.bottom-BU(20)}; hits[i]=button; right=button.left-BU(10);
        const bool primary=b->buttons[i].primary, hover=banner_hover==i, press=banner_press==i;
        COLORREF fill=primary?(press?launcher::theme::pressed():launcher::theme::top()):(hover?RGB(44,56,84):RGB(33,43,66));
        HBRUSH bf=CreateSolidBrush(fill); HPEN bp=CreatePen(PS_SOLID,1,primary?fill:RGB(53,65,95));
        auto ob2=SelectObject(dc,bf); auto op2=SelectObject(dc,bp);
        RoundRect(dc,button.left,button.top,button.right,button.bottom,BU(9),BU(9)); SelectObject(dc,ob2); SelectObject(dc,op2); DeleteObject(bf); DeleteObject(bp);
        SetTextColor(dc,primary?launcher::theme::on_accent():RGB(230,235,245));
        DrawTextW(dc,label.c_str(),(int)label.size(),&button,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
      }
      banner_hits=hits;
      RECT title{r.left+BU(18),r.top+BU(10),right-BU(8),r.top+BU(36)};
      SelectObject(dc,banner_title_font); SetTextColor(dc,RGB(240,244,252));
      const auto t=wide(b->title); DrawTextW(dc,t.c_str(),(int)t.size(),&title,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
      RECT detail{r.left+BU(18),r.top+BU(36),right-BU(8),r.bottom-BU(8)};
      SelectObject(dc,banner_text_font); SetTextColor(dc,RGB(160,172,196));
      const auto d=wide(b->detail); DrawTextW(dc,d.c_str(),(int)d.size(),&detail,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_END_ELLIPSIS);
    }
    BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY); SelectObject(dc,old); DeleteObject(bmp); DeleteDC(dc);
    if(msg==WM_PAINT) EndPaint(w,&ps);
    return 0;
  }
  if(msg==WM_MOUSEMOVE || msg==WM_LBUTTONDOWN || msg==WM_LBUTTONUP) {
    POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; int hit=-1;
    for(size_t i=0;i<banner_hits.size();++i) if(PtInRect(&banner_hits[i],pt)) hit=(int)i;
    if(msg==WM_MOUSEMOVE) { if(hit!=banner_hover) { banner_hover=hit; InvalidateRect(w,nullptr,FALSE); TRACKMOUSEEVENT t{sizeof t,TME_LEAVE,w,0}; TrackMouseEvent(&t); } }
    else if(msg==WM_LBUTTONDOWN) { banner_press=hit; SetCapture(w); InvalidateRect(w,nullptr,FALSE); }
    else { ReleaseCapture(); const int pressed=banner_press; banner_press=-1; InvalidateRect(w,nullptr,FALSE); if(pressed>=0 && pressed==hit) banner_click(hit); }
    return 0;
  }
  if(msg==WM_MOUSELEAVE) { banner_hover=-1; InvalidateRect(w,nullptr,FALSE); return 0; }
  if(msg==WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr,banner_hover>=0?IDC_HAND:IDC_ARROW)); return TRUE; }
  return DefWindowProcW(w,msg,wp,lp);
}
void banner_create() {
  if(banner_window || !owner) return;
  WNDCLASSW wc{}; wc.lpfnWndProc=banner_proc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"MeleeUnlockedBanner";
  wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); RegisterClassW(&wc);
  NONCLIENTMETRICSW metrics{sizeof metrics}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof metrics,&metrics,0);
  LOGFONTW font=metrics.lfMessageFont; font.lfQuality=CLEARTYPE_QUALITY;
  font.lfHeight=-BU(15); font.lfWeight=FW_SEMIBOLD; banner_title_font=CreateFontIndirectW(&font);
  font.lfHeight=-BU(12); font.lfWeight=FW_NORMAL; banner_text_font=CreateFontIndirectW(&font);
  banner_window=CreateWindowExW(0,L"MeleeUnlockedBanner",L"",WS_CHILD|WS_CLIPSIBLINGS,0,0,0,0,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
}
void process_invites(const Json& state,const std::map<std::string,int>& ping,const std::string& me,bool playing) {
  if(playing || !owner) return;
  for(const auto& request:state.value("requests",Json::array())) {
    if(request.value("state",std::string())!="pending" || request.value("to",std::string())!=me) continue;
    const auto id=request.value("id",std::string()), sender=request.value("from",std::string());
    if(id.empty() || prompted_requests.count(id)) continue;
    if(auto_reject) {
      prompted_requests.insert(id); enqueue("cancel",{{"request",id}});
      label(STATUS,"Match request automatically declined."); continue;
    }
    if(!incoming_since.count(id)) {
      incoming_since[id]=GetTickCount64();
      if(sound_enabled && !test_mode) request_chime(sound_volume);
      FLASHWINFO flash{sizeof flash,owner,FLASHW_TRAY|FLASHW_TIMERNOFG,5,0}; if(!test_mode) FlashWindowEx(&flash);
    }
    // Let the on-demand peer probe return before presenting its result.
    if(!ping.count(sender) && GetTickCount64()-incoming_since[id]<2200) continue;
    prompted_requests.insert(id);
    Json player=Json::object();
    for(const auto& item:state.value("players",Json::array())) if(item.value("id",std::string())==sender) { player=item; break; }
    std::string name=player.value("name",std::string("Player"));
    std::string location=player.value("location",launcher::lang::tx("Location unavailable"));
    std::string mains;
    for(const auto& value:player.value("mains",Json::array())) {
      if(!value.is_number_integer()) continue; int n=value.get<int>(); if(n<0||n>=26) continue;
      if(!mains.empty()) mains+=" / "; mains+=characters[n];
    }
    const auto it=ping.find(sender);
    const std::string latency=it==ping.end()?launcher::lang::tx("ping unavailable"):std::to_string(it->second)+" ms";
    // The version they asked for, when it is not plain vanilla.
    const std::string mode=request.value("mode",std::string("vanilla"));
    Json mine; { std::lock_guard<std::mutex> lock(mutex); mine=config; }
    const std::string version=mode=="vanilla"?std::string():"  |  "+mode_title(mode,mine,player);
    // A request of the other kind says so (only the launcher with both kinds has two to tell apart).
    const bool p2p_test=!p2p_matches && request.value("transport",std::string())==kP2pTransport;
    const std::string detail=location+"  |  "+latency+"  |  "+(mains.empty()?launcher::lang::tx("mains not shared"):mains)+version+
                             (p2p_test?"  |  "+launcher::lang::tr("lobby.p2p.test_detail"):std::string());
    // A banner, not a blocking dialog: the launcher keeps working while the player decides, and a
    // request that expires or is withdrawn takes its banner with it. The title is one template, so
    // each language puts the name where its grammar wants it.
    show_banner("request:"+id,launcher::lang::tr(p2p_test?"lobby.p2p.test_request_from":"lobby.request_from",{{"name",name}}),detail,
                {{"Accept",true},{"Decline",false}},
                [id](int button){
                  FLASHWINFO stop{sizeof stop,owner,FLASHW_STOP,0,0}; FlashWindowEx(&stop);
                  enqueue(button==0?"accept":"cancel",{{"request",id}});
                  if(window) label(STATUS,button==0?"Accepted. Starting direct match...":"Match request declined.");
                },10);
    bring_to_front();
  }
  for(auto it=incoming_since.begin();it!=incoming_since.end();) {
    bool found=false; for(const auto& request:state.value("requests",Json::array()))
      if(request.value("id",std::string())==it->first && request.value("state",std::string())=="pending") found=true;
    if(!found) { close_banner("request:"+it->first); it=incoming_since.erase(it); } else ++it;
  }
}
// Private chat requests: a banner with Accept, Decline and Block, like a match request but without
// a sound and without taking the foreground. It goes when the request is answered, withdrawn or
// runs out.
void process_private(const Json& state) {
  if(!owner) return;
  std::set<std::string> waiting;
  for(const auto& room:state.value("private",Json::array())) {
    if(room.value("state",std::string())!="incoming") continue;
    const auto id=room.value("id",std::string());
    if(id.empty()) continue;
    waiting.insert(id);
    if(!private_prompted.insert(id).second) continue;
    show_banner("pm:"+id,launcher::lang::tr("lobby.pm.request_from",{{"name",room.value("name",std::string("Player"))}}),
                launcher::lang::tr("lobby.pm.request_detail"),
                {{"Accept",true},{"Decline",false},{"Block",false}},
                [id](int button){
                  Json data={{"room",id}};
                  if(button==2) data["block"]=true;
                  enqueue(button==0?"pm_accept":"pm_decline",data);
                  if(window) SetTimer(window,2,150,nullptr);
                },5);
    if(!test_mode) { FLASHWINFO flash{sizeof flash,owner,FLASHW_TRAY|FLASHW_TIMERNOFG,3,0}; FlashWindowEx(&flash); }
  }
  for(auto it=private_prompted.begin();it!=private_prompted.end();) {
    if(waiting.count(*it)) { ++it; continue; }
    close_banner("pm:"+*it); it=private_prompted.erase(it);
  }
}
// Closes a private room for this player (the other one is told) and takes its tab away at once.
void close_room(const std::string& id,bool block) {
  Json data={{"room",id}};
  if(block) data["block"]=true;
  enqueue("pm_close",data);
  rooms_closing.insert(id);
  if(active_room==id) show_room(std::string());
}
// Right click on a private tab: close it, or close it and ignore that player's requests.
void room_menu(HWND w,const std::string& id,POINT at) {
  const Json* room=find_room(id); if(!room) return;
  std::string name=room->value("name",std::string());
  for(size_t i=name.find('&');i!=std::string::npos;i=name.find('&',i+2)) name.insert(i,1,'&');   // not a menu shortcut
  HMENU menu=CreatePopupMenu();
  AppendMenuW(menu,MF_STRING,1,launcher::lang::trw("lobby.pm.close").c_str());
  AppendMenuW(menu,MF_STRING,2,launcher::lang::trw("lobby.pm.block",{{"name",name}}).c_str());
  ClientToScreen(w,&at);
  const int pick=(int)TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN|TPM_TOPALIGN,at.x,at.y,0,w,nullptr);
  DestroyMenu(menu);
  if(pick==1 || pick==2) close_room(id,pick==2);
}
// Private Chat on a player or a friend: the room with them if there is one, else a request.
void open_private(HWND w,const std::string& target) {
  for(const auto& room:private_rooms)
    if(room.value("peer",std::string())==target && room.value("state",std::string())=="open") {
      show_room(room.value("id",std::string())); lobby_tab=0;
      return;
    }
  enqueue("pm_request",{{"target",target}});
  SetTimer(w,2,150,nullptr);
}
// The Show filter over the Players list. A player passes when they take requests for that version
// (a mod also installed, a custom ISO announced); the player's own card always shows.
bool shown_by_filter(const Json& p) {
  if(roster_filter=="all" || p.value("is_self",false)) return true;
  if(!open_to(p,roster_filter)) return false;
  if(roster_filter=="akaneia" || roster_filter=="ace") {
    const Json has=p.value("has",Json::object());
    return has.is_object() && has.count(roster_filter)!=0;
  }
  if(roster_filter=="custom") return !iso_hash(p).empty();
  return true;
}
std::string filter_title(const std::string& filter) {
  if(filter=="vanilla") return launcher::lang::tr("mode.vanilla");
  if(filter=="akaneia") return "Akaneia";
  if(filter=="ace") return "ACE";
  if(filter=="custom") return launcher::lang::tr("mode.custom");
  return launcher::lang::tr("lobby.filter.all");
}
std::string filter_caption() { return launcher::lang::tr("lobby.filter.button",{{"mode",filter_title(roster_filter)}}); }
void refresh() {
  try { consume_results(); }
  catch(const std::exception& ex) { std::lock_guard<std::mutex> lock(mutex); notice=ex.what(); }
  Json state,local_profile; std::string status,me,copy; std::map<std::string,int> ping; bool active,playing,wanted;
  { std::lock_guard<std::mutex> lock(mutex); state=snapshot; local_profile=config; status=notice; ping=pings; me=self; active=joined; playing=running; wanted=go_online_requested;
    if(!copy_code.empty() && copy_code_notice==notice) copy=copy_code; }
  label(STATUS,status);
  label(GO_ONLINE,wanted?"Go Offline":"Go Online");
  auto refill=[&](int control,Json& old,Json next,auto format) {
    HWND h=GetDlgItem(window,control); std::string choice=selected(h,old);
    auto top=SendMessageW(h,LB_GETTOPINDEX,0,0);
    if(old==next) return;
    const bool visible=(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE)!=0;
    if(visible) SendMessageW(h,WM_SETREDRAW,FALSE,0);
    SendMessageW(h,LB_RESETCONTENT,0,0); old=next;
    for(size_t i=0;i<old.size();++i) {
      SendMessageW(h,LB_ADDSTRING,0,(LPARAM)wide(format(old[i])).c_str());
      if(old[i].value("id",std::string())==choice) SendMessageW(h,LB_SETCURSEL,i,0);
    }
    SendMessageW(h,LB_SETTOPINDEX,top,0);
    if(visible) SendMessageW(h,WM_SETREDRAW,TRUE,0); InvalidateRect(h,nullptr,FALSE);
  };
  // Only invite participants see a measured direct ping.
  std::set<std::string> invited_peers;
  for(const auto& request:state.value("requests",Json::array())) if(request.value("state",std::string())=="pending") {
    auto from=request.value("from",std::string()),to=request.value("to",std::string());
    if(from==me) invited_peers.insert(to); if(to==me) invited_peers.insert(from);
  }
  for(auto it=ping.begin();it!=ping.end();) if(!invited_peers.count(it->first)) it=ping.erase(it); else ++it;
  Json roster=state.value("players",Json::array());
  if(active) { local_profile["id"]=me; local_profile["is_self"]=true; local_profile["status"]=playing?"In game":"Online"; roster.insert(roster.begin(),local_profile); }
  if(displayed_pings!=ping) { displayed_pings=ping; InvalidateRect(GetDlgItem(window,PLAYERS),nullptr,FALSE); }
  // The player's own card says "Finish game setup" while this PC cannot take a match.
  static bool drawn_ready=false;
  if(drawn_ready!=can_play) { drawn_ready=can_play; InvalidateRect(GetDlgItem(window,PLAYERS),nullptr,FALSE); }
  // The Show filter: only the players who take requests for that version, and the player's own card.
  roster_all=roster;
  if(roster_filter!="all") {
    Json shown=Json::array();
    for(const auto& p:roster) if(shown_by_filter(p)) shown.push_back(p);
    roster=std::move(shown);
  }
  refill(PLAYERS,rows,roster,[&](const Json& p){return peer_text(p,ping);});
  if(p2p_matches) {
    // A pasted invite was answered: that player is selected, ready for Send Match Request.
    const Json invite=state.value("invite",Json::object());
    const std::string found=invite.is_object()?invite.value("found",std::string()):std::string();
    const std::string answer=found.empty()?std::string():found+"/"+std::to_string(invite.value("seq",0u));
    if(answer!=invite_selected && !found.empty()) {
      for(int i=0;i<(int)rows.size();++i) if(rows[i].value("id",std::string())==found) {
        SendMessageW(GetDlgItem(window,PLAYERS),LB_SETCURSEL,i,0); invite_selected=answer;
        InvalidateRect(GetDlgItem(window,PLAYERS),nullptr,FALSE);
        break;
      }
    }
    const bool searching=state.value("self",Json::object()).value("searching",false);
    const std::string chosen=selected(GetDlgItem(window,PLAYERS),rows);
    bool chosen_blocked=false;
    for(const auto& p:rows) if(p.value("id",std::string())==chosen) chosen_blocked=p.value("blocked",false);
    label(FIND_MATCH,launcher::lang::tr(searching?"lobby.search.stop":"lobby.search.button"));
    label(BLOCK_PLAYER,launcher::lang::tr(chosen_blocked?"lobby.block.undo":"lobby.block.button"));
    EnableWindow(GetDlgItem(window,FIND_MATCH),searching || (active && !playing && can_play));
    EnableWindow(GetDlgItem(window,BLOCK_PLAYER),!chosen.empty() && chosen!=me);
    for(int id:{INVITE_CONNECT,INVITE_COPY}) EnableWindow(GetDlgItem(window,id),invite.is_object() && invite.count("text")!=0);
  }
  refill(REQUESTS,requests,state.value("requests",Json::array()),[&](const Json& r){
    std::string peer=r["from"]==me?r["to"].get<std::string>():r["from"].get<std::string>();
    std::string description=peer;
    for(auto& p:roster_all) if(p["id"]==peer) description=peer_text(p,ping);
    const std::string kind=!p2p_matches && r.value("transport",std::string())==kP2pTransport?" | "+launcher::lang::tr("lobby.p2p.test_button"):std::string();
    return launcher::lang::tx(r["to"]==me?"Incoming:":"Sent:")+" "+description+kind+" | "+launcher::lang::tx(r.value("state",std::string()))+" ("+std::to_string(r.value("remaining",0))+"s)";
  });
  Json social=state.value("friend_requests",Json::array()); for(auto& f:social) f["incoming"]=true;
  for(auto& f:state.value("friends",Json::array())) social.push_back(f);
  refill(FRIENDS,friends,social,[](const Json& f){
    std::string s=(f.value("incoming",false)?"Friend request: ":"")+f.value("name",std::string())+" | "+f.value("status",std::string());
    if(!f.value("stocks",Json::array()).empty()) { s+=" | Stocks:"; for(auto n:f["stocks"]) s+=" "+std::to_string(n.get<int>()); }
    return s;
  });
  int wins=0,losses=0;
  for(const auto& game:history) {
    if(game.value("result",std::string())=="win") ++wins;
    if(game.value("result",std::string())=="loss") ++losses;
  }
  label(RECORD,launcher::lang::fill(launcher::lang::tx("Local history: {wins} wins / {losses} losses (unranked)"),
                                    launcher::lang::Args{{"wins",std::to_string(wins)},{"losses",std::to_string(losses)}}));
  if(displayed_history!=history) {
  HWND past=GetDlgItem(window,HISTORY);
  auto history_top=SendMessageW(past,LB_GETTOPINDEX,0,0);
  const bool history_visible=(GetWindowLongPtrW(past,GWL_STYLE)&WS_VISIBLE)!=0;
  if(history_visible) SendMessageW(past,WM_SETREDRAW,FALSE,0); SendMessageW(past,LB_RESETCONTENT,0,0);
  for(size_t i=0;i<history.size() && i<100;++i) {
    const auto& game=history[i];
    const std::string line=game.value("when",std::string())+" | Game "+std::to_string(game.value("game",1))+" | "+
                           game.value("result",std::string("incomplete"))+" | vs "+
                           game.value("opponent",std::string("Unknown"))+" ("+game.value("code",std::string())+") | "+game.value("build",std::string());
    SendMessageW(past,LB_ADDSTRING,0,(LPARAM)wide(line).c_str());
  }
  SendMessageW(past,LB_SETTOPINDEX,history_top,0);
  if(history_visible) SendMessageW(past,WM_SETREDRAW,TRUE,0); InvalidateRect(past,nullptr,FALSE);
  displayed_history=history;
  }
  // Private rooms: one tab each once the request is accepted. A room that has just opened comes to
  // the front on both sides.
  {
    std::vector<Json> open_rooms;
    std::set<std::string> present, listed;
    for(const auto& room:state.value("private",Json::array())) {
      const auto id=room.value("id",std::string()), room_state=room.value("state",std::string());
      listed.insert(id);
      if(id.empty() || rooms_closing.count(id) || (room_state!="open" && room_state!="closed")) continue;
      open_rooms.push_back(room); present.insert(id);
    }
    for(auto it=rooms_closing.begin();it!=rooms_closing.end();) if(!listed.count(*it)) it=rooms_closing.erase(it); else ++it;
    std::stable_sort(open_rooms.begin(),open_rooms.end(),[](const Json& a,const Json& b){
      return a.value("created",0ull)<b.value("created",0ull); });
    const int tab_before=lobby_tab; const std::string room_before=active_room;
    for(const auto& room:open_rooms)
      if(rooms_known.insert(room.value("id",std::string())).second && room.value("state",std::string())=="open") {
        show_room(room.value("id",std::string())); lobby_tab=0;
      }
    for(auto it=rooms_known.begin();it!=rooms_known.end();) if(!present.count(*it)) it=rooms_known.erase(it); else ++it;
    for(auto it=room_seen.begin();it!=room_seen.end();) if(!present.count(it->first)) it=room_seen.erase(it); else ++it;
    if(!active_room.empty() && !present.count(active_room)) show_room(std::string());
    private_rooms=Json(open_rooms);
    // The room in front, on the page in front, is read.
    if(!active_room.empty() && lobby_tab==0 && IsWindowVisible(window)) {
      const Json* room=find_room(active_room);
      const Json messages=room?room->value("messages",Json::array()):Json::array();
      if(messages.is_array() && !messages.empty()) room_seen[active_room]=messages.back().value("id",std::string());
    }
    // Repaint the tabs when anything they show changed.
    std::string strip=active_room+"|"+std::to_string(lobby_tab);
    for(const auto& room:private_rooms)
      strip+="|"+room.value("id",std::string())+room.value("name",std::string())+room.value("state",std::string())+(room_unread(room)?"*":"");
    static std::string strip_drawn;
    if(strip!=strip_drawn || tab_before!=lobby_tab || room_before!=active_room) {
      strip_drawn=strip;
      RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }
  }
  Json next_chat=state.value("messages",Json::array());
  if(!active_room.empty()) {
    // The list shows the room in front; a private room's messages are never mixed with the lobby's.
    next_chat=Json::array();
    if(const Json* room=find_room(active_room)) {
      next_chat=room->value("messages",Json::array());
      if(room->value("state",std::string())=="closed") {
        const Json line={{"id","closed"},{"system",true},{"sender",""},{"name",""},
                         {"text",launcher::lang::tr("lobby.pm.closed_by",{{"name",room->value("name",std::string())}})}};
        next_chat.push_back(line);
      }
    }
  }
  if(next_chat!=chat_messages || chat_switched) {
    chat_messages=std::move(next_chat);HWND list=GetDlgItem(window,CHATLOG);
    int old_count=(int)SendMessageW(list,LB_GETCOUNT,0,0);
    int top=(int)SendMessageW(list,LB_GETTOPINDEX,0,0);
    RECT bounds{};GetClientRect(list,&bounds);
    int visible=std::max(1,int(bounds.bottom-bounds.top)/std::max(1,U(60)));
    bool at_bottom=chat_switched || old_count==0 || top+visible>=old_count-1;
    chat_switched=false;
    SendMessageW(list,WM_SETREDRAW,FALSE,0);SendMessageW(list,LB_RESETCONTENT,0,0);
    for(const auto& message:chat_messages) SendMessageW(list,LB_ADDSTRING,0,(LPARAM)wide(message.value("name",std::string("Player"))).c_str());
    SendMessageW(list,LB_SETTOPINDEX,at_bottom?std::max(0,int(chat_messages.size())-5):top,0);
    SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);
  }
  for(int id:{MODE,URL}) EnableWindow(GetDlgItem(window,id),!active && !playing);
  for(int id:{NAME,LOCATION,SAVE_PROFILE}) EnableWindow(GetDlgItem(window,id),!playing);
  SendMessageW(GetDlgItem(window,CODE),EM_SETREADONLY,TRUE,0);
  EnableWindow(GetDlgItem(window,GO_ONLINE),!playing);
  EnableWindow(GetDlgItem(window,REQUEST),active && !playing && can_play && !selected(GetDlgItem(window,PLAYERS),rows).empty() && selected(GetDlgItem(window,PLAYERS),rows)!=me);
  // The same rule as Send Match Request; why a player cannot be asked this way shows when it is pressed.
  EnableWindow(GetDlgItem(window,P2P_REQUEST),active && !playing && can_play && !selected(GetDlgItem(window,PLAYERS),rows).empty() && selected(GetDlgItem(window,PLAYERS),rows)!=me);
  EnableWindow(GetDlgItem(window,ACCEPT),active && !playing && can_play);
  // Invite to Match: a friend who is online now, while this PC can take a match. Friends can play
  // while out of the public lobby, so it does not wait for Go Online.
  {
    const int pick=(int)SendMessageW(GetDlgItem(window,FRIENDS),LB_GETCURSEL,0,0);
    const bool online_friend=pick>=0 && pick<(int)friends.size() && !friends[pick].value("incoming",false) &&
                             friends[pick].value("online",false);
    EnableWindow(GetDlgItem(window,INVITE_FRIEND),online_friend && !playing && can_play);
    // Private chat needs no game setup, only the other player online (peer-to-peer lobby only).
    const bool peer_lobby=peer_mode(local_profile);
    const std::string chosen=selected(GetDlgItem(window,PLAYERS),rows);
    EnableWindow(GetDlgItem(window,PM_FRIEND),online_friend && peer_lobby);
    EnableWindow(GetDlgItem(window,PM_PLAYER),active && peer_lobby && !chosen.empty() && chosen!=me);
  }
#ifdef MELEE_NO_SLIPPI
  if(!can_play && !playing) label(STATUS,launcher::lang::tr("lobby.p2p.setup_hint"));
#else
  if(!can_play && !playing) label(STATUS,p2p_matches?launcher::lang::tr("lobby.p2p.setup_hint"):std::string("To play, finish disc and Slippi setup on the Play tab."));
  // The code field's caption follows what the code is: the account's, or the one made from the name.
  if(!p2p_matches) label(PROFILE_CODE,hosted_code.empty()?launcher::lang::tr("lobby.profile.code"):std::string("Slippi connect code (linked)"));
#endif
  // Copy code sits next to "Can't reach X. Use Slippi Direct with their code" while the status says it.
  copy_button_code=(can_play || playing)?copy:std::string();
  label(PLAYER_HEADING,launcher::lang::tr("lobby.players_online",{{"count",std::to_string(roster_all.size())}}));
  EnableWindow(GetDlgItem(window,ADD_FRIEND),TRUE);
  {
    // Send: the public chat while online, a private room while it is open.
    const Json* room=active_room.empty()?nullptr:find_room(active_room);
    EnableWindow(GetDlgItem(window,SEND),active_room.empty()?active:(room && room->value("state",std::string())=="open"));
  }
  process_invites(state,ping,me,playing);
  process_private(state);
  for(int i=0;i<26;++i) EnableWindow(GetDlgItem(window,CHARACTER_FIRST+i),!playing);
  EnableWindow(GetDlgItem(window,p2p_matches?P2P_FIGHTER:P2P_FIGHTER_TEST),!playing);
  layout_all();
  // The page paints its empty states (the badge and text of an empty list) itself. When a list
  // fills or empties, or a private room opens, what was painted is out of date in the strips no
  // control covers: the badge's top edge stayed above the first player. Repaint the page then.
  {
    static std::string painted;
    std::string now=std::to_string(lobby_tab)+(rows.empty()?"r":"R")+(chat_messages.empty()?"c":"C")+(friends.empty()?"f":"F")+
                    (history.empty()?"h":"H")+(requests.empty()?"q":"Q")+"|"+active_room+"|"+std::to_string(private_rooms.size());
    if(painted!=now) { painted=now; InvalidateRect(window,nullptr,FALSE); }
  }
}
void prefs_updated() {
  label(OPEN_TO,launcher::lang::tx("Open to:")+" "+open_summary());
  if(prefs_changed) prefs_changed();
  announce();
}
// The character button of the Profile page (peer-to-peer matches): what this player's matches are
// played with. "First main" follows the mains below it; a color the character does not have falls
// back to its first. The choice is saved at once and goes to the lobby with the profile, which
// puts it into the next match setup (launcher_lobby_p2p.cpp, p2p_mine).
int fighter_character() { return p2p_character>=0?p2p_character:selected_mains.empty()?2:selected_mains[0]; }
int fighter_button() { return p2p_matches?P2P_FIGHTER:P2P_FIGHTER_TEST; }   // the one of the two this build creates
std::string fighter_caption() {
  const int ch=fighter_character(), colors=p2p_color_count(ch);
  const std::string who=p2p_character>=0?std::string(characters[ch])
                                        :launcher::lang::tr("lobby.p2p.fighter_first",{{"character",std::string(characters[ch])}});
  return launcher::lang::tr("lobby.p2p.fighter_button",{{"character",who},{"number",std::to_string((p2p_color<colors?p2p_color:0)+1)}});
}
void choose_fighter(HWND w) {
  enum { FIRST_MAIN=1, CHARACTER=100, COLOR=200 };
  const int ch=fighter_character(), colors=p2p_color_count(ch), color=p2p_color<colors?p2p_color:0;
  HMENU menu=CreatePopupMenu(), shades=CreatePopupMenu();
  for(int k=0;k<colors;++k)
    AppendMenuW(shades,MF_STRING|(k==color?MF_CHECKED:0),COLOR+k,wide(launcher::lang::tr("lobby.p2p.color",{{"number",std::to_string(k+1)}})).c_str());
  AppendMenuW(menu,MF_POPUP,(UINT_PTR)shades,wide(launcher::lang::tr("lobby.p2p.color_menu")).c_str());
  AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
  const int first=selected_mains.empty()?2:selected_mains[0];
  AppendMenuW(menu,MF_STRING|(p2p_character<0?MF_CHECKED:0),FIRST_MAIN,
              wide(launcher::lang::tr("lobby.p2p.fighter_first",{{"character",std::string(characters[first])}})).c_str());
  // Two columns: twenty-six names in one would not fit a small screen.
  for(int i=0;i<26;++i)
    AppendMenuW(menu,MF_STRING|(p2p_character==i?MF_CHECKED:0)|(i==12?MF_MENUBARBREAK:0),CHARACTER+i,wide(characters[i]).c_str());
  RECT anchor{}; GetWindowRect(GetDlgItem(w,fighter_button()),&anchor);
  const int pick=(int)TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN|TPM_TOPALIGN,anchor.left,anchor.bottom,0,w,nullptr);
  DestroyMenu(menu);   // the color menu goes with it
  if(pick<=0) return;
  if(pick==FIRST_MAIN) p2p_character=-1;
  else if(pick>=CHARACTER && pick<CHARACTER+26) p2p_character=pick-CHARACTER;
  else if(pick>=COLOR && pick<COLOR+colors) p2p_color=pick-COLOR;
  if(p2p_color>=p2p_color_count(fighter_character())) p2p_color=0;
  { std::lock_guard<std::mutex> lock(mutex); config["p2p_ch"]=p2p_character; config["p2p_col"]=p2p_color; save(config); }
  label(fighter_button(),fighter_caption());
  announce();
}
// The Open to button: which versions this player takes match requests for. Mods not on this PC
// are shown greyed with the reason; a custom ISO is chosen here and matched by content, not name.
void choose_open_to(HWND w) {
  enum { VANILLA=1, AKANEIA, ACE, CUSTOM, PICK_CUSTOM, CLEAR_CUSTOM };
  const auto open=open_list();
  auto is_open=[&](const char* m){ return std::find(open.begin(),open.end(),m)!=open.end(); };
  HMENU menu=CreatePopupMenu();
  auto item=[&](UINT id,const std::string& text,bool checked,bool enabled){
    AppendMenuW(menu,MF_STRING|(checked?MF_CHECKED:0)|(enabled?0:MF_GRAYED),id,wide(text).c_str());
  };
  item(VANILLA,launcher::lang::tx("Vanilla"),is_open("vanilla"),true);
  const bool akaneia=mod_has.count("akaneia")!=0, ace=mod_has.count("ace")!=0;
  item(AKANEIA,akaneia?"Akaneia "+mod_has["akaneia"].get<std::string>():"Akaneia ("+launcher::lang::tx("not installed")+")",is_open("akaneia") && akaneia,akaneia);
  item(ACE,ace?"ACE "+mod_has["ace"].get<std::string>():"ACE ("+launcher::lang::tx("not installed")+")",is_open("ace") && ace,ace);
  const bool custom=!custom_hash.empty();
  item(CUSTOM,custom?current_prefs.custom_name:launcher::lang::tx("Custom ISO"),is_open("custom") && custom,custom);
  AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
  item(PICK_CUSTOM,launcher::lang::tx(custom?"Choose a different custom ISO...":"Choose a custom ISO..."),false,true);
  if(custom) item(CLEAR_CUSTOM,launcher::lang::tx("Remove the custom ISO"),false,true);
  RECT anchor{}; GetWindowRect(GetDlgItem(w,OPEN_TO),&anchor);
  const int pick=(int)TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN|TPM_TOPALIGN,anchor.left,anchor.bottom,0,w,nullptr);
  DestroyMenu(menu);
  if(pick<=0) return;
  auto next=open;
  auto toggle=[&](const char* m){
    auto it=std::find(next.begin(),next.end(),m);
    if(it!=next.end()) next.erase(it); else next.push_back(m);
  };
  if(pick==VANILLA) toggle("vanilla");
  else if(pick==AKANEIA) toggle("akaneia");
  else if(pick==ACE) toggle("ace");
  else if(pick==CUSTOM) toggle("custom");
  else if(pick==CLEAR_CUSTOM) {
    { std::lock_guard<std::mutex> lock(mutex); current_prefs.custom_iso.clear(); current_prefs.custom_name.clear(); custom_hash.clear(); }
    next.erase(std::remove(next.begin(),next.end(),std::string("custom")),next.end());
  } else if(pick==PICK_CUSTOM) {
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{}; ofn.lStructSize=sizeof ofn; ofn.hwndOwner=owner;
    ofn.lpstrFilter=L"GameCube disc image (*.iso;*.gcm)\0*.iso;*.gcm\0All files\0*.*\0";
    ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH; ofn.Flags=OFN_FILEMUSTEXIST;
    if(!GetOpenFileNameW(&ofn)) return;
    const std::string path=utf8(file);
    // The name other players see: the file's name, plain text, at most 32 characters.
    std::wstring stem=std::filesystem::path(file).stem().wstring();
    std::string name;
    for(wchar_t c:stem) { if(c>=32 && c<127) name+=(char)c; else if(c>=127) name+='?'; if(name.size()>=32) break; }
    while(!name.empty() && name.back()==' ') name.pop_back();
    if(name.empty()) name="Custom ISO";
    const std::string hash=content_hash16(path);
    if(hash.empty()) { label(STATUS,"That file could not be read."); return; }
    { std::lock_guard<std::mutex> lock(mutex); current_prefs.custom_iso=path; current_prefs.custom_name=name; custom_hash=hash; }
    if(std::find(next.begin(),next.end(),std::string("custom"))==next.end()) next.push_back("custom");
  }
  if(next.empty()) next.push_back("vanilla");
  { std::lock_guard<std::mutex> lock(mutex); current_prefs.open_to=join_list(next); }
  prefs_updated();
}
void set_filter(const std::string& filter) {
  if(roster_filter==filter) return;
  roster_filter=filter; label(PLAYER_FILTER,filter_caption()); refresh();
}
// The Show button over the Players list: everyone, or the players who take requests for one version.
void choose_filter(HWND w) {
  HMENU menu=CreatePopupMenu();
  for(UINT i=0;i<5;++i)
    AppendMenuW(menu,MF_STRING|(roster_filter==roster_filters[i]?MF_CHECKED:0),i+1,wide(filter_title(roster_filters[i])).c_str());
  RECT anchor{}; GetWindowRect(GetDlgItem(w,PLAYER_FILTER),&anchor);
  const int pick=(int)TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN|TPM_TOPALIGN,anchor.left,anchor.bottom,0,w,nullptr);
  DestroyMenu(menu);
  if(pick>=1 && pick<=5) set_filter(roster_filters[pick-1]);
}
// Puts text on the clipboard (the code from "Can't reach X"). False when another program has it open.
bool copy_text(HWND w,const std::wstring& value) {
  if(!OpenClipboard(w)) return false;
  bool copied=false;
  if(EmptyClipboard()) {
    const size_t bytes=(value.size()+1)*sizeof(wchar_t);
    if(HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes)) {
      if(void* target=GlobalLock(memory)) {
        std::memcpy(target,value.c_str(),bytes); GlobalUnlock(memory);
        copied=SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;
      }
      if(!copied) GlobalFree(memory);   // the clipboard owns the memory only once SetClipboardData succeeds
    }
  }
  CloseClipboard();
  return copied;
}
// Send Match Request (a player in the list) and Invite to Match (a friend): the version is chosen
// from those both players can play before anything is sent.
void send_request(HWND w,int button,const std::string& target,const Json& them,bool p2p=false) {
  // P2P Direct (the test control): vanilla only, so there is no version to pick. The lobby checks the
  // rest (the other launcher, Source Port on both sides) and says what is missing.
  if(p2p) { enqueue("request",{{"target",target},{"mode","vanilla"},{"transport",kP2pTransport}}); return; }
  const Json mine=own_profile();
  const auto modes=common_modes(mine,them);
  // Vanilla alone goes straight out. When a mod is possible too (or only a mod), the player
  // picks; with nothing in common the request still goes out as vanilla so the reason shows.
  std::string mode=modes.empty()?"vanilla":modes[0];
  if(modes.size()>1 || (modes.size()==1 && modes[0]!="vanilla")) {
    HMENU menu=CreatePopupMenu();
    for(size_t i=0;i<modes.size();++i) AppendMenuW(menu,MF_STRING,(UINT_PTR)(i+1),wide(mode_title(modes[i],mine,them)).c_str());
    RECT anchor{}; GetWindowRect(GetDlgItem(w,button),&anchor);
    const int pick=(int)TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN|TPM_BOTTOMALIGN,anchor.left,anchor.top,0,w,nullptr);
    DestroyMenu(menu);
    if(pick<=0) return;
    mode=modes[(size_t)pick-1];
  }
  enqueue("request",{{"target",target},{"mode",mode}});
}
LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
  if(msg==WM_CREATE) {
    window=w; displayed_history=Json();
    NONCLIENTMETRICSW metrics{sizeof metrics}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof metrics,&metrics,0);
    LOGFONTW font=metrics.lfMessageFont; font.lfQuality=CLEARTYPE_QUALITY;
    font.lfHeight=-U(12); ui_font=CreateFontIndirectW(&font);
    font.lfWeight=FW_SEMIBOLD; ui_name=CreateFontIndirectW(&font); font.lfWeight=FW_NORMAL;
    font.lfHeight=-U(11); ui_small=CreateFontIndirectW(&font);
    font.lfWeight=FW_BOLD; ui_small_bold=CreateFontIndirectW(&font); font.lfWeight=FW_NORMAL;
    font.lfHeight=-U(21); font.lfWeight=FW_SEMIBOLD; ui_title=CreateFontIndirectW(&font);
    ui_brush=CreateSolidBrush(ui_panel); ui_background=CreateSolidBrush(ui_bg); ui_field_brush=CreateSolidBrush(ui_field);
    auto control=[&](int id,const wchar_t* type,const wchar_t* title,DWORD style=0) { add(w,id,type,title,0,0,0,0,style); };
    control(GO_ONLINE,L"BUTTON",L"Go Online");
    control(ONLINE_HINT,L"STATIC",L"Go Online shows your name to other players and lets them send you match requests while the launcher is open.");
    control(TAB_CHAT,L"BUTTON",L"Back to Chat"); control(TAB_FRIENDS,L"BUTTON",L"Friends");
    control(TAB_HISTORY,L"BUTTON",L"History"); control(TAB_PROFILE,L"BUTTON",L"Profile");
    control(PLAYER_HEADING,L"STATIC",L"Players online (0)");
    control(PLAYER_FILTER,L"BUTTON",L"");
    for(int id:{PLAYERS,FRIENDS,REQUESTS,HISTORY}) control(id,L"LISTBOX",L"",WS_VSCROLL);
    control(REQUEST,L"BUTTON",L"Send Match Request"); control(ADD_FRIEND,L"BUTTON",L"Add Friend");
    control(ACCEPT,L"BUTTON",L"Accept Match"); control(DECLINE,L"BUTTON",L"Decline / Cancel");
    control(FRIEND_ACCEPT,L"BUTTON",L"Accept Friend"); control(FRIEND_DECLINE,L"BUTTON",L"Decline");
    control(INVITE_FRIEND,L"BUTTON",launcher::lang::trw("lobby.invite").c_str());
    control(REMOVE_FRIEND,L"BUTTON",L"Remove Friend");
    control(PM_PLAYER,L"BUTTON",launcher::lang::trw("lobby.pm.button").c_str());
    control(PM_FRIEND,L"BUTTON",launcher::lang::trw("lobby.pm.button").c_str());
    control(CHATLOG,L"LISTBOX",L"",WS_VSCROLL);
    control(CHAT,L"EDIT",L"",ES_AUTOHSCROLL); SetWindowSubclass(GetDlgItem(w,CHAT),chat_proc,1,0); SendMessageW(GetDlgItem(w,CHAT),EM_SETLIMITTEXT,300,0);
    SendMessageW(GetDlgItem(w,CHAT),EM_SETCUEBANNER,TRUE,(LPARAM)launcher::lang::txw(L"Send a message...").c_str());
    control(SEND,L"BUTTON",L"Send"); control(EMOJI,L"BUTTON",L"Emoji");
    for(int i=0;i<8;++i) {
      emoji_icons[i]=(HICON)LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(300+i),IMAGE_ICON,U(24),U(24),LR_SHARED);
      emoji_icon_offsets[i]=visible_icon_offset(emoji_icons[i]);
    }
#ifdef MELEE_NO_SLIPPI   // the code controls get their text further down, with the other peer-to-peer controls
    control(FRIEND_CODE,L"EDIT",L"",ES_AUTOHSCROLL);
    control(FRIEND_SEND,L"BUTTON",L"Send Friend Request"); control(FRIEND_HINT,L"STATIC",L""); control(STATUS,L"STATIC",L""); control(RECORD,L"STATIC",L"");
    control(COPY_CODE,L"BUTTON",launcher::lang::trw("lobby.copy_code").c_str());
    control(PROFILE_NAME,L"STATIC",L"Display name"); control(PROFILE_CODE,L"STATIC",L"");
#else
    control(FRIEND_CODE,L"EDIT",L"",ES_AUTOHSCROLL); SendMessageW(GetDlgItem(w,FRIEND_CODE),EM_SETCUEBANNER,TRUE,(LPARAM)launcher::lang::txw(L"Slippi code, e.g. FOX#123").c_str());
    control(FRIEND_SEND,L"BUTTON",L"Send Friend Request"); control(FRIEND_HINT,L"STATIC",L"Enter an online player's Slippi code."); control(STATUS,L"STATIC",L""); control(RECORD,L"STATIC",L"");
    control(COPY_CODE,L"BUTTON",launcher::lang::trw("lobby.copy_code").c_str());
    control(PROFILE_NAME,L"STATIC",L"Display name"); control(PROFILE_CODE,L"STATIC",L"Slippi connect code (linked)");
#endif
    control(PROFILE_LOCATION,L"STATIC",L"Location"); control(PROFILE_MAINS,L"STATIC",L"Your three mains");
    control(PROFILE_MODE,L"STATIC",L"Connection"); control(URL_LABEL,L"STATIC",L"Bootstrap peer (optional)");
    for(int id:{NAME,CODE,LOCATION,URL}) control(id,L"EDIT",L"",ES_AUTOHSCROLL);
    SendMessageW(GetDlgItem(w,CODE),EM_SETREADONLY,TRUE,0);
#ifndef MELEE_NO_SLIPPI
    SendMessageW(GetDlgItem(w,CODE),EM_SETCUEBANNER,TRUE,(LPARAM)launcher::lang::txw(L"Sign in through Slippi Launcher").c_str());
#endif
    HWND tips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,w,nullptr,GetModuleHandleW(nullptr),nullptr);
    lobby_tips=tips;
    for(int i=0;i<26;++i) {
      stock_names[i]=wide(characters[i]); control(CHARACTER_FIRST+i,L"BUTTON",stock_names[i].c_str());
      stock_icons[i]=(HICON)LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(100+i),IMAGE_ICON,U(24),U(24),LR_DEFAULTCOLOR);
      stock_icon_offsets[i]=visible_icon_offset(stock_icons[i]);
      TOOLINFOW tip{sizeof tip}; tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS; tip.hwnd=w; tip.uId=(UINT_PTR)GetDlgItem(w,CHARACTER_FIRST+i); tip.lpszText=stock_names[i].data(); SendMessageW(tips,TTM_ADDTOOLW,0,(LPARAM)&tip);
    }
    for(int id:{PLAYERS,CHATLOG}) {
      TOOLINFOW tip{sizeof tip};tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tip.hwnd=w;
      tip.uId=(UINT_PTR)GetDlgItem(w,id);tip.lpszText=LPSTR_TEXTCALLBACKW;
      SendMessageW(tips,TTM_ADDTOOLW,0,(LPARAM)&tip);
    }
    SendMessageW(tips,TTM_SETMAXTIPWIDTH,0,U(260));
    control(MODE,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    SendMessageW(GetDlgItem(w,MODE),CB_ADDSTRING,0,(LPARAM)L"Peer-to-peer (no service)");
    SendMessageW(GetDlgItem(w,MODE),CB_ADDSTRING,0,(LPARAM)L"Hosted lobby service");
    control(SAVE_PROFILE,L"BUTTON",L"Save Profile");
    control(AUTO_REJECT,L"BUTTON",L"Auto reject invites: Off");
    control(REQUEST_SOUND,L"BUTTON",L"Request sound: On");
    control(OPEN_TO,L"BUTTON",L"Open to: Vanilla");
    control(VOLUME_LABEL,L"STATIC",L"Volume 65%");

    if(p2p_matches) {
      // No Slippi account here: the code is made from the name, and players who cannot find each
      // other through the public network connect with an invite line.
      SetWindowTextW(GetDlgItem(w,PROFILE_CODE),launcher::lang::trw("lobby.profile.code").c_str());
      SendMessageW(GetDlgItem(w,CODE),EM_SETCUEBANNER,TRUE,(LPARAM)L"");
      SendMessageW(GetDlgItem(w,FRIEND_CODE),EM_SETCUEBANNER,TRUE,(LPARAM)launcher::lang::trw("lobby.friend.code_cue").c_str());
      SetWindowTextW(GetDlgItem(w,FRIEND_HINT),launcher::lang::trw("lobby.friend.code_hint").c_str());
      control(INVITE_EDIT,L"EDIT",L"",ES_AUTOHSCROLL); SendMessageW(GetDlgItem(w,INVITE_EDIT),EM_SETLIMITTEXT,600,0);
      SendMessageW(GetDlgItem(w,INVITE_EDIT),EM_SETCUEBANNER,TRUE,(LPARAM)launcher::lang::trw("lobby.invite.cue").c_str());
      control(INVITE_CONNECT,L"BUTTON",launcher::lang::trw("lobby.invite.connect").c_str());
      control(INVITE_COPY,L"BUTTON",launcher::lang::trw("lobby.invite.copy").c_str());
      control(FIND_MATCH,L"BUTTON",launcher::lang::trw("lobby.search.button").c_str());
      control(BLOCK_PLAYER,L"BUTTON",launcher::lang::trw("lobby.block.button").c_str());
      // In the Profile page's slot that Open to has in the other build (nothing but vanilla is
      // played peer to peer, so that button is never shown here).
      control(P2P_FIGHTER,L"BUTTON",L"");
    } else {
      // Both kinds of match: P2P Direct is a test control beside Go Online, with the character
      // and color its matches are played with (a hosted match picks its character in the game).
      control(P2P_REQUEST,L"BUTTON",launcher::lang::trw("lobby.p2p.test_button").c_str());
      control(P2P_FIGHTER_TEST,L"BUTTON",L"");
      static std::wstring p2p_tip; p2p_tip=launcher::lang::trw("lobby.p2p.test_tip");
      TOOLINFOW tip{sizeof tip}; tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS; tip.hwnd=w; tip.uId=(UINT_PTR)GetDlgItem(w,P2P_REQUEST); tip.lpszText=p2p_tip.data(); SendMessageW(tips,TTM_ADDTOOLW,0,(LPARAM)&tip);
    }

    control(EMPTY_PLAYERS,L"STATIC",L"No players here yet\nGo online to discover players.",SS_CENTER);
    control(EMPTY_FRIENDS,L"STATIC",L"Your friends will appear here.\nSelect a player, then Add Friend.",SS_CENTER);
    control(EMPTY_CHAT,L"STATIC",L"Welcome to the lobby\nGo online and start a conversation.",SS_CENTER);
    Json cfg; { std::lock_guard<std::mutex> lock(mutex); cfg=config; }
    bool peer=peer_mode(cfg);
    SendMessageW(GetDlgItem(w,MODE),CB_SETCURSEL,peer?0:1,0);
    label(URL_LABEL,peer?"Bootstrap peer (optional)":"Lobby service URL");
    label(URL,cfg.value("url",std::string())); label(NAME,cfg.value("name",account_name));
    label(CODE,account_code); label(LOCATION,cfg.value("location",std::string()));
    auto mains=cfg.value("mains",Json::array({2,20,9}));
    selected_mains=mains.get<std::vector<int>>();
    auto_reject=cfg.value("auto_reject_invites",false);
    sound_enabled=cfg.value("request_sound",true);
    sound_volume=cfg.value("request_volume",65);
    label(AUTO_REJECT,auto_reject?"Auto reject invites: On":"Auto reject invites: Off");
    label(REQUEST_SOUND,sound_enabled?"Request sound: On":"Request sound: Off");
    label(VOLUME_LABEL,launcher::lang::fill(launcher::lang::tx("Volume {percent}%"),launcher::lang::Args{{"percent",std::to_string(sound_volume)}}));
    label(OPEN_TO,launcher::lang::tx("Open to:")+" "+open_summary());
    label(PLAYER_FILTER,filter_caption());
    label(PROFILE_MAINS,launcher::lang::fill(launcher::lang::tx("Main characters ({count}/3)"),launcher::lang::Args{{"count",std::to_string(selected_mains.size())}}));
    label(fighter_button(),fighter_caption());
    SetTimer(w,1,1000,nullptr); layout_all(); refresh(); return 0;
  }
  if(msg==WM_SIZE) { layout_all(); return 0; }
  if(msg==WM_ERASEBKGND) return 1;
  if(msg==WM_PAINT) { paint_lobby(w); return 0; }
  // The launcher's test captures (WM_APP+41) print every page off screen, this one included.
  if(msg==WM_PRINTCLIENT) { paint_lobby(w,(HDC)wp); return 0; }
  // Test captures: the Show filter without its popup menu (wParam 0 to 4: All, Vanilla, Akaneia,
  // ACE, Custom ISO), since a menu would open on the visible desktop.
  if(msg==WM_APP+42) { if(wp<5) set_filter(roster_filters[wp]); return 0; }
  // Test runs: a match request without the version menu. wParam 0: the selected player, 1: the
  // selected friend; lParam 1 Vanilla, 2 Akaneia, 3 ACE, 4 the custom ISO.
  if(msg==WM_APP+43) {
    const std::string target=wp==0?selected(GetDlgItem(w,PLAYERS),rows):selected(GetDlgItem(w,FRIENDS),friends);
    std::string mode;
    if(lp>=1 && lp<=3) mode=roster_filters[lp]; else if(lp==4 && !custom_hash.empty()) mode="custom:"+custom_hash;
    if(!target.empty() && target!=self && !mode.empty()) enqueue("request",{{"target",target},{"mode",mode}});
    return 0;
  }
  if(msg==WM_DRAWITEM) {
    // Drawn off screen and copied in one step: drawn straight into the control, each repaint showed
    // the background fill before the row or button itself, which read as flicker.
    DRAWITEMSTRUCT* d=(DRAWITEMSTRUCT*)lp; const RECT r=d->rcItem; const int cw=r.right-r.left, ch=r.bottom-r.top;
    HDC mem=cw>0&&ch>0?CreateCompatibleDC(d->hDC):nullptr; HBITMAP bmp=mem?CreateCompatibleBitmap(d->hDC,cw,ch):nullptr;
    if(!bmp) { if(mem) DeleteDC(mem); draw_control(d); return TRUE; }
    auto old=SelectObject(mem,bmp); SetViewportOrgEx(mem,-r.left,-r.top,nullptr);
    DRAWITEMSTRUCT copy=*d; copy.hDC=mem; draw_control(&copy);
    SetViewportOrgEx(mem,0,0,nullptr); BitBlt(d->hDC,r.left,r.top,cw,ch,mem,0,0,SRCCOPY);
    SelectObject(mem,old); DeleteObject(bmp); DeleteDC(mem);
    return TRUE;
  }
  if(msg==WM_MEASUREITEM) { ((MEASUREITEMSTRUCT*)lp)->itemHeight=U(((MEASUREITEMSTRUCT*)lp)->CtlID==CHATLOG?60:76); return TRUE; }
  if(msg==WM_NOTIFY && ((NMHDR*)lp)->code==TTN_GETDISPINFOW) {
    auto* tip=(NMTTDISPINFOW*)lp;HWND list=(HWND)tip->hdr.idFrom;
    int id=GetDlgCtrlID(list);hover_code.clear();
    if(id==PLAYERS || id==CHATLOG) {
      POINT point{};GetCursorPos(&point);ScreenToClient(list,&point);
      auto result=SendMessageW(list,LB_ITEMFROMPOINT,0,MAKELPARAM(point.x,point.y));
      int index=LOWORD(result);
      if(!HIWORD(result)) {
        std::string code;
        if(id==PLAYERS && index<(int)rows.size()) code=rows[index].value("code",std::string());
        else if(id==CHATLOG && index<(int)chat_messages.size()) {
          auto sender=chat_messages[index].value("sender",std::string());
          for(const auto& player:roster_all) if(player.value("id",std::string())==sender) {code=player.value("code",std::string());break;}
        }
#ifdef MELEE_NO_SLIPPI
        hover_code=wide(code);
#else
        if(p2p_matches) hover_code=wide(code);
        else hover_code=wide(code.empty()?launcher::lang::tx("Slippi code unavailable"):launcher::lang::tx("Slippi code:")+" "+code);
#endif
      }
      tip->lpszText=hover_code.empty()?const_cast<wchar_t*>(L""):hover_code.data();
    }
    return 0;
  }
  if(msg==WM_CTLCOLORSTATIC || msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX) {
    int id=GetDlgCtrlID((HWND)lp); bool background=id==ONLINE_HINT||id==STATUS;
    SetTextColor((HDC)wp,(id==ONLINE_HINT||id==STATUS||id==EMPTY_CHAT||id==EMPTY_FRIENDS||id==EMPTY_PLAYERS)?ui_dim:ui_text);
    bool field=id==CHAT||id==NAME||id==CODE||id==LOCATION||id==URL||id==FRIEND_CODE||id==INVITE_EDIT;
    SetBkColor((HDC)wp,background?ui_bg:field?ui_field:ui_panel); return (LRESULT)(background?ui_background:field?ui_field_brush:ui_brush);
  }
  if((msg==WM_LBUTTONDOWN || msg==WM_MOUSEMOVE || msg==WM_LBUTTONUP) && lobby_tab==3) {
    const int x=MulDiv(GET_X_LPARAM(lp),96,owner?GetDpiForWindow(owner):96);
    const int y=MulDiv(GET_Y_LPARAM(lp),96,owner?GetDpiForWindow(owner):96);
    if(msg==WM_LBUTTONDOWN && x>=slider_left-10 && x<=slider_right+10 && y>=slider_y-12 && y<=slider_y+17) {
      slider_drag=true; SetCapture(w);
    }
    if(slider_drag) {
      if(msg==WM_LBUTTONDOWN || msg==WM_MOUSEMOVE) {
        int next=std::clamp((x-slider_left)*100/(slider_right-slider_left),0,100);
        if(next!=sound_volume) {
          sound_volume=next; label(VOLUME_LABEL,launcher::lang::fill(launcher::lang::tx("Volume {percent}%"),launcher::lang::Args{{"percent",std::to_string(next)}}));
          RECT dirty{U(slider_left-12),U(slider_y-15),U(slider_right+12),U(slider_y+22)};
          InvalidateRect(w,&dirty,FALSE);
        }
      }
      if(msg==WM_LBUTTONUP) {
        slider_drag=false; ReleaseCapture();
        { std::lock_guard<std::mutex> lock(mutex); config["request_volume"]=sound_volume; save(config); }
      }
      return 0;
    }
  }
  if(msg==WM_DESTROY) { KillTimer(w,1); if(emoji_popup) DestroyWindow(emoji_popup); DeleteObject(ui_font); DeleteObject(ui_small); DeleteObject(ui_small_bold); DeleteObject(ui_title); DeleteObject(ui_brush); DeleteObject(ui_background); DeleteObject(ui_field_brush); DeleteObject(ui_name); for(auto icon:stock_icons) if(icon) DestroyIcon(icon); lobby_tips=nullptr; return 0; }
  // Timer 2: one early refresh after a chat action, so its result shows without waiting a second.
  if(msg==WM_TIMER) { if(wp==2) KillTimer(w,2); refresh(); return 0; }
  // The private chat tabs (painted by paint_room_tabs): click to bring a room to the front, the
  // close mark to close it, right click for the menu with Block.
  if((msg==WM_LBUTTONDOWN || msg==WM_RBUTTONUP) && lobby_tab==0) {
    const POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
    for(size_t i=0;i<room_tab_hits.size() && i<room_tab_ids.size() && i<room_close_hits.size();++i) {
      if(!PtInRect(&room_tab_hits[i],pt)) continue;
      const std::string room=room_tab_ids[i];
      if(msg==WM_LBUTTONDOWN) {
        if(!room.empty() && PtInRect(&room_close_hits[i],pt)) close_room(room,false);
        else show_room(room);
      } else if(!room.empty()) room_menu(w,room,pt);
      refresh(); RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
      if(msg==WM_LBUTTONDOWN && active_room==room) SetFocus(GetDlgItem(w,CHAT));
      return 0;
    }
  }
  if(msg==WM_COMMAND) {
    int id=LOWORD(wp);
    if(id==EMOJI && HIWORD(wp)==BN_CLICKED) {show_emoji_picker();return 0;}
    if(id==ADD_FRIEND && HIWORD(wp)==BN_CLICKED) {
      auto target=selected(GetDlgItem(w,PLAYERS),rows);
      if(!target.empty()&&target!=self) { enqueue("friend",{{"target",target}}); label(STATUS,"Friend request sent."); }
      else { lobby_tab=1; adding_friend=true; layout_all(); RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN); SetFocus(GetDlgItem(w,FRIEND_CODE)); }
      return 0;
    }
    if(id==FRIEND_SEND && HIWORD(wp)==BN_CLICKED) {
      const std::string code=normalize_code(text(FRIEND_CODE));
#ifdef MELEE_NO_SLIPPI
      if(!valid_code(code)) { label(FRIEND_HINT,launcher::lang::tr("lobby.friend.code_hint")); return 0; }
#else
      if(!valid_code(code)) { label(FRIEND_HINT,p2p_matches?launcher::lang::tr("lobby.friend.code_hint"):std::string("Enter a Slippi code such as FOX#123.")); return 0; }
#endif
      if(code==account_code) { label(FRIEND_HINT,"That is your own code."); return 0; }
      for(const auto& player:rows) if(!player.value("is_self",false) && player.value("code",std::string())==code) { enqueue("friend",{{"target",player["id"]}}); label(FRIEND_HINT,"Friend request sent. Waiting for acceptance."); return 0; }
      // Not in the public list: the peer lobby looks the code up directly, so a player who never
      // went online in the public lobby is found too (while their launcher is open).
      Json cfg; { std::lock_guard<std::mutex> lock(mutex); cfg=config; }
      if(peer_mode(cfg)) { enqueue("friend_code",{{"code",code}}); label(FRIEND_HINT,launcher::lang::fill(launcher::lang::tx("Looking for {code}. They get the request when their launcher is open."),launcher::lang::Args{{"code",code}})); return 0; }
      label(FRIEND_HINT,"Player not found yet. Both players must be online in the lobby."); return 0;
    }
    if(id>=TAB_CHAT && id<=TAB_PROFILE && HIWORD(wp)==BN_CLICKED) { lobby_tab=id-TAB_CHAT; layout_all(); RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN); return 0; }
    if(id>=CHARACTER_FIRST && id<CHARACTER_FIRST+26 && HIWORD(wp)==BN_CLICKED) {
      int character=id-CHARACTER_FIRST; auto found=std::find(selected_mains.begin(),selected_mains.end(),character);
      if(found!=selected_mains.end()) selected_mains.erase(found);
      else if(selected_mains.size()<3) selected_mains.push_back(character);
      label(PROFILE_MAINS,launcher::lang::fill(launcher::lang::tx("Main characters ({count}/3)"),launcher::lang::Args{{"count",std::to_string(selected_mains.size())}}));
      label(fighter_button(),fighter_caption());   // "First main" names the first of these
      InvalidateRect(GetDlgItem(w,id),nullptr,FALSE); return 0;
    }
    if(id==AUTO_REJECT && HIWORD(wp)==BN_CLICKED) {
      auto_reject=!auto_reject; { std::lock_guard<std::mutex> lock(mutex); config["auto_reject_invites"]=auto_reject; save(config); }
      label(AUTO_REJECT,auto_reject?"Auto reject invites: On":"Auto reject invites: Off"); refresh(); return 0;
    }
    if(id==REQUEST_SOUND && HIWORD(wp)==BN_CLICKED) {
      sound_enabled=!sound_enabled; { std::lock_guard<std::mutex> lock(mutex); config["request_sound"]=sound_enabled; save(config); }
      label(REQUEST_SOUND,sound_enabled?"Request sound: On":"Request sound: Off"); return 0;
    }
    if(id==SAVE_PROFILE && HIWORD(wp)==BN_CLICKED) {
      if(selected_mains.empty()) { label(STATUS,"Select at least one main."); return 0; }
      auto cfg=own_profile(); bool online;
      { std::lock_guard<std::mutex> lock(mutex); online=go_online_requested; config=cfg; save(config); notice="Profile saved."; }
      if(online) enqueue("join",cfg);
      lobby_tab=0; refresh(); RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN); return 0;
    }
    if(id==CHATLOG && HIWORD(wp)==LBN_SELCHANGE) {
      int index=(int)SendMessageW(GetDlgItem(w,CHATLOG),LB_GETCURSEL,0,0);
      if(index>=0 && index<(int)chat_messages.size() && !chat_messages[index].value("system",false)) {
        auto sender=chat_messages[index].value("sender",std::string());
        // A player the Show filter hides: show everyone again, then select them.
        auto listed=[&](const Json& list){ for(const auto& p:list) if(p.value("id",std::string())==sender) return true; return false; };
        if(!listed(rows) && listed(roster_all)) set_filter("all");
        for(int i=0;i<(int)rows.size();++i) if(rows[i].value("id",std::string())==sender) {
          SendMessageW(GetDlgItem(w,PLAYERS),LB_SETCURSEL,i,0);layout_all();InvalidateRect(w,nullptr,FALSE);
          return 0;
        }
        label(STATUS,"That player is no longer online.");
      }
      return 0;
    }
    if((id==FRIENDS || id==PLAYERS || id==REQUESTS) && HIWORD(wp)==LBN_SELCHANGE) { refresh(); return 0; }
    if(id==MODE && HIWORD(wp)==CBN_SELCHANGE) {
      label(URL_LABEL,SendMessageW(GetDlgItem(w,MODE),CB_GETCURSEL,0,0)==0?"Bootstrap peer (optional)":"Lobby service URL");
    } else if(id==GO_ONLINE && HIWORD(wp)==BN_CLICKED) {
      bool online; { std::lock_guard<std::mutex> lock(mutex); online=!go_online_requested; }
      // Going online needs a code to be announced under. Without the account that is the code made
      // from the name (announced_code), which is enough for P2P Direct; a hosted request still asks
      // for the account when it is sent.
      const std::string online_code=announced_code();
      if(online && (text(NAME).empty() || online_code.empty() || selected_mains.empty())) {
        lobby_tab=3; layout_all();
        if(p2p_matches) label(STATUS,identity.empty()?launcher::lang::tr("lobby.p2p.identity_failed"):std::string("Choose a display name and at least one main in Profile."));
#ifndef MELEE_NO_SLIPPI
        // No code although a name is there: no account, and no identity file to make one from.
        else label(STATUS,online_code.empty() && !text(NAME).empty()?"Sign in through Slippi Launcher, then reopen this tab.":"Choose a display name and at least one main in Profile.");
#endif
        SetFocus(GetDlgItem(w,NAME)); return 0;
      }
      { std::lock_guard<std::mutex> lock(mutex);
        go_online_requested=online;
        notice=online?"Joining the public lobby...":"Leaving the public roster; friend presence remains active.";
      }
      if(online) {
        Json cfg=own_profile();
        enqueue("join",cfg);
      } else enqueue("leave");
      refresh();
    }
    else if(id==SEND && !text(CHAT).empty()) {
      // With a private room in front the line goes to that room's other player only, never to the
      // public chat (also when the room has closed meanwhile: the player is then told so).
      if(active_room.empty()) enqueue("chat",{{"text",text(CHAT)}});
      else enqueue("pm",{{"room",active_room},{"text",text(CHAT)}});
      SetWindowTextW(GetDlgItem(w,CHAT),L""); SetTimer(w,2,150,nullptr);
    }
    else if((id==PM_PLAYER || id==PM_FRIEND) && HIWORD(wp)==BN_CLICKED) {
      std::string target;
      if(id==PM_PLAYER) target=selected(GetDlgItem(w,PLAYERS),rows);
      else {
        const int pick=(int)SendMessageW(GetDlgItem(w,FRIENDS),LB_GETCURSEL,0,0);
        if(pick>=0 && pick<(int)friends.size() && !friends[pick].value("incoming",false)) target=friends[pick].value("id",std::string());
      }
      if(target.empty() || target==self) return 0;
      open_private(w,target);
      refresh(); RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }
    else if(id==REQUEST) {
      auto target=selected(GetDlgItem(w,PLAYERS),rows); if(target.empty() || target==self) return 0;
      Json them=Json::object(); for(const auto& p:rows) if(p.value("id",std::string())==target) { them=p; break; }
      send_request(w,REQUEST,target,them);
    } else if(id==P2P_REQUEST && HIWORD(wp)==BN_CLICKED) {
      // The same request and accept flow as Send Match Request, with the peer-to-peer transport.
      auto target=selected(GetDlgItem(w,PLAYERS),rows); if(target.empty() || target==self) return 0;
      Json them=Json::object(); for(const auto& p:rows) if(p.value("id",std::string())==target) { them=p; break; }
      send_request(w,P2P_REQUEST,target,them,true);
    } else if(id==INVITE_FRIEND && HIWORD(wp)==BN_CLICKED) {
      // A friend can be asked while either player is out of the public lobby (launcher_lobby_p2p.cpp).
      auto target=selected(GetDlgItem(w,FRIENDS),friends); if(target.empty()) return 0;
      Json them=Json::object(); for(const auto& f:friends) if(f.value("id",std::string())==target) { them=f; break; }
      if(!them.value("incoming",false)) send_request(w,INVITE_FRIEND,target,them);
    } else if(id==ADD_FRIEND) {
      auto target=selected(GetDlgItem(w,PLAYERS),rows); if(!target.empty() && target!=self) enqueue("friend",{{"target",target}});
    } else if(id==OPEN_TO && HIWORD(wp)==BN_CLICKED) {
      choose_open_to(w);
    } else if((id==P2P_FIGHTER || id==P2P_FIGHTER_TEST) && HIWORD(wp)==BN_CLICKED) {
      choose_fighter(w);
    } else if(id==PLAYER_FILTER && HIWORD(wp)==BN_CLICKED) {
      choose_filter(w);
    } else if(id==COPY_CODE && HIWORD(wp)==BN_CLICKED) {
      // The status line then confirms the copy, and the button goes until the next "Can't reach".
      const std::string code=copy_button_code;
      if(code.empty() || !copy_text(w,wide(code))) { MessageBeep(MB_ICONWARNING); return 0; }
      { std::lock_guard<std::mutex> lock(mutex); notice=launcher::lang::tr(p2p_matches?"lobby.p2p.code_copied":"lobby.code_copied",{{"code",code}}); }
      refresh();
    } else if(id==FIND_MATCH && HIWORD(wp)==BN_CLICKED) {
      // P2P Unranked: the lobby pairs this player with another searching player by itself.
      bool on; { std::lock_guard<std::mutex> lock(mutex); on=!snapshot.value("self",Json::object()).value("searching",false); }
      enqueue("search",{{"on",on}}); SetTimer(w,2,150,nullptr);
    } else if(id==BLOCK_PLAYER && HIWORD(wp)==BN_CLICKED) {
      const auto target=selected(GetDlgItem(w,PLAYERS),rows); if(target.empty() || target==self) return 0;
      bool is_blocked=false;
      for(const auto& p:rows) if(p.value("id",std::string())==target) is_blocked=p.value("blocked",false);
      enqueue(is_blocked?"unblock":"block",{{"target",target}}); SetTimer(w,2,150,nullptr);
    } else if(id==INVITE_CONNECT && HIWORD(wp)==BN_CLICKED) {
      if(text(INVITE_EDIT).empty()) { SetFocus(GetDlgItem(w,INVITE_EDIT)); return 0; }
      enqueue("connect",{{"invite",text(INVITE_EDIT)}});
      SetWindowTextW(GetDlgItem(w,INVITE_EDIT),L""); SetTimer(w,2,150,nullptr);
    } else if(id==INVITE_COPY && HIWORD(wp)==BN_CLICKED) {
      // One line of text for the other player to paste: the identity key, the name, and where this
      // lobby can be reached (its own network addresses, and the address another player's launcher
      // saw it at, once one has).
      Json invite; { std::lock_guard<std::mutex> lock(mutex); invite=snapshot.value("invite",Json::object()); }
      const std::string line=invite.is_object()?invite.value("text",std::string()):std::string();
      const bool copied=!line.empty() && copy_text(w,wide(line));
      if(!copied) MessageBeep(MB_ICONWARNING);
      { std::lock_guard<std::mutex> lock(mutex);
        notice=copied?launcher::lang::tr("lobby.invite.copied",{{"port",std::to_string(invite.value("port",0))}}):launcher::lang::tr("lobby.invite.none"); }
      refresh();
    } else if(id==ACCEPT || id==DECLINE) {
      auto target=selected(GetDlgItem(w,REQUESTS),requests); if(!target.empty()) enqueue(id==ACCEPT?"accept":"cancel",{{"request",target}});
    } else if(id==FRIEND_ACCEPT || id==FRIEND_DECLINE || id==REMOVE_FRIEND) {
      auto target=selected(GetDlgItem(w,FRIENDS),friends); if(!target.empty()) enqueue(id==FRIEND_ACCEPT?"friend_accept":id==FRIEND_DECLINE?"friend_decline":"unfriend",{{"target",target}});
    }
    return 0;
  }
  if(msg==WM_CLOSE) { ShowWindow(w,SW_HIDE); return 0; } // Presence continues with launcher open.
  return DefWindowProcW(w,msg,wp,lp);
}
}
void set_account(const std::string& name,const std::string& account) {
  // Signed in, out, or as someone else: the player leaves the public lobby and joins again as who
  // they now are. Without an account the code is the one made from the name (announced_code).
  bool changed=hosted_code!=account; account_name=name; hosted_code=account;
  const std::string code=announced_code(); account_code=code;
  bool leave=false;
  { std::lock_guard<std::mutex> lock(mutex); leave=changed&&go_online_requested;
    if(leave) go_online_requested=false;
    config["code"]=code;
    if(config.value("name",std::string()).empty()) config["name"]=name;
  }
  if(leave) enqueue("leave");
  if(window) { label(CODE,code); if(text(NAME).empty()) label(NAME,name); }
}
void p2p_player(std::string& name,std::string& code) {
  std::lock_guard<std::mutex> lock(mutex);
  name=config.value("name",std::string()); code=account_code;
}
void init(HWND parent,const std::string& dir) {
  owner=parent; directory=dir;
  try {
    std::ifstream f(std::filesystem::u8path(dir+"/lobby-history.json"));
    auto data=Json::parse(f); if(data.is_array()) {
      for(const auto& game:data) if(game.is_object() && game.value("id",Json()).is_string() &&
          game.value("result",Json()).is_string()) history.push_back(game);
    }
  } catch(...) {}
  try {
    std::ifstream f(std::filesystem::u8path(dir+"/lobby-profile.json")); auto data=Json::parse(f);
    bool valid=data.is_object();
    for(auto field:{"mode","url","name","code","location","build","token"})
      if(data.count(field) && !data[field].is_string()) valid=false;
    if(data.count("mains")) {
      const auto& mains=data["mains"];
      if(!mains.is_array() || mains.empty() || mains.size()>3) valid=false;
      else for(auto& c:mains) if(!c.is_number_integer() || c.get<int>()<0 || c.get<int>()>25) valid=false;
    }
    if(data.count("udp_port") && (!data["udp_port"].is_number_integer() || data["udp_port"].get<int>()<1 || data["udp_port"].get<int>()>65535)) valid=false;
    if(data.count("auto_reject_invites") && !data["auto_reject_invites"].is_boolean()) valid=false;
    if(data.count("request_sound") && !data["request_sound"].is_boolean()) valid=false;
    if(data.count("request_volume") && (!data["request_volume"].is_number_integer() || data["request_volume"].get<int>()<0 || data["request_volume"].get<int>()>100)) valid=false;
    if(data.count("peer_port") && (!data["peer_port"].is_number_integer() || data["peer_port"].get<int>()<1 || data["peer_port"].get<int>()>65535)) valid=false;
    if(data.count("mode") && data["mode"]!="peer" && data["mode"]!="service") valid=false;
    // The match character and color: a damaged value is dropped, the rest of the profile stays.
    if(data.count("p2p_ch") && (!data["p2p_ch"].is_number_integer() || data["p2p_ch"].get<int>()<-1 || data["p2p_ch"].get<int>()>25)) data.erase("p2p_ch");
    if(data.count("p2p_col") && (!data["p2p_col"].is_number_integer() || data["p2p_col"].get<int>()<0 || data["p2p_col"].get<int>()>5)) data.erase("p2p_col");
    // Whether an account was signed in when the profile was last saved says nothing about now.
    if(data.is_object()) data.erase("hosted_ok");
    if(valid) config=data;
  } catch(...) {}
  {
    // Tests: "<character>/<color>" instead of the saved choice, so a run needs no click.
    if(const char* fighter=test_env("MELEE_LAUNCHER_TEST_LOBBY_FIGHTER")) {
      int ch=-1,col=0;
      if(std::sscanf(fighter,"%d/%d",&ch,&col)==2 && ch>=0 && ch<=25 && col>=0 && col<p2p_color_count(ch)) { config["p2p_ch"]=ch; config["p2p_col"]=col; }
    }
    p2p_character=config.value("p2p_ch",-1); p2p_color=config.value("p2p_col",0);
  }
  // The saved mains, also before the Lobby page is opened (a re-announce sends them).
  if(config.count("mains") && config["mains"].is_array() && !config["mains"].empty()) selected_mains=config["mains"].get<std::vector<int>>();
  WNDCLASSW wc{}; wc.lpfnWndProc=proc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"MeleeUnlockedLobby";
  wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=nullptr; RegisterClassW(&wc);
  if(const char* name=test_env("MELEE_LAUNCHER_TEST_LOBBY_NAME")) config["name"]=std::string(name);
  // Before the worker starts, so the identity file is made (or read) by one thread only.
  identity=identity_key(dir);
  if(p2p_matches) {
    account_name=config.value("name",std::string());
    account_code=identity.empty()?std::string():derived_code(account_name,identity);
  } else account_code=announced_code();   // until the launcher names the account (set_account)
  config["code"]=account_code;
  if(config.value("name",std::string()).empty()) config["name"]=account_name;
  worker=std::thread(work);
}
void open(const std::string& version,bool ready) {
  build=version; can_play=ready;
  const int dpi=owner?GetDpiForWindow(owner):96;
  RECT client{}; if(owner) GetClientRect(owner,&client);
  if(!window) window=CreateWindowExW(0,L"MeleeUnlockedLobby",L"",WS_CHILD|WS_CLIPCHILDREN,
                                     MulDiv(190,dpi,96),0,client.right-MulDiv(190,dpi,96),client.bottom,
                                     owner,nullptr,GetModuleHandleW(nullptr),nullptr);
  layout_all();
  ShowWindow(window,SW_SHOW); SetFocus(window); refresh();
}
void hide() { if(window) ShowWindow(window,SW_HIDE); }
void refresh_theme() {
  if(!window) return;
  InvalidateRect(window,nullptr,FALSE);
  for(int id:{GO_ONLINE,REQUEST,ADD_FRIEND,TAB_PROFILE,TAB_FRIENDS,TAB_HISTORY,TAB_CHAT,
              AUTO_REJECT,REQUEST_SOUND,OPEN_TO,SAVE_PROFILE,SEND,EMOJI,ACCEPT,DECLINE,
              PLAYER_FILTER,INVITE_FRIEND,COPY_CODE,PM_PLAYER,PM_FRIEND,INVITE_CONNECT,INVITE_COPY,FIND_MATCH,BLOCK_PLAYER,P2P_FIGHTER,
              P2P_REQUEST,P2P_FIGHTER_TEST})
    if(HWND h=GetDlgItem(window,id)) InvalidateRect(h,nullptr,FALSE);
  for(int i=0;i<26;++i) if(HWND h=GetDlgItem(window,CHARACTER_FIRST+i)) InvalidateRect(h,nullptr,FALSE);
}
bool take_match(Match& match) {
  std::lock_guard<std::mutex> lock(mutex); if(matches.empty()) return false;
  match=matches.front(); matches.pop_front(); pending_match=match; pending_started=false; test_match_taken=true; return true;
}
void stop_for_match(const Match& match) {
  if(!match.p2p_lobby_port) return;
  std::unique_lock<std::mutex> lock(mutex);
  hold_wanted=true; wake.notify_one();
  // The worker answers within its 50 ms tick plus the 600 ms it keeps answering the other launcher.
  // Should it not (it never has), the game still retries its bind for two seconds.
  hold_changed.wait_for(lock,std::chrono::seconds(3),[]{ return held; });
}
void restart_after_match() {
  { std::lock_guard<std::mutex> lock(mutex); if(!hold_wanted) return; hold_wanted=false; }
  wake.notify_one();
}
void game_running(bool value) {
  if(!value && pending_started && pending_match) {
    try {
      consume_results();
      // A peer-to-peer match has no result mailbox: the game's own result file says who won.
      if(result_events==0) {
        const std::string first=pending_match->p2p_result;
        record_match(*pending_match,p2p_outcome(first),1);
        // Further games of the same session: "<id>.g2.json", "<id>.g3.json", ... beside the first
        // (source_p2p.cpp names them), until one is missing.
        if(first.size()>5) for(int game=2;game<=127;++game) {
          const std::string next=first.substr(0,first.size()-5)+".g"+std::to_string(game)+".json";
          std::error_code ec;
          if(!std::filesystem::exists(std::filesystem::u8path(next),ec)) break;
          record_match(*pending_match,p2p_outcome(next),game);
        }
      }
      std::lock_guard<std::mutex> lock(mutex); notice="Match ended. Results saved in local history.";
    } catch(const std::exception& ex) { std::lock_guard<std::mutex> lock(mutex); notice=ex.what(); }
  }
  { std::lock_guard<std::mutex> lock(mutex);
    running=value;
    if(value) {
      game_started=std::filesystem::file_time_type::clock::now(); pending_started=pending_match.has_value();
      result_offset=0; result_events=0; result_mailbox_ready=false;
      if(pending_started) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(directory+"/lobby-game-status.json.results"),ec);
        result_mailbox_ready=!ec;
      }
    } else { pending_match.reset(); pending_started=false; result_mailbox_ready=false; }
  }
  if(!value) enqueue("available");
}
void shutdown() {
  { std::lock_guard<std::mutex> lock(mutex); stopping=true; }
  wake.notify_one(); if(worker.joinable()) worker.join();
  if(window) DestroyWindow(window); window=nullptr;
  if(banner_window) DestroyWindow(banner_window); banner_window=nullptr;
  for(HFONT* f:{&banner_title_font,&banner_text_font}) if(*f) { DeleteObject(*f); *f=nullptr; }
}

void set_game_state(const std::string& next_build,bool ready,const std::string& setup_hint) {
  const bool changed=next_build!=build || ready!=can_play || setup_hint!=setup_hint_text;
  build=next_build; can_play=ready; setup_hint_text=setup_hint;
  // Other players see a changed Game Build or setup at once, not only after the next Lobby click.
  if(changed) announce();
}

void refresh_mods(const std::vector<std::string>& detected_json_paths, bool static_engine_ready) {
  std::string contents,game_dir;
  for(const auto& path:detected_json_paths) {
    std::ifstream in(std::filesystem::u8path(path),std::ios::binary);
    if(!in) continue;
    contents.assign(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
    game_dir=std::filesystem::u8path(path).parent_path().parent_path().parent_path().u8string();
    break;
  }
  mods_seen=contents;
  // The same reading the lobby test checks (launcher_lobby_p2p.cpp read_mod_scan).
  ModDiscs discs=read_mod_scan(contents,game_dir);
  // A lobby acceptance must enter a match without first-run card prompts. Each mod has its
  // own save folder; a completed vanilla card does not mean that the mod's setup is complete.
  for(auto it=discs.paths.begin();it!=discs.paths.end();) {
    if(!static_engine_ready || !has_game_save(directory+"/User/GC/Mods/"+it->first)) {
      discs.has.erase(it->first);
      it=discs.paths.erase(it);
    } else ++it;
  }
  bool changed;
  { std::lock_guard<std::mutex> lock(mutex);
    const bool custom_ready=static_engine_ready && !custom_hash.empty() &&
        has_game_save(directory+"/User/GC/Mods/custom-"+custom_hash);
    changed=discs.has!=mod_has || custom_ready!=custom_launch_ready;
    custom_launch_ready=custom_ready; mod_has=discs.has; mod_paths=discs.paths;
  }
  if(!changed) return;
  if(window) { label(OPEN_TO,launcher::lang::tx("Open to:")+" "+open_summary()); InvalidateRect(GetDlgItem(window,PLAYERS),nullptr,FALSE); }
  announce();
}

void set_prefs(const Prefs& next) {
  std::string hash;
  if(!next.custom_iso.empty()) hash=content_hash16(next.custom_iso);
  { std::lock_guard<std::mutex> lock(mutex);
    current_prefs=next;
    if(hash.empty()) { current_prefs.custom_iso.clear(); current_prefs.custom_name.clear(); }
    custom_hash=hash;
  }
  if(window) label(OPEN_TO,launcher::lang::tx("Open to:")+" "+open_summary());
  announce();
}
Prefs prefs() { std::lock_guard<std::mutex> lock(mutex); Prefs p=current_prefs; p.open_to=join_list(open_list()); return p; }
void on_prefs_changed(std::function<void()> callback) { prefs_changed=std::move(callback); }

// Test runs only (MELEE_LAUNCHER_TEST): what a player would click.
// MELEE_LAUNCHER_TEST_LOBBY_AUTOSEARCH=1 goes online and presses Find match (only the build where
// every match is peer to peer has it); MELEE_LAUNCHER_TEST_LOBBY_REQUEST=1 goes online and sends a
// peer-to-peer match request to the first player seen (in the launcher with both kinds of match
// that is the P2P Direct test control; the value "hosted" sends the other kind instead);
// MELEE_LAUNCHER_TEST_LOBBY_ACCEPT=1 goes online and accepts every request. One match per run.
static void test_drive() {
  static const bool search=p2p_matches && test_env("MELEE_LAUNCHER_TEST_LOBBY_AUTOSEARCH")!=nullptr;
  static const char* const request_kind=test_env("MELEE_LAUNCHER_TEST_LOBBY_REQUEST");
  static const bool request=request_kind!=nullptr;
  static const bool accept=test_env("MELEE_LAUNCHER_TEST_LOBBY_ACCEPT")!=nullptr;
  static bool searched=false;
  static ULONGLONG next_try=0;
  if((!search && !request && !accept) || !can_play || build.empty()) return;
  Json state; std::string me; bool playing,taken;
  { std::lock_guard<std::mutex> lock(mutex); state=snapshot; me=self; playing=running; taken=test_match_taken; }
  if(playing || taken) return;
  const auto now=GetTickCount64();
  const Json mine=state.value("self",Json::object());
  if(!mine.is_object() || !mine.value("visible",false)) {
    if(now<next_try) return;
    const Json cfg=own_profile();
    if(cfg.value("name",std::string()).empty() || cfg.value("code",std::string()).empty()) return;
    { std::lock_guard<std::mutex> lock(mutex); go_online_requested=true; }
    enqueue("join",cfg); next_try=now+5000;
    return;
  }
  if(accept) for(const auto& r:state.value("requests",Json::array())) {
    const auto id=r.value("id",std::string());
    if(r.value("state",std::string())!="pending" || r.value("to",std::string())!=me || id.empty() || prompted_requests.count(id)) continue;
    prompted_requests.insert(id);   // no banner for it either
    enqueue("accept",{{"request",id}});
  }
  if(now<next_try) return;
  if(search && !searched) {
    if(mine.value("searching",false)) { searched=true; return; }
    next_try=now+1500; enqueue("search",{{"on",true}});
  }
  if(request && state.value("requests",Json::array()).empty()) for(const auto& p:state.value("players",Json::array())) {
    const auto id=p.value("id",std::string());
    if(id.empty() || id==me || p.value("status",std::string())!="Online" || !p.value("ready",false)) continue;
    next_try=now+5000;   // the lobby takes one request every three seconds
    Json data={{"target",id},{"mode","vanilla"}};
    if(!p2p_matches && std::string(request_kind)!="hosted") data["transport"]=kP2pTransport;
    enqueue("request",data);
    break;
  }
}
void tick_ui() {
  if(test_mode) test_drive();
  // The Lobby page refreshes itself while it is showing; otherwise requests still need answering.
  if(window && IsWindowVisible(window)) return;
  try { consume_results(); } catch(...) {}
  Json state; std::map<std::string,int> ping; std::string me; bool playing;
  { std::lock_guard<std::mutex> lock(mutex); state=snapshot; ping=pings; me=self; playing=running; }
  process_invites(state,ping,me,playing);
  process_private(state);
}
void owner_resized() { banner_place(); }
void refresh_language() {
  if(window) { label(OPEN_TO,launcher::lang::tx("Open to:")+" "+open_summary()); RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN); }
  if(banner_window) InvalidateRect(banner_window,nullptr,FALSE);
}

void show_banner(const std::string& id,const std::string& title,const std::string& detail,
                 const std::vector<BannerButton>& buttons,std::function<void(int)> on_click,int priority,bool keep_open) {
  banner_create();
  for(auto& b:banners) if(b.id==id) {
    b.title=title; b.detail=detail; b.buttons=buttons; b.click=std::move(on_click); b.priority=priority; b.keep_open=keep_open;
    banner_place(); return;
  }
  Banner b; b.id=id; b.title=title; b.detail=detail; b.buttons=buttons; b.click=std::move(on_click);
  b.priority=priority; b.keep_open=keep_open; b.shown=GetTickCount64();
  banners.push_back(std::move(b));
  banner_place();
}
void close_banner(const std::string& id) {
  const auto before=banners.size();
  banners.erase(std::remove_if(banners.begin(),banners.end(),[&](const Banner& b){ return b.id==id; }),banners.end());
  if(banners.size()!=before) banner_place();
}
bool banner_shown(const std::string& id) {
  return std::any_of(banners.begin(),banners.end(),[&](const Banner& b){ return b.id==id; });
}
void bring_to_front() {
  if(!owner || test_mode) return;
  if(IsIconic(owner)) ShowWindow(owner,SW_RESTORE);
  // Windows may refuse the focus to a background process: the taskbar button flashes instead.
  if(!SetForegroundWindow(owner)) { FLASHWINFO flash{sizeof flash,owner,FLASHW_TRAY|FLASHW_TIMERNOFG,5,0}; FlashWindowEx(&flash); }
}
void log(const std::string& line) { lobby_log(directory,line); }
}
