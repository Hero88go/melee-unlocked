// Linux: the Vulkan renderer's DXC path (gx_dxc_linux.cpp) loads libdxcompiler.so and turns HLSL into
// SPIR-V with the arguments Windows uses. Run with LD_LIBRARY_PATH at the shipped library.
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace host { void log(const char*, ...) {} }   // the loader logs which compiler it found
namespace gx {
bool linux_dxc_available();
std::vector<uint32_t> linux_dxc_spirv(const std::string& hlsl, const std::vector<std::wstring>& arguments, std::string* error);
}

int main() {
  if (!gx::linux_dxc_available()) { std::puts("FAIL: libdxcompiler.so did not load"); return 1; }
  const std::string ps =
      "Texture2D t : register(t0); SamplerState s : register(s0);\n"
      "cbuffer C : register(b0) { float4 tint; };\n"
      "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return t.Sample(s, uv) * tint; }\n";
  const std::vector<std::wstring> args = {L"-spirv", L"-E", L"main", L"-T", L"ps_6_0", L"-fspv-target-env=vulkan1.3",
                                          L"-fvk-b-shift", L"0", L"0", L"-fvk-t-shift", L"2", L"0",
                                          L"-fvk-s-shift", L"3", L"0", L"-O3", L"-Wno-ignored-attributes"};
  std::string error;
  const std::vector<uint32_t> words = gx::linux_dxc_spirv(ps, args, &error);
  if (words.size() < 5 || words[0] != 0x07230203u) { std::printf("FAIL: no SPIR-V (%s)\n", error.c_str()); return 1; }
  // A broken shader reports the compiler's message instead of output.
  error.clear();
  if (!gx::linux_dxc_spirv("float4 main() : SV_Target { return undefined_name; }", args, &error).empty() || error.empty()) {
    std::puts("FAIL: a broken shader did not report an error");
    return 1;
  }
  std::printf("PASS: %zu SPIR-V words; errors reported\n", words.size());
  return 0;
}
