// Verification, routing, copy independence, replacement and save preservation.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "launcher_mod_catalog.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
using namespace launcher::mod_catalog;
namespace fs = std::filesystem;
int failures = 0;
void check(bool ok, const char* what) { std::cout << (ok ? "ok   " : "FAIL ") << what << '\n'; failures += !ok; }
void write(const fs::path& p, const std::string& text) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << text; }
std::string read(const fs::path& p) { std::ifstream f(p,std::ios::binary); return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()}; }
int main(int argc, char** argv) {
  const auto root=fs::temp_directory_path() / ("mu-mod-catalog-test-"+std::to_string(GetCurrentProcessId()));
  fs::create_directories(root);
  const std::string abc="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  std::string digest,error;
  check(!download_digest("","",&digest,&error),"a missing download hash is refused");
  check(download_digest("sha256:"+abc,"",&digest,&error)&&digest==abc,"GitHub SHA-256 is accepted");
  check(download_digest("",abc,&digest,&error),"a measured pinned hash is accepted");
  check(!download_digest("sha256:"+abc,std::string(64,'0'),&digest,&error),"a replaced release cannot override a pinned hash");
  check(!download_digest("sha256:wrong","",&digest,&error),"malformed digest refused");
  std::vector<CatalogMod> mods;
  nlohmann::json j={{"mods", {{{"id","te"},{"name","20XX TE"},{"repo","official/save"},{"page","https://example.com/release"},
    {"kind","gci"},{"one_click",true},{"policy","licensed"},{"sha256",abc},{"version","2"},{"future",5}}}}};
  check(parse(j.dump(),&mods,&error)&&mods.size()==1&&mods[0].version=="2"&&mods[0].one_click,"full catalog fields parse and unknown fields are ignored");
  j["mods"][0]["policy"]="official_page_only";
  check(parse(j.dump(),&mods,&error)&&!mods[0].one_click,"catalog download policy is retained");
  j["mods"][0]["id"]="../../escape";
  check(!parse(j.dump(),&mods,&error),"unsafe catalog IDs are refused");
  j["mods"][0]["id"]="te";j["mods"].push_back(j["mods"][0]);
  check(!parse(j.dump(),&mods,&error),"duplicate catalog IDs are refused");
  j["mods"].erase(1); j["mods"][0]["policy"]="permission_granted";
  j["mods"][0]["output_sha256"]=abc; j["mods"][0]["note"]="Untested on the Static Recomp.";
  check(parse(j.dump(),&mods,&error)&&mods[0].one_click&&mods[0].output_sha256==abc&&mods[0].note=="Untested on the Static Recomp.","permitted downloads retain result hashes and support notes");
  j["mods"][0]["output_sha256"]="not-a-hash";
  check(!parse(j.dump(),&mods,&error),"malformed result hashes cannot enter the catalog");
  j["mods"][0]["output_sha256"]=abc; j["mods"][0]["note"]="first\nsecond";
  check(!parse(j.dump(),&mods,&error),"multiline metadata cannot enter card support notes");
  // Validate the launcher's actual offline catalog so packaging cannot ship a stale fallback.
  const fs::path source_file=fs::path(__FILE__).parent_path().parent_path()/"app/launcher_mods.inl";
  const std::string launcher=read(source_file), open_marker="R\"catalog(", close_marker=")catalog\"";
  const size_t open=launcher.find(open_marker), close=open==std::string::npos ? std::string::npos : launcher.find(close_marker,open+open_marker.size());
  std::vector<CatalogMod> defaults;
  const bool parsed_defaults=open!=std::string::npos&&close!=std::string::npos&&parse(launcher.substr(open+open_marker.size(),close-open-open_marker.size()),&defaults,&error);
  check(parsed_defaults&&defaults.size()==5,"embedded offline catalog contains all five planned mods");
  std::set<std::string> default_ids;
  bool ready=parsed_defaults;
  const std::string xdelta_pin="d81f59b2fe5e8589c0ee9782e231c805084f4d23dfade413903a4cad63b4e342";
  for(const auto& item:defaults) {
    default_ids.insert(item.id);
    ready&=item.one_click&&valid_sha256(item.sha256)&&!item.url.empty()&&!item.tag.empty()&&!item.asset_pattern.empty()&&!item.version.empty()&&!item.credits.empty();
    if(item.kind!="gci") ready&=valid_sha256(item.patch_sha256)&&item.tool_sha256==xdelta_pin;
    if(item.id=="akaneia"||item.id=="ace"||item.id=="hackpack") ready&=valid_sha256(item.output_sha256)&&!item.note.empty();
  }
  check(ready&&default_ids==std::set<std::string>{"te","tmce","akaneia","ace","hackpack"},"every planned offline row has one-click retrieval and its required verification pins");
  const std::string detected_json=R"({"items":[{"id":"custom123","key":"abcd","kind":"asset_mod","name":"ACE in a filename","path":"Mods/Discs/a.iso","needs_engine":"either","status":"supported","enabled":true},{"id":"tmce","key":"tmce","kind":"tmce","path":"Mods/Discs/t.iso","needs_engine":"source","status":"supported"},{"id":"bad","key":"bad","kind":"code_mod","needs_engine":"recomp","status":"not_supported_yet"}]})";
  const auto found=detected(detected_json);
  check(found.size()==3&&found[0].id=="custom123","detected kinds and content IDs retained without name guesses");
  check(play_engine(found[0],"recomp")=="recomp"&&play_engine(found[0],"source")=="source","both-engine packs follow the player's selection");
  check(play_engine(found[1],"recomp")=="source"&&!playable(found[2]),"Source-only and unsupported routing follows metadata");
  write(root/"original.iso","abc");
  check(sha256_file(root/"original.iso")==abc,"streaming SHA-256 matches the standard vector");
  CatalogMod m;m.id="mod";m.sha256=abc;mods={m};
  check(matching_archive(mods,abc)&&!matching_archive(mods,std::string(64,'0')),"dropped archives require exact catalog hashes");
  write(root/"tools/xdelta-untrusted.exe","wrong");write(root/"tools/xdelta-verified.EXE","abc");
  check(pinned_xdelta(root/"tools",abc).filename()=="xdelta-verified.EXE","selection skips an unpinned tool and handles uppercase extension");
  check(pinned_xdelta(root/"tools","").empty(),"a tool cannot be selected without a pin");
  check(shipped_tool(root,"xdelta-verified.EXE",abc).filename()=="xdelta-verified.EXE","shipped tools are found under the launcher tools directory");
  check(shipped_tool(root,"missing.exe",abc).empty()&&shipped_tool(root,"xdelta-verified.EXE","").empty(),"missing or unpinned shipped tools are refused");
  write(root/"tools/xdelta-verified.EXE","changed");
  check(shipped_tool(root,"xdelta-verified.EXE",abc).empty(),"altered shipped tool bytes are refused");
  write(root/"tools/xdelta-verified.EXE","abc");
  const auto pack=root/"game/Mods/Discs/mod.iso";
  check(copy_pack(root/"original.iso",pack,true,&error),"pack copy installs atomically");
  write(root/"original.iso","edited");check(read(pack)=="abc","editing the original cannot change the installed pack");
  check(copy_pack(root/"original.iso",pack,true,&error)&&read(pack)=="edited","an update replaces the installed contents");
  check(!copy_pack(root/"missing.iso",pack,true,&error)&&read(pack)=="edited","failed update preserves the installed disc");
  write(root/"game/User/GC/Mods/mod/save.gci","save");write(root/"game/Mods/.cache/detected.json","cache");
  check(!remove_pack(root/"game",root/"original.iso",&error),"remove refuses unmanaged original files");
  check(remove_pack(root/"game",pack,&error)&&!fs::exists(pack)&&read(root/"game/User/GC/Mods/mod/save.gci")=="save","remove clears the disc/cache and keeps saves");
  check(link_pack(root/"game",root/"original.iso",&error)&&remove_pack(root/"game",root/"original.iso",&error)&&fs::exists(root/"original.iso"),"removing a managed link keeps the original disc");
  write(root/"settings.ini","volume 17\nmod_te_enabled 0\nmod_enabled abcd 0\n");
  check(enable_pack(root/"settings.ini","te",true,&error)&&enable_pack(root/"settings.ini","abcd",true,&error)&&read(root/"settings.ini").find("volume 17")!=std::string::npos&&read(root/"settings.ini").find("mod_enabled abcd 1")!=std::string::npos,"launcher choices preserve other preferences");
  fs::create_directories(root/"blocked.ini");
  check(!enable_pack(root/"blocked.ini","te",true,&error),"unreadable existing settings cannot be overwritten");
  write(root/"patch.xdelta","fake");
  check(!apply_delta(root/"tools/xdelta-verified.EXE",std::string(64,'0'),root/"patch.xdelta",abc,root/"original.iso",root/"output.iso",&error)&&!fs::exists(root/"output.iso"),"unverified executables are refused before starting a process");
  // Optional integration gate uses an independently verified official xdelta and synthetic data.
  if(argc==5) {
    const fs::path tool=fs::u8path(argv[1]),patch=fs::u8path(argv[2]),source=fs::u8path(argv[3]),expected=fs::u8path(argv[4]);
    check(apply_delta(tool,sha256_file(tool),patch,sha256_file(patch),source,root/"synthetic.out",&error)&&read(root/"synthetic.out")==read(expected),"verified xdelta applies a synthetic patch byte for byte");
    std::atomic<bool> cancelled{true};
    check(!apply_delta(tool,sha256_file(tool),patch,sha256_file(patch),source,root/"cancelled.out",&error,&cancelled)&&!fs::exists(root/"cancelled.out")&&error=="Cancelled. Nothing was installed.","cancelled patch jobs leave no staged output");

  }
  fs::remove_all(root);
  std::cout << (failures?"FAILED":"PASS") << ": mod catalog, " << failures << " failures\n";
  return failures?1:0;
}
