#ifdef __APPLE__
// One translation worker, no GL context or game-state access. Header-only for both Mac hosts.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
namespace shader_translation {
inline std::string read_text(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}
inline void write_text(const std::filesystem::path& path, const std::string& text) {
  std::ofstream f(path, std::ios::binary);
  f.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!f) throw std::runtime_error("Cannot write shader cache: " + path.string());
}
inline void replace_all(std::string& text, const std::string& from, const std::string& to) {
  size_t at = 0;
  while ((at = text.find(from, at)) != std::string::npos) { text.replace(at, from.size(), to); at += to.size(); }
}
inline void run_tool(std::vector<std::string> args, const std::filesystem::path& log, const std::filesystem::path& tools) {
  // Prefer shipped tools. M2's copied Homebrew glslang has a relative dylib
  // rpath that breaks after copying; only that dyld failure falls back locally.
  std::vector<std::string> candidates;
  std::error_code ec;
  const auto shipped = tools / args[0];
  if (std::filesystem::is_regular_file(shipped, ec)) candidates.push_back(shipped.string());
  const std::string homebrew = "/opt/homebrew/bin/" + args[0];
  if (std::filesystem::is_regular_file(homebrew, ec)) candidates.push_back(homebrew);
  candidates.push_back(args[0]); // PATH is the final fallback.
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  for (size_t index = 0; index < candidates.size(); ++index) {
    argv[0] = candidates[index].data();
    posix_spawn_file_actions_t files;
    posix_spawn_file_actions_init(&files);
    posix_spawn_file_actions_addopen(&files, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_adddup2(&files, STDOUT_FILENO, STDERR_FILENO);
    pid_t child = 0;
    const int error = posix_spawnp(&child, argv[0], &files, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&files);
    if (error) {
      if ((error == ENOENT || error == ENOEXEC) && index + 1 < candidates.size()) continue;
      throw std::runtime_error("Cannot run " + candidates[index] + ": " + std::strerror(error));
    }
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
      const pid_t waited = waitpid(child, &status, WNOHANG);
      if (waited == child) break;
      if (waited < 0 && errno != EINTR) throw std::runtime_error("waitpid failed for shader compiler");
      if (std::chrono::steady_clock::now() >= deadline) {
        kill(child, SIGKILL); // Only the helper PID this call spawned.
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        throw std::runtime_error("Shader helper timed out after 15s; killed owned PID " + std::to_string(child));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (WIFEXITED(status) && !WEXITSTATUS(status)) return;
    const auto failure = read_text(log);
    if (index + 1 < candidates.size() && failure.find("dyld[") != std::string::npos &&
        failure.find("Library not loaded:") != std::string::npos) {
      static bool warned = false;
      if (!warned) {
        std::fprintf(stderr, "Mac shaders: copied tool lacks dylibs; using local Homebrew/PATH fallback\n");
        warned = true;
      }
      continue;
    }
    throw std::runtime_error(candidates[index] + " failed:\n" + failure);
  }
}


// This function only generates files and runs helpers; it never calls OpenGL.
inline std::string translate(const std::filesystem::path& cache, const std::string& name,
                             bool vertex, int height, const std::string& hlsl,
                             const std::filesystem::path& tools) {
  const auto prefix = cache / (name + "." + std::to_string(getpid()));
  const std::string input = prefix.string() + ".hlsl", spv = prefix.string() + ".spv", translated = prefix.string() + ".glsl";
  const std::filesystem::path log = prefix.string() + ".log";
  std::string source = hlsl;
  if (!vertex) {
    // Every index is constant in the GX generator. Name each resource
    // explicitly so SPIRV-Cross can combine texture and sampler objects;
    // GLSL cannot combine arrays of separate samplers.
    std::string samplers, textures;
    for (unsigned i = 0; i < 8; ++i) {
      samplers += "SamplerState samp" + std::to_string(i) + " : register(s" + std::to_string(i) + ");\n";
      textures += "Texture2D Tex" + std::to_string(i) + " : register(t" + std::to_string(i) + ");\n";
    }
    replace_all(source, "SamplerState samp[8] : register(s0);", samplers);
    replace_all(source, "Texture2D Tex[8] : register(t0);", textures);
    for (unsigned i = 0; i < 8; ++i) {
      replace_all(source, "Tex[" + std::to_string(i) + "]", "Tex" + std::to_string(i));
      replace_all(source, "samp[" + std::to_string(i) + "]", "samp" + std::to_string(i));
    }
    // OpenGL's fragment coordinate is measured from the bottom; GX and the shared HLSL use the top.
    const auto at = source.find("int2 ditherindex");
    if (at != std::string::npos) source.insert(at, "rawpos.y = " + std::to_string(height) + ".0 - rawpos.y;\n");
  }
  write_text(input, source);
  run_tool({"glslangValidator", "-D", "-V", "--auto-map-locations", "--auto-map-bindings", "--hlsl-iomap", "-e", "main", "-S", vertex ? "vert" : "frag", "-o", spv, input}, log, tools);
  std::vector<std::string> args{"spirv-cross", spv, "--version",
    "410", "--no-420pack-extension",
    "--no-es", "--output", translated};
  if (vertex) args.push_back("--fixup-clipspace");
  // macOS's GL 4.1 driver links stage outputs to inputs by name, not location: give both sides
  // the same name per location (unused locations are ignored).
  for (int location = 0; location < 32; ++location) {
    args.insert(args.end(), {"--rename-interface-variable", vertex ? "out" : "in",
                             std::to_string(location), "yg_vary" + std::to_string(location)});
  }
  run_tool(std::move(args), log, tools);
  std::string glsl = read_text(translated);
  if (glsl.empty()) throw std::runtime_error("SPIRV-Cross produced an empty shader");
  std::filesystem::rename(translated, cache / (name + ".glsl"));
  std::filesystem::remove(input); std::filesystem::remove(spv); std::filesystem::remove(log);
  return glsl;
}

struct Result {
  enum State { Pending, Ready, Failed };
  std::atomic<State> state{Pending};
  std::string glsl, error; // Written once before the release-store, read after acquire-load.
};
class Queue {
 public:
  ~Queue() { stop(); }
  // Called only by the frame thread. A stage is read/queued once per renderer, including failures.
  std::shared_ptr<Result> request(const std::filesystem::path& path, std::function<std::string()> translate) {
    const auto key = path.string();
    if (auto found = results_.find(key); found != results_.end()) return found->second;
    auto result = std::make_shared<Result>();
    result->glsl = read_text(path);
    if (!result->glsl.empty()) {
      ++cache_hits_;
      result->state.store(Result::Ready, std::memory_order_release);
    }
    else {
      ++translation_misses_;
      std::lock_guard<std::mutex> lock(mutex_);
      if (!worker_.joinable()) worker_ = std::thread([this] { work(); });
      jobs_.push_back({result, std::move(translate)});
      wake_.notify_one();
    }
    results_.emplace(key, result);
    return result;
  }
  size_t cache_hits() const { return cache_hits_; }
  size_t translation_misses() const { return translation_misses_; }
  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      for (auto& job : jobs_) {
        job.result->error = "translation cancelled at shutdown";
        job.result->state.store(Result::Failed, std::memory_order_release);
      }
      jobs_.clear();
    }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
  }
 private:
  struct Job { std::shared_ptr<Result> result; std::function<std::string()> translate; };
  void work() {
    for (;;) {
      Job job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
        if (stopping_) return;
        job = std::move(jobs_.front()); jobs_.pop_front();
      }
      // No queue lock is held during filesystem I/O, a helper, or its waitpid.
      try {
        job.result->glsl = job.translate();
        if (job.result->glsl.empty()) throw std::runtime_error("empty translated shader");
        job.result->state.store(Result::Ready, std::memory_order_release);
      } catch (const std::exception& error) {
        job.result->error = error.what();
        std::fprintf(stderr, "opengl: async translation failed: %s\n", error.what());
        job.result->state.store(Result::Failed, std::memory_order_release);
      }
    }
  }
  size_t cache_hits_ = 0, translation_misses_ = 0; // frame-thread counters; one per unique stage
  std::unordered_map<std::string, std::shared_ptr<Result>> results_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Job> jobs_;
  bool stopping_ = false;
  std::thread worker_;
};
} // namespace shader_translation

#endif  // __APPLE__
