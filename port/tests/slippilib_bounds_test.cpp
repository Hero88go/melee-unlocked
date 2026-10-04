// The replay parser of the Static Recomp viewer against damaged files: a post frame update with no
// frame, a split message that states more than it holds, a size table whose length byte is 0x80 or
// more, payloads that end inside a value. A whole replay must read as it always did.
#include "slippilib/SlippiGame.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;
void put32(Bytes& out, uint32_t value) { for (int shift = 24; shift >= 0; shift -= 8) out.push_back((uint8_t)(value >> shift)); }
void set32(Bytes& out, size_t at, uint32_t value) { for (int k = 0; k < 4; ++k) out[at + k] = (uint8_t)(value >> (24 - 8 * k)); }
void set_float(Bytes& out, size_t at, float value) { uint32_t bits; std::memcpy(&bits, &value, 4); set32(out, at, bits); }

// The payload sizes event: command, length byte, then (command, size) entries of three bytes.
Bytes table(const std::vector<std::pair<uint8_t, uint16_t>>& entries) {
  Bytes out{0x35, (uint8_t)(1 + 3 * entries.size())};
  for (const auto& e : entries) { out.push_back(e.first); out.push_back((uint8_t)(e.second >> 8)); out.push_back((uint8_t)e.second); }
  return out;
}
// Events take the place of the payload after the command byte: at[k] below is payload byte k.
Bytes event(uint8_t command, size_t payload) { Bytes e(1 + payload, 0); e[0] = command; return e; }
Bytes game_init() {
  Bytes e = event(0x36, 420);
  e[1] = 3; e[2] = 12; e[3] = 0;                 // version 3.12.0
  e[1 + 18] = 0x00; e[1 + 19] = 0x1F;            // stage 31
  for (int p = 0; p < 4; ++p) e[1 + 100 + 36 * p + 1] = p < 2 ? 0 : 3;   // two players, two empty slots
  e[1 + 100] = 2; e[1 + 136] = 9;                // characters
  set32(e, 1 + 316, 0x12345678u);                // random seed
  return e;
}
Bytes frame_start(int32_t frame, uint32_t seed) { Bytes e = event(0x3A, 8); set32(e, 1, (uint32_t)frame); set32(e, 5, seed); return e; }
Bytes pre_frame(int32_t frame, uint8_t slot) {
  Bytes e = event(0x37, 63);
  set32(e, 1, (uint32_t)frame); e[5] = slot;
  e[1 + 10] = 0x00; e[1 + 11] = 0x0E;            // animation
  set_float(e, 1 + 24, 0.5f);                    // joystick X
  e[1 + 58] = 200;                               // raw joystick X
  set_float(e, 1 + 59, 42.0f);                   // percent
  return e;
}
Bytes post_frame(int32_t frame, uint8_t slot) { Bytes e = event(0x38, 33); set32(e, 1, (uint32_t)frame); e[5] = slot; e[7] = 1; return e; }
Bytes frame_end(int32_t frame) { Bytes e = event(0x3C, 8); set32(e, 1, (uint32_t)frame); set32(e, 5, (uint32_t)frame); return e; }
Bytes split(const Bytes& block, uint16_t stated, uint8_t command, bool last) {
  Bytes e = event(0x10, 516);
  std::memcpy(e.data() + 1, block.data(), block.size() < 512 ? block.size() : 512);
  e[1 + 512] = (uint8_t)(stated >> 8); e[1 + 513] = (uint8_t)stated; e[1 + 514] = command; e[1 + 515] = last ? 1 : 0;
  return e;
}
void add(Bytes& out, const Bytes& more) { out.insert(out.end(), more.begin(), more.end()); }

// The events of a short, whole replay (after the size table).
Bytes whole_events() {
  Bytes out;
  add(out, game_init());
  Bytes codes(16, 0); codes[0] = 0xC2; codes[15] = 0x77;
  add(out, split(codes, 16, 0x3D, true));
  for (int32_t frame = -123; frame <= -122; ++frame) {
    add(out, frame_start(frame, 0xAABBCCDDu));
    add(out, pre_frame(frame, 0)); add(out, pre_frame(frame, 1));
    add(out, post_frame(frame, 0)); add(out, post_frame(frame, 1));
    add(out, frame_end(frame));
  }
  Bytes end = event(0x39, 1); end[1] = 2;
  add(out, end);
  out.push_back('U');   // the metadata key that follows the events in a .slp file
  return out;
}
const std::vector<std::pair<uint8_t, uint16_t>> whole_sizes = {
  {0x36, 420}, {0x37, 63}, {0x38, 33}, {0x39, 1}, {0x3A, 8}, {0x3C, 8}, {0x3D, 16}, {0x10, 516}};

int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) { std::printf("FAILED: %s\n", what); ++failures; }
}
std::filesystem::path g_path;
// Writes the events as a .slp file and opens it. The parser keeps the file open: the caller lets
// the game go before the next one is written.
std::unique_ptr<Slippi::SlippiGame> open_replay(const Bytes& raw) {
  Bytes file{'{', 'U', 3, 'r', 'a', 'w', '[', '$', 'U', '#', 'l'};
  put32(file, (uint32_t)raw.size());
  add(file, raw);
  { std::ofstream out(g_path, std::ios::binary | std::ios::trunc); out.write((const char*)file.data(), (std::streamsize)file.size()); }
  return Slippi::SlippiGame::FromFile(g_path.u8string());
}
void check_whole(const Bytes& raw, const char* what) {
  auto game = open_replay(raw);
  if (!game) { check(false, "cannot open the test file"); return; }
  bool ok = game->AreSettingsLoaded();
  Slippi::GameSettings* settings = game->GetSettings();
  ok = ok && settings->stage == 31 && settings->randomSeed == 0x12345678u;
  ok = ok && game->DoesPlayerExist(0) && game->DoesPlayerExist(1) && !game->DoesPlayerExist(2);
  ok = ok && settings->players[0].characterId == 2 && settings->players[1].characterId == 9;
  ok = ok && settings->geckoCodes.size() == 16 && settings->geckoCodes[0] == 0xC2 && settings->geckoCodes[15] == 0x77;
  ok = ok && game->GetVersionString() == "3.12.0";
  ok = ok && game->DoesFrameExist(-123) && game->DoesFrameExist(-122) && !game->DoesFrameExist(-121);
  if (ok) {
    Slippi::FrameData* frame = game->GetFrame(-122);
    ok = frame->frame == -122 && frame->randomSeedExists && frame->randomSeed == 0xAABBCCDDu && frame->inputsFullyFetched;
    ok = ok && frame->players.count(0) && frame->players.count(1) && frame->followers.empty();
    if (ok) {
      const Slippi::PlayerFrameData& p = frame->players[1];
      ok = p.animation == 0x0E && p.joystickX == 0.5f && p.joystickXRaw == 200 && p.percent == 42.0f && p.internalCharacterId == 1;
    }
  }
  ok = ok && game->GetLatestIndex() == -122 && game->GetLastFinalizedFrame() == -122;
  ok = ok && game->IsProcessingComplete() && game->GetGameEndMethod() == 2;
  check(ok, what);
}
void run() {
  // A whole replay reads as before.
  {
    Bytes raw = table(whole_sizes);
    add(raw, whole_events());
    check_whole(raw, "whole replay");
  }
  // The same replay behind a size table of 0xFF bytes (as a signed char that length was -1).
  {
    Bytes raw = table(whole_sizes);
    raw[1] = 0xFF;
    raw.resize(2 + 254, 0);   // entries for command 0 with size 0 fill it, the last two bytes are no entry
    add(raw, whole_events());
    check_whole(raw, "size table of 255 bytes");
  }
  // Size tables that state more than the file holds, nothing, or part of an entry.
  {
    Bytes raw{0x35, 0xFF, 0x36, 0x01, 0xA4, 0x00};
    auto game = open_replay(raw);
    check(game && !game->AreSettingsLoaded() && !game->DoesFrameExist(0), "size table longer than the file");
  }
  {
    Bytes raw{0x35, 0x80, 0x36, 0x01};
    auto game = open_replay(raw);
    check(game && !game->AreSettingsLoaded(), "size table of 0x80 bytes, cut short");
  }
  {
    Bytes raw{0x35, 0x00, 0x36, 0x01, 0xA4, 0x00, 0x00, 0x00};
    auto game = open_replay(raw);
    check(game && !game->AreSettingsLoaded(), "size table of no bytes");
  }
  {
    Bytes raw{0x35, 0x03, 0x36, 0x01, 0x00, 0x00, 0x00, 0x00};
    auto game = open_replay(raw);
    check(game && !game->AreSettingsLoaded(), "size table ending inside an entry");
  }
  // A post frame update for a frame that never started: nothing to write to.
  {
    Bytes raw = table({{0x38, 33}});
    add(raw, post_frame(5, 0));
    add(raw, post_frame(-123, 1));
    auto game = open_replay(raw);
    check(game && !game->AreSettingsLoaded() && !game->DoesFrameExist(5) && !game->DoesFrameExist(-123), "post frame update with no frame");
  }
  // A split message that states 65535 bytes at the end of the file: the 512 that are there are taken.
  {
    Bytes raw = table({{0x10, 516}, {0x3D, 16}});
    add(raw, split(Bytes(512, 0xAB), 0xFFFF, 0x3D, true));
    auto game = open_replay(raw);
    check(game && game->AreSettingsLoaded() && game->GetSettings()->geckoCodes.size() == 512 &&
          game->GetSettings()->geckoCodes[511] == 0xAB, "oversized split block");
  }
  // A split message the table makes too short for its own length and flags: skipped.
  {
    Bytes raw = table({{0x10, 4}, {0x3D, 16}});
    raw.insert(raw.end(), {0x10, 0xFF, 0xFF, 0x3D, 0x01});
    auto game = open_replay(raw);
    check(game && !game->AreSettingsLoaded() && game->GetSettings()->geckoCodes.empty(), "split message shorter than its block");
  }
  // A payload that ends inside the event's values: the missing ones read as their defaults.
  {
    Bytes raw = table({{0x3A, 8}, {0x37, 6}, {0x3C, 5}});
    add(raw, frame_start(7, 1));
    raw.insert(raw.end(), {0x37, 0, 0, 0, 7, 1, 0});
    raw.insert(raw.end(), {0x3C, 0, 0, 0, 7, 0xFF});
    auto game = open_replay(raw);
    bool ok = game && game->DoesFrameExist(7);
    if (ok) {
      Slippi::FrameData* frame = game->GetFrame(7);
      ok = frame->players.count(1) == 1 && frame->players[1].animation == 0 && frame->players[1].joystickX == 0.0f;
      ok = ok && game->GetLastFinalizedFrame() == 7;   // the cut-off value falls back to the frame's number
    }
    check(ok, "payload ending inside a value");
  }
  // Frame events too short for a frame number make no frames (one byte each here).
  {
    Bytes raw = table({{0x3A, 0}, {0x37, 3}});
    raw.insert(raw.end(), 1000, 0x3A);
    raw.insert(raw.end(), {0x37, 0, 0, 0});
    auto game = open_replay(raw);
    check(game && !game->DoesFrameExist(0) && game->GetFrameAt(0) == nullptr, "frame events with no frame number");
  }
}
}  // namespace

int main() {
  g_path = std::filesystem::current_path() / "slippilib_bounds_test.slp";
  try {
    run();
  } catch (const std::exception& e) {
    std::printf("FAILED: exception %s\n", e.what());
    ++failures;
  }
  std::error_code ec;
  std::filesystem::remove(g_path, ec);
  if (failures) return 1;
  std::printf("slippilib bounds test passed\n");
  return 0;
}
