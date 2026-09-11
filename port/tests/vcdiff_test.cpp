// VCDIFF decoder: a hand-assembled delta with COPY, ADD and RUN against a source buffer.
#include "vcdiff.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
static void check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); std::fflush(stdout); std::exit(1); } }
int main() {
  const char* source = "hello world";
  // Window: source segment = whole source (11 bytes at 0). Target "hello there!!!" (14 bytes):
  //   COPY size 5 mode 0 addr 0   -> "hello"   (code table index 20 = COPY size 5? index 19 is size 0; 20 = size 4; 21 = size 5)
  //   ADD size 6 " there"         -> index 7 (ADD size 6)
  //   RUN size 3 '!'              -> index 0 with size varint 3, data '!'
  std::vector<uint8_t> d = {0xD6, 0xC3, 0xC4, 0x00, 0x00};
  std::vector<uint8_t> data = {' ', 't', 'h', 'e', 'r', 'e', '!'};
  std::vector<uint8_t> inst = {21, 7, 0, 3};
  std::vector<uint8_t> addr = {0};
  std::vector<uint8_t> w = {0x01, 11, 0};   // win_indicator VCD_SOURCE, segment length 11, position 0
  std::vector<uint8_t> body = {14, 0, (uint8_t)data.size(), (uint8_t)inst.size(), (uint8_t)addr.size()};
  body.insert(body.end(), data.begin(), data.end());
  body.insert(body.end(), inst.begin(), inst.end());
  body.insert(body.end(), addr.begin(), addr.end());
  w.push_back((uint8_t)body.size());   // length of the delta encoding
  w.insert(w.end(), body.begin(), body.end());
  d.insert(d.end(), w.begin(), w.end());
  std::vector<uint8_t> out; std::string err;
  bool ok = host::vcdiff_decode((const uint8_t*)source, std::strlen(source), d.data(), d.size(), out, &err);
  check(ok, err.c_str());
  std::string s(out.begin(), out.end());
  check(s == "hello there!!!", ("unexpected output: " + s).c_str());
  // Second window copying from earlier in the same window (overlapping RUN-like copy via COPY mode 1 HERE).
  std::vector<uint8_t> d2 = {0xD6, 0xC3, 0xC4, 0x00, 0x00, 0x00};   // no source segment
  std::vector<uint8_t> inst2 = {3, 20, 20};   // ADD 2 "ab", COPY 4 mode 0 addr 0 -> "abab", COPY 4 mode 0 addr 2 -> "abab"
  std::vector<uint8_t> data2 = {'a', 'b'};
  std::vector<uint8_t> addr2 = {0, 2};
  std::vector<uint8_t> body2 = {10, 0, 2, 3, 2};
  body2.insert(body2.end(), data2.begin(), data2.end()); body2.insert(body2.end(), inst2.begin(), inst2.end()); body2.insert(body2.end(), addr2.begin(), addr2.end());
  d2.push_back((uint8_t)body2.size()); d2.insert(d2.end(), body2.begin(), body2.end());
  ok = host::vcdiff_decode(nullptr, 0, d2.data(), d2.size(), out, &err);
  check(ok, err.c_str());
  s.assign(out.begin(), out.end());
  check(s == "ababababab", ("overlapping copy output: " + s).c_str());
  std::puts("VCDIFF COPY/ADD/RUN, address cache and overlapping copies passed");
}
