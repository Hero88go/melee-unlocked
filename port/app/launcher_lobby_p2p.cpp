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
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
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
bool valid_profile(const Json& profile) {
  if(!profile.is_object()) return false;
  for(const auto& field:{"name","code","location","build"}) {
    if(!profile.count(field) || !profile[field].is_string()) return false;
    auto value=profile[field].get<std::string>();
    // Location is optional: Go Online only asks for a name, a code and a main, so a blank location
    // must not make the join fail (it did, silently, and the button fell back to Go Online).
    if((value.empty() && std::string(field)!="location") || value.size()>(std::string(field)=="name"?32:std::string(field)=="code"?10:std::string(field)=="location"?48:80)) return false;
    if(std::any_of(value.begin(),value.end(),[](unsigned char c){return c<32;})) return false;
  }
  const auto code=profile["code"].get<std::string>();
  const auto hash=code.find('#');
  if(hash==std::string::npos || hash<1 || hash>4 || code.size()-hash-1<1 || code.size()-hash-1>5) return false;
  for(size_t i=0;i<code.size();++i) {
    if(i==hash) continue;
    if(i<hash ? !(code[i]>='A'&&code[i]<='Z') && !(code[i]>='0'&&code[i]<='9')
              : !(code[i]>='0'&&code[i]<='9')) return false;
  }
  if(!profile.count("mains") || !profile["mains"].is_array() || (profile["mains"].empty() || profile["mains"].size()>3)) return false;
  std::set<int> mains;
  for(const auto& main:profile["mains"]) {
    if(!main.is_number_integer() || main.get<int>()<0 || main.get<int>()>25) return false;
    mains.insert(main.get<int>());
  }
  return mains.size()==profile["mains"].size() && profile.count("ready") && profile["ready"].is_boolean();
}
bool valid_wire_profile(const Json& profile) {
  if(!profile.is_object() || !profile.count("ready") || !profile["ready"].is_boolean()) return false;
  Json copy=profile; copy["ready"]=true; return valid_profile(copy);
}
bool valid_stocks(const Json& value) {
  if(!value.is_array() || value.size()>4) return false;
  for(const auto& count:value) if(!count.is_number_integer() || count.get<int>()<0 || count.get<int>()>99) return false;
  return true;
}
void send_udp(SOCKET socket,const sockaddr_in& to,const Json& payload) {
  auto text=std::string(wire_prefix)+payload.dump();
  if(text.size()<=1400) sendto(socket,text.data(),static_cast<int>(text.size()),0,
                               reinterpret_cast<const sockaddr*>(&to),sizeof to);
}
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
    ULONGLONG seen=0,hello_sent=0,ping_sent=0,last_chat=0,last_request=0,last_friend_confirm=0;
    bool visible=false;
    std::string status="Offline",ping_nonce;
    Json stocks=Json::array();
    int ping=-1;
  };
  struct Candidate { sockaddr_in address{}; ULONGLONG hello_sent=0,seen=0; };
  struct Topic { Bytes20 hash{}; ULONGLONG searched=0; int attempts=0; bool done=true,public_topic=false; };
  struct Outbound { std::string target; Json data; ULONGLONG sent=0,expires=0; };
  std::string directory,id,bootstrap;
  SOCKET socket=INVALID_SOCKET;
  int listen_port=0;
  Bytes32 ed_seed{},ed_public{},x_secret{},x_public{};
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
  std::map<std::string,Outbound> outbound;
  std::vector<Json> messages;
  std::vector<Topic> topics;
  std::vector<sockaddr_in> seeds;
  ULONGLONG last_request=0,last_chat=0;

  Impl(const std::string& dir,const std::string& seed,int requested_port):directory(dir),bootstrap(seed) {
    load();
    sockaddr_in address{};
    if(bootstrap.empty()) {
      if(resolve("router.bittorrent.com:6881",address)) seeds.push_back(address);
      if(resolve("dht.transmissionbt.com:6881",address)) seeds.push_back(address);
    }
    const bool dht_only=bootstrap.rfind("dht://",0)==0;
    const auto custom=dht_only?bootstrap.substr(6):bootstrap;
    if(!custom.empty()) {
      if(!resolve(custom,address)) throw std::runtime_error("Bootstrap peer must be a valid host:UDP-port");
      seeds.insert(seeds.begin(),address);
      if(!dht_only) add_candidate(address); // A manually supplied peer can connect before DHT convergence.
    }
    if(seeds.empty()) throw std::runtime_error("Cannot resolve DHT bootstrap; enter a reachable peer IP:port");
    socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    sockaddr_in local{}; local.sin_family=AF_INET; local.sin_port=htons(static_cast<u_short>(requested_port));
    local.sin_addr.s_addr=htonl(INADDR_ANY);
    if(socket==INVALID_SOCKET || bind(socket,reinterpret_cast<sockaddr*>(&local),sizeof local)!=0) {
      if(socket!=INVALID_SOCKET) closesocket(socket);
      throw std::runtime_error("Cannot open peer lobby UDP port");
    }
    int size=sizeof local; getsockname(socket,reinterpret_cast<sockaddr*>(&local),&size);
    listen_port=ntohs(local.sin_port);
    u_long nonblocking=1; ioctlsocket(socket,FIONBIO,&nonblocking);
    dht_socket_handle=socket;
    Bytes20 node{}; random_bytes(node.data(),node.size());
    if(dht_init(1,-1,node.data(),reinterpret_cast<const uint8_t*>("MU01"))<0) {
      dht_socket_handle=INVALID_SOCKET; closesocket(socket); socket=INVALID_SOCKET;
      throw std::runtime_error("Cannot initialize peer discovery");
    }
    refresh_topics();
  }
  ~Impl() {
    dht_uninit(); dht_socket_handle=INVALID_SOCKET;
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
    if(!out) throw std::runtime_error("Cannot save peer lobby identity");
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
        throw std::runtime_error("Peer lobby identity is invalid; restore the original file");
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
  void refresh_topics() {
    std::map<std::string,Topic> old;
    for(const auto& t:topics) old.emplace(hex(t.hash.data(),t.hash.size()),t);
    topics.clear();
    std::vector<std::string> names;
    if(visible) names.push_back("melee-unlocked-lobby-v1-public");
    for(const auto& f:friends) names.push_back("melee-unlocked-lobby-v1-friend:"+std::min(id,f.first)+":"+std::max(id,f.first));
    for(const auto& name:names) {
      Topic t; t.hash=topic_hash(name); t.public_topic=name=="melee-unlocked-lobby-v1-public";
      auto it=old.find(hex(t.hash.data(),t.hash.size()));
      if(it!=old.end()) t=it->second;
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
  void send_hello(const sockaddr_in& address) {
    if(!valid_wire_profile(profile)) return;
    Json body={{"v",1},{"t","hello"},{"pk",id},{"xk",hex(x_public.data(),x_public.size())},
               {"profile",profile},{"visible",visible},{"time",std::time(nullptr)},
               {"nonce",nonce_id(12)}};
    auto content=body.dump(); Bytes64 signature{};
    crypto_ed25519_sign(signature.data(),ed_secret.data(),reinterpret_cast<const uint8_t*>(content.data()),content.size());
    body["sig"]=hex(signature.data(),signature.size()); send_udp(socket,address,body);
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
    auto body=plain.dump(); if(body.size()>500) return;
    auto shared=key(it->second);
    std::array<uint8_t,24> nonce{}; std::array<uint8_t,16> mac{};
    random_bytes(nonce.data(),nonce.size()); std::string cipher(body.size(),'\0');
    static constexpr uint8_t ad[]={'M','U','L','1'};
    crypto_aead_lock(reinterpret_cast<uint8_t*>(cipher.data()),mac.data(),shared.data(),nonce.data(),
                     ad,sizeof ad,reinterpret_cast<const uint8_t*>(body.data()),body.size());
    Json packet={{"v",1},{"t","data"},{"pk",id},{"nonce",hex(nonce.data(),nonce.size())},
                 {"mac",hex(mac.data(),mac.size())},{"ct",hex(reinterpret_cast<const uint8_t*>(cipher.data()),cipher.size())}};
    send_udp(socket,it->second.address,packet);
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
  bool handle_event(const std::string& sender,const Json& event) {
    if(!event.count("action") || !event["action"].is_string() || !event.count("data") || !event["data"].is_object()) return false;
    const auto action=event["action"].get<std::string>();
    const auto& data=event["data"];
    auto peer=peers.find(sender); if(peer==peers.end()) return false;
    auto now=GetTickCount64();
    if(action=="friend") {
      if(!visible || !peer->second.visible) return false;
      if(friends.count(sender)==0 && incoming.size()<100) {
        incoming[sender]={{"id",sender},{"name",peer->second.profile.value("name",std::string("?"))}};
        save();
      }
    } else if(action=="friend_accept") {
      if(!outgoing_friends.count(sender) && !friends.count(sender)) return false;
      if(friends.size()>=100) return false;
      incoming.erase(sender); outgoing_friends.erase(sender);
      friends[sender]={{"id",sender},{"name",peer->second.profile.value("name",std::string("?"))}};
      refresh_topics(); save();
    } else if(action=="friend_decline") {
      incoming.erase(sender); save();
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
      if(rid.size()!=32 || !visible || !peer->second.visible || busy() ||
         !profile.value("ready",false) || !peer->second.profile.value("ready",false) ||
         peer->second.profile.value("build",std::string())!=profile.value("build",std::string())) return false;
      if(now-peer->second.last_request<3000) return false;
      peer->second.last_request=now;
      if(!requests.count(rid)) {
        if(!requests.empty()) return false;
        requests[rid]={{"id",rid},{"from",sender},{"to",id},{"state","pending"},{"transport","slippi-direct"}};
        request_expiry[rid]=now+30000;
        peer->second.ping_nonce=nonce_id(8); peer->second.ping_sent=now;
        send_data(sender,{{"k","ping"},{"nonce",peer->second.ping_nonce}});
      }
    } else if(action=="accept") {
      auto rid=data.value("request",std::string()); auto it=requests.find(rid);
      if(it==requests.end() || it->second.value("from",std::string())!=id ||
         it->second.value("to",std::string())!=sender || it->second.value("state",std::string())!="pending") return false;
      it->second["state"]="accepted"; it->second["confirmed"]=true; request_expiry[rid]=now+120000;
    } else if(action=="cancel") {
      auto rid=data.value("request",std::string()); auto it=requests.find(rid);
      if(it!=requests.end() && (it->second.value("from",std::string())==sender || it->second.value("to",std::string())==sender)) {
        requests.erase(it); request_expiry.erase(rid);
      }
    } else return false;
    return true;
  }
  void on_hello(const Json& packet,const sockaddr_in& from) {
    if(packet.value("v",0)!=1 || !packet.count("profile") || !packet.count("sig") ||
       !packet["sig"].is_string()) return;
    Bytes32 pub{},xpub{}; Bytes64 signature{};
    auto sender=packet.value("pk",std::string());
    if(sender==id || !unhex(sender,pub.data(),pub.size()) ||
       !unhex(packet.value("xk",std::string()),xpub.data(),xpub.size()) ||
       !unhex(packet["sig"].get<std::string>(),signature.data(),signature.size()) ||
       !valid_wire_profile(packet["profile"]) || !packet.count("time") || !packet["time"].is_number_integer()) return;
    auto moment=packet["time"].get<long long>();
    if(std::llabs(static_cast<long long>(std::time(nullptr))-moment)>120) return;
    auto signed_part=packet; signed_part.erase("sig"); auto text=signed_part.dump();
    if(crypto_ed25519_check(signature.data(),pub.data(),reinterpret_cast<const uint8_t*>(text.data()),text.size())!=0) return;
    if(!peers.count(sender) && peers.size()>=256) return;
    auto& peer=peers[sender]; peer.address=from; peer.xkey=xpub; peer.profile=packet["profile"];
    auto now=GetTickCount64();
    if(now-peer.hello_sent>1000) { send_hello(from); peer.hello_sent=now; }
    if(friends.count(sender) && now-peer.last_friend_confirm>30000) {
      queue_event(sender,"friend_accept",Json::object()); peer.last_friend_confirm=now;
    }
    const bool share_status=visible || friends.count(sender)!=0;
    send_data(sender,{{"k","presence"},{"visible",visible},
                      {"status",share_status?status.value("status",std::string("Online")):std::string("Online")},
                      {"stocks",share_status?status.value("stocks",Json::array()):Json::array()}});
  }
  void on_data(const Json& packet,const sockaddr_in& from) {
    if(packet.value("v",0)!=1) return;
    auto sender=packet.value("pk",std::string()); auto it=peers.find(sender);
    if(it==peers.end() || !packet.count("ct") || !packet["ct"].is_string()) return;
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
    crypto_wipe(nonce_key.data(),nonce_key.size()); if(verified!=0) return;
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
        if(pending->second.data.value("action",std::string())=="accept") {
          auto rid=pending->second.data["data"].value("request",std::string());
          if(requests.count(rid)) requests[rid]["confirmed"]=true;
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
  void join(const Json& next) {
    Json public_profile=next;
    for(const auto& field:{"mode","url","token","udp_port","peer_port"}) public_profile.erase(field);
    auto code=public_profile.value("code",std::string());
    std::transform(code.begin(),code.end(),code.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
    public_profile["code"]=code;
    if(!valid_profile(public_profile)) throw std::runtime_error("Enter a valid profile, Slippi code and one to three different mains");
    profile=public_profile; visible=true; refresh_topics(); save();
    last_hello=0; last_presence=0;
  }
  void command(const std::string& action,const Json& data) {
    if(action=="leave") {
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
        throw std::runtime_error("Join the lobby and enter a message up to 300 characters");
      last_chat=now;
      auto message_id=nonce_id();
      chat_append({{"id",message_id},{"sender",id},{"name",profile.value("name",std::string())},
                   {"text",value},{"time",now},{"wall_time",std::time(nullptr)}});
      for(const auto& peer:peers) if(peer.second.visible && now-peer.second.seen<20000)
        queue_event(peer.first,"chat",{{"text",value}});
      return;
    }
    auto target=data.value("target",std::string());
    if(action=="friend" || action=="friend_accept" || action=="friend_decline" || action=="unfriend") {
      if(action=="friend") {
        if(!visible || !peers.count(target) || now-peers.at(target).seen>=20000) throw std::runtime_error("Player is offline");
        outgoing_friends.insert(target);
      } else if(action=="friend_accept" || action=="friend_decline") {
        if(!incoming.count(target)) throw std::runtime_error("Friend request no longer exists");
        incoming.erase(target);
        if(action=="friend_accept") {
          friends[target]={{"id",target},{"name",peers.count(target)?peers.at(target).profile.value("name",std::string("?")):std::string("?")}};
          refresh_topics();
        }
      } else {
        if(!friends.count(target)) throw std::runtime_error("Player is not a friend");
        friends.erase(target); outgoing_friends.erase(target); refresh_topics();
      }
      save(); if(peers.count(target)) queue_event(target,action,Json::object());
      return;
    }
    if(action=="request") {
      auto peer=peers.find(target);
      if(!visible || peer==peers.end() || !peer->second.visible || now-peer->second.seen>=20000)
        throw std::runtime_error("Player left the lobby");
      if(busy() || peer->second.status!="Online" || !profile.value("ready",false) ||
         !peer->second.profile.value("ready",false)) throw std::runtime_error("A player is busy or needs game setup");
      if(profile.value("build",std::string())!=peer->second.profile.value("build",std::string()))
        throw std::runtime_error("Choose the same game build and version");
      if(!requests.empty() || now-last_request<3000) throw std::runtime_error("A match request is already pending");
      last_request=now; auto rid=nonce_id();
      requests[rid]={{"id",rid},{"from",id},{"to",target},{"state","pending"},{"transport","slippi-direct"}};
      request_expiry[rid]=now+30000; queue_event(target,"request",{{"request",rid}});
      peer->second.ping_nonce=nonce_id(8); peer->second.ping_sent=now;
      send_data(target,{{"k","ping"},{"nonce",peer->second.ping_nonce}});
      return;
    }
    if(action=="accept" || action=="cancel") {
      auto rid=data.value("request",std::string()); auto it=requests.find(rid);
      if(it==requests.end() || it->second.value("state",std::string())!="pending") throw std::runtime_error("Request expired or cancelled");
      if(action=="accept") {
        if(it->second.value("to",std::string())!=id || busy()) throw std::runtime_error("Only the invited available player can accept");
        it->second["state"]="accepted"; it->second["confirmed"]=false; request_expiry[rid]=now+120000;
      } else { target=it->second.value("from",std::string())==id?it->second.value("to",std::string()):it->second.value("from",std::string());
        requests.erase(it); request_expiry.erase(rid); }
      if(target.empty()) target=it->second.value("from",std::string());
      queue_event(target,action,{{"request",rid}}); return;
    }
    throw std::runtime_error("Unknown peer lobby action");
  }
  void tick() {
    const auto now=GetTickCount64();
    for(int i=0;i<128;++i) {
      char packet[4097]; sockaddr_in from{}; int address_size=sizeof from;
      int length=recvfrom(socket,packet,sizeof packet-1,0,reinterpret_cast<sockaddr*>(&from),&address_size);
      if(length<=0) break;
      packet[length]='\0';
      if(packet[0]=='d') { time_t wait=0; dht_periodic(packet,length,reinterpret_cast<sockaddr*>(&from),address_size,&wait,dht_result,this); continue; }
      if(length<4 || std::memcmp(packet,wire_prefix,4)!=0 || length>1400) continue;
      try {
        auto body=Json::parse(std::string(packet+4,length-4),nullptr,false);
        if(!body.is_object()) continue;
        const auto type=body.value("t",std::string());
        if(type=="hello") on_hello(body,from);
        else if(type=="data") on_data(body,from);
      } catch(...) {} // Untrusted UDP input cannot take down the launcher.
    }
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
        if(dht_search(topic.hash.data(),listen_port,AF_INET,dht_result,this)<0) topic.done=true;
        topic.searched=now; ++topic.attempts; last_topics=now;
        break;
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
    for(auto it=requests.begin();it!=requests.end();) if(now>=request_expiry[it->first]) {
      request_expiry.erase(it->first); it=requests.erase(it);
    } else ++it;
    for(auto it=seen_events.begin();it!=seen_events.end();) if(now-it->second>120000) it=seen_events.erase(it); else ++it;
    for(auto it=candidates.begin();it!=candidates.end();) if(now-it->second.seen>120000) it=candidates.erase(it); else ++it;
  }
  Json state() const {
    const auto now=GetTickCount64();
    int good=0,dubious=0; dht_nodes(AF_INET,&good,&dubious,nullptr,nullptr);
    Json result={{"self",{{"id",id},{"visible",visible}}},{"players",Json::array()},
                 {"requests",Json::array()},{"friends",Json::array()},{"friend_requests",Json::array()},
                 {"messages",Json::array()},
                 {"network",{{"dht_nodes",good+dubious},{"candidates",candidates.size()},{"peers",peers.size()}}}};
    for(const auto& peer:peers) if(peer.second.seen && now-peer.second.seen<20000 && peer.second.visible && visible) {
      Json player=peer.second.profile;
      player["id"]=peer.first; player["status"]=peer.second.status; player["stocks"]=peer.second.stocks;
      player["busy"]=peer.second.status!="Online";
      result["players"].push_back(player);
    }
    for(const auto& friend_entry:friends) {
      auto peer=peers.find(friend_entry.first);
      bool online=peer!=peers.end() && peer->second.seen && now-peer->second.seen<20000;
      result["friends"].push_back({{"id",friend_entry.first},
        {"name",online?peer->second.profile.value("name",std::string("?")):friend_entry.second.value("name",std::string("?"))},
        {"status",online?peer->second.status:std::string("Offline")},
        {"stocks",online?peer->second.stocks:Json::array()}});
    }
    for(const auto& request:incoming) result["friend_requests"].push_back(request.second);
    for(const auto& request:requests) {
      auto item=request.second;
      item["remaining"]=static_cast<int>((request_expiry.at(request.first)-now)/1000);
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

PeerLobby::PeerLobby(const std::string& dir,const std::string& seed,int port):p_(std::make_unique<Impl>(dir,seed,port)) {}
PeerLobby::~PeerLobby()=default;
void PeerLobby::join(const Json& profile) { p_->join(profile); }
void PeerLobby::command(const std::string& action,const Json& data) { p_->command(action,data); }
void PeerLobby::presence(const Json& status) { p_->set_presence(status); }
void PeerLobby::tick() { p_->tick(); }
Json PeerLobby::state() const { return p_->state(); }
std::map<std::string,int> PeerLobby::pings() const { return p_->pings(); }
const std::string& PeerLobby::id() const { return p_->id; }
bool PeerLobby::visible() const { return p_->visible; }
int PeerLobby::port() const { return p_->listen_port; }
}
