// Native Linux OpenGL backend for the shared Melee GX command decoder.
// The generated GX shaders remain the authoritative TEV/lighting implementation:
// glslang and SPIRV-Cross translate them to GLSL, with persistent caching.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "test_env.h"
#include "renderer_gl.h"
#include "../runtime/hle/yougame_parity.h"
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "../runtime/hle/yougame_agent_present.h"
#endif
#ifdef MELEE_LINUX
#include "../runtime/host/window.h"
#endif
#ifdef MELEE_LINUX_UNLOCKED
#include "renderer_presenter.h"
#endif
#include "gx_shader.h"
#include "gx_texture.h"
#ifdef GX_PC_SETTINGS   // the full game (port/linux/full): texture packs and F2 captures
#include "texture_pack.h"
#include "video_background.h"
#include "video_background_learning.h"
#include "user_shader.h"
#endif
#include "crt_shader.h"
#include "gx_retro.h"
#ifdef __APPLE__
#include "shader_translation.h"
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#include "../mac/platform_paths.h"
#else
#include <GL/glew.h>
#endif
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#ifdef __APPLE__
#include <unordered_set>
#endif
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#ifdef __APPLE__
namespace host { void log(const char* fmt, ...); }   // melee.log (--log-file)
#endif
namespace linux_renderer {
namespace {
// Keep shader progress visible both in standalone stderr and the app's melee.log.
void shader_log(const char* fmt, ...) {
  char line[2048];
  va_list args; va_start(args, fmt);
  std::vsnprintf(line, sizeof line, fmt, args); va_end(args);
  std::fprintf(stderr, "%s\n", line);
#ifdef __APPLE__
  host::log("%s", line);
#endif
}
#ifdef __APPLE__
using shader_translation::read_text;
using shader_translation::replace_all;
#else
std::string read_text(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}
void write_text(const std::filesystem::path& path, const std::string& text) {
  std::ofstream f(path, std::ios::binary);
  f.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!f) throw std::runtime_error("Cannot write shader cache: " + path.string());
}
void replace_all(std::string& text, const std::string& from, const std::string& to) {
  size_t at = 0;
  while ((at = text.find(from, at)) != std::string::npos) { text.replace(at, from.size(), to); at += to.size(); }
}
void run_tool(std::vector<std::string> args, const std::filesystem::path& log) {
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t files;
  posix_spawn_file_actions_init(&files);
  posix_spawn_file_actions_addopen(&files, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  posix_spawn_file_actions_adddup2(&files, STDOUT_FILENO, STDERR_FILENO);
  // The release ships the shader tools in bin/ beside the program; a system copy is the fallback.
  std::string shipped;
  {
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
      const auto candidate = exe.parent_path() / "bin" / args[0];
      if (std::filesystem::is_regular_file(candidate, ec)) { shipped = candidate.string(); argv[0] = shipped.data(); }
    }
  }
  pid_t child = 0;
  const int error = posix_spawnp(&child, argv[0], &files, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&files);
  if (error) throw std::runtime_error("Cannot run " + args[0] + ": " + std::strerror(error));
  int status = 0;
  while (waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) throw std::runtime_error("waitpid failed for shader compiler");
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status))
    throw std::runtime_error(args[0] + " failed:\n" + read_text(log));
}
// HLSL to GLSL through the shader tools (glslangValidator, SPIRV-Cross): two process runs, tens of
// milliseconds or more. The result lands in `output` (the disk cache). Any thread.
void translate_shader(GLenum stage, const std::string& source, const std::filesystem::path& prefix,
                      const std::filesystem::path& output) {
  const std::string input = prefix.string() + ".hlsl", spv = prefix.string() + ".spv", translated = prefix.string() + ".glsl";
  const std::filesystem::path log = prefix.string() + ".log";
  write_text(input, source);
  run_tool({"glslangValidator", "-D", "-V", "--auto-map-locations", "--auto-map-bindings", "--hlsl-iomap", "-e", "main", "-S", stage == GL_VERTEX_SHADER ? "vert" : "frag", "-o", spv, input}, log);
  std::vector<std::string> args{"spirv-cross", spv, "--version", "450", "--no-es", "--output", translated};
  if (stage == GL_VERTEX_SHADER) args.push_back("--fixup-clipspace");
  run_tool(std::move(args), log);
  if (read_text(translated).empty()) throw std::runtime_error("SPIRV-Cross produced an empty shader");
  std::filesystem::rename(translated, output);
  std::error_code ec;
  std::filesystem::remove(input, ec); std::filesystem::remove(spv, ec); std::filesystem::remove(log, ec);
}
#endif
GLuint compile_gl(GLenum stage, const std::string& source) {
#ifdef __APPLE__
  std::string compatible = source;
  replace_all(compatible, "#version 450 core", "#version 410 core");
  const char* data = compatible.c_str();
#endif
  GLuint shader = glCreateShader(stage);
#ifndef __APPLE__
  const char* data = source.c_str();
#endif
  glShaderSource(shader, 1, &data, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint count = 0; glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &count);
    std::string log(std::max(count, 1), '\0');
    glGetShaderInfoLog(shader, count, nullptr, log.data()); glDeleteShader(shader);
    throw std::runtime_error("OpenGL GX shader failed:\n" + log);
  }
  return shader;
}
GLuint link_gl(GLuint vs, GLuint ps, bool retrievable = false) {
  GLuint program = glCreateProgram();
  if (retrievable) glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
  glAttachShader(program, vs); glAttachShader(program, ps); glLinkProgram(program);
  GLint ok = 0; glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint count = 0; glGetProgramiv(program, GL_INFO_LOG_LENGTH, &count);
    std::string log(std::max(count, 1), '\0');
    glGetProgramInfoLog(program, count, nullptr, log.data()); glDeleteProgram(program);
    throw std::runtime_error("OpenGL GX link failed:\n" + log);
  }
  return program;
}
// The draw state a GX program is generated from. Every program a run builds is recorded and the
// next start builds them all before the game runs, so a stage or effect seen once never compiles
// mid-match again (the Direct3D backends do the same with their recipes.bin).
struct ProgramRecipe {
  uint32_t components = 0;
  gx::BPMemory bp{};
  uint32_t xf[0x58]{};
};
constexpr uint64_t RECIPE_MAGIC = 0x3150524C47505847ull;   // "GXPGLRP1"
constexpr size_t RECIPE_LIMIT = 16384;
struct ProgramKey {
  uint64_t vertex, pixel;
  bool operator==(const ProgramKey& other) const { return vertex == other.vertex && pixel == other.pixel; }
};
struct KeyHash { size_t operator()(const ProgramKey& k) const { return k.vertex ^ (k.pixel * 31); } };
struct Texture {
  GLuint id = 0, fbo = 0;
  uint32_t width = 0, height = 0, levels = 1;
  uint64_t used = 0, bytes = 0;
};

#ifdef __APPLE__
extern "C" void mac_gl_update_context();   // port/mac/mac_gl.mm
extern "C" bool mac_gl_surface_size(int* w, int* h);   // port/mac/mac_gl.mm

#endif
// Diagnostic only: CPU wall time at API boundaries, no GPU synchronization.
struct GlProbeScope {
  using Clock = std::chrono::steady_clock;
  double* total; Clock::time_point start;
  explicit GlProbeScope(double* p) : total(p) { if (total) start = Clock::now(); }
  ~GlProbeScope() { if (total) *total += std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
};
struct GlProbe {
  bool on = [] { const char* v = host::test_env::getenv("MELEE_GL_COSTS"); return v && *v == '1'; }();
  double total=0, attrib=0, texture=0, buffers=0, draw=0, copy=0, swap=0, prewarm=0;
  uint64_t draws=0, uploads=0, copy_allocs=0, vs_bytes=0, index_bytes=0;
  double* slot(double& value) { return on ? &value : nullptr; }
  void reset() { total=attrib=texture=buffers=draw=copy=swap=prewarm=0; draws=uploads=copy_allocs=vs_bytes=index_bytes=0; }
};
// Each entry describes a call actually emitted since the last external GL boundary.
// Unknown differs from every value, including zero/disabled and an empty texture.
template <class T> struct GlValue {
  bool known = false;
  T value{};
  bool changed(const T& next) {
    if (known && value == next) return false;
    value = next; known = true; return true;
  }
};
struct EmittedState {
  GlValue<GLuint> program, vao;
  GlValue<GLenum> active_texture;
  std::array<GlValue<GLuint>, 8> textures, samplers;
  GlValue<std::array<GLfloat, 4>> viewport;
  GlValue<std::array<GLdouble, 2>> depth_range;
  GlValue<bool> scissor, blend, depth, cull;
  GlValue<std::array<GLint, 4>> scissor_rect;
  GlValue<std::array<GLenum, 4>> blend_func;
  GlValue<std::array<GLenum, 2>> blend_equation;
  GlValue<std::array<GLboolean, 4>> color_mask;
  GlValue<GLboolean> depth_mask;
  GlValue<GLenum> depth_func, front_face, cull_face;
};
struct ProgramAttributes {
  std::array<GLint, 15> locations;
  GLuint vao = 0;
};
class Renderer final : public gx::Backend {
 public:
  Renderer(SDL_Window* window, const std::filesystem::path& cache, int scale, bool vsync)
#ifdef __APPLE__
      : window_(window), cache_(cache / (
        "gx-gl410-v2-s"
        + std::to_string(std::clamp(scale, 1, 6)))), scale_(std::clamp(scale, 1, 6)),
#else
      : window_(window), cache_(cache / ("gx-gl-v5-s" + std::to_string(std::clamp(scale, 1, 6)))), scale_(std::clamp(scale, 1, 6)),
#endif
        width_(gx::EFB_WIDTH * scale_), height_(gx::EFB_HEIGHT * scale_) {
    std::filesystem::create_directories(cache_);
    context_ = SDL_GL_CreateContext(window_);
    if (!context_) throw std::runtime_error(std::string("SDL OpenGL context: ") + SDL_GetError());
#ifndef __APPLE__
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK || !GLEW_VERSION_4_5) throw std::runtime_error("OpenGL 4.5 is required");
#endif
    glGetError(); // GLEW can probe an invalid legacy enum in a core context.
    SDL_GL_SetSwapInterval(vsync ? 1 : 0);
    std::fprintf(stderr, "Linux GX: OpenGL %s, %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    glGenVertexArrays(1, &vao_); glBindVertexArray(vao_);
    glGenBuffers(1, &vertices_); glGenBuffers(1, &indices_); glGenBuffers(2, uniform_);
    glBindBuffer(GL_UNIFORM_BUFFER, uniform_[0]); glBufferData(GL_UNIFORM_BUFFER, sizeof(gx::VSConstants), nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, uniform_[1]); glBufferData(GL_UNIFORM_BUFFER, sizeof(gx::PSConstants), nullptr, GL_STREAM_DRAW);
    glGenFramebuffers(1, &efb_); glBindFramebuffer(GL_FRAMEBUFFER, efb_);
    glGenTextures(1, &color_); glBindTexture(GL_TEXTURE_2D, color_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
    glGenTextures(1, &depth_); glBindTexture(GL_TEXTURE_2D, depth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width_, height_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("OpenGL EFB is incomplete");
    glClearColor(0, 0, 0, 1); glClearDepth(0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glGenTextures(1, &last_color_); glBindTexture(GL_TEXTURE_2D, last_color_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glGenFramebuffers(1, &last_frame_); glBindFramebuffer(GL_FRAMEBUFFER, last_frame_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, last_color_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("OpenGL capture framebuffer is incomplete");
    GLuint cv = compile_gl(GL_VERTEX_SHADER, R"(#version 450 core
out vec2 uv;
uniform vec4 source_rect;
void main() { vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); gl_Position=vec4(p*2-1,0,1); uv=source_rect.zw+vec2(p.x,1-p.y)*source_rect.xy; }
)");
    GLuint cp = compile_gl(GL_FRAGMENT_SHADER, R"(#version 450 core
in vec2 uv; layout(location=0) out vec4 color;
uniform sampler2D source_texture; uniform int copy_kind;
void main() {
  color=texture(source_texture,uv);
  if(copy_kind==1) color.a=1;
  if(copy_kind==2) { uint z=min(uint((1-color.r)*16777216.0),0xFFFFFFu); color=vec4(vec3((z>>16)&255u,(z>>8)&255u,z&255u)/255.0,1); }
}
)");
    copy_program_ = link_gl(cv, cp); glDeleteShader(cv); glDeleteShader(cp);
    copy_source_ = glGetUniformLocation(copy_program_, "source_texture");
    copy_kind_ = glGetUniformLocation(copy_program_, "copy_kind");
    copy_rect_ = glGetUniformLocation(copy_program_, "source_rect");
    GLint formats = 0; glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
    binaries_ = formats > 0;
#ifdef __APPLE__
    seed_cache();
    boot_prewarm_ = true;
#endif
    prewarm();
#ifdef __APPLE__
    boot_prewarm_ = false;
    std::fprintf(stderr, "opengl: shader translation %s; frame compile budget %.1f ms\n",
                 sync_shaders_ ? "synchronous (diagnostic)" : "asynchronous (one worker)", compile_budget_ms_);
#endif
  }
  ~Renderer() override {
#ifdef YOUGAME_AGENT_TEST_HOOK
    yougame::agent_present::recorder().stop(); // Drain evidence before the host's exit path.
#endif
#ifdef __APPLE__
    translations_.stop(); // Finish the active helper before any captured renderer state is destroyed.
    report_skips(true);
    std::fprintf(stderr, "opengl: shader cache: %zu hits, %zu translation misses (%s)\n",
        sync_shaders_ ? sync_cache_hits_ : translations_.cache_hits(),
        sync_shaders_ ? sync_cache_misses_ : translations_.translation_misses(),
        sync_shaders_ ? "sync" : "async");
#endif
#ifndef __APPLE__
    stop_translators();
#endif
    if (!context_) return;
    save_recipes();
    for (auto& p : attribute_locations_) glDeleteVertexArrays(1, &p.second.vao);
    for (auto& p : programs_) glDeleteProgram(p.second);
    for (auto& p : shaders_) glDeleteShader(p.second);
    for (auto& p : textures_) destroy(p.second);
    for (auto& p : copies_) destroy(p.second);
    for (auto& p : samplers_) glDeleteSamplers(1, &p.second);
    glDeleteProgram(copy_program_); glDeleteTextures(1, &color_); glDeleteTextures(1, &depth_);
    glDeleteTextures(1, &last_color_); glDeleteFramebuffers(1, &last_frame_);
    glDeleteFramebuffers(1, &efb_); glDeleteBuffers(1, &vertices_); glDeleteBuffers(1, &indices_);
    glDeleteBuffers(2, uniform_); glDeleteVertexArrays(1, &vao_); SDL_GL_DeleteContext(context_);
  }
  void presentation_stats(uint32_t* frames, uint32_t* pipes, uint32_t* textures) const override {
    if (frames) *frames = presented_;
    if (pipes) *pipes = programs_.size();
    if (textures) *textures = textures_.size() + copies_.size();
  }
  void set_skip_present(bool skip) override { skip_ = skip; }
  void submit_frame(const gx::Frame& frame) override { submit_frame(frame, nullptr); }
  void set_hooks(Hooks hooks) { hooks_ = std::move(hooks); }
  const gx::RenderOptions* presentation_options() const override { return hooks_.options; }
  void submit_frame(const gx::Frame& frame, const gx::DrawMatrices* overrides) override {
#ifdef MELEE_LINUX_X11_EMBED
    int drawable_w = 0, drawable_h = 0;
    if (hooks_.drawable_size && !hooks_.drawable_size(&drawable_w, &drawable_h)) return;
#endif
    source_retrace_ = frame.source_retrace; // Includes shader work before drawing and after the swap.
#ifdef __APPLE__
    Clock::time_point probe_start;
    if (probe_.on) { probe_.reset(); probe_start = Clock::now(); }
    compile_spent_ms_ = 0;
    drain_programs();
#endif
    if (hooks_.frame_begin) { hooks_.frame_begin(frame); invalidate_state(); }
    widenable_scene_ = gx::frame_has_widenable_scene(frame);   // as the D3D backends: 16:9 reaches the screen
    const int lightweight = hooks_.options ? hooks_.options->lightweight : 0;
    gx::set_lightweight_level(lightweight);
    if (hooks_.options) gx::publish_look(*hooks_.options);
    effects_filter_ = gx::effects_filter(frame, gx::lightweight_effects_level(hooks_.options ? hooks_.options->effects_level : 0, lightweight));   // "Visual effects"
    lightweight_filter_ = gx::lightweight_filter(frame, lightweight);   // "Lightweight"
#ifdef GX_PC_SETTINGS
    if (hooks_.options) {
      // Texture packs switched on or off, or a skin picked on the character select: every cached
      // texture was built with or without its replacement (as the Windows renderers).
      gx::video_bg::set_enabled(hooks_.options->video_backgrounds);
      gx::video_bg::begin_frame(frame.scene_major, frame.scene_minor);
      if (gx::texpack::configure(hooks_.options->custom_textures, hooks_.options->dump_textures) || gx::texpack::take_cosmetics_changed()) {
        invalidate_state();
        for (auto& t : textures_) destroy(t.second);
        textures_.clear();
        replacement_bytes_ = 0;
      }
    }
#endif
    if (hooks_.options) {
      if (hooks_.options->anisotropy != anisotropy_applied_) {
        invalidate_state();
        anisotropy_applied_ = hooks_.options->anisotropy;
        for (auto& smp : samplers_) glDeleteSamplers(1, &smp.second);
        samplers_.clear();
      }
    }
    apply_msaa(hooks_.options ? hooks_.options->msaa : 1);
    ++sequence_; bind_vertex_array(vao_);
    const size_t original_count = frame.vertices.size();
    vertex_offsets_.assign(frame.draws.size(), 0);
    size_t total_count = original_count;
    if (overrides) for (size_t i = 0; i < frame.draws.size(); ++i)
      if (overrides[i].vertices) { vertex_offsets_[i] = total_count; total_count += frame.draws[i].vertex_count; }
    { GlProbeScope vertex_probe(probe_.slot(probe_.buffers));
    if (probe_.on) probe_.vs_bytes = total_count * sizeof(gx::Vertex);
    glBindBuffer(GL_ARRAY_BUFFER, vertices_);
    glBufferData(GL_ARRAY_BUFFER, total_count * sizeof(gx::Vertex), nullptr, GL_STREAM_DRAW);
    if (original_count) glBufferSubData(GL_ARRAY_BUFFER, 0, original_count * sizeof(gx::Vertex), frame.vertices.data());
    if (overrides) for (size_t i = 0; i < frame.draws.size(); ++i)
      if (overrides[i].vertices) glBufferSubData(GL_ARRAY_BUFFER, vertex_offsets_[i] * sizeof(gx::Vertex), frame.draws[i].vertex_count * sizeof(gx::Vertex), overrides[i].vertices);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, uniform_[0]); glBindBufferBase(GL_UNIFORM_BUFFER, 1, uniform_[1]);
    gx::set_main_projection(gx::frame_scene_draw(frame));
    gx::set_css_scene(frame.scene_major == 1 && frame.scene_minor == 0);
#ifdef GX_PC_SETTINGS
    // Menu video backgrounds: the character select's video goes in after Melee's untextured
    // animated backdrop, the stage select's right after the learned backdrop draw (as on Windows).
    const gx::EfbCopy* screen = nullptr;
    for (const auto& copy : frame.copies) if (copy.to_xfb) screen = &copy;
    int fullscreen_slot = -1;
    const std::shared_ptr<const gx::video_bg::Frame> fullscreen = gx::video_bg::fullscreen_frame(&fullscreen_slot);
    const bool css_fullscreen = fullscreen && fullscreen_slot == 0 && screen;
    unsigned leading_untextured = 0;
    bool background_drawn = false;
#endif
    for (const auto& command : frame.commands) {
      if (command.kind == gx::FrameCommand::Draw) {
        const auto& dc = frame.draws.at(command.index);
#ifdef GX_PC_SETTINGS
        if (css_fullscreen && !background_drawn) {
          bool textured = false;
          for (const auto& texture : dc.textures) textured |= texture.used;
          if (!textured) ++leading_untextured;
          else if (gx::video_bg::learning::should_insert_fullscreen_layer(fullscreen_slot, leading_untextured, textured)) {
            draw_video_layer(fullscreen, fullscreen_slot, *screen);
            background_drawn = true;
          }
        }
        draw_video_slot_ = -1;
#endif
        draw(frame, dc, overrides ? overrides + command.index : nullptr, vertex_offsets_[command.index]);
#ifdef GX_PC_SETTINGS
        if (fullscreen && screen && fullscreen_slot == 1 && draw_video_slot_ == 1 && !background_drawn) {
          draw_video_layer(fullscreen, fullscreen_slot, *screen);
          background_drawn = true;
        }
#endif
      } else {
        const auto& c = frame.copies.at(command.index);
        if (c.to_xfb) { if (!skip_) { capture_sequence_ = frame.sequence; present(c, frame); } }
        else copy(c);
        if (c.clear) clear(c);
      }
    }
    trim();
#ifdef __APPLE__
    report_skips(false);
    if (probe_.on) {
      probe_.total = milliseconds(probe_start);
      if (probe_.total > 20 || sequence_ % 60 == 0)
        std::fprintf(stderr, "glcost: seq=%llu scene=%02X:%02X match=%u total=%.3f compile=%.3f attrib=%.3f texture=%.3f buffers=%.3f draw=%.3f copy=%.3f swap=%.3f prewarm=%.3f draws=%llu uploads=%llu copy_allocs=%llu vertex_bytes=%llu index_bytes=%llu programs=%zu pending=%zu\n",
          (unsigned long long)sequence_, frame.scene_major, frame.scene_minor, frame.source_match_frame,
          probe_.total, compile_spent_ms_, probe_.attrib, probe_.texture, probe_.buffers, probe_.draw,
          probe_.copy, probe_.swap, probe_.prewarm, (unsigned long long)probe_.draws,
          (unsigned long long)probe_.uploads, (unsigned long long)probe_.copy_allocs,
          (unsigned long long)probe_.vs_bytes, (unsigned long long)probe_.index_bytes,
          programs_.size(), compile_pending_.size());
    }
#endif
  }
  // --capture-frame / --capture-sim-frame / --capture-every / --capture-burst, and F2 (the next N
  // frames into capture/), as the Windows renderers write them.
  void captures(uint64_t sequence) {
    const gx::RenderOptions* o = hooks_.options;
    const uint32_t n = presented_;
    try {
      if (o && !o->capture_path.empty()) {
        if (o->capture_sim_frame && !capture_frame_ && sequence >= o->capture_sim_frame) capture_frame_ = n;
        const uint32_t at = capture_frame_ ? capture_frame_ : o->capture_frame;
        const bool burst = o->capture_burst && at && n >= at && n < at + o->capture_burst;
        if (n == at && !burst) capture_window(o->capture_path);
        else if (burst || (o->capture_every && n % o->capture_every == 0)) {
          const std::string& base = o->capture_path;
          char suffix[48];
          std::snprintf(suffix, sizeof suffix, "_%05u.ppm", n);
          capture_window(base.substr(0, base.size() > 4 && base.compare(base.size() - 4, 4, ".ppm") == 0 ? base.size() - 4 : base.size()) + suffix);
        }
      }
#ifdef GX_PC_SETTINGS
      if (unsigned want = gx::gx_capture_request()) {
        gx::gx_capture_request_set(want - 1);
        std::filesystem::create_directories("capture");
        char name[64];
        std::snprintf(name, sizeof name, "capture/blink_%05u.ppm", n);
        capture_window(name);
        if (want == 1) std::fprintf(stderr, "capture: wrote the requested frames into capture/\n");
      }
#endif
    } catch (const std::exception& e) {
      std::fprintf(stderr, "capture: %s\n", e.what());
    }
  }
  // The window as it is about to be shown (picture, border art, the settings and practice menus).
  void capture_window(const std::filesystem::path& path) {
    int w = 0, h = 0; SDL_GL_GetDrawableSize(window_, &w, &h);
    if (w <= 0 || h <= 0) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream f(path, std::ios::binary); f << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y) f.write(reinterpret_cast<char*>(pixels.data()) + size_t(y) * w * 3, size_t(w) * 3);
    if (!f) throw std::runtime_error("Cannot write frame capture: " + path.string());
  }
  void capture(const std::filesystem::path& path) {
    resolve_msaa();
    // XFB submission can clear the EFB immediately. Preserve the displayed
    // source before that clear so an end-of-run capture shows the actual frame.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, have_last_ ? last_frame_ : efb_);
    std::vector<uint8_t> pixels(static_cast<size_t>(width_) * height_ * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream f(path, std::ios::binary); f << "P6\n" << width_ << " " << height_ << "\n255\n";
    for (int y = height_ - 1; y >= 0; --y) f.write(reinterpret_cast<char*>(pixels.data()) + y * width_ * 3, width_ * 3);
    if (!f) throw std::runtime_error("Cannot write frame capture: " + path.string());
  }
  bool check_smoke_pixels() {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, efb_);
    auto pixel = [this](int x, int y) {
      std::array<uint8_t, 4> color{};
      glReadPixels(x * scale_, height_ - 1 - y * scale_, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color.data());
      return color;
    };
    const auto background = pixel(0, 0), red = pixel(150, 400), blue = pixel(320, 200), texture = pixel(500, 275);
    auto near = [](uint8_t v, int target) { return std::abs(int(v) - target) <= 3; };
    return background[0] == 0 && background[1] == 0 && background[2] == 0 &&
           red[0] > 180 && red[1] < 50 && red[2] < 50 &&
           blue[2] > 150 && blue[0] < 65 && blue[1] < 65 &&
           near(texture[0], 200) && near(texture[1], 200) && near(texture[2], 200);
  }
  void prepare_smoke_frame(const gx::Frame& frame) {
    // The self-test inspects its first frame. Prepare its shaders synchronously, as startup
    // prewarming does, instead of inspecting draws skipped while translation is still pending.
    struct Restore { bool& flag; bool saved; ~Restore() { flag = saved; } } restore{prewarming_, prewarming_};
    prewarming_ = true;
    for (const auto& dc : frame.draws) program(dc);
  }
  void compare_culling(const gx::Frame& frame, const std::filesystem::path& prefix) {
    const bool saved = clockwise_front_;
    clockwise_front_ = true; submit_frame(frame); capture(prefix.string() + ".cw.ppm");
    clockwise_front_ = false; submit_frame(frame); capture(prefix.string() + ".ccw.ppm");
    clockwise_front_ = saved; submit_frame(frame);
  }
 private:
  // A linked program as the driver stores it; the next start loads it without compiling. A driver
  // update changes the format or rejects the data, and the program is then built from GLSL again.
  GLuint load_binary(const std::filesystem::path& path) {
    const std::string data = read_text(path);
    if (data.size() <= sizeof(GLenum)) return 0;
    GLenum format = 0; std::memcpy(&format, data.data(), sizeof format);
    GLuint p = glCreateProgram();
    glProgramBinary(p, format, data.data() + sizeof format, static_cast<GLsizei>(data.size() - sizeof format));
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (ok) return p;
    glDeleteProgram(p); glGetError();
    std::error_code ec; std::filesystem::remove(path, ec);
    return 0;
  }
  void save_binary(GLuint p, const std::filesystem::path& path) {
    GLint length = 0; glGetProgramiv(p, GL_PROGRAM_BINARY_LENGTH, &length);
    if (length <= 0) return;
    std::string data(sizeof(GLenum) + length, '\0');
    GLenum format = 0;
    glGetProgramBinary(p, length, nullptr, &format, data.data() + sizeof format);
    std::memcpy(data.data(), &format, sizeof format);
    write_cache(path, data);
  }
  // Cache writes never stop the game: a full disk or read-only folder only loses the speedup.
  void write_cache(const std::filesystem::path& path, const std::string& data) {
    const auto partial = path.string() + "." + std::to_string(getpid());
    { std::ofstream f(partial, std::ios::binary); f.write(data.data(), static_cast<std::streamsize>(data.size())); if (!f) return; }
    std::error_code ec; std::filesystem::rename(partial, path, ec);
    if (ec) std::filesystem::remove(partial, ec);
  }
  // MELEE_TEST_SHOT=<dir>:<n>,<n>,...: save the picture presented to the window (after scaling and letterbox) at
  // those presents, as PPM. What the player sees, unlike capture_efb's game frame. <dir>:all instead appends
  // "<present> <hash>" for every present to <dir>/hashes.txt, so two builds' whole sequences can be compared: with
  // --frame-mode off, catch-up after a stall skips presents, so the same present number can show a different frame.
  void test_shot(int w, int h) {
    static std::filesystem::path dir; static std::vector<uint64_t> at; static bool parsed = false, all = false;
    static std::ofstream hashes;
    if (!parsed) {
      parsed = true;
      if (const char* v = host::test_env::getenv("MELEE_TEST_SHOT")) {
        std::string spec(v); const auto colon = spec.rfind(':');
        if (colon != std::string::npos) {
          dir = spec.substr(0, colon);
          if (spec.compare(colon + 1, std::string::npos, "all") == 0) { all = true; hashes.open(dir / "hashes.txt"); }
          else for (size_t i = colon + 1; i < spec.size();) { size_t j = spec.find(',', i); if (j == std::string::npos) j = spec.size();
            at.push_back(std::strtoull(spec.substr(i, j - i).c_str(), nullptr, 10)); i = j + 1; }
        }
      }
    }
    if (!all && (at.empty() || std::find(at.begin(), at.end(), presented_) == at.end())) return;
    std::vector<uint8_t> px(size_t(w) * h * 3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0); glReadBuffer(GL_BACK); glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, efb_);
    if (all) {
      // Flushed per line: an automated run can end without unwinding statics.
      hashes << presented_ << ' ' << std::hex << gx::hash_bytes(px.data(), px.size()) << std::dec << std::endl;
      return;
    }
    std::ofstream f(dir / ("present-" + std::to_string(presented_) + ".ppm"), std::ios::binary);
    f << "P6\n" << w << ' ' << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y) f.write(reinterpret_cast<const char*>(px.data() + size_t(y) * w * 3), std::streamsize(w) * 3);
  }
#ifdef __APPLE__
  static bool parse_recipes(const std::string& data, std::vector<ProgramRecipe>& out) {
    uint64_t header[3]{};
    if (data.size() < sizeof header) return false;
    std::memcpy(header, data.data(), sizeof header);
    const size_t count = static_cast<size_t>(header[1]);
    if (header[0] != RECIPE_MAGIC || count > RECIPE_LIMIT || data.size() != sizeof header + count * sizeof(ProgramRecipe) ||
        gx::hash_bytes(data.data() + sizeof header, count * sizeof(ProgramRecipe)) != header[2]) return false;
    out.resize(count);
    std::memcpy(out.data(), data.data() + sizeof header, count * sizeof(ProgramRecipe));
    return true;
  }
  // MELEE_SHADER_SEED (the Mac bundle's Resources/shader-seed): shaders and program recipes recorded by our own
  // scripted walks, merged into this player's cache before the prewarm, so a first run doesn't translate them in
  // the middle of a frame. The player's own files are never overwritten.
  void seed_cache() {
    const char* root = std::getenv("MELEE_SHADER_SEED");
    if (!root || !*root) return;
    const auto seed = std::filesystem::path(root) / cache_.filename();
    std::error_code ec;
    if (!std::filesystem::is_directory(seed, ec)) {
      shader_log("opengl: no shader seed for %s; missing shaders will translate on demand", cache_.filename().c_str());
      return;
    }
    size_t copied = 0;
    for (const auto& entry : std::filesystem::directory_iterator(seed, ec)) {
      if (entry.path().extension() != ".glsl" || std::filesystem::exists(cache_ / entry.path().filename(), ec)) continue;
      if (std::filesystem::copy_file(entry.path(), cache_ / entry.path().filename(), ec)) ++copied;
    }
    std::vector<ProgramRecipe> mine, seeded;
    parse_recipes(read_text(cache_ / "recipes.bin"), mine);
    parse_recipes(read_text(seed / "recipes.bin"), seeded);
    std::unordered_set<uint64_t> have;
    for (const auto& r : mine) have.insert(gx::hash_bytes(&r, sizeof r));
    size_t added = 0;
    for (const auto& r : seeded)
      if (mine.size() < RECIPE_LIMIT && have.insert(gx::hash_bytes(&r, sizeof r)).second) { mine.push_back(r); ++added; }
    if (added) {
      const size_t bytes = mine.size() * sizeof(ProgramRecipe);
      const uint64_t header[3] = {RECIPE_MAGIC, mine.size(), gx::hash_bytes(mine.data(), bytes)};
      std::string data(sizeof header + bytes, '\0');
      std::memcpy(data.data(), header, sizeof header);
      std::memcpy(data.data() + sizeof header, mine.data(), bytes);
      write_cache(cache_ / "recipes.bin", data);
    }
    shader_log("opengl: shader seed added %zu shaders and %zu program recipes", copied, added);
  }
  // Every recorded program is built again at start. macOS keeps no program binaries, so a big cache (or the shipped
  // seed) would hold the first frame for seconds: there the start builds what fits in MELEE_PREWARM_BOOT_MS (500),
  // the player's own programs first, and the rest are built between frames, MELEE_PREWARM_FRAME_MS (4) per frame.
  // Missing translations are queued; ready GLSL shares the per-frame compile budget. Elsewhere all recipes are visited at start.
  void prewarm() {
    std::vector<ProgramRecipe> list;
    if (!parse_recipes(read_text(cache_ / "recipes.bin"), list)) return;
    for (const auto& r : list)
      if (recipe_hashes_.insert(gx::hash_bytes(&r, sizeof r)).second) recipes_.push_back(r);
    saved_recipes_ = recipes_.size();
    pending_ = recipes_; pending_next_ = 0;
    const double boot_ms = env_ms("MELEE_PREWARM_BOOT_MS", 500);
    const size_t count = pending_.size();
    const double ms = prewarm_some(boot_ms);
    shader_log("opengl: prewarmed %zu programs from %zu recipes before guest startup in %.1f ms%s%s",
                 programs_.size(), count, ms, binaries_ ? "" : " (driver keeps no program binaries)",
                 pending_next_ < pending_.size() ? "; the rest between frames" : "");
  }
  static double env_ms(const char* name, double fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    char* end = nullptr; const double value = std::strtod(v, &end);
    return end != v && !*end && std::isfinite(value) && value >= 0 ? value : fallback;
  }
  // Builds pending programs until budget_ms has passed; returns the time spent.
  double prewarm_some(double budget_ms) {
    const auto start = std::chrono::steady_clock::now();
    auto spent = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); };
    prewarming_ = true;
    while (pending_next_ < pending_.size() && spent() < budget_ms && compile_allowed()) {
      const ProgramRecipe& r = pending_[pending_next_++];
      gx::DrawCall dc{}; dc.components = r.components; dc.bp = r.bp;
      std::memcpy(dc.xf_regs, r.xf, sizeof r.xf);
      try { program(dc); } catch (const std::exception&) {
        // A bad recorded program loses only its prewarm, including its deferred retry.
        builds_.erase(ProgramKey{gx::make_vs_uid(dc).hash(), gx::make_ps_uid(dc).hash()});
      }
    }
    prewarming_ = false;
    if (pending_next_ >= pending_.size() && !pending_.empty()) {
      shader_log("opengl: prewarm done at frame %u (sim %u): %zu programs ready, %zu queued", presented_, source_retrace_, programs_.size(), builds_.size());
      pending_.clear(); pending_.shrink_to_fit(); pending_next_ = 0;
    }
    return spent();
  }
#else
  void prewarm() {
    const std::string data = read_text(cache_ / "recipes.bin");
    uint64_t header[3]{};
    if (data.size() < sizeof header) return;
    std::memcpy(header, data.data(), sizeof header);
    const size_t count = static_cast<size_t>(header[1]);
    if (header[0] != RECIPE_MAGIC || count > RECIPE_LIMIT || data.size() != sizeof header + count * sizeof(ProgramRecipe) ||
        gx::hash_bytes(data.data() + sizeof header, count * sizeof(ProgramRecipe)) != header[2]) return;
    const auto start = std::chrono::steady_clock::now();
    prewarming_ = true;
    for (size_t i = 0; i < count; ++i) {
      ProgramRecipe r; std::memcpy(&r, data.data() + sizeof header + i * sizeof r, sizeof r);
      gx::DrawCall dc{}; dc.components = r.components; dc.bp = r.bp;
      std::memcpy(dc.xf_regs, r.xf, sizeof r.xf);
      try { program(dc); } catch (const std::exception&) {}   // a bad recipe only loses its prewarm
    }
    prewarming_ = false;
    saved_recipes_ = recipes_.size();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    shader_log("opengl: prewarmed %zu programs from %zu recipes before guest startup in %.1f ms%s",
                 programs_.size(), count, ms, binaries_ ? "" : " (driver keeps no program binaries)");
  }
#endif
  void save_recipes() {
    if (recipes_.size() == saved_recipes_) return;
    saved_recipes_ = recipes_.size();
    const size_t bytes = recipes_.size() * sizeof(ProgramRecipe);
    const uint64_t header[3] = {RECIPE_MAGIC, recipes_.size(), gx::hash_bytes(recipes_.data(), bytes)};
    std::string data(sizeof header + bytes, '\0');
    std::memcpy(data.data(), header, sizeof header);
    std::memcpy(data.data() + sizeof header, recipes_.data(), bytes);
    write_cache(cache_ / "recipes.bin", data);
  }
  static void destroy(Texture& t) { if (t.id) glDeleteTextures(1, &t.id); if (t.fbo) glDeleteFramebuffers(1, &t.fbo); }
#ifdef __APPLE__
  GLuint shader(GLenum stage, const std::string& hlsl) {
    const uint64_t hash = gx::hash_bytes(hlsl.data(), hlsl.size());
    const uint64_t key = hash ^ stage;
    if (auto i = shaders_.find(key); i != shaders_.end()) return i->second;
    std::ostringstream name; name << (stage == GL_VERTEX_SHADER ? "vs-" : "ps-") << std::hex << hash;
    const auto output = cache_ / (name.str() + ".glsl");
    const auto tools =
        mac_host::resources() / "bin";
    auto translate = [this, stage, hlsl, name = name.str(), tools] {
      return shader_translation::translate(cache_, name, stage == GL_VERTEX_SHADER, height_, hlsl, tools);
    };
    std::string glsl;
    if (sync_shaders_) {
      glsl = read_text(output);
      if (glsl.empty()) { ++sync_cache_misses_; glsl = translate(); }
      else ++sync_cache_hits_;
    } else {
      const auto source = translations_.request(output, std::move(translate));
      if (source->state.load(std::memory_order_acquire) != shader_translation::Result::Ready) return 0;
      if (!compile_allowed()) return 0;
      glsl = source->glsl;
    }
    // GLSL 4.10 can only say early_fragment_tests through GL_ARB_shader_image_load_store, which macOS
    // lacks. Without it the depth test runs after the shader: a visual difference for alpha-tested
    // pixels only, never game state.
    replace_all(glsl, "#extension GL_ARB_shader_image_load_store : require\n", "");
    replace_all(glsl, "layout(early_fragment_tests) in;\n", "");
    const auto start = Clock::now();
    invalidate_state(); // compile/link and uniform initialization may change bindings.
    GLuint compiled = compile_gl(stage, glsl);
    compile_spent_ms_ += milliseconds(start);
    shaders_.emplace(key, compiled); return compiled;
  }
  using Clock = std::chrono::steady_clock;
  static double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  }
  bool compile_allowed() const {
    // GL calls cannot be interrupted. Never start another compile/link once the budget is spent.
    return sync_shaders_ || boot_prewarm_ || compile_spent_ms_ < compile_budget_ms_;
  }
  struct ProgramBuild {
    ProgramRecipe recipe;
    std::string vertex, pixel;
    bool binary_tried = false, background_only = false;
    double work_ms = 0; // active build attempts only; excludes time waiting in the queue
  };
  void remember_recipe(const ProgramRecipe& r) {
    if (recipes_.size() < RECIPE_LIMIT && recipe_hashes_.insert(gx::hash_bytes(&r, sizeof r)).second) {
      recipes_.push_back(r);
      if (!prewarming_ && recipes_.size() >= saved_recipes_ + 32) save_recipes();
    }
  }
  GLuint program(const gx::DrawCall& dc) {
    const auto vs = gx::make_vs_uid(dc); const auto ps = gx::make_ps_uid(dc);
    ProgramKey key{vs.hash(), ps.hash()};
    if (auto i = programs_.find(key); i != programs_.end()) return i->second;
    auto found = builds_.find(key);
    if (found == builds_.end()) {
      ProgramBuild build{}; build.background_only = prewarming_;
      build.recipe.components = dc.components; build.recipe.bp = dc.bp;
      std::memcpy(build.recipe.xf, dc.xf_regs, sizeof build.recipe.xf);
      remember_recipe(build.recipe); // Keep even recipes whose translation is pending at exit.
      build.vertex = gx::generate_vertex_shader(vs); build.pixel = gx::generate_pixel_shader(ps);
      found = builds_.emplace(key, std::move(build)).first;
      compile_pending_.push_back(key);
    }
    if (!prewarming_) found->second.background_only = false;
    GLuint p = finish_program(key, found->second);
    if (p) builds_.erase(found);
    return p;
  }
  GLuint finish_program(const ProgramKey& key, ProgramBuild& build) {
    GlProbeScope work(&build.work_ms);
    invalidate_state();
    std::ostringstream name; name << "prog-" << std::hex << key.vertex << "-" << key.pixel << ".bin";
    const auto binary = cache_ / name.str();
    GLuint p = 0;
    if (binaries_ && !build.binary_tried) {
      if (!compile_allowed()) return 0;
      const auto start = Clock::now();
      p = load_binary(binary); build.binary_tried = true;
      compile_spent_ms_ += milliseconds(start);
    }
    if (!p) {
      // Ask for both stages even when one is pending, so both translations enter the queue now.
      const GLuint vertex = shader(GL_VERTEX_SHADER, build.vertex);
      const GLuint pixel = shader(GL_FRAGMENT_SHADER, build.pixel);
      if (!vertex || !pixel || !compile_allowed()) return 0;
      const auto start = Clock::now();
      p = link_gl(vertex, pixel, binaries_);
      if (binaries_) save_binary(p, binary);
      compile_spent_ms_ += milliseconds(start);
    }
    const auto binding_start = Clock::now();
    // gx_shader.cpp's HLSL registers: VSBlock b0, PSBlock b1, Tex0..7 t0..7.
    // GLSL 410 omits binding layouts; apply the same table after every link/load.
    for (int i = 0; i < 2; ++i) {
      const GLuint block = glGetUniformBlockIndex(p, i ? "PSBlock" : "VSBlock");
      if (block != GL_INVALID_INDEX) glUniformBlockBinding(p, block, i);
    }
    glUseProgram(p);
    GLint count = 0; glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &count);
    for (GLint i = 0; i < count; ++i) {
      char name[512]; GLint size = 0; GLenum type = 0;
      glGetActiveUniform(p, i, sizeof name, nullptr, &size, &type, name);
      if (type == GL_SAMPLER_2D) {
        if (size > 8) throw std::runtime_error("GX combined sampler array exceeded eight textures");
        const char* texture_name = std::strstr(name, "Tex");
        if (!texture_name || texture_name[3] < '0' || texture_name[3] > '7') throw std::runtime_error("Cannot identify translated GX sampler " + std::string(name));
        glUniform1i(glGetUniformLocation(p, name), texture_name[3] - '0');
      }
    }
    cache_attributes(p);   // inside the compile budget: it may wait for the driver to finish the link
    compile_spent_ms_ += milliseconds(binding_start);
    programs_.emplace(key, p);
    if (!build.background_only) log_new_program(key, build.work_ms + milliseconds(work.start));
    return p;
  }
  void drain_programs() {
    // Rotate past shaders still translating; a slow/failing shader cannot starve ready programs.
    const size_t count = std::min<size_t>(compile_pending_.size(), 64);
    for (size_t i = 0; i < count && compile_allowed(); ++i) {
      const auto key = compile_pending_.front(); compile_pending_.pop_front();
      auto found = builds_.find(key);
      if (found == builds_.end()) continue;
      try {
        if (finish_program(key, found->second)) builds_.erase(found);
        else compile_pending_.push_back(key);
      } catch (const std::exception& error) {
        if (!found->second.background_only) throw;
        std::fprintf(stderr, "opengl: ignoring bad prewarm recipe: %s\n", error.what());
        builds_.erase(found);
      }
    }
  }
  void report_skips(bool final) {
    const double seconds = milliseconds(skip_report_) / 1000;
    if (!final && seconds < 1) return;
    if (skipped_draws_) std::fprintf(stderr, "opengl: skipped %llu draws awaiting shaders in %.3f s%s\n",
        static_cast<unsigned long long>(skipped_draws_), seconds, final ? " (final interval)" : "");
    skipped_draws_ = 0; skip_report_ = Clock::now();
  }
#else
  GLuint shader(GLenum stage, const std::string& hlsl) {
    const uint64_t hash = gx::hash_bytes(hlsl.data(), hlsl.size());
    const uint64_t key = hash ^ stage;
    if (auto i = shaders_.find(key); i != shaders_.end()) return i->second;
    std::ostringstream name; name << (stage == GL_VERTEX_SHADER ? "vs-" : "ps-") << std::hex << hash;
    const auto output = cache_ / (name.str() + ".glsl");
    std::string glsl = read_text(output);
    if (glsl.empty()) {
      const auto prefix = cache_ / (name.str() + "." + std::to_string(getpid()));
      std::string source = hlsl;
      if (stage == GL_FRAGMENT_SHADER) {
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
        if (at != std::string::npos) source.insert(at, "rawpos.y = " + std::to_string(height_) + ".0 - rawpos.y;\n");
      }
      // During play the translation runs on a worker and this shader is "not ready" (0) until the
      // file appears: the draws that need it are skipped for those few frames instead of the whole
      // frame stopping (a first-time effect stuttered every Linux player until it was cached).
      // Startup prewarming waits as before, and a translation that failed in the background is
      // tried here once more so its error is reported the same way.
      bool failed_before = false;
      {
        std::lock_guard<std::mutex> lock(translate_mutex_);
        failed_before = translate_failed_.count(key) != 0;
        if (!prewarming_ && !failed_before) {
          if (translate_pending_.insert(key).second) {
            translate_jobs_.push_back({key, stage, std::move(source), prefix, output});
            translate_cv_.notify_one();
            start_translators();
          }
          return 0;
        }
      }
      translate_shader(stage, source, prefix, output);
      glsl = read_text(output);
    }
    invalidate_state(); // compile/link and uniform initialization may change bindings.
    GLuint compiled = compile_gl(stage, glsl); shaders_.emplace(key, compiled); return compiled;
  }
  GLuint program(const gx::DrawCall& dc) {
    const auto vs = gx::make_vs_uid(dc); const auto ps = gx::make_ps_uid(dc);
    ProgramKey key{vs.hash(), ps.hash()};
    if (auto i = programs_.find(key); i != programs_.end()) return i->second;
    const auto started = std::chrono::steady_clock::now();
    invalidate_state();
    std::ostringstream name; name << "prog-" << std::hex << key.vertex << "-" << key.pixel << ".bin";
    const auto binary = cache_ / name.str();
    GLuint p = binaries_ ? load_binary(binary) : 0;
    if (!p) {
      const GLuint vsh = shader(GL_VERTEX_SHADER, gx::generate_vertex_shader(vs));
      const GLuint psh = shader(GL_FRAGMENT_SHADER, gx::generate_pixel_shader(ps));
      if (!vsh || !psh) return 0;   // still being translated in the background
      p = link_gl(vsh, psh, binaries_);
      if (binaries_) save_binary(p, binary);
    }
    if (recipes_.size() < RECIPE_LIMIT) {
      ProgramRecipe r; r.components = dc.components; r.bp = dc.bp;
      std::memcpy(r.xf, dc.xf_regs, sizeof r.xf);
      recipes_.push_back(r);
      if (!prewarming_ && recipes_.size() >= saved_recipes_ + 32) save_recipes();
    }
    for (int i = 0; i < 2; ++i) {
      const GLuint block = glGetUniformBlockIndex(p, i ? "PSBlock" : "VSBlock");
      if (block != GL_INVALID_INDEX) glUniformBlockBinding(p, block, i);
    }
    glUseProgram(p);
    GLint count = 0; glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &count);
    for (GLint i = 0; i < count; ++i) {
      char name[512]; GLint size = 0; GLenum type = 0;
      glGetActiveUniform(p, i, sizeof name, nullptr, &size, &type, name);
      if (type == GL_SAMPLER_2D) {
        if (size > 8) throw std::runtime_error("GX combined sampler array exceeded eight textures");
        const char* texture_name = std::strstr(name, "Tex");
        if (!texture_name || texture_name[3] < '0' || texture_name[3] > '7') throw std::runtime_error("Cannot identify translated GX sampler " + std::string(name));
        glUniform1i(glGetUniformLocation(p, name), texture_name[3] - '0');
      }
    }
    programs_.emplace(key, p);
    if (!prewarming_) log_new_program(key, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    return p;
  }
#endif
  void log_new_program(const ProgramKey& key, double ms) {
    static unsigned logged = 0; // cap across renderer instances for this process
    if (logged < 400) {
      ++logged;
      shader_log("opengl: new program at frame %u (sim %u) in %.1f ms (vs %016llx ps %016llx)",
                 presented_, source_retrace_, ms, (unsigned long long)key.vertex, (unsigned long long)key.pixel);
    } else if (logged == 400) {
      ++logged;
      shader_log("further new programs not logged");
    }
  }
  void invalidate_state() { emitted_ = {}; }
  void use_program(GLuint p) { if (emitted_.program.changed(p)) glUseProgram(p); }
  void bind_vertex_array(GLuint vao) { if (emitted_.vao.changed(vao)) glBindVertexArray(vao); }
  static void capability(GlValue<bool>& state, GLenum cap, bool enabled) {
    if (state.changed(enabled)) { if (enabled) glEnable(cap); else glDisable(cap); }
  }
  // Querying a just-linked program makes Apple's GL finish the link on the spot. Built outside the compile budget it
  // stalled scene entries by 50-80 ms; built only at first draw it moved 5-8 ms hitches into matches (Mac A/B, 10-26).
  // So the async path builds it as a program finishes, charged to the compile budget; the first draw is the fallback.
  const ProgramAttributes& cache_attributes(GLuint p) {
    // Reflection is immutable for a linked (or binary-loaded) program.
    static const char* names[] = {"rawpos", "rawnorm0", "color0", "color1",
      "rawtex0", "rawtex1", "rawtex2", "rawtex3", "rawtex4", "rawtex5", "rawtex6", "rawtex7",
      "blend_indices", "blend_indices2", "blend_index7"};
    std::array<GLint, 15> locations;
    for (size_t i = 0; i < locations.size(); ++i) locations[i] = glGetAttribLocation(p, names[i]);
    auto& entry = attribute_locations_.emplace(p, ProgramAttributes{locations, 0}).first->second;
    glGenVertexArrays(1, &entry.vao); glBindVertexArray(entry.vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices_);
    configure_attributes(p);
    invalidate_state();
    return entry;
  }
  void attributes(GLuint p) {
    GlProbeScope cost(probe_.slot(probe_.attrib));
    const auto found = attribute_locations_.find(p);
    bind_vertex_array((found != attribute_locations_.end() ? found->second : cache_attributes(p)).vao);
  }
  void configure_attributes(GLuint p) {
    glBindBuffer(GL_ARRAY_BUFFER, vertices_);
    for (unsigned i = 0; i < 16; ++i) glDisableVertexAttribArray(i);
    const auto& locations = attribute_locations_.at(p).locations;
    size_t next = 0;
    auto add = [&](int count, GLenum type, bool normalized, size_t offset, bool integer = false) {
      const GLint loc = locations[next++]; if (loc < 0) return;
      glEnableVertexAttribArray(loc);
      if (integer) glVertexAttribIPointer(loc, count, type, sizeof(gx::Vertex), reinterpret_cast<void*>(offset));
      else glVertexAttribPointer(loc, count, type, normalized ? GL_TRUE : GL_FALSE, sizeof(gx::Vertex), reinterpret_cast<void*>(offset));
    };
    add(3, GL_FLOAT, false, 0); add(3, GL_FLOAT, false, 12);
    add(4, GL_UNSIGNED_BYTE, true, 24); add(4, GL_UNSIGNED_BYTE, true, 28);
    for (int i = 0; i < 8; ++i) add(2, GL_FLOAT, false, 32 + 8 * i);
    add(4, GL_UNSIGNED_BYTE, false, 96, true);
    add(4, GL_UNSIGNED_BYTE, false, 100, true);
    add(1, GL_UNSIGNED_BYTE, false, 104, true);
  }
  GLuint texture(const gx::TextureRef& t) {
    GlProbeScope cost(probe_.slot(probe_.texture));
    if (auto c = copies_.find(t.addr & 0x3FFFFFFFu); c != copies_.end()) { c->second.used = sequence_; return c->second.id; }
    if (!t.data || !t.width || !t.height) return 0;
#ifdef GX_PC_SETTINGS
    if (gx::video_bg::wants_texture_names()) {
      int slot = -1;
      const auto video = gx::video_bg::lookup(gx::texpack::base_name(t, *t.data), t.width, t.height, &slot);
      if (video && slot >= 0 && slot < 2 && !video->bgra.empty())
        if (const GLuint id = video_texture(video, slot)) { draw_video_slot_ = slot; return id; }
    }
#endif
    const uint64_t desc[] = {t.data->hash, t.width, t.height, t.format, t.tlut_format, t.mip_levels};
    const uint64_t key = gx::look_key(gx::hash_bytes(desc, sizeof desc), look_surface_);
    if (auto i = textures_.find(key); i != textures_.end()) { i->second.used = sequence_; return i->second.id; }
    if (probe_.on) ++probe_.uploads;
    invalidate_state(); // Allocation binds on the currently active texture unit.
    if (auto small = gx::look_replacement(t, look_surface_)) {   // the look's treated copy, one level (gx_retro.h)
      Texture entry; entry.width = small->width; entry.height = small->height; entry.used = sequence_; entry.levels = 1;
      glGenTextures(1, &entry.id); glBindTexture(GL_TEXTURE_2D, entry.id);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, small->width, small->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, small->pixels.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      entry.bytes = small->pixels.size();
      const GLuint id = entry.id; textures_.emplace(key, entry); return id;
    }
    Texture entry; entry.width = t.width; entry.height = t.height; entry.used = sequence_;
    entry.levels = gx::texture_mip_count(t.width, t.height, t.mip_levels);
    // Custom texture pack or a skin's companion textures: Dolphin's name for this texture, then its
    // PNG if a source has one (once per unique texture, never per draw), as on Windows.
    std::string pack_name;
#ifdef GX_PC_SETTINGS
    if (gx::texpack::enabled() || gx::texpack::dumping() || gx::texpack::cosmetics_enabled()) {
      pack_name = gx::texpack::base_name(t, *t.data);
      if (gx::texpack::has(pack_name)) {
        constexpr uint64_t budget = 512ull << 20;
        std::shared_ptr<const gx::texpack::Replacement> r = gx::texpack::load(pack_name, replacement_bytes_ < budget ? budget - replacement_bytes_ : 0);
        gx::texpack::note_lookup(r != nullptr);
        if (r) {
          glGenTextures(1, &entry.id); glBindTexture(GL_TEXTURE_2D, entry.id);
          entry.width = r->width; entry.height = r->height; entry.levels = std::max(1u, r->levels);
          for (uint32_t level = 0; level < entry.levels && level < 16; ++level) {
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, r->level_width[level], r->level_height[level], 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, r->pixels.data() + r->level_offset[level]);
            entry.bytes += static_cast<uint64_t>(r->level_width[level]) * r->level_height[level] * 4;
          }
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, entry.levels - 1);
          replacement_bytes_ += entry.bytes;
          const GLuint id = entry.id; textures_.emplace(key, entry); return id;
        }
      }
    }
#endif
    glGenTextures(1, &entry.id); glBindTexture(GL_TEXTURE_2D, entry.id);
    const uint8_t* data = t.data->image.data(); size_t remaining = t.data->image.size();
    uint32_t w = t.width, h = t.height;
    for (uint32_t level = 0; level < entry.levels; ++level) {
      const uint32_t bytes = gx::texture_level_bytes(w, h, t.format);
      if (bytes > remaining) { destroy(entry); throw std::runtime_error("GX texture mip exceeded snapshot bytes"); }
      gx::decode_texture(data, w, h, t.format, t.data->palette.data(), t.tlut_format, decoded_);
#ifdef GX_PC_SETTINGS
      if (gx::texpack::dumping() && !pack_name.empty()) gx::texpack::dump_level(pack_name, level, decoded_.data(), w, h);
#endif
      glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, decoded_.data());
      entry.bytes += static_cast<uint64_t>(w) * h * 4; data += bytes; remaining -= bytes;
      w = std::max(1u, w / 2); h = std::max(1u, h / 2);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, entry.levels - 1);
    const GLuint id = entry.id; textures_.emplace(key, entry); return id;
  }
  GLuint sampler(uint32_t mode0, uint32_t mode1) {
    const uint64_t key = (uint64_t(mode0) << 32) | mode1;
    if (auto i = samplers_.find(key); i != samplers_.end()) return i->second;
    GLuint s = 0; glGenSamplers(1, &s);
    const GLint wraps[] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT};
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, wraps[gx::bits(mode0, 0, 2)]);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_T, wraps[gx::bits(mode0, 2, 2)]);
    const uint32_t minf = gx::bits(mode0, 5, 3), mip = minf & 3; const bool linear = minf & 4;
    const GLint filter = !mip ? (linear ? GL_LINEAR : GL_NEAREST) : (mip == 2 ? (linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR) : (linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST));
    glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, filter); glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, gx::bits(mode0, 4, 1) ? GL_LINEAR : GL_NEAREST);
    glSamplerParameterf(s, GL_TEXTURE_LOD_BIAS, float(gx::sbits(mode0, 9, 8)) / 32);
    glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, gx::bits(mode1, 0, 8) / 16.0f);
    glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, mip ? gx::bits(mode1, 8, 8) / 16.0f : 0);
    // Anisotropic filtering (Video settings), on linear filters as the Windows renderers.
    if (anisotropy_applied_ > 1 && linear && gx::bits(mode0, 4, 1) &&
#ifdef __APPLE__
        SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic"))
#else
        (GLEW_ARB_texture_filter_anisotropic || GLEW_EXT_texture_filter_anisotropic))
#endif
      glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY_EXT, float(std::clamp(anisotropy_applied_, 1, 16)));
    samplers_.emplace(key, s); return s;
  }
  void draw(const gx::Frame& frame, const gx::DrawCall& dc, const gx::DrawMatrices* matrices, size_t vertex_override) {
    if (gx::skip_for_effects(effects_filter_, dc)) return;
    if (gx::skip_for_lightweight(lightweight_filter_, frame, dc)) return;   // "Lightweight" 2+: scenery behind the play plane
    GLuint p = program(dc);
    if (!p) {
#ifdef __APPLE__
      ++skipped_draws_;
#else
      ++waiting_draws_;
#endif
      return;   // its shader is still being translated (see shader())
    }
    use_program(p); attributes(p); glBindFramebuffer(GL_FRAMEBUFFER, draw_fbo());
    const float* vp = reinterpret_cast<const float*>(&dc.xf_regs[0x1A]);
    float x = (vp[3] - vp[0] - 342) * scale_, y = (vp[4] + vp[1] - 342) * scale_;
    float w = vp[0] * 2 * scale_, h = -vp[1] * 2 * scale_;
    if (w < 0) { x += w; w = -w; } if (h < 0) { y += h; h = -h; }
    const std::array<GLfloat, 4> viewport{x, height_ - y - h, std::max(w, 1.0f), std::max(h, 1.0f)};
    if (emitted_.viewport.changed(viewport)) glViewportIndexedf(0, viewport[0], viewport[1], viewport[2], viewport[3]);
    float znear = std::clamp(1 - vp[5] / 16777216.0f, 0.0f, 1.0f), zfar = std::clamp(1 - (vp[5] - vp[2]) / 16777216.0f, 0.0f, 1.0f);
    if (zfar < znear) std::swap(zfar, znear);
    if (emitted_.depth_range.changed({znear, zfar})) glDepthRange(znear, zfar);
    const auto tl = dc.bp.reg[gx::BP_SCISSORTL], br = dc.bp.reg[gx::BP_SCISSORBR], so = dc.bp.reg[gx::BP_SCISSOROFFSET];
    const int ox = gx::bits(so, 0, 10) * 2 - 342, oy = gx::bits(so, 10, 10) * 2 - 342;
    const int l = std::clamp(int(gx::bits(tl, 12, 12)) - ox - 342, 0, gx::EFB_WIDTH), t = std::clamp(int(gx::bits(tl, 0, 12)) - oy - 342, 0, gx::EFB_HEIGHT);
    const int r = std::clamp(int(gx::bits(br, 12, 12)) - ox - 341, 0, gx::EFB_WIDTH), b = std::clamp(int(gx::bits(br, 0, 12)) - oy - 341, 0, gx::EFB_HEIGHT);
    if (r <= l || b <= t) return;
    capability(emitted_.scissor, GL_SCISSOR_TEST, true);
    const std::array<GLint, 4> rect{l * scale_, height_ - b * scale_, (r - l) * scale_, (b - t) * scale_};
    if (emitted_.scissor_rect.changed(rect)) glScissor(rect[0], rect[1], rect[2], rect[3]);
    const auto blend = dc.bp.blendmode(), zmode = dc.bp.zmode(), cull = dc.bp.cullmode();
    const GLenum src[] = {GL_ZERO,GL_ONE,GL_DST_COLOR,GL_ONE_MINUS_DST_COLOR,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_DST_ALPHA,GL_ONE_MINUS_DST_ALPHA};
    const GLenum dst[] = {GL_ZERO,GL_ONE,GL_SRC_COLOR,GL_ONE_MINUS_SRC_COLOR,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_DST_ALPHA,GL_ONE_MINUS_DST_ALPHA};
    GLenum sf = src[gx::bits(blend, 8, 3)], df = dst[gx::bits(blend, 5, 3)];
    if ((dc.bp.zcontrol() & 7) != 1) { if (sf == GL_DST_ALPHA) sf = GL_ONE; if (sf == GL_ONE_MINUS_DST_ALPHA) sf = GL_ZERO; if (df == GL_DST_ALPHA) df = GL_ONE; if (df == GL_ONE_MINUS_DST_ALPHA) df = GL_ZERO; }
    capability(emitted_.blend, GL_BLEND, (blend & 1) != 0);
    if (emitted_.blend_func.changed({sf, df, GL_ONE, GL_ZERO})) glBlendFuncSeparate(sf, df, GL_ONE, GL_ZERO);
    const GLenum equation = (blend & (1 << 11)) ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD;
    if (emitted_.blend_equation.changed({equation, GL_FUNC_ADD})) glBlendEquationSeparate(equation, GL_FUNC_ADD);
    const GLboolean rgb = (blend & 8) ? GL_TRUE : GL_FALSE, alpha = (blend & 16) ? GL_TRUE : GL_FALSE;
    if (emitted_.color_mask.changed({rgb, rgb, rgb, alpha})) glColorMask(rgb, rgb, rgb, alpha);
    const GLenum compare[] = {GL_NEVER,GL_GREATER,GL_EQUAL,GL_GEQUAL,GL_LESS,GL_NOTEQUAL,GL_LEQUAL,GL_ALWAYS};
    capability(emitted_.depth, GL_DEPTH_TEST, (zmode & 1) != 0);
    const GLboolean depth_write = (zmode & 1) && (zmode & 16);
    if (emitted_.depth_mask.changed(depth_write)) glDepthMask(depth_write);
    const GLenum depth_func = compare[gx::bits(zmode, 1, 3)];
    if (emitted_.depth_func.changed(depth_func)) glDepthFunc(depth_func);
    capability(emitted_.cull, GL_CULL_FACE, cull != 0);
    if (cull) {
      const GLenum front = clockwise_front_ ? GL_CW : GL_CCW;
      const GLenum face = cull == 2 ? GL_FRONT : cull == 3 ? GL_FRONT_AND_BACK : GL_BACK;
      if (emitted_.front_face.changed(front)) glFrontFace(front);
      if (emitted_.cull_face.changed(face)) glCullFace(face);
    }
    gx::VSConstants vc{}; gx::PSConstants pc{};
    gx::fill_vs_constants(dc, vc, scale_, matrices); gx::fill_ps_constants(dc, pc, scale_);
#ifdef __APPLE__
    // Apple's GL-on-Metal flushes the whole command buffer when a buffer the GPU may still read is
    // updated in place, i.e. once per draw here. Orphaning (new storage each time) avoids that.
    { GlProbeScope cost(probe_.slot(probe_.buffers));
    glBindBuffer(GL_UNIFORM_BUFFER, uniform_[0]); glBufferData(GL_UNIFORM_BUFFER, sizeof vc, &vc, GL_STREAM_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, uniform_[1]); glBufferData(GL_UNIFORM_BUFFER, sizeof pc, &pc, GL_STREAM_DRAW);
    }
#else
    glBindBuffer(GL_UNIFORM_BUFFER, uniform_[0]); glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof vc, &vc);
    glBindBuffer(GL_UNIFORM_BUFFER, uniform_[1]); glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof pc, &pc);
#endif
    const bool flat = gx::lightweight_flat(lightweight_filter_, dc);   // "Lightweight" 3+: one white texel instead
    look_surface_ = gx::look_surface(lightweight_filter_, dc);
    const bool unfiltered = gx::look_unfiltered(lightweight_filter_, dc);
    for (int unit = 0; unit < 8; ++unit) {
      const GLenum active = GL_TEXTURE0 + unit;
      if (emitted_.active_texture.changed(active)) glActiveTexture(active);
      // Keep allocation after the active-unit change, as in the original command stream.
      const GLuint tex = !dc.textures[unit].used ? 0 : flat ? white_texture() : texture(dc.textures[unit]);
      if (emitted_.textures[unit].changed(tex)) glBindTexture(GL_TEXTURE_2D, tex);
      const GLuint samp = sampler(unfiltered ? gx::unfiltered_mode0(dc.textures[unit].mode0) : dc.textures[unit].mode0, dc.textures[unit].mode1);
      if (emitted_.samplers[unit].changed(samp)) glBindSampler(unit, samp);
    }
    index_scratch_.clear(); gx::DrawTopology topology = gx::DrawTopology::Unsupported;
    if (dc.segment_count) {
      for (uint32_t i = 0; i < dc.segment_count; ++i) {
        const auto& seg = frame.segments.at(dc.first_segment + i);
        const uint32_t base = vertex_override ? uint32_t(vertex_override + seg.first_vertex - dc.first_vertex) : seg.first_vertex;
        topology = gx::append_segment_indices(index_scratch_, seg.primitive, seg.vertex_count, base);
      }
    } else topology = gx::append_segment_indices(index_scratch_, dc.primitive, dc.vertex_count, vertex_override ? uint32_t(vertex_override) : dc.first_vertex);
    if (index_scratch_.empty()) return;
    if (probe_.on) probe_.index_bytes += index_scratch_.size() * sizeof(uint32_t);
    { GlProbeScope cost(probe_.slot(probe_.buffers));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices_); glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_scratch_.size() * sizeof(uint32_t), index_scratch_.data(), GL_STREAM_DRAW);
    }
    if (probe_.on) ++probe_.draws;
    GlProbeScope cost(probe_.slot(probe_.draw));
    const bool wire = gx::look_wire(lightweight_filter_, dc);   // "Lightweight" 5 or a Wire look
    if (wire) { capability(emitted_.cull, GL_CULL_FACE, false); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); }
    glDrawElements(topology == gx::DrawTopology::Lines ? GL_LINES : GL_TRIANGLES, index_scratch_.size(), GL_UNSIGNED_INT, nullptr);
    if (wire) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }
  // MSAA (Anti-aliasing 2x/4x/8x MSAA): draws go to multisampled renderbuffers, blitted into the EFB
  // textures (colour and depth) before every copy, present and capture.
  GLuint draw_fbo() const { return msaa_fbo_ ? msaa_fbo_ : efb_; }
  void apply_msaa(int wanted) {
    GLint max_samples = 1; glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    int n = 1;
    for (int k = std::clamp(wanted, 1, 8); k > 1; k /= 2) if (k <= max_samples) { n = k; break; }
    if (n == msaa_samples_) return;
    if (msaa_fbo_) { glDeleteFramebuffers(1, &msaa_fbo_); glDeleteRenderbuffers(2, msaa_rb_); msaa_fbo_ = 0; msaa_rb_[0] = msaa_rb_[1] = 0; }
    msaa_samples_ = n;
    if (n > 1) {
      glGenRenderbuffers(2, msaa_rb_);
      glBindRenderbuffer(GL_RENDERBUFFER, msaa_rb_[0]); glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, GL_RGBA8, width_, height_);
      glBindRenderbuffer(GL_RENDERBUFFER, msaa_rb_[1]); glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, GL_DEPTH_COMPONENT32F, width_, height_);
      glGenFramebuffers(1, &msaa_fbo_); glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaa_rb_[0]);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaa_rb_[1]);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &msaa_fbo_); glDeleteRenderbuffers(2, msaa_rb_); msaa_fbo_ = 0; msaa_samples_ = 1;
        std::fprintf(stderr, "opengl: %dx MSAA unavailable\n", n);
      } else {
        glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE);
        glClearColor(0, 0, 0, 1); glClearDepth(0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, draw_fbo());
  }
  void resolve_msaa() {
    if (!msaa_fbo_) return;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo_); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, efb_);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, efb_);
  }
  GLuint white_texture() {
    if (white_) return white_;
    invalidate_state(); // Allocation changes the current unit's texture binding.
    const uint32_t white = 0xFFFFFFFFu;
    glGenTextures(1, &white_); glBindTexture(GL_TEXTURE_2D, white_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    return white_;
  }
  void clear(const gx::EfbCopy& c) {
    invalidate_state();
    glBindFramebuffer(GL_FRAMEBUFFER, draw_fbo()); glEnable(GL_SCISSOR_TEST);
    glScissor(c.src_x * scale_, height_ - (c.src_y + c.src_h) * scale_, c.src_w * scale_, c.src_h * scale_);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_TRUE);
    const uint32_t clear_colour = gx::look_clear_colour(lightweight_filter_.in_match, c.clear_color);   // the look's background
    glClearColor(((clear_colour >> 16) & 255) / 255.0f, ((clear_colour >> 8) & 255) / 255.0f, (clear_colour & 255) / 255.0f, (clear_colour >> 24) / 255.0f);
    glClearDepth(1 - c.clear_z / 16777215.0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
  void copy(const gx::EfbCopy& c) {
    invalidate_state();
    bind_vertex_array(vao_);
    GlProbeScope cost(probe_.slot(probe_.copy));
    if (!c.src_w || !c.src_h) return;
    resolve_msaa();
    const uint32_t w = std::max(1u, c.src_w / (c.half_scale ? 2 : 1)) * scale_, h = std::max(1u, c.src_h / (c.half_scale ? 2 : 1)) * scale_;
    auto& e = copies_[c.dest_addr & 0x3FFFFFFFu];
    if (!e.id || e.width != w || e.height != h) {
      if (probe_.on) ++probe_.copy_allocs;
      destroy(e); e = {}; e.width = w; e.height = h;
      glGenTextures(1, &e.id); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, e.id);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0); glGenFramebuffers(1, &e.fbo); glBindFramebuffer(GL_FRAMEBUFFER, e.fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, e.id, 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("OpenGL EFB copy is incomplete");
    }
    e.used = sequence_; glBindFramebuffer(GL_FRAMEBUFFER, e.fbo); glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glUseProgram(copy_program_); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, c.is_depth ? depth_ : color_); glBindSampler(0, 0);
    glUniform1i(copy_source_, 0);
    glUniform1i(copy_kind_, c.is_depth ? 2 : (c.efb_alpha ? 0 : 1));
    // EFB is stored bottom-up by OpenGL; sampled GX textures store their first (top) row at y=0.
    glUniform4f(copy_rect_, c.src_w / float(gx::EFB_WIDTH), c.src_h / float(gx::EFB_HEIGHT), c.src_x / float(gx::EFB_WIDTH), 1 - (c.src_y + c.src_h) / float(gx::EFB_HEIGHT));
    glDrawArrays(GL_TRIANGLES, 0, 3); glBindFramebuffer(GL_FRAMEBUFFER, draw_fbo());
    invalidate_state();
  }
  void present(const gx::EfbCopy& c, const gx::Frame& frame) {
    const int test_fill = frame.test_fill;
    invalidate_state();
    resolve_msaa();
    int w = 0, h = 0;
#ifdef MELEE_LINUX_X11_EMBED
    if (hooks_.drawable_size) {
      if (!hooks_.drawable_size(&w, &h)) return;
    } else
#endif
    SDL_GL_GetDrawableSize(window_, &w, &h);
    if (w <= 0 || h <= 0) return;
#ifdef __APPLE__
    // A new window size (full screen, a drag): update the GL surface to it (port/mac/mac_gl.mm).
    if (w != drawable_w_ || h != drawable_h_) {
      if (drawable_w_ && !host::test_env::getenv("MELEE_TEST_NO_GL_UPDATE")) mac_gl_update_context();
      // The surface must be the window's size in pixels as SDL counts them; a bigger one (a Retina surface SDL does not
      // know about) draws the picture small in its lower left (embed_mac.mm, 10-26).
      int sw = 0, sh = 0;
      if (mac_gl_surface_size(&sw, &sh) && (sw != w || sh != h))
        host::log("Mac video: window %dx%d, GL surface %dx%d DOES NOT MATCH", w, h, sw, sh);
      else
        host::log("Mac video: window %dx%d, GL surface %dx%d", w, h, sw, sh);
      drawable_w_ = w; drawable_h_ = h; surface_probe_ = host::test_env::getenv("MELEE_TEST_SURFACE") ? 4 : 0;
    }
    static const bool probing = host::test_env::getenv("MELEE_TEST_SURFACE") != nullptr;
    if (probing && presented_ % 30 == 0) surface_probe_ = std::max(surface_probe_, 1);
#endif
#ifdef __APPLE__
    // CopyImageSubData requires 4.3. A framebuffer blit preserves the same EFB pixels.
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, efb_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, last_frame_);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
#else
    glCopyImageSubData(color_, GL_TEXTURE_2D, 0, 0, 0, 0, last_color_, GL_TEXTURE_2D, 0, 0, 0, 0, width_, height_, 1);
#endif
    have_last_ = true;
    glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    const float fill = test_fill > 0 ? 1.0f : 0.0f; glClearColor(fill, fill, fill, 1); glClear(GL_COLOR_BUFFER_BIT);
#ifdef __APPLE__
    if (surface_probe_ > 0) {
      // MELEE_TEST_SURFACE: is the window's GL surface as big as the window? Fill it with magenta and
      // read its far corner back; a surface left at the old size has nothing there.
      --surface_probe_;
      glClearColor(1, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT); glClearColor(fill, fill, fill, 1);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0); glReadBuffer(GL_BACK);
      uint8_t near[4]{}, far[4]{};
      glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, near);
      glReadPixels(w - 1, h - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, far);
      std::fprintf(stderr, "Mac video: surface probe %dx%d near %u,%u,%u far %u,%u,%u: %s\n", w, h, near[0], near[1], near[2],
                   far[0], far[1], far[2], far[0] == 255 && far[1] == 0 && far[2] == 255 ? "full size" : "SMALLER than the window");
    }
#endif
#ifdef GX_PC_SETTINGS
    if (test_fill < 0 && hooks_.options) border_art(w, h);
#endif
    if (test_fill < 0) {
      const double aspect = hooks_.options ? gx::presented_aspect(*hooks_.options, w, h, widenable_scene_) : 73.0 / 60.0;
      int pw = w, ph = std::lround(w / aspect);
      if (ph > h) { ph = h; pw = std::lround(h * aspect); }
      picture(c, w, h, pw, ph);
    }
    if (hooks_.before_swap) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, w, h);
      hooks_.before_swap(w, h);
      invalidate_state(); // UI owns arbitrary GL state.
      glBindVertexArray(vao_);
    }
    if (test_fill < 0) { ++presented_; captures(capture_sequence_); --presented_; }   // the finished window, menus included
    test_shot(w, h);
#ifdef YOUGAME_AGENT_TEST_HOOK
    auto& evidence = yougame::agent_present::recorder();
    yougame::agent_present::Image strip;
    int window_w = 0, window_h = 0;
    if (evidence.enabled()) SDL_GetWindowSize(window_, &window_w, &window_h);
    if (evidence.wants_frame(uint64_t(presented_) + 1)) {
      // Read the actual window back buffer, including letterbox and UI, before swapping.
      // As in f7bef74/19ae8c7; restore pack state because before_swap can leave a PBO bound.
      GLint read_fbo = 0, pack_buffer = 0, alignment = 0, row_length = 0, skip_rows = 0, skip_pixels = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
      glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
      glGetIntegerv(GL_PACK_ALIGNMENT, &alignment); glGetIntegerv(GL_PACK_ROW_LENGTH, &row_length);
      glGetIntegerv(GL_PACK_SKIP_ROWS, &skip_rows); glGetIntegerv(GL_PACK_SKIP_PIXELS, &skip_pixels);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
      GLint read_buffer = 0; glGetIntegerv(GL_READ_BUFFER, &read_buffer);
      glReadBuffer(GL_BACK); glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
      glPixelStorei(GL_PACK_SKIP_ROWS, 0); glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
      std::vector<uint8_t> pixels(size_t(w) * h * 3);
      glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
      glReadBuffer(read_buffer); glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, pack_buffer);
      glPixelStorei(GL_PACK_ALIGNMENT, alignment); glPixelStorei(GL_PACK_ROW_LENGTH, row_length);
      glPixelStorei(GL_PACK_SKIP_ROWS, skip_rows); glPixelStorei(GL_PACK_SKIP_PIXELS, skip_pixels);
      strip = yougame::agent_present::downscale(pixels.data(), w, h);
    }
    const auto record_present = [&] {
      evidence.after_swap(presented_, frame.source_retrace, frame.scene_major, frame.scene_minor,
                          w, h, window_w, window_h, std::move(strip));
    };
#endif
#ifdef __APPLE__
    { GlProbeScope cost(probe_.slot(probe_.swap)); SDL_GL_SwapWindow(window_); } ++presented_;
#ifdef YOUGAME_AGENT_TEST_HOOK
    record_present();
#endif
    // Notify both the regular-window and embedded-panel first-frame policies.
    if (hooks_.after_swap) hooks_.after_swap(presented_);
    if (!pending_.empty()) {
      static const double frame_ms = env_ms("MELEE_PREWARM_FRAME_MS", 4);
      GlProbeScope cost(probe_.slot(probe_.prewarm));
      prewarm_some(frame_ms);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, draw_fbo());
#else
    SDL_GL_SwapWindow(window_); ++presented_;
#ifdef YOUGAME_AGENT_TEST_HOOK
    record_present();
#endif
    if (hooks_.after_swap) hooks_.after_swap(presented_);
    glBindFramebuffer(GL_FRAMEBUFFER, draw_fbo());
#endif
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) throw std::runtime_error("OpenGL GX frame failed with error " + std::to_string(error));
    // Ready = the game drew its first real frame, visible or not: the page hides the embed under its loading cover
    // until ready, so waiting for visibility deadlocked until the page's 20 s safety (10-25).
    yougame::parity::signals.presented(test_fill < 0);
    invalidate_state();
  }
  // The picture on the window: the EFB's frame region averaged over each output pixel when it is
  // larger (supersampling), sharpened, with brightness, contrast and vibrance, or drawn through the
  // CRT tube model. The same maths as the Windows present pass (gx_d3d11.cpp, crt_shader.h).
  void picture(const gx::EfbCopy& c, int w, int h, int pw, int ph) {
    const gx::RenderOptions* o = hooks_.options;
    if (!picture_program_) picture_program_ = build_picture_program();
    const gx::CrtLook look = gx::crt_look(o ? o->crt_filter : 0, o ? o->crt_curve : 1.0f);
    const float lines = std::max(float(c.src_h) * c.y_scale, 1.0f);
    // A shader preset (Video settings) takes the place of the built-in CRT model, as on Windows: the
    // plain picture goes into a console-sized texture and the preset draws that into the window.
    bool shader_on = false;
#ifdef GX_PC_SETTINGS
    shader_on = o && gx::user_shader::gl_prepare(o->shader_preset, [](const char* name) { return (const void*)SDL_GL_GetProcAddress(name); });
    glBindVertexArray(vao_);
#endif
    const bool crt_on = o && o->crt_filter && !shader_on;
    float vh = float(ph), vw = float(pw);
    gx::CrtFit fit{vh, 0.0f, lines};
    // Only the built-in CRT model lays its lines on whole pixels (crt_fit: more, finer lines, no border).
    if (crt_on) fit = gx::crt_fit(vh, lines);
    const float vx = std::floor((w - vw) * 0.5f), vy = std::floor((h - vh) * 0.5f);
    const uint32_t sw = std::max(c.src_w, 1u), sh = uint32_t(std::max(std::lround(lines), 1L));
    if (shader_on) {
      shader_target(shader_source_, sw, sh);
      shader_target(shader_output_, uint32_t(w), uint32_t(h));
      glBindFramebuffer(GL_FRAMEBUFFER, shader_source_.fbo);
      glViewport(0, 0, GLsizei(sw), GLsizei(sh));
    } else {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(GLint(vx), GLint(vy), GLsizei(std::max(vw, 1.0f)), GLsizei(std::max(vh, 1.0f)));
    }
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_STENCIL_TEST);
    glUseProgram(picture_program_);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, color_); glBindSampler(0, 0);
    const auto u = [this](const char* n) { return glGetUniformLocation(picture_program_, n); };
    glUniform1i(u("src"), 0);
    glUniform4f(u("rect"), c.src_w / float(gx::EFB_WIDTH), c.src_h / float(gx::EFB_HEIGHT), c.src_x / float(gx::EFB_WIDTH),
                1 - (c.src_y + c.src_h) / float(gx::EFB_HEIGHT));
    glUniform4f(u("sharp"), 1.0f / width_, 1.0f / height_, o && !shader_on ? std::clamp(o->sharpness, 0.0f, 1.0f) : 0.0f, 0.0f);
    const float out_w = shader_on ? float(sw) : vw, out_h = shader_on ? float(sh) : vh;
    const float bx = float(std::clamp(int(std::lround(double(c.src_w) * scale_ / std::max(out_w, 1.0f))), 1, 4));
    const float by = float(std::clamp(int(std::lround(double(c.src_h) * scale_ / std::max(out_h, 1.0f))), 1, 4));
    glUniform4f(u("box"), bx, by, 0, 0);
    glUniform4f(u("col"), o ? o->brightness : 1.0f, o ? o->contrast : 1.0f, o ? o->vibrance : 1.0f, 0.0f);
    glUniform4f(u("crt"), look.scan, look.mask, look.curve, fit.lines);
    glUniform4f(u("crt2"), float(c.src_w), vh / fit.lines, look.halation, fit.shift);
    glUniform1i(u("crt_on"), crt_on ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
#ifdef GX_PC_SETTINGS
    if (shader_on) {
      // The preset into the window-sized texture, then that onto the window.
      glBindFramebuffer(GL_FRAMEBUFFER, shader_output_.fbo);
      glViewport(0, 0, w, h); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
      const gx::user_shader::Target where{vx, vy, uint32_t(std::max(vw, 1.0f)), uint32_t(std::max(vh, 1.0f))};
      const bool drawn = gx::user_shader::gl_draw(shader_source_.id, sw, sh, shader_output_.id, uint32_t(w), uint32_t(h), where, presented_);
      glBindVertexArray(vao_); glUseProgram(0); glDisable(GL_SCISSOR_TEST);
      if (drawn) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, shader_output_.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      }
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
#endif
    glViewport(0, 0, w, h);
  }
  // A colour texture with its framebuffer, resized when needed (the shader preset's source and output).
  void shader_target(Texture& t, uint32_t w, uint32_t h) {
    if (t.id && t.width == w && t.height == h) return;
    if (!t.id) { glGenTextures(1, &t.id); glGenFramebuffers(1, &t.fbo); }
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(w), GLsizei(h), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.id, 0);
    t.width = w; t.height = h;
  }
  Texture shader_source_, shader_output_;
  GLuint build_picture_program() {
    GLuint v = compile_gl(GL_VERTEX_SHADER, R"(#version 450 core
out vec2 p;
void main() { vec2 q = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(q * 2.0 - 1.0, 0.0, 1.0); p = q; }
)");
    GLuint f = compile_gl(GL_FRAGMENT_SHADER, R"(#version 450 core
in vec2 p; layout(location = 0) out vec4 color;
uniform sampler2D src;
uniform vec4 rect, sharp, box, col, crt, crt2;
uniform int crt_on;
// t: a point of the picture, 0..1 from its top left (as the Windows shaders count).
vec2 picture_uv(vec2 t) { return rect.zw + vec2(t.x, 1.0 - t.y) * rect.xy; }
vec4 downsample(vec2 uv) {
  int nx = int(box.x), ny = int(box.y);
  if (nx <= 1 && ny <= 1) return texture(src, uv);
  vec2 foot = sharp.xy * vec2(nx, ny);
  vec4 acc = vec4(0.0);
  for (int y = 0; y < ny; ++y)
    for (int x = 0; x < nx; ++x) acc += texture(src, uv + (vec2(x, y) + 0.5) / vec2(nx, ny) * foot - 0.5 * foot);
  return acc / float(nx * ny);
}
vec3 crt_linear(vec3 c) { return pow(clamp(c, 0.0, 1.0), vec3(2.4)); }
vec3 crt_at(vec2 t) {
  vec2 half_texel = 0.5 * sharp.xy / rect.xy;
  return crt_linear(texture(src, picture_uv(clamp(t, half_texel, 1.0 - half_texel))).rgb);
}
vec3 crt_scanline(vec2 t, float line_y, vec2 cell) {
  const float ox[4] = float[](-0.9, -0.3, 0.3, 0.9);
  const float wx[4] = float[](0.262, 0.862, 0.862, 0.262);
  vec3 acc = vec3(0.0);
  for (int k = 0; k < 4; ++k) {
    float x = t.x + ox[k] * cell.x;
    acc += wx[k] * (crt_at(vec2(x, line_y - 0.25 * cell.y)) + crt_at(vec2(x, line_y + 0.25 * cell.y)));
  }
  return acc / (2.0 * 2.248);
}
vec3 crt_beam(vec3 c, float d, float depth, float pixel) {
  vec3 level = sqrt(clamp(c, 0.0, 1.0));
  vec3 sigma = mix(vec3(mix(0.62, 0.24, depth)), vec3(mix(0.62, 0.43, depth)), level);
  sigma = sqrt(sigma * sigma + pixel * pixel / 12.0);
  return c * exp(-0.5 * d * d / (sigma * sigma)) / (sigma * 2.5066);
}
vec3 crt_mask(vec2 pos, float depth, float per_line) {
  if (depth <= 0.0) return vec3(1.0);
  vec3 m;
  if (per_line >= 2.8) {
    int x = int(pos.x) % 3;
    m = x == 0 ? vec3(1, 0, 0) : (x == 1 ? vec3(0, 1, 0) : vec3(0, 0, 1));
    return mix(vec3(1.0), m, depth) / (1.0 - depth * 2.0 / 3.0);
  }
  m = (int(pos.x) & 1) != 0 ? vec3(0, 1, 0) : vec3(1, 0, 1);
  return mix(vec3(1.0), m, depth) / (1.0 - depth * 0.5);
}
vec3 crt_pixel(vec2 pos, vec2 t) {
  vec2 one_pixel = fwidth(t);
  float face = 1.0;
  if (crt.z > 0.0) {
    vec2 q = t * 2.0 - 1.0;
    q *= 1.0 + crt.z * vec2(q.y * q.y, q.x * q.x);
    q /= 1.0 + crt.z * 0.5;
    vec2 px = 4.0 * one_pixel;
    vec2 e = smoothstep(0.0, 1.0, (1.0 - abs(q)) / max(px, vec2(1e-5)));
    vec2 corner = max(abs(q) - (1.0 - 0.045), 0.0) / 0.045;
    face = e.x * e.y * clamp((1.0 - length(corner)) / max(max(px.x, px.y) / 0.045, 1e-5), 0.0, 1.0);
    face *= 1.0 - 0.35 * crt.z * dot(q, q) * 0.5;
    t = q * 0.5 + 0.5;
  }
  if (face <= 0.0) return vec3(0.0);
  float lines = max(crt.w, 1.0), per_line = max(crt2.y, 0.01);
  vec2 cell = 1.0 / vec2(max(crt2.x, 1.0), lines);
  float depth = crt.x * clamp(per_line - 1.0, 0.0, 1.0);
  float y = t.y * lines - 0.5 + crt2.w;
  float upper = floor(y), d = y - upper;
  vec3 a = crt_scanline(t, (upper + 0.5) * cell.y, cell);
  vec3 b = crt_scanline(t, (upper + 1.5) * cell.y, cell);
  float pixel = 1.0 / per_line;
  vec3 c = crt_beam(a, d, depth, pixel) + crt_beam(b, 1.0 - d, depth, pixel);
  if (crt2.z > 0.0) {
    vec2 h = 1.6 * cell;
    vec3 halo = 0.25 * (crt_at(t + vec2(-h.x, -h.y)) + crt_at(t + vec2(h.x, -h.y)) + crt_at(t + vec2(-h.x, h.y)) + crt_at(t + h));
    c = mix(c, max(c, halo), crt2.z);
  }
  c *= crt_mask(pos, crt.y, per_line);
  return pow(clamp(c * face, 0.0, 1.0), vec3(1.0 / 2.2));
}
void main() {
  vec2 uv = rect.zw + p * rect.xy;
  vec3 rgb;
  if (crt_on != 0) {
    rgb = crt_pixel(gl_FragCoord.xy, vec2(p.x, 1.0 - p.y));
  } else {
    vec4 c = downsample(uv);
    rgb = c.rgb;
    if (sharp.z > 0.0) {
      // Contrast-adaptive sharpening over neighbours one output pixel away.
      vec2 st = sharp.xy * max(box.xy, vec2(1.0));
      vec3 n = texture(src, uv + vec2(0, st.y)).rgb, s = texture(src, uv - vec2(0, st.y)).rgb;
      vec3 w = texture(src, uv - vec2(st.x, 0)).rgb, e = texture(src, uv + vec2(st.x, 0)).rgb;
      vec3 mn = min(min(min(n, s), min(w, e)), rgb), mx = max(max(max(n, s), max(w, e)), rgb);
      vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-4)), 0.0, 1.0));
      vec3 wgt = amp * (-1.0 / mix(8.0, 5.0, clamp(sharp.z, 0.0, 1.0)));
      rgb = clamp((rgb + (n + s + w + e) * wgt) / (1.0 + 4.0 * wgt), 0.0, 1.0);
    }
  }
  rgb = (rgb * col.x - 0.5) * col.y + 0.5;
  float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
  color = vec4(clamp(mix(vec3(luma), rgb, col.z), 0.0, 1.0), 1.0);
}
)");
    const GLuint program = link_gl(v, f); glDeleteShader(v); glDeleteShader(f);
    return program;
  }
#ifdef GX_PC_SETTINGS
  // A video frame in its slot's texture (re-uploaded only when the decoder has a new one). The rows
  // arrive as the Windows decoder hands them on, so they sample the same way.
  GLuint video_texture(const std::shared_ptr<const gx::video_bg::Frame>& f, int slot) {
    if (!f || slot < 0 || slot >= gx::video_bg::kVideoSlots || f->bgra.empty()) return 0;
    invalidate_state(); // Video uploads also bind on the current texture unit.
    VideoTexture& v = video_[slot];
    if (!v.id || v.width != f->width || v.height != f->height) {
      if (!v.id) glGenTextures(1, &v.id);
      glBindTexture(GL_TEXTURE_2D, v.id);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, f->width, f->height, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      v.width = f->width; v.height = f->height; v.serial = 0;
    }
    if (v.serial != f->serial) {
      glBindTexture(GL_TEXTURE_2D, v.id);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width, f->height, GL_BGRA, GL_UNSIGNED_BYTE, f->bgra.data());
      v.serial = f->serial;
    }
    return v.id;
  }
  // The copy shader's rectangle uses the Windows convention (v counted from the top), so the
  // Windows rectangles are used as they are.
  void copy_quad(GLuint texture, const float rect[4]) {
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(copy_program_);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture); glBindSampler(0, 0);
    glUniform1i(glGetUniformLocation(copy_program_, "source_texture"), 0);
    glUniform1i(glGetUniformLocation(copy_program_, "copy_kind"), 1);
    glUniform4f(glGetUniformLocation(copy_program_, "source_rect"), rect[0], rect[1], rect[2], rect[3]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  void draw_video_layer(const std::shared_ptr<const gx::video_bg::Frame>& f, int slot, const gx::EfbCopy& screen) {
    const GLuint id = video_texture(f, slot);
    if (!id) return;
    glBindVertexArray(vao_);
    glBindFramebuffer(GL_FRAMEBUFFER, draw_fbo());
    glEnable(GL_SCISSOR_TEST);
    const GLint x = GLint(screen.src_x * scale_), y = GLint(height_ - (screen.src_y + screen.src_h) * scale_);
    glViewport(x, y, GLsizei(screen.src_w * scale_), GLsizei(screen.src_h * scale_));
    glScissor(x, y, GLsizei(screen.src_w * scale_), GLsizei(screen.src_h * scale_));
    const float rect[4] = {1.0f, -1.0f, 0.0f, 1.0f};
    copy_quad(id, rect);
    glBindVertexArray(vao_);
    invalidate_state(); // The next game draw must restore the state changed by the video pass.
    if (!video_layer_logged_[slot & 1]) {
      video_layer_logged_[slot & 1] = true;
      std::fprintf(stderr, "video backgrounds: %s full-screen layer drawn (OpenGL)\n", slot ? "SSS" : "CSS");
    }
  }
  // Border art (Video settings) fills the window behind the picture, cropped to the window's shape.
  void border_art(int w, int h) {
    gx::video_bg::set_border(hooks_.options->border_art);
    bool reversed = false;
    const auto art = gx::video_bg::border_frame(&reversed);
    if (!art) return;
    const GLuint id = video_texture(art, gx::video_bg::kBorderSlot);
    if (!id) return;
    const float window = float(w) / std::max(float(h), 1.0f), shape = float(art->width) / std::max(float(art->height), 1.0f);
    const float sx = shape > window ? window / shape : 1.0f, sy = shape > window ? 1.0f : shape / window;
    const float rect[4] = {sx, reversed ? -sy : sy, (1.0f - sx) * 0.5f, reversed ? 1.0f - (1.0f - sy) * 0.5f : (1.0f - sy) * 0.5f};
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, w, h);
    copy_quad(id, rect);
  }
  struct VideoTexture { GLuint id = 0; uint32_t width = 0, height = 0; uint64_t serial = 0; };
  VideoTexture video_[gx::video_bg::kVideoSlots];
  bool video_layer_logged_[2] = {false, false};
#endif
  int draw_video_slot_ = -1;   // the video slot the last draw sampled (the stage select's layer follows it)
  void trim() {
    if (sequence_ % 120) return;
    invalidate_state(); // Deleting a bound texture also changes the binding.
    for (auto i = textures_.begin(); i != textures_.end();) {
      if (i->second.used + 180 < sequence_) { destroy(i->second); i = textures_.erase(i); } else ++i;
    }
    for (auto i = copies_.begin(); i != copies_.end();) {
      if (i->second.used + 3600 < sequence_) { destroy(i->second); i = copies_.erase(i); } else ++i;
    }
    uint64_t bytes = 0;
    for (const auto& item : textures_) bytes += item.second.bytes;
    constexpr uint64_t budget = 256ull << 20;
    if (bytes > budget) {
      std::vector<std::pair<uint64_t, uint64_t>> oldest;
      for (const auto& item : textures_)
        if (item.second.used != sequence_) oldest.emplace_back(item.second.used, item.first);
      std::sort(oldest.begin(), oldest.end());
      for (const auto& item : oldest) {
        if (bytes <= budget * 9 / 10) break;
        auto i = textures_.find(item.second);
        bytes -= i->second.bytes; destroy(i->second); textures_.erase(i);
      }
    }
  }
  SDL_Window* window_; SDL_GLContext context_ = nullptr; std::filesystem::path cache_;
  Hooks hooks_;
  GlProbe probe_;
  EmittedState emitted_;
  std::unordered_map<GLuint, ProgramAttributes> attribute_locations_;
  GLint copy_source_ = -1, copy_kind_ = -1, copy_rect_ = -1;
  int scale_, width_, height_; bool skip_ = false; uint64_t sequence_ = 0; uint32_t presented_ = 0, source_retrace_ = 0;
  GLuint vao_ = 0, vertices_ = 0, indices_ = 0, uniform_[2]{}, efb_ = 0, color_ = 0, depth_ = 0, copy_program_ = 0;
  GLuint msaa_fbo_ = 0, msaa_rb_[2]{};
  int msaa_samples_ = 1;
  GLuint last_frame_ = 0, last_color_ = 0; bool have_last_ = false;
#ifdef __APPLE__
  int drawable_w_ = 0, drawable_h_ = 0, surface_probe_ = 0;
#endif
  GLuint picture_program_ = 0;              // the present pass (picture())
  uint32_t capture_frame_ = 0;
  uint64_t capture_sequence_ = 0;           // the simulation frame being presented              // --capture-sim-frame resolved to a presented frame
  uint64_t replacement_bytes_ = 0;          // texture pack replacements held, against a 512 MB budget
  int anisotropy_applied_ = 1;              // the filtering the cached samplers were made with
  gx::EffectsFilter effects_filter_;   // "Visual effects" for the frame being submitted (submit_frame)
  gx::LightweightFilter lightweight_filter_;   // "Lightweight" for the frame being submitted
  GLuint white_ = 0;                           // "Lightweight" flat shading (white_texture)
  gx::Surface look_surface_ = gx::Surface::Textured;   // the look's treatment for the textures being bound
  bool widenable_scene_ = true;   // the frame being presented can be shown widened (frame_has_widenable_scene)
  bool clockwise_front_ = true;
#ifdef __APPLE__
  shader_translation::Queue translations_;
  size_t sync_cache_hits_ = 0, sync_cache_misses_ = 0;
  bool sync_shaders_ = [] { const char* v = std::getenv("MELEE_SYNC_SHADERS"); return v && std::strcmp(v, "1") == 0; }();
  bool boot_prewarm_ = false;
  double compile_spent_ms_ = 0, compile_budget_ms_ = env_ms("MELEE_ASYNC_COMPILE_MS", 3);
  Clock::time_point skip_report_ = Clock::now();
  uint64_t skipped_draws_ = 0;
  std::unordered_map<ProgramKey, ProgramBuild, KeyHash> builds_;
  std::deque<ProgramKey> compile_pending_;
#endif
  std::unordered_map<ProgramKey, GLuint, KeyHash> programs_;
#ifdef __APPLE__
  std::vector<ProgramRecipe> recipes_, pending_; size_t saved_recipes_ = 0, pending_next_ = 0;
  std::unordered_set<uint64_t> recipe_hashes_; bool binaries_ = false, prewarming_ = false;
#else
  std::vector<ProgramRecipe> recipes_; size_t saved_recipes_ = 0; bool binaries_ = false, prewarming_ = false;
#endif
  std::unordered_map<uint64_t, GLuint> shaders_, samplers_;
#ifndef __APPLE__
  // Background shader translation (shader()). Workers only run the tools and write files; every GL
  // call stays on the render thread.
  struct TranslateJob { uint64_t key; GLenum stage; std::string source; std::filesystem::path prefix, output; };
  std::mutex translate_mutex_;
  std::condition_variable translate_cv_;
  std::deque<TranslateJob> translate_jobs_;
  std::unordered_set<uint64_t> translate_pending_, translate_failed_;
  std::vector<std::thread> translators_;
  bool translate_quit_ = false;
  std::atomic<uint64_t> waiting_draws_{0}; uint64_t translated_ = 0;
  void start_translators() {   // translate_mutex_ held
    if (!translators_.empty()) return;
    const unsigned n = std::clamp(std::thread::hardware_concurrency() / 4, 1u, 3u);
    for (unsigned i = 0; i < n; ++i) translators_.emplace_back([this] { translate_worker(); });
  }
  void translate_worker() {
    for (;;) {
      TranslateJob job;
      {
        std::unique_lock<std::mutex> lock(translate_mutex_);
        translate_cv_.wait(lock, [&] { return translate_quit_ || !translate_jobs_.empty(); });
        if (translate_quit_) return;
        job = std::move(translate_jobs_.front()); translate_jobs_.pop_front();
      }
      bool ok = true;
      try { translate_shader(job.stage, job.source, job.prefix, job.output); }
      catch (const std::exception& e) { ok = false; std::fprintf(stderr, "opengl: background shader translation failed: %s\n", e.what()); }
      std::lock_guard<std::mutex> lock(translate_mutex_);
      translate_pending_.erase(job.key);
      if (!ok) translate_failed_.insert(job.key);
      else if ((++translated_ & 15) == 1)
        std::fprintf(stderr, "opengl: %llu shaders translated in the background so far; %llu draws waited for one\n",
                     (unsigned long long)translated_, (unsigned long long)waiting_draws_.load());
    }
  }
  void stop_translators() {
    { std::lock_guard<std::mutex> lock(translate_mutex_); translate_quit_ = true; }
    translate_cv_.notify_all();
    for (auto& t : translators_) t.join();
    translators_.clear();
  }
#endif
  std::unordered_map<uint64_t, Texture> textures_; std::unordered_map<uint32_t, Texture> copies_;
  std::vector<uint8_t> decoded_; std::vector<uint32_t> index_scratch_;
  std::vector<size_t> vertex_offsets_;
};
} // namespace

std::unique_ptr<gx::Backend> create(SDL_Window* window, const std::filesystem::path& cache, int scale, bool vsync) {
  return std::make_unique<Renderer>(window, cache, scale, vsync);
}
void capture_efb(gx::Backend& backend, const std::filesystem::path& path) {
#ifdef MELEE_LINUX_UNLOCKED
  if (capture_threaded(backend, path)) return;
#endif
  dynamic_cast<Renderer&>(backend).capture(path);
}
void capture_culling_variants(gx::Backend& backend, const gx::Frame& frame, const std::filesystem::path& path) {
  dynamic_cast<Renderer&>(backend).compare_culling(frame, path);
}
bool smoke_test(gx::Backend& backend, const std::filesystem::path& capture) {
  gx::Frame frame; gx::DrawCall dc{};
  dc.primitive = 0x90; dc.vertex_count = 3; dc.components = gx::VB_HAS_COL0;
  dc.bp.reg[gx::BP_BLENDMODE] = 8 | 16;
  dc.bp.reg[gx::BP_SCISSORTL] = (342 << 12) | 342; dc.bp.reg[gx::BP_SCISSORBR] = (981 << 12) | 869;
  dc.bp.reg[gx::BP_SCISSOROFFSET] = 171 | (171 << 10);
  dc.bp.reg[gx::BP_GENMODE] = 0;
  // One TEV pass emits vertex colour; alpha compare ALWAYS, logic operations off.
  dc.bp.reg[gx::BP_TEV_COLOR_ENV] = (15 << 12) | (15 << 8) | (15 << 4) | 10 | (1 << 19);
  dc.bp.reg[gx::BP_TEV_ALPHA_ENV] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
  dc.bp.reg[gx::BP_ALPHACOMPARE] = (7 << 16) | (7 << 19);
  dc.bp.reg[gx::BP_TEV_KSEL] = 0 | (1 << 2);
  dc.bp.reg[gx::BP_TEV_KSEL + 1] = 2 | (3 << 2);
  dc.xf_regs[0x08] = 0; dc.xf_regs[0x09] = 0;
  float viewport[6] = {320, -264, 16777216, 662, 606, 16777216};
  std::memcpy(&dc.xf_regs[0x1A], viewport, sizeof viewport);
  // Orthographic projection with NDC x/y and a reversed depth of 0.5.
  float projection[7] = {1,0,1,0,0,-0.5f,1};
  std::memcpy(&dc.xf_regs[0x20], projection, sizeof projection);
  dc.posMatrices[0] = dc.posMatrices[5] = dc.posMatrices[10] = 1;
  const float positions[3][3] = {{-0.8f,-0.7f,0},{0.8f,-0.7f,0},{0,0.7f,0}};
  for (int i = 0; i < 3; ++i) { gx::Vertex v{}; std::memcpy(v.pos, positions[i], sizeof v.pos); v.col0[i] = 255; v.col0[3] = 255; frame.vertices.push_back(v); }
  frame.draws.push_back(dc); frame.commands.push_back({gx::FrameCommand::Draw, 0});
  // A second draw exercises separate HLSL texture/sampler arrays, a nonzero
  // texture unit, UV generation and a mip upload through the same translation.
  gx::DrawCall textured = dc;
  textured.first_vertex = 3; textured.components |= gx::VB_HAS_UV0;
  textured.bp.reg[gx::BP_GENMODE] = 1;
  textured.bp.reg[gx::BP_TREF] = 7 | (1 << 6);
  textured.bp.reg[gx::BP_TEV_COLOR_ENV] = (15 << 12) | (15 << 8) | (15 << 4) | 8 | (1 << 19);
  textured.bp.reg[gx::BP_TEV_ALPHA_ENV] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  textured.xf_regs[0x3F] = 1;
  textured.xf_regs[0x40] = 5 << 7; // first UV as the texgen source
  textured.matrix_index_a = 60 << 6; // identity texture matrix
  auto pixels = std::make_shared<gx::TextureSnapshot>();
  pixels->image.assign(32, 200); pixels->hash = gx::hash_bytes(pixels->image.data(), pixels->image.size());
  auto& t = textured.textures[7]; t.used = true; t.addr = 0x1000; t.width = 8; t.height = 4; t.format = 1; t.data = pixels;
  for (int i = 0; i < 3; ++i) {
    auto v = frame.vertices[i]; v.pos[0] = v.pos[0] * 0.25f + 0.6f; v.pos[1] *= 0.25f;
    v.texmtx[0] = 60; v.uv[0][0] = i == 1 ? 1 : 0; v.uv[0][1] = i == 2 ? 1 : 0;
    frame.vertices.push_back(v);
  }
  frame.draws.push_back(textured); frame.commands.push_back({gx::FrameCommand::Draw, 1});
  gx::EfbCopy present{}; present.to_xfb = true; present.src_w = 640; present.src_h = 528;
  frame.copies.push_back(present); frame.commands.push_back({gx::FrameCommand::Copy, 0});
#ifdef __APPLE__
  // A cold async renderer needs several frames for the helper results to arrive. This test
  // repeats its fixed picture; the live frame path never waits for a translation.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
  bool matched = false;
  do {
    backend.submit_frame(frame);
    matched = dynamic_cast<Renderer&>(backend).check_smoke_pixels();
    if (matched) break;
    SDL_PumpEvents(); SDL_Delay(10);
  } while (std::chrono::steady_clock::now() < deadline);
  capture_efb(backend, capture);
#else
  dynamic_cast<Renderer&>(backend).prepare_smoke_frame(frame);
  backend.submit_frame(frame); capture_efb(backend, capture);
#endif
  const GLenum error = glGetError(); if (error != GL_NO_ERROR) throw std::runtime_error("OpenGL smoke error " + std::to_string(error));
#ifdef __APPLE__
  if (!matched)
#else
  if (!dynamic_cast<Renderer&>(backend).check_smoke_pixels())
#endif
    throw std::runtime_error("OpenGL GX smoke pixels did not match vertex-color and unit-7 texture draws");
  return true;
}
void set_hooks(gx::Backend& backend, Hooks hooks) { dynamic_cast<Renderer&>(backend).set_hooks(std::move(hooks)); }
} // namespace linux_renderer
