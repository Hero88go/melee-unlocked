#include "launcher_replay_data.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

using Bytes=std::vector<uint8_t>;
void put32(Bytes& out,uint32_t value) {for(int shift=24;shift>=0;shift-=8) out.push_back(uint8_t(value>>shift));}
void put16(Bytes& out,uint16_t value) {out.push_back(uint8_t(value>>8));out.push_back(uint8_t(value));}
void post(Bytes& raw,int frame,uint16_t action,uint8_t status,uint8_t player=0) {
  Bytes event(85,0);event[0]=0x38;event[1]=uint8_t(frame>>24);event[2]=uint8_t(frame>>16);
  event[3]=uint8_t(frame>>8);event[4]=uint8_t(frame);event[5]=player;event[6]=0;event[7]=player?18:1;
  event[8]=uint8_t(action>>8);event[9]=uint8_t(action);event[0x21]=3;event[0x33]=status;
  raw.insert(raw.end(),event.begin(),event.end());
}
void key(Bytes& out,const char* value) {out.push_back('U');out.push_back(uint8_t(std::strlen(value)));out.insert(out.end(),value,value+std::strlen(value));}
using launcher::replay::Info;
using launcher::replay::Opening;
std::string clock_text(int frame) {
  if(frame<0) return "-";
  char buffer[16];std::snprintf(buffer,sizeof buffer,"%d:%02d",frame/3600,frame/60%60);return buffer;
}
const char* direction_text(int d) { return d==0?"down":d==1?"left":d==2?"right":d==3?"up":"-"; }
const char* opening_text(Opening o) { return o==Opening::Neutral?"Neutral":o==Opening::CounterHit?"Counter Hit":"Trade"; }
std::string fixed(double value,int digits) { char buffer[32];std::snprintf(buffer,sizeof buffer,"%.*f",digits,value);return buffer; }
std::string percent_text(int a,int b) { return b?std::to_string(int(std::floor(100.0*a/b+0.5)))+"%":"-"; }
std::string ratio_text(int a,int b) { return b?fixed(double(a)/b,1):"-"; }
// Rows in the Slippi Launcher's layout, using the frame convention of the API (0 = first frame).
std::vector<std::string> stock_rows(const Info& info,int victim) {
  std::vector<std::string> rows;
  for(const auto& s:info.players[victim].stock_list) {
    std::string row=(s.start_frame==0?std::string("-"):clock_text(s.start_frame))+" "+clock_text(s.end_frame)+" ";
    row+=s.direction<0?std::string("- -"):std::string(launcher::replay::move_name(s.kill_move))+" "+direction_text(s.direction);
    row+=" "+std::to_string(int(s.percent))+"%";
    rows.push_back(row);
  }
  return rows;
}
std::vector<std::string> opening_rows(const Info& info,int player) {
  std::vector<std::string> rows;
  for(const auto& c:info.players[player].conversions)
    rows.push_back(clock_text(c.start_frame)+"-"+clock_text(c.end_frame)+" "+std::to_string(int(c.end_percent-c.start_percent))+"% ("+
      std::to_string(int(c.start_percent))+"-"+std::to_string(int(c.end_percent))+"%) "+std::to_string(c.moves)+" "+opening_text(c.opening));
  return rows;
}
std::vector<std::string> summary(const Info& info) {
  std::vector<std::string> out;
  const auto& a=info.players[0];const auto& b=info.players[1];
  const double minutes=(info.last_frame+39)/3600.0;  // Slippi: frames since the first playable frame (-39)
  auto pair=[&](const char* label,const std::string& x,const std::string& y){out.push_back(std::string(label)+" "+x+" | "+y);};
  auto conversion=[](const launcher::replay::Player& p){return (p.openings?fixed(100.0*p.successful_conversions/p.openings,1):std::string("-"))+
    "% ("+std::to_string(p.successful_conversions)+"/"+std::to_string(p.openings)+")";};
  pair("kills",std::to_string(a.kills),std::to_string(b.kills));
  pair("damage",fixed(a.damage_done,1),fixed(b.damage_done,1));
  pair("conversion",conversion(a),conversion(b));
  pair("openings/kill",ratio_text(a.openings,a.kills),ratio_text(b.openings,b.kills));
  pair("damage/opening",a.openings?fixed(a.damage_done/a.openings,1):"-",b.openings?fixed(b.damage_done/b.openings,1):"-");
  pair("roll/air/spot",std::to_string(a.rolls)+"/"+std::to_string(a.air_dodges)+"/"+std::to_string(a.spot_dodges),
       std::to_string(b.rolls)+"/"+std::to_string(b.air_dodges)+"/"+std::to_string(b.spot_dodges));
  pair("neutral",std::to_string(a.neutral_wins)+" ("+percent_text(a.neutral_wins,a.neutral_wins+b.neutral_wins)+")",
       std::to_string(b.neutral_wins)+" ("+percent_text(b.neutral_wins,a.neutral_wins+b.neutral_wins)+")");
  pair("counter",std::to_string(a.counter_hits)+" ("+percent_text(a.counter_hits,a.counter_hits+b.counter_hits)+")",
       std::to_string(b.counter_hits)+" ("+percent_text(b.counter_hits,a.counter_hits+b.counter_hits)+")");
  pair("beneficial",std::to_string(a.beneficial_trades)+" ("+percent_text(a.beneficial_trades,a.trades)+")",
       std::to_string(b.beneficial_trades)+" ("+percent_text(b.beneficial_trades,b.trades)+")");
  pair("wd/wl/dd/ledge",std::to_string(a.wavedashes)+"/"+std::to_string(a.wavelands)+"/"+std::to_string(a.dash_dances)+"/"+std::to_string(a.ledge_grabs),
       std::to_string(b.wavedashes)+"/"+std::to_string(b.wavelands)+"/"+std::to_string(b.dash_dances)+"/"+std::to_string(b.ledge_grabs));
  pair("inputs/min",fixed(a.inputs/minutes,1),fixed(b.inputs/minutes,1));
  pair("digital/min",fixed(a.digital_inputs/minutes,1),fixed(b.digital_inputs/minutes,1));
  pair("lcancel",percent_text(a.l_success,a.l_success+a.l_fail)+" ("+std::to_string(a.l_success)+"/"+std::to_string(a.l_success+a.l_fail)+")",
       percent_text(b.l_success,b.l_success+b.l_fail)+" ("+std::to_string(b.l_success)+"/"+std::to_string(b.l_success+b.l_fail)+")");
  return out;
}
void dump(const Info& info) {
  std::cout<<info.stage<<" ("<<info.stage_id<<") "<<info.played_on<<" last_frame="<<info.last_frame<<" winner="<<info.winner<<"\n";
  for(const auto& p:info.players) std::cout<<"  P"<<p.port<<" "<<p.name<<" "<<p.code<<" "<<launcher::replay::character_name(p.character)
    <<" costume "<<p.costume<<" stocks "<<p.stocks<<" inputs "<<p.inputs<<" digital "<<p.digital_inputs<<"\n";
  for(const auto& line:summary(info)) std::cout<<"  "<<line<<"\n";
  for(int p=0;p<2;++p) {
    std::cout<<"  kills by "<<info.players[p].name<<" (opponent stocks):\n";
    for(const auto& row:stock_rows(info,1-p)) std::cout<<"    "<<row<<"\n";
    std::cout<<"  openings by "<<info.players[p].name<<":\n";
    for(const auto& row:opening_rows(info,p)) std::cout<<"    "<<row<<"\n";
  }
}
// Ground truth: the Slippi Launcher's own stats screen for this replay.
int ground_truth() {
  const std::filesystem::path path="C:/Users/Chandler/Documents/Slippi/2025-11/Game_20251115T000848.slp";
  std::error_code ec;
  if(!std::filesystem::exists(path,ec)) {std::cout<<"ground-truth replay not found, skipped\n";return 0;}
  const auto begin=std::chrono::steady_clock::now();
  auto info=launcher::replay::inspect(path,true);
  const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
  std::cout<<"ground truth parsed in "<<fixed(ms,1)<<" ms\n";
  if(!info.valid || info.players.size()!=2) return 20;
  dump(info);
  int failures=0;
  auto expect=[&](const std::vector<std::string>& got,const std::vector<std::string>& want,const char* what) {
    for(size_t i=0;i<want.size();++i) {
      const std::string actual=i<got.size()?got[i]:"<missing>";
      if(actual!=want[i]) {std::cout<<"MISMATCH "<<what<<": got '"<<actual<<"' want '"<<want[i]<<"'\n";++failures;}
    }
  };
  expect(summary(info),{"kills 2 | 1","damage 296.9 | 171.8","conversion 87.5% (7/8) | 45.5% (5/11)",
    "openings/kill 4.0 | 11.0","damage/opening 37.1 | 15.6","roll/air/spot 0/2/1 | 2/3/0","neutral 4 (67%) | 2 (33%)",
    "counter 3 (27%) | 8 (73%)","beneficial 1 (100%) | 0 (0%)","wd/wl/dd/ledge 15/1/5/1 | 0/0/14/0",
    "inputs/min 328.0 | 432.3","digital/min 134.5 | 166.2","lcancel 13% (1/8) | 14% (1/7)"},"summary");
  expect(stock_rows(info,1),{"- 0:17 Self Destruct down 77%","0:18 0:38 Forward Air left 76%","0:39 0:57 Self Destruct down 19%",
    "0:58 1:24 Forward Air right 124%"},"hero stocks");
  expect(stock_rows(info,0),{"- 0:16 Self Destruct down 34%","0:17 0:40 Self Destruct down 18%","0:41 0:53 Forward Air right 72%",
    "0:54 - - - 51%"},"jjashya stocks");
  expect(opening_rows(info,0),{"0:02-0:04 14% (0-14%) 2 Neutral","0:07-0:17 63% (14-77%) 6 Neutral","0:22-0:25 10% (0-10%) 1 Neutral",
    "0:27-0:30 29% (10-39%) 3 Counter Hit","0:32-0:38 37% (39-76%) 4 Neutral"},"jjashya openings");
  expect(opening_rows(info,1),{"0:03-0:06 32% (0-32%) 2 Counter Hit","0:07-0:08 2% (32-34%) 1 Counter Hit",
    "0:25-0:27 9% (0-9%) 1 Counter Hit","0:27-0:29 2% (9-11%) 1 Counter Hit","0:30-0:32 7% (11-18%) 1 Counter Hit"},"hero openings");
  if(info.winner!=0) {std::cout<<"MISMATCH winner "<<info.winner<<"\n";++failures;}
  if(info.stage!="Yoshi's Story" || info.stage_id!=8 || (info.last_frame+123)/60!=84) {std::cout<<"MISMATCH stage/length\n";++failures;}
  if(ms>100) {std::cout<<"too slow\n";++failures;}
  auto quick=launcher::replay::inspect(path,false);
  if(quick.stage_id!=8 || quick.players.size()!=2) {std::cout<<"MISMATCH quick inspect\n";++failures;}
  std::cout<<"quick: winner="<<quick.winner<<" played_on="<<quick.played_on<<"\n";
  return failures?21:0;
}
int main(int argc,char** argv) {
  Bytes raw{0x35,7,0x36,3,0,0x38,0,84};
  Bytes start(769,0);start[0]=0x36;start[1]=3;start[2]=16;start[0x13]=0;start[0x14]=0x1f;
  for(int p=0;p<4;++p) start[0x66+p*0x24]=p<2?0:3;
  start[0x65]=2;start[0x65+0x24]=9;start[0x67]=4;start[0x67+0x24]=4;
  std::memcpy(start.data()+0x1a5,"Fox player",10);
  std::memcpy(start.data()+0x1a5+0x1f,"Marth player",12);
  std::memcpy(start.data()+0x221,"FOX",3);
  start[0x224]=0x81;start[0x225]=0x94;
  std::memcpy(start.data()+0x226,"123",3);
  raw.insert(raw.end(),start.begin(),start.end());
  post(raw,0,0x46,2);post(raw,1,0x46,0);post(raw,2,0x1d,0); // edge cancel removes failure
  post(raw,3,0x46,1);post(raw,4,0x0e,0);
  for(int frame=0;frame<=4;++frame) post(raw,frame,0x0e,0,1);
  Bytes file{'{','U',3,'r','a','w','[','$','U','#','l'};put32(file,uint32_t(raw.size()));
  file.insert(file.end(),raw.begin(),raw.end());
  key(file,"metadata");file.push_back('{');key(file,"startAt");file.push_back('S');key(file,"2026-09-27T03:17:02Z");
  key(file,"lastFrame");file.push_back('l');put32(file,4);file.push_back('}');file.push_back('}');
  const auto path=std::filesystem::current_path()/"launcher_replay_data_test.slp";
  {std::ofstream out(path,std::ios::binary);out.write((const char*)file.data(),file.size());}
  auto info=launcher::replay::inspect(path,true);
  std::filesystem::remove(path);
  if(!info.valid || info.players.size()!=2 || info.stage!="Battlefield") return 1;
  if(info.players[0].name!="Fox player" || info.players[1].name!="Marth player") return 2;
  if(info.players[0].code!="FOX#123") return 6;
  if(!info.l_cancel_available || info.players[0].l_success!=1 || info.players[0].l_fail!=0) return 3;
  if(info.start_at!="2026-09-27T03:17:02Z" || launcher::replay::display_date(info).find("2026")==std::string::npos) return 4;
  if(launcher::replay::move_name(14)!=std::string("Forward Air") || launcher::replay::move_name(-1)!=std::string("Self Destruct")) return 7;
  auto inspect_raw=[&](const Bytes& events) {
    Bytes slp{'{','U',3,'r','a','w','[','$','U','#','l'};put32(slp,uint32_t(events.size()));
    slp.insert(slp.end(),events.begin(),events.end());
    {std::ofstream out(path,std::ios::binary);out.write((const char*)slp.data(),slp.size());}
    auto result=launcher::replay::inspect(path,true);
    std::filesystem::remove(path);
    return result;
  };
  // A Game Start of 0x241 bytes with four players: the names and the first three connect codes fit,
  // the fourth code (to 0x249) is not read, so nothing past the event is touched.
  {
    Bytes events{0x35,7,0x36,0x02,0x40,0x38,0,84};
    Bytes short_start(start.begin(),start.begin()+0x241);
    for(int p=0;p<4;++p) short_start[0x66+p*0x24]=0;
    short_start[0x23f]='A';short_start[0x240]='B';   // the start of a fourth code the event cuts off
    events.insert(events.end(),short_start.begin(),short_start.end());
    auto old=inspect_raw(events);
    if(!old.valid || old.players.size()!=4 || old.players[0].name!="Fox player" || old.players[0].code!="FOX#123" ||
       !old.players[3].code.empty()) return 8;
  }
  // A frame far past the ones read so far is skipped instead of growing the frame list to reach it.
  {
    Bytes events=raw;
    post(events,200000,0x0e,0);
    auto sparse=inspect_raw(events);
    if(!sparse.valid || sparse.last_frame!=4) return 9;
  }
  // A small file that walks its frame numbers up one allowed jump at a time: the frame list stays
  // within the frame events the file has room for plus one jump, so only the first step is taken
  // (before the bound, all forty were, and the list doubled its way to 120,000 frames and beyond).
  {
    Bytes events=raw;
    for(int step=1;step<=40;++step) post(events,3000*step,0x0e,0);
    auto walk=inspect_raw(events);
    if(!walk.valid || walk.last_frame!=3000) return 10;
    if(walk.players[0].l_success!=1 || walk.players[0].l_fail!=0) return 11;
  }
  // A recording of ordinary shape, longer than several jumps: every frame is kept and the same
  // landing sequence as above, placed in its last five frames, gives the same statistics.
  {
    const int count=9000;
    Bytes events{0x35,7,0x36,3,0,0x38,0,84};
    events.insert(events.end(),start.begin(),start.end());
    const uint16_t actions[5]={0x46,0x46,0x1d,0x46,0x0e};
    const uint8_t statuses[5]={2,0,0,1,0};
    for(int i=0;i<count;++i) {
      const int tail=i-(count-5);
      post(events,-123+i,tail>=0?actions[tail]:uint16_t(0x0e),tail>=0?statuses[tail]:uint8_t(0));
      post(events,-123+i,0x0e,0,1);
    }
    auto whole=inspect_raw(events);
    if(!whole.valid || whole.players.size()!=2 || whole.last_frame!=-123+count-1) return 12;
    for(int p=0;p<2;++p) {
      const auto& a=whole.players[p];const auto& b=info.players[p];
      if(a.l_success!=b.l_success || a.l_fail!=b.l_fail || a.kills!=b.kills || a.openings!=b.openings ||
         a.rolls!=b.rolls || a.stocks!=b.stocks) return 13;
    }
  }
  if(argc>1) {
    auto actual=launcher::replay::inspect(argv[1],true);
    if(!actual.valid) return 5;
    if(actual.players.size()==2) dump(actual);
    auto quick=launcher::replay::inspect(argv[1],false);
    std::cout<<"quick winner="<<quick.winner<<" played_on="<<quick.played_on<<"\n";
    return 0;
  }
  if(int result=ground_truth()) return result;
  std::cout<<"replay metadata and Slippi edge-cancel L-cancel test passed\n";
}
