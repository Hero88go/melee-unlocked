// A stage skin drawn over the original stage's gameplay. The served file is the original archive with
// only the meshes changed: every joint, transform, animation, collision line, gameplay table and
// pointer the game simulates with stays the original's, and each joint shows the skin's mesh (its
// display lists, materials and textures) from the skin's matching joint. A joint the skin removed
// shows nothing; a skin's added scenery is not carried over. The result is checked against the
// original by stage_safety::matches, the same strict gameplay comparison every online stage passes,
// so it can only be used when the simulation provably cannot tell it from the original.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "stage_dat_safety.h"
#include <algorithm>
#include <cctype>
#include <functional>

namespace host::cosmetics::stage_safety {

namespace graft_detail {
inline void put(std::vector<uint8_t>& v, size_t at, uint32_t x) {
  v[at] = uint8_t(x >> 24); v[at + 1] = uint8_t(x >> 16); v[at + 2] = uint8_t(x >> 8); v[at + 3] = uint8_t(x);
}
// Joints carry their behaviour, not a mesh, at +16 when they are particle or spline joints.
constexpr uint32_t kBehaviourJoint = 0x20u | 0x4000u;
// The transform part of a joint (+20..+56: rotation, scale, translation) and its non-drawing flags:
// what pairs a skin joint with an original one when the skin removed some of their siblings.
constexpr uint32_t kDrawFlags = 0x6u | 0x80u | 0x100u | 0x10000u | 0x1C0000u | 0x70000000u;
}  // namespace graft_detail

// Stage models the game only draws: the stage's code starts each one's animation and does nothing
// more with it (no joint looked up by index, no check for the animation's end, which on some stages
// draws a random number, no collision). From the stage code (gr*.c in the decomp). A skin's own
// version of such a model, with the scenery and animations it added, can be drawn whole.
inline std::set<uint32_t> visual_only_models(std::string stage_file) {
  for (char& c : stage_file) c = (char)std::tolower((unsigned char)c);
  const size_t slash = stage_file.find_last_of("/\\");
  if (slash != std::string::npos) stage_file = stage_file.substr(slash + 1);
  // Model 0 is never one: its joints are the stage's points (spawns, camera and blast lines).
  if (stage_file == "griz.dat" || stage_file == "griz.usd") return {1};       // Fountain of Dreams, grizumi.c
  if (stage_file == "grnla.dat" || stage_file == "grnla.usd") return {1, 2};  // Final Destination, grlast.c
  return {};
}

// `out` gets the grafted file; `note` says what was done or why it could not be. False leaves `out`
// empty: the caller serves the original stage, as before. `whole` (visual_only_models) are the
// models taken from the skin whole instead of joint by joint.
inline bool graft_visuals(const std::vector<uint8_t>& clean, const std::vector<uint8_t>& skin,
                          std::vector<uint8_t>* out, std::string* note, const std::set<uint32_t>& whole = {}) {
  using namespace graft_detail;
  out->clear();
  Archive a{clean}, b{skin};
  if (!a.parse()) { *note = "the original stage file is not one this reads"; return false; }
  if (!b.parse()) { *note = "the skin's stage tables are malformed or unsupported"; return false; }
  // A skin that changes the collision or the stage's parameters is another stage, not a new look:
  // drawn over the original's collision its platforms would sit where nobody stands. Compared by
  // content (MapCollData in mp/types.h: vertices, lines, line ranges, collision joints), not by file
  // layout, which a stage editor that saves the file again changes without changing the stage.
  {
    const uint32_t ca = a.roots.at("coll_data"), cb = b.roots.at("coll_data");
    auto same_bytes = [&](uint32_t x, uint32_t y, uint64_t n) {
      return a.bounds(x, n) && b.bounds(y, n) && !std::memcmp(a.bytes.data() + 32ull + x, b.bytes.data() + 32ull + y, n);
    };
    auto array = [&](uint32_t field, uint64_t stride) {   // the array a header field points at, by count
      const uint32_t count = a.get(ca + field + 4);
      if (count != b.get(cb + field + 4)) return false;
      if (!count) return true;
      if (!a.pointers.count(ca + field) || !b.pointers.count(cb + field)) return false;
      return same_bytes(a.get(ca + field), b.get(cb + field), count * stride);
    };
    const bool same = a.bounds(ca, 0x30) && b.bounds(cb, 0x30) &&
                      same_bytes(ca + 0x10, cb + 0x10, 0x14) && same_bytes(ca + 0x2C, cb + 0x2C, 4) &&
                      array(0x00, 8) && array(0x08, 16) && array(0x24, 0x28);
    if (!same) { *note = "the skin changes the stage's collision"; return false; }
    std::string why;
    Compare param{a, b, &why}; param.allow_visual = false;
    if (!param.object(a.roots.at("grGroundParam"), b.roots.at("grGroundParam"))) {
      *note = "the skin changes the stage's parameters: " + why;
      return false;
    }
  }
  const uint32_t ha = a.roots.at("map_head"), hb = b.roots.at("map_head");
  const uint32_t count = a.get(ha + 12);
  if (count != b.get(hb + 12)) { *note = "the skin has a different number of stage models"; return false; }

  // 1. Pair the joints: each original joint with the skin joint at the same place, children matched by
  //    transform and flags so a removed sibling does not shift the rest. 0 = no counterpart.
  std::map<uint32_t, uint32_t> pairs;
  std::function<void(uint32_t, uint32_t, unsigned)> walk = [&](uint32_t ja, uint32_t jb, unsigned depth) {
    if (depth > 256 || pairs.size() > 16384 || pairs.count(ja)) return;
    pairs[ja] = jb;
    auto children = [](const Archive& z, uint32_t j) {
      std::vector<uint32_t> list;
      if (!j || !z.pointers.count(j + 8)) return list;
      for (uint32_t c = z.get(j + 8); list.size() < 4096; ) {
        list.push_back(c);
        if (!z.pointers.count(c + 12)) break;
        c = z.get(c + 12);
      }
      return list;
    };
    const std::vector<uint32_t> ca = children(a, ja), cb = children(b, jb);
    std::vector<bool> used(cb.size(), false);
    auto same = [&](uint32_t x, uint32_t y) {
      return (a.get(x + 4) & ~kDrawFlags) == (b.get(y + 4) & ~kDrawFlags) &&
             !std::memcmp(a.bytes.data() + 32ull + x + 20, b.bytes.data() + 32ull + y + 20, 36);
    };
    size_t next = 0;
    for (uint32_t x : ca) {
      uint32_t match = 0;
      for (size_t k = next; k < cb.size(); ++k)
        if (!used[k] && same(x, cb[k])) { match = cb[k]; used[k] = true; next = k + 1; break; }
      walk(x, match, depth + 1);
    }
  };
  // 0. Models taken whole: everything the skin's model entry reaches, as long as none of it is a
  //    joint that spawns particles (a random number draw), a gameplay table, a slot filled from
  //    another file, or a joint of a model kept from the original. Otherwise that model is paired
  //    joint by joint like the rest.
  const std::set<uint32_t> gameplay_b = {b.roots.at("coll_data"), b.roots.at("grGroundParam")};
  std::set<uint32_t> whole_joints;   // the skin's joints under the models asked for whole
  std::function<void(const Archive&, uint32_t, std::set<uint32_t>&, unsigned)> tree =
      [&](const Archive& z, uint32_t j, std::set<uint32_t>& into, unsigned depth) {
        for (; j && depth < 256 && into.size() < 16384 && into.insert(j).second; j = z.pointers.count(j + 12) ? z.get(j + 12) : 0)
          if (z.pointers.count(j + 8)) tree(z, z.get(j + 8), into, depth + 1);
      };
  for (uint32_t i : whole) {
    const uint32_t pb = b.get(hb + 8) + i * 52;
    if (i < count && b.pointers.count(pb)) tree(b, b.get(pb), whole_joints, 0);
  }
  // A model whose joints hold the stage's points (map header +0: spawns, camera, blast lines) is
  // gameplay whatever a table says.
  std::set<uint32_t> point_roots;
  for (const Archive* z : {&a, &b}) {
    const uint32_t h = z->roots.at("map_head");
    if (!z->pointers.count(h)) continue;
    const uint32_t sets = z->get(h), n = z->get(h + 4);
    for (uint32_t i = 0; i < n && i < 64; ++i)
      if (z->pointers.count(sets + i * 12)) point_roots.insert((z == &a ? 0x80000000u : 0u) | z->get(sets + i * 12));
  }
  std::map<uint32_t, uint32_t> whole_from;   // model index -> the skin's model entry
  std::set<uint32_t> copy;                   // skin objects to copy (their start offsets)
  for (uint32_t i : whole) {
    if (i >= count) continue;
    const uint32_t pa = a.get(ha + 8) + i * 52, pb = b.get(hb + 8) + i * 52;
    if ((a.pointers.count(pa) && point_roots.count(0x80000000u | a.get(pa))) || (b.pointers.count(pb) && point_roots.count(b.get(pb)))) continue;
    std::set<uint32_t> reach;
    std::vector<uint32_t> pending;
    for (uint32_t k = 0; k < 52; k += 4) if (b.pointers.count(pb + k)) pending.push_back(b.get(pb + k));
    bool ok = !pending.empty();
    while (!pending.empty() && ok) {
      const uint32_t at = pending.back(); pending.pop_back();
      if (gameplay_b.count(at) || at >= b.size || (b.joint_offsets.count(at) && !whole_joints.count(at))) { ok = false; break; }
      if (!reach.insert(at).second) continue;
      if (whole_joints.count(at) && (b.get(at + 4) & kBehaviourJoint)) { ok = false; break; }
      const uint32_t n = b.extent(at);
      if (b.externs.lower_bound(at) != b.externs.end() && b.externs.lower_bound(at)->first < uint64_t(at) + n) { ok = false; break; }
      for (auto p = b.pointers.lower_bound(at); p != b.pointers.end() && *p < uint64_t(at) + n; ++p) pending.push_back(b.get(*p));
      if (reach.size() > 200000) ok = false;
    }
    if (!ok) continue;
    copy.insert(reach.begin(), reach.end());
    whole_from[i] = pb;
  }

  for (uint32_t i = 0; i < count; ++i) {
    if (whole_from.count(i)) continue;
    const uint32_t pa = a.get(ha + 8) + i * 52, pb = b.get(hb + 8) + i * 52;
    if (!a.pointers.count(pa)) continue;
    walk(a.get(pa), b.pointers.count(pb) ? b.get(pb) : 0, 0);
  }

  // 2. Which skin meshes to bring over: everything reachable from the paired joints' +16, as long as
  //    it reaches no joint and no gameplay table of the skin (a skinned mesh bound to the skin's own
  //    bones would follow bones the game never moves); such a joint keeps the original mesh.
  std::map<uint32_t, uint32_t> mesh_of;   // original joint -> skin mesh offset, or UINT32_MAX for none
  for (const auto& [ja, jb] : pairs) {
    if (a.get(ja + 4) & kBehaviourJoint) continue;   // behaviour, not a mesh: untouched
    if (!jb || (b.get(jb + 4) & kBehaviourJoint) || !b.pointers.count(jb + 16)) { mesh_of[ja] = UINT32_MAX; continue; }
    std::set<uint32_t> reach;
    std::vector<uint32_t> pending{b.get(jb + 16)};
    bool ok = true;
    while (!pending.empty() && ok) {
      const uint32_t at = pending.back(); pending.pop_back();
      if (b.joint_offsets.count(at) || gameplay_b.count(at) || at >= b.size) { ok = false; break; }
      if (!reach.insert(at).second) continue;
      const uint32_t n = b.extent(at);
      // A slot the loader fills in from another file would arrive empty in the copy.
      if (b.externs.lower_bound(at) != b.externs.end() && b.externs.lower_bound(at)->first < uint64_t(at) + n) { ok = false; break; }
      for (auto p = b.pointers.lower_bound(at); p != b.pointers.end() && *p < uint64_t(at) + n; ++p)
        pending.push_back(b.get(*p));
      if (reach.size() > 200000) ok = false;
    }
    if (!ok) continue;   // keeps the original mesh
    mesh_of[ja] = b.get(jb + 16);
    copy.insert(reach.begin(), reach.end());
  }

  // 3. Lay the copied objects out after the original data: contiguous runs stay together, and each
  //    keeps its position modulo 32 (texture data must stay 32-byte aligned).
  std::vector<uint8_t> data(clean.begin() + 32, clean.begin() + 32 + a.size);
  std::map<uint32_t, uint32_t> moved;   // skin offset -> new offset
  for (auto it = copy.begin(); it != copy.end();) {
    const uint32_t run_start = *it;
    uint32_t run_end = run_start + b.extent(run_start);
    auto jt = std::next(it);
    while (jt != copy.end() && *jt == run_end) { run_end += b.extent(*jt); ++jt; }
    size_t base = (data.size() + 31) & ~size_t(31);
    base += run_start & 31;
    data.resize(base, 0);
    data.insert(data.end(), b.bytes.begin() + 32 + run_start, b.bytes.begin() + 32 + run_end);
    for (auto kt = it; kt != jt; ++kt) moved[*kt] = uint32_t(base + (*kt - run_start));
    it = jt;
  }
  data.resize((data.size() + 31) & ~size_t(31), 0);

  // 4. Relocations: the original's, the copied objects' own (moved), and each joint's new mesh.
  std::set<uint32_t> relocs(a.pointers.begin(), a.pointers.end());
  for (const auto& [from, to] : moved) {
    const uint32_t n = b.extent(from);
    for (auto p = b.pointers.lower_bound(from); p != b.pointers.end() && *p < uint64_t(from) + n; ++p) {
      const uint32_t at = to + (*p - from);
      const auto target = moved.find(b.get(*p));
      if (target == moved.end()) { *note = "a skin mesh points outside what was copied"; return false; }
      put(data, at, target->second);
      relocs.insert(at);
    }
  }
  unsigned shown = 0, hidden = 0, kept = 0;
  std::set<uint32_t> unhooked;   // original objects whose pointer is overwritten below
  for (const auto& [ja, jb] : pairs) {
    if (a.get(ja + 4) & kBehaviourJoint) continue;
    const auto m = mesh_of.find(ja);
    if (m == mesh_of.end()) { ++kept; continue; }
    if (a.pointers.count(ja + 16)) unhooked.insert(a.get(ja + 16));
    if (m->second == UINT32_MAX) {
      put(data, ja + 16, 0); relocs.erase(ja + 16); ++hidden;
    } else {
      put(data, ja + 16, moved.at(m->second)); relocs.insert(ja + 16); ++shown;
    }
  }
  // Models taken whole: the original's entry points at the skin's model (its pointer slots); the
  // entry's other words stay the original's.
  std::set<uint32_t> whole_models;
  for (const auto& [i, pb] : whole_from) {
    const uint32_t pa = a.get(ha + 8) + i * 52;
    for (uint32_t k = 0; k < 52; k += 4) {
      if (a.pointers.count(pa + k)) unhooked.insert(a.get(pa + k));
      if (b.pointers.count(pb + k)) { put(data, pa + k, moved.at(b.get(pb + k))); relocs.insert(pa + k); }
      else if (a.pointers.count(pa + k)) { put(data, pa + k, 0); relocs.erase(pa + k); }
    }
    whole_models.insert(i);
  }
  // Every original object that lost its pointer keeps one, in a table nothing reads: the archive's
  // object bounds (each object ends where the next pointed-to one starts) then stay the original's,
  // and the gameplay tables beside an unhooked model are measured as before.
  if (!unhooked.empty()) {
    data.resize((data.size() + 31) & ~size_t(31), 0);
    for (uint32_t target : unhooked) {
      const size_t at = data.size();
      data.resize(at + 4, 0);
      put(data, at, target);
      relocs.insert((uint32_t)at);
    }
    data.resize((data.size() + 31) & ~size_t(31), 0);
  }

  // 5. The archive: header, data, relocation table, then the original's root, external and string
  //    tables unchanged (they name data offsets, which the original part keeps).
  const uint32_t np = word(clean.data() + 12);
  const size_t old_tables = 32ull + a.size + size_t(word(clean.data() + 8)) * 4;
  std::vector<uint8_t> file(32, 0);
  std::copy(clean.begin() + 16, clean.begin() + 32, file.begin() + 16);
  file.insert(file.end(), data.begin(), data.end());
  for (uint32_t r : relocs) { file.resize(file.size() + 4); put(file, file.size() - 4, r); }
  file.insert(file.end(), clean.begin() + old_tables, clean.end());   // roots (np entries) + strings
  put(file, 0, uint32_t(file.size()));
  put(file, 4, uint32_t(data.size()));
  put(file, 8, uint32_t(relocs.size()));
  put(file, 12, np);

  // 6. The proof: the grafted file must match the original in every gameplay respect.
  std::string why;
  if (!matches(clean, file, &why, whole_models)) { *note = "the grafted stage did not match the original's gameplay: " + why; return false; }
  // How much of the skin's look made it: its meshes that found a place, of all it has.
  std::set<uint32_t> used, all;
  for (const auto& [ja, m] : mesh_of) if (m != UINT32_MAX) used.insert(m);
  for (uint32_t j : b.joint_offsets)
    if (!(b.get(j + 4) & kBehaviourJoint) && b.pointers.count(j + 16)) all.insert(b.get(j + 16));
  *out = std::move(file);
  *note = std::to_string(shown) + " joints show the skin, " + std::to_string(hidden) + " the skin removed, " +
          std::to_string(kept) + " keep the original mesh; " + std::to_string(used.size()) + " of the skin's " +
          std::to_string(all.size()) + " meshes drawn";
  if (!whole_models.empty()) {
    *note += "; the skin's own model";
    for (uint32_t i : whole_models) *note += " " + std::to_string(i);
    *note += " drawn whole";
  }
  return true;
}

}  // namespace host::cosmetics::stage_safety
