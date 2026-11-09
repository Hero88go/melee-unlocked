// The RTX Pack's NVIDIA renderer, loaded as a separate program (rtx_integrator.h). It is built from
// NVIDIA's SDKs outside this repository and reaches the game only through RtxIntegrator.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "rtx_integrator.h"

#ifdef _WIN32
#include <windows.h>

#include <filesystem>

namespace gx {

std::unique_ptr<RtxIntegrator> load_rtx_plugin(const std::wstring& dir, std::string* error) {
  const std::filesystem::path dll = std::filesystem::path(dir) / L"melee_rtx_nvidia.dll";
  std::error_code ec;
  if (!std::filesystem::is_regular_file(dll, ec)) return nullptr;   // no RTX Pack: nothing to say
  // Its own dependencies (none beyond Windows and D3D12 today) are looked for in its folder first.
  HMODULE module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!module) {
    if (error) *error = "melee_rtx_nvidia.dll could not be loaded (error " + std::to_string(GetLastError()) + ")";
    return nullptr;
  }
  using AbiFn = uint32_t (*)();
  using CreateFn = RtxIntegrator* (*)();
  const auto abi = reinterpret_cast<AbiFn>(GetProcAddress(module, "melee_rtx_plugin_abi"));
  const auto create = reinterpret_cast<CreateFn>(GetProcAddress(module, "melee_rtx_create_integrator"));
  if (!abi || !create) {
    FreeLibrary(module);
    if (error) *error = "melee_rtx_nvidia.dll is not an RTX Pack renderer";
    return nullptr;
  }
  if (abi() != rtx_plugin_abi()) {
    FreeLibrary(module);
    if (error) *error = "the RTX Pack is for another version of the game; download it again from PC settings > Video";
    return nullptr;
  }
  // The module stays loaded for the life of the process: the integrator's code lives in it.
  RtxIntegrator* integrator = create();
  if (!integrator && error) *error = "the RTX Pack renderer did not start";
  return std::unique_ptr<RtxIntegrator>(integrator);
}

}  // namespace gx

#else

namespace gx {
std::unique_ptr<RtxIntegrator> load_rtx_plugin(const std::wstring&, std::string*) { return nullptr; }
}  // namespace gx

#endif
