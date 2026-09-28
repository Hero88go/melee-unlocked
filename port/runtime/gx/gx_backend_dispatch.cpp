// Backend-neutral dispatch: a future renderer never gets cast to a D3D type.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_backend.h"

namespace gx {
const RenderOptions& render_options(Backend* backend) {
  static const RenderOptions defaults{};
  const auto* options = backend ? backend->presentation_options() : nullptr;
  return options ? *options : defaults;
}
void render_resize(Backend* backend, int w, int h) {
  if (backend) backend->resize(w, h);
}
void render_stats(Backend* backend, uint32_t* frames, uint32_t* pipelines, uint32_t* textures) {
  if (backend) backend->presentation_stats(frames, pipelines, textures);
  else {
    if (frames) *frames = 0;
    if (pipelines) *pipelines = 0;
    if (textures) *textures = 0;
  }
}
std::string render_profile_line(Backend* backend) {
  return backend ? backend->profile_line() : std::string{};
}
} // namespace gx
