// Native Linux OpenGL backend for the shared Melee GX command decoder.
// The generated GX shaders remain the authoritative TEV/lighting implementation:
// glslang and SPIRV-Cross translate them to GLSL, with persistent caching.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "renderer_gl.h"
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
#include <GL/glew.h>
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
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
namespace linux_renderer {
namespace {
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
GLuint compile_gl(GLenum stage, const std::string& source) {
  GLuint shader = glCreateShader(stage);
  const char* data = source.c_str();
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

class Renderer final : public gx::Backend {
 public:
  Renderer(SDL_Window* window, const std::filesystem::path& cache, int scale, bool vsync)
      : window_(window), cache_(cache / ("gx-gl-v5-s" + std::to_string(std::clamp(scale, 1, 6)))), scale_(std::clamp(scale, 1, 6)),
        width_(gx::EFB_WIDTH * scale_), height_(gx::EFB_HEIGHT * scale_) {
    std::filesystem::create_directories(cache_);
    context_ = SDL_GL_CreateContext(window_);
    if (!context_) throw std::runtime_error(std::string("SDL OpenGL context: ") + SDL_GetError());
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK || !GLEW_VERSION_4_5) throw std::runtime_error("OpenGL 4.5 is required");
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
    GLint formats = 0; glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
    binaries_ = formats > 0;
    prewarm();
  }
  ~Renderer() override {
    stop_translators();
    if (!context_) return;
    save_recipes();
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
    if (hooks_.frame_begin) hooks_.frame_begin(frame);
    widenable_scene_ = gx::frame_has_widenable_scene(frame);   // as the D3D backends: 16:9 reaches the screen
    effects_filter_ = gx::effects_filter(frame, hooks_.options ? hooks_.options->effects_level : 0);   // "Visual effects"
#ifdef GX_PC_SETTINGS
    if (hooks_.options) {
      // Texture packs switched on or off, or a skin picked on the character select: every cached
      // texture was built with or without its replacement (as the Windows renderers).
      gx::video_bg::set_enabled(hooks_.options->video_backgrounds);
      gx::video_bg::begin_frame(frame.scene_major, frame.scene_minor);
      if (gx::texpack::configure(hooks_.options->custom_textures, hooks_.options->dump_textures) || gx::texpack::take_cosmetics_changed()) {
        for (auto& t : textures_) destroy(t.second);
        textures_.clear();
        replacement_bytes_ = 0;
      }
    }
#endif
    if (hooks_.options) {
      if (hooks_.options->anisotropy != anisotropy_applied_) {
        anisotropy_applied_ = hooks_.options->anisotropy;
        for (auto& smp : samplers_) glDeleteSamplers(1, &smp.second);
        samplers_.clear();
      }
    }
    ++sequence_; glBindVertexArray(vao_);
    const size_t original_count = frame.vertices.size();
    vertex_offsets_.assign(frame.draws.size(), 0);
    size_t total_count = original_count;
    if (overrides) for (size_t i = 0; i < frame.draws.size(); ++i)
      if (overrides[i].vertices) { vertex_offsets_[i] = total_count; total_count += frame.draws[i].vertex_count; }
    glBindBuffer(GL_ARRAY_BUFFER, vertices_);
    glBufferData(GL_ARRAY_BUFFER, total_count * sizeof(gx::Vertex), nullptr, GL_STREAM_DRAW);
    if (original_count) glBufferSubData(GL_ARRAY_BUFFER, 0, original_count * sizeof(gx::Vertex), frame.vertices.data());
    if (overrides) for (size_t i = 0; i < frame.draws.size(); ++i)
      if (overrides[i].vertices) glBufferSubData(GL_ARRAY_BUFFER, vertex_offsets_[i] * sizeof(gx::Vertex), frame.draws[i].vertex_count * sizeof(gx::Vertex), overrides[i].vertices);
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
        if (c.to_xfb) { if (!skip_) { capture_sequence_ = frame.sequence; present(c, frame.test_fill); } }
        else copy(c);
        if (c.clear) clear(c);
      }
    }
    trim();
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
    std::fprintf(stderr, "opengl: prewarmed %zu programs from %zu recipes before guest startup in %.1f ms%s\n",
                 programs_.size(), count, ms, binaries_ ? "" : " (driver keeps no program binaries)");
  }
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
    GLuint compiled = compile_gl(stage, glsl); shaders_.emplace(key, compiled); return compiled;
  }
  GLuint program(const gx::DrawCall& dc) {
    const auto vs = gx::make_vs_uid(dc); const auto ps = gx::make_ps_uid(dc);
    ProgramKey key{vs.hash(), ps.hash()};
    if (auto i = programs_.find(key); i != programs_.end()) return i->second;
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
    programs_.emplace(key, p); return p;
  }
  void attributes(GLuint p) {
    glBindBuffer(GL_ARRAY_BUFFER, vertices_);
    for (unsigned i = 0; i < 16; ++i) glDisableVertexAttribArray(i);
    auto add = [p](const char* name, int count, GLenum type, bool normalized, size_t offset, bool integer = false) {
      const GLint loc = glGetAttribLocation(p, name); if (loc < 0) return;
      glEnableVertexAttribArray(loc);
      if (integer) glVertexAttribIPointer(loc, count, type, sizeof(gx::Vertex), reinterpret_cast<void*>(offset));
      else glVertexAttribPointer(loc, count, type, normalized ? GL_TRUE : GL_FALSE, sizeof(gx::Vertex), reinterpret_cast<void*>(offset));
    };
    add("rawpos", 3, GL_FLOAT, false, 0); add("rawnorm0", 3, GL_FLOAT, false, 12);
    add("color0", 4, GL_UNSIGNED_BYTE, true, 24); add("color1", 4, GL_UNSIGNED_BYTE, true, 28);
    for (int i = 0; i < 8; ++i) { const auto name = "rawtex" + std::to_string(i); add(name.c_str(), 2, GL_FLOAT, false, 32 + 8 * i); }
    add("blend_indices", 4, GL_UNSIGNED_BYTE, false, 96, true);
    add("blend_indices2", 4, GL_UNSIGNED_BYTE, false, 100, true);
    add("blend_index7", 1, GL_UNSIGNED_BYTE, false, 104, true);
  }
  GLuint texture(const gx::TextureRef& t) {
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
    const uint64_t key = gx::hash_bytes(desc, sizeof desc);
    if (auto i = textures_.find(key); i != textures_.end()) { i->second.used = sequence_; return i->second.id; }
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
        std::unique_ptr<gx::texpack::Replacement> r = gx::texpack::load(pack_name, replacement_bytes_ < budget ? budget - replacement_bytes_ : 0);
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
    if (anisotropy_applied_ > 1 && linear && gx::bits(mode0, 4, 1) && (GLEW_ARB_texture_filter_anisotropic || GLEW_EXT_texture_filter_anisotropic))
      glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY_EXT, float(std::clamp(anisotropy_applied_, 1, 16)));
    samplers_.emplace(key, s); return s;
  }
  void draw(const gx::Frame& frame, const gx::DrawCall& dc, const gx::DrawMatrices* matrices, size_t vertex_override) {
    if (gx::skip_for_effects(effects_filter_, dc)) return;
    GLuint p = program(dc);
    if (!p) { ++waiting_draws_; return; }   // its shader is still being translated (see shader())
    glUseProgram(p); attributes(p); glBindFramebuffer(GL_FRAMEBUFFER, efb_);
    const float* vp = reinterpret_cast<const float*>(&dc.xf_regs[0x1A]);
    float x = (vp[3] - vp[0] - 342) * scale_, y = (vp[4] + vp[1] - 342) * scale_;
    float w = vp[0] * 2 * scale_, h = -vp[1] * 2 * scale_;
    if (w < 0) { x += w; w = -w; } if (h < 0) { y += h; h = -h; }
    glViewportIndexedf(0, x, height_ - y - h, std::max(w, 1.0f), std::max(h, 1.0f));
    float znear = std::clamp(1 - vp[5] / 16777216.0f, 0.0f, 1.0f), zfar = std::clamp(1 - (vp[5] - vp[2]) / 16777216.0f, 0.0f, 1.0f);
    if (zfar < znear) std::swap(zfar, znear);
    glDepthRange(znear, zfar);
    const auto tl = dc.bp.reg[gx::BP_SCISSORTL], br = dc.bp.reg[gx::BP_SCISSORBR], so = dc.bp.reg[gx::BP_SCISSOROFFSET];
    const int ox = gx::bits(so, 0, 10) * 2 - 342, oy = gx::bits(so, 10, 10) * 2 - 342;
    const int l = std::clamp(int(gx::bits(tl, 12, 12)) - ox - 342, 0, gx::EFB_WIDTH), t = std::clamp(int(gx::bits(tl, 0, 12)) - oy - 342, 0, gx::EFB_HEIGHT);
    const int r = std::clamp(int(gx::bits(br, 12, 12)) - ox - 341, 0, gx::EFB_WIDTH), b = std::clamp(int(gx::bits(br, 0, 12)) - oy - 341, 0, gx::EFB_HEIGHT);
    if (r <= l || b <= t) return;
    glEnable(GL_SCISSOR_TEST); glScissor(l * scale_, height_ - b * scale_, (r - l) * scale_, (b - t) * scale_);
    const auto blend = dc.bp.blendmode(), zmode = dc.bp.zmode(), cull = dc.bp.cullmode();
    const GLenum src[] = {GL_ZERO,GL_ONE,GL_DST_COLOR,GL_ONE_MINUS_DST_COLOR,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_DST_ALPHA,GL_ONE_MINUS_DST_ALPHA};
    const GLenum dst[] = {GL_ZERO,GL_ONE,GL_SRC_COLOR,GL_ONE_MINUS_SRC_COLOR,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_DST_ALPHA,GL_ONE_MINUS_DST_ALPHA};
    GLenum sf = src[gx::bits(blend, 8, 3)], df = dst[gx::bits(blend, 5, 3)];
    if ((dc.bp.zcontrol() & 7) != 1) { if (sf == GL_DST_ALPHA) sf = GL_ONE; if (sf == GL_ONE_MINUS_DST_ALPHA) sf = GL_ZERO; if (df == GL_DST_ALPHA) df = GL_ONE; if (df == GL_ONE_MINUS_DST_ALPHA) df = GL_ZERO; }
    if (blend & 1) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFuncSeparate(sf, df, GL_ONE, GL_ZERO); glBlendEquationSeparate((blend & (1 << 11)) ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD, GL_FUNC_ADD);
    glColorMask(blend & 8, blend & 8, blend & 8, blend & 16);
    const GLenum compare[] = {GL_NEVER,GL_GREATER,GL_EQUAL,GL_GEQUAL,GL_LESS,GL_NOTEQUAL,GL_LEQUAL,GL_ALWAYS};
    if (zmode & 1) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask((zmode & 1) && (zmode & 16)); glDepthFunc(compare[gx::bits(zmode, 1, 3)]);
    if (cull) { glEnable(GL_CULL_FACE); glFrontFace(clockwise_front_ ? GL_CW : GL_CCW); glCullFace(cull == 2 ? GL_FRONT : cull == 3 ? GL_FRONT_AND_BACK : GL_BACK); } else glDisable(GL_CULL_FACE);
    gx::VSConstants vc{}; gx::PSConstants pc{};
    gx::fill_vs_constants(dc, vc, scale_, matrices); gx::fill_ps_constants(dc, pc, scale_);
    glBindBuffer(GL_UNIFORM_BUFFER, uniform_[0]); glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof vc, &vc);
    glBindBuffer(GL_UNIFORM_BUFFER, uniform_[1]); glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof pc, &pc);
    for (int unit = 0; unit < 8; ++unit) {
      glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, dc.textures[unit].used ? texture(dc.textures[unit]) : 0);
      glBindSampler(unit, sampler(dc.textures[unit].mode0, dc.textures[unit].mode1));
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
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices_); glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_scratch_.size() * sizeof(uint32_t), index_scratch_.data(), GL_STREAM_DRAW);
    glDrawElements(topology == gx::DrawTopology::Lines ? GL_LINES : GL_TRIANGLES, index_scratch_.size(), GL_UNSIGNED_INT, nullptr);
  }
  void clear(const gx::EfbCopy& c) {
    glBindFramebuffer(GL_FRAMEBUFFER, efb_); glEnable(GL_SCISSOR_TEST);
    glScissor(c.src_x * scale_, height_ - (c.src_y + c.src_h) * scale_, c.src_w * scale_, c.src_h * scale_);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_TRUE);
    glClearColor(((c.clear_color >> 16) & 255) / 255.0f, ((c.clear_color >> 8) & 255) / 255.0f, (c.clear_color & 255) / 255.0f, (c.clear_color >> 24) / 255.0f);
    glClearDepth(1 - c.clear_z / 16777215.0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
  void copy(const gx::EfbCopy& c) {
    if (!c.src_w || !c.src_h) return;
    const uint32_t w = std::max(1u, c.src_w / (c.half_scale ? 2 : 1)) * scale_, h = std::max(1u, c.src_h / (c.half_scale ? 2 : 1)) * scale_;
    auto& e = copies_[c.dest_addr & 0x3FFFFFFFu];
    if (!e.id || e.width != w || e.height != h) {
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
    glUniform1i(glGetUniformLocation(copy_program_, "source_texture"), 0);
    glUniform1i(glGetUniformLocation(copy_program_, "copy_kind"), c.is_depth ? 2 : (c.efb_alpha ? 0 : 1));
    // EFB is stored bottom-up by OpenGL; sampled GX textures store their first (top) row at y=0.
    glUniform4f(glGetUniformLocation(copy_program_, "source_rect"), c.src_w / float(gx::EFB_WIDTH), c.src_h / float(gx::EFB_HEIGHT), c.src_x / float(gx::EFB_WIDTH), 1 - (c.src_y + c.src_h) / float(gx::EFB_HEIGHT));
    glDrawArrays(GL_TRIANGLES, 0, 3); glBindFramebuffer(GL_FRAMEBUFFER, efb_);
  }
  void present(const gx::EfbCopy& c, int test_fill) {
    int w, h; SDL_GL_GetDrawableSize(window_, &w, &h); if (w <= 0 || h <= 0) return;
    glCopyImageSubData(color_, GL_TEXTURE_2D, 0, 0, 0, 0, last_color_, GL_TEXTURE_2D, 0, 0, 0, 0, width_, height_, 1);
    have_last_ = true;
    glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    const float fill = test_fill > 0 ? 1.0f : 0.0f; glClearColor(fill, fill, fill, 1); glClear(GL_COLOR_BUFFER_BIT);
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
      glBindVertexArray(vao_);
    }
    if (test_fill < 0) { ++presented_; captures(capture_sequence_); --presented_; }   // the finished window, menus included
    SDL_GL_SwapWindow(window_); ++presented_; glBindFramebuffer(GL_FRAMEBUFFER, efb_);
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) throw std::runtime_error("OpenGL GX frame failed with error " + std::to_string(error));
  }
  // The picture on the window: the EFB's frame region averaged over each output pixel when it is
  // larger (supersampling), sharpened, with brightness, contrast and vibrance, or drawn through the
  // CRT tube model. The same maths as the Windows present pass (gx_d3d11.cpp, crt_shader.h).
  void picture(const gx::EfbCopy& c, int w, int h, int pw, int ph) {
    const gx::RenderOptions* o = hooks_.options;
    if (!picture_program_) picture_program_ = build_picture_program();
    const gx::CrtLook look = gx::crt_look(o ? o->crt_filter : 0);
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
    gx::CrtFit fit{vh, 0.0f};
    // Only the built-in CRT model snaps to whole pixels per line; a preset smooths its own lines: no border.
    if (crt_on) { fit = gx::crt_fit(vh, lines); vh = fit.height; vw = vh * float(pw) / float(std::max(ph, 1)); }
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
    glUniform4f(u("crt"), look.scan, look.mask, look.curve, lines);
    glUniform4f(u("crt2"), float(c.src_w), vh / lines, look.halation, fit.shift);
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
    glBindFramebuffer(GL_FRAMEBUFFER, efb_);
    glEnable(GL_SCISSOR_TEST);
    const GLint x = GLint(screen.src_x * scale_), y = GLint(height_ - (screen.src_y + screen.src_h) * scale_);
    glViewport(x, y, GLsizei(screen.src_w * scale_), GLsizei(screen.src_h * scale_));
    glScissor(x, y, GLsizei(screen.src_w * scale_), GLsizei(screen.src_h * scale_));
    const float rect[4] = {1.0f, -1.0f, 0.0f, 1.0f};
    copy_quad(id, rect);
    glBindVertexArray(vao_);
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
  int scale_, width_, height_; bool skip_ = false; uint64_t sequence_ = 0; uint32_t presented_ = 0;
  GLuint vao_ = 0, vertices_ = 0, indices_ = 0, uniform_[2]{}, efb_ = 0, color_ = 0, depth_ = 0, copy_program_ = 0;
  GLuint last_frame_ = 0, last_color_ = 0; bool have_last_ = false;
  GLuint picture_program_ = 0;              // the present pass (picture())
  uint32_t capture_frame_ = 0;
  uint64_t capture_sequence_ = 0;           // the simulation frame being presented              // --capture-sim-frame resolved to a presented frame
  uint64_t replacement_bytes_ = 0;          // texture pack replacements held, against a 512 MB budget
  int anisotropy_applied_ = 1;              // the filtering the cached samplers were made with
  gx::EffectsFilter effects_filter_;   // "Visual effects" for the frame being submitted (submit_frame)
  bool widenable_scene_ = true;   // the frame being presented can be shown widened (frame_has_widenable_scene)
  bool clockwise_front_ = true;
  std::unordered_map<ProgramKey, GLuint, KeyHash> programs_;
  std::vector<ProgramRecipe> recipes_; size_t saved_recipes_ = 0; bool binaries_ = false, prewarming_ = false;
  std::unordered_map<uint64_t, GLuint> shaders_, samplers_;
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
  backend.submit_frame(frame); capture_efb(backend, capture);
  const GLenum error = glGetError(); if (error != GL_NO_ERROR) throw std::runtime_error("OpenGL smoke error " + std::to_string(error));
  if (!dynamic_cast<Renderer&>(backend).check_smoke_pixels())
    throw std::runtime_error("OpenGL GX smoke pixels did not match vertex-color and unit-7 texture draws");
  return true;
}
void set_hooks(gx::Backend& backend, Hooks hooks) { dynamic_cast<Renderer&>(backend).set_hooks(std::move(hooks)); }
} // namespace linux_renderer
