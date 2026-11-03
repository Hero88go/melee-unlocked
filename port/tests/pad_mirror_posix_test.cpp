// Window/USB-free checks of the reader and the actual macOS runtime wrapper.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <nlohmann/json.hpp>
#include "../runtime/hle/yougame_link.h"
#include "../runtime/host/pad_mirror_posix.h"
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

// Only host declarations are substituted; the production wrapper is compiled below.
namespace host {
struct PadState {
  uint16_t button;
  int8_t stick_x, stick_y, sub_x, sub_y;
  uint8_t trig_l, trig_r, analog_a, analog_b;
  int8_t err;
};
struct { bool no_gc_adapter = false; } options;
unsigned log_count = 0, rumble_logs = 0;
void log(const char* text, ...) {
  ++log_count;
  if (std::string(text) == "gc adapter: rumble unavailable through the YouGame app on this system") ++rumble_logs;
}
}
namespace yougame {
bool test_stdio = true;
pad_mirror::Attachment test_attachment;
bool stdio_bridge() { return test_stdio; }
pad_mirror::Attachment pad_mirror_attachment() { return test_attachment; }
}
#include "../runtime/host/gc_adapter_mirror_mac.h"

using namespace yougame::pad_mirror;
using json = nlohmann::json;

struct Fixture {
  int writer, reader;
  PadMirror* view;
  Fixture() {
    char path[] = "/tmp/mu-pad-mirror-XXXXXX";
    writer = mkstemp(path);
    CHECK(writer >= 3);
    CHECK(ftruncate(writer, 4096) == 0);
    reader = open(path, O_RDONLY);
    CHECK(unlink(path) == 0);
    CHECK(reader >= 3);
    struct stat st{};
    CHECK(fstat(reader, &st) == 0 && st.st_nlink == 0);
    void* m = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, writer, 0);
    CHECK(m != MAP_FAILED);
    view = static_cast<PadMirror*>(m);
    std::memcpy(view->magic, "YGPM", 4);
    view->version = 1;
    view->reports = 1;
    view->rate_hz = 1000;
    view->connected = 1;
    view->report[0] = 0x21;
    view->report[1] = 0x10;
    view->report[2] = 0x01; // A on port 1, three disconnected ports.
    for (int i = 4; i <= 7; ++i) view->report[i] = 128;
    __atomic_store_n(&view->seq, 2, __ATOMIC_RELEASE);
  }
  ~Fixture() { munmap(view, 4096); close(reader); close(writer); }
  Attachment info() const { return {reader, 4096}; }
};

static void parser() {
  Attachment out;
  json pads = {{"type", "pads"}, {"posix", {{"version", 1}, {"fd", 7}, {"size", 4096}}}};
  CHECK(attachment(pads, out) && out.fd == 7 && out.size == 4096);
  for (const char* key : {"version", "fd", "size"}) {
    for (const json& bad : {json(-1), json(1.5), json("1"), json(nullptr), json(true), json::object()}) {
      auto p = pads; p["posix"][key] = bad;
      CHECK(!attachment(p, out));
    }
    auto p = pads; p["posix"].erase(key); CHECK(!attachment(p, out));
  }
  auto p = pads; p["posix"]["version"] = 2; CHECK(!attachment(p, out));
  p = pads; p["posix"]["fd"] = uint64_t(INT_MAX) + 1; CHECK(!attachment(p, out));
  p = pads; p["posix"] = 4; CHECK(!attachment(p, out));
  CHECK(!attachment(json::object(), out));
}

static void validation() {
  Fixture f;
  Reader r;
  const auto rejected = [&](Attachment info) {
    CHECK(!r.attach(info) && !r.attached() && r.rate_hz() == 0);
    CHECK(fcntl(f.reader, F_GETFD) >= 0); // Always borrowed, also on failures.
  };
  CHECK(r.attach(f.info()));
  CHECK((fcntl(f.reader, F_GETFD) & FD_CLOEXEC) != 0);
  f.view->magic[0] = 'X'; rejected(f.info()); f.view->magic[0] = 'Y';
  for (uint32_t v : {0u, 2u}) { f.view->version = v; rejected(f.info()); }
  f.view->version = 1;
  rejected({f.reader, sizeof(PadMirror) - 1});
  rejected({f.reader, 4097});
  rejected({f.reader, SIZE_MAX});
  CHECK(ftruncate(f.writer, sizeof(PadMirror) - 1) == 0);
  rejected({f.reader, sizeof(PadMirror)});
  CHECK(ftruncate(f.writer, 4096) == 0);
  for (int fd : {-1, 0, 1, 2, INT_MAX}) rejected({fd, 4096});
  int pipes[2]; CHECK(pipe(pipes) == 0);
  rejected({pipes[0], 4096}); close(pipes[0]); close(pipes[1]);
  int directory = open("/tmp", O_RDONLY); CHECK(directory >= 3);
  rejected({directory, 4096}); close(directory);
  rejected({f.writer, 4096}); // Enforce the app's O_RDONLY contract too.
  CHECK(r.attach({f.reader, sizeof(PadMirror)}));
  r.reset();
  CHECK(fcntl(f.reader, F_GETFD) >= 0);

  unsigned logs = 0, resets = 0;
  f.view->magic[0] = 'X';
  for (int i = 0; i < 3; ++i)
    r.attach_once(f.info(), [&] { ++resets; }, [&](const char*, int) { ++logs; });
  CHECK(logs == 1 && resets == 1 && !r.attached());
}

static void snapshots() {
  Fixture f;
  Reader r;
  CHECK(r.attach(f.info()));
  uint8_t rep[37]{};
  const auto now = Reader::Clock::now();
  CHECK(r.read(rep, now) && r.rate_hz() == 1000 && rep[0] == 0x21);
  CHECK(r.read(rep, now + std::chrono::milliseconds(499)));
  CHECK(!r.read(rep, now + std::chrono::milliseconds(500)) && r.rate_hz() == 0);
  ++f.view->reports;
  CHECK(r.read(rep, now + std::chrono::seconds(1)) && r.rate_hz() == 1000);
  f.view->connected = 0; CHECK(!r.read(rep, now)); f.view->connected = 1;
  f.view->report[0] = 0x22; CHECK(!r.read(rep, now)); f.view->report[0] = 0x21;
  f.view->reports = 0; CHECK(!r.read(rep, now)); f.view->reports = 3;

  unsigned retries = 0;
  __atomic_store_n(&f.view->seq, 3, __ATOMIC_RELEASE);
  CHECK(!r.read(rep, now, [&] { ++retries; }));
  CHECK(retries == 8 && r.rate_hz() == 0);
  retries = 0;
  CHECK(r.read(rep, now, [&] {
    if (++retries == 3) __atomic_store_n(&f.view->seq, 4, __ATOMIC_RELEASE);
  }));
  CHECK(retries == 3 && r.rate_hz() == 1000);
}

static void readonly_mapping() {
  Fixture f;
  Reader r;
  CHECK(r.attach(f.info()));
  errno = 0;
  CHECK(mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, f.reader, 0) == MAP_FAILED && errno == EACCES);
  CHECK(mprotect(const_cast<PadMirror*>(r.view()), 4096, PROT_READ | PROT_WRITE) == -1);
  CHECK(pwrite(f.reader, "X", 1, 0) == -1 && errno == EBADF);
  pid_t pid = fork(); CHECK(pid >= 0);
  if (!pid) {
    // Exit from the fault handler, avoiding a core file or OS crash reporter.
    std::signal(SIGSEGV, [](int) { _exit(73); });
    std::signal(SIGBUS, [](int) { _exit(73); });
    auto* bytes = reinterpret_cast<volatile uint8_t*>(const_cast<PadMirror*>(r.view()));
    bytes[29] = 0;
    _exit(74);
  }
  int status = 0;
  CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 73);
  CHECK(f.view->report[0] == 0x21);
}

static void runtime() {
  Fixture f;
  yougame::test_attachment = f.info();
  host::PadState pads[4]{};
  CHECK(host::gcadapter_poll(pads) == 1);
  CHECK(pads[0].button == 0x0100 && pads[0].err == 0 && pads[0].stick_x == 0);
  CHECK(host::gcadapter_poll_rate_hz() == 1000);
  CHECK(host::gcadapter_problem() == "Controllers come from the YouGame app");
  CHECK(host::log_count == 1); // Poll, rate and status share one attachment.
  f.view->report[4] = 255; f.view->report[8] = 30;
  ++f.view->reports;
  CHECK(host::gcadapter_poll(pads) == 1 && pads[0].stick_x == 127 && pads[0].trig_l == 30);
  host::gcadapter_recalibrate(0);
  CHECK(host::gcadapter_poll(pads) == 1 && pads[0].stick_x == 0 && pads[0].trig_l == 0);
  uint8_t before[80]; std::memcpy(before, f.view, sizeof before);
  host::gcadapter_rumble(0, true); host::gcadapter_rumble(0, false);
  CHECK(host::rumble_logs == 1 && !std::memcmp(before, f.view, sizeof before));
  host::options.no_gc_adapter = true;
  CHECK(!host::gcadapter_poll(pads) && host::gcadapter_poll_rate_hz() == 0);
  host::options.no_gc_adapter = false;
  f.view->connected = 0;
  CHECK(!host::gcadapter_poll(pads) && host::gcadapter_poll_rate_hz() == 0);
  CHECK(host::gcadapter_problem() == "No GameCube adapter from the YouGame app");
  f.view->connected = 1;
  Fixture next;
  yougame::test_attachment = next.info();
  CHECK(host::gcadapter_poll(pads) == 1 && pads[0].stick_x == 0);
  host::gcadapter_shutdown();
  CHECK(fcntl(f.reader, F_GETFD) >= 0 && fcntl(next.reader, F_GETFD) >= 0);
  yougame::test_stdio = false;
  CHECK(!host::gcadapter_poll(pads) && host::gcadapter_poll_rate_hz() == 0);
}

int main() {
  parser(); validation(); snapshots(); readonly_mapping(); runtime();
  std::puts("POSIX pad mirror: PASS");
}
