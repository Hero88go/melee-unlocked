// EXPERIMENTAL: DLSS 5 Neural Rendering over the DLSS / DLAA output (D3D12 only).
//
// NVIDIA's published route first (the RTX Remix 1.6 SDK): the NGX core loads NVIDIA's signed
// nvngx_dlssnr.dll, which the release ships beside the executable, and reports whether this GPU
// and driver can run it. When it will not, a model build in the DLSS5 folder (for example a
// community FP16 build for RTX 20 and 30 series cards) is reached directly through the helper DLL.
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
  // Defaults are NVIDIA's (RTX Remix 1.6), whose documented range for these four is 0..1.
  float intensity = 1.0f;         // DLSSNR.Intensity: experimental 0..10 UI range; >1 is undocumented
  float detail = 0.7f;            // DLSSNR.LocalStructureStrength: details, shadows and materials
  float tone = 0.3f;              // DLSSNR.LocalToneStrength: lightness, darkness and color
  float skin = 0.5f;              // DLSSNR.SkinStructureStrength: characters (with the auto mask); -1 lets the model decide
  int style = 0;                  // DLSSNR.Style: 0/1/2 = NVIDIA's Model A/B/C
  int preset = 2;                 // DLSSNR.Hint.Render.Preset: 0 default, 1..3 = A..C; NVIDIA uses B
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
  // R8_UNORM, 1 where the HUD was drawn, same size as depth; returned to hud_mask_state. Optional.
  // The model is told to leave those pixels alone (its control mask is 1 - this).
  ID3D12Resource* hud_mask = nullptr; uint32_t hud_mask_state = 0;
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
// Turns the pass off for the rest of the session with a reason for the log (the renderer found its
// commands refused by the device).
void disable(const char* why);
// One line for the settings panel: running, or why not.
const char* status();
// A DLSS 5 model file (nvngx_dlssnr.dll) is beside the game, in its DLSS5 folder, or in the driver.
bool model_found();
// The model's own GPU memory in bytes as NVIDIA's runtime reports it, 0 when unknown.
unsigned long long vram_bytes();
// True when the model runs through NVIDIA's published route (not the direct fallback).
bool official_route();
// Releases the model's feature and scratch textures. The GPU must be idle.
void shutdown();

}  // namespace dlss5
}  // namespace gx
