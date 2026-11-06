// Linux: HLSL to SPIR-V with DXC's libdxcompiler.so, as Windows uses dxcompiler.dll (gx_vulkan.cpp).
// The same compiler and arguments on every OS give the same SPIR-V, so the shipped shader cache fits
// Linux too and no shader needs a separate compiler process. The library is looked for in bin/
// beside the program, then on the system; without it gx_vulkan.cpp falls back to glslangValidator.
// In its own file: DXC's Windows type adapter (WinAdapter.h) stays out of the renderer.
#if defined(__linux__)
#include "dxc/include/dxc/dxcapi.h"

#include <dlfcn.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace host { void log(const char* fmt, ...); }   // host.h

namespace gx {
namespace {
struct LinuxDxc {
  CComPtr<IDxcUtils> utils;
  CComPtr<IDxcCompiler3> compiler;
};

LinuxDxc* linux_dxc() {
  static LinuxDxc* instance = []() -> LinuxDxc* {
    void* library = nullptr;
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) library = dlopen((exe.parent_path() / "bin" / "libdxcompiler.so").c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) library = dlopen("libdxcompiler.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) { host::log("vulkan: libdxcompiler.so not found, shaders compile with glslangValidator"); return nullptr; }
    auto create = (DxcCreateInstanceProc)dlsym(library, "DxcCreateInstance");
    auto* d = new LinuxDxc;
    if (!create || FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&d->utils))) ||
        FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&d->compiler)))) {
      host::log("vulkan: libdxcompiler.so did not start, shaders compile with glslangValidator");
      delete d;
      return nullptr;
    }
    host::log("vulkan: shaders compile with DXC (libdxcompiler.so)");
    return d;
  }();
  return instance;
}
}  // namespace

bool linux_dxc_available() { return linux_dxc() != nullptr; }

// The arguments gx_vulkan.cpp's Windows compile_hlsl passes, as wide strings.
std::vector<uint32_t> linux_dxc_spirv(const std::string& hlsl, const std::vector<std::wstring>& arguments, std::string* error) {
  LinuxDxc* d = linux_dxc();
  if (!d) { *error = "no shader compiler"; return {}; }
  DxcBuffer source{hlsl.data(), hlsl.size(), DXC_CP_UTF8};
  std::vector<LPCWSTR> args;
  for (const std::wstring& a : arguments) args.push_back(a.c_str());
  CComPtr<IDxcResult> result;
  if (FAILED(d->compiler->Compile(&source, args.data(), (UINT32)args.size(), nullptr, IID_PPV_ARGS(&result)))) {
    *error = "compile call failed";
    return {};
  }
  HRESULT status = E_FAIL;
  result->GetStatus(&status);
  if (FAILED(status)) {
    CComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    *error = errors && errors->GetStringLength() ? std::string(errors->GetStringPointer(), errors->GetStringLength()) : "unknown error";
    return {};
  }
  CComPtr<IDxcBlob> object;
  result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
  if (!object || object->GetBufferSize() % 4) { *error = "no SPIR-V output"; return {}; }
  std::vector<uint32_t> words(object->GetBufferSize() / 4);
  std::memcpy(words.data(), object->GetBufferPointer(), object->GetBufferSize());
  return words;
}
}  // namespace gx
#endif
