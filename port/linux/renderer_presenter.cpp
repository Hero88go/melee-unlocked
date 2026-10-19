// Linux presentation is independent of 60 Hz game simulation. Complete source
// frames cross a bounded queue; the existing solver re-evaluates authored tracks.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "renderer_presenter.h"
#include "frame_queue.h"
#include "subframe.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace linux_renderer {
namespace {
using Clock = std::chrono::steady_clock;
double seconds() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }
class Presenter final : public gx::Backend {
 public:
  Presenter(SDL_Window* window, std::filesystem::path cache, int scale, double fps, bool vsync) {
    if (!std::isfinite(fps) || fps < 1 || fps > 1000) throw std::runtime_error("Presentation FPS must be between 1 and 1000");
    std::promise<void> ready;
    auto initialized = ready.get_future();
    worker_ = std::thread([this, window, cache = std::move(cache), scale, fps, vsync, ready = std::move(ready)]() mutable {
      bool started = false;
      try {
        auto renderer = create(window, cache, scale, vsync);
        ready.set_value(); started = true;
        loop(*renderer, fps);
      } catch (...) {
        const auto error = std::current_exception();
        { std::lock_guard<std::mutex> lock(error_mutex_); failure_ = error; }
        if (!started) ready.set_exception(error);
        fail_capture(error);
        queue_.finish(true);
      }
    });
    try { initialized.get(); }
    catch (...) { queue_.finish(true); worker_.join(); throw; }
  }
  ~Presenter() override { queue_.finish(); if (worker_.joinable()) worker_.join(); }
  void submit_frame(const gx::Frame& frame) override {
    rethrow_failure();
    if (!queue_.push(frame)) { rethrow_failure(); throw std::runtime_error("Linux presenter stopped"); }
  }
  void submit_and_recycle(gx::Frame& frame) override {
    rethrow_failure();
    if (!queue_.push_and_recycle(frame)) { rethrow_failure(); throw std::runtime_error("Linux presenter stopped"); }
  }
  void presentation_stats(uint32_t* frames, uint32_t* pipes, uint32_t* textures) const override {
    if (frames) *frames = presented_.load();
    if (pipes) *pipes = pipelines_.load();
    if (textures) *textures = textures_.load();
  }
  PresenterStats statistics() const { return {sources_.load(),presented_.load(),subframes_.load(),posed_.load(),drained_.load()}; }
  void capture(const std::filesystem::path& path) {
    rethrow_failure();
    auto task = std::make_unique<Capture>(); task->path = path;
    auto done = task->done.get_future();
    {
      std::lock_guard<std::mutex> lock(capture_mutex_);
      if (capture_) throw std::runtime_error("A Linux frame capture is already pending");
      capture_ = std::move(task);
    }
    // Recheck after posting in case the worker failed between the first check
    // and the mailbox write; a dead worker cannot complete the request.
    try { rethrow_failure(); } catch (...) { fail_capture(std::current_exception()); }
    done.get();
  }
  void synchronize() { capture({}); }
 private:
  struct Capture { std::filesystem::path path; std::promise<void> done; };
  void fail_capture(std::exception_ptr error) {
    std::unique_ptr<Capture> task;
    { std::lock_guard<std::mutex> lock(capture_mutex_); task = std::move(capture_); }
    if (task) task->done.set_exception(error);
  }
  void service_capture(gx::Backend& renderer, const gx::Frame* frame = nullptr) {
    if (queue_.size()) return;
    std::unique_ptr<Capture> task;
    { std::lock_guard<std::mutex> lock(capture_mutex_); task = std::move(capture_); }
    if (task) {
      try {
        if (!task->path.empty()) {
          capture_efb(renderer, task->path);
          if (frame && std::getenv("MELEE_GL_COMPARE_CULLING")) capture_culling_variants(renderer, *frame, task->path);
        }
        task->done.set_value();
      }
      catch (...) { task->done.set_exception(std::current_exception()); }
    }
  }
  void rethrow_failure() const { std::lock_guard<std::mutex> lock(error_mutex_); if (failure_) std::rethrow_exception(failure_); }
  void loop(gx::Backend& renderer, double fps) {
    constexpr double period = 1.0 / 60.0;
    const bool unlocked = fps > 60.5;
    gx::SubFrameSolver solver;
    std::vector<gx::DrawMatrices> overrides;
    gx::Frame frames[2]; int current = -1; bool previous = false;
    uint64_t rendered = 0;
    double next_present = seconds(), last_report = next_present;
    while (true) {
      gx::Frame incoming;
      bool fresh = false;
      if ((current < 0 || frames[current].sequence == rendered) && queue_.try_pop(incoming)) {
        const int next = current < 0 ? 0 : current ^ 1;
        queue_.recycle(std::move(frames[next])); frames[next] = std::move(incoming);
        previous = current >= 0 && !frames[next].discontinuous;
        current = next; fresh = true; ++sources_;
        if (unlocked) solver.set_frames(previous ? &frames[current ^ 1] : nullptr, &frames[current]);
      }
      if (current < 0) {
        service_capture(renderer);
        if (queue_.drained()) break;
        queue_.wait_available(std::chrono::milliseconds(2)); continue;
      }
      auto& frame = frames[current];
      if (fresh && queue_.size()) {
        renderer.set_skip_present(true); renderer.submit_frame(frame); renderer.set_skip_present(false);
        rendered = frame.sequence; ++drained_; continue;
      }
      const bool first = rendered != frame.sequence;
      if (!first) service_capture(renderer, &frame);
      if (!first && queue_.drained()) break;
      if (!unlocked && !first) { queue_.wait_available(std::chrono::milliseconds(2)); continue; }
      double now = seconds();
      if (now < next_present) {
        // Arrival wakes the clock so a new source is adopted before drawing;
        // rendering never holds the simulation thread at the monitor's rate.
        if (queue_.size()) continue;
        const double wait = std::min(next_present - now, 0.002);
        if (wait > 0.0002) queue_.wait_available(std::chrono::duration<double>(wait));
        else std::this_thread::yield();
        continue;
      }
      const bool menus = !gx::frame_in_match(frame);
      if (unlocked && previous) {
        solver.set_menu_mode(menus);
        const double phase = std::clamp((now - frame.time) / period, 0.0, 1.0);
        solver.build(phase, menus, overrides, true);
        renderer.submit_frame(frame, overrides.data());
        ++subframes_; posed_ += solver.stats().posed;
      } else renderer.submit_frame(frame);
      rendered = frame.sequence; ++presented_;
      service_capture(renderer, &frame);
      uint32_t pf = 0, pp = 0, pt = 0; renderer.presentation_stats(&pf, &pp, &pt); pipelines_ = pp; textures_ = pt;
      next_present += 1.0 / fps;
      const double finished = seconds();
      if (next_present < finished - 1.0 / fps) next_present = finished;
      if (finished - last_report >= 5) {
        const auto s = statistics();
        std::fprintf(stderr, "Linux presenter: %llu source, %llu display, %llu authored subframes, %llu re-posed draws, %llu drained\n",
          static_cast<unsigned long long>(s.simulation), static_cast<unsigned long long>(s.presentation), static_cast<unsigned long long>(s.subframes), static_cast<unsigned long long>(s.posed), static_cast<unsigned long long>(s.drained));
        last_report = finished;
      }
    }
  }
  gx::FrameQueue queue_; std::thread worker_;
  mutable std::mutex error_mutex_; std::exception_ptr failure_;
  std::mutex capture_mutex_; std::unique_ptr<Capture> capture_;
  std::atomic<uint64_t> sources_{0}, presented_{0}, subframes_{0}, posed_{0}, drained_{0};
  std::atomic<uint32_t> pipelines_{0}, textures_{0};
};
} // namespace
std::unique_ptr<gx::Backend> create_threaded(SDL_Window* window, const std::filesystem::path& cache, int scale, double fps, bool vsync) {
  gx::set_native_pose_capture_enabled(fps > 60.5); gx::set_authored_capture_wanted(fps > 60.5);
  return std::make_unique<Presenter>(window, cache, scale, fps, vsync);
}
PresenterStats presenter_stats(const gx::Backend& backend) { return dynamic_cast<const Presenter&>(backend).statistics(); }
bool capture_threaded(gx::Backend& backend, const std::filesystem::path& path) {
  if (auto* presenter = dynamic_cast<Presenter*>(&backend)) { presenter->capture(path); return true; }
  return false;
}

bool presenter_self_test(SDL_Window* window, const std::filesystem::path& cache) {
  auto backend = create_threaded(window, cache, 1, 240, false);
  double start = 0;
  PresenterStats baseline;
  for (unsigned source = 0; source < 9; ++source) {
    const bool warmup = source == 0;
    const unsigned tick = warmup ? 0 : source - 1;
    gx::Frame frame; frame.sequence = source + 1; frame.time = warmup ? seconds() : start + tick / 60.0;
    frame.scene_major = 2; frame.scene_minor = 2;
    gx::DrawCall draw{}; draw.identity = 1; draw.object_generation = 1; draw.primitive = 0x90; draw.vertex_count = 3;
    draw.components = gx::VB_HAS_COL0;
    draw.bp.reg[gx::BP_BLENDMODE] = 8 | 16;
    draw.bp.reg[gx::BP_SCISSORTL] = (342 << 12) | 342; draw.bp.reg[gx::BP_SCISSORBR] = (981 << 12) | 869;
    draw.bp.reg[gx::BP_SCISSOROFFSET] = 171 | (171 << 10);
    draw.bp.reg[gx::BP_TEV_COLOR_ENV] = (15 << 12) | (15 << 8) | (15 << 4) | 10 | (1 << 19);
    draw.bp.reg[gx::BP_TEV_ALPHA_ENV] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
    draw.bp.reg[gx::BP_ALPHACOMPARE] = (7 << 16) | (7 << 19);
    draw.bp.reg[gx::BP_TEV_KSEL] = 1 << 2; draw.bp.reg[gx::BP_TEV_KSEL + 1] = 2 | (3 << 2);
    const float viewport[] = {320,-264,16777216,662,606,16777216};
    const float projection[] = {1,0,1,0,-1.01f,-0.202f,0};
    std::memcpy(&draw.xf_regs[0x1A], viewport, sizeof viewport);
    std::memcpy(&draw.xf_regs[0x20], projection, sizeof projection);
    draw.posMatrices[0] = draw.posMatrices[5] = draw.posMatrices[10] = 1;
    draw.normalMatrices[0] = draw.normalMatrices[4] = draw.normalMatrices[8] = 1;
    draw.posMatrices[3] = tick * 0.1f;
    auto pose = std::make_shared<gx::AuthoredPose>(); gx::AuthoredJoint joint;
    joint.generation = 1; joint.scale = {1,1,1}; joint.frame = tick; joint.rate = 1; joint.end = 20;
    joint.translation[0] = tick * 0.1f; joint.world = {1,0,0,tick * 0.1f,0,1,0,0,0,0,1,0};
    NativeMelee::PackedTrack track; track.channel = 5; track.value_format = 128;
    // Console FObj linear packet: x=0 at time 0, x=2 at time 20.
    track.bytes = {0x12,0,20,2}; joint.tracks.push_back(track); pose->joints.push_back(joint);
    draw.authored_pose = pose;
    const float positions[3][3] = {{-9,-7,-10},{-3,-7,-10},{-6,1,-10}};
    for (int i = 0; i < 3; ++i) {
      gx::Vertex vertex{}; std::memcpy(vertex.pos, positions[i], sizeof vertex.pos);
      vertex.col0[i] = 255; vertex.col0[3] = 255; frame.vertices.push_back(vertex);
    }
    frame.draws.push_back(std::move(draw)); frame.commands.push_back({gx::FrameCommand::Draw, 0});
    gx::EfbCopy copy{}; copy.to_xfb = true; copy.src_w = 640; copy.src_h = 528;
    copy.clear = true; copy.clear_z = 0xFFFFFF; copy.clear_color = 0xFF000000;
    frame.copies.push_back(copy); frame.commands.push_back({gx::FrameCommand::Copy,0});
    backend->submit_and_recycle(frame);
    if (warmup) {
      // A cold HLSL->SPIR-V->GLSL compile can exceed the entire fixture's
      // measurement span. Complete the same shader and GPU path first, then
      // start the independent clocks with a clean statistics baseline.
      dynamic_cast<Presenter&>(*backend).synchronize();
      baseline = presenter_stats(*backend); start = seconds(); continue;
    }
    std::this_thread::sleep_until(Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(start + (tick + 1) / 60.0))));
  }
  dynamic_cast<Presenter&>(*backend).synchronize();
  auto stats = presenter_stats(*backend);
  stats.simulation -= baseline.simulation; stats.presentation -= baseline.presentation;
  stats.subframes -= baseline.subframes; stats.posed -= baseline.posed;
  if (stats.simulation != 8 || stats.presentation <= stats.simulation || !stats.subframes || !stats.posed)
    throw std::runtime_error("Linux authored presenter self-test did not produce sampled subframes");
  std::fprintf(stderr, "Linux authored presenter self-test: %llu source, %llu display, %llu re-posed draws\n",
    static_cast<unsigned long long>(stats.simulation), static_cast<unsigned long long>(stats.presentation), static_cast<unsigned long long>(stats.posed));
  return true;
}
} // namespace linux_renderer
