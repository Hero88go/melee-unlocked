// SPDX-License-Identifier: GPL-2.0-or-later
#include "hle.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hle {
void CARDCancel(ppc::Context&, uint8_t*);
void CARDFastDeleteAsync(ppc::Context&, uint8_t*);
void CARDSetAttributesAsync(ppc::Context&, uint8_t*);
}

namespace {
std::vector<uint8_t> ram(ppc::RAM_SIZE);
ppc::Context cpu{};
std::vector<host::Completion> completions;
struct Callback { uint32_t address, r3, r4, r5, r6; };
std::vector<Callback> callbacks;

void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
uint32_t guest(uint32_t offset) { return ppc::RAM_BASE + offset; }
void put_name(uint32_t offset, const char* name) {
  std::fill_n(host::ptr(guest(offset), 32), 32, 0);
  std::copy_n(name, std::char_traits<char>::length(name), host::ptr(guest(offset), 32));
}
void invoke(void (*fn)(ppc::Context&, uint8_t*), std::initializer_list<uint32_t> args) {
  cpu.r[3] = cpu.r[4] = cpu.r[5] = cpu.r[6] = cpu.r[7] = 0;
  uint32_t* target = cpu.r + 3;
  for (uint32_t arg : args) *target++ = arg;
  fn(cpu, ram.data());
}
void take_callback(uint32_t expected_addr, uint32_t expected_result = 0) {
  require(completions.size() == 1, "successful async operation did not queue one callback");
  host::pump_completions();
  require(!callbacks.empty(), "async completion was not delivered");
  const Callback cb = callbacks.back();
  callbacks.pop_back();
  require(cb.address == expected_addr && cb.r3 == 0 && cb.r4 == expected_result,
          "async callback arguments mismatch");
}
void expect_no_callback() {
  const size_t before = callbacks.size();
  host::pump_completions();
  require(callbacks.size() == before, "failed immediate operation invoked callback");
}
}  // namespace

namespace host {
Options options;
uint8_t* ram = nullptr;
uint32_t ram_size = ppc::RAM_SIZE;
ppc::Context* cpu = nullptr;
uint8_t* ptr(uint32_t addr, uint32_t bytes) {
  if (addr < ppc::RAM_BASE || (uint64_t)(addr - ppc::RAM_BASE) + bytes > ram_size)
    throw std::runtime_error("guest RAM range invalid");
  return ram + (addr - ppc::RAM_BASE);
}
uint32_t rd32(uint32_t addr) {
  const uint8_t* p = ptr(addr, 4);
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
void wr32(uint32_t addr, uint32_t v) {
  uint8_t* p = ptr(addr, 4); p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
void wr16(uint32_t addr, uint16_t v) { uint8_t* p = ptr(addr, 2); p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
void post_completion(Completion fn) { completions.push_back(std::move(fn)); }
void pump_completions() {
  std::vector<Completion> pending; pending.swap(completions);
  for (Completion& fn : pending) fn();
}
void call_guest(uint32_t addr, uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6) {
  callbacks.push_back({addr, r3, r4, r5, r6});
}
void log(const char*, ...) {}
void mark_ram_write(uint32_t, uint32_t) {}
}  // namespace host

int main() {
  try {
    constexpr uint32_t CALLBACK = 0x5678;
    host::ram = ram.data(); host::cpu = &cpu;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    host::options.card_dir = (std::filesystem::temp_directory_path() /
      ("melee-hle-card-" + std::to_string(stamp))).string();
    std::fill_n(host::ptr(guest(0), 6), 6, 0);
    std::copy_n("GALE01", 6, host::ptr(guest(0), 6));

    // Async results return synchronously; successful callbacks are queued until a pump point.
    invoke(hle::CARDMountAsync, {1, 0, 0, CALLBACK});
    require((int32_t)cpu.r[3] == -3, "invalid channel mount did not report NOCARD");
    expect_no_callback();
    invoke(hle::CARDMountAsync, {0, 0, 0, 0x1234});
    require(cpu.r[3] == 0 && callbacks.empty() && completions.size() == 1,
            "mount callback was not deferred");
    take_callback(0x1234);

    invoke(hle::CARDCheckAsync, {0, CALLBACK});
    require(cpu.r[3] == 0, "async card check failed");
    take_callback(CALLBACK);

    invoke(hle::CARDProbeEx, {1, guest(0x80), guest(0x84)});
    require((int32_t)cpu.r[3] == -3, "invalid channel probe did not report NOCARD");

    constexpr uint32_t NAME = 0x100, INFO = 0x180, SOURCE = 0x2000, DEST = 0x5000;
    constexpr uint32_t SIZE = 0x2000;
    put_name(NAME + 0x40, "");
    invoke(hle::CARDCreateAsync, {0, guest(NAME + 0x40), SIZE, guest(INFO), CALLBACK});
    require((int32_t)cpu.r[3] == -12, "empty filename did not report NAMETOOLONG");
    expect_no_callback();
    put_name(NAME + 0x40, "BAD_SIZE");
    invoke(hle::CARDOpen, {0, guest(NAME + 0x40), guest(INFO)});
    require((int32_t)cpu.r[3] == -4, "missing file did not report NOFILE");
    invoke(hle::CARDFastOpen, {0, 126, guest(INFO)});
    require((int32_t)cpu.r[3] == -4, "invalid fast-open file number did not report NOFILE");
    put_name(NAME, "HLE_CARD_TEST");
    invoke(hle::CARDCreateAsync, {0, guest(NAME), SIZE, guest(INFO), CALLBACK});
    require(cpu.r[3] == 0 && host::rd32(guest(INFO)) == 0 &&
            host::rd32(guest(INFO) + 12) == SIZE, "async create failed");
    take_callback(CALLBACK);

    invoke(hle::CARDCreateAsync, {0, guest(NAME), SIZE, guest(INFO), CALLBACK});
    require((int32_t)cpu.r[3] == -7, "duplicate create did not report EXIST");
    expect_no_callback();
    put_name(NAME + 0x40, "BAD_SIZE");
    invoke(hle::CARDCreateAsync, {0, guest(NAME + 0x40), SIZE - 1, guest(INFO), CALLBACK});
    require((int32_t)cpu.r[3] == -128, "unaligned create did not report fatal-size error");
    expect_no_callback();

    for (uint32_t pass = 0; pass < 3; ++pass) {
      uint8_t* source = host::ptr(guest(SOURCE), SIZE);
      for (uint32_t i = 0; i < SIZE; ++i) source[i] = (uint8_t)(i * 37u + pass * 53u);
      invoke(hle::CARDWriteAsync, {guest(INFO), guest(SOURCE), SIZE, 0, CALLBACK});
      require(cpu.r[3] == 0, "repeated async write failed");
      take_callback(CALLBACK);
      invoke(hle::CARDReadAsync, {guest(INFO), guest(DEST), SIZE, 0, CALLBACK});
      require(cpu.r[3] == 0, "repeated async read failed");
      take_callback(CALLBACK);
      require(std::equal(source, source + SIZE, host::ptr(guest(DEST), SIZE)),
              "repeated async readback mismatch");
    }

    invoke(hle::CARDWriteAsync, {guest(INFO), guest(SOURCE), SIZE, SIZE, CALLBACK});
    require((int32_t)cpu.r[3] == -11, "out-of-bounds async write did not report LIMIT");
    expect_no_callback();
    invoke(hle::CARDReadAsync, {guest(INFO), guest(DEST), SIZE, 0, CALLBACK});
    require(cpu.r[3] == 0, "valid read failed after an async write error");
    take_callback(CALLBACK);
    uint8_t* last_pattern = host::ptr(guest(SOURCE), SIZE);
    for (uint32_t i = 0; i < SIZE; ++i) last_pattern[i] = (uint8_t)(i * 37u + 2u * 53u);
    require(std::equal(last_pattern, last_pattern + SIZE, host::ptr(guest(DEST), SIZE)),
            "failed out-of-bounds write changed persisted data");

    invoke(hle::CARDReadAsync, {guest(INFO), guest(DEST), SIZE, SIZE, CALLBACK});
    require((int32_t)cpu.r[3] == -11, "out-of-bounds async read did not report LIMIT");
    expect_no_callback();
    invoke(hle::CARDReadAsync, {guest(INFO), guest(DEST), SIZE, 0, CALLBACK});
    require(cpu.r[3] == 0, "valid operation failed after an async error");
    take_callback(CALLBACK);
    invoke(hle::CARDCancel, {guest(INFO)});
    require(cpu.r[3] == 0, "cancel of an already-completed card operation failed");

    invoke(hle::CARDUnmount, {0});
    require(cpu.r[3] == 0, "unmount failed");
    invoke(hle::CARDUnmount, {1});
    require((int32_t)cpu.r[3] == -3, "invalid-channel unmount did not report NOCARD");
    invoke(hle::CARDCheckAsync, {0, CALLBACK});
    require((int32_t)cpu.r[3] == -3, "async check without a mounted card did not report NOCARD");
    expect_no_callback();
    invoke(hle::CARDCancel, {guest(INFO)});
    require((int32_t)cpu.r[3] == -3, "cancel without a mounted card did not report NOCARD");
    invoke(hle::CARDCreateAsync, {0, guest(NAME), SIZE, guest(INFO), CALLBACK});
    require((int32_t)cpu.r[3] == -3, "create without a mounted card did not report NOCARD");
    expect_no_callback();
    invoke(hle::CARDReadAsync, {guest(INFO), guest(DEST), SIZE, 0, CALLBACK});
    require((int32_t)cpu.r[3] == -4, "read after unmount did not report NOFILE");
    expect_no_callback();
    invoke(hle::CARDMountAsync, {0, 0, 0, CALLBACK});
    require(cpu.r[3] == 0, "remount failed");
    take_callback(CALLBACK);
    invoke(hle::CARDOpen, {0, guest(NAME), guest(INFO)});
    require(cpu.r[3] == 0, "persisted file did not reopen after remount");
    invoke(hle::CARDReadAsync, {guest(INFO), guest(DEST), SIZE, 0, CALLBACK});
    require(cpu.r[3] == 0, "persisted data read failed after remount");
    take_callback(CALLBACK);
    uint8_t* source = host::ptr(guest(SOURCE), SIZE);
    for (uint32_t i = 0; i < SIZE; ++i) source[i] = (uint8_t)(i * 37u + 2u * 53u);
    require(std::equal(source, source + SIZE, host::ptr(guest(DEST), SIZE)),
            "persisted pattern differs after remount");

    put_name(NAME + 0x80, "SECOND_FILE");
    invoke(hle::CARDCreateAsync, {0, guest(NAME + 0x80), SIZE, guest(INFO + 0x20), CALLBACK});
    require(cpu.r[3] == 0, "second file create failed");
    take_callback(CALLBACK);
    invoke(hle::CARDRenameAsync, {0, guest(NAME), guest(NAME + 0x80), CALLBACK});
    require((int32_t)cpu.r[3] == -7, "rename over existing file did not report EXIST");
    expect_no_callback();
    invoke(hle::CARDFastOpen, {0, host::rd32(guest(INFO + 0x20) + 4), guest(INFO + 0x40)});
    require(cpu.r[3] == 0, "fast open failed for a valid file number");
    invoke(hle::CARDFastDeleteAsync, {0, host::rd32(guest(INFO + 0x20) + 4), CALLBACK});
    require(cpu.r[3] == 0, "fast async delete failed");
    take_callback(CALLBACK);
    invoke(hle::CARDFastOpen, {0, host::rd32(guest(INFO + 0x20) + 4), guest(INFO + 0x40)});
    require((int32_t)cpu.r[3] == -4, "fast open found a fast-deleted file");
    put_name(NAME + 0xC0, "RENAMED_TEST");
    invoke(hle::CARDRenameAsync, {0, guest(NAME), guest(NAME + 0xC0), CALLBACK});
    require(cpu.r[3] == 0, "valid async rename failed");
    take_callback(CALLBACK);
    invoke(hle::CARDOpen, {0, guest(NAME), guest(INFO)});
    require((int32_t)cpu.r[3] == -4, "old name remained open after rename");
    invoke(hle::CARDOpen, {0, guest(NAME + 0xC0), guest(INFO)});
    require(cpu.r[3] == 0, "renamed file could not be opened");
    std::fill_n(host::ptr(guest(0x7000), 0x6C), 0x6C, 0);
    host::ptr(guest(0x7000), 0x6C)[0x2E] = 1;
    invoke(hle::CARDSetStatusAsync, {0, host::rd32(guest(INFO) + 4), guest(0x7000), CALLBACK});
    require(cpu.r[3] == 0, "async status write failed");
    take_callback(CALLBACK);
    invoke(hle::CARDGetStatus, {0, host::rd32(guest(INFO) + 4), guest(0x7100)});
    require(cpu.r[3] == 0 && host::ptr(guest(0x7100), 0x6C)[0x2E] == 1,
            "status metadata did not survive a status read");
    invoke(hle::CARDSetAttributesAsync, {0, host::rd32(guest(INFO) + 4), 0x05, CALLBACK});
    require(cpu.r[3] == 0, "async attribute update failed");
    take_callback(CALLBACK);
    bool found_header = false;
    for (const auto& entry : std::filesystem::directory_iterator(host::options.card_dir)) {
      if (entry.path().extension() != ".gci") continue;
      std::array<uint8_t, 64> header{};
      std::ifstream input(entry.path(), std::ios::binary);
      if (input.read((char*)header.data(), (std::streamsize)header.size()) &&
          std::string((const char*)header.data() + 8, 12) == "RENAMED_TEST") {
        found_header = header[0x34] == 0x05;
      }
    }
    require(found_header, "attribute byte was not persisted in the GCI header");
    invoke(hle::CARDDeleteAsync, {0, guest(NAME + 0xC0), CALLBACK});
    require(cpu.r[3] == 0, "async delete failed");
    take_callback(CALLBACK);
    invoke(hle::CARDOpen, {0, guest(NAME + 0xC0), guest(INFO)});
    require((int32_t)cpu.r[3] == -4, "deleted file remained open");

    invoke(hle::CARDFormatAsync, {0, CALLBACK});
    require(cpu.r[3] == 0, "async format failed");
    take_callback(CALLBACK);
    invoke(hle::CARDOpen, {0, guest(NAME + 0x80), guest(INFO)});
    require((int32_t)cpu.r[3] == -4, "formatted card still exposes a file");

    invoke(hle::CARDUnmount, {0});
    std::error_code ec;
    std::filesystem::remove_all(host::options.card_dir, ec);
    require(!ec, "temporary HLE card directory cleanup failed");
    std::cout << "HLE card lifecycle, deferred callbacks, error recovery, repeated I/O and remount passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "HLE card test failed: " << e.what() << '\n';
    std::error_code ec;
    if (!host::options.card_dir.empty()) std::filesystem::remove_all(host::options.card_dir, ec);
    return 1;
  }
}
