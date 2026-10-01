// SPDX-License-Identifier: GPL-2.0-or-later
#include "user_gecko.h"
#include "host.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>

namespace user_gecko {
namespace {

std::string g_path;
std::vector<Code> g_codes;
bool g_code_patches_allowed = false;

// The DOL's code sections (main.dol: .init and .text). A write there changes instructions the
// translated code will never read.
bool in_code(uint32_t a) { return (a >= 0x80003100u && a < 0x80005520u) || (a >= 0x80005940u && a < 0x803B7240u); }

std::string hex8(uint32_t v) { char b[12]; std::snprintf(b, sizeof b, "%08X", v); return b; }

// Checks every line and says why a code cannot run here, or leaves reason empty.
void classify(Code& c) {
  if (c.lines.empty()) { c.reason = "has no code lines"; return; }
  for (size_t i = 0; i < c.lines.size(); ++i) {
    const uint32_t w = c.lines[i].first, v = c.lines[i].second;
    const uint32_t type = w >> 24;
    if (type == 0xE0 || type == 0xF0) continue;   // terminators: nothing to do
    if (type & 0x10) { c.reason = "writes through the pointer register (code type " + hex8(w).substr(0, 2) + "), which needs the Gecko handler"; return; }
    const uint32_t kind = type & 0x0E;
    if (type > 0x07 || kind > 0x06) {
      c.reason = type == 0xC2 || type == 0xC3 ? "injects PowerPC code (C2), which this build cannot run"
                                              : "uses code type " + hex8(w).substr(0, 2) + ", which needs the Gecko handler";
      return;
    }
    const uint32_t addr = 0x80000000u | (w & 0x01FFFFFFu);
    uint32_t len = kind == 0x00 ? ((v >> 16) + 1) : kind == 0x02 ? ((v >> 16) + 1) * 2 : kind == 0x04 ? 4 : v;
    if (kind == 0x06) i += (v + 7) / 8;           // the string's bytes follow on the next lines
    if (i >= c.lines.size() && kind == 0x06) { c.reason = "has a string write cut short"; return; }
    if (addr < 0x80003100u || addr + len > 0x81800000u) { c.reason = "writes outside game memory (" + hex8(addr) + ")"; return; }
    if (in_code(addr) || in_code(addr + len - 1)) {
      if (!g_code_patches_allowed) { c.reason = "patches the game's code at " + hex8(addr) + ", and the Source Port runs the game's source code, not PowerPC"; return; }
      c.patches_code = true;
    }
  }
  c.supported = true;
}

// A write of `len` bytes at `addr` by a code that patches the game's code. The first time, the
// bytes it replaces inside the code are kept, so switching the code off can put them back.
void keep_original(Code& c, uint32_t addr, uint32_t len) {
  if (c.patch_live) return;
  for (uint32_t k = 0; k < len; ++k)
    if (in_code(addr + k)) c.original.push_back({addr + k, host::rd8(addr + k)});
}

// The patched instructions are in RAM: the functions holding them run from there from now on.
void run_patched_functions(Code& c) {
  uint32_t last = 0;
  int words = 0, missing = 0;
  for (const auto& b : c.original) {
    const uint32_t word = b.first & ~3u;
    if (word == last) continue;
    last = word;
    if (ppc::redirect_function_at(word)) ++words; else ++missing;
  }
  host::log("gecko: code \"%s\" patches the game's code: %d instruction words now run from memory%s", c.name.c_str(), words,
            missing ? " (some are not inside a function and stay as they were)" : "");
}

// The code was switched off: the game's own instructions go back (the functions keep running from
// RAM, which holds the original bytes again).
void restore_original(Code& c) {
  for (const auto& b : c.original) host::wr8(b.first, b.second);
  c.original.clear();
  c.patch_live = false;
  host::log("gecko: code \"%s\" is off: the game's own code is back", c.name.c_str());
}

}  // namespace

void set_code_patches_allowed(bool allowed) { g_code_patches_allowed = allowed; }

void load(const std::string& path, const std::vector<std::string>& enabled_names, bool chosen) {
  g_path = path;
  g_codes.clear();
  std::ifstream f(path);
  if (!f) { host::log("gecko: no user codes (%s not found)", path.c_str()); return; }
  std::vector<std::string> ini_enabled;
  std::string line, section;
  static const std::regex code_line(R"(^\s*([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8}))");
  while (std::getline(f, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos) continue;
    line = line.substr(first);
    if (line[0] == '[') { section = line; continue; }
    if (section == "[Gecko_Enabled]") { if (line[0] == '$') ini_enabled.push_back(line.substr(1)); continue; }
    if (section != "[Gecko]") continue;
    if (line[0] == '$') {
      Code c; c.name = line.substr(1);
      const size_t bracket = c.name.find('[');   // Dolphin: "$Name [Author]"
      if (bracket != std::string::npos) c.name = c.name.substr(0, bracket);
      while (!c.name.empty() && c.name.back() == ' ') c.name.pop_back();
      g_codes.push_back(c);
      continue;
    }
    if (g_codes.empty()) continue;
    if (line[0] == '*') { g_codes.back().notes.push_back(line.substr(1)); continue; }
    std::smatch m;
    if (std::regex_search(line, m, code_line))
      g_codes.back().lines.push_back({(uint32_t)std::stoul(m[1].str(), nullptr, 16), (uint32_t)std::stoul(m[2].str(), nullptr, 16)});
  }
  const std::vector<std::string>& on = chosen ? enabled_names : ini_enabled;
  int supported = 0, enabled = 0;
  for (Code& c : g_codes) {
    classify(c);
    c.enabled = c.supported && std::find(on.begin(), on.end(), c.name) != on.end();
    supported += c.supported; enabled += c.enabled;
    if (!c.supported) host::log("gecko: \"%s\" cannot run here: %s", c.name.c_str(), c.reason.c_str());
  }
  host::log("gecko: %zu user codes in %s (%d usable, %d on)", g_codes.size(), path.c_str(), supported, enabled);
}

const char* native_equivalent(const Code& c) {
  // Slippi's optional codes (GALE01r2.ini) that the Source Port carries as C, by name or by the
  // code's first line.
  struct Known { const char* name; uint32_t w, v; const char* label; };
  static const Known known[] = {
      {"widescreen 16:9", 0x043BB05Cu, 0x3EB00000u, "Widescreen 16:9"},
      {"disable screen shake", 0x04030E44u, 0x4E800020u, "Disable Screen Shake"},
      {"flash red on failed l-cancel", 0xC20C0148u, 0x0000000Cu, "Flash Red on Failed L-Cancel"},
      {"lagless fod", 0xC21CBB90u, 0x00000005u, "Lagless FoD"},
  };
  std::string name = c.name;
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
  for (const Known& k : known) {
    if (name.find(k.name) != std::string::npos) return k.label;
    if (!c.lines.empty() && c.lines[0].first == k.w && c.lines[0].second == k.v) return k.label;
  }
  return nullptr;
}

const std::string& path() { return g_path; }
std::vector<Code>& codes() { return g_codes; }
bool any_enabled() { return std::any_of(g_codes.begin(), g_codes.end(), [](const Code& c) { return c.enabled; }); }

std::string add(const std::string& name, const std::string& body) {
  Code c; c.name = name;
  std::istringstream in(body);
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) { first = false; continue; }
    line = line.substr(start);
    // A pasted whole code (Dolphin's own "$Name" line included) names itself; the typed Name field
    // is then only what is offered until the paste says otherwise.
    if (first && line[0] == '$') { c.name = line.substr(1); first = false; continue; }
    first = false;
    if (line[0] == '$') continue;   // a second header inside one paste: only the first code is kept
    if (line[0] == '*') { c.notes.push_back(line.substr(1)); continue; }
    std::smatch m;
    static const std::regex code_line(R"(^\s*([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8}))");
    if (std::regex_search(line, m, code_line))
      c.lines.push_back({(uint32_t)std::stoul(m[1].str(), nullptr, 16), (uint32_t)std::stoul(m[2].str(), nullptr, 16)});
  }
  while (!c.name.empty() && c.name.back() == ' ') c.name.pop_back();
  if (c.name.empty()) return "needs a name";
  if (std::any_of(g_codes.begin(), g_codes.end(), [&](const Code& e) { return e.name == c.name; }))
    return "a code named \"" + c.name + "\" already exists";
  if (c.lines.empty()) return "no XXXXXXXX YYYYYYYY code lines found";
  classify(c);
  g_codes.push_back(c);
  return "";
}

void remove(const std::string& name) {
  g_codes.erase(std::remove_if(g_codes.begin(), g_codes.end(), [&](const Code& c) { return c.name == name; }), g_codes.end());
}

bool save() {
  if (g_path.empty()) return false;
  std::ofstream f(g_path, std::ios::trunc);
  if (!f) return false;
  f << "[Gecko]\n";
  for (const Code& c : g_codes) {
    f << "$" << c.name << "\n";
    for (const std::string& n : c.notes) f << "*" << n << "\n";
    for (const auto& l : c.lines) { char b[20]; std::snprintf(b, sizeof b, "%08X %08X", l.first, l.second); f << b << "\n"; }
  }
  // Kept for Dolphin and other tools that read this file directly; this app tracks enabled codes in
  // port-settings.ini instead (a code's on/off state should not depend on which game folder it is
  // playing from), so this section is written but never read back by load() once `chosen` is true.
  f << "\n[Gecko_Enabled]\n";
  for (const Code& c : g_codes) if (c.enabled) f << "$" << c.name << "\n";
  return f.good();
}

void apply() {
  for (Code& c : g_codes) {
    if (!c.enabled) { if (c.patch_live) restore_original(c); continue; }
    if (!c.patches_code) {
      for (size_t i = 0; i < c.lines.size(); ++i) {
        const uint32_t w = c.lines[i].first, v = c.lines[i].second, type = w >> 24;
        if (type == 0xE0 || type == 0xF0) continue;
        const uint32_t addr = 0x80000000u | (w & 0x01FFFFFFu);
        switch (type & 0x0E) {
          case 0x00: for (uint32_t k = 0; k <= (v >> 16); ++k) host::wr8(addr + k, (uint8_t)v); break;
          case 0x02: for (uint32_t k = 0; k <= (v >> 16); ++k) host::wr16(addr + 2 * k, (uint16_t)v); break;
          case 0x04: host::wr32(addr, v); break;
          case 0x06: {
            for (uint32_t k = 0; k < v; ++k) {
              const auto& l = c.lines[i + 1 + k / 8];
              const uint32_t word = (k % 8) < 4 ? l.first : l.second;
              host::wr8(addr + k, (uint8_t)(word >> (24 - 8 * (k % 4))));
            }
            i += (v + 7) / 8;
            break;
          }
        }
      }
      continue;
    }
    // A code that writes into the game's code. A byte is written only when it differs: a mod
    // session watches its code blocks for changes, and the same values every frame would have a
    // block compared again every frame.
    auto put8 = [](uint32_t a, uint8_t b) { if (host::rd8(a) != b) host::wr8(a, b); };
    for (size_t i = 0; i < c.lines.size(); ++i) {
      const uint32_t w = c.lines[i].first, v = c.lines[i].second, type = w >> 24;
      if (type == 0xE0 || type == 0xF0) continue;
      const uint32_t addr = 0x80000000u | (w & 0x01FFFFFFu);
      switch (type & 0x0E) {
        case 0x00:
          keep_original(c, addr, (v >> 16) + 1);
          for (uint32_t k = 0; k <= (v >> 16); ++k) put8(addr + k, (uint8_t)v);
          break;
        case 0x02:
          keep_original(c, addr, ((v >> 16) + 1) * 2);
          for (uint32_t k = 0; k <= (v >> 16); ++k) { put8(addr + 2 * k, (uint8_t)(v >> 8)); put8(addr + 2 * k + 1, (uint8_t)v); }
          break;
        case 0x04:
          keep_original(c, addr, 4);
          for (uint32_t k = 0; k < 4; ++k) put8(addr + k, (uint8_t)(v >> (24 - 8 * k)));
          break;
        case 0x06: {
          keep_original(c, addr, v);
          for (uint32_t k = 0; k < v; ++k) {
            const auto& l = c.lines[i + 1 + k / 8];
            const uint32_t word = (k % 8) < 4 ? l.first : l.second;
            put8(addr + k, (uint8_t)(word >> (24 - 8 * (k % 4))));
          }
          i += (v + 7) / 8;
          break;
        }
      }
    }
    if (!c.patch_live) { c.patch_live = true; run_patched_functions(c); }
  }
}

}  // namespace user_gecko
