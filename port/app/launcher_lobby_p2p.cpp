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
#include <cstdlib>
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
  if(code=="p2p_version") return "needs a launcher of the same kind and version for peer-to-peer matches";
  if(code=="p2p_fighter") return "was sent a character or color that does not exist";
  if(code=="p2p_engine") return "needs Source Port selected on both sides for a peer-to-peer match";
  if(code=="no_account") return "has no account for that kind of match";
  if(code=="transport") return "does not know that kind of match";
  return "cannot play right now";
}
// ---- peer-to-peer matches (every match of the build without the Slippi layer, a request's own
// choice in the other build) ----
// The legal stages as the game's stage select numbers them (external ids): Battlefield, Final
// Destination, Dream Land, Yoshi's Story, Fountain of Dreams, Pokemon Stadium.
constexpr int legal_stages[6]={0x1F,0x20,0x1C,0x08,0x02,0x03};
// An automatic request is answered by a launcher, not a person, so it does not wait thirty seconds.
constexpr ULONGLONG auto_request_wait=8000;
bool hex_text(const std::string& value,size_t size) {
  return value.size()==size && std::all_of(value.begin(),value.end(),[](char c){ return (c>='0'&&c<='9')||(c>='a'&&c<='f'); });
}
// A UDP port nothing holds right now. The game binds it a moment later; a port taken in between
// fails that game's session, which the player sees as a match that did not start.
int free_udp_port() {
  SOCKET s=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP); if(s==INVALID_SOCKET) return 0;
  sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_ANY);
  int port=0,size=sizeof a;
  if(bind(s,reinterpret_cast<sockaddr*>(&a),sizeof a)==0 && getsockname(s,reinterpret_cast<sockaddr*>(&a),&size)==0) port=ntohs(a.sin_port);
  closesocket(s); return port;
}
// This PC's own IPv4 addresses (never loopback): where a player on the same network reaches it.
std::vector<std::string> lan_addresses() {
  std::vector<std::string> out; char host[256]{};
  if(gethostname(host,sizeof host)!=0) return out;
  addrinfo hint{},*result=nullptr; hint.ai_family=AF_INET; hint.ai_socktype=SOCK_DGRAM;
  if(getaddrinfo(host,nullptr,&hint,&result)!=0) return out;
  for(auto* a=result;a && out.size()<3;a=a->ai_next) {
    const auto* in=reinterpret_cast<const sockaddr_in*>(a->ai_addr);
    if((ntohl(in->sin_addr.s_addr)>>24)==127) continue;
    char ip[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET,&in->sin_addr,ip,sizeof ip);
    if(std::find(out.begin(),out.end(),std::string(ip))==out.end()) out.push_back(ip);
  }
  freeaddrinfo(result); return out;
}
// The character and color of a setup: a character the select screen has, and a color that
// character has. A setup that names no color means the first one.
bool legal_fighter(const Json& s) {
  if(!s.is_object() || !s.count("ch") || !s["ch"].is_number_integer()) return false;
  const int ch=s["ch"].get<int>();
  if(ch<0 || ch>25) return false;
  if(!s.count("col")) return true;
  return s["col"].is_number_integer() && s["col"].get<int>()>=0 && s["col"].get<int>()<p2p_color_count(ch);
}
// One player's half of a match setup as it arrives: the game's UDP port, where to reach it besides
// the address the lobby sees, the character, its color and the name.
bool valid_setup(const Json& s) {
  if(!s.is_object() || !s.count("port") || !s["port"].is_number_integer() || s["port"].get<int>()<1 || s["port"].get<int>()>65535) return false;
  if(!legal_fighter(s)) return false;
  if(!s.count("name") || !s["name"].is_string() || s["name"].get<std::string>().size()>96) return false;
  if(s.count("lobby") && !s["lobby"].is_boolean()) return false;
  if(!s.count("addrs") || !s["addrs"].is_array() || s["addrs"].size()>4) return false;
  for(const auto& a:s["addrs"]) { sockaddr_in e{}; if(!a.is_string() || !numeric_endpoint(a.get<std::string>(),e)) return false; }
  return true;
}
}

std::string derived_code(const std::string& name,const std::string& identity_key) {
  std::string code;
  for(unsigned char c:name) {
    if(code.size()>=4) break;
    if(c<128 && std::isalnum(c)) code+=static_cast<char>(std::toupper(c));
  }
  if(code.empty()) code="MU";   // a name with no Latin letter or digit still gets a valid code
  uint8_t digest[4]{};
  crypto_blake2b(digest,sizeof digest,reinterpret_cast<const uint8_t*>(identity_key.data()),identity_key.size());
  char digits[8]{}; std::snprintf(digits,sizeof digits,"#%03u",((unsigned(digest[0])<<16)|(unsigned(digest[1])<<8)|digest[2])%1000u);
  return code+digits;
}
std::string identity_key(const std::string& directory) {
  try {
    const auto path=std::filesystem::u8path(directory+"/lobby-peer-identity.json");
    Bytes32 seed{},pub{}; Bytes64 secret{};
    std::error_code ec;
    if(std::filesystem::exists(path,ec)) {
      std::ifstream in(path,std::ios::binary); const auto file=Json::parse(in,nullptr,false);
      if(!file.is_object() || !file.count("ed_seed") || !file["ed_seed"].is_string() ||
         !unhex(file["ed_seed"].get<std::string>(),seed.data(),seed.size())) return {};
    } else {
      // The same file the lobby makes on its first start (PeerLobby::Impl::save).
      Bytes32 x{}; random_bytes(seed.data(),seed.size()); random_bytes(x.data(),x.size());
      std::filesystem::create_directories(std::filesystem::u8path(directory),ec);
      std::ofstream out(path,std::ios::binary|std::ios::trunc); if(!out) return {};
      out<<Json{{"ed_seed",hex(seed.data(),seed.size())},{"x_secret",hex(x.data(),x.size())},{"profile",Json::object()},
                {"friends",Json::array()},{"incoming",Json::array()},{"outgoing",Json::array()}}.dump(2);
      crypto_wipe(x.data(),x.size());
    }
    crypto_ed25519_key_pair(secret.data(),pub.data(),seed.data());   // wipes the seed
    crypto_wipe(secret.data(),secret.size());
    return hex(pub.data(),pub.size());
  } catch(...) { return {}; }
}
std::string p2p_name(const std::string& name) {
  const std::string clean=clean_chat_text(name);
  std::string out;
  for(size_t i=0;i<clean.size();) {
    const unsigned char c=static_cast<unsigned char>(clean[i]);
    const size_t length=c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:4;
    if(out.size()+length>31) break;
    // The colon separates the two names; a quote or a backslash would change how the game's command
    // line is split.
    if(!(length==1 && (c==':' || c=='"' || c=='\\'))) out.append(clean,i,length);
    i+=length;
  }
  while(!out.empty() && out.back()==' ') out.pop_back();
  while(!out.empty() && out.front()==' ') out.erase(out.begin());
  return out.empty()?std::string("Player"):out;
}
int p2p_color_count(int character) {
  // In the order of the character select screen's ids (the lobby's `characters` list).
  static constexpr int counts[26]={6,5,4,4,6,4,5,4,5,5,4,4,5,4,4,5,5,6,5,5,4,5,5,5,4,5};
  return character>=0 && character<26?counts[character]:0;
}
std::string p2p_arguments(const Json& p,const std::string& identity_file,const std::string& result_file) {
  try {
    if(!p.is_object()) return {};
    const int slot=p.value("slot",-1),port=p.value("port",0),stage=p.value("stage",-1),delay=p.value("delay",0);
    const std::string seed=p.value("seed",std::string()),expect=p.value("expect",std::string());
    if(slot<0 || slot>1 || port<1 || port>65535 || delay<1 || delay>15) return {};
    if(std::find(std::begin(legal_stages),std::end(legal_stages),stage)==std::end(legal_stages)) return {};
    if(!hex_text(seed,8) || !hex_text(expect,64)) return {};
    if(identity_file.empty() || result_file.empty() || identity_file.find('"')!=std::string::npos || result_file.find('"')!=std::string::npos) return {};
    const Json chars=p.value("chars",Json::array()),names=p.value("names",Json::array()),peers=p.value("peers",Json::array());
    if(!chars.is_array() || chars.size()!=2 || !names.is_array() || names.size()!=2 || !names[0].is_string() || !names[1].is_string() ||
       !peers.is_array() || peers.empty() || peers.size()>6) return {};
    std::string out=" --p2p-port "+std::to_string(port);
    for(const auto& peer:peers) {
      // Written back from the parsed address, so no text of the other player's reaches the command line.
      sockaddr_in address{};
      if(!peer.is_string() || !numeric_endpoint(peer.get<std::string>(),address)) return {};
      out+=" --p2p-peer "+endpoint_key(address);
    }
    std::string fighters;
    for(const auto& c:chars) {
      if(!c.is_array() || c.size()!=2 || !c[0].is_number_integer() || !c[1].is_number_integer() ||
         c[0].get<int>()<0 || c[0].get<int>()>25 || c[1].get<int>()<0 || c[1].get<int>()>=p2p_color_count(c[0].get<int>())) return {};
      fighters+=(fighters.empty()?"":":")+std::to_string(c[0].get<int>())+"/"+std::to_string(c[1].get<int>());
    }
    return out+" --p2p-slot "+std::to_string(slot)+" --p2p-chars "+fighters+" --p2p-stage "+std::to_string(stage)+
           " --p2p-seed "+seed+" --p2p-delay "+std::to_string(delay)+" --p2p-identity \""+identity_file+"\" --p2p-expect "+expect+
           " --p2p-result \""+result_file+"\" --p2p-names \""+p2p_name(names[0].get<std::string>())+":"+p2p_name(names[1].get<std::string>())+"\"";
  } catch(const std::exception&) { return {}; }
}

// "0.8.1:source" -> "Source Port 0.8.1", as the Play page names the builds.
static std::string build_name(const std::string& build) {
  auto colon=build.find(':');
  std::string version=build.substr(0,colon), kind=colon==std::string::npos?std::string():build.substr(colon+1);
  std::string name=kind=="source"?"Source Port":kind=="recomp"?"Static Recomp":kind.empty()?lang::tr("lobby.build_unknown"):kind;
  return version.empty()?name:name+" "+version;
}
std::string build_version(const std::string& build) { return build.substr(0,build.find(':')); }
// "0.8.5:source" -> "source": the Game Build a profile is on.
static std::string build_engine(const Json& profile) {
  const std::string build=profile.is_object()?profile.value("build",std::string()):std::string();
  const auto colon=build.find(':');
  return colon==std::string::npos?std::string():build.substr(colon+1);
}
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
std::string clean_chat_text(const std::string& value) {
  std::string out; out.reserve(value.size());
  for(size_t i=0;i<value.size();) {
    const unsigned char c=static_cast<unsigned char>(value[i]);
    const size_t length=c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:(c>>3)==30?4:0;
    if(!length || i+length>value.size()) return {};
    uint32_t cp=length==1?c:length==2?(c&0x1Fu):length==3?(c&0x0Fu):(c&0x07u);
    for(size_t k=1;k<length;++k) {
      const unsigned char n=static_cast<unsigned char>(value[i+k]);
      if((n&0xC0)!=0x80) return {};
      cp=(cp<<6)|(n&0x3Fu);
    }
    if((length==2 && cp<0x80) || (length==3 && cp<0x800) || (length==4 && cp<0x10000)) return {};
    if((cp>=0xD800 && cp<=0xDFFF) || cp>0x10FFFF) return {};
    const bool control=cp<32 || (cp>=0x7F && cp<0xA0);
    const bool direction=(cp>=0x202A && cp<=0x202E) || (cp>=0x2066 && cp<=0x2069) || cp==0x200E || cp==0x200F;
    if(!control && !direction) out.append(value,i,length);
    i+=length;
  }
  return out;
}
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
  if(out.count("searching") && !out["searching"].is_boolean()) out.erase("searching");
  return out;
}
std::string mode_problem(const Json& receiver,const Json& sender,const std::string& mode) {
  const auto mine=receiver.value("build",std::string()), theirs=sender.value("build",std::string());
  if(mode=="vanilla") {
    if(!open_to(receiver,"vanilla")) return "mode_closed";
    // Source Port and Static Recomp play each other online (one netcode, checked against each other
    // before every release), so the Game Build may differ; only the version has to match.
    if(build_version(mine)!=build_version(theirs)) return "version";
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
    ULONGLONG last_private_request=0,last_private_reply=0;
    ULONGLONG probe_since=0;        // automatic pairing: when this player's ping was first asked for
    bool visible=false;
    int protocol=1;                 // from the hello; 0.8.1 sends none
    // The matches this player's launcher starts (on_hello): peer to peer, and the hosted kind.
    bool p2p=false,hosted=true;
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
  // Private chat. A room is "outgoing" (this player asked, no answer yet), "incoming" (the other
  // player asked), "open", or "closed" (the other player closed it; kept so this player can still
  // read it until they close it too). There is at most one room per other player, and a message is
  // only ever stored in the room whose peer sent it.
  struct PrivateRoom {
    std::string peer,name,state;
    ULONGLONG created=0,expires=0,last_in=0,last_out=0;
    bool delivered=false;
    std::vector<Json> messages;
  };
  std::map<std::string,PrivateRoom> rooms;       // by room id
  std::set<std::string> private_blocked;         // players whose requests are ignored, this session only
  ULONGLONG last_private_request=0;
  // The launcher where every match is peer to peer (p2p_on). searching: this player wants an automatic pairing, and says so in
  // the profile every hello carries. blocked: players never paired with and never answered, kept
  // with the friends. invites: pasted "Connect by address" lines, by identity key, until when.
  bool searching=false;
  // The character and color for this player's matches, from the launcher's settings with every
  // profile update: -1 is "the first main". Never sent with the profile, only in a match setup.
  int p2p_ch=-1,p2p_col=0;
  // Whether this player can start the hosted kind of match right now (an account for it), from the
  // launcher's settings with every profile update like the two above. A profile that does not say is
  // one that can. Never sent with the profile: a hosted request is refused with "no_account".
  bool hosted_ok=true;
  std::set<std::string> blocked;
  std::map<std::string,ULONGLONG> auto_skip,invites;   // auto_skip: not asked again before this time
  ULONGLONG last_auto=0,lan_at=0;                       // last_auto: when automatic pairing looks next
  std::string invite_found; unsigned invite_seq=0;
  std::vector<std::string> lan;                  // this PC's own addresses, refreshed now and then
  std::string observed;                          // this lobby's address as another player's launcher saw it
  // Tests: MELEE_P2P_SEPARATE_PORT=1 gives the game a free port of its own, and the lobby stays up.
  const bool separate_port=[]{ const char* v=std::getenv("MELEE_P2P_SEPARATE_PORT"); return v && std::string(v)=="1"; }();
  // A launcher announcing an older protocol (tests) plays the older flow.
  bool p2p_capable() const { return !test.protocol || test.protocol>=p2p_protocol; }
  // Every match of this launcher is peer to peer: the build without the other transport (or a test
  // standing in for it). It alone has searching, automatic requests, blocking and invites. Whether
  // one match is peer to peer is asked of its request (p2p_request), never of this.
  bool p2p_on() const { return (p2p_matches || test.p2p_only) && p2p_capable(); }
  static bool p2p_request(const Json& request) { return request.value("transport",std::string())==kP2pTransport; }
  // A peer-to-peer match with this player: their launcher knows the setup and is of this launcher's
  // kind (the two kinds of game refuse each other in their handshake).
  bool p2p_peer(const Peer& peer) const { return p2p_capable() && peer.p2p && peer.hosted==!p2p_on(); }
  void set_searching(bool on) {
    if(searching==on) return;
    searching=on; auto_skip.clear(); last_auto=0;
    for(auto& peer:peers) { peer.second.hello_sent=0; peer.second.probe_since=0; }   // everyone hears on the next tick
    log(on?"searching for a match":"stopped searching");
  }
  // This player's half of a match setup, made once per request.
  Json p2p_mine(const std::string& rid) {
    auto& r=requests[rid];
    if(r.count("p2p_mine")) return r["p2p_mine"];
    // The game takes over this lobby's own port: the path between the two lobby sockets is the one
    // path known to work, and a router that has never seen a new port would turn the other game away.
    const int port=separate_port?free_udp_port():listen_port;
    Json addrs=Json::array();
    for(const auto& ip:lan_addresses()) addrs.push_back(ip+":"+std::to_string(port));
    const auto mains=profile.value("mains",Json::array());
    // The character chosen on the Profile page, else the first main; the color chosen there when
    // that character has it, else its first.
    const int ch=p2p_ch>=0?p2p_ch:!mains.empty() && mains[0].is_number_integer()?mains[0].get<int>():2;
    const int col=p2p_col>=0 && p2p_col<p2p_color_count(ch)?p2p_col:0;
    r["p2p_mine"]={{"port",port},{"addrs",addrs},{"ch",ch},{"col",col},
                   {"name",p2p_name(profile.value("name",std::string()))}};
    if(!separate_port) r["p2p_mine"]["lobby"]=true;   // dial the address this lobby is heard from, port included
    return r["p2p_mine"];
  }
  // A refusal this player is not told about: an automatic request, or one from a blocked player.
  void refuse_quietly(const std::string& sender,const std::string& rid,const std::string& code,const std::string& mode) {
    queue_event(sender,"decline",{{"request",rid},{"code",code},{"mode",mode},{"build",profile.value("build",std::string())},
                                  {"reason",wire_reason(code)}});
    log("quietly refuse request "+short_id(rid)+" from "+short_id(sender)+": "+code);
  }
  // Invited player answers: Accept, or an automatic request between two searching players.
  void accept_request(const std::string& rid) {
    auto& r=requests.at(rid);
    const auto target=r.value("from",std::string()); const auto now=GetTickCount64();
    // An automatic match nobody confirms must not hold this player for two minutes.
    r["state"]="accepted"; r["confirmed"]=false; request_expiry[rid]=now+(r.value("auto",false)?15000:120000);
    tracking[rid]={now,false};
    Json body={{"request",rid}};
    if(p2p_request(r)) body["p2p"]=p2p_mine(rid);
    queue_event(target,"accept",body);
    log("accepted request "+short_id(rid)+" from "+short_id(target));
  }
  std::string invite_text() const {
    std::string addrs;
    auto add=[&](const std::string& a){ if(addrs.find(a)==std::string::npos) addrs+=(addrs.empty()?"":",")+a; };
    if(!observed.empty()) add(observed);
    for(const auto& ip:lan) add(ip+":"+std::to_string(listen_port));
    return "MUINVITE1|"+id+"|"+addrs+"|"+profile.value("name",std::string());
  }
  // Who an automatic request may go to: searching, ready, free, the same version, reachable under the
  // same rule as a request by hand, not blocked and not just tried.
  bool auto_candidate(const std::string& other,const Peer& peer,ULONGLONG now) const {
    if(!peer.seen || now-peer.seen>=20000 || !p2p_peer(peer) || peer.status!="Online") return false;
    if(!peer.profile.count("searching") || !peer.profile["searching"].is_boolean() || !peer.profile["searching"].get<bool>()) return false;
    if(!peer.profile.value("ready",false) || blocked.count(other) || auto_skip.count(other)) return false;
    if(!(visible && peer.visible) && !friends.count(other)) return false;
    return mode_problem(peer.profile,profile,"vanilla").empty() && mode_problem(profile,peer.profile,"vanilla").empty();
  }

  ULONGLONG private_wait() const { return test.private_request_ms?test.private_request_ms:30000; }
  bool knows_private() const { return !test.no_private && (!test.protocol || test.protocol>=private_protocol); }
  static bool room_id(const std::string& value) {
    return value.size()==32 && std::all_of(value.begin(),value.end(),[](char c){ return (c>='0'&&c<='9')||(c>='a'&&c<='f'); });
  }
  std::string room_with(const std::string& peer_id) const {
    for(const auto& room:rooms) if(room.second.peer==peer_id) return room.first;
    return {};
  }
  size_t private_live() const {
    size_t count=0;
    for(const auto& room:rooms) if(room.second.state!="closed") ++count;
    return count;
  }
  // Strangers can ask inside the public lobby; friends anywhere.
  bool private_reach(const std::string& other,const Peer& peer) const {
    return (visible && peer.visible) || friends.count(other)!=0;
  }
  void private_append(PrivateRoom& room,const Json& message) {
    room.messages.push_back(message);
    if(room.messages.size()>private_max_messages)
      room.messages.erase(room.messages.begin(),room.messages.begin()+(room.messages.size()-private_max_messages));
  }
  // The same room under a new id (what it holds stays).
  PrivateRoom& private_move(const std::string& from,const std::string& to) {
    PrivateRoom room=std::move(rooms[from]); rooms.erase(from);
    return rooms[to]=std::move(room);
  }
  // Closed rooms the player left lying around must not grow without end.
  void private_trim() {
    while(rooms.size()>private_max_rooms*2) {
      auto oldest=rooms.end();
      for(auto it=rooms.begin();it!=rooms.end();++it)
        if(it->second.state=="closed" && (oldest==rooms.end() || it->second.created<oldest->second.created)) oldest=it;
      if(oldest==rooms.end()) break;
      rooms.erase(oldest);
    }
  }
  // A request this player no longer stands behind (it ran out, or was withdrawn) stops being resent,
  // so a late copy cannot open a room on the other side.
  void private_forget(const std::string& rid) {
    for(auto it=outbound.begin();it!=outbound.end();) {
      const auto& packet=it->second.data;
      if(packet.value("action",std::string())=="pm_request" && packet["data"].value("room",std::string())==rid) it=outbound.erase(it);
      else ++it;
    }
  }
  static size_t event_size(const std::string& action,const Json& data) {
    return Json{{"k","event"},{"id",std::string(32,'0')},{"action",action},{"data",data}}.dump().size();
  }
  void private_open(const std::string& rid) {
    auto& room=rooms.at(rid);
    room.state="open"; room.expires=0; room.name=peer_name(room.peer);
    say("lobby.pm.opened",{{"name",room.name},{"room",rid}},"pm_open");
    log("private chat "+short_id(rid)+" open with "+short_id(room.peer));
  }
  // An answer to a private chat message this player cannot use, at most one every three seconds per
  // player, so a flood of them cannot fill the outgoing queue.
  void private_reply(const std::string& sender,Peer& peer,const std::string& action,const Json& data) {
    const auto now=GetTickCount64();
    if(peer.last_private_reply && now-peer.last_private_reply<3000) return;
    peer.last_private_reply=now;
    queue_event(sender,action,data);
  }

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
      // Two entry points were not enough: on a day when the first did not answer and the second
      // gave a handful of nodes, players could not see each other at all. More independent ones.
      if(resolve("dht.libtorrent.org:25401",address)) seeds.push_back(address);
      if(resolve("router.utorrent.com:6881",address)) seeds.push_back(address);
      if(resolve("dht.aelitis.com:6881",address)) seeds.push_back(address);
    }
    const bool dht_only=bootstrap.rfind("dht://",0)==0;
    const auto custom=dht_only?bootstrap.substr(6):bootstrap;
    if(!custom.empty()) {
      if(!resolve(custom,address)) throw std::runtime_error(lang::tr("lobby.error.bootstrap"));
      seeds.insert(seeds.begin(),address);
      if(!dht_only) add_candidate(address); // A manually supplied peer can connect before DHT convergence.
    }
    // With peer-to-peer matches a lobby without the DHT still works: players on the same network
    // (or behind a forwarded port) find each other with an invite.
    if(seeds.empty() && !test.no_dht && !p2p_on()) throw std::runtime_error(lang::tr("lobby.error.no_dht"));
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
    if(!blocked.empty()) file["blocked"]=Json(blocked);
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
      for(const auto& f:file.value("blocked",Json::array())) if(f.is_string() && f.get<std::string>().size()==64 && blocked.size()<1000) blocked.insert(f.get<std::string>());
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
    // Searching is said in the profile, but is this session's state: it is never saved with it.
    Json shown=profile;
    if(searching) shown["searching"]=true;
    Json body={{"v",1},{"lv",test.protocol?test.protocol:lobby_protocol},{"t","hello"},{"pk",id},{"xk",hex(x_public.data(),x_public.size())},
               {"profile",shown},{"visible",visible},{"time",wall_clock()},{"nonce",nonce}};
    // The matches this launcher starts, when it starts more than one kind. A launcher where every
    // match is peer to peer sends no list, and neither does an older one: on_hello reads the
    // protocol number for those. Older launchers ignore the field like any they do not know.
    if(p2p_capable() && !p2p_on()) body["tp"]=Json::array({kHostedTransport,kP2pTransport});
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
    Json launch={{"request",rid},{"opponent",other},{"name",peer->second.profile.value("name",std::string())},
                 {"code",peer->second.profile.value("code",std::string())},{"build",profile.value("build",std::string())},
                 {"mode",r.value("mode",std::string("vanilla"))},
                 {"character",!mains.empty() && mains[0].is_number_integer()?mains[0].get<int>():2}};
    if(p2p_request(r)) {
      // Everything both games must pass identically comes from the request id and the two halves of
      // the setup, so the two launchers build the same match without another message. The requester
      // is slot 0. Two players on the same character and color: the second takes the next color.
      if(!r.count("p2p_mine") || !r.count("p2p_theirs")) { log("launch "+short_id(rid)+": the match setup is incomplete"); return; }
      const bool requester=r.value("from",std::string())==id;
      const Json& mine=r["p2p_mine"]; const Json& theirs=r["p2p_theirs"];
      const Json& first=requester?mine:theirs; const Json& second=requester?theirs:mine;
      uint8_t digest[4]{};
      crypto_blake2b(digest,sizeof digest,reinterpret_cast<const uint8_t*>(rid.data()),rid.size());
      const uint32_t seed=(uint32_t(digest[0])<<24)|(uint32_t(digest[1])<<16)|(uint32_t(digest[2])<<8)|digest[3];
      // Where the other game listens. Their game on their lobby's port: exactly the address this
      // lobby hears them from, port included (what their router shows for that socket, which is not
      // the port they bound). Their game on a port of its own: that address with the game's port.
      // Then the addresses they named themselves (the same network).
      char ip[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET,&peer->second.address.sin_addr,ip,sizeof ip);
      Json reach=Json::array({theirs.value("lobby",false)?endpoint_key(peer->second.address)
                                                         :std::string(ip)+":"+std::to_string(theirs["port"].get<int>())});
      for(const auto& a:theirs["addrs"]) if(reach.size()<6 && std::find(reach.begin(),reach.end(),a)==reach.end()) reach.push_back(a);
      const int c0=first["ch"].get<int>(),c1=second["ch"].get<int>();
      const int k0=first.value("col",0); int k1=second.value("col",0);
      if(c1==c0 && k1==k0) k1=(k0+1)%p2p_color_count(c0);   // both checked against that character's colors (valid_setup)
      launch["p2p"]={{"slot",requester?0:1},{"port",mine["port"]},{"peers",reach},
                     {"chars",Json::array({Json::array({c0,k0}),Json::array({c1,k1})})},
                     {"stage",legal_stages[seed%6]},{"seed",hex(digest,sizeof digest)},{"delay",p2p_input_delay},{"expect",other},
                     {"names",Json::array({p2p_name(first["name"].get<std::string>()),p2p_name(second["name"].get<std::string>())})},
                     {"auto",r.value("auto",false)}};
      set_searching(false);   // a match started: searching ends on both sides
    }
    launched.insert(rid);
    launches.push_back(launch);
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
    // Tests: a launcher from before private chat does not know these actions (see the end of this
    // function: an unknown action is never acknowledged).
    if(action.rfind("pm",0)==0 && !knows_private()) return false;
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
      if(blocked.count(sender)) return true;   // acknowledged and dropped
      auto value=data["text"].get<std::string>(); if(value.empty() || value.size()>300) return false;
      if(now-peer->second.last_chat<1000) return false;
      peer->second.last_chat=now;
      chat_append({{"id",event["id"]},{"sender",sender},{"name",peer->second.profile.value("name",std::string("?"))},
                   {"text",value},{"time",now},{"wall_time",std::time(nullptr)}});
    } else if(action=="request") {
      auto rid=data.value("request",std::string());
      if(rid.size()!=32) return false;
      // A blocked player is answered, so their launcher stops resending, but with nothing to read
      // into it, and this player never hears of it.
      if(blocked.count(sender)) { refuse_quietly(sender,rid,"unavailable","vanilla"); return true; }
      const bool automatic=p2p_on() && data.count("auto") && data["auto"].is_boolean() && data["auto"].get<bool>();
      if(now-peer->second.last_request<3000) {
        log_limited("hold "+sender,"hold request from "+short_id(sender)+": another one within 3 s");
        return false;
      }
      std::string mode=data.value("mode",std::string("vanilla"));
      if(mode.size()>40 || !plain_text(mode,40,40)) mode="unknown";
      // The request carries the sender's current Game Build, which beats the profile last heard.
      Json sender_profile=peer->second.profile;
      if(data.count("build") && data["build"].is_string() && data["build"].get<std::string>().size()<=80) sender_profile["build"]=data["build"];
      // The request's own transport. A launcher where every match is peer to peer does not read the
      // field (and sends none); in the other one a request without it is the hosted kind, as from
      // an older launcher, which is also what a launcher on an older protocol makes of any request.
      std::string transport=p2p_on()?kP2pTransport:kHostedTransport;
      bool known_transport=true;
      if(!p2p_on() && p2p_capable() && data.count("transport")) {
        const std::string named=data["transport"].is_string()?data["transport"].get<std::string>():std::string();
        if(named==kP2pTransport) transport=named; else if(named!=kHostedTransport) known_transport=false;
      }
      const bool p2p=transport==kP2pTransport;
      std::string refusal=refusal_for(sender,peer->second,data,sender_profile,mode);
      if(refusal.empty() && !known_transport) refusal="transport";
      // A launcher where every match is peer to peer names no transport, so its request reads as the
      // hosted kind here: it is neither, and the two kinds of game do not play each other.
      if(refusal.empty() && !p2p_on() && p2p_capable() && !peer->second.hosted) refusal="p2p_version";
      // A peer-to-peer match needs the setup both launchers exchange on accept, which an older
      // launcher (or one of the other kind) does not send. Only vanilla is played this way.
      if(refusal.empty() && p2p && (!p2p_peer(peer->second) || mode!="vanilla")) refusal="p2p_version";
      // Where both engines exist, only the Source Port plays peer to peer, on both sides.
      if(refusal.empty() && p2p && !p2p_on() && (build_engine(profile)!="source" || build_engine(sender_profile)!="source")) refusal="p2p_engine";
      // The hosted kind needs this player's account for it.
      if(refusal.empty() && !p2p && !hosted_ok) refusal="no_account";
      // An automatic request is for a player who is searching too, and is never shown as a prompt.
      if(refusal.empty() && automatic && !searching) refusal="not_searching";
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
      if(!refusal.empty()) {
        if(automatic) refuse_quietly(sender,rid,refusal,mode); else refuse(sender,rid,refusal,mode,sender_profile);
        return true;
      }
      peer->second.last_request=now;
      if(!requests.count(rid)) {
        requests[rid]={{"id",rid},{"from",sender},{"to",id},{"state","pending"},{"transport",transport},{"mode",mode}};
        request_expiry[rid]=now+(automatic?auto_request_wait:30000);
        peer->second.ping_nonce=nonce_id(8); peer->second.ping_sent=now;
        send_data(sender,{{"k","ping"},{"nonce",peer->second.ping_nonce}});
        log("request "+short_id(rid)+" in from "+short_id(sender)+", mode "+mode+(p2p?", peer to peer":"")+(automatic?", automatic":""));
        // Two searching players: the answer is yes, at once.
        if(automatic) { requests[rid]["auto"]=true; accept_request(rid); }
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
      if(p2p_request(it->second)) {
        // The accept carries the other player's half of the match setup; this player's half goes
        // back with the acknowledgment (on_data). Without theirs there is no match to start.
        if(data.count("p2p") && data["p2p"].is_object() && !legal_fighter(data["p2p"])) {
          // A character or color the game does not have: refused with a reason both players read,
          // never passed on to the game.
          requests.erase(it); request_expiry.erase(rid); tracking.erase(rid);
          queue_event(sender,"decline",{{"request",rid},{"code","p2p_fighter"},{"reason",wire_reason("p2p_fighter")}});
          say("lobby.refused.p2p_fighter",{{"name",name}},"warn");
          log("accept of "+short_id(rid)+" refused: character or color out of range");
          return true;
        }
        if(!data.count("p2p") || !valid_setup(data["p2p"])) { log("accept of "+short_id(rid)+" without a usable match setup"); return false; }
        it->second["p2p_theirs"]=data["p2p"]; p2p_mine(rid);
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
        // An automatic request that fell through goes back to searching without a word.
        if(pending && it->second.value("auto",false)) auto_skip[sender]=now+4000;
        else if(mine && pending) say(code=="auto"?"lobby.declined.auto":"lobby.declined.user",{{"name",name}},"declined");
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
        const bool automatic=it->second.value("auto",false);
        requests.erase(it); request_expiry.erase(rid); tracking.erase(rid);
        // An automatic request refused: try someone else first, this player again a little later
        // (they were busy), or much later (they cannot play this launcher at all).
        if(automatic) auto_skip[sender]=now+(code=="busy" || code=="answering" || code=="not_searching"?4000:30000);
        else declined(sender,code,mode,data);
        log("request "+short_id(rid)+" declined by "+short_id(sender)+": "+(code.empty()?std::string("no reason"):code));
      }
    } else if(action=="pm_request") {
      const auto rid=data.value("room",std::string());
      if(!room_id(rid)) return false;
      auto known=rooms.find(rid);
      if(known!=rooms.end()) return known->second.peer==sender;   // a resend
      // A blocked player gets no answer at all: their request runs out as if nobody was there.
      if(private_blocked.count(sender) || blocked.count(sender)) return false;
      if(peer->second.last_private_request && now-peer->second.last_private_request<2000) {
        log_limited("pm hold "+sender,"hold private chat request from "+short_id(sender)+": another one within 2 s");
        return false;
      }
      peer->second.last_private_request=now;
      if(!private_reach(sender,peer->second)) {
        queue_event(sender,"pm_decline",{{"room",rid},{"code","hidden"}});
        return true;
      }
      const auto held=room_with(sender);
      if(!held.empty()) {
        const auto state=rooms[held].state;
        if(state=="outgoing") {
          // Both players asked at once. The one with the lower id drops its own request and opens
          // this one; the other keeps its own, which the first is now accepting.
          if(id<sender) {
            for(auto it=outbound.begin();it!=outbound.end();)
              if(it->second.target==sender && it->second.data.value("action",std::string())=="pm_request") it=outbound.erase(it); else ++it;
            private_move(held,rid);
            queue_event(sender,"pm_accept",{{"room",rid}});
            private_open(rid);
          }
          return true;
        }
        if(state=="open") {
          // They lost this room (their close never arrived) or a request crossed an accept: carry on
          // in the room they ask for.
          private_move(held,rid);
          queue_event(sender,"pm_accept",{{"room",rid}});
          return true;
        }
        if(state=="incoming") rooms.erase(held);   // they asked again: the newer request replaces it
      }
      if(private_live()>=private_max_rooms) {
        queue_event(sender,"pm_decline",{{"room",rid},{"code","full"}});
        return true;
      }
      // A room they closed earlier continues, with what was said in it, if this request is accepted.
      auto& room=held.empty() || !rooms.count(held)?rooms[rid]:private_move(held,rid);
      room.peer=sender; room.name=name; room.state="incoming"; room.created=now; room.expires=now+private_wait();
      private_trim();
      say("lobby.pm.incoming",{{"name",name},{"room",rid}},"pm_request");
      log("private chat request "+short_id(rid)+" in from "+short_id(sender));
    } else if(action=="pm_accept") {
      const auto rid=data.value("room",std::string());
      if(!room_id(rid)) return false;
      auto it=rooms.find(rid);
      if(it==rooms.end()) {
        const auto held=room_with(sender);
        // They answer under another id than the room held here (see "open" above): follow them.
        if(!held.empty() && rooms[held].state=="open") private_move(held,rid);
        // Answered after the request ran out here: close it on their side too.
        else private_reply(sender,peer->second,"pm_close",{{"room",rid}});
        return true;
      }
      if(it->second.peer!=sender) return false;
      if(it->second.state=="outgoing") private_open(rid);
    } else if(action=="pm_decline") {
      const auto rid=data.value("room",std::string());
      auto it=rooms.find(rid);
      if(it!=rooms.end() && it->second.peer==sender && it->second.state=="outgoing") {
        const auto code=data.value("code",std::string());
        rooms.erase(it);
        say(code=="hidden"?"lobby.pm.declined.hidden":code=="full"?"lobby.pm.declined.full":"lobby.pm.declined",
            {{"name",name}},"pm_declined");
        log("private chat request "+short_id(rid)+" declined by "+short_id(sender));
      }
    } else if(action=="pm") {
      const auto rid=data.value("room",std::string());
      if(!room_id(rid) || !data.count("text") || !data["text"].is_string()) return false;
      // Only the room held with the player this message came from can take it, whatever id it names.
      auto it=rooms.find(rid);
      if(it==rooms.end() || it->second.peer!=sender) {
        const auto held=room_with(sender);
        it=held.empty()?rooms.end():rooms.find(held);
      }
      if(it==rooms.end() || it->second.state=="closed") {
        // This player closed the room and they did not hear it: tell them again.
        if(!private_blocked.count(sender)) private_reply(sender,peer->second,"pm_close",{{"room",rid}});
        return true;
      }
      auto& room=it->second;
      if(room.state!="open") return true;   // nothing is shown before the request is accepted
      const auto raw=data["text"].get<std::string>();
      const auto value=raw.size()>private_max_text?std::string():clean_chat_text(raw);
      if(value.empty()) return true;        // acknowledged and dropped
      if(room.last_in && now-room.last_in<1000) return false;
      room.last_in=now; room.name=name;
      private_append(room,{{"id",event["id"]},{"sender",sender},{"name",peer->second.profile.value("name",std::string("?"))},
                           {"text",value},{"time",now},{"wall_time",std::time(nullptr)}});
    } else if(action=="pm_close") {
      const auto rid=data.value("room",std::string());
      auto it=rooms.find(rid);
      if(it!=rooms.end() && it->second.peer==sender) {
        const auto state=it->second.state;
        if(state=="open") { it->second.state="closed"; it->second.name=name; }
        else if(state!="closed") {
          rooms.erase(it);
          say(state=="outgoing"?"lobby.pm.declined":"lobby.pm.missed",{{"name",name}},state=="outgoing"?"pm_declined":"pm_missed");
        }
        log("private chat "+short_id(rid)+" closed by "+short_id(sender));
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
    // The matches their launcher starts. With a list ("tp"): what it names. Without one: a launcher
    // that knows the peer-to-peer setup plays nothing else, and an older one only the hosted kind.
    peer.p2p=peer.protocol>=p2p_protocol; peer.hosted=!peer.p2p;
    // (A test standing in for the launcher where every match is peer to peer, as released before
    // the list existed, does not read it either.)
    if(peer.p2p && !test.p2p_only && packet.count("tp") && packet["tp"].is_array() && packet["tp"].size()<=8) {
      peer.p2p=false;
      for(const auto& item:packet["tp"]) if(item.is_string()) { if(item.get<std::string>()==kP2pTransport) peer.p2p=true; else peer.hosted=true; }
    }
    if(!known) log("peer "+short_id(sender)+" at "+endpoint_tag(from)+", protocol "+std::to_string(peer.protocol)+
                   (peer.p2p?(peer.hosted?", both kinds of match":", peer-to-peer matches only"):"")+
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
      auto nonce=content.value("nonce",std::string());
      if(nonce.size()==16) {
        Json pong={{"k","pong"},{"nonce",nonce}};
        // Where this launcher hears them from: the address their invite should name, which a
        // launcher behind a router cannot know by itself.
        if(p2p_on()) pong["you"]=endpoint_key(from);
        send_data(sender,pong);
      }
    } else if(kind=="pong") {
      if(content.value("nonce",std::string())==it->second.ping_nonce && it->second.ping_sent)
        it->second.ping=static_cast<int>(GetTickCount64()-it->second.ping_sent);
      sockaddr_in seen_as{};
      if(p2p_on() && content.count("you") && content["you"].is_string() && numeric_endpoint(content["you"].get<std::string>(),seen_as))
        observed=endpoint_key(seen_as);
    } else if(kind=="ack") {
      auto ref=content.value("ref",std::string()); auto pending=outbound.find(ref);
      if(pending!=outbound.end() && pending->second.target==sender) {
        const auto action=pending->second.data.value("action",std::string());
        const auto rid=pending->second.data["data"].value("request",std::string());
        auto request=requests.find(rid);
        if(action=="accept" && request!=requests.end() && request->second.value("state",std::string())=="accepted") {
          // A peer-to-peer match starts only with the requester's half of the setup, which this
          // acknowledgment carries. One without it (the request was gone on their side) starts nothing.
          bool complete=true;
          if(p2p_request(request->second)) {
            complete=content.count("p2p") && valid_setup(content["p2p"]);
            if(complete) request->second["p2p_theirs"]=content["p2p"];
            else if(content.count("p2p") && content["p2p"].is_object() && !legal_fighter(content["p2p"])) {
              say("lobby.refused.p2p_fighter",{{"name",peer_name(sender)}},"warn");
              log("accept of "+short_id(rid)+" acknowledged with a character or color out of range");
            }
            else log("accept of "+short_id(rid)+" acknowledged without a match setup");
          }
          if(complete) {
            request->second["confirmed"]=true; tracking.erase(rid);
            if(request->second.value("auto",false)) request_expiry[rid]=GetTickCount64()+120000;   // it had the short wait
            push_launch(rid);
          }
        }
        if(action=="request" && request!=requests.end() && request->second.value("state",std::string())=="pending" &&
           !request->second.value("delivered",false)) {
          request->second["delivered"]=true; tracking.erase(rid);
          say("lobby.delivered",{{"name",peer_name(sender)}},"delivered");
          log("request "+short_id(rid)+" delivered to "+short_id(sender));
        }
        if(action=="pm_request") {
          auto room=rooms.find(pending->second.data["data"].value("room",std::string()));
          if(room!=rooms.end() && room->second.state=="outgoing") room->second.delivered=true;
        }
        outbound.erase(pending);
      }
    } else if(kind=="event") {
      auto event_id=content.value("id",std::string()); if(event_id.size()!=32) return;
      auto seen=seen_events.find(event_id);
      if(seen!=seen_events.end() || handle_event(sender,content)) {
        if(seen_events.size()>=2048) seen_events.erase(seen_events.begin());
        seen_events[event_id]=GetTickCount64();
        Json ack={{"k","ack"},{"ref",event_id}};
        // The acknowledgment of an accept carries the requester's half of the match setup, on a
        // resend too (the first one may be the packet that was lost).
        if(p2p_capable() && content.count("action") && content["action"].is_string() && content["action"].get<std::string>()=="accept" &&
           content.count("data") && content["data"].is_object() && content["data"].count("request") && content["data"]["request"].is_string()) {
          auto r=requests.find(content["data"]["request"].get<std::string>());
          if(r!=requests.end() && r->second.value("from",std::string())==id && r->second.value("to",std::string())==sender &&
             p2p_request(r->second) && r->second.count("p2p_mine")) ack["p2p"]=r->second["p2p_mine"];
        }
        send_data(sender,ack);
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
    // The match character and color ride with the launcher's settings; they are not profile fields
    // (public_profile leaves them out), so other players learn them only in a match setup.
    p2p_ch=-1; p2p_col=0; hosted_ok=true;
    if(next.is_object()) {
      if(next.count("hosted_ok") && next["hosted_ok"].is_boolean()) hosted_ok=next["hosted_ok"].get<bool>();
      if(next.count("p2p_ch") && next["p2p_ch"].is_number_integer() && next["p2p_ch"].get<int>()>=0 && next["p2p_ch"].get<int>()<=25) p2p_ch=next["p2p_ch"].get<int>();
      if(next.count("p2p_col") && next["p2p_col"].is_number_integer() && next["p2p_col"].get<int>()>=0 && next["p2p_col"].get<int>()<=5) p2p_col=next["p2p_col"].get<int>();
    }
    Json candidate=public_profile(next);
    // With peer-to-peer matches the code is made from the name: the message names what the player sets.
    if(!valid_profile(candidate)) throw std::runtime_error(lang::tr(p2p_matches?"lobby.p2p.error.profile":"lobby.error.profile"));
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
    if(!valid_code(code)) throw std::runtime_error(lang::tr(p2p_matches?"lobby.p2p.error.code_format":"lobby.error.code_format"));
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
      set_searching(false);   // only players in the public lobby are paired
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
    if(action=="search" && p2p_on()) {
      const bool on=data.value("on",false);
      if(on && !visible) throw std::runtime_error(lang::tr("lobby.search.offline"));
      if(on && !profile.value("ready",false)) throw std::runtime_error(lang::tr("lobby.error.self_not_ready"));
      if(on!=searching) { set_searching(on); say(on?"lobby.search.on":"lobby.search.off",Json::object(),"search"); }
      return;
    }
    if(action=="connect" && p2p_on()) {
      // "MUINVITE1|<identity key>|<address>,<address>|<name>": the addresses become direct peer
      // candidates, and the player is selected once their launcher answers.
      std::string line=data.value("invite",std::string());
      while(!line.empty() && static_cast<unsigned char>(line.back())<=32) line.pop_back();
      while(!line.empty() && static_cast<unsigned char>(line.front())<=32) line.erase(line.begin());
      const auto a=line.find('|'),b=a==std::string::npos?a:line.find('|',a+1),c=b==std::string::npos?b:line.find('|',b+1);
      if(c==std::string::npos || line.size()>600 || line.substr(0,a)!="MUINVITE1") throw std::runtime_error(lang::tr("lobby.invite.bad"));
      const std::string key=line.substr(a+1,b-a-1),list=line.substr(b+1,c-b-1);
      if(!hex_text(key,64)) throw std::runtime_error(lang::tr("lobby.invite.bad"));
      if(key==id) throw std::runtime_error(lang::tr("lobby.invite.self"));
      int added=0;
      for(size_t at=0;at<=list.size() && added<4;) {
        size_t comma=list.find(',',at); if(comma==std::string::npos) comma=list.size();
        const std::string item=list.substr(at,comma-at);
        sockaddr_in address{};
        if(!item.empty() && item.size()<=80 && resolve(item,address)) {
          add_candidate(address); candidates[endpoint_key(address)].hello_sent=0; ++added;
        }
        at=comma+1;
      }
      if(!added) throw std::runtime_error(lang::tr("lobby.invite.bad"));
      invites[key]=now+30000;
      std::string name=clean_chat_text(line.substr(c+1));
      if(name.empty() || name.size()>96) name=lang::tr("lobby.other_player");
      say("lobby.invite.trying",{{"name",name}});
      log("invite: "+std::to_string(added)+" address(es) for "+short_id(key));
      return;
    }
    auto target=data.value("target",std::string());
    if((action=="block" || action=="unblock") && p2p_on()) {
      if(!hex_text(target,64) || target==id) throw std::runtime_error(lang::tr("lobby.error.offline"));
      if(action=="unblock") {
        if(blocked.erase(target)) { save(); say("lobby.unblocked",{{"name",peer_name(target)}}); }
        return;
      }
      if(blocked.size()>=1000) throw std::runtime_error(lang::tr("lobby.error.block_full"));
      blocked.insert(target); save();
      // Whatever is pending between the two ends here.
      for(auto it=requests.begin();it!=requests.end();) {
        const auto& r=it->second;
        if(r.value("state",std::string())=="pending" && (r.value("from",std::string())==target || r.value("to",std::string())==target)) {
          if(peers.count(target)) queue_event(target,"cancel",{{"request",it->first},{"code","auto"}});
          request_expiry.erase(it->first); tracking.erase(it->first); it=requests.erase(it);
        } else ++it;
      }
      say("lobby.blocked",{{"name",peer_name(target)}});
      log("blocked "+short_id(target));
      return;
    }
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
      if(blocked.count(target)) throw std::runtime_error(lang::tr("lobby.error.blocked",{{"name",name}}));
      if(busy()) throw std::runtime_error(lang::tr("lobby.error.self_busy"));
      if(!profile.value("ready",false)) throw std::runtime_error(lang::tr("lobby.error.self_not_ready"));
      // The request's own transport: peer to peer when every match of this launcher is, or when the
      // player asked for it; else the hosted kind.
      const bool p2p=p2p_on() || (p2p_capable() && data.count("transport") && data["transport"].is_string() &&
                                  data["transport"].get<std::string>()==kP2pTransport);
      // A launcher that does not know the peer-to-peer match setup would start something else, and
      // one of the other kind starts a game this one's does not play with.
      if(p2p && !p2p_peer(peer->second)) throw std::runtime_error(lang::tr("lobby.declined.p2p_version",{{"name",name}}));
      // The same from the other side: a launcher where every match is peer to peer has no hosted kind.
      if(!p2p && p2p_capable() && !peer->second.hosted) throw std::runtime_error(lang::tr("lobby.declined.p2p_version",{{"name",name}}));
      if(!p2p && !hosted_ok) throw std::runtime_error(lang::tr("lobby.error.self_no_account"));
      if(peer->second.status!="Online") throw std::runtime_error(lang::tr("lobby.declined.busy",{{"name",name}}));
      if(!peer->second.profile.value("ready",false)) throw std::runtime_error(lang::tr("lobby.declined.not_ready",{{"name",name}}));
      std::string mode=data.value("mode",std::string("vanilla"));
      if(p2p && !p2p_on()) {
        // Where both engines and mods exist: peer to peer is vanilla on the Source Port, both sides.
        if(mode!="vanilla") throw std::runtime_error(lang::tr("lobby.p2p.error.vanilla"));
        if(build_engine(profile)!="source") throw std::runtime_error(lang::tr("lobby.p2p.error.engine"));
        if(build_engine(peer->second.profile)!="source") throw std::runtime_error(lang::tr("lobby.declined.p2p_engine",{{"name",name}}));
      }
      // The same checks the other launcher makes, from its side, so a request that would be refused
      // is not sent at all and the reason shows straight away.
      const auto problem=mode_problem(peer->second.profile,profile,mode);
      if(!problem.empty()) throw std::runtime_error(declined_text(problem,mode,peer->second.profile,name));
      const auto own=mode_problem(profile,peer->second.profile,mode);
      if(!own.empty()) throw std::runtime_error(lang::tr("lobby.error.self_mode",{{"mode",mode_label(mode,profile)}}));
      if(!requests.empty() || now-last_request<3000) throw std::runtime_error(lang::tr("lobby.error.pending"));
      last_request=now; auto rid=nonce_id();
      // auto: sent by automatic pairing to a player who is searching too; their launcher answers.
      const bool automatic=p2p_on() && data.count("auto") && data["auto"].is_boolean() && data["auto"].get<bool>();
      requests[rid]={{"id",rid},{"from",id},{"to",target},{"state","pending"},{"transport",p2p?kP2pTransport:kHostedTransport},{"mode",mode},{"delivered",false}};
      request_expiry[rid]=now+(automatic?auto_request_wait:30000); tracking[rid]={now,false};
      Json body={{"request",rid},{"mode",mode},{"build",profile.value("build",std::string())},{"ready",true}};
      // Named only where a request can be either kind: the launcher where every match is peer to
      // peer never sent the field, and a hosted request stays exactly what an older launcher sends.
      if(p2p && !p2p_on()) body["transport"]=kP2pTransport;
      if(automatic) { requests[rid]["auto"]=true; body["auto"]=true; }
      queue_event(target,"request",body);
      peer->second.ping_nonce=nonce_id(8); peer->second.ping_sent=now;
      send_data(target,{{"k","ping"},{"nonce",peer->second.ping_nonce}});
      say(automatic?"lobby.search.asking":"lobby.sent",{{"name",name}},"sent");
      log("request "+short_id(rid)+" out to "+short_id(target)+", mode "+mode+(p2p?", peer to peer":"")+", their protocol "+std::to_string(peer->second.protocol)+(automatic?", automatic":""));
      return;
    }
    if(action=="pm_request") {
      auto peer=peers.find(target);
      if(target==id || peer==peers.end() || now-peer->second.seen>=20000) throw std::runtime_error(lang::tr("lobby.error.offline"));
      const auto name=peer_name(target);
      if(!private_reach(target,peer->second)) throw std::runtime_error(lang::tr("lobby.error.left",{{"name",name}}));
      // A launcher from before private chat never answers the request, so say why straight away.
      if(peer->second.protocol<private_protocol) throw std::runtime_error(lang::tr("lobby.pm.unsupported",{{"name",name}}));
      private_blocked.erase(target);   // asking someone unblocks them
      const auto held=room_with(target);
      if(!held.empty()) {
        const auto state=rooms[held].state;
        if(state=="open") throw std::runtime_error(lang::tr("lobby.pm.already",{{"name",name}}));
        if(state=="outgoing") throw std::runtime_error(lang::tr("lobby.pm.pending",{{"name",name}}));
        if(state=="incoming") {       // they already asked: this is the answer
          queue_event(target,"pm_accept",{{"room",held}});
          private_open(held);
          return;
        }
      }
      if(now-last_private_request<3000) throw std::runtime_error(lang::tr("lobby.pm.wait"));
      if(private_live()>=private_max_rooms) throw std::runtime_error(lang::tr("lobby.pm.full"));
      last_private_request=now;
      const auto rid=nonce_id();
      // A room they closed earlier continues, with what was said in it, if they accept.
      auto& room=held.empty()?rooms[rid]:private_move(held,rid);
      room.peer=target; room.name=name; room.state="outgoing"; room.created=now; room.expires=now+private_wait(); room.delivered=false;
      private_trim();
      queue_event(target,"pm_request",{{"room",rid}});
      say("lobby.pm.sent",{{"name",name}},"pm_sent");
      log("private chat request "+short_id(rid)+" out to "+short_id(target));
      return;
    }
    if(action=="pm_accept" || action=="pm_decline") {
      const auto rid=data.value("room",std::string()); auto it=rooms.find(rid);
      if(it==rooms.end() || it->second.state!="incoming") throw std::runtime_error(lang::tr("lobby.pm.gone"));
      const auto other=it->second.peer;
      if(action=="pm_accept") {
        queue_event(other,"pm_accept",{{"room",rid}});
        private_open(rid);
      } else {
        rooms.erase(it);
        queue_event(other,"pm_decline",{{"room",rid},{"code","user"}});
        if(data.value("block",false)) { private_blocked.insert(other); say("lobby.pm.blocked",{{"name",peer_name(other)}},"pm_blocked"); }
        log("declined private chat request "+short_id(rid)+" from "+short_id(other));
      }
      return;
    }
    if(action=="pm") {
      const auto rid=data.value("room",std::string()); auto it=rooms.find(rid);
      if(it==rooms.end() || it->second.state!="open") throw std::runtime_error(lang::tr("lobby.pm.closed"));
      auto& room=it->second;
      auto peer=peers.find(room.peer);
      if(peer==peers.end() || now-peer->second.seen>=20000) throw std::runtime_error(lang::tr("lobby.error.offline"));
      const auto value=clean_chat_text(data.value("text",std::string()));
      const Json body={{"room",rid},{"text",value}};
      // 500 bytes is what one encrypted peer message holds (send_data); quotes and backslashes count twice.
      if(value.empty() || value.size()>private_max_text || (room.last_out && now-room.last_out<1000) || event_size("pm",body)>500)
        throw std::runtime_error(lang::tr("lobby.error.chat"));
      room.last_out=now;
      private_append(room,{{"id",nonce_id()},{"sender",id},{"name",profile.value("name",std::string())},
                           {"text",value},{"time",now},{"wall_time",std::time(nullptr)}});
      queue_event(room.peer,"pm",body);   // to that one player, never to the lobby
      return;
    }
    if(action=="pm_close") {
      const auto rid=data.value("room",std::string()); auto it=rooms.find(rid);
      if(it==rooms.end()) return;
      const auto other=it->second.peer, state=it->second.state;
      rooms.erase(it);
      if(state=="outgoing") private_forget(rid);
      if(peers.count(other)) {
        if(state=="incoming") queue_event(other,"pm_decline",{{"room",rid},{"code","user"}});
        else if(state!="closed") queue_event(other,"pm_close",{{"room",rid}});
      }
      if(data.value("block",false)) { private_blocked.insert(other); say("lobby.pm.blocked",{{"name",peer_name(other)}},"pm_blocked"); }
      log("closed private chat "+short_id(rid)+" with "+short_id(other));
      return;
    }
    if(action=="pm_unblock") { private_blocked.erase(target); return; }
    if(action=="accept" || action=="cancel") {
      auto rid=data.value("request",std::string()); auto it=requests.find(rid);
      if(it==requests.end() || it->second.value("state",std::string())!="pending") throw std::runtime_error(lang::tr("lobby.error.expired"));
      const bool incoming_request=it->second.value("to",std::string())==id;
      target=incoming_request?it->second.value("from",std::string()):it->second.value("to",std::string());
      if(action=="accept") {
        if(!incoming_request || busy()) throw std::runtime_error(lang::tr("lobby.error.cannot_accept"));
        accept_request(rid);
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
  // P2P Unranked: while this player is searching and free, ask the best candidate for a match, at
  // most one request at a time. Candidates are pinged first so the choice is the lowest ping, and
  // near-equal pings are settled at random, so three players do not keep asking in the same circle.
  void auto_pair(ULONGLONG now) {
    if(!searching || now<last_auto) return;
    uint32_t pick=0; random_bytes(&pick,sizeof pick);
    last_auto=now+2000+pick%1000;   // the next look, not in step with the other searching launchers
    for(auto it=auto_skip.begin();it!=auto_skip.end();) if(now>=it->second) it=auto_skip.erase(it); else ++it;
    if(!visible || busy() || !requests.empty() || !profile.value("ready",false) || now-last_request<3000) return;
    std::vector<std::pair<int,std::string>> pool;
    bool measuring=false;
    for(auto& item:peers) {
      auto& peer=item.second;
      if(!auto_candidate(item.first,peer,now)) continue;
      if(!peer.probe_since) peer.probe_since=now;
      if(now-peer.ping_sent>(peer.ping<0?1000u:5000u)) {
        peer.ping_nonce=nonce_id(8); peer.ping_sent=now;
        send_data(item.first,{{"k","ping"},{"nonce",peer.ping_nonce}});
      }
      // No answer to the ping yet: wait for it a little, then count this player as far away.
      if(peer.ping<0 && now-peer.probe_since<2500) { measuring=true; continue; }
      pool.emplace_back(peer.ping<0?999:peer.ping/10,item.first);
    }
    if(pool.empty() || measuring) return;
    const int best=std::min_element(pool.begin(),pool.end())->first;
    std::vector<std::string> nearest;
    for(const auto& item:pool) if(item.first==best) nearest.push_back(item.second);
    const std::string target=nearest[(pick>>12)%nearest.size()];
    try { command("request",{{"target",target},{"mode","vanilla"},{"auto",true}}); }
    catch(const std::exception&) {
      auto_skip[target]=now+5000;
      log("automatic request to "+short_id(target)+" could not be sent");
    }
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
      // A first ping to each player: its answer says how this launcher is seen from outside (for the
      // invite) and gives automatic pairing a ping to compare before it asks anyone.
      if(p2p_on() && peer.second.protocol>=p2p_protocol && peer.second.ping<0 && now-peer.second.ping_sent>3000) {
        peer.second.ping_nonce=nonce_id(8); peer.second.ping_sent=now;
        send_data(peer.first,{{"k","ping"},{"nonce",peer.second.ping_nonce}});
      }
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
      if(r.value("auto",false)) {
        // Automatic pairing moves on to someone else by itself.
      } else if(p2p_on()) {
        // There is no code to fall back on here: the way around is an invite (Connect by address).
        say(outgoing?"lobby.p2p.unreachable":"lobby.p2p.accept_unreachable",{{"name",peer_name(other)}},"warn");
      } else if(p2p_request(r)) {
        // A peer-to-peer request of the launcher with both kinds: no invite to offer, and the other
        // player's code would start a different kind of match.
        say("lobby.p2p.no_route",{{"name",peer_name(other)}},"warn");
      } else if(outgoing && heard && peer_protocol(other)<2) {
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
      if(it->second.value("auto",false)) {
        // An automatic request nobody took: both players go on searching, and this one asks
        // someone else before it asks the same player again.
        const bool mine=it->second.value("from",std::string())==id;
        if(mine) auto_skip[it->second.value("to",std::string())]=now+15000;
      } else if(state=="pending") {
        if(it->second.value("from",std::string())==id) {
          const auto to=it->second.value("to",std::string());
          if(it->second.value("delivered",false)) say("lobby.expired",{{"name",peer_name(to)}},"expired");
          else if(!tracking.count(it->first) || !tracking[it->first].warned) {
            if(p2p_on()) say("lobby.p2p.unreachable",{{"name",peer_name(to)}},"warn");
            else if(p2p_request(it->second)) say("lobby.p2p.no_route",{{"name",peer_name(to)}},"warn");
            else say("lobby.unreachable",{{"name",peer_name(to)},{"code",peer_code(to)}},"unreachable");
          }
        } else say("lobby.missed",{{"name",peer_name(it->second.value("from",std::string()))}},"missed");
      }
      log("request "+short_id(it->first)+" expired ("+state+")");
      tracking.erase(it->first); request_expiry.erase(it->first); it=requests.erase(it);
    } else ++it;
    for(auto it=tracking.begin();it!=tracking.end();) if(!requests.count(it->first)) it=tracking.erase(it); else ++it;
    // A private chat request nobody answered. An older launcher never acknowledges one, so the
    // request also ends here for a player whose version this launcher got wrong.
    for(auto it=rooms.begin();it!=rooms.end();) {
      const auto& room=it->second;
      if((room.state!="outgoing" && room.state!="incoming") || now<room.expires) { ++it; continue; }
      const auto rid=it->first, other=room.peer, state=room.state;
      const bool delivered=room.delivered;
      it=rooms.erase(it);
      if(state=="outgoing") {
        private_forget(rid);
        say(delivered?"lobby.pm.expired":"lobby.pm.no_answer",{{"name",peer_name(other)}},"pm_expired");
        if(peers.count(other)) queue_event(other,"pm_close",{{"room",rid}});
      } else say("lobby.pm.missed",{{"name",peer_name(other)}},"pm_missed");
      log("private chat request "+short_id(rid)+" expired ("+state+(delivered?", delivered)":")"));
    }
    bool lookups_done=false;
    for(auto it=code_lookups.begin();it!=code_lookups.end();) if(now>=it->second) {
      say("lobby.friend.not_found",{{"code",it->first},{"version",build_version(profile.value("build",std::string()))}},"warn");
      log("friend lookup ended without an answer");
      it=code_lookups.erase(it); lookups_done=true;
    } else ++it;
    if(lookups_done) refresh_topics();
    if(p2p_on()) {
      if(!lan_at || now-lan_at>30000) { lan=lan_addresses(); lan_at=now; }
      // A pasted invite: the player is selected once their launcher answers at one of its addresses.
      for(auto it=invites.begin();it!=invites.end();) {
        auto peer=peers.find(it->first);
        if(peer!=peers.end() && peer->second.seen && now-peer->second.seen<20000) {
          invite_found=it->first; ++invite_seq;
          say("lobby.invite.found",{{"name",peer_name(it->first)}});
          log("invite: "+short_id(it->first)+" answered");
          it=invites.erase(it);
        } else if(now>=it->second) {
          say("lobby.invite.not_found",Json::object(),"warn");
          log("invite: no answer from "+short_id(it->first));
          it=invites.erase(it);
        } else ++it;
      }
      auto_pair(now);
    }
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
      player["p2p"]=p2p_peer(peer.second);   // a peer-to-peer match with them can be asked for
      if(blocked.count(peer.first)) player["blocked"]=true;
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
      if(online) { item["protocol"]=peer->second.protocol; item["visible"]=peer->second.visible; item["p2p"]=p2p_peer(peer->second); }
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
    // Private rooms, each with its own messages: never part of "messages" above.
    result["private"]=Json::array(); result["private_blocked"]=Json::array();
    for(const auto& item:rooms) {
      const auto& room=item.second;
      auto peer=peers.find(room.peer);
      const bool online=peer!=peers.end() && peer->second.seen && now-peer->second.seen<20000;
      const auto known=peer==peers.end()?std::string():peer->second.profile.value("name",std::string());
      Json entry={{"id",item.first},{"peer",room.peer},{"name",!known.empty()?known:!room.name.empty()?room.name:peer_name(room.peer)},
                  {"state",room.state},{"online",online},{"created",room.created},
                  {"remaining",room.expires>now?static_cast<int>((room.expires-now)/1000):0},{"messages",Json::array()}};
      if(room.state=="open" || room.state=="closed") for(const auto& message:room.messages) entry["messages"].push_back(message);
      result["private"].push_back(entry);
    }
    for(const auto& item:private_blocked) result["private_blocked"].push_back(item);
    if(p2p_on()) {
      result["self"]["searching"]=searching;
      result["blocked"]=Json(blocked);
      result["invite"]={{"text",invite_text()},{"port",listen_port},{"found",invite_found},{"seq",invite_seq}};
    }
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
void PeerLobby::presence_now() { p_->last_presence=0; }
void PeerLobby::tick() { p_->tick(); }
Json PeerLobby::state() const { return p_->state(); }
std::map<std::string,int> PeerLobby::pings() const { return p_->pings(); }
bool PeerLobby::take_launch(Json& launch) {
  if(p_->launches.empty()) return false;
  launch=p_->launches.front(); p_->launches.pop_front(); return true;
}
std::string PeerLobby::invite() const { return p_->invite_text(); }
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
