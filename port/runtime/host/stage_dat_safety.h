// Stage gameplay equivalence, independent of DAT offsets and visual mesh layout.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace host::cosmetics::stage_safety {
inline uint32_t word(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
struct Archive {
  const std::vector<uint8_t>& bytes;
  uint32_t size = 0;
  std::set<uint32_t> pointers, starts, joint_offsets, visual_offsets;
  std::map<std::string, uint32_t> roots;
  std::map<uint32_t,uint32_t> typed_sizes;
  std::set<uint32_t> active_animations;
  uint32_t get(uint32_t at) const { return word(bytes.data() + 32ull + at); }
  bool bounds(uint32_t at, uint64_t n) const { return uint64_t(at) + n <= size; }
  bool parse() {
    if (bytes.size() < 32 || bytes.size() > 64ull * 1024 * 1024 || word(bytes.data()) != bytes.size()) return false;
    size = word(bytes.data() + 4);
    const uint64_t nr = word(bytes.data() + 8), np = word(bytes.data() + 12), ne = word(bytes.data() + 16);
    const uint64_t table = 32ull + size + nr * 4, strings = table + (np + ne) * 8;
    // External symbols can replace pointers at load time; an unknown reference is never safe.
    if (!np || np > 1024 || ne || nr > size / 4 || strings > bytes.size()) return false;
    starts.insert(size);
    for (uint64_t i = 0; i < nr; ++i) {
      const uint32_t at = word(bytes.data() + 32ull + size + i * 4);
      if ((at & 3) || !bounds(at, 4) || !pointers.insert(at).second || get(at) >= size) return false;
      starts.insert(get(at));
    }
    for (uint64_t i = 0; i < np; ++i) {
      const uint32_t at = word(bytes.data() + table + i * 8);
      const uint64_t name = strings + word(bytes.data() + table + i * 8 + 4);
      if (!bounds(at, 1) || name >= bytes.size()) return false;
      const auto* p = bytes.data() + name;
      const size_t n = strnlen(reinterpret_cast<const char*>(p), bytes.size() - size_t(name));
      if (!n || n > 255 || n == bytes.size() - name) return false;
      for (size_t k = 0; k < n; ++k) if (p[k] < 32 || p[k] > 126) return false;
      if (!roots.emplace(std::string(reinterpret_cast<const char*>(p), n), at).second) return false;
      starts.insert(at);
    }
    if (!roots.count("map_head") || !roots.count("coll_data") || !roots.count("grGroundParam")) return false;
    const uint32_t head = roots.at("map_head");
    if (!bounds(head, 48) || !pointers.count(head + 8) || get(head + 12) > 256) return false;
    const uint32_t models = get(head + 8), count = get(head + 12);
    if (!bounds(models, uint64_t(count) * 52)) return false;
    std::vector<uint32_t> pending;
    for (uint32_t i = 0; i < count; ++i) {
      if (pointers.count(models + i * 52)) pending.push_back(get(models + i * 52));
      for (uint32_t k : {16u,24u,28u}) if (pointers.count(models + i * 52 + k)) visual_offsets.insert(get(models + i * 52 + k));
      for (uint32_t k : {4u,8u}) {
        const uint32_t slot = models+i*52+k;
        if (!pointers.count(slot)) continue;
        uint32_t table = get(slot);
        for (uint32_t j=0; j<256; ++j,table+=4) {
          if (!bounds(table,4)) return false;
          if (!get(table)) break;
          if (!pointers.count(table) || !mark_animation(get(table), k==4 ? 0 : 1)) return false;
          if (j==255) return false;
        }
      }
    }
    while (!pending.empty()) {
      const uint32_t at = pending.back(); pending.pop_back();
      if (!bounds(at, 64) || joint_offsets.size() > 16384) return false;
      if (!joint_offsets.insert(at).second) continue;
      for (uint32_t k : {8u, 12u}) if (pointers.count(at + k)) pending.push_back(get(at + k));
    }
    pending.assign(visual_offsets.begin(), visual_offsets.end());
    while (!pending.empty()) {
      const uint32_t at = pending.back(); pending.pop_back();
      const uint32_t n = extent(at);
      for (auto p = pointers.lower_bound(at); p != pointers.end() && *p < uint64_t(at) + n; ++p) {
        const uint32_t to = get(*p);
        if (!joint_offsets.count(to) && to != roots.at("coll_data") && to != roots.at("grGroundParam") &&
            visual_offsets.insert(to).second) pending.push_back(to);
      }
      if (visual_offsets.size() > 100000) return false;
    }
    return true;
  }
  uint32_t extent(uint32_t at) const {
    const auto end = starts.upper_bound(at);
    return end == starts.end() ? 0 : *end - at;
  }
  // Canonical file descriptors exclude exporter alignment padding, while every
  // animation frame, bytecode byte and reference retains its exact meaning.
  bool mark_animation(uint32_t at, unsigned type, unsigned depth=0) {
    if (depth>256 || typed_sizes.size()>100000) return false;
    if (active_animations.count(at)) return false;
    const uint32_t n = type==0 ? 20 : type==1 ? 12 : type==2 ? 16 : type==3 ? 20 : 16;
    if (!bounds(at,n)) return false;
    auto old=typed_sizes.emplace(at,n);
    if (!old.second) return old.first->second==n;
    active_animations.insert(at);
    struct Pop { std::set<uint32_t>& set; uint32_t at; ~Pop() { set.erase(at); } } pop{active_animations,at};
    const auto follow=[&](uint32_t k,unsigned t) { return !pointers.count(at+k) || mark_animation(get(at+k),t,depth+1); };
    if (type==0) return follow(0,0)&&follow(4,0)&&follow(8,2); // joint / AObj; RObj remains generic
    if (type==1) return follow(0,1)&&follow(4,1)&&follow(8,4); // material joint / MatAnim
    if (type==2) return follow(8,3); // AObj / FObj
    if (type==4) return follow(0,4)&&follow(4,2); // MatAnim; texture/render animations remain generic
    if (!follow(0,3)) return false;
    const uint32_t len=get(at+4);
    if (!len || len>size || !pointers.count(at+16) || !bounds(get(at+16),len)) return false;
    auto data=typed_sizes.emplace(get(at+16),len);
    return data.second || data.first->second==len;
  }
};

struct Compare {
  const Archive& a;
  const Archive& b;
  std::string* reason;
  std::set<std::pair<uint32_t, uint32_t>> visited;
  std::map<uint32_t, uint32_t> forward, backward;
  uint32_t nodes = 0;
  bool allow_visual = true;
  bool fail(const char* why) { *reason = why; return false; }
  // Generic gameplay objects retain every non-pointer byte and their graph of pointers.
  // Boundary/padding changes are refused: no guessed span can conceal a changed script.
  bool object(uint32_t x, uint32_t y, unsigned depth = 0) {
    if (depth > 256 || ++nodes > 200000) return fail("stage gameplay graph exceeds supported bounds");
    if (!a.bounds(x, 1) || !b.bounds(y, 1)) return fail("stage gameplay pointer is out of bounds");
    if (allow_visual && !a.typed_sizes.count(x) && !b.typed_sizes.count(y) &&
        a.visual_offsets.count(x) && b.visual_offsets.count(y)) return true;
    if (a.joint_offsets.count(x) || b.joint_offsets.count(y)) {
      if (!a.joint_offsets.count(x) || !b.joint_offsets.count(y)) return fail("stage joint reference differs");
      return joint(x, y, depth);
    }
    if ((forward.count(x) && forward[x] != y) || (backward.count(y) && backward[y] != x))
      return fail("stage gameplay pointer sharing differs");
    forward[x] = y; backward[y] = x;
    if (!visited.emplace(x, y).second) return true;
    const uint32_t n = a.typed_sizes.count(x) ? a.typed_sizes.at(x) : a.extent(x);
    const uint32_t bn = b.typed_sizes.count(y) ? b.typed_sizes.at(y) : b.extent(y);
    if (!n || n != bn) {
      *reason = "stage gameplay object size differs at " + std::to_string(x) + "/" + std::to_string(y) +
          " (" + std::to_string(n) + "/" + std::to_string(b.extent(y)) + ")";
      return false;
    }
    for (uint32_t k = 0; k < n;) {
      const bool pa = a.pointers.count(x + k), pb = b.pointers.count(y + k);
      if (pa != pb) return fail("stage gameplay pointer layout differs");
      if (pa) {
        if (k + 4 > n || !object(a.get(x + k), b.get(y + k), depth + 1)) return false;
        k += 4;
      } else {
        if (a.bytes[32ull + x + k] != b.bytes[32ull + y + k]) {
          *reason = "stage gameplay data differs at " + std::to_string(x) + "/" + std::to_string(y) + "+" + std::to_string(k);
          return false;
        }
        ++k;
      }
    }
    return true;
  }
  bool field(uint32_t x, uint32_t y) {
    const bool pa = a.pointers.count(x), pb = b.pointers.count(y);
    if (pa != pb) return fail("stage gameplay pointer presence differs");
    return pa ? object(a.get(x), b.get(y)) : a.get(x) == b.get(y) || fail("stage gameplay value differs");
  }
  bool animation_slots(uint32_t x, uint32_t y) {
    const bool pa = a.pointers.count(x), pb = b.pointers.count(y);
    if (pa != pb) return fail("stage animation slots differ");
    if (!pa) return a.get(x) == b.get(y) || fail("stage animation slots differ");
    x = a.get(x); y = b.get(y);
    // NULL-terminated pointer tables may be padded differently by DAT exporters.
    for (uint32_t i = 0; i < 256; ++i, x += 4, y += 4) {
      if (!a.bounds(x,4) || !b.bounds(y,4)) return fail("stage animation slots are truncated");
      if (!a.get(x) || !b.get(y)) return !a.get(x) && !b.get(y) || fail("stage animation count differs");
      if (!field(x,y)) return false;
    }
    return fail("stage animation count exceeds supported bounds");
  }
  std::set<std::pair<uint32_t, uint32_t>> joints, active_joints;
  bool joint(uint32_t x, uint32_t y, unsigned depth = 0) {
    if (depth > 256 || joints.size() > 16384 || !a.bounds(x, 64) || !b.bounds(y, 64))
      return fail("stage joint tree is malformed or too large");
    if (active_joints.count({x,y})) return fail("stage joint tree contains a cycle");
    if ((forward.count(x) && forward[x] != y) || (backward.count(y) && backward[y] != x)) return fail("stage joint mapping differs");
    forward[x] = y; backward[y] = x;
    if (!joints.emplace(x, y).second) return true;
    active_joints.emplace(x,y);
    // Only drawing flags: retain particle/spline/instance and every transform flag.
    // HIDDEN participates in collision activation (mpLib_8005667C); retain it.
    constexpr uint32_t visual = 0x6u | 0x80u | 0x100u | 0x10000u | 0x1C0000u | 0x70000000u;
    if ((a.get(x + 4) & ~visual) != (b.get(y + 4) & ~visual)) {
      *reason="stage joint transform flags differ at "+std::to_string(x)+"/"+std::to_string(y)+" ("+
          std::to_string(a.get(x+4))+"/"+std::to_string(b.get(y+4))+")"; return false;
    }
    if (std::memcmp(a.bytes.data() + 32ull + x + 20, b.bytes.data() + 32ull + y + 20, 36))
      return fail("stage joint transform differs");
    for (uint32_t k : {0u, 56u, 60u}) if (!field(x + k, y + k)) return false;
    // +16 is the render mesh, except on particle/spline joints, whose behavior is retained.
    if ((a.get(x + 4) & (0x20u | 0x4000u)) && !field(x + 16, y + 16)) return false;
    for (uint32_t k : {8u, 12u}) {
      const bool pa = a.pointers.count(x + k), pb = b.pointers.count(y + k);
      if (pa != pb) return fail("stage joint hierarchy differs");
      if (pa ? !joint(a.get(x + k), b.get(y + k), depth + 1) : a.get(x + k) != b.get(y + k))
        return fail("stage joint hierarchy differs");
    }
    active_joints.erase({x,y});
    return true;
  }
  bool map() {
    const uint32_t x = a.roots.at("map_head"), y = b.roots.at("map_head");
    if (!a.bounds(x, 48) || !b.bounds(y, 48)) return fail("stage map header is truncated");
    for (uint32_t k = 0; k < 48; k += 4) {
      if (k == 8 || k == 24 || k == 28 || k == 40 || k == 44) continue;
      // +40/+44 is the material draw-flag override list (grDatFiles_801C6228).
      if (!field(x + k, y + k)) { *reason = "map header +" + std::to_string(k) + ": " + *reason; return false; }
    }
    const uint32_t count = a.get(x + 12);
    if (!count || count > 256 || !a.pointers.count(x + 8) || !b.pointers.count(y + 8))
      return fail("stage map model array is malformed");
    const uint32_t am = a.get(x + 8), bm = b.get(y + 8);
    if (!a.bounds(am, uint64_t(count) * 52) || !b.bounds(bm, uint64_t(count) * 52))
      return fail("stage map model array is truncated");
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t p = am + i * 52, q = bm + i * 52;
      const bool pa = a.pointers.count(p), pb = b.pointers.count(q);
      if (pa != pb) return fail("stage model presence differs");
      if (pa ? !joint(a.get(p), b.get(q)) : a.get(p) != b.get(q)) { *reason="model "+std::to_string(i)+": "+*reason; return false; }
      // Animation completion can consume RNG, even on a background. Preserve every
      // animation, collision binding and unknown field. Camera/light/fog descriptors draw only.
      for (uint32_t k = 4; k < 52; k += 4) {
        if (k == 16 || k == 24 || k == 28) continue;
        if (k == 4 || k == 8 || k == 12) {
          if (!animation_slots(p+k,q+k)) return false;
          continue;
        }
        if (!field(p + k, q + k)) { *reason = "model " + std::to_string(i) + "+" + std::to_string(k) + ": " + *reason; return false; }
      }
    }
    return true;
  }
};
inline bool visual_root(const std::string& name) {
  for (const char* suffix : {"_image", "_image_desc", "_tlut", "_tlut_desc"}) {
    const size_t n = std::strlen(suffix);
    if (name.size() >= n && name.compare(name.size() - n, n, suffix) == 0) return true;
  }
  return name == "map_texg";
}
inline bool matches(const std::vector<uint8_t>& clean, const std::vector<uint8_t>& candidate, std::string* reason) {
  Archive a{clean}, b{candidate};
  if (!a.parse() || !b.parse()) { *reason = "stage DAT gameplay tables are malformed or unsupported"; return false; }
  Compare compare{a, b, reason};
  if (!compare.map()) return false;
  for (const auto& root : a.roots) {
    if (root.first == "map_head" || visual_root(root.first)) continue;
    const auto other = b.roots.find(root.first);
    if (other == b.roots.end()) { *reason = "stage gameplay symbol is missing: " + root.first; return false; }
    Compare gameplay{a,b,reason}; gameplay.allow_visual=false;
    if (!gameplay.object(root.second, other->second)) { *reason = root.first + ": " + *reason; return false; }
  }
  for (const auto& root : b.roots)
    if (!a.roots.count(root.first) && !visual_root(root.first)) { *reason = "unknown stage gameplay symbol: " + root.first; return false; }
  *reason = "gameplay matches";
  return true;
}
} // namespace host::cosmetics::stage_safety
