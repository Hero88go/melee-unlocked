// Package rules, canonical identity, pool policy and peer isolation without the game/window.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_ranked_rules.h"
#include "yougame_compatibility.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame::ranked_rules;
using yougame::compatibility_key;
using json = nlohmann::json;

int main() {
  Rules rules, same, list;
  const std::string all = R"({"version":1,"stages":[32,2,85],"characters":"all"})";
  CHECK(parse(all, false, rules));
  CHECK(rules.all_characters && rules.character_allowed(255)); // session capability still limits "all"
  CHECK(rules.stages == std::vector<uint16_t>({2,32,85}));
  CHECK(rules.canonical == R"({"characters":"all","stages":[2,32,85],"version":1})");
  CHECK(parse(" { \"characters\": \"all\", \"stages\": [2,85,32], \"version\": 1 } ", false, same));
  CHECK(same.digest == rules.digest);
  CHECK(rules.digest.size() == 64);
  CHECK(rules.digest == "742949e54412b237abb2f612b06f9db42517167836c445f88002e4dae8eaa6de");
  // A known canonical SHA-256 pins the algorithm as well as JSON serialization.
  CHECK(host::hackpack_ai::rules::sha256_hex(reinterpret_cast<const uint8_t*>("abc"), 3) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(parse(R"({"version":1,"stages":[85,0],"characters":[25,0,2]})", false, list));
  CHECK(!list.all_characters && list.character_allowed(25) && !list.character_allowed(1));
  CHECK(parse(R"({"characters":[2,25,0],"stages":[0,85],"version":1})", false, same));
  CHECK(same.digest == list.digest && same.digest != rules.digest);
  CHECK(parse(R"({"version":1,"stages":[65535,86],"characters":[255,26]})", true, same));
  CHECK(same.character_allowed(255));
  CHECK(!parse(R"({"version":1,"stages":[2],"characters":[]})", false, same));

  const auto reject = [&](const std::string& text, bool extended = false) {
    Rules out = rules;
    CHECK(!parse(text, extended, out));
    CHECK(out.digest.empty() && out.stages.empty()); // failures cannot expose a previous valid pool
  };
  for (const auto& text : {"", "{", "null", "[]", "true", "1", "{}",
      R"({"version":1,"stages":[2],"characters":"all","extra":0})",
      R"({"version":1,"stages":[2]})",
      R"({"version":1,"characters":"all"})",
      R"({"stages":[2],"characters":"all"})",
      R"({"version":1,"version":1,"stages":[2],"characters":"all"})",
      R"({"version":1,"stages":[2],"stages":[3],"characters":"all"})",
      R"({"version":1,"stages":[2],"characters":"all","characters":[2]})",
      R"({"version":1,"stages":[2],"characters":"all"} {})"}) reject(text);
  const json base = {{"version",1},{"stages",{2}},{"characters","all"}};
  for (const json& version : {json(0),json(2),json(-1),json(1.0),json(true),json("1"),json(nullptr),json::array(),json::object()}) {
    auto j = base; j["version"] = version; reject(j.dump());
  }
  for (const json& stages : {json::array(),json(nullptr),json("all"),json(2),json(true),json::object(),json({2,2})}) {
    auto j = base; j["stages"] = stages; reject(j.dump());
  }
  for (const json& characters : {json("ALL"),json(""),json(nullptr),json(2),json(true),json::object(),json({2,2})}) {
    auto j = base; j["characters"] = characters; reject(j.dump());
  }
  for (const char* field : {"stages", "characters"}) {
    for (const json& bad : {json(-1),json(2.0),json(true),json("2"),json(nullptr),json::array(),json::object(),
                           json(UINT64_MAX),json(INT64_MIN)}) {
      auto j = base; j[field] = json::array({bad}); reject(j.dump(), true);
    }
    auto j = base;
    j[field] = json::array({std::string(field) == "stages" ? 86 : 26}); reject(j.dump());
    CHECK(parse(j.dump(), true, same));
    j[field] = json::array({std::string(field) == "stages" ? 65536 : 256}); reject(j.dump(), true);
  }
  CHECK(parse(all + std::string(kMaxBytes - all.size(), ' '), false, same));
  reject(all + std::string(kMaxBytes + 1 - all.size(), ' '));
  reject(std::string(8000, '[') + std::string(8000, ']'));

  const std::string fp = "0123456789abcdef" + std::string(48, 'a');
  CHECK(compatibility_key("0.9.18", false, "") == "mu-0.9.18");
  CHECK(compatibility_key("0.9.18", true, fp) == "mu-0.9.18-mod-0123456789abcdef");
  CHECK(compatibility_key("0.9.18", false, "", true) == "mu-0.9.18-st");
  CHECK(compatibility_key("0.9.18", true, fp, true) == "mu-0.9.18-st-mod-0123456789abcdef");
  for (bool mod : {false,true}) for (bool engine : {false,true}) {
    CHECK(compatibility_key("0.9.18", mod, fp, engine, rules.digest) ==
          compatibility_key("0.9.18", mod, fp, engine) + "-r" + rules.digest.substr(0,8));
    const std::string suffix = std::string(engine ? "-st" : "") + (mod ? "-mod-0123456789abcdef" : "") + "-r" + rules.digest.substr(0,8);
    const auto key = compatibility_key(std::string(100,'v'), mod, fp, engine, rules.digest);
    CHECK(key == "mu-" + std::string(61-suffix.size(),'v') + suffix && key.size() == 64);
    CHECK(key != compatibility_key(std::string(100,'v'), mod, fp, engine, list.digest));
    CHECK(compatibility_key("dev build/+", mod, fp, engine, rules.digest) ==
          compatibility_key("dev build/+", mod, fp, engine) + "-r" + rules.digest.substr(0,8));
  }
  CHECK(compatibility_key("v", true, fp, true, "bad").empty());
  const std::string tagged = std::string(27,'v') + "-st" + std::string(100,'v');
  CHECK(compatibility_key(tagged,true,fp,true,rules.digest) != compatibility_key(tagged,true,fp,false,rules.digest));

  const std::vector<uint16_t> mm = {3,8}, empty;
  for (int mode = -1; mode <= 4; ++mode) for (bool yougame : {false,true}) for (bool mod : {false,true}) {
    const bool active = applies(yougame,mode,mod,fp);
    CHECK(active == (yougame && mode == 0 && mod));
    CHECK(stage_pool(&rules,active,mm) == (active ? rules.stages : mm));
    CHECK(stage_pool(nullptr,active,mm) == mm);
  }
  CHECK(!applies(true,0,true,"bad"));
  CHECK(stage_pool(nullptr,true,empty) == std::vector<uint16_t>({2,3,8,28,31,32}));
  CHECK(stage_pool(&rules,true,empty) == rules.stages);
  CHECK(peer_verdict(rules.digest,true,rules.digest,false) == 1);
  CHECK(peer_verdict(rules.digest,true,list.digest,false) == -1);
  CHECK(peer_verdict(rules.digest,true,"",false) == -1);
  CHECK(peer_verdict("",true,rules.digest,false) == -1);
  CHECK(peer_verdict("",true,"",false) == 1);
  CHECK(peer_verdict(rules.digest,false,"",false) == 0);
  CHECK(peer_verdict(rules.digest,false,"",true) == -1);
  CHECK(peer_verdict("",false,"",true) == 1);
  std::string collision = rules.digest; collision.back() = collision.back() == 'a' ? 'b' : 'a';
  CHECK(compatibility_key("v",true,fp,true,collision) == compatibility_key("v",true,fp,true,rules.digest));
  CHECK(peer_verdict(rules.digest,true,collision,false) == -1);
  std::puts("ranked rules: parser, canonical digest, keys, pools and peer isolation passed");
}
