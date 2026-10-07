#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace host::disc_fst {
bool path_by_offset(const std::vector<uint8_t>& fst, uint32_t offset, std::string* path);
bool music_paths(const std::vector<uint8_t>& fst, std::vector<std::string>* paths);
}  // namespace host::disc_fst
