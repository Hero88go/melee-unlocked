// SPDX-License-Identifier: GPL-2.0-or-later
// Best-effort, serverless lobby. Mainline DHT only discovers UDP endpoints;
// all lobby data is signed or authenticated directly between launcher peers.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <ctime>
#include <dht.h>
#include <monocypher.h>
#include <monocypher-ed25519.h>
#include "launcher_lobby_p2p.h"
#include "launcher_lang.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <vector>

namespace launcher::lobby {
namespace {
using Json = nlohmann::json;
using Bytes20 = std::array<uint8_t,20>;
using Bytes32 = std::array<uint8_t,32>;
using Bytes64 = std::array<uint8_t,64>;
constexpr char wire_prefix[] = "MUL1";
// A request (or an accept) that has not been acknowledged by then is reported as unreachable.
constexpr ULONGLONG unreachable_after = 10000;
SOCKET dht_socket_handle = INVALID_SOCKET;

void random_bytes(void* out, size_t size) {
  if(BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), static_cast<ULONG>(size),
                     BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    throw std::runtime_error("Windows random generator failed");
}
std::string hex(const uint8_t* data, size_t size) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result(size*2,'0');
  for(size_t i=0;i<size;++i) { result[2*i]=digits[data[i]>>4]; result[2*i+1]=digits[data[i]&15]; }
  return result;
}
int nibble(char c) {
  if(c>='0' && c<='9') return c-'0';
  if(c>='a' && c<='f') return c-'a'+10;
  return -1;
}
bool unhex(const std::string& value, uint8_t* out, size_t size) {
  if(value.size()!=size*2) return false;
  for(size_t i=0;i<size;++i) {
    int hi=nibble(value[2*i]),lo=nibble(value[2*i+1]);
    if(hi<0 || lo<0) return false;
    out[i]=static_cast<uint8_t>((hi<<4)|lo);
  }
  return true;
}
std::string nonce_id(size_t bytes=16) {
  std::array<uint8_t,24> data{}; random_bytes(data.data(),bytes); return hex(data.data(),bytes);
}
Bytes20 topic_hash(const std::string& name) {
  Bytes20 result{}; crypto_blake2b(result.data(),result.size(),
                                  reinterpret_cast<const uint8_t*>(name.data()),name.size()); return result;
}
std::string endpoint_key(const sockaddr_in& a) {
  char ip[INET_ADDRSTRLEN]{};
  inet_ntop(AF_INET,&a.sin_addr,ip,sizeof ip);
  return std::string(ip)+":"+std::to_string(ntohs(a.sin_port));
}
bool resolve(const std::string& value,sockaddr_in& out) {
  auto separator=value.rfind(':');
  if(separator==std::string::npos) return false;
  addrinfo hint{},*result=nullptr; hint.ai_family=AF_INET; hint.ai_socktype=SOCK_DGRAM;
  if(getaddrinfo(value.substr(0,separator).c_str(),value.substr(separator+1).c_str(),&hint,&result)!=0) return false;
  out=*reinterpret_cast<sockaddr_in*>(result->ai_addr); freeaddrinfo(result); return true;
}
bool numeric_endpoint(const std::string& value,sockaddr_in& out) {
  auto separator=value.rfind(':'); if(separator==std::string::npos) return false;
  auto port=value.substr(separator+1);
  if(port.empty() || port.size()>5 || !std::all_of(port.begin(),port.end(),[](char c){return c>='0'&&c<='9';})) return false;
  int number=std::stoi(port); if(number<1 || number>65535) return false;
  out={}; out.sin_family=AF_INET; out.sin_port=htons(static_cast<u_short>(number));
  return inet_pton(AF_INET,value.substr(0,separator).c_str(),&out.sin_addr)==1;
}
bool code_format(const std::string& code) {
  const auto hash=code.find('#');
  if(hash==std::string::npos || hash<1 || hash>4 || code.size()-hash-1<1 || code.size()-hash-1>5) return false;
  for(size_t i=0;i<code.size();++i) {
    if(i==hash) continue;
    if(i<hash ? !(code[i]>='A'&&code[i]<='Z') && !(code[i]>='0'&&code[i]<='9')
              : !(code[i]>='0'&&code[i]<='9')) return false;
  }
  return true;
}
// Which base field of a profile fails, for lobby.log (the same rules as 0.8.1, which drops any
// hello that breaks them, so they cannot be loosened here).
std::string profile_problem(const Json& profile) {
  if(!profile.is_object()) return "not an object";
  for(const auto& field:{"name","code","location","build"}) {
    if(!profile.count(field) || !profile[field].is_string()) return std::string(field)+" missing";
    auto value=profile[field].get<std::string>();
    // Location is optional: Go Online only asks for a name, a code and a main, so a blank location
    // must not make the join fail (it did, silently, and the button fell back to Go Online).
    if(value.empty() && std::string(field)!="location") return std::string(field)+" empty";
    if(value.size()>(std::string(field)=="name"?32:std::string(field)=="code"?10:std::string(field)=="location"?48:80))
      return std::string(field)+" too long";
    if(std::any_of(value.begin(),value.end(),[](unsigned char c){return c<32;})) return std::string(field)+" has control characters";
  }
  if(!code_format(profile["code"].get<std::string>())) return "code format";
  if(!profile.count("mains") || !profile["mains"].is_array() || (profile["mains"].empty() || profile["mains"].size()>3)) return "mains count";
  std::set<int> mains;
  for(const auto& main:profile["mains"]) {
    if(!main.is_number_integer() || main.get<int>()<0 || main.get<int>()>25) return "mains value";
    mains.insert(main.get<int>());
  }
  if(mains.size()!=profile["mains"].size()) return "mains repeated";
  if(!profile.count("ready") || !profile["ready"].is_boolean()) return "ready missing";
  return {};
}
bool valid_profile(const Json& profile) { return profile_problem(profile).empty(); }
bool valid_wire_profile(const Json& profile) {
  if(!profile.is_object() || !profile.count("ready") || !profile["ready"].is_boolean()) return false;
  Json copy=profile; copy["ready"]=true; return valid_profile(copy);
}
bool valid_stocks(const Json& value) {
  if(!value.is_array() || value.size()>4) return false;
  for(const auto& count:value) if(!count.is_number_integer() || count.get<int>()<0 || count.get<int>()>99) return false;
  return true;
}
// Text another player typed, shown as-is: valid UTF-8 with no control characters and no bidi
// overrides (which could make a name read as something else).
bool plain_text(const std::string& value,size_t max_chars,size_t max_bytes) {
  if(value.size()>max_bytes) return false;
  size_t chars=0;
  for(size_t i=0;i<value.size();) {
    const unsigned char c=static_cast<unsigned char>(value[i]);
    const size_t length=c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:(c>>3)==30?4:0;
    if(!length || i+length>value.size()) return false;
    uint32_t cp=length==1?c:length==2?(c&0x1Fu):length==3?(c&0x0Fu):(c&0x07u);
    for(size_t k=1;k<length;++k) {
      const unsigned char n=static_cast<unsigned char>(value[i+k]);
      if((n&0xC0)!=0x80) return false;
      cp=(cp<<6)|(n&0x3Fu);
    }
    if((length==2 && cp<0x80) || (length==3 && cp<0x800) || (length==4 && cp<0x10000)) return false;
    if(cp<32 || (cp>=0x7F && cp<0xA0) || (cp>=0xD800 && cp<=0xDFFF) || cp>0x10FFFF) return false;
    if((cp>=0x202A && cp<=0x202E) || (cp>=0x2066 && cp<=0x2069) || cp==0x200E || cp==0x200F) return false;
    i+=length; ++chars;
  }
  return chars<=max_chars;
}
bool mod_version(const Json& value) {
  if(!value.is_string()) return false;
  const auto text=value.get<std::string>();
  if(text.size()>16) return false;
  return std::all_of(text.begin(),text.end(),[](unsigned char c){
    return std::isalnum(c) || c=='.' || c=='-' || c=='_' || c=='+'; });
}
bool hex16(const std::string& value) {
  return value.size()==16 && std::all_of(value.begin(),value.end(),[](char c){ return (c>='0'&&c<='9')||(c>='a'&&c<='f'); });
}
// The English phrase sent with a refusal, only for a launcher too old or too new to know the code.
const char* wire_reason(const std::string& code) {
  if(code=="hidden") return "is not in the public lobby";
  if(code=="busy") return "is in a game or another match";
  if(code=="not_ready") return "has not finished game setup";
  if(code=="sender_not_ready") return "sees your game setup as unfinished";
  if(code=="build") return "is on a different Game Build";
  if(code=="version") return "is on a different version";
  if(code=="answering") return "is answering another match request";
  if(code=="mode_missing") return "does not have that version installed";
  if(code=="mode_closed") return "is not open to that version";
  if(code=="mode_version") return "has a different version of that mod";
  if(code=="mode_hash") return "has a different custom ISO";
  if(code=="expired") return "the request expired";
  return "cannot play right now";
}
}

// "0.8.1:source" -> "Source Port 0.8.1", as the Play page names the builds.
static std::string build_name(const std::string& build) {
  auto colon=build.find(':');
  std::string version=build.substr(0,colon), kind=colon==std::string::npos?std::string():build.substr(colon+1);
  std::string name=kind=="source"?"Source Port":kind=="recomp"?"Static Recomp":kind.empty()?lang::tr("lobby.build_unknown"):kind;
  return version.empty()?name:name+" "+version;
}
std::string build_version(const std::string& build) { return build.substr(0,build.find(':')); }
int compare_versions(const std::string& a,const std::string& b) {
  int x[4]={0,0,0,0},y[4]={0,0,0,0};
  std::sscanf(a.c_str(),"%d.%d.%d.%d",&x[0],&x[1],&x[2],&x[3]);
  std::sscanf(b.c_str(),"%d.%d.%d.%d",&y[0],&y[1],&y[2],&y[3]);
  for(int i=0;i<4;++i) if(x[i]!=y[i]) return x[i]<y[i]?-1:1;
  return a==b?0:(a<b?-1:1);
}
std::string normalize_code(std::string code) {
  for(size_t at=code.find("\xEF\xBC\x83");at!=std::string::npos;at=code.find("\xEF\xBC\x83")) code.replace(at,3,"#");
  code.erase(std::remove_if(code.begin(),code.end(),[](unsigned char c){ return c==' '||c=='\t'||c=='\r'||c=='\n'; }),code.end());
  std::transform(code.begin(),code.end(),code.begin(),[](unsigned char c){ return static_cast<char>(std::toupper(c)); });
  return code;
}
bool valid_code(const std::string& code) { return code.size()<=10 && code_format(code); }
bool valid_iso_name(const std::string& name) { return !name.empty() && plain_text(name,32,96); }
bool open_to(const Json& profile,const std::string& mode) {
  // A profile without the field (0.8.1, or 0.8.5 before any choice) plays vanilla only.
  if(!profile.is_object() || !profile.count("open") || !profile["open"].is_array()) return mode=="vanilla";
  const std::string kind=mode.rfind("custom:",0)==0?"custom":mode;
  for(const auto& item:profile["open"]) if(item.is_string() && item.get<std::string>()==kind) return true;
  return false;
}
std::string iso_hash(const Json& profile) {
  if(!profile.is_object() || !profile.count("iso") || !profile["iso"].is_object()) return {};
  const auto& iso=profile["iso"];
  return iso.count("h") && iso["h"].is_string() ? iso["h"].get<std::string>() : std::string();
}
static bool has_mod(const Json& profile,const std::string& mod,std::string* version=nullptr) {
  if(!profile.is_object() || !profile.count("has") || !profile["has"].is_object()) return false;
  const auto& has=profile["has"];
  if(!has.count(mod) || !has[mod].is_string()) return false;
  if(version) *version=has[mod].get<std::string>();
  return true;
}
Json clean_profile(const Json& profile) {
  if(!profile.is_object()) return profile;
  Json out=profile;
  if(out.count("has")) {
    bool ok=out["has"].is_object() && out["has"].size()<=2;
    if(ok) for(auto it=out["has"].begin();it!=out["has"].end();++it)
      if((it.key()!="akaneia" && it.key()!="ace") || !mod_version(it.value())) ok=false;
    if(!ok) out.erase("has");
  }
  if(out.count("open")) {
    bool ok=out["open"].is_array() && out["open"].size()<=4;
    std::set<std::string> seen;
    if(ok) for(const auto& item:out["open"]) {
      if(!item.is_string()) { ok=false; break; }
      const auto mode=item.get<std::string>();
      if((mode!="vanilla" && mode!="akaneia" && mode!="ace" && mode!="custom") || !seen.insert(mode).second) { ok=false; break; }
    }
    if(!ok) out.erase("open");
  }
  if(out.count("iso")) {
    const auto& iso=out["iso"];
    bool ok=iso.is_object() && iso.size()==2 && iso.count("n") && iso.count("h") && iso["n"].is_string() && iso["h"].is_string() &&
            valid_iso_name(iso["n"].get<std::string>()) && hex16(iso["h"].get<std::string>());
    if(!ok) out.erase("iso");
  }
  return out;
}
std::string mode_problem(const Json& receiver,const Json& sender,const std::string& mode) {
  const auto mine=receiver.value("build",std::string()), theirs=sender.value("build",std::string());
  if(mode=="vanilla") {
    if(!open_to(receiver,"vanilla")) return "mode_closed";
    if(mine!=theirs) return build_version(mine)!=build_version(theirs)?"version":"build";
    return {};
  }
  // Mods run on Static Recomp whichever Game Build either player picked, so only the version counts.
  if(build_version(mine)!=build_version(theirs)) return "version";
  if(mode=="akaneia" || mode=="ace") {
    std::string have,want;
    if(!has_mod(receiver,mode,&have)) return "mode_missing";
    if(!open_to(receiver,mode)) return "mode_closed";
    if(has_mod(sender,mode,&want) && want!=have) return "mode_version";
    return {};
  }
  if(mode.rfind("custom:",0)==0) {
    if(iso_hash(receiver).empty()) return "mode_missing";
    if(!open_to(receiver,"custom")) return "mode_closed";
    if(iso_hash(receiver)!=mode.substr(7)) return "mode_hash";
    return {};
  }
  return "mode_unknown";
}
std::vector<std::string> common_modes(const Json& mine,const Json& theirs) {
  std::vector<std::string> modes;
  for(const char* mode:{"vanilla","akaneia","ace"})
    if(mode_problem(mine,theirs,mode).empty() && mode_problem(theirs,mine,mode).empty()) modes.push_back(mode);
  const auto hash=iso_hash(mine);
  if(!hash.empty() && mode_problem(mine,theirs,"custom:"+hash).empty() && mode_problem(theirs,mine,"custom:"+hash).empty())
    modes.push_back("custom:"+hash);
  return modes;
}
bool has_game_save(const std::string& card_dir) {
  std::error_code ec;
  for(std::filesystem::directory_iterator it(std::filesystem::u8path(card_dir),ec), end; !ec && it!=end; it.increment(ec)) {
    if(it->path().extension()!=".gci") continue;
    std::ifstream in(it->path(),std::ios::binary);
    char id[6]{};
    if(in.read(id,sizeof id) && std::string(id,sizeof id)=="GALE01") return true;
  }
  return false;
}
ModDiscs read_mod_scan(const std::string& detected_json,const std::string& game_dir) {
  ModDiscs out;
  const Json scan=Json::parse(detected_json,nullptr,false);
  if(!scan.is_object() || !scan.count("items") || !scan["items"].is_array()) return out;
  for(const auto& item:scan["items"]) {
    try {
      if(!item.is_object() || item.value("kind",std::string())!="mex") continue;
      if(item.value("needs_engine",std::string())!="recomp" || item.value("status",std::string())=="not_supported_yet") continue;
      const std::string mod=item.value("id",std::string());
      // Unknown m-ex packs stay custom; a filename cannot grant a named compatibility badge.
      if(mod!="akaneia" && mod!="ace") continue;
      auto disc=std::filesystem::u8path(item.value("path",std::string()));
      if(disc.empty()) continue;
      if(disc.is_relative()) disc=std::filesystem::u8path(game_dir)/disc;
      std::error_code ec;
      if(!std::filesystem::is_regular_file(disc,ec)) continue;
      // The version as other launchers accept it: letters, digits and . - _ +, at most 16.
      std::string version;
      for(char c:item.value("version",std::string()))
        if(std::isalnum(static_cast<unsigned char>(c)) || c=='.' || c=='-' || c=='_' || c=='+') version+=c;
      if(version.size()>16) version.resize(16);
      out.has[mod]=version; out.paths[mod]=disc.u8string();
    } catch(const std::exception&) {}   // a damaged entry is skipped, not the whole scan
  }
  return out;
}
std::string find_match_disc(const std::string& mode,const std::map<std::string,std::string>& mod_paths,
                            const std::string& custom_hash,const std::string& custom_iso,
                            const std::string& custom_name,std::string& path,std::string& name) {
  path.clear(); name.clear();
  if(mode=="vanilla") return {};
  if(mode=="akaneia" || mode=="ace") {
    const auto it=mod_paths.find(mode);
    if(it!=mod_paths.end() && !it->second.empty()) { path=it->second; name=mode=="akaneia"?"Akaneia":"ACE"; }
  } else if(mode.rfind("custom:",0)==0 && !custom_hash.empty() && mode.substr(7)==custom_hash && !custom_iso.empty()) {
    path=custom_iso; name=custom_name;
  }
  return path.empty()?lang::tr("lobby.mod_disc_missing"):std::string();
}
void lobby_log(const std::string& directory,const std::string& line) {
  static std::mutex mutex;
  if(directory.empty()) return;
  std::lock_guard<std::mutex> lock(mutex);
  try {
    const auto path=std::filesystem::u8path(directory+"/lobby.log");
    std::error_code ec;
    const auto size=std::filesystem::file_size(path,ec);
    if(!ec && size>1024*1024) std::filesystem::rename(path,std::filesystem::u8path(directory+"/lobby.log.1"),ec);
    std::ofstream out(path,std::ios::binary|std::ios::app);
    if(!out) return;
    SYSTEMTIME t{}; GetLocalTime(&t);
    char stamp[48];
    std::snprintf(stamp,sizeof stamp,"%04u-%02u-%02u %02u:%02u:%02u.%03u ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
    out<<stamp<<line<<"\r\n";
  } catch(...) {}   // a log line must never take the lobby down
}

// The vendored DHT asks the embedding program to provide these primitives.
extern "C" int dht_gettimeofday(struct timeval* out,struct timezone*) {
  out->tv_sec=static_cast<long>(std::time(nullptr)); out->tv_usec=0; return 0;
}
extern "C" int dht_sendto(int,const void* data,int length,int flags,const sockaddr* to,int size) {
  return sendto(dht_socket_handle,static_cast<const char*>(data),length,flags,to,size);
}
extern "C" int dht_blacklisted(const sockaddr*,int) { return 0; }
extern "C" void dht_hash(void* out,int size,const void* a,int alen,const void* b,int blen,const void* c,int clen) {
  std::string input;
  if(a && alen>0) input.append(static_cast<const char*>(a),alen);
  if(b && blen>0) input.append(static_cast<const char*>(b),blen);
  if(c && clen>0) input.append(static_cast<const char*>(c),clen);
  crypto_blake2b(static_cast<uint8_t*>(out),size,reinterpret_cast<const uint8_t*>(input.data()),input.size());
}
extern "C" int dht_random_bytes(void* out,size_t size) {
  return BCryptGenRandom(nullptr,static_cast<PUCHAR>(out),static_cast<ULONG>(size),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0?0:-1;
}

struct PeerLobby::Impl {
  struct Peer {
    sockaddr_in address{};
    Bytes32 xkey{};
    Json profile=Json::object();
    ULONGLONG seen=0,hello_sent=0,ping_sent=0,last_chat=0,last_request=0,last_friend_confirm=0,last_friend_request=0;
    bool visible=false;
    int protocol=1;                 // from the hello; 0.8.1 sends none
    std::string status="Offline",ping_nonce;
    Json stocks=Json::array();
    int ping=-1;
  };
  struct Candidate { sockaddr_in address{}; ULONGLONG hello_sent=0,seen=0; };
  // announce: search and also list ourselves under the topic (not for a friend code we look up).
  struct Topic { Bytes20 hash{}; ULONGLONG searched=0; int attempts=0; bool done=true,public_topic=false,announce=true; };
  struct Outbound { std::string target; Json data; ULONGLONG sent=0,expires=0; };
  struct Tracking { ULONGLONG sent=0; bool warned=false; };   // a request or an accept not yet acknowledged
  std::string directory,id,bootstrap;
  PeerTestOptions test;
  bool drop_in=false,drop_out=false;
  SOCKET socket=INVALID_SOCKET;
  int listen_port=0;
  Bytes32 ed_seed{},ed_public{},x_secret{},x_public{},log_key{};
  Bytes64 ed_secret{};
  Json profile=Json::object(),status={{"status","Online"},{"stocks",Json::array()}};
  bool visible=false;
  ULONGLONG started=GetTickCount64(),last_seed=0,last_presence=0,last_hello=0,last_topics=0,last_peers=0;
  std::map<std::string,Peer> peers;
  std::map<std::string,Candidate> candidates;
  std::map<std::string,Json> friends,incoming;
  std::set<std::string> outgoing_friends;
  std::map<std::string,Json> requests;
  std::map<std::string,ULONGLONG> request_expiry,seen_events;
  std::map<std::string,Tracking> tracking;
  std::map<std::string,Outbound> outbound;
  std::map<std::string,ULONGLONG> code_lookups;   // Add Friend by code: codes being looked for, until when
  std::deque<Json> launches;
  std::set<std::string> launched;
  std::vector<Json> messages;
  std::vector<Topic> topics;
  std::vector<sockaddr_in> seeds;
  std::deque<std::string> hello_nonces;          // our recent hellos: an echo of one is not a copy of us
  ULONGLONG last_request=0,last_chat=0,identity_warned=0;
  // Messages for the player (why a request did not go through, a delivery, a clock problem), each
  // with a sequence number so the launcher shows every one once, in the player's language.
  std::deque<Json> notices; unsigned notice_seq=0;
  std::map<std::string,ULONGLONG> log_seen,skew_seen,skew_replied;

  long long wall_clock() const { return static_cast<long long>(std::time(nullptr))+test.clock_offset; }
  static std::string short_id(const std::string& value) { return value.substr(0,8); }
  // Endpoints are logged as a keyed hash: the same address reads the same within one run, and no
  // IP address ever reaches lobby.log (which players send with crash reports).
  std::string endpoint_tag(const sockaddr_in& a) const {
    uint8_t raw[6]; std::memcpy(raw,&a.sin_addr,4); std::memcpy(raw+4,&a.sin_port,2);
    uint8_t digest[4]{};
    crypto_blake2b_keyed(digest,sizeof digest,log_key.data(),log_key.size(),raw,sizeof raw);
    return "ep:"+hex(digest,sizeof digest);
  }
  void log(const std::string& line) const { lobby_log(directory,line); }
  // The same problem is logged once a minute, not once per packet.
  void log_limited(const std::string& key,const std::string& line) {
    const auto now=GetTickCount64();
    if(log_seen.size()>512) log_seen.clear();
    auto& last=log_seen[key];
    if(last && now-last<60000) return;
    last=now; log(line);
  }
  void drop(const sockaddr_in& from,const std::string& what,const std::string& why) {
    log_limited(what+"|"+why.substr(0,32)+"|"+endpoint_key(from),"drop "+what+" from "+endpoint_tag(from)+": "+why);
  }
  std::string peer_name(const std::string& peer_id) const {
    auto it=peers.find(peer_id);
    if(it!=peers.end()) { auto name=it->second.profile.value("name",std::string()); if(!name.empty()) return name; }
    auto f=friends.find(peer_id);
    if(f!=friends.end()) { auto name=f->second.value("name",std::string()); if(!name.empty()) return name; }
    return lang::tr("lobby.other_player");
  }
  std::string peer_code(const std::string& peer_id) const {
    auto it=peers.find(peer_id);
    if(it!=peers.end()) return it->second.profile.value("code",std::string());
    auto f=friends.find(peer_id);
    return f==friends.end()?std::string():f->second.value("code",std::string());
  }
  int peer_protocol(const std::string& peer_id) const {
    auto it=peers.find(peer_id); return it==peers.end()?1:it->second.protocol;
  }
  void say(const std::string& key,const Json& args,const std::string& kind="info") {
    std::vector<std::pair<std::string,std::string>> list;
    for(auto it=args.begin();it!=args.end();++it) if(it.value().is_string()) list.emplace_back(it.key(),it.value().get<std::string>());
    notices.push_back({{"seq",++notice_seq},{"key",key},{"args",args},{"kind",kind},{"text",lang::fill(lang::tr(key),list)}});
    while(notices.size()>16) notices.pop_front();
  }
  // How a mode reads in a sentence: "Vanilla", "Akaneia 1.0.1", "ACE", or the custom ISO's name.
  static std::string mode_label(const std::string& mode,const Json& owner) {
    if(mode=="vanilla") return lang::tr("mode.vanilla");
    if(mode=="akaneia" || mode=="ace") {
      std::string label=mode=="akaneia"?"Akaneia":"ACE",version;
      if(has_mod(owner,mode,&version) && !version.empty()) label+=" "+version;
      return label;
    }
    if(mode.rfind("custom:",0)==0) {
      if(!iso_hash(owner).empty() && iso_hash(owner)==mode.substr(7)) return owner["iso"]["n"].get<std::string>();
      return lang::tr("mode.custom");
    }
    return mode;
  }
  // "version" says who has to update, from this player's side.
  std::string version_code(const std::string& code,const Json& them) const {
    if(code!="version") return code;
    return compare_versions(build_version(them.value("build",std::string())),build_version(profile.value("build",std::string())))<0
           ? "update_them" : "update_you";
  }
  // The arguments of a refusal sentence, seen from this player: {name} is the other player, and
  // {mine} and {theirs} are builds, versions or mod versions, whichever the reason is about.
  Json reason_args(const std::string& code,const std::string& mode,const Json& them,const std::string& name) const {
    const bool mod=mode=="akaneia" || mode=="ace";
    Json args={{"name",name},{"mode",mode_label(mode,mod && !has_mod(them,mode)?profile:them)}};
    if(code=="build") {
      args["mine"]=build_name(profile.value("build",std::string()));
      args["theirs"]=build_name(them.value("build",std::string()));
    } else if(code=="update_them" || code=="update_you") {
      args["mine"]=build_version(profile.value("build",std::string()));
      args["theirs"]=build_version(them.value("build",std::string()));
    } else if(code=="mode_version") {
      std::string mine,theirs; has_mod(profile,mode,&mine); has_mod(them,mode,&theirs);
      args["mode"]=mode=="akaneia"?"Akaneia":"ACE"; args["mine"]=mine; args["theirs"]=theirs;
    }
    return args;
  }

  Impl(const std::string& dir,const std::string& seed,int requested_port,const PeerTestOptions& options)
      :directory(dir),bootstrap(seed),test(options) {
    random_bytes(log_key.data(),log_key.size());
    load();
    sockaddr_in address{};
    if(bootstrap.empty() && !test.no_dht) {
      if(resolve("router.bittorrent.com:6881",address)) seeds.push_back(address);
      if(resolve("dht.transmissionbt.com:6881",address)) seeds.push_back(address);
    }
    const bool dht_only=bootstrap.rfind("dht://",0)==0;
    const auto custom=dht_only?bootstrap.substr(6):bootstrap;
    if(!custom.empty()) {
      if(!resolve(custom,address)) throw std::runtime_error(lang::tr("lobby.error.bootstrap"));
      seeds.insert(seeds.begin(),address);
      if(!dht_only) add_candidate(address); // A manually supplied peer can connect before DHT convergence.
    }
    if(seeds.empty() && !test.no_dht) throw std::runtime_error(lang::tr("lobby.error.no_dht"));
    socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    sockaddr_in local{}; local.sin_family=AF_INET; local.sin_port=htons(static_cast<u_short>(requested_port));
    local.sin_addr.s_addr=htonl(INADDR_ANY);
    if(socket==INVALID_SOCKET || bind(socket,reinterpret_cast<sockaddr*>(&local),sizeof local)!=0) {
      if(socket!=INVALID_SOCKET) closesocket(socket);
      throw std::runtime_error(lang::tr("lobby.error.port"));
    }
    int size=sizeof local; getsockname(socket,reinterpret_cast<sockaddr*>(&local),&size);
    listen_port=ntohs(local.sin_port);
    u_long nonblocking=1; ioctlsocket(socket,FIONBIO,&nonblocking);
    if(!test.no_dht) {
      dht_socket_handle=socket;
      Bytes20 node{}; random_bytes(node.data(),node.size());
      if(dht_init(1,-1,node.data(),reinterpret_cast<const uint8_t*>("MU01"))<0) {
        dht_socket_handle=INVALID_SOCKET; closesocket(socket); socket=INVALID_SOCKET;
        throw std::runtime_error(lang::tr("lobby.error.discovery"));
      }
    }
    refresh_topics();
    log("lobby start: id "+short_id(id)+", protocol "+std::to_string(lobby_protocol)+", UDP port "+std::to_string(listen_port)+
        (test.no_dht?", no DHT":"")+(custom.empty()?"":", seed "+endpoint_tag(address))+", friends "+std::to_string(friends.size()));
  }
  ~Impl() {
    if(!test.no_dht) { dht_uninit(); dht_socket_handle=INVALID_SOCKET; }
    if(socket!=INVALID_SOCKET) closesocket(socket);
    crypto_wipe(ed_secret.data(),ed_secret.size()); crypto_wipe(x_secret.data(),x_secret.size());
  }
  void save() const {
    std::filesystem::create_directories(std::filesystem::u8path(directory));
    Json file={{"ed_seed",hex(ed_seed.data(),ed_seed.size())},{"x_secret",hex(x_secret.data(),x_secret.size())},
               {"profile",profile},{"friends",Json::array()},{"incoming",Json::array()},{"outgoing",Json::array()}};
    for(const auto& item:friends) file["friends"].push_back(item.second);
    for(const auto& item:incoming) file["incoming"].push_back(item.second);
    for(const auto& item:outgoing_friends) file["outgoing"].push_back(item);
    auto path=std::filesystem::u8path(directory+"/lobby-peer-identity.json");
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(!out) throw std::runtime_error(lang::tr("lobby.error.save_identity"));
    out<<file.dump(2);
  }
  void load() {
    const auto path=std::filesystem::u8path(directory+"/lobby-peer-identity.json");
    if(std::filesystem::exists(path)) {
      std::ifstream in(path,std::ios::binary); auto file=Json::parse(in,nullptr,false);
      if(!file.is_object() || !file.count("ed_seed") || !file.count("x_secret") ||
         !file["ed_seed"].is_string() || !file["x_secret"].is_string() ||
         !unhex(file["ed_seed"].get<std::string>(),ed_seed.data(),ed_seed.size()) ||
         !unhex(file["x_secret"].get<std::string>(),x_secret.data(),x_secret.size()))
        throw std::runtime_error(lang::tr("lobby.error.identity"));
      if(file.value("profile",Json::object()).is_object()) profile=file.value("profile",Json::object());
      for(const auto& f:file.value("friends",Json::array())) if(f.is_object() && f.value("id",std::string()).size()==64) friends[f["id"]]=f;
      for(const auto& f:file.value("incoming",Json::array())) if(f.is_object() && f.value("id",std::string()).size()==64) incoming[f["id"]]=f;
      for(const auto& f:file.value("outgoing",Json::array())) if(f.is_string() && f.get<std::string>().size()==64) outgoing_friends.insert(f.get<std::string>());
    } else {
      random_bytes(ed_seed.data(),ed_seed.size()); random_bytes(x_secret.data(),x_secret.size());
      save();
    }
    auto seed=ed_seed; crypto_ed25519_key_pair(ed_secret.data(),ed_public.data(),seed.data());
    crypto_x25519_public_key(x_public.data(),x_secret.data());
    id=hex(ed_public.data(),ed_public.size());
  }
  static std::string code_topic(const std::string& code) { return "melee-unlocked-lobby-v1-code:"+code; }
  void refresh_topics() {
    std::map<std::string,Topic> old;
    for(const auto& t:topics) old.emplace(hex(t.hash.data(),t.hash.size()),t);
    topics.clear();
    std::vector<std::pair<std::string,bool>> names;   // topic, list ourselves under it
    if(visible) names.push_back({"melee-unlocked-lobby-v1-public",true});
    for(const auto& f:friends) names.push_back({"melee-unlocked-lobby-v1-friend:"+std::min(id,f.first)+":"+std::max(id,f.first),true});
    // Our own Slippi code, public lobby or not, so a friend can find us by the code alone.
    const auto own=normalize_code(profile.value("code",std::string()));
    if(valid_code(own)) names.push_back({code_topic(own),true});
    for(const auto& lookup:code_lookups) if(lookup.first!=own) names.push_back({code_topic(lookup.first),false});
    std::set<std::string> added;
    for(const auto& name:names) {
      Topic t; t.hash=topic_hash(name.first); t.public_topic=name.first=="melee-unlocked-lobby-v1-public"; t.announce=name.second;
      const auto key=hex(t.hash.data(),t.hash.size());
      if(!added.insert(key).second) continue;
      auto it=old.find(key);
      if(it!=old.end()) { t=it->second; t.announce=name.second; }
      topics.push_back(t);
    }
  }
  static void dht_result(void* closure,int event,const uint8_t* hash,const void* data,size_t length) {
    auto* self=static_cast<Impl*>(closure);
    if(event==DHT_EVENT_SEARCH_DONE && hash) {
      for(auto& topic:self->topics) if(std::equal(topic.hash.begin(),topic.hash.end(),hash)) topic.done=true;
    }
    if(event!=DHT_EVENT_VALUES || !data) return;
    auto compact=static_cast<const uint8_t*>(data);
    for(size_t i=0;i+6<=length;i+=6) {
      sockaddr_in peer{}; peer.sin_family=AF_INET;
      std::memcpy(&peer.sin_addr,compact+i,4); std::memcpy(&peer.sin_port,compact+i+4,2);
      self->add_candidate(peer);
    }
  }
  void add_candidate(const sockaddr_in& address) {
    if(address.sin_port==0 || candidates.size()>512) return;
    auto& entry=candidates[endpoint_key(address)]; entry.address=address; entry.seen=GetTickCount64();
  }
  void send_packet(const sockaddr_in& to,const Json& payload) {
    if(drop_out) return;
    const auto text=std::string(wire_prefix)+payload.dump();
    // 0.8.1 drops anything over 1400 bytes, and so does this launcher.
    if(text.size()>1400) { log_limited("oversize","not sent: "+payload.value("t",std::string("packet"))+" of "+std::to_string(text.size())+" bytes"); return; }
    sendto(socket,text.data(),static_cast<int>(text.size()),0,reinterpret_cast<const sockaddr*>(&to),sizeof to);
  }
  // The signed introduction. lv tells newer launchers what this one understands; 0.8.1 ignores it
  // (it checks only the fields it knows) and still verifies the signature over the whole body.
  Json hello_body() {
    const auto nonce=nonce_id(12);
    hello_nonces.push_back(nonce); while(hello_nonces.size()>64) hello_nonces.pop_front();
    Json body={{"v",1},{"lv",lobby_protocol},{"t","hello"},{"pk",id},{"xk",hex(x_public.data(),x_public.size())},
               {"profile",profile},{"visible",visible},{"time",wall_clock()},{"nonce",nonce}};
    auto content=body.dump(); Bytes64 signature{};
    crypto_ed25519_sign(signature.data(),ed_secret.data(),reinterpret_cast<const uint8_t*>(content.data()),content.size());
    body["sig"]=hex(signature.data(),signature.size());
    return body;
  }
  void send_hello(const sockaddr_in& address) {
    if(!valid_wire_profile(profile)) return;
    send_packet(address,hello_body());
  }
  Bytes32 key(const Peer& peer) const {
    Bytes32 secret{},derived{};
    crypto_x25519(secret.data(),x_secret.data(),peer.xkey.data());
    std::string input="MeleeUnlockedPeerLobby1";
    input.append(reinterpret_cast<const char*>(secret.data()),secret.size());
    const auto& first=std::lexicographical_compare(x_public.begin(),x_public.end(),peer.xkey.begin(),peer.xkey.end())?x_public:peer.xkey;
    const auto& second=&first==&x_public?peer.xkey:x_public;
    input.append(reinterpret_cast<const char*>(first.data()),first.size());
    input.append(reinterpret_cast<const char*>(second.data()),second.size());
    crypto_blake2b(derived.data(),derived.size(),reinterpret_cast<const uint8_t*>(input.data()),input.size());
    crypto_wipe(secret.data(),secret.size()); return derived;
  }
  void send_data(const std::string& target,const Json& plain) {
    auto it=peers.find(target); if(it==peers.end()) return;
    auto body=plain.dump();
    if(body.size()>500) { log_limited("oversize data","not sent: "+plain.value("k",std::string("data"))+" of "+std::to_string(body.size())+" bytes"); return; }
    auto shared=key(it->second);
    std::array<uint8_t,24> nonce{}; std::array<uint8_t,16> mac{};
    random_bytes(nonce.data(),nonce.size()); std::string cipher(body.size(),'\0');
    static constexpr uint8_t ad[]={'M','U','L','1'};
    crypto_aead_lock(reinterpret_cast<uint8_t*>(cipher.data()),mac.data(),shared.data(),nonce.data(),
                     ad,sizeof ad,reinterpret_cast<const uint8_t*>(body.data()),body.size());
    Json packet={{"v",1},{"t","data"},{"pk",id},{"nonce",hex(nonce.data(),nonce.size())},
                 {"mac",hex(mac.data(),mac.size())},{"ct",hex(reinterpret_cast<const uint8_t*>(cipher.data()),cipher.size())}};
    send_packet(it->second.address,packet);
    crypto_wipe(shared.data(),shared.size());
  }
  void queue_event(const std::string& target,const std::string& action,const Json& data) {
    if(outbound.size()>=1024) return;
    const auto event_id=nonce_id();
    Json packet={{"k","event"},{"id",event_id},{"action",action},{"data",data}};
    outbound[event_id]={target,packet,0,GetTickCount64()+30000};
    send_data(target,packet); outbound[event_id].sent=GetTickCount64();
  }
  bool busy() const {
    if(status.value("status",std::string("Online"))!="Online") return true;
    for(const auto& request:requests) if(request.second.value("state",std::string())=="accepted") return true;
    return false;
  }
  void chat_append(const Json& message) {
    messages.push_back(message);
    if(messages.size()>100) messages.erase(messages.begin(),messages.begin()+messages.size()-100);
  }
  // Why this player cannot take a request right now, as a code both sides turn into their own
  // sentence; empty when it can go ahead.
  std::string refusal_for(const std::string& sender,const Peer& peer,const Json& data,const Json& sender_profile,
                          const std::string& mode) const {
    // Friends may ask while this player is out of the public lobby; strangers only inside it.
    if(!(visible && peer.visible) && !friends.count(sender)) return "hidden";
    if(busy()) return "busy";
    if(!profile.value("ready",false)) return "not_ready";
    // The request says whether its sender is ready, so a profile heard a few seconds ago cannot
    // refuse a player who has just finished setup.
    const bool sender_ready=data.count("ready") && data["ready"].is_boolean() ? data["ready"].get<bool>()
                                                                               : peer.profile.value("ready",false);
    if(!sender_ready) return "sender_not_ready";
    return mode_problem(profile,sender_profile,mode);
  }
  // Refuses a request out loud: the sender learns why (0.8.1 has no use for the reason and would never
  // acknowledge it, so it only sees its request expire), and so does this player, who would
  // otherwise never know someone tried.
  void refuse(const std::string& sender,const std::string& rid,const std::string& code,const std::string& mode,
              const Json& sender_profile) {
    if(peer_protocol(sender)>=2)
      queue_event(sender,"decline",{{"request",rid},{"code",code},{"mode",mode},{"build",profile.value("build",std::string())},
                                    {"reason",wire_reason(code)}});
    const auto vcode=version_code(code,sender_profile);
    const std::string line="lobby.refused."+vcode;
    say(lang::has_key(line)?line:"lobby.refused.other",reason_args(vcode,mode,sender_profile,peer_name(sender)),"refused");
    log("refuse request "+short_id(rid)+" from "+short_id(sender)+": "+code+" (mode "+mode+")");
  }
  // A refusal as this player reads it (the other side's reason code, turned around).
  std::string declined_text(const std::string& code,const std::string& mode,const Json& them,const std::string& name) const {
    const auto vcode=version_code(code,them);
    const std::string line="lobby.declined."+vcode;
    Json args=reason_args(vcode,mode,them,name); args["reason"]=wire_reason(code);
    std::vector<std::pair<std::string,std::string>> list;
    for(auto it=args.begin();it!=args.end();++it) if(it.value().is_string()) list.emplace_back(it.key(),it.value().get<std::string>());
    return lang::fill(lang::tr(lang::has_key(line)?line:"lobby.declined.other"),list);
  }
  // The other side refused or cancelled a request: tell this player why, in their language.
  void declined(const std::string& sender,const std::string& code,const std::string& mode,const Json& data) {
    auto peer=peers.find(sender);
    Json them=peer==peers.end()?Json::object():peer->second.profile;
    if(data.count("build") && data["build"].is_string() && data["build"].get<std::string>().size()<=80) them["build"]=data["build"];
    const auto vcode=version_code(code,them);
    const std::string line="lobby.declined."+vcode;
    if(!code.empty() && lang::has_key(line)) { say(line,reason_args(vcode,mode,them,peer_name(sender)),"declined"); return; }
    std::string reason=data.value("reason",std::string());
    if(reason.size()>200) reason.resize(200);
    if(!plain_text(reason,200,200)) reason.clear();
    say("lobby.declined.other",{{"name",peer_name(sender)},{"reason",reason.empty()?code:reason}},"declined");
  }
  // Both sides agree on a match: hand it to the launcher once.
  void push_launch(const std::string& rid) {
    if(launched.count(rid) || !requests.count(rid)) return;
    const auto& r=requests[rid];
    const auto other=r.value("from",std::string())==id?r.value("to",std::string()):r.value("from",std::string());
    auto peer=peers.find(other);
    if(peer==peers.end()) { log("launch "+short_id(rid)+": the other player is no longer known"); return; }
    const auto mains=profile.value("mains",Json::array());
    launched.insert(rid);
    launches.push_back({{"request",rid},{"opponent",other},{"name",peer->second.profile.value("name",std::string())},
                        {"code",peer->second.profile.value("code",std::string())},{"build",profile.value("build",std::string())},
                        {"mode",r.value("mode",std::string("vanilla"))},
                        {"character",!mains.empty() && mains[0].is_number_integer()?mains[0].get<int>():2}});
    say("lobby.accepted_start",{{"name",peer_name(other)}},"launch");
    log("launch "+short_id(rid)+" against "+short_id(other)+", mode "+r.value("mode",std::string("vanilla")));
  }
  void ask_friend(const std::string& target) {
    outgoing_friends.insert(target); save();
    queue_event(target,"friend",Json::object());
    say("lobby.friend.sent",{{"name",peer_name(target)}});
    log("friend request to "+short_id(target));
  }
  // A friend's name or code changed (or 0.8.1 stored no code): keep the list current.
  void remember_friend(const std::string& peer_id,const Json& their) {
    auto f=friends.find(peer_id); if(f==friends.end()) return;
    const auto name=their.value("name",std::string()), code=their.value("code",std::string());
    if(f->second.value("name",std::string())==name && f->second.value("code",std::string())==code) return;
    f->second["name"]=name; f->second["code"]=code;
    try { save(); } catch(...) {}
  }
  // Add Friend by code: this hello came from the player being looked for.
  void answer_lookup(const std::string& sender,const Json& their) {
    if(code_lookups.empty()) return;
    auto it=code_lookups.find(normalize_code(their.value("code",std::string())));
    if(it==code_lookups.end()) return;
    code_lookups.erase(it); refresh_topics();
    if(friends.count(sender)) { say("lobby.friend.already",{{"name",peer_name(sender)}}); return; }
    ask_friend(sender);
  }
  void clock_skew(const std::string& sender,const Json& their,long long skew) {
    const auto now=GetTickCount64();
    if(skew_seen.size()>256) skew_seen.clear();
    auto& last=skew_seen[sender];
    if(last && now-last<600000) return;
    last=now;
    auto name=their.is_object()?their.value("name",std::string()):std::string();
    if(name.empty()) name=lang::tr("lobby.other_player");
    say("lobby.clock_skew",{{"name",name},{"minutes",std::to_string((std::llabs(skew)+59)/60)}},"warn");
  }
  void identity_clash() {
    const auto now=GetTickCount64();
    if(identity_warned && now-identity_warned<600000) return;
    identity_warned=now;
    say("lobby.identity_copied",Json::object(),"warn");
    log("another launcher is using this lobby identity (lobby-peer-identity.json was copied)");
  }
  bool handle_event(const std::string& sender,const Json& event) {
    if(!event.count("action") || !event["action"].is_string() || !event.count("data") || !event["data"].is_object()) return false;
    const auto action=event["action"].get<std::string>();
    const auto& data=event["data"];
    auto peer=peers.find(sender); if(peer==peers.end()) return false;
    auto now=GetTickCount64();
    const auto name=peer_name(sender);
    if(action=="friend") {
      // Anyone who found this player (in the public lobby, or by their Slippi code while they are
      // hidden) may ask; the player still has to accept. A friend asking again, because their side
      // lost the friendship, is confirmed straight away.
      if(friends.count(sender)) { queue_event(sender,"friend_accept",Json::object()); return true; }
      if(now-peer->second.last_friend_request<10000) return true;
      peer->second.last_friend_request=now;
      if(!incoming.count(sender) && incoming.size()<100) {
        incoming[sender]={{"id",sender},{"name",peer->second.profile.value("name",std::string("?"))},
                          {"code",peer->second.profile.value("code",std::string())}};
        save();
        say("lobby.friend.incoming",{{"name",name},{"id",sender}},"friend");
        log("friend request from "+short_id(sender));
      }
    } else if(action=="friend_accept") {
      if(!outgoing_friends.count(sender) && !friends.count(sender)) return false;
      if(friends.size()>=100) return false;
      const bool fresh=friends.count(sender)==0;
      incoming.erase(sender); outgoing_friends.erase(sender);
      friends[sender]={{"id",sender},{"name",peer->second.profile.value("name",std::string("?"))},
                       {"code",peer->second.profile.value("code",std::string())}};
      refresh_topics(); save();
      if(fresh) { say("lobby.friend.accepted",{{"name",name}},"friend_ok"); log("friend request accepted by "+short_id(sender)); }
    } else if(action=="friend_decline") {
      incoming.erase(sender);
      if(outgoing_friends.erase(sender)) say("lobby.friend.declined",{{"name",name}});
      save();
    } else if(action=="unfriend") {
      friends.erase(sender); incoming.erase(sender); outgoing_friends.erase(sender); refresh_topics(); save();
    } else if(action=="chat") {
      if(!visible || !peer->second.visible || !data.count("text") || !data["text"].is_string()) return false;
      auto value=data["text"].get<std::string>(); if(value.empty() || value.size()>300) return false;
      if(now-peer->second.last_chat<1000) return false;
      peer->second.last_chat=now;
      chat_append({{"id",event["id"]},{"sender",sender},{"name",peer->second.profile.value("name",std::string("?"))},
                   {"text",value},{"time",now},{"wall_time",std::time(nullptr)}});
    } else if(action=="request") {
      auto rid=data.value("request",std::string());
      if(rid.size()!=32) return false;
      if(now-peer->second.last_request<3000) {
        log_limited("hold "+sender,"hold request from "+short_id(sender)+": another one within 3 s");
        return false;
      }
      std::string mode=data.value("mode",std::string("vanilla"));
      if(mode.size()>40 || !plain_text(mode,40,40)) mode="unknown";
      // The request carries the sender's current Game Build, which beats the profile last heard.
      Json sender_profile=peer->second.profile;
      if(data.count("build") && data["build"].is_string() && data["build"].get<std::string>().size()<=80) sender_profile["build"]=data["build"];
      std::string refusal=refusal_for(sender,peer->second,data,sender_profile,mode);
      if(refusal.empty() && !requests.empty() && !requests.count(rid)) {
        // Both players pressed Send Match Request at once: each has its own request out to the other.
        // The one with the lower id withdraws its own and takes this one, so its player gets the
        // Accept prompt; the other refuses this one and keeps its own, which the first now shows.
        std::string mine;
        for(const auto& r:requests)
          if(r.second.value("from",std::string())==id && r.second.value("to",std::string())==sender &&
             r.second.value("state",std::string())=="pending") mine=r.first;
        if(!mine.empty() && requests.size()==1 && id<sender) {
          requests.erase(mine); request_expiry.erase(mine); tracking.erase(mine);
          queue_event(sender,"cancel",{{"request",mine},{"code","crossed"}});
          log("requests crossed with "+short_id(sender)+": taking theirs");
        } else if(!mine.empty() && requests.size()==1) {
          log("requests crossed with "+short_id(sender)+": keeping ours");
          return true;   // our request stands; the other side is taking it (see above)
        } else refusal="answering";
      }
      if(!refusal.empty()) { refuse(sender,rid,refusal,mode,sender_profile); return true; }
      peer->second.last_request=now;
      if(!requests.count(rid)) {
        requests[rid]={{"id",rid},{"from",sender},{"to",id},{"state","pending"},{"transport","slippi-direct"},{"mode",mode}};
        request_expiry[rid]=now+30000;
        peer->second.ping_nonce=nonce_id(8); peer->second.ping_sent=now;
        send_data(sender,{{"k","ping"},{"nonce",peer->second.ping_nonce}});
        log("request "+short_id(rid)+" in from "+short_id(sender)+", mode "+mode);
      }
    } else if(action=="accept") {
      auto rid=data.value("request",std::string()); auto it=requests.find(rid);
      if(it==requests.end() || it->second.value("from",std::string())!=id || it->second.value("to",std::string())!=sender) {
        // Answered after the request ran out here: say so instead of leaving them waiting for a match.
        if(peer->second.protocol>=2 && rid.size()==32)
          queue_event(sender,"decline",{{"request",rid},{"code","expired"},{"reason",wire_reason("expired")}});
        log("accept for an unknown request "+short_id(rid)+" from "+short_id(sender));
        return true;
      }
      if(it->second.value("state",std::string())!="pending") return true;   // a resend
      if(status.value("status",std::string("Online"))!="Online") {
        // This player started a game while waiting.
        requests.erase(it); request_expiry.erase(rid); tracking.erase(rid);
        queue_event(sender,"decline",{{"request",rid},{"code","busy"},{"reason",wire_reason("busy")}});
        say("lobby.cancelled_busy",{{"name",name}},"warn");
        log("accept of "+short_id(rid)+" arrived while in a game");
        return true;
      }
      it->second["state"]="accepted"; it->second["confirmed"]=true; request_expiry[rid]=now+120000; tracking.erase(rid);
      log("request "+short_id(rid)+" accepted by "+short_id(sender));
      push_launch(rid);
    } else if(action=="cancel") {
      auto rid=data.value("request",std::string()); auto it=requests.find(rid);
      if(it!=requests.end() && (it->second.value("from",std::string())==sender || it->second.value("to",std::string())==sender)) {
        const bool mine=it->second.value("from",std::string())==id;
        const bool pending=it->second.value("state",std::string())=="pending";
        const auto code=data.value("code",std::string("user"));
        if(mine && pending) say(code=="auto"?"lobby.declined.auto":"lobby.declined.user",{{"name",name}},"declined");
        else if(!mine && pending) { if(code!="crossed") say("lobby.withdrawn",{{"name",name}},"withdrawn"); }
        else say("lobby.cancelled",{{"name",name}},"warn");
        requests.erase(it); request_expiry.erase(rid); tracking.erase(rid);
        log("request "+short_id(rid)+" cancelled by "+short_id(sender)+" ("+(code.size()<=16?code:"?")+")");
      }
    } else if(action=="decline") {
      auto rid=data.value("request",std::string()); auto it=requests.find(rid);
      if(it!=requests.end() &&
         ((it->second.value("from",std::string())==id && it->second.value("to",std::string())==sender) ||
          (it->second.value("to",std::string())==id && it->second.value("from",std::string())==sender))) {
        std::string code=data.value("code",std::string());
        if(code.size()>24 || !plain_text(code,24,24)) code.clear();
        const auto mode=it->second.value("mode",std::string("vanilla"));
        requests.erase(it); request_expiry.erase(rid); tracking.erase(rid);
        declined(sender,code,mode,data);
        log("request "+short_id(rid)+" declined by "+short_id(sender)+": "+(code.empty()?std::string("no reason"):code));
      }
    } else return false;
    return true;
  }
  void on_hello(const Json& packet,const sockaddr_in& from) {
    if(packet.value("v",0)!=1 || !packet.count("profile") || !packet.count("sig") || !packet["sig"].is_string() ||
       !packet.count("time") || !packet["time"].is_number_integer()) { drop(from,"hello","malformed"); return; }
    Bytes32 pub{},xpub{}; Bytes64 signature{};
    const auto sender=packet.value("pk",std::string());
    if(!unhex(sender,pub.data(),pub.size()) || !unhex(packet.value("xk",std::string()),xpub.data(),xpub.size()) ||
       !unhex(packet["sig"].get<std::string>(),signature.data(),signature.size())) { drop(from,"hello","bad keys"); return; }
    // The signature first, so everything reported below really came from that player.
    auto signed_part=packet; signed_part.erase("sig"); const auto text=signed_part.dump();
    if(crypto_ed25519_check(signature.data(),pub.data(),reinterpret_cast<const uint8_t*>(text.data()),text.size())!=0) {
      drop(from,"hello","bad signature"); return;
    }
    if(sender==id) {
      // Our own hello coming back is normal (the DHT lists us too). One we never sent means this
      // identity file was copied to another PC, and the two launchers ignore each other.
      const auto nonce=packet.value("nonce",std::string());
      if(std::find(hello_nonces.begin(),hello_nonces.end(),nonce)==hello_nonces.end()) identity_clash();
      return;
    }
    if(!valid_wire_profile(packet["profile"])) {
      drop(from,"hello","invalid profile from "+short_id(sender)+": "+profile_problem(packet["profile"]));
      return;
    }
    const long long skew=wall_clock()-packet["time"].get<long long>();
    if(skew>120 || skew<-120) {
      // Hellos older than two minutes are refused so a recorded one cannot be replayed, which also
      // means two PCs with clocks that far apart never see each other. Say so instead of nothing.
      drop(from,"hello","clocks differ by "+std::to_string(skew)+" s, from "+short_id(sender));
      clock_skew(sender,packet["profile"],skew);
      // Answer (once a minute) so their launcher can see the difference and tell its player too.
      if(skew_replied.size()>256) skew_replied.clear();
      auto& replied=skew_replied[sender];
      const auto now=GetTickCount64();
      if(!replied || now-replied>60000) { replied=now; send_hello(from); }
      return;
    }
    if(!peers.count(sender) && peers.size()>=256) { drop(from,"hello","peer table full"); return; }
    const bool known=peers.count(sender)!=0;
    auto& peer=peers[sender]; peer.address=from; peer.xkey=xpub; peer.profile=clean_profile(packet["profile"]);
    peer.protocol=packet.count("lv") && packet["lv"].is_number_integer() && packet["lv"].get<int>()>=1 && packet["lv"].get<int>()<1000
                  ? packet["lv"].get<int>() : 1;
    if(!known) log("peer "+short_id(sender)+" at "+endpoint_tag(from)+", protocol "+std::to_string(peer.protocol)+
                   ", build "+peer.profile.value("build",std::string())+(peer.profile.value("ready",false)?"":", setup needed"));
    auto now=GetTickCount64();
    if(now-peer.hello_sent>1000) { send_hello(from); peer.hello_sent=now; }
    if(friends.count(sender)) {
      if(now-peer.last_friend_confirm>30000) { queue_event(sender,"friend_accept",Json::object()); peer.last_friend_confirm=now; }
      remember_friend(sender,peer.profile);
    }
    const bool share_status=visible || friends.count(sender)!=0;
    send_data(sender,{{"k","presence"},{"visible",visible},
                      {"status",share_status?status.value("status",std::string("Online")):std::string("Online")},
                      {"stocks",share_status?status.value("stocks",Json::array()):Json::array()}});
    answer_lookup(sender,peer.profile);
  }
  void on_data(const Json& packet,const sockaddr_in& from) {
    if(packet.value("v",0)!=1) return;
    auto sender=packet.value("pk",std::string()); auto it=peers.find(sender);
    if(it==peers.end()) {
      // They know us and we do not know them: our hello to them, or theirs to us, was lost. Send
      // ours (they answer with theirs) instead of dropping everything they send, which is how a
      // match request could vanish without a trace.
      if(sender.size()==64 && sender!=id) {
        drop(from,"data","from unknown player "+short_id(sender)+": introducing ourselves");
        add_candidate(from);
      }
      return;
    }
    if(!packet.count("ct") || !packet["ct"].is_string()) return;
    Bytes32 nonce_key=key(it->second); std::array<uint8_t,24> nonce{}; std::array<uint8_t,16> mac{};
    auto encoded=packet["ct"].get<std::string>();
    if(encoded.size()>1000 || encoded.size()%2 ||
       !unhex(packet.value("nonce",std::string()),nonce.data(),nonce.size()) ||
       !unhex(packet.value("mac",std::string()),mac.data(),mac.size())) return;
    std::string cipher(encoded.size()/2,'\0'),plain(cipher.size(),'\0');
    if(!unhex(encoded,reinterpret_cast<uint8_t*>(cipher.data()),cipher.size())) return;
    static constexpr uint8_t ad[]={'M','U','L','1'};
    int verified=crypto_aead_unlock(reinterpret_cast<uint8_t*>(plain.data()),mac.data(),nonce_key.data(),nonce.data(),
                                    ad,sizeof ad,reinterpret_cast<const uint8_t*>(cipher.data()),cipher.size());
    crypto_wipe(nonce_key.data(),nonce_key.size());
    if(verified!=0) { drop(from,"data","cannot decrypt, from "+short_id(sender)); return; }
    auto content=Json::parse(plain,nullptr,false); if(!content.is_object()) return;
    it->second.address=from; it->second.seen=GetTickCount64();
    const auto kind=content.value("k",std::string());
    if(kind=="presence") {
      auto value=content.value("status",std::string());
      if(value!="Online" && value!="Launching" && value!="In game" && value!="In match") return;
      auto stocks=content.value("stocks",Json::array()); if(!valid_stocks(stocks)) return;
      it->second.visible=content.value("visible",false);
      it->second.status=value; it->second.stocks=value=="In match"?stocks:Json::array();
    } else if(kind=="ping") {
      auto nonce=content.value("nonce",std::string()); if(nonce.size()==16) send_data(sender,{{"k","pong"},{"nonce",nonce}});
    } else if(kind=="pong") {
      if(content.value("nonce",std::string())==it->second.ping_nonce && it->second.ping_sent)
        it->second.ping=static_cast<int>(GetTickCount64()-it->second.ping_sent);
    } else if(kind=="ack") {
      auto ref=content.value("ref",std::string()); auto pending=outbound.find(ref);
      if(pending!=outbound.end() && pending->second.target==sender) {
        const auto action=pending->second.data.value("action",std::string());
        const auto rid=pending->second.data["data"].value("request",std::string());
        auto request=requests.find(rid);
        if(action=="accept" && request!=requests.end() && request->second.value("state",std::string())=="accepted") {
          request->second["confirmed"]=true; tracking.erase(rid);
          push_launch(rid);
        }
        if(action=="request" && request!=requests.end() && request->second.value("state",std::string())=="pending" &&
           !request->second.value("delivered",false)) {
          request->second["delivered"]=true; tracking.erase(rid);
          say("lobby.delivered",{{"name",peer_name(sender)}},"delivered");
          log("request "+short_id(rid)+" delivered to "+short_id(sender));
        }
        outbound.erase(pending);
      }
    } else if(kind=="event") {
      auto event_id=content.value("id",std::string()); if(event_id.size()!=32) return;
      auto seen=seen_events.find(event_id);
      if(seen!=seen_events.end() || handle_event(sender,content)) {
        if(seen_events.size()>=2048) seen_events.erase(seen_events.begin());
        seen_events[event_id]=GetTickCount64();
        send_data(sender,{{"k","ack"},{"ref",event_id}});
      }
    } else if(kind=="peers" && content.count("list") && content["list"].is_array()) {
      size_t count=0;
      for(const auto& p:content["list"]) {
        if(++count>16) break;
        if(!p.is_string()) continue; sockaddr_in address{};
        if(numeric_endpoint(p.get<std::string>(),address)) add_candidate(address);
      }
    }
  }
  void set_presence(const Json& next) {
    if(!next.is_object()) return;
    const auto value=next.value("status",std::string("Online"));
    const auto stocks=next.value("stocks",Json::array());
    if((value=="Online" || value=="Launching" || value=="In game" || value=="In match") && valid_stocks(stocks))
      status={{"status",value},{"stocks",value=="In match"?stocks:Json::array()}};
  }
  // Only what other players need goes into the signed profile; launcher settings stay local, and the
  // hello stays well under the 1400-byte limit (worst case about 1000 bytes with a 32-character
  // custom ISO name, both mod badges and every field at its longest).
  static Json public_profile(const Json& next) {
    Json out=Json::object();
    if(!next.is_object()) return out;
    for(const char* field:{"name","code","location","build","mains","ready","wins","losses","has","open","iso"})
      if(next.count(field)) out[field]=next[field];
    for(const char* field:{"wins","losses"})
      if(out.count(field) && (!out[field].is_number_integer() || out[field].get<long long>()<0 || out[field].get<long long>()>999999)) out.erase(field);
    if(out.count("code") && out["code"].is_string()) out["code"]=normalize_code(out["code"].get<std::string>());
    if(!out.count("location")) out["location"]="";
    return out;
  }
  // Re-announces this player (ready, Game Build, mods) to everyone known, without joining the public
  // lobby: players who keep it hidden still show friends the right state.
  void update_profile(const Json& next,bool show=false) {
    Json candidate=public_profile(next);
    if(!valid_profile(candidate)) throw std::runtime_error(lang::tr("lobby.error.profile"));
    if(candidate.count("iso")) {
      const auto& iso=candidate["iso"];
      if(!iso.is_object() || !iso.count("n") || !iso["n"].is_string() || !valid_iso_name(iso["n"].get<std::string>()))
        throw std::runtime_error(lang::tr("lobby.error.iso_name"));
    }
    candidate=clean_profile(candidate);
    const bool changed=candidate!=profile;
    const bool code_changed=normalize_code(candidate.value("code",std::string()))!=normalize_code(profile.value("code",std::string()));
    profile=candidate;
    if(show && !visible) { visible=true; log("joined the public lobby"); }
    if(code_changed || show) refresh_topics();
    if(changed || show) save();
    if(changed) {
      for(auto& peer:peers) peer.second.hello_sent=0;   // everyone hears about it on the next tick
      log(std::string("profile: ")+(profile.value("ready",false)?"ready":"setup needed")+", build "+profile.value("build",std::string())+
          (profile.count("has")?", mods "+profile["has"].dump():"")+(profile.count("open")?", open to "+profile["open"].dump():"")+
          (profile.count("iso")?", custom ISO "+iso_hash(profile):""));
    }
    if(show) { last_hello=0; last_presence=0; }
  }
  void join(const Json& next) { update_profile(next,true); }
  // Add Friend by code: a player already known is asked at once; otherwise the code's DHT topic is
  // searched for a minute (players announce their own code there, public lobby or not).
  void add_friend_by_code(const std::string& raw) {
    const auto code=normalize_code(raw);
    if(!valid_code(code)) throw std::runtime_error(lang::tr("lobby.error.code_format"));
    if(code==normalize_code(profile.value("code",std::string()))) throw std::runtime_error(lang::tr("lobby.friend.self"));
    for(const auto& f:friends) if(normalize_code(f.second.value("code",std::string()))==code)
      throw std::runtime_error(lang::tr("lobby.friend.already",{{"name",f.second.value("name",std::string("?"))}}));
    const auto now=GetTickCount64();
    for(const auto& peer:peers) if(peer.second.seen && now-peer.second.seen<20000 &&
                                   normalize_code(peer.second.profile.value("code",std::string()))==code) {
      // 0.8.1 only takes friend requests inside the public lobby, from someone else in it.
      if(peer.second.protocol<2 && !(visible && peer.second.visible))
        throw std::runtime_error(lang::tr("lobby.friend.legacy",{{"name",peer_name(peer.first)},
                                          {"version",build_version(profile.value("build",std::string()))}}));
      ask_friend(peer.first); return;
    }
    if(code_lookups.size()>=8 && !code_lookups.count(code)) throw std::runtime_error(lang::tr("lobby.error.too_many_lookups"));
    code_lookups[code]=now+60000; refresh_topics();
    // Their code topic is searched right away rather than on the next round.
    for(auto& topic:topics) if(!topic.announce) { topic.attempts=0; topic.searched=0; }
    say("lobby.friend.looking",{{"code",code}});
    log("friend lookup started");
  }
  void command(const std::string& action,const Json& data) {
    if(action=="leave") {
      if(visible) log("left the public lobby");
      visible=false; refresh_topics(); last_presence=0;
      for(const auto& peer:peers) if(peer.second.seen && GetTickCount64()-peer.second.seen<20000) {
        const bool share=friends.count(peer.first)!=0;
        send_data(peer.first,{{"k","presence"},{"visible",false},
                              {"status",share?status.value("status",std::string("Online")):std::string("Online")},
                              {"stocks",share?status.value("stocks",Json::array()):Json::array()}});
      }
      for(auto it=requests.begin();it!=requests.end();) {
        if(it->second.value("state",std::string())=="pending") { request_expiry.erase(it->first); it=requests.erase(it); }
        else ++it;
      }
      return;
    }
    if(action=="available") {
      for(auto it=requests.begin();it!=requests.end();) {
        if(it->second.value("state",std::string())=="accepted") { request_expiry.erase(it->first); it=requests.erase(it); }
        else ++it;
      }
      return;
    }
    auto now=GetTickCount64();
    if(action=="chat") {
      auto value=data.value("text",std::string());
      if(!visible || value.empty() || value.size()>300 || now-last_chat<1000 ||
         std::any_of(value.begin(),value.end(),[](unsigned char c){return c<32;}))
        throw std::runtime_error(lang::tr("lobby.error.chat"));
      last_chat=now;
      auto message_id=nonce_id();
      chat_append({{"id",message_id},{"sender",id},{"name",profile.value("name",std::string())},
                   {"text",value},{"time",now},{"wall_time",std::time(nullptr)}});
      for(const auto& peer:peers) if(peer.second.visible && now-peer.second.seen<20000)
        queue_event(peer.first,"chat",{{"text",value}});
      return;
    }
    if(action=="profile") { update_profile(data); return; }
    if(action=="friend_code") { add_friend_by_code(data.value("code",std::string())); return; }
    auto target=data.value("target",std::string());
    if(action=="friend" || action=="friend_accept" || action=="friend_decline" || action=="unfriend") {
      if(action=="friend") {
        if(!peers.count(target) || now-peers.at(target).seen>=20000) throw std::runtime_error(lang::tr("lobby.error.offline"));
        if(friends.count(target)) throw std::runtime_error(lang::tr("lobby.friend.already",{{"name",peer_name(target)}}));
        ask_friend(target);
        return;
      } else if(action=="friend_accept" || action=="friend_decline") {
        if(!incoming.count(target)) throw std::runtime_error(lang::tr("lobby.error.friend_gone"));
        const auto entry=incoming[target];
        incoming.erase(target);
        if(action=="friend_accept") {
          friends[target]={{"id",target},{"name",peers.count(target)?peers.at(target).profile.value("name",std::string("?")):entry.value("name",std::string("?"))},
                           {"code",peers.count(target)?peers.at(target).profile.value("code",std::string()):entry.value("code",std::string())}};
          refresh_topics();
        }
        log(std::string("friend request ")+(action=="friend_accept"?"accepted":"declined")+" for "+short_id(target));
      } else {
        if(!friends.count(target)) throw std::runtime_error(lang::tr("lobby.error.not_friend"));
        friends.erase(target); outgoing_friends.erase(target); refresh_topics();
        log("unfriended "+short_id(target));
      }
      save(); if(peers.count(target)) queue_event(target,action,Json::object());
      return;
    }
    if(action=="request") {
      auto peer=peers.find(target);
      const auto name=peer_name(target);
      // Friends can play while either of them is out of the public lobby.
      if(peer==peers.end() || now-peer->second.seen>=20000 || (!(visible && peer->second.visible) && !friends.count(target)))
        throw std::runtime_error(lang::tr("lobby.error.left",{{"name",name}}));
      if(busy()) throw std::runtime_error(lang::tr("lobby.error.self_busy"));
      if(!profile.value("ready",false)) throw std::runtime_error(lang::tr("lobby.error.self_not_ready"));
      if(peer->second.status!="Online") throw std::runtime_error(lang::tr("lobby.declined.busy",{{"name",name}}));
      if(!peer->second.profile.value("ready",false)) throw std::runtime_error(lang::tr("lobby.declined.not_ready",{{"name",name}}));
      std::string mode=data.value("mode",std::string("vanilla"));
      // The same checks the other launcher makes, from its side, so a request that would be refused
      // is not sent at all and the reason shows straight away.
      const auto problem=mode_problem(peer->second.profile,profile,mode);
      if(!problem.empty()) throw std::runtime_error(declined_text(problem,mode,peer->second.profile,name));
      const auto own=mode_problem(profile,peer->second.profile,mode);
      if(!own.empty()) throw std::runtime_error(lang::tr("lobby.error.self_mode",{{"mode",mode_label(mode,profile)}}));
      if(!requests.empty() || now-last_request<3000) throw std::runtime_error(lang::tr("lobby.error.pending"));
      last_request=now; auto rid=nonce_id();
      requests[rid]={{"id",rid},{"from",id},{"to",target},{"state","pending"},{"transport","slippi-direct"},{"mode",mode},{"delivered",false}};
      request_expiry[rid]=now+30000; tracking[rid]={now,false};
      queue_event(target,"request",{{"request",rid},{"mode",mode},{"build",profile.value("build",std::string())},{"ready",true}});
      peer->second.ping_nonce=nonce_id(8); peer->second.ping_sent=now;
      send_data(target,{{"k","ping"},{"nonce",peer->second.ping_nonce}});
      say("lobby.sent",{{"name",name}},"sent");
      log("request "+short_id(rid)+" out to "+short_id(target)+", mode "+mode+", their protocol "+std::to_string(peer->second.protocol));
      return;
    }
    if(action=="accept" || action=="cancel") {
      auto rid=data.value("request",std::string()); auto it=requests.find(rid);
      if(it==requests.end() || it->second.value("state",std::string())!="pending") throw std::runtime_error(lang::tr("lobby.error.expired"));
      const bool incoming_request=it->second.value("to",std::string())==id;
      target=incoming_request?it->second.value("from",std::string()):it->second.value("to",std::string());
      if(action=="accept") {
        if(!incoming_request || busy()) throw std::runtime_error(lang::tr("lobby.error.cannot_accept"));
        it->second["state"]="accepted"; it->second["confirmed"]=false; request_expiry[rid]=now+120000;
        tracking[rid]={now,false};
        queue_event(target,"accept",{{"request",rid}});
        log("accepted request "+short_id(rid)+" from "+short_id(target));
      } else {
        // code: "user" (Decline), "auto" (auto reject). 0.8.1 reads only the request id.
        std::string code=data.value("code",std::string("user"));
        if(code!="auto") code="user";
        requests.erase(it); request_expiry.erase(rid); tracking.erase(rid);
        queue_event(target,"cancel",{{"request",rid},{"code",code}});
        log(std::string(incoming_request?"declined":"withdrew")+" request "+short_id(rid)+" ("+code+")");
      }
      return;
    }
    throw std::runtime_error(lang::tr("lobby.error.unknown_action"));
  }
  void tick() {
    const auto now=GetTickCount64();
    for(int i=0;i<128;++i) {
      char packet[4097]; sockaddr_in from{}; int address_size=sizeof from;
      int length=recvfrom(socket,packet,sizeof packet-1,0,reinterpret_cast<sockaddr*>(&from),&address_size);
      if(length<=0) break;
      if(drop_in) continue;
      packet[length]='\0';
      if(packet[0]=='d') {
        if(!test.no_dht) { time_t wait=0; dht_periodic(packet,length,reinterpret_cast<sockaddr*>(&from),address_size,&wait,dht_result,this); }
        continue;
      }
      if(length<4 || std::memcmp(packet,wire_prefix,4)!=0) continue;
      if(length>1400) { drop(from,"packet","over 1400 bytes"); continue; }
      try {
        auto body=Json::parse(std::string(packet+4,length-4),nullptr,false);
        if(!body.is_object()) { drop(from,"packet","not JSON"); continue; }
        const auto type=body.value("t",std::string());
        if(type=="hello") on_hello(body,from);
        else if(type=="data") on_data(body,from);
      } catch(...) { drop(from,"packet","unreadable"); } // Untrusted UDP input cannot take down the launcher.
    }
    if(!test.no_dht) {
      time_t wait=0; dht_periodic(nullptr,0,nullptr,0,&wait,dht_result,this);
      int good=0,dubious=0; dht_nodes(AF_INET,&good,&dubious,nullptr,nullptr);
      if((good+dubious==0 && now-last_seed>5000) || now-last_seed>60000) {
        for(const auto& seed:seeds) dht_ping_node(reinterpret_cast<const sockaddr*>(&seed),sizeof seed);
        last_seed=now;
      }
      if(good+dubious>0 && now-started>1000 && now-last_topics>500) {
        for(auto& topic:topics) {
          const ULONGLONG interval=topic.attempts<3?15000:(topic.public_topic?45000:120000);
          if(!topic.done || (topic.searched && now-topic.searched<interval)) continue;
          topic.done=false;
          // A friend code being looked up is only searched: listing ourselves under it would make us
          // answer for someone else's code.
          if(dht_search(topic.hash.data(),topic.announce?listen_port:0,AF_INET,dht_result,this)<0) topic.done=true;
          topic.searched=now; ++topic.attempts; last_topics=now;
          break;
        }
      }
    }
    int hello_budget=8;
    for(auto& candidate:candidates) if(hello_budget && now-candidate.second.hello_sent>3000) {
      send_hello(candidate.second.address); candidate.second.hello_sent=now; --hello_budget;
    }
    for(auto& peer:peers) {
      if(now-peer.second.hello_sent>5000 && valid_wire_profile(profile)) {
        send_hello(peer.second.address); peer.second.hello_sent=now;
      }
      if(now-peer.second.seen>20000) continue;
      if(visible || friends.count(peer.first)) {
        if(now-last_presence>1500)
          send_data(peer.first,{{"k","presence"},{"visible",visible},{"status",status.value("status",std::string("Online"))},
                                {"stocks",status.value("stocks",Json::array())}});
        bool invited=false;
        for(const auto& request:requests) if(request.second.value("state",std::string())=="pending" &&
          (request.second.value("from",std::string())==peer.first || request.second.value("to",std::string())==peer.first)) invited=true;
        if(invited && now-peer.second.ping_sent>500) {
          peer.second.ping_nonce=nonce_id(8); peer.second.ping_sent=now;
          send_data(peer.first,{{"k","ping"},{"nonce",peer.second.ping_nonce}});
        }
      }
    }
    if(now-last_presence>1500) last_presence=now;
    if(now-last_peers>5000 && visible) {
      Json list=Json::array();
      for(const auto& peer:peers) if(peer.second.visible && now-peer.second.seen<20000 && list.size()<16)
        list.push_back(endpoint_key(peer.second.address));
      for(const auto& peer:peers) if(peer.second.visible && now-peer.second.seen<20000)
        send_data(peer.first,{{"k","peers"},{"list",list}});
      last_peers=now;
    }
    for(auto& item:outbound) if(now<item.second.expires && now-item.second.sent>500 && peers.count(item.second.target)) {
      send_data(item.second.target,item.second.data); item.second.sent=now;
    }
    for(auto it=outbound.begin();it!=outbound.end();) if(now>=it->second.expires) it=outbound.erase(it); else ++it;
    // A request (or this player's Accept) nobody has acknowledged after ten seconds: say so, with the
    // code to use in Direct, instead of waiting out the full thirty.
    for(auto& item:requests) {
      auto track=tracking.find(item.first);
      if(track==tracking.end() || track->second.warned || now-track->second.sent<unreachable_after) continue;
      const auto& r=item.second;
      const bool outgoing=r.value("from",std::string())==id && r.value("state",std::string())=="pending" && !r.value("delivered",false);
      const bool accepting=r.value("to",std::string())==id && r.value("state",std::string())=="accepted" && !r.value("confirmed",true);
      if(!outgoing && !accepting) continue;
      track->second.warned=true;
      const auto other=outgoing?r.value("to",std::string()):r.value("from",std::string());
      auto peer=peers.find(other);
      const bool heard=peer!=peers.end() && peer->second.seen>track->second.sent;
      if(outgoing && heard && peer_protocol(other)<2) {
        // An older launcher that hears us refuses without a word; its version says who must update.
        say("lobby.no_answer_legacy",{{"name",peer_name(other)},{"version",build_version(profile.value("build",std::string()))}},"warn");
      } else {
        say(outgoing?"lobby.unreachable":"lobby.accept_unreachable",{{"name",peer_name(other)},{"code",peer_code(other)}},"unreachable");
      }
      log("request "+short_id(item.first)+(outgoing?" not delivered":": accept not delivered")+" to "+short_id(other)+
          " after "+std::to_string((now-track->second.sent)/1000)+" s, "+(heard?"their messages still arrive":"nothing heard from them")+
          ", their protocol "+std::to_string(peer_protocol(other)));
    }
    for(auto it=requests.begin();it!=requests.end();) if(now>=request_expiry[it->first]) {
      const auto state=it->second.value("state",std::string());
      if(state=="pending") {
        if(it->second.value("from",std::string())==id) {
          const auto to=it->second.value("to",std::string());
          if(it->second.value("delivered",false)) say("lobby.expired",{{"name",peer_name(to)}},"expired");
          else if(!tracking.count(it->first) || !tracking[it->first].warned)
            say("lobby.unreachable",{{"name",peer_name(to)},{"code",peer_code(to)}},"unreachable");
        } else say("lobby.missed",{{"name",peer_name(it->second.value("from",std::string()))}},"missed");
      }
      log("request "+short_id(it->first)+" expired ("+state+")");
      tracking.erase(it->first); request_expiry.erase(it->first); it=requests.erase(it);
    } else ++it;
    for(auto it=tracking.begin();it!=tracking.end();) if(!requests.count(it->first)) it=tracking.erase(it); else ++it;
    bool lookups_done=false;
    for(auto it=code_lookups.begin();it!=code_lookups.end();) if(now>=it->second) {
      say("lobby.friend.not_found",{{"code",it->first},{"version",build_version(profile.value("build",std::string()))}},"warn");
      log("friend lookup ended without an answer");
      it=code_lookups.erase(it); lookups_done=true;
    } else ++it;
    if(lookups_done) refresh_topics();
    for(auto it=seen_events.begin();it!=seen_events.end();) if(now-it->second>120000) it=seen_events.erase(it); else ++it;
    for(auto it=candidates.begin();it!=candidates.end();) if(now-it->second.seen>120000) it=candidates.erase(it); else ++it;
  }
  Json state() const {
    const auto now=GetTickCount64();
    int good=0,dubious=0;
    if(!test.no_dht) dht_nodes(AF_INET,&good,&dubious,nullptr,nullptr);
    const Json latest=notices.empty()?Json::object():notices.back();
    Json result={{"self",{{"id",id},{"visible",visible},{"protocol",lobby_protocol},{"profile",profile}}},{"players",Json::array()},
                 {"requests",Json::array()},{"friends",Json::array()},{"friend_requests",Json::array()},
                 {"messages",Json::array()},{"notices",Json::array()},{"lookups",Json::array()},
                 {"network",{{"dht_nodes",good+dubious},{"candidates",candidates.size()},{"peers",peers.size()}}},
                 {"notice",latest.value("text",std::string())},{"notice_seq",notice_seq}};
    for(const auto& item:notices) result["notices"].push_back(item);
    for(const auto& lookup:code_lookups) result["lookups"].push_back(lookup.first);
    for(const auto& peer:peers) if(peer.second.seen && now-peer.second.seen<20000 && peer.second.visible && visible) {
      Json player=peer.second.profile;
      player["id"]=peer.first; player["status"]=peer.second.status; player["stocks"]=peer.second.stocks;
      player["busy"]=peer.second.status!="Online"; player["protocol"]=peer.second.protocol;
      player["friend"]=friends.count(peer.first)!=0;
      result["players"].push_back(player);
    }
    for(const auto& friend_entry:friends) {
      auto peer=peers.find(friend_entry.first);
      bool online=peer!=peers.end() && peer->second.seen && now-peer->second.seen<20000;
      Json item=online?peer->second.profile:Json::object();
      item["id"]=friend_entry.first;
      item["name"]=online?peer->second.profile.value("name",std::string("?")):friend_entry.second.value("name",std::string("?"));
      item["code"]=online?peer->second.profile.value("code",std::string()):friend_entry.second.value("code",std::string());
      item["status"]=online?peer->second.status:std::string("Offline");
      item["stocks"]=online?peer->second.stocks:Json::array();
      item["online"]=online;
      if(online) { item["protocol"]=peer->second.protocol; item["visible"]=peer->second.visible; }
      result["friends"].push_back(item);
    }
    for(const auto& request:incoming) result["friend_requests"].push_back(request.second);
    for(const auto& request:requests) {
      auto item=request.second;
      auto expiry=request_expiry.find(request.first);
      item["remaining"]=expiry==request_expiry.end() || expiry->second<now?0:static_cast<int>((expiry->second-now)/1000);
      const auto other=item.value("from",std::string())==id?item.value("to",std::string()):item.value("from",std::string());
      auto peer=peers.find(other);
      Json who=peer==peers.end()?Json::object():peer->second.profile;
      who["name"]=peer_name(other); who["code"]=peer_code(other);
      item["peer"]=who;
      result["requests"].push_back(item);
    }
    if(visible) for(const auto& message:messages) if(now-message.value("time",now)<3600000) result["messages"].push_back(message);
    return result;
  }
  std::map<std::string,int> pings() const {
    std::map<std::string,int> result; auto now=GetTickCount64();
    for(const auto& peer:peers) if(peer.second.ping>=0 && peer.second.seen && now-peer.second.seen<20000)
      for(const auto& request:requests) if(request.second.value("state",std::string())!="cancelled" &&
        (request.second.value("from",std::string())==peer.first || request.second.value("to",std::string())==peer.first)) { result[peer.first]=peer.second.ping; break; }
    return result;
  }
};

PeerLobby::PeerLobby(const std::string& dir,const std::string& seed,int port,const PeerTestOptions& test)
    :p_(std::make_unique<Impl>(dir,seed,port,test)) {}
PeerLobby::~PeerLobby()=default;
void PeerLobby::join(const Json& profile) { p_->join(profile); }
void PeerLobby::update_profile(const Json& profile) { p_->update_profile(profile); }
void PeerLobby::command(const std::string& action,const Json& data) { p_->command(action,data); }
void PeerLobby::presence(const Json& status) { p_->set_presence(status); }
void PeerLobby::tick() { p_->tick(); }
Json PeerLobby::state() const { return p_->state(); }
std::map<std::string,int> PeerLobby::pings() const { return p_->pings(); }
bool PeerLobby::take_launch(Json& launch) {
  if(p_->launches.empty()) return false;
  launch=p_->launches.front(); p_->launches.pop_front(); return true;
}
void PeerLobby::add_address(const std::string& host_port) {
  sockaddr_in address{};
  if(!resolve(host_port,address)) { p_->log("add address: cannot resolve"); throw std::runtime_error(lang::tr("lobby.error.bootstrap")); }
  p_->add_candidate(address);
}
void PeerLobby::test_drop(bool incoming,bool outgoing) { p_->drop_in=incoming; p_->drop_out=outgoing; }
const std::string& PeerLobby::id() const { return p_->id; }
bool PeerLobby::visible() const { return p_->visible; }
int PeerLobby::port() const { return p_->listen_port; }
}
