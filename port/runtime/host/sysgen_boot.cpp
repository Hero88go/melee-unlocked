// SPDX-License-Identifier: GPL-2.0-or-later
#include "sysgen_boot.h"
#include "sysgen.h"
#include "host.h"

namespace host {
namespace { sysgen::Assets assets; }
bool required_sys_asset(const std::string& name) {
  return name == "GameSetup_gui.dat" || name == "slpCSS.dat";
}
void prepare_sys_assets(const std::string& user_dir, const std::string& data_dir) {
#ifdef MELEE_NO_SLIPPI
  (void)user_dir; (void)data_dir;
#else
  try {
    sysgen::Config config;
    config.sys_dir = std::filesystem::u8path(options.sys_dir).lexically_normal();
    if (config.sys_dir.filename().empty()) config.sys_dir = config.sys_dir.parent_path();
    config.build_dir = config.sys_dir.parent_path();
    if (config.build_dir.empty()) config.build_dir = ".";
    if (!data_dir.empty()) config.data_dir = std::filesystem::u8path(data_dir);
    else if (!user_dir.empty()) {
      auto user = std::filesystem::u8path(user_dir).lexically_normal();
      if (user.filename().empty()) user = user.parent_path();
      config.data_dir = user.parent_path();
      if (config.data_dir.empty()) config.data_dir = ".";
    }
    sysgen::Disc disc;
    disc.verify_retail = [] { return disc_has_vanilla_dol(); };
    disc.read = [](const std::string& name, size_t expected, std::vector<uint8_t>& out) {
      uint32_t offset = 0, size = 0;
      if (!disc_find_file(name, &offset, &size) || size != expected) return false;
      out.resize(size);
      return disc_read(offset, out.data(), size);
    };
    std::string error;
    if (!assets.prepare(config, disc, error)) die("Sys generation failed: %s", error.c_str());
    else if (!assets.status_message().empty()) host::log("%s", assets.status_message().c_str());
  } catch (const std::exception&) {
    die("Sys generation failed: cannot prepare boot paths or read disc assets");
  }
#endif
}
std::string sys_game_file_path(const std::string& name) {
  // Ordinary Slippi file/diff lookup stays unchanged; only the two managed assets redirect.
  if (!required_sys_asset(name)) return options.sys_dir + "/GameFiles/GALE01/" + name;
  return assets.lookup(std::filesystem::u8path(options.sys_dir), name).u8string();
}
std::vector<std::string> generated_sys_names() {
  return assets.generated_names(std::filesystem::u8path(options.sys_dir));
}
}  // namespace host
