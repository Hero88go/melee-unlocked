#pragma once
#include "gx_core.h"
#include "render_options.h"
namespace gx {
// Window and D3D12 resources belong to the worker; simulation supplies owned frames.
std::unique_ptr<Backend> create_threaded_backend(const RenderOptions& options, bool visible);
}
