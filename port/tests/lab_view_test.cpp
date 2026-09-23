// Lab view: events in, silhouettes out.
//
// With no arguments this is a unit test: it writes a two-frame character pack (a square, and a
// circle written the way svgo writes arcs), feeds the Slippi events a match would send, and checks
// where the silhouette lands on screen and that nothing is drawn outside a match.
//
// With arguments it replays a real .slp through the same path the EXI device uses and dumps the
// ImGui draw data for chosen frames, which tools/lab_render.py turns into PNGs. That is how the
// view can be checked without a Windows machine or an ISO:
//   port_lab_view_test <replay.slp> <lab dir> <output prefix> <frame> [<frame>...]
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.h"
#include "lab_view.h"
#include "imgui.h"
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace host {
Options options;
void log(const char* fmt, ...) { va_list a; va_start(a, fmt); std::vfprintf(stderr, fmt, a); va_end(a); std::fputc('\n', stderr); }
}

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

void put16(std::vector<uint8_t>& v, size_t at, uint16_t x) { v[at] = (uint8_t)(x >> 8); v[at + 1] = (uint8_t)x; }
void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) { for (int i = 0; i < 4; ++i) v[at + i] = (uint8_t)(x >> (24 - 8 * i)); }
void putf(std::vector<uint8_t>& v, size_t at, float f) { uint32_t u; std::memcpy(&u, &f, 4); put32(v, at, u); }

struct Box { float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; int n = 0; };

// Draws one UI frame and returns the extent of what was drawn in `color` above the HUD (which
// uses the player's color too, along the bottom).
Box frame_box(ImU32 color, float w, float h) {
  ImGui::GetIO().DisplaySize = ImVec2(w, h);
  ImGui::NewFrame();
  lab::draw(true, w, h);
  ImGui::Render();
  Box b;
  for (ImDrawList* list : ImGui::GetDrawData()->CmdLists)
    for (const ImDrawVert& v : list->VtxBuffer)
      if (v.col == color && v.pos.y < h * 0.75f) {
        b.x0 = std::min(b.x0, v.pos.x); b.x1 = std::max(b.x1, v.pos.x);
        b.y0 = std::min(b.y0, v.pos.y); b.y1 = std::max(b.y1, v.pos.y);
        ++b.n;
      }
  return b;
}

int draw_vertex_count(float w, float h) {
  ImGui::GetIO().DisplaySize = ImVec2(w, h);
  ImGui::NewFrame();
  lab::draw(true, w, h);
  ImGui::Render();
  return ImGui::GetDrawData()->TotalVtxCount;
}

void write_pack(const std::filesystem::path& file) {
  std::vector<uint8_t> b;
  auto u8 = [&](uint8_t x) { b.push_back(x); };
  auto u16 = [&](uint16_t x) { b.push_back((uint8_t)x); b.push_back((uint8_t)(x >> 8)); };
  auto u32 = [&](uint32_t x) { for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(x >> (8 * i))); };
  auto f32 = [&](float f) { uint32_t u; std::memcpy(&u, &f, 4); u32(u); };
  b.insert(b.end(), {'M', 'L', 'A', 'B'});
  u32(1);
  f32(1.0f); f32(0); f32(9); f32(14);
  u16(400);
  for (int a = 0; a < 400; ++a) u16(a == 14 ? 0 : 0xFFFF);   // Wait -> animation 0
  u16(1);
  const char* name = "Wait1";
  u8((uint8_t)std::strlen(name)); b.insert(b.end(), name, name + std::strlen(name));
  u16(2); u16(0); u16(1);
  const char* paths[2] = {
    "M400 400h200v200H400z",                                          // 200 x 200 square around the center
    "M500 400a100 100 0 1 1 0 200 100 100 0 1 1 0-200z",             // radius 100 circle, svgo spacing
  };
  u32(2);
  for (const char* d : paths) { u16((uint16_t)std::strlen(d)); b.insert(b.end(), d, d + std::strlen(d)); }
  std::ofstream(file, std::ios::binary).write((const char*)b.data(), (std::streamsize)b.size());
}

std::vector<uint8_t> game_start() {
  std::vector<uint8_t> e(0x2A1, 0);
  e[0] = 0x36;
  put16(e, 0x13, 32);                      // Final Destination
  e[0x65] = 2; e[0x66] = 0;               // port 1: Fox, human
  for (int i = 1; i < 4; ++i) e[0x66 + 0x24 * i] = 3;   // others empty
  return e;
}

std::vector<uint8_t> post_frame(int32_t frame, float x, float y, float counter) {
  std::vector<uint8_t> e(0x54, 0);
  e[0] = 0x38;
  put32(e, 1, (uint32_t)frame);
  e[5] = 0; e[6] = 0; e[7] = 1;           // port 1, not a follower, internal Fox
  put16(e, 8, 14);                         // Wait
  putf(e, 0x0A, x); putf(e, 0x0E, y); putf(e, 0x12, 1.0f);
  putf(e, 0x1A, 60.0f); e[0x21] = 4;
  putf(e, 0x22, counter);
  return e;
}

std::vector<uint8_t> bookend(int32_t frame) {
  std::vector<uint8_t> e(9, 0);
  e[0] = 0x3C;
  put32(e, 1, (uint32_t)frame);
  return e;
}

void feed(const std::vector<uint8_t>& e) { lab::feed(e.data(), (uint32_t)e.size()); }

int unit_test() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "melee_unlocked_lab_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  write_pack(dir / "1.lab");
  host::options.lab_dir = dir.string();

  const float W = 730, H = 600;   // Slippi Lab's viewBox, so one world unit is camera-scale pixels
  CHECK(draw_vertex_count(W, H) == 0);           // no match: the game image is left alone
  CHECK(!lab::match_in_progress());

  feed(game_start());
  feed(post_frame(-123, 10, 0, 0));
  feed(bookend(-123));
  CHECK(lab::match_in_progress());

  // The pack loads on a worker; until then the view draws a round marker. The square silhouette
  // is exactly four vertices, which is how the loop knows the pack arrived.
  const ImU32 red = IM_COL32(0xB9, 0x1C, 0x1C, 255);
  Box square;
  for (int i = 0; i < 300 && square.n != 4; ++i) {
    square = frame_box(red, W, H);
    if (square.n != 4) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  // Camera: starts on the player, scale eases once from 5 toward min(640/100, 480/100) = 4.8.
  const float scale1 = 5 + (4.8f - 5) * 0.04f;
  const float half = 10 * scale1;   // the square is 20 world units wide at character scale 1
  CHECK(square.n == 4);
  CHECK(std::fabs(square.x0 - (W / 2 - half)) < 0.5f && std::fabs(square.x1 - (W / 2 + half)) < 0.5f);
  CHECK(std::fabs(square.y0 - (H / 2 - half)) < 0.5f && std::fabs(square.y1 - (H / 2 + half)) < 0.5f);

  // Next frame shows animation frame 1, the arc-drawn circle of the same size, still centered on
  // the player (the camera eased toward the same target).
  feed(post_frame(-122, 10, 0, 1));
  feed(bookend(-122));
  const Box circle = frame_box(red, W, H);
  const float scale2 = scale1 + (4.8f - scale1) * 0.04f;
  const float r = 10 * scale2;
  CHECK(circle.n > 16);   // a curve, not four corners
  CHECK(std::fabs(circle.x0 - (W / 2 - r)) < 1.0f && std::fabs(circle.x1 - (W / 2 + r)) < 1.0f);
  CHECK(std::fabs(circle.y0 - (H / 2 - r)) < 1.0f && std::fabs(circle.y1 - (H / 2 + r)) < 1.0f);

  // Game end hands the window back to the game.
  const std::vector<uint8_t> end = {0x39, 0, 0};
  feed(end);
  CHECK(!lab::match_in_progress());
  CHECK(draw_vertex_count(W, H) == 0);

  fs::remove_all(dir, ec);
  if (g_failures) std::printf("%d failure(s)\n", g_failures);
  else std::printf("lab view: all checks passed\n");
  return g_failures ? 1 : 0;
}

// Replays a .slp through lab::feed, event by event, as the EXI device would hand them over.
int replay_dump(int argc, char** argv) {
  host::options.lab_dir = argv[2];
  const std::string prefix = argv[3];
  std::set<int> wanted;
  for (int i = 4; i < argc; ++i) wanted.insert(std::atoi(argv[i]));
  std::ifstream in(argv[1], std::ios::binary);
  std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (b.size() < 17 || b[15] != 0x35) { std::fprintf(stderr, "%s: not a .slp this can read\n", argv[1]); return 2; }
  const uint32_t raw = ((uint32_t)b[11] << 24) | (b[12] << 16) | (b[13] << 8) | b[14];
  const size_t end = raw ? std::min(b.size(), (size_t)15 + raw) : b.size();
  uint32_t sizes[256] = {};
  const uint8_t len = b[16];
  for (uint32_t i = 1; i + 2 < (uint32_t)len + 1; i += 3) sizes[b[16 + i]] = (b[17 + i] << 8) | b[18 + i];
  size_t p = 15 + len + 1;
  const float W = 1280, H = 720;
  int dumped = 0;
  while (p < end) {
    const uint8_t c = b[p];
    if (!sizes[c]) { std::fprintf(stderr, "unknown event %02X at %zu\n", c, p); break; }
    const uint32_t size = sizes[c] + 1;
    if (p + size > end) break;
    lab::feed(&b[p], size);
    if (c == 0x36) std::this_thread::sleep_for(std::chrono::seconds(3));   // give the packs time to load
    if (c == 0x3C) {
      const int32_t frame = (int32_t)(((uint32_t)b[p + 1] << 24) | (b[p + 2] << 16) | (b[p + 3] << 8) | b[p + 4]);
      ImGui::GetIO().DisplaySize = ImVec2(W, H);
      ImGui::NewFrame();
      lab::draw(true, W, H);   // every frame, so the camera eases as it does in game
      ImGui::Render();
      if (wanted.count(frame)) {
        const std::string path = prefix + "_" + std::to_string(frame) + ".bin";
        std::ofstream out(path, std::ios::binary);
        const ImDrawData* dd = ImGui::GetDrawData();
        const int lists = dd->CmdListsCount;
        out.write((const char*)&lists, 4);
        for (const ImDrawList* l : dd->CmdLists) {
          const int nv = l->VtxBuffer.Size, ni = l->IdxBuffer.Size;
          out.write((const char*)&nv, 4); out.write((const char*)l->VtxBuffer.Data, sizeof(ImDrawVert) * nv);
          out.write((const char*)&ni, 4); out.write((const char*)l->IdxBuffer.Data, sizeof(ImDrawIdx) * ni);
        }
        ++dumped;
      }
    }
    p += size;
  }
  unsigned char* pixels; int aw, ah;
  ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &aw, &ah);
  std::ofstream atlas(prefix + "_atlas.rgba", std::ios::binary);
  atlas.write((const char*)&aw, 4); atlas.write((const char*)&ah, 4); atlas.write((const char*)pixels, (std::streamsize)aw * ah * 4);
  std::fprintf(stderr, "dumped %d frame(s)\n", dumped);
  return dumped == (int)wanted.size() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().Fonts->Build();
  ImGui::GetIO().DeltaTime = 1.0f / 60;
  const int result = argc >= 5 ? replay_dump(argc, argv) : unit_test();
  ImGui::DestroyContext();
  return result;
}
