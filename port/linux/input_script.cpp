// Parse the original Windows host's script format without Windows input code.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "input_script.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace linux_host {
namespace {
long long number(const std::string& text,long long low,long long high) {
  if (text.empty()) throw std::runtime_error("missing number");
  char* end=nullptr;
  errno=0;
  const long long value=std::strtoll(text.c_str(),&end,0);
  if (errno || end==text.c_str() || *end || value<low || value>high)
    throw std::runtime_error("invalid value: "+text);
  return value;
}
}
void InputScript::clear() { *this=InputScript{}; }
void InputScript::mark_match_start(uint32_t retrace) { match_start=retrace; match_marked=true; }
bool InputScript::load(const std::string& path,std::string* error) {
  clear();
  std::ifstream in(path);
  if (!in) { if (error) *error="cannot open input script: "+path; return false; }
  std::string line;
  unsigned line_number=0;
  Section section=Section::Absolute;
  try {
    while (std::getline(in,line)) {
      ++line_number;
      const auto comment=line.find('#');
      if (comment!=std::string::npos) line.resize(comment);
      std::replace(line.begin(),line.end(),'+',' ');
      std::istringstream words(line);
      std::string first;
      if (!(words>>first)) continue;
      if (first[0]=='@') {
        std::string value,extra;
        if (first=="@match") { section=Section::Match; has_relative=true; }
        else if (first=="@matchrt") { section=Section::MatchRetrace; has_relative=true; }
        else if (first=="@scene") {
          if (has_scene || !(words>>value)) throw std::runtime_error("one @scene major[:minor] is supported");
          const auto colon=value.find(':');
          scene_major=uint32_t(number(value.substr(0,colon),0,UINT32_MAX));
          has_minor=colon!=std::string::npos;
          if (has_minor) scene_minor=uint32_t(number(value.substr(colon+1),0,UINT32_MAX));
          has_scene=true; has_relative=true; section=Section::Scene;
        } else if (first=="@loop" || first=="@release") {
          if (!(words>>value)) throw std::runtime_error("missing directive interval");
          const uint32_t frames=uint32_t(number(value,1,UINT32_MAX));
          if (first=="@loop") loop=frames;
          else release_after=frames;
        } else throw std::runtime_error("unsupported directive: "+first);
        if (words>>extra) throw std::runtime_error("unexpected directive argument: "+extra);
        continue;
      }
      Entry entry{uint32_t(number(first,0,UINT32_MAX)),0,section,{}};
      entry.pad.err=0;
      std::string token;
      while (words>>token) {
        auto& pad=entry.pad;
        if (token=="A") pad.button|=0x0100;
        else if (token=="B") pad.button|=0x0200;
        else if (token=="X") pad.button|=0x0400;
        else if (token=="Y") pad.button|=0x0800;
        else if (token=="START") pad.button|=0x1000;
        else if (token=="Z") pad.button|=0x0010;
        else if (token=="L") pad.button|=0x0040;
        else if (token=="R") pad.button|=0x0020;
        else if (token=="DL") pad.button|=0x0001;
        else if (token=="DR") pad.button|=0x0002;
        else if (token=="DD") pad.button|=0x0004;
        else if (token=="DU") pad.button|=0x0008;
        else {
          const auto equals=token.find('=');
          if (equals==std::string::npos) throw std::runtime_error("unknown input token: "+token);
          const std::string key=token.substr(0,equals),value=token.substr(equals+1);
          if (key=="p") entry.port=unsigned(number(value,1,4)-1);
          else if (key=="sx") pad.stick_x=int8_t(number(value,-128,127));
          else if (key=="sy") pad.stick_y=int8_t(number(value,-128,127));
          else if (key=="cx") pad.sub_x=int8_t(number(value,-128,127));
          else if (key=="cy") pad.sub_y=int8_t(number(value,-128,127));
          else if (key=="l") pad.trigger_l=uint8_t(number(value,0,255));
          else if (key=="r") pad.trigger_r=uint8_t(number(value,0,255));
          else throw std::runtime_error("unknown input axis: "+key);
        }
      }
      if ((entry.pad.button&0x0040) && !entry.pad.trigger_l) entry.pad.trigger_l=255;
      if ((entry.pad.button&0x0020) && !entry.pad.trigger_r) entry.pad.trigger_r=255;
      ports|=1u<<entry.port;
      entries.push_back(entry);
    }
    if (in.bad()) throw std::runtime_error("input script read failed");
    if (entries.empty()) throw std::runtime_error("input script has no controller entries");
    return true;
  } catch(const std::exception& exception) {
    if (error) *error=path+":"+std::to_string(line_number)+": "+exception.what();
    clear();
    return false;
  }
}

bool InputScript::apply(uint32_t retrace,const MuStatePod& state,MuPadStatus out[4]) {
  if (!active()) return false;
  if (release_after && state.match_frame>1 && !release_at) release_at=retrace+release_after;
  if (release_at && retrace>=release_at) { clear(); return false; }
  if (has_scene && !scene_seen && state.scene_major==scene_major && (!has_minor || state.scene==scene_minor)) {
    scene_seen=true; scene_start=retrace;
  }
  if (!state.match_frame) has_match_retrace=false;
  else if (!has_match_retrace || state.match_frame<last_match_frame) {
    has_match_retrace=true; match_retrace_start=retrace;
  }
  last_match_frame=state.match_frame;
  const bool started=has_relative && (has_scene ? scene_seen : match_marked);
  const uint32_t start=has_scene ? scene_start : match_start;
  uint32_t match_relative=has_scene ? state.match_frame : (started ? retrace-start : 0);
  if (loop) match_relative%=loop;
  for (unsigned port=0;port<4;++port) {
    out[port]={}; out[port].err=(ports&(1u<<port)) ? 0 : -1;
    for (const auto& entry : entries) {
      if (entry.port!=port) continue;
      bool eligible=false;
      switch (entry.section) {
        case Section::Absolute: eligible=!started && entry.frame<=retrace; break;
        case Section::Scene: eligible=scene_seen && entry.frame<=retrace-scene_start; break;
        case Section::Match: eligible=started && (!has_scene || state.match_frame) && entry.frame<=match_relative; break;
        case Section::MatchRetrace: eligible=has_match_retrace && entry.frame<=retrace-match_retrace_start; break;
      }
      if (eligible) out[port]=entry.pad;
    }
  }
  return true;
}
} // namespace linux_host
