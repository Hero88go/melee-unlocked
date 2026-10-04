#include "launcher_replay_data.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <climits>
#include <sstream>

namespace launcher::replay {
namespace {
#include "launcher_replay_lag.inl"
constexpr const char* characters[]={"Captain Falcon","Donkey Kong","Fox","Mr. Game & Watch","Kirby","Bowser",
  "Link","Luigi","Mario","Marth","Mewtwo","Ness","Peach","Pikachu","Ice Climbers","Jigglypuff",
  "Samus","Yoshi","Zelda","Sheik","Falco","Young Link","Dr. Mario","Roy","Pichu","Ganondorf"};
unsigned be16(const uint8_t* p) { return unsigned(p[0])<<8|p[1]; }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
float befloat(const uint8_t* p) { uint32_t bits=be32(p); float value{}; std::memcpy(&value,&bits,4); return value; }
std::string game_text(const uint8_t* p,size_t count) {
  // UTF-8 is common in modern Slippi files. Keep ASCII names readable if a legacy
  // Shift-JIS tag is encountered; the metadata names take precedence when present.
  std::string result;
  for(size_t i=0;i<count && p[i];++i) if(p[i]>=32) result.push_back(char(p[i]));
  return result;
}
std::string game_code(const uint8_t* p,size_t count) {
  // Connect codes in older game-start events use a Shift-JIS full-width #.
  // Normalize that separator for the launcher when metadata has no UTF-8 code.
  std::string result;
  for(size_t i=0;i<count && p[i];++i) {
    if(p[i]>=128) {
      if(result.empty() || result.back()!='#') result.push_back('#');
    } else if(p[i]>=32) result.push_back(char(p[i]));
  }
  return result;
}
const char* stage_name(unsigned id) {
  // Slippi (external) stage ids.
  static constexpr const char* names[]={nullptr,nullptr,"Fountain of Dreams","Pokemon Stadium",
    "Princess Peach's Castle","Kongo Jungle","Brinstar","Corneria","Yoshi's Story","Onett","Mute City",
    "Rainbow Cruise","Jungle Japes","Great Bay","Hyrule Temple","Brinstar Depths","Yoshi's Island",
    "Green Greens","Fourside","Mushroom Kingdom I","Mushroom Kingdom II",nullptr,"Venom","Poke Floats",
    "Big Blue","Icicle Mountain","Icetop","Flat Zone","Dream Land","Yoshi's Island N64","Kongo Jungle N64",
    "Battlefield","Final Destination","Target Test (Mario)","Target Test (Captain Falcon)",
    "Target Test (Young Link)","Target Test (Donkey Kong)","Target Test (Dr. Mario)","Target Test (Falco)",
    "Target Test (Fox)","Target Test (Ice Climbers)","Target Test (Kirby)","Target Test (Bowser)",
    "Target Test (Link)","Target Test (Luigi)","Target Test (Marth)","Target Test (Mewtwo)",
    "Target Test (Ness)","Target Test (Peach)","Target Test (Pichu)","Target Test (Pikachu)",
    "Target Test (Jigglypuff)","Target Test (Samus)","Target Test (Sheik)","Target Test (Yoshi)",
    "Target Test (Zelda)","Target Test (Mr. Game & Watch)","Target Test (Roy)","Target Test (Ganondorf)"};
  if(id==84) return "Home-Run Contest";
  return id<sizeof(names)/sizeof(names[0]) && names[id]?names[id]:"Melee stage";
}
#include "launcher_replay_stats.inl"
void merge_metadata(Info& info,const nlohmann::json& metadata) {
  if(!metadata.is_object()) return;
  if(metadata.find("startAt")!=metadata.end() && metadata["startAt"].is_string()) info.start_at=metadata["startAt"].get<std::string>();
  if(metadata.find("lastFrame")!=metadata.end() && metadata["lastFrame"].is_number_integer()) info.last_frame=metadata["lastFrame"].get<int>();
  if(metadata.find("playedOn")!=metadata.end() && metadata["playedOn"].is_string()) {
    const auto on=metadata["playedOn"].get<std::string>();
    info.played_on=on=="dolphin"?"Dolphin":on=="nintendont"?"Nintendont":on=="network"?"Console":"";
  }
  if(metadata.find("players")==metadata.end() || !metadata["players"].is_object()) return;
  for(auto& player:info.players) {
    auto key=std::to_string(player.port-1);
    auto it=metadata["players"].find(key);
    if(it==metadata["players"].end() || !it->is_object()) continue;
    auto names=it->value("names",nlohmann::json::object());
    if(!names.is_object()) continue;
    auto netplay=names.value("netplay",std::string());
    auto code=names.value("code",std::string());
    if(!netplay.empty()) player.name=netplay;
    if(!code.empty()) player.code=code;
  }
}
}
const char* character_name(int id) { return id>=0 && id<26?characters[id]:"Unknown character"; }
const char* move_name(int id) {
  switch(id) {
    case -1:return "Self Destruct"; case 1:return "Miscellaneous"; case 2:case 3:case 4:return "Jab";
    case 5:return "Rapid Jabs"; case 6:return "Dash Attack"; case 7:return "Forward Tilt"; case 8:return "Up Tilt";
    case 9:return "Down Tilt"; case 10:return "Forward Smash"; case 11:return "Up Smash"; case 12:return "Down Smash";
    case 13:return "Neutral Air"; case 14:return "Forward Air"; case 15:return "Back Air"; case 16:return "Up Air";
    case 17:return "Down Air"; case 18:return "Neutral B"; case 19:return "Side B"; case 20:return "Up B";
    case 21:return "Down B"; case 50:return "Getup Attack"; case 51:return "Getup Attack (Slow)";
    case 52:return "Grab Pummel"; case 53:return "Forward Throw"; case 54:return "Back Throw";
    case 55:return "Up Throw"; case 56:return "Down Throw"; case 61:return "Edge Attack (Slow)";
    case 62:return "Edge Attack"; default:return "Unknown Move";
  }
}
Info inspect(const std::filesystem::path& path,bool with_stats) {
  Info info;info.path=path;
  std::ifstream file(path,std::ios::binary|std::ios::ate);
  if(!file) return info;
  const auto total=file.tellg();
  if(total<15 || total>std::streamoff(512u*1024u*1024u)) return info;
  file.seekg(0); std::array<uint8_t,15> header{};file.read((char*)header.data(),header.size());
  if(!file || header[0]!='{' || header[1]!='U' || header[3]!='r' || header[6]!='[' ||
     header[10]!='l') return info;
  // A recording the game never finished (closed or crashed mid-match) keeps length 0: its events
  // run to the end of the file, with no metadata after them.
  uint32_t raw_size=be32(header.data()+11);
  if(raw_size==0) raw_size=uint32_t(std::min<uint64_t>(uint64_t(total)-15u,0xFFFFFFFFu));
  if(raw_size<2 || uint64_t(raw_size)+15>uint64_t(total)) return info;
  const size_t read_size=with_stats?raw_size:std::min<uint32_t>(raw_size,4096);
  std::vector<uint8_t> raw(read_size);
  file.read((char*)raw.data(),read_size);
  if(!file || raw[0]!=0x35) return info;
  const size_t table_length=raw[1], first_event=1+table_length;
  if(table_length<4 || (table_length-1)%3 || first_event>raw.size()) return info;
  std::array<unsigned,256> sizes{};
  for(size_t i=2;i+2<first_event;i+=3) sizes[raw[i]]=be16(raw.data()+i+1);
  std::vector<FrameData> frames;
  // A recording's frames arrive in order (a rollback steps back a few), so a frame far past the ones
  // read so far is a damaged file: skipped, so a file cannot make the list allocate for frames it
  // does not hold.
  constexpr int64_t kMaxFrames=8*60*60*60, kMaxJump=60*60;
  // Every frame of a recording has at least one frame event, so the list is also held to the frame
  // events the file has room for (plus one jump): a small file cannot walk it up to the cap a jump
  // at a time.
  size_t frame_event=0;
  if(sizes[0x37]+1u>=0x3bu) frame_event=sizes[0x37]+1u;
  if(sizes[0x38]+1u>0x26u && (!frame_event || sizes[0x38]+1u<frame_event)) frame_event=sizes[0x38]+1u;
  const int64_t max_slots=frame_event?
    std::min<int64_t>(kMaxFrames,int64_t((raw.size()-first_event)/frame_event)+kMaxJump):0;
  auto slot=[&](const uint8_t* event)->FrameSlot* {
    const int64_t index=int64_t(int32_t(be32(event+1)))-first_frame;
    if(index<0 || index>=max_slots || index>int64_t(frames.size())+kMaxJump) return nullptr;
    if(size_t(index)>=frames.size())
      frames.resize(size_t(std::min<int64_t>(max_slots,std::max<int64_t>(index+1,int64_t(frames.size())*2))));
    return &frames[size_t(index)][event[5]];
  };
  for(size_t i=first_event;i<raw.size();) {
    const uint8_t command=raw[i]; const unsigned payload=sizes[command];
    if(!payload || i+1+payload>raw.size()) break;
    const uint8_t* event=raw.data()+i;
    const size_t length=1+payload;
    if(command==0x36 && length>=0x241) {
      info.valid=true; info.version=std::to_string(event[1])+"."+std::to_string(event[2])+"."+std::to_string(event[3]);
      info.stage_id=int(be16(event+0x13)); info.stage=stage_name(info.stage_id);
      for(int p=0;p<4;++p) {
        if(event[0x66+p*0x24]==3) continue;
        Player player;player.port=p+1; player.character=event[0x65+p*0x24];
        player.start_stocks=event[0x67+p*0x24]; player.costume=event[0x68+p*0x24];
        player.name=game_text(event+0x1a5+p*0x1f,0x1f);
        // Older recordings end before the connect codes (the fourth player's ends at 0x249).
        if(length>=size_t(0x221+(p+1)*0xa)) player.code=game_code(event+0x221+p*0xa,0xa);
        if(player.name.empty()) player.name=player.code.empty()?"Player "+std::to_string(p+1):player.code;
        info.players.push_back(player);
      }
    } else if(command==0x37 && with_stats && length>=0x3b && event[5]<4 && event[6]==0) {
      if(auto* target=slot(event))
        target->pre={true,befloat(event+0x19),befloat(event+0x1d),befloat(event+0x21),befloat(event+0x25),
                     befloat(event+0x33),befloat(event+0x37),be16(event+0x31)};
    } else if(command==0x38 && with_stats && length>0x26 && event[5]<4 && event[6]==0) {
      const int frame=int32_t(be32(event+1));
      if(auto* target=slot(event)) {
        target->post={true,int(be16(event+8)),event[7],event[0x21],event[0x1e],length>0x33?event[0x33]:0,
                      befloat(event+0x16),befloat(event+0x22)};
        info.last_frame=std::max(info.last_frame,frame);
      }
    }
    i+=length;
  }
  if(with_stats) compute_stats(info,frames);
  else if(info.valid && sizes[0x39]>=6 && raw_size>sizes[0x39]+1u) {
    // Quick winner from the game-end placements (3.13+), without reading any frames.
    std::vector<uint8_t> end(sizes[0x39]+1u);
    file.clear();file.seekg(15u+raw_size-end.size());
    file.read((char*)end.data(),end.size());
    if(file && end[0]==0x39 && end[1]!=7)
      for(size_t p=0;p<info.players.size();++p)
        if(int8_t(end[3+info.players[p].port-1])==0) {info.winner=int(p);break;}
  }
  info.stats_loaded=with_stats;
  info.l_cancel_available=with_stats && sizes[0x38]>=0x34 && info.players.size()==2;
  // Metadata is after raw bytes. This reads only the small UBJSON tail, not all frames.
  auto tail_size=uint64_t(total)-15u-raw_size;
  if(tail_size>0 && tail_size<1024u*1024u) {
    file.clear();file.seekg(15u+raw_size);
    std::vector<uint8_t> tail(size_t(tail_size)+1);tail[0]='{';
    file.read((char*)tail.data()+1,tail_size);
    if(file) {
      auto object=nlohmann::json::from_ubjson(tail,false,false);
      if(object.is_object() && object.find("metadata")!=object.end()) merge_metadata(info,object["metadata"]);
    }
  }
  return info;
}
std::string display_date(const Info& info) {
  int y=0,m=0,d=0,hh=0,mm=0,ss=0;
  if(std::sscanf(info.start_at.c_str(),"%4d-%2d-%2dT%2d:%2d:%2d",&y,&m,&d,&hh,&mm,&ss)==6) {
    std::tm utc{};utc.tm_year=y-1900;utc.tm_mon=m-1;utc.tm_mday=d;utc.tm_hour=hh;utc.tm_min=mm;utc.tm_sec=ss;
    const auto stamp=_mkgmtime(&utc);std::tm local{};
    if(stamp!=-1 && localtime_s(&local,&stamp)==0) {
      char buffer[64]{};std::strftime(buffer,sizeof buffer,"%b %d, %Y  %I:%M %p",&local);return buffer;
    }
  }
  const auto stem=info.path.stem().string();
  if(std::sscanf(stem.c_str(),"Game_%4d%2d%2dT%2d%2d%2d",&y,&m,&d,&hh,&mm,&ss)==6) {
    std::tm local{};local.tm_year=y-1900;local.tm_mon=m-1;local.tm_mday=d;
    local.tm_hour=hh;local.tm_min=mm;local.tm_sec=ss;
    char buffer[64]{};std::strftime(buffer,sizeof buffer,"%b %d, %Y  %I:%M %p",&local);return buffer;
  }
  std::error_code ec;auto changed=std::filesystem::last_write_time(info.path,ec);
  if(!ec) {
    auto system=std::chrono::time_point_cast<std::chrono::system_clock::duration>(changed-
      std::filesystem::file_time_type::clock::now()+std::chrono::system_clock::now());
    auto stamp=std::chrono::system_clock::to_time_t(system);std::tm local{};
    if(localtime_s(&local,&stamp)==0) {char buffer[64]{};std::strftime(buffer,sizeof buffer,"%b %d, %Y  %I:%M %p",&local);return buffer;}
  }
  return "Date unavailable";
}
}
