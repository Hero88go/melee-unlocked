// A shader preset of the player's choice over the game picture: RetroArch slang presets (.slangp),
// run by librashader (third_party/librashader, loaded at run time; without its DLL this is off).
// Presets live in the Shaders folder next to the program. Presentation only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;

namespace gx::user_shader {

bool available();                        // the library is present and loaded
std::vector<std::string> presets();      // .slangp files under Shaders, as paths relative to it
void open_folder();
std::string status();                    // why the chosen preset is not running; empty when it is

// Where in the output the picture goes, in pixels.
struct Target { float x, y; uint32_t width, height; };

// prepare: true when a filter chain for `preset` is ready, building it when the choice changed.
// `wait` must return once the GPU has finished every frame submitted so far (the old chain is freed
// after it). A preset that failed stays off until another is chosen. draw records its passes: the
// source must be readable as a shader resource, the output is a render target view of the back
// buffer. The command list's heaps, pipeline, targets and viewport are the chain's afterwards.
bool d3d12_prepare(ID3D12Device* device, const std::string& preset, void (*wait)(void*), void* owner);
bool d3d12_draw(ID3D12GraphicsCommandList* list, ID3D12Resource* source, size_t output_rtv, int output_format,
                uint32_t output_width, uint32_t output_height, const Target& where, size_t frame);
void d3d12_release();

bool d3d11_prepare(ID3D11Device* device, const std::string& preset);
bool d3d11_draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, ID3D11RenderTargetView* output,
                const Target& where, size_t frame);
void d3d11_release();

}  // namespace gx::user_shader
