// SPDX-License-Identifier: GPL-2.0-or-later
#include "threaded_backend.h"
#include "frame_queue.h"
#include "host.h"
#include "window.h"
#include <future>
#include <thread>
namespace gx {
namespace {
class ThreadedBackend final : public Backend {
  FrameQueue queue;
  std::thread worker;
public:
  ThreadedBackend(D3D12Options options, bool visible) {
    std::promise<void> initialized;
    auto ready = initialized.get_future();
    worker = std::thread([this, options, visible, init = std::move(initialized)]() mutable {
      bool started = false;
      try {
        void* window = host::window_create(options.window_w, options.window_h, L"Melee Port (development)", visible);
        std::unique_ptr<Backend> renderer(create_d3d12_backend(window, options.window_w, options.window_h, options));
        host::window_set_resize_callback([&renderer](int w, int h) { d3d12_resize(renderer.get(), w, h); });
        init.set_value(); started = true;
        uint64_t submitted = 0;
        for (;;) {
          host::window_pump();
          if (host::window_closed()) { queue.finish(true); break; }
          Frame frame;
          if (queue.pop(frame)) { renderer->submit_frame(frame); ++submitted; }
          else if (queue.drained()) break;
        }
        host::log("renderer: drained %llu source frames on its own thread", submitted);
        host::window_set_resize_callback({});
        renderer.reset();
        host::window_destroy();
      } catch (...) {
        if (!started) init.set_exception(std::current_exception());
        else host::request_exit(3);
        queue.finish(true);
      }
    });
    try { ready.get(); }
    catch (...) { queue.finish(true); worker.join(); throw; }
  }
  ~ThreadedBackend() override { queue.finish(); worker.join(); }
  void submit_frame(const Frame& frame) override {
    if (!queue.push(frame)) throw ExitRequested{host::exit_code()};
  }
};
}
std::unique_ptr<Backend> create_threaded_backend(const D3D12Options& options, bool visible) {
  return std::make_unique<ThreadedBackend>(options, visible);
}
}
