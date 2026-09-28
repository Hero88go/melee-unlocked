// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <winhttp.h>
#include <commctrl.h>
#include <nlohmann/json.hpp>
#include "launcher_lobby.h"
#include "launcher_lobby_p2p.h"
#include "launcher_theme.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
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
       PROFILE_LOCATION, PROFILE_MAINS, PROFILE_MODE, PLAYER_HEADING, EMPTY_PLAYERS, EMPTY_FRIENDS, EMPTY_CHAT, ADVANCED, FRIEND_CODE, FRIEND_SEND, FRIEND_HINT, EMOJI, AUTO_REJECT, REQUEST_SOUND, VOLUME_LABEL };
HWND owner{}, window{};
std::string directory, build;
std::string account_name, account_code;
std::vector<int> selected_mains{2,20,9};
const int CHARACTER_FIRST=700;
bool can_play=false;
std::mutex mutex;
std::condition_variable wake;
std::thread worker;
bool stopping=false, running=false, joined=false;
bool go_online_requested=false;
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
void label(int id,const std::string& s) { if(text(id)!=s) SetWindowTextW(GetDlgItem(window,id),wide(s).c_str()); }
void enqueue(std::string action,Json data=Json::object()) {
  { std::lock_guard<std::mutex> lock(mutex); if(commands.size()<32) commands.push_back({action,data}); }
  wake.notify_one();
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
void record_match(const Match& match,const std::string& result,int game_number) {
  SYSTEMTIME now{}; GetLocalTime(&now);
  char when[32]{};
  std::snprintf(when,sizeof when,"%04u-%02u-%02u %02u:%02u",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute);
  history.insert(history.begin(),Json{{"id",match.id+"/"+std::to_string(game_number)},
                                      {"opponent",match.opponent},{"code",match.code},{"build",match.build},
                                      {"result",result},{"when",when},{"game",game_number}});
  try { save_history(); } catch(...) { history.erase(history.begin()); throw; }
  bool announce=false;{std::lock_guard<std::mutex> lock(mutex);announce=go_online_requested;}
  if(announce) enqueue("join",profile_config());
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
  std::string s=p.value("name",std::string("?"))+" | "+p.value("location",std::string("Unknown"));
  auto it=ping.find(p.value("id",std::string()));
  s+=it==ping.end()?" | Ping unavailable":" | "+std::to_string(it->second)+" ms";
  s+=" | ";
  for(auto c:p.value("mains",Json::array())) { int i=c.get<int>(); if(i>=0 && i<26) s+=std::string(characters[i])+" / "; }
  return s+" | "+(p.value("ready",true)?p.value("status",std::string("Online")):std::string("Finish game setup"));
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
void work() {
  WSADATA ws{}; WSAStartup(MAKEWORD(2,2),&ws);
  {
    Probe probe;
    Json cfg;
    { std::lock_guard<std::mutex> lock(mutex); cfg=config; }
    std::unique_ptr<PeerLobby> peer;
    if(peer_mode(cfg) && cfg.count("name")) {
      try { peer=std::make_unique<PeerLobby>(directory,cfg.value("url",std::string()),cfg.value("peer_port",0)); }
      catch(const std::exception& ex) { std::lock_guard<std::mutex> lock(mutex); notice=ex.what(); }
    }
    if(peer) probe.poll(Json(),Json(),"");
    std::string cookie;
    for(;;) {
      std::deque<Command> batch; bool playing; std::filesystem::file_time_type started;
      {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait_for(lock,std::chrono::milliseconds(peer?50:(joined?800:5000)),[]{ return stopping || !commands.empty(); });
        if(stopping) break;
        batch.swap(commands); playing=running; started=game_started;
      }
      try {
        for(const auto& c:batch) {
          try {
            if(c.action=="join") {
              Json next=c.data;
              if(peer_mode(next)) {
                if(!peer || !peer_mode(cfg) || cfg.value("url",std::string())!=next.value("url",std::string())) {
                  peer.reset();
                  peer=std::make_unique<PeerLobby>(directory,next.value("url",std::string()),next.value("peer_port",0));
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
              std::lock_guard<std::mutex> lock(mutex);
              if(c.action=="leave") { joined=false; go_online_requested=false; }
            }
            std::lock_guard<std::mutex> lock(mutex);
            notice=peer_mode(cfg)?"You're online. Select a player to send a match request.":
                                   "Connected. Location and Slippi code are player supplied.";
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
          if(peer) pings=peer->pings();
          joined=identity.value("visible",false);
          if(joined && !go_online_requested) commands.push_back({"leave",Json::object()});
          for(auto& r:state.value("requests",Json::array())) {
            if(r.value("state",std::string())!="accepted" || !r.value("confirmed",true) || playing || !joined) continue;
            std::string id=r["id"], peer=r["from"]==self?r["to"].get<std::string>():r["from"].get<std::string>();
            if(launched.count(id)) continue;
            for(auto& p:state["players"]) if(p["id"]==peer && r.value("transport",std::string())=="slippi-direct") {
              launched.insert(id); matches.push_back({id,p["code"].get<std::string>(),cfg["mains"][0].get<int>(),cfg.value("build",std::string()),p.value("name",std::string())});
              notice="Match accepted. Starting Slippi Direct...";
            }
          }
        }
      } catch(const std::exception& ex) { std::lock_guard<std::mutex> lock(mutex); notice=ex.what(); snapshot=Json::object(); pings.clear(); }
    }
    if(!peer_mode(cfg) && !cfg.value("token",std::string()).empty()) { try { api(cfg,"offline",Json::object()); } catch(...) {} }
  }
  WSACleanup();
}
#include "launcher_lobby_ui.inl"
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
      if(sound_enabled) request_chime(sound_volume);
      FLASHWINFO flash{sizeof flash,owner,FLASHW_TRAY|FLASHW_TIMERNOFG,5,0}; FlashWindowEx(&flash);
    }
    // Let the on-demand peer probe return before presenting its result.
    if(!ping.count(sender) && GetTickCount64()-incoming_since[id]<2200) continue;
    prompted_requests.insert(id);
    Json player=Json::object();
    for(const auto& item:state.value("players",Json::array())) if(item.value("id",std::string())==sender) { player=item; break; }
    std::string name=player.value("name",std::string("Player"));
    std::string location=player.value("location",std::string("Location unavailable"));
    std::string mains;
    for(const auto& value:player.value("mains",Json::array())) {
      if(!value.is_number_integer()) continue; int n=value.get<int>(); if(n<0||n>=26) continue;
      if(!mains.empty()) mains+=" / "; mains+=characters[n];
    }
    const auto it=ping.find(sender);
    const std::string latency=it==ping.end()?"Unavailable":std::to_string(it->second)+" ms";
    const std::string message=name+" sent you a match request.\n\nLocation: "+location+
      "\nDirect ping: "+latency+"\nPreferred characters: "+(mains.empty()?"Not shared":mains)+
      "\n\nAccept and launch Slippi Direct?";
    int answer=MessageBoxW(owner,wide(message).c_str(),L"New match request",MB_YESNO|MB_ICONQUESTION|MB_SETFOREGROUND);
    FLASHWINFO stop{sizeof stop,owner,FLASHW_STOP,0,0}; FlashWindowEx(&stop);
    enqueue(answer==IDYES?"accept":"cancel",{{"request",id}});
    label(STATUS,answer==IDYES?"Accepted. Starting direct match...":"Match request declined.");
    break;
  }
  for(auto it=incoming_since.begin();it!=incoming_since.end();) {
    bool found=false; for(const auto& request:state.value("requests",Json::array()))
      if(request.value("id",std::string())==it->first && request.value("state",std::string())=="pending") found=true;
    if(!found) it=incoming_since.erase(it); else ++it;
  }
}
void refresh() {
  try { consume_results(); }
  catch(const std::exception& ex) { std::lock_guard<std::mutex> lock(mutex); notice=ex.what(); }
  Json state,local_profile; std::string status,me; std::map<std::string,int> ping; bool active,playing,wanted;
  { std::lock_guard<std::mutex> lock(mutex); state=snapshot; local_profile=config; status=notice; ping=pings; me=self; active=joined; playing=running; wanted=go_online_requested; }
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
  refill(PLAYERS,rows,roster,[&](const Json& p){return peer_text(p,ping);});
  refill(REQUESTS,requests,state.value("requests",Json::array()),[&](const Json& r){
    std::string peer=r["from"]==me?r["to"].get<std::string>():r["from"].get<std::string>();
    std::string description=peer;
    for(auto& p:rows) if(p["id"]==peer) description=peer_text(p,ping);
    return std::string(r["to"]==me?"Incoming: ":"Sent: ")+description+" | "+r.value("state",std::string())+" ("+std::to_string(r.value("remaining",0))+"s)";
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
  label(RECORD,"Local history: "+std::to_string(wins)+" wins / "+std::to_string(losses)+" losses (unranked)");
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
  Json next_chat=state.value("messages",Json::array());
  if(next_chat!=chat_messages) {
    chat_messages=std::move(next_chat);HWND list=GetDlgItem(window,CHATLOG);
    int old_count=(int)SendMessageW(list,LB_GETCOUNT,0,0);
    int top=(int)SendMessageW(list,LB_GETTOPINDEX,0,0);
    RECT bounds{};GetClientRect(list,&bounds);
    int visible=std::max(1,int(bounds.bottom-bounds.top)/std::max(1,U(60)));
    bool at_bottom=old_count==0 || top+visible>=old_count-1;
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
  EnableWindow(GetDlgItem(window,ACCEPT),active && !playing && can_play);
  if(!can_play && !playing) label(STATUS,"To play, finish disc and Slippi setup on the Play tab.");
  label(PLAYER_HEADING,"Players online ("+std::to_string(rows.size())+")");
  EnableWindow(GetDlgItem(window,ADD_FRIEND),TRUE);
  EnableWindow(GetDlgItem(window,SEND),active);
  process_invites(state,ping,me,playing);
  for(int i=0;i<26;++i) EnableWindow(GetDlgItem(window,CHARACTER_FIRST+i),!playing);
  layout();
}
LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
  if(msg==WM_CREATE) {
    window=w; displayed_history=Json();
    NONCLIENTMETRICSW metrics{sizeof metrics}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof metrics,&metrics,0);
    LOGFONTW font=metrics.lfMessageFont; font.lfQuality=CLEARTYPE_QUALITY;
    font.lfHeight=-U(12); ui_font=CreateFontIndirectW(&font);
    font.lfWeight=FW_SEMIBOLD; ui_name=CreateFontIndirectW(&font); font.lfWeight=FW_NORMAL;
    font.lfHeight=-U(11); ui_small=CreateFontIndirectW(&font);
    font.lfHeight=-U(21); font.lfWeight=FW_SEMIBOLD; ui_title=CreateFontIndirectW(&font);
    ui_brush=CreateSolidBrush(ui_panel); ui_background=CreateSolidBrush(ui_bg); ui_field_brush=CreateSolidBrush(ui_field);
    auto control=[&](int id,const wchar_t* type,const wchar_t* title,DWORD style=0) { add(w,id,type,title,0,0,0,0,style); };
    control(GO_ONLINE,L"BUTTON",L"Go Online");
    control(ONLINE_HINT,L"STATIC",L"Go Online shows your name to other players and lets them send you match requests while the launcher is open.");
    control(TAB_CHAT,L"BUTTON",L"Back to Chat"); control(TAB_FRIENDS,L"BUTTON",L"Friends");
    control(TAB_HISTORY,L"BUTTON",L"History"); control(TAB_PROFILE,L"BUTTON",L"Profile");
    control(PLAYER_HEADING,L"STATIC",L"Players online (0)");
    for(int id:{PLAYERS,FRIENDS,REQUESTS,HISTORY}) control(id,L"LISTBOX",L"",WS_VSCROLL);
    control(REQUEST,L"BUTTON",L"Send Match Request"); control(ADD_FRIEND,L"BUTTON",L"Add Friend");
    control(ACCEPT,L"BUTTON",L"Accept Match"); control(DECLINE,L"BUTTON",L"Decline / Cancel");
    control(FRIEND_ACCEPT,L"BUTTON",L"Accept Friend"); control(FRIEND_DECLINE,L"BUTTON",L"Decline");
    control(REMOVE_FRIEND,L"BUTTON",L"Remove Friend");
    control(CHATLOG,L"LISTBOX",L"",WS_VSCROLL);
    control(CHAT,L"EDIT",L"",ES_AUTOHSCROLL); SetWindowSubclass(GetDlgItem(w,CHAT),chat_proc,1,0); SendMessageW(GetDlgItem(w,CHAT),EM_SETLIMITTEXT,300,0);
    SendMessageW(GetDlgItem(w,CHAT),EM_SETCUEBANNER,TRUE,(LPARAM)L"Send a message...");
    control(SEND,L"BUTTON",L"Send"); control(EMOJI,L"BUTTON",L"Emoji");
    for(int i=0;i<8;++i) {
      emoji_icons[i]=(HICON)LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(300+i),IMAGE_ICON,U(24),U(24),LR_SHARED);
      emoji_icon_offsets[i]=visible_icon_offset(emoji_icons[i]);
    }
    control(FRIEND_CODE,L"EDIT",L"",ES_AUTOHSCROLL); SendMessageW(GetDlgItem(w,FRIEND_CODE),EM_SETCUEBANNER,TRUE,(LPARAM)L"Slippi code, e.g. FOX#123");
    control(FRIEND_SEND,L"BUTTON",L"Send Friend Request"); control(FRIEND_HINT,L"STATIC",L"Enter an online player's Slippi code."); control(STATUS,L"STATIC",L""); control(RECORD,L"STATIC",L"");
    control(PROFILE_NAME,L"STATIC",L"Display name"); control(PROFILE_CODE,L"STATIC",L"Slippi connect code (linked)");
    control(PROFILE_LOCATION,L"STATIC",L"Location"); control(PROFILE_MAINS,L"STATIC",L"Your three mains");
    control(PROFILE_MODE,L"STATIC",L"Connection"); control(URL_LABEL,L"STATIC",L"Bootstrap peer (optional)");
    for(int id:{NAME,CODE,LOCATION,URL}) control(id,L"EDIT",L"",ES_AUTOHSCROLL);
    SendMessageW(GetDlgItem(w,CODE),EM_SETREADONLY,TRUE,0);
    SendMessageW(GetDlgItem(w,CODE),EM_SETCUEBANNER,TRUE,(LPARAM)L"Sign in through Slippi Launcher");
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
    control(VOLUME_LABEL,L"STATIC",L"Volume 65%");

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
    label(VOLUME_LABEL,"Volume "+std::to_string(sound_volume)+"%");
    label(PROFILE_MAINS,"Main characters ("+std::to_string(selected_mains.size())+"/3)");
    SetTimer(w,1,1000,nullptr); layout(); refresh(); return 0;
  }
  if(msg==WM_SIZE) { layout(); return 0; }
  if(msg==WM_ERASEBKGND) return 1;
  if(msg==WM_PAINT) { paint_lobby(w); return 0; }
  if(msg==WM_DRAWITEM) { draw_control((DRAWITEMSTRUCT*)lp); return TRUE; }
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
          for(const auto& player:rows) if(player.value("id",std::string())==sender) {code=player.value("code",std::string());break;}
        }
        hover_code=wide(code.empty()?"Slippi code unavailable":"Slippi code: "+code);
      }
      tip->lpszText=hover_code.empty()?const_cast<wchar_t*>(L""):hover_code.data();
    }
    return 0;
  }
  if(msg==WM_CTLCOLORSTATIC || msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX) {
    int id=GetDlgCtrlID((HWND)lp); bool background=id==ONLINE_HINT||id==STATUS;
    SetTextColor((HDC)wp,(id==ONLINE_HINT||id==STATUS||id==EMPTY_CHAT||id==EMPTY_FRIENDS||id==EMPTY_PLAYERS)?ui_dim:ui_text);
    bool field=id==CHAT||id==NAME||id==CODE||id==LOCATION||id==URL||id==FRIEND_CODE;
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
          sound_volume=next; label(VOLUME_LABEL,"Volume "+std::to_string(next)+"%");
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
  if(msg==WM_DESTROY) { KillTimer(w,1); if(emoji_popup) DestroyWindow(emoji_popup); DeleteObject(ui_font); DeleteObject(ui_small); DeleteObject(ui_title); DeleteObject(ui_brush); DeleteObject(ui_background); DeleteObject(ui_field_brush); DeleteObject(ui_name); for(auto icon:stock_icons) if(icon) DestroyIcon(icon); lobby_tips=nullptr; return 0; }
  if(msg==WM_TIMER) { refresh(); return 0; }
  if(msg==WM_COMMAND) {
    int id=LOWORD(wp);
    if(id==EMOJI && HIWORD(wp)==BN_CLICKED) {show_emoji_picker();return 0;}
    if(id==ADD_FRIEND && HIWORD(wp)==BN_CLICKED) {
      auto target=selected(GetDlgItem(w,PLAYERS),rows);
      if(!target.empty()&&target!=self) { enqueue("friend",{{"target",target}}); label(STATUS,"Friend request sent."); }
      else { lobby_tab=1; adding_friend=true; layout(); RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN); SetFocus(GetDlgItem(w,FRIEND_CODE)); }
      return 0;
    }
    if(id==FRIEND_SEND && HIWORD(wp)==BN_CLICKED) {
      std::string code=text(FRIEND_CODE); std::transform(code.begin(),code.end(),code.begin(),[](unsigned char c){return (char)std::toupper(c);});
      for(const auto& player:rows) if(!player.value("is_self",false) && player.value("code",std::string())==code) { enqueue("friend",{{"target",player["id"]}}); label(FRIEND_HINT,"Friend request sent. Waiting for acceptance."); return 0; }
      label(FRIEND_HINT,"Player not found yet. Both players must be online in the lobby."); return 0;
    }
    if(id>=TAB_CHAT && id<=TAB_PROFILE && HIWORD(wp)==BN_CLICKED) { lobby_tab=id-TAB_CHAT; layout(); RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN); return 0; }
    if(id>=CHARACTER_FIRST && id<CHARACTER_FIRST+26 && HIWORD(wp)==BN_CLICKED) {
      int character=id-CHARACTER_FIRST; auto found=std::find(selected_mains.begin(),selected_mains.end(),character);
      if(found!=selected_mains.end()) selected_mains.erase(found);
      else if(selected_mains.size()<3) selected_mains.push_back(character);
      label(PROFILE_MAINS,"Main characters ("+std::to_string(selected_mains.size())+"/3)");
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
      auto cfg=profile_config(); bool online;
      { std::lock_guard<std::mutex> lock(mutex); online=go_online_requested; config=cfg; save(config); notice="Profile saved."; }
      if(online) enqueue("join",cfg);
      lobby_tab=0; refresh(); RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN); return 0;
    }
    if(id==CHATLOG && HIWORD(wp)==LBN_SELCHANGE) {
      int index=(int)SendMessageW(GetDlgItem(w,CHATLOG),LB_GETCURSEL,0,0);
      if(index>=0 && index<(int)chat_messages.size()) {
        auto sender=chat_messages[index].value("sender",std::string());
        for(int i=0;i<(int)rows.size();++i) if(rows[i].value("id",std::string())==sender) {
          SendMessageW(GetDlgItem(w,PLAYERS),LB_SETCURSEL,i,0);layout();InvalidateRect(w,nullptr,FALSE);
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
      if(online && (text(NAME).empty() || account_code.empty() || selected_mains.empty())) { lobby_tab=3; layout(); label(STATUS,account_code.empty()?"Sign in through Slippi Launcher, then reopen this tab.":"Choose a display name and at least one main in Profile."); SetFocus(GetDlgItem(w,NAME)); return 0; }
      { std::lock_guard<std::mutex> lock(mutex);
        go_online_requested=online;
        notice=online?"Joining the public lobby...":"Leaving the public roster; friend presence remains active.";
      }
      if(online) {
        Json cfg=profile_config();
        enqueue("join",cfg);
      } else enqueue("leave");
      refresh();
    }
    else if(id==SEND && !text(CHAT).empty()) { enqueue("chat",{{"text",text(CHAT)}}); label(CHAT,""); }
    else if(id==REQUEST || id==ADD_FRIEND) {
      auto target=selected(GetDlgItem(w,PLAYERS),rows); if(!target.empty() && target!=self) enqueue(id==REQUEST?"request":"friend",{{"target",target}});
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
void set_account(const std::string& name,const std::string& code) {
  bool changed=account_code!=code; account_name=name; account_code=code;
  bool leave=false;
  { std::lock_guard<std::mutex> lock(mutex); leave=changed&&go_online_requested;
    if(leave) go_online_requested=false;
    config["code"]=code;
    if(config.value("name",std::string()).empty()) config["name"]=name;
  }
  if(leave) enqueue("leave");
  if(window) { label(CODE,code); if(text(NAME).empty()) label(NAME,name); }
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
    if(valid) config=data;
  } catch(...) {}
  WNDCLASSW wc{}; wc.lpfnWndProc=proc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"MeleeUnlockedLobby";
  wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=nullptr; RegisterClassW(&wc);
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
  layout();
  ShowWindow(window,SW_SHOW); SetFocus(window); refresh();
}
void hide() { if(window) ShowWindow(window,SW_HIDE); }
void refresh_theme() {
  if(!window) return;
  InvalidateRect(window,nullptr,FALSE);
  for(int id:{GO_ONLINE,REQUEST,ADD_FRIEND,TAB_PROFILE,TAB_FRIENDS,TAB_HISTORY,TAB_CHAT,
              AUTO_REJECT,REQUEST_SOUND,SAVE_PROFILE,SEND,EMOJI,ACCEPT,DECLINE})
    if(HWND h=GetDlgItem(window,id)) InvalidateRect(h,nullptr,FALSE);
  for(int i=0;i<26;++i) if(HWND h=GetDlgItem(window,CHARACTER_FIRST+i)) InvalidateRect(h,nullptr,FALSE);
}
bool take_match(Match& match) {
  std::lock_guard<std::mutex> lock(mutex); if(matches.empty()) return false;
  match=matches.front(); matches.pop_front(); pending_match=match; pending_started=false; return true;
}
void game_running(bool value) {
  if(!value && pending_started && pending_match) {
    try {
      consume_results();
      if(result_events==0) record_match(*pending_match,"incomplete",1);
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
}
}
