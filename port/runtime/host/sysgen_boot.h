// Shared boot/lookup bridge, used by Source Port and Static Recomp on every host.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <vector>
namespace host {
void prepare_sys_assets(const std::string& user_dir, const std::string& data_dir);
std::string sys_game_file_path(const std::string& name);
std::vector<std::string> generated_sys_names();
bool required_sys_asset(const std::string& name);
}  // namespace host
