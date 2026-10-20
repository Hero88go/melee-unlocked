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
#include <vector>
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
    for (const auto& command : frame.commands) {
      if (command.kind == gx::FrameCommand::Draw) {
        const auto& dc = frame.draws.at(command.index);
        draw(frame, dc, overrides ? overrides + command.index : nullptr, vertex_offsets_[command.index]);
      } else {
        const auto& c = frame.copies.at(command.index);
        if (c.to_xfb) { if (!skip_) present(c, frame.test_fill); }
        else copy(c);
        if (c.clear) clear(c);
      }
    }
    trim();
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
      const std::string input = prefix.string() + ".hlsl", spv = prefix.string() + ".spv", translated = prefix.string() + ".glsl";
      const std::filesystem::path log = prefix.string() + ".log";
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
      write_text(input, source);
      run_tool({"glslangValidator", "-D", "-V", "--auto-map-locations", "--auto-map-bindings", "--hlsl-iomap", "-e", "main", "-S", stage == GL_VERTEX_SHADER ? "vert" : "frag", "-o", spv, input}, log);
      std::vector<std::string> args{"spirv-cross", spv, "--version", "450", "--no-es", "--output", translated};
      if (stage == GL_VERTEX_SHADER) args.push_back("--fixup-clipspace");
      run_tool(std::move(args), log);
      glsl = read_text(translated);
      if (glsl.empty()) throw std::runtime_error("SPIRV-Cross produced an empty shader");
      std::filesystem::rename(translated, output);
      std::filesystem::remove(input); std::filesystem::remove(spv); std::filesystem::remove(log);
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
      p = link_gl(shader(GL_VERTEX_SHADER, gx::generate_vertex_shader(vs)), shader(GL_FRAGMENT_SHADER, gx::generate_pixel_shader(ps)), binaries_);
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
    const uint64_t desc[] = {t.data->hash, t.width, t.height, t.format, t.tlut_format, t.mip_levels};
    const uint64_t key = gx::hash_bytes(desc, sizeof desc);
    if (auto i = textures_.find(key); i != textures_.end()) { i->second.used = sequence_; return i->second.id; }
    Texture entry; entry.width = t.width; entry.height = t.height; entry.used = sequence_;
    entry.levels = gx::texture_mip_count(t.width, t.height, t.mip_levels);
    glGenTextures(1, &entry.id); glBindTexture(GL_TEXTURE_2D, entry.id);
    const uint8_t* data = t.data->image.data(); size_t remaining = t.data->image.size();
    uint32_t w = t.width, h = t.height;
    for (uint32_t level = 0; level < entry.levels; ++level) {
      const uint32_t bytes = gx::texture_level_bytes(w, h, t.format);
      if (bytes > remaining) { destroy(entry); throw std::runtime_error("GX texture mip exceeded snapshot bytes"); }
      gx::decode_texture(data, w, h, t.format, t.data->palette.data(), t.tlut_format, decoded_);
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
    samplers_.emplace(key, s); return s;
  }
  void draw(const gx::Frame& frame, const gx::DrawCall& dc, const gx::DrawMatrices* matrices, size_t vertex_override) {
    GLuint p = program(dc); glUseProgram(p); attributes(p); glBindFramebuffer(GL_FRAMEBUFFER, efb_);
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
    if (test_fill < 0) {
      const double aspect = hooks_.options ? gx::presented_aspect(*hooks_.options, w, h, false) : 73.0 / 60.0;
      int pw = w, ph = std::lround(w / aspect);
      if (ph > h) { ph = h; pw = std::lround(h * aspect); }
      glBindFramebuffer(GL_READ_FRAMEBUFFER, efb_);
      glBlitFramebuffer(c.src_x * scale_, height_ - (c.src_y + c.src_h) * scale_, (c.src_x + c.src_w) * scale_, height_ - c.src_y * scale_, (w - pw) / 2, (h - ph) / 2, (w + pw) / 2, (h + ph) / 2, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    if (hooks_.before_swap) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, w, h);
      hooks_.before_swap(w, h);
      glBindVertexArray(vao_);
    }
    SDL_GL_SwapWindow(window_); ++presented_; glBindFramebuffer(GL_FRAMEBUFFER, efb_);
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) throw std::runtime_error("OpenGL GX frame failed with error " + std::to_string(error));
  }
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
  bool clockwise_front_ = true;
  std::unordered_map<ProgramKey, GLuint, KeyHash> programs_;
  std::vector<ProgramRecipe> recipes_; size_t saved_recipes_ = 0; bool binaries_ = false, prewarming_ = false;
  std::unordered_map<uint64_t, GLuint> shaders_, samplers_;
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
