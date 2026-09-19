// EXPERIMENTAL: DLSS 5 Neural Rendering over the DLSS / DLAA output (D3D12 only).
//
// NVIDIA ships no public SDK for Neural Rendering yet, so this drives the model the way the community
// tools do: NGX core from NVIDIA's DLSS SDK, the model (nvngx_dlssnr.dll, from the driver store or
// placed beside the executable) reached through a small forwarder DLL, feature 18. The model is never
// shipped with the game. Everything here fails closed: any error turns it off for the session and
// the frame is presented exactly as DLSS produced it.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;

namespace gx {
namespace dlss5 {

// The model's own controls. It reads them when its feature is built, so changing any of them
// rebuilds the feature (a brief reset of its history, no restart).
struct Tuning {
  float intensity = 1.0f;         // DLSSNR.Intensity: how strongly the result replaces the frame, 0..1
  float detail = 1.0f;            // DLSSNR.LocalStructureStrength: added surface detail
  float tone = 1.0f;              // DLSSNR.LocalToneStrength: local lighting and contrast
  float skin = -1.0f;             // DLSSNR.SkinStructureStrength: -1 lets the model decide
  int style = 0;                  // DLSSNR.Style
  int preset = 0;                 // DLSSNR.Hint.Render.Preset, 0 = the model's default
  bool auto_mask = true;          // DLSSNR.UseAutoMask: the model decides what to leave alone
  bool operator!=(const Tuning& o) const {
    return intensity != o.intensity || detail != o.detail || tone != o.tone || skin != o.skin ||
           style != o.style || preset != o.preset || auto_mask != o.auto_mask;
  }
};

// Named DLSS 5 tuning presets, one text file per name in a Dlss5Profiles folder beside
// port-settings.ini (same convention as ControllerProfiles). Different games, different taste in
// the picture, different opponents to compare against -- a name is easier to get back to than
// remembering seven slider positions.
void profile_set_folder(const std::string& settings_path);
std::vector<std::string> profile_list();
bool profile_save(const std::string& name, const Tuning& t);
bool profile_load(const std::string& name, Tuning& t);   // false, t untouched, if the file is missing or unreadable
bool profile_delete(const std::string& name);

struct Inputs {
  ID3D12Device* device;
  ID3D12GraphicsCommandList* list;
  ID3D12Resource* color; uint32_t w, h;          // R8G8B8A8_UNORM, UNORDERED_ACCESS on entry and exit; edited in place
  ID3D12Resource* depth; uint32_t depth_state;   // D32_FLOAT, returned to depth_state
  ID3D12Resource* mvec; uint32_t mvec_state;     // R16G16_FLOAT pixel-space vectors, returned to mvec_state
  uint32_t guide_x, guide_y, guide_w, guide_h;   // region of depth/mvec that maps onto the whole color image
  bool reset;                                    // no relation to the previous frame
  Tuning tuning;
};

// Runs the model over in.color. Returns true when the image was edited.
bool evaluate(const Inputs& in);
bool running();
// One line for the settings panel: running, or why not.
const char* status();
// Releases the model's feature and scratch textures. The GPU must be idle.
void shutdown();

}  // namespace dlss5
}  // namespace gx
