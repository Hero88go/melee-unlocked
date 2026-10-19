// Deterministic controller scripts shared with existing port/scripts inputs.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mu_host.h"
#include <cstdint>
#include <string>
#include <vector>

namespace linux_host {
class InputScript {
public:
  bool load(const std::string& path,std::string* error);
  bool active() const { return !entries.empty(); }
  void clear();
  void mark_match_start(uint32_t retrace);
  bool apply(uint32_t retrace,const MuStatePod& state,MuPadStatus out[4]);
private:
  enum class Section { Absolute,Scene,Match,MatchRetrace };
  struct Entry { uint32_t frame; unsigned port; Section section; MuPadStatus pad; };
  std::vector<Entry> entries;
  unsigned ports=1;
  bool has_scene=false,has_minor=false,scene_seen=false,match_marked=false,has_relative=false;
  uint32_t scene_major=0,scene_minor=0,scene_start=0,match_start=0;
  uint32_t loop=0,release_after=0,release_at=0,last_match_frame=0,match_retrace_start=0;
  bool has_match_retrace=false;
};
} // namespace linux_host
