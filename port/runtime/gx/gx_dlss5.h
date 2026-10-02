// EXPERIMENTAL: DLSS 5 Neural Rendering over the DLSS / DLAA output (D3D12 only).
//
// Uses NVIDIA's NGX core and a small forwarder DLL to reach feature 18 in nvngx_dlssnr.dll
// (from the driver store or placed beside the executable). The model is never shipped with the game.
// A failed evaluation preserves the original DLSS/DLAA image. Failed model settings can be retried
// by changing the tuning; setup failures disable neural rendering for the session.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;
struct ID3D12Fence;

namespace gx {
namespace dlss5 {

// Neural appearance and processing controls. Changes rebuild the feature and reset its history.
struct Tuning {
  float intensity = 1.0f;         // DLSSNR.Intensity: experimental 0..10 UI range; >1 is undocumented
  float detail = 1.0f;            // DLSSNR.LocalStructureStrength: added surface detail
  float tone = 1.0f;              // DLSSNR.LocalToneStrength: local lighting and contrast
  float skin = -1.0f;             // DLSSNR.SkinStructureStrength: -1 lets the model decide
  int style = 0;                  // DLSSNR.Style: 0/1/2 = RenoDX Model A/B/C
  int preset = 0;                 // DLSSNR.Hint.Render.Preset, a weight hint (may be inert)
  bool auto_mask = true;          // DLSSNR.UseAutoMask: automatic skin-region detection
  int resolution_scale = 100;     // percentage per axis, independent of game resolution (25..100)
  int downsample_filter = 0;      // 0 area, 1 bilinear, 2 nearest
  int upsample_filter = 0;        // 0 bilinear, 1 bicubic, 2 nearest
  int reconstruction = 0;         // 0 RGB residual, 1 processed image
  int passes = 1;                 // 1..4, each with its own temporal feature
  float tone_restore = 0.0f;      // 0..1: how much of the black level lift is taken back out after the model
  // The feature comparison. tone_restore is left out on purpose: the correction runs after the
  // model, so moving its slider must not rebuild the feature.
  bool operator!=(const Tuning& o) const {
    return intensity != o.intensity || detail != o.detail || tone != o.tone || skin != o.skin ||
           style != o.style || preset != o.preset || auto_mask != o.auto_mask ||
           resolution_scale != o.resolution_scale || downsample_filter != o.downsample_filter ||
           upsample_filter != o.upsample_filter || reconstruction != o.reconstruction || passes != o.passes;
  }
};

// Named DLSS 5 tuning presets, one text file per name in a Dlss5Profiles folder beside
// port-settings.ini (same convention as ControllerProfiles). Different games, different taste in
// the picture, different opponents to compare against -- a name is easier to get back to than
// remembering individual slider positions.
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
  bool warm_only = false;                        // build and run the model but leave in.color untouched
  ID3D12Fence* fence = nullptr;                   // queue fence for resource/descriptor lifetime
  uint64_t signal_value = 0;                     // value signalled after this list executes
  // Size the model works at before the neural resolution scale, 0 = w/h. Smaller than the frame on
  // one axis when the frame is stored anamorphic (see display_model_size); the result is still
  // written back at w x h.
  uint32_t model_w = 0, model_h = 0;
};

// The size at which a w x h frame has square pixels on screen. display_shape is the width / height
// the WHOLE frame would have at the presented aspect. One axis is kept and the other shrunk, both
// even. Gives 0, 0 (no change) when the frame is within 2% of that shape already.
void display_model_size(uint32_t w, uint32_t h, float display_shape, uint32_t* model_w, uint32_t* model_h);

// Whether the model still has to be built (or rebuilt) for this size and tuning. The first build and
// the first evaluation take about a tenth of a second; doing them on a menu frame with warm_only set
// keeps that stall out of the match countdown. model_w/model_h as in Inputs.
bool needs_warmup(uint32_t w, uint32_t h, const Tuning& t, uint32_t model_w = 0, uint32_t model_h = 0);

// Runs the model over in.color. Returns true when the image was edited.
bool evaluate(const Inputs& in);
bool running();
// One line for the settings panel: running, or why not.
const char* status();
// The DLSS 5 model file (nvngx_dlssnr.dll) is on this computer. It is never shipped with the game.
bool model_found();
// Releases the model's feature and scratch textures. The GPU must be idle.
void shutdown();

}  // namespace dlss5
}  // namespace gx
