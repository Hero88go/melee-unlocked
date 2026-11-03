// Exercises the production translation queue and helpers without SDL, GL, a game, or sockets.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "../linux/shader_translation.h"
#include <future>
#include <iostream>
using namespace shader_translation;
using Clock = std::chrono::steady_clock;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void completed(const std::shared_ptr<Result>& result) {
  const auto deadline = Clock::now() + std::chrono::seconds(35);
  while (result->state.load(std::memory_order_acquire) == Result::Pending && Clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  require(result->state.load(std::memory_order_acquire) != Result::Pending, "worker did not finish");
}
int main(int argc, char** argv) {
  try {
    require(argc == 3, "usage: translation-check <scratch-dir> <tool-dir>");
    const std::filesystem::path scratch = argv[1], tools = argv[2];
    const auto async_cache = scratch / "async", sync_cache = scratch / "sync";
    std::filesystem::create_directories(async_cache); std::filesystem::create_directories(sync_cache);
    const std::string vertex = R"(
struct Output { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Output main(float3 position : POSITION) { Output o; o.position=float4(position,1); o.uv=position.xy; return o; }
)";
    const std::string pixel = R"(
SamplerState samp[8] : register(s0);
Texture2D Tex[8] : register(t0);
float4 main(float2 uv : TEXCOORD0) : SV_Target0 { return Tex[7].Sample(samp[7], uv); }
)";
    Queue queue;
    std::promise<void> release, entered;
    auto gate = release.get_future().share();
    const auto caller = std::this_thread::get_id();
    std::atomic<unsigned> calls{0};
    auto blocked = queue.request(async_cache / "blocked.glsl", [&] {
      require(std::this_thread::get_id() != caller, "translation ran on caller");
      ++calls; entered.set_value(); gate.wait(); return "ready";
    });
    entered.get_future().wait();
    const auto start = Clock::now();
    auto duplicate = queue.request(async_cache / "blocked.glsl", [] { return "incorrect"; });
    auto vs = queue.request(async_cache / "vs-fixture.glsl", [&] {
      ++calls; return translate(async_cache, "vs-fixture", true, 528, vertex, tools);
    });
    auto ps = queue.request(async_cache / "ps-fixture.glsl", [&] {
      ++calls; return translate(async_cache, "ps-fixture", false, 528, pixel, tools);
    });
    auto failed = queue.request(async_cache / "failed.glsl", []() -> std::string { throw std::runtime_error("expected failure fixture"); });
    auto after_failure = queue.request(async_cache / "after-failure.glsl", [] { return "survived"; });
    write_text(async_cache / "cached.glsl", "cached shader");
    auto cached = queue.request(async_cache / "cached.glsl", []() -> std::string { throw std::runtime_error("cache hit translated"); });
    const double enqueue_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    const bool returned_without_waiting = duplicate == blocked && calls == 1 &&
        vs->state.load() == Result::Pending && ps->state.load() == Result::Pending &&
        cached->state.load(std::memory_order_acquire) == Result::Ready && cached->glsl == "cached shader";
    release.set_value();
    require(returned_without_waiting, "queue waited, duplicated work, or did not serve a cache hit");
    for (auto& result : {blocked, vs, ps, failed, after_failure}) completed(result);
    require(vs->state == Result::Ready && ps->state == Result::Ready, "real helper translation failed");
    require(failed->state == Result::Failed && !failed->error.empty(), "worker lost failure");
    require(after_failure->state == Result::Ready && after_failure->glsl == "survived", "failure stopped queue");
    require(calls == 3, "duplicate translation ran");
    require(queue.cache_hits() == 1 && queue.translation_misses() == 5, "cache counters include duplicates or miss unique stages");
    const auto sync_vertex = translate(sync_cache, "vs-fixture", true, 528, vertex, tools);
    const auto sync_pixel = translate(sync_cache, "ps-fixture", false, 528, pixel, tools);
    require(vs->glsl == sync_vertex && ps->glsl == sync_pixel, "async and sync GLSL differ");
    require(read_text(async_cache / "vs-fixture.glsl") == vs->glsl &&
            read_text(async_cache / "ps-fixture.glsl") == ps->glsl, "published cache differs");
    require(ps->glsl.find("Tex7") != std::string::npos, "texture/sampler translation missing");
    queue.stop();
    std::cout << "Translation checks: 7/7 passed (nonblocking, dedup, cache hit, failure, continuation, sync equality, atomic cache)\n"
              << "Caller queued/checked jobs in " << enqueue_ms << " ms while worker was blocked; 2 real shader stages matched sync exactly\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
