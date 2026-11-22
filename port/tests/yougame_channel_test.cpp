// The local channel (yougame_channel.h): names, the Linux order, the folder check, hello lines, the
// playerPending schedule, line handling, and on Mac/Linux a fake host on a private socket folder.
// argv[1]: a scratch folder for the sockets (created; short, sun_path holds 104 bytes).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_channel.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <cerrno>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame;
using json = nlohmann::json;
int groups = 0;

void names() {
  CHECK(channel::pipe_name("S-1-5-21-1-2-3-1001", false) == "\\\\.\\pipe\\yougame-sdk-v1-S-1-5-21-1-2-3-1001");
  CHECK(channel::pipe_name("S-1-5-21-1-2-3-1001", true) == "\\\\.\\pipe\\yougame-staging-sdk-v1-S-1-5-21-1-2-3-1001");
  CHECK(channel::mac_socket(501, false) == "/tmp/yougame-501/sdk-v1.sock");
  CHECK(channel::mac_socket(501, true) == "/tmp/yougame-staging-501/sdk-v1.sock");
  CHECK(channel::mac_socket(501, false).size() < 104);
  ++groups;
}

void linux_order() {
  using V = std::vector<std::string>;
  CHECK((channel::linux_sockets("/run/user/1000", 1000, false) ==
         V{"/run/user/1000/yougame/sdk-v1.sock", "/tmp/yougame-1000/sdk-v1.sock"}));   // same folder once
  CHECK((channel::linux_sockets("/run/user/1000/", 1000, false) ==
         V{"/run/user/1000/yougame/sdk-v1.sock", "/tmp/yougame-1000/sdk-v1.sock"}));
  CHECK((channel::linux_sockets("/var/xdg", 1000, false) ==
         V{"/var/xdg/yougame/sdk-v1.sock", "/run/user/1000/yougame/sdk-v1.sock", "/tmp/yougame-1000/sdk-v1.sock"}));
  CHECK((channel::linux_sockets("/var/xdg", 1000, true) ==
         V{"/var/xdg/yougame-staging/sdk-v1.sock", "/run/user/1000/yougame-staging/sdk-v1.sock",
           "/tmp/yougame-staging-1000/sdk-v1.sock"}));
  CHECK((channel::linux_sockets(nullptr, 7, false) == V{"/run/user/7/yougame/sdk-v1.sock", "/tmp/yougame-7/sdk-v1.sock"}));
  CHECK((channel::linux_sockets("", 7, false) == V{"/run/user/7/yougame/sdk-v1.sock", "/tmp/yougame-7/sdk-v1.sock"}));
  CHECK((channel::linux_sockets("relative", 7, false) == V{"/run/user/7/yougame/sdk-v1.sock", "/tmp/yougame-7/sdk-v1.sock"}));
  // This machine's candidates: production first, then staging, never mixed.
  const auto mine = channel::candidates();
  CHECK(!mine.empty());
  bool seen_staging = false;
  for (const auto& c : mine) {
    if (c.staging) seen_staging = true;
    else CHECK(!seen_staging);
    CHECK((c.path.find("staging") != std::string::npos) == c.staging);
  }
  CHECK(seen_staging && !mine.front().staging);
  ++groups;
}

void hello_lines() {
  const json hello = channel::hello_request({{"type", "hello"}, {"id", 1}}, "0.9.96");
  CHECK(hello["type"] == "hello" && hello["id"] == 1);
  CHECK(hello["game"] == "melee-unlocked" && hello["version"] == "0.9.96" && hello["channel"] == 1);
  const std::string line = channel::request_line(hello);
  CHECK(line.rfind("yg {", 0) == 0 && line.find('\n') == std::string::npos);
  const json back = json::parse(line.substr(3));
  CHECK(back == hello);
  // Other requests pass unchanged.
  const json code = channel::hello_request({{"type", "mpCode"}, {"id", 2}}, "0.9.96");
  CHECK(code == json({{"type", "mpCode"}, {"id", 2}}));
  // The 64 K line limit, newline included: never sent.
  CHECK(channel::line_fits(std::string(channel::kLineLimit - 1, 'x')));
  CHECK(!channel::line_fits(std::string(channel::kLineLimit, 'x')));
  CHECK(channel::request_line({{"type", "big"}, {"blob", std::string(channel::kLineLimit, 'x')}}).empty());
  ++groups;
}

void lines_and_pushes() {
  using A = channel::LineAction;
  CHECK(channel::classify("yg {\"re\":3,\"ok\":true,\"result\":{}}") == A::Answer);
  CHECK(channel::classify("yg {\"re\":\"3\",\"ok\":true}") == A::Drop);
  CHECK(channel::classify("yg {\"push\":{\"type\":\"playerArrived\",\"result\":{}}}") == A::PlayerArrived);
  // Unknown push types go to the stdio handler, which acts only on types it knows: skipped there.
  CHECK(channel::classify("yg {\"push\":{\"type\":\"somethingNew\",\"x\":1}}") == A::Push);
  CHECK(channel::classify("yg {\"push\":{\"type\":\"join\",\"room\":\"abc\"}}") == A::Push);
  CHECK(channel::classify("yg {\"push\":{}}") == A::Push);
  CHECK(channel::classify("yg {\"push\":{\"type\":\"pads\",\"handle\":42}}") == A::AppOnly);
  CHECK(channel::classify("yg {\"push\":7}") == A::Drop);
  CHECK(channel::classify("yg {\"caps\":{\"relaunch\":true}}") == A::AppOnly);   // the app's own child only
  CHECK(channel::classify("yg {\"re\":4,\"ok\":true,\"caps\":{}}") == A::Answer);   // the link strips caps
  // Codes read on the reader thread, before the connection's end.
  CHECK(channel::answer_code("yg {\"re\":5,\"ok\":false,\"code\":\"accountChanged\"}") == "accountChanged");
  CHECK(channel::answer_code("yg {\"re\":5,\"ok\":true,\"code\":\"accountChanged\"}").empty());
  CHECK(channel::answer_code("yg {\"push\":{\"code\":\"accountChanged\"}}").empty());
  CHECK(channel::answer_code("nonsense").empty());
  CHECK(channel::retry_after_ms({{"code", "accountSwitching"}, {"retryAfter", 2}}) == 2000);
  CHECK(channel::retry_after_ms({{"code", "accountSwitching"}}) == 2000);
  CHECK(channel::retry_after_ms({{"retryAfter", 0}}) == 1000 && channel::retry_after_ms({{"retryAfter", 600}}) == 10000);
  CHECK(channel::retry_after_ms({{"retryAfter", "2"}}) == 2000 && channel::retry_after_ms({{"retryAfter", 1.5}}) == 1500);
  CHECK(channel::log_text("a\nb\rc\td") == "a b c d");
  CHECK(channel::log_text(std::string(500, 'x')).size() == 120);
  CHECK(channel::classify("yg not json") == A::Drop);
  CHECK(channel::classify("yg [1,2]") == A::Drop);
  CHECK(channel::classify("embed rect 0 0 1 1") == A::Drop);
  CHECK(channel::classify("yg-pads 1 2 3") == A::Drop);
  CHECK(channel::classify("") == A::Drop);
  // Refusals: notYet in our words, always; standalone_teams only when the host gave none.
  CHECK(!channel::refusal_text({{"re", 1}, {"ok", false}, {"code", "notYet"}, {"error", "x"}}).empty());
  CHECK(channel::refusal_text({{"re", 1}, {"ok", false}, {"code", "standalone_teams"}, {"error", "Their words"}}).empty());
  CHECK(channel::refusal_text({{"re", 1}, {"ok", false}, {"code", "standalone_teams"}}).empty());
  CHECK(channel::refusal_text({{"re", 1}, {"ok", false}, {"code", "notYet"}}) == "YouGame can't do this here yet.");
  // No refusal sends the player to the YouGame app: a copy the player started does everything the app's does.
  for (const char* code : {"notYet", "notHere", "unknown_build", "old_build", "build_replaced", "standalone_teams"})
    CHECK(channel::refusal_text({{"re", 1}, {"ok", false}, {"code", code}}).find("YouGame app") == std::string::npos);
  CHECK(channel::refusal_text({{"re", 1}, {"ok", false}, {"code", "busy"}}).empty());
  CHECK(!channel::refusal_text({{"re", 1}, {"ok", false}, {"code", "unknown_build"}}).empty());
  CHECK(!channel::refusal_text({{"re", 1}, {"ok", false}, {"code", "old_build"}}).empty());
  CHECK(channel::refusal_text({{"ok", false}, {"code", "build_replaced"}}) ==
        channel::refusal_text({{"ok", false}, {"code", "old_build"}}));
  CHECK(channel::refusal_text({{"ok", false}, {"code", "unknown_build"}, {"error", "Get the latest version"}}) ==
        "Get the latest version");
  CHECK(channel::refusal_text({{"ok", false}, {"code", "build_replaced"}, {"current", "0.9.205-online-test.117"}}) ==
        "This copy of Melee is out of date (the latest is 0.9.205-online-test.117). Update it to play online.");
  CHECK(channel::refusal_text({{"ok", false}, {"code", "build_replaced"}, {"current", " \n"}}) ==
        channel::refusal_text({{"ok", false}, {"code", "old_build"}}));
  CHECK(!channel::refusal_text({{"re", 1}, {"ok", false}, {"code", "notHere"}, {"retryAfter", 2}}).empty());
  CHECK(channel::refusal_text({{"re", 1}, {"ok", true}, {"code", "notYet"}}).empty());
  // Hello results.
  CHECK(channel::hello_pending({{"playerPending", true}}));
  CHECK(!channel::hello_pending({{"playerPending", false}}));
  CHECK(!channel::hello_pending({{"playerPending", true}, {"player", {{"id", "p1"}}}}));   // never both
  CHECK(channel::hello_player({{"player", {{"id", "p1"}, {"name", "A"}}}}));
  CHECK(!channel::hello_player({{"player", {{"id", ""}}}}));
  CHECK(!channel::hello_player({{"player", nullptr}}));
  // The hybrid route (the launcher's bridge for who the player is and every match, the app's channel for the
  // extension's own features; everything to the bridge while the channel is down).
  for (const char* t : {"hello", "mpCode", "mpTicket", "mpRank", "mpFriend", "mpRoom", "muRestartOlder"}) {
    CHECK(channel::to_launcher_bridge(t, true));
    CHECK(channel::to_launcher_bridge(t, false));
  }
  for (const char* t : {"gameState", "matchState", "menuButton", "pause", "inputKind", "directAttach", "signIn", "updateGame", "openFeedback"}) {
    CHECK(!channel::to_launcher_bridge(t, true));
    CHECK(channel::to_launcher_bridge(t, false));
    CHECK(!channel::to_launcher_bridge(t, true, true));
    CHECK(channel::to_launcher_bridge(t, false, true));
  }
  // The launcher not signed in, the app's channel naming a player (app_sign_in): matches go over the app's channel.
  // Its own questions stay with the launcher (its sign-in is asked first every time), and with the channel down
  // everything is the launcher's again.
  for (const char* t : {"mpCode", "mpTicket", "mpRank", "mpFriend", "mpRoom"}) {
    CHECK(!channel::to_launcher_bridge(t, true, true));
    CHECK(channel::to_launcher_bridge(t, false, true));
  }
  for (const char* t : {"hello", "muRestartOlder"}) {
    CHECK(channel::to_launcher_bridge(t, true, true));
    CHECK(channel::to_launcher_bridge(t, false, true));
  }
  // Whose sign-in: the launcher's whenever it has one; else the app's while its channel is up and names a player.
  using channel::HybridSignIn;
  for (bool up : {true, false})
    for (bool app : {true, false}) CHECK(channel::hybrid_sign_in(true, up, app) == HybridSignIn::Launcher);
  CHECK(channel::hybrid_sign_in(false, true, true) == HybridSignIn::App);
  CHECK(channel::hybrid_sign_in(false, false, true) == HybridSignIn::None);   // the app closed: play never depends on it
  CHECK(channel::hybrid_sign_in(false, true, false) == HybridSignIn::None);
  CHECK(channel::hybrid_sign_in(false, false, false) == HybridSignIn::None);
  // Neither signed in: a YouGame row opens the app's sign-in panel, only once the sign-in was asked and named nobody,
  // with the app's channel up and signed out.
  CHECK(channel::hybrid_offers_app_sign_in(true, false, true, true));
  CHECK(!channel::hybrid_offers_app_sign_in(false, false, true, true));   // not asked yet (the launcher may be signed in)
  CHECK(!channel::hybrid_offers_app_sign_in(true, true, true, true));     // signed in (the launcher's or the app's)
  CHECK(!channel::hybrid_offers_app_sign_in(true, false, false, true));   // the app's channel is down
  CHECK(!channel::hybrid_offers_app_sign_in(true, false, true, false));   // the app is not signed out (pending)
  ++groups;
}

// The Windows signer rule, as data: the name and the subject's O, L, ST and C must all match.
void publisher_rule() {
  channel::Publisher seen{L"Fluo Inc.", L"Fluo Inc.", L"Orlando", L"Florida", L"US"};
  CHECK(channel::publisher_matches(seen));
  auto off = [&](std::wstring channel::Publisher::*field, const wchar_t* value) {
    channel::Publisher p = seen;
    p.*field = value;
    return channel::publisher_matches(p);
  };
  CHECK(!off(&channel::Publisher::simple_name, L"Fluo Inc"));
  CHECK(!off(&channel::Publisher::organization, L"Someone Else"));
  CHECK(!off(&channel::Publisher::locality, L"Miami"));
  CHECK(!off(&channel::Publisher::state, L"FL"));
  CHECK(!off(&channel::Publisher::country, L""));
  using V = channel::TrustVerdict;
  CHECK(channel::trust_verdict(0) == V::Trusted);
  CHECK(channel::trust_verdict((int32_t)0x800B010Cu) == V::Revoked);   // CERT_E_REVOKED
  CHECK(channel::trust_verdict((int32_t)0x80092010u) == V::Revoked);   // CRYPT_E_REVOKED
  CHECK(channel::trust_verdict((int32_t)0x80092013u) == V::RevocationUnknown);   // CRYPT_E_REVOCATION_OFFLINE
  CHECK(channel::trust_verdict((int32_t)0x80092012u) == V::RevocationUnknown);   // CRYPT_E_NO_REVOCATION_CHECK
  CHECK(channel::trust_verdict((int32_t)0x800B010Eu) == V::RevocationUnknown);   // CERT_E_REVOCATION_FAILURE
  CHECK(channel::trust_verdict((int32_t)0x800B0100u) == V::Untrusted); // TRUST_E_NOSIGNATURE
  CHECK(channel::trust_verdict((int32_t)0x800B0109u) == V::Untrusted); // CERT_E_UNTRUSTEDROOT
  CHECK(channel::trust_verdict((int32_t)0x80096010u) == V::Untrusted); // TRUST_E_BAD_DIGEST
  ++groups;
}

// H1: the Windows read loop's rule (detail::read_once) against a fake pipe with CancelIoEx's real
// semantics: a cancel with no read pending is lost. close() sets the flag, then cancels. Run many
// times with close() landing at every point of the loop; the reader must always end.
struct FakePipe {
  std::mutex m;
  std::condition_variable cv;
  bool pending = false, cancelled = false;
  channel::detail::Issued issue() {
    std::lock_guard<std::mutex> l(m);
    pending = true; cancelled = false;
    return channel::detail::Issued::Pending;
  }
  void cancel() {
    std::lock_guard<std::mutex> l(m);
    if (pending) { cancelled = true; cv.notify_all(); }   // nothing pending: lost, as on Windows
  }
  // false: cancelled (the loop ends). `hung`: no completion within 2 s, the bug H1 fixed.
  bool wait(bool* hung) {
    std::unique_lock<std::mutex> l(m);
    if (!cv.wait_for(l, std::chrono::seconds(2), [&] { return cancelled; })) *hung = true;
    pending = false;
    return false;
  }
};

void read_loop_close() {
  using channel::detail::Issued;
  using channel::detail::read_once;
  {  // closed before the read was issued: cancelled at once, never waited on
    std::atomic<bool> closed{true};
    FakePipe pipe;
    bool hung = false;
    CHECK(!read_once(closed, [&] { return pipe.issue(); }, [&] { pipe.cancel(); }, [&] { return pipe.wait(&hung); }));
    CHECK(!hung);
  }
  {  // a read done at once is not cancelled; a failed issue never waits
    std::atomic<bool> closed{false};
    int cancels = 0, waits = 0;
    CHECK(read_once(closed, [] { return Issued::Done; }, [&] { ++cancels; }, [&] { ++waits; return true; }));
    CHECK(cancels == 0 && waits == 1);
    CHECK(!read_once(closed, [] { return Issued::Failed; }, [&] { ++cancels; }, [&] { ++waits; return true; }));
    CHECK(cancels == 0 && waits == 1);
  }
  for (int round = 0; round < 300; ++round) {   // close() racing the loop
    std::atomic<bool> closed{false};
    FakePipe pipe;
    bool hung = false;
    std::thread reader([&] {
      while (!closed) {
        if (!read_once(closed, [&] { return pipe.issue(); }, [&] { pipe.cancel(); }, [&] { return pipe.wait(&hung); })) break;
      }
    });
    std::this_thread::sleep_for(std::chrono::microseconds(round % 50));
    closed.exchange(true);   // Connection::close(): the flag, then the cancel
    pipe.cancel();
    reader.join();
    CHECK(!hung);
  }
  ++groups;
}

void player_wait() {
  using W = channel::PlayerWait;
  {  // a player in the first answer: nothing to wait for, nothing to tell
    W w;
    w.answered(false, true, 100);
    CHECK(w.due_at() == 0 && !w.take_arrival());
  }
  {  // pending: retries at +2 s and +6 s; the second retry pending too: gives up, nothing told
    W w;
    w.answered(true, false, 1000);
    CHECK(w.waiting() && w.due_at() == 3000);
    w.sent();
    w.answered(true, false, 3010);
    CHECK(w.due_at() == 7000);
    w.sent();
    CHECK(w.due_at() == 0);
    w.answered(true, false, 7010);
    CHECK(!w.waiting() && w.due_at() == 0 && !w.take_arrival());
  }
  {  // the first retry carries the player: told once, no second retry
    W w;
    w.answered(true, false, 0);
    w.sent();
    w.answered(false, true, 2000);
    CHECK(w.due_at() == 0);
    CHECK(w.take_arrival());
    CHECK(!w.take_arrival());
    w.pushed();   // a late push changes nothing
    CHECK(!w.take_arrival());
  }
  {  // the push before any retry: told once, no retries
    W w;
    w.answered(true, false, 0);
    w.pushed();
    CHECK(w.due_at() == 0 && w.take_arrival() && !w.take_arrival());
    w.answered(false, true, 5000);   // a later hello with the player: still told only once
    CHECK(!w.take_arrival());
  }
  {  // the push read before the pending answer was looked at
    W w;
    w.pushed();
    w.answered(true, false, 0);
    CHECK(w.due_at() == 0 && w.take_arrival() && !w.take_arrival());
  }
  {  // a retry answered with neither (signed out meanwhile): stop
    W w;
    w.answered(true, false, 0);
    w.sent();
    w.answered(false, false, 2000);
    CHECK(w.due_at() == 0 && !w.take_arrival());
  }
  {  // a retry without an answer still leaves the second one
    W w;
    w.answered(true, false, 0);
    w.sent();
    CHECK(w.due_at() == 6000);
  }
  {  // Online menu retries: at most once every 5 s
    channel::RetryGate g;
    CHECK(g.allow(1000) && !g.allow(1001) && !g.allow(5999) && g.allow(6000) && !g.allow(10999) && g.allow(11000));
  }
  ++groups;
}

#ifndef _WIN32
std::string g_base;

void folder_rules() {
  struct stat st{};
  st.st_mode = S_IFDIR | 0700;
  st.st_uid = 501;
  CHECK(channel::folder_reason(st, 501).empty());
  st.st_uid = 502;
  CHECK(channel::folder_reason(st, 501) == "folder belongs to another user");
  st.st_uid = 501;
  for (mode_t bad : {0755, 0711, 0770, 0701, 0600, 01700, 04700}) {
    st.st_mode = S_IFDIR | bad;
    CHECK(!channel::folder_reason(st, 501).empty());
  }
  st.st_mode = S_IFLNK | 0700;
  CHECK(channel::folder_reason(st, 501) == "folder is a link");
  st.st_mode = S_IFREG | 0700;
  CHECK(channel::folder_reason(st, 501) == "not a folder");
  ++groups;
}

// A folder for one case under the scratch folder, emptied first (only names this test makes).
std::string fresh(const std::string& name, mode_t mode) {
  const std::string dir = g_base + "/" + name;
  unlink((dir + "/sdk-v1.sock").c_str());
  unlink(dir.c_str());   // a link from a previous run
  rmdir(dir.c_str());
  CHECK(mkdir(dir.c_str(), 0700) == 0);
  CHECK(chmod(dir.c_str(), mode) == 0);
  return dir;
}

int listen_at(const std::string& path) {
  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  CHECK(fd >= 0);
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  CHECK(path.size() < sizeof a.sun_path);
  std::memcpy(a.sun_path, path.c_str(), path.size() + 1);
  CHECK(bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
  CHECK(listen(fd, 4) == 0);
  return fd;
}

std::string read_line(int fd) {
  std::string line;
  char c;
  while (recv(fd, &c, 1, 0) == 1) {
    if (c == '\n') return line;
    line.push_back(c);
  }
  return line;
}

void send_all(int fd, const std::string& text) {
  size_t sent = 0;
  while (sent < text.size()) {
    const ssize_t n = send(fd, text.data() + sent, text.size() - sent, 0);
    CHECK(n > 0);
    sent += (size_t)n;
  }
}

void open_refusals() {
  const uid_t me = geteuid();
  std::unique_ptr<channel::Connection> c;
  std::string why;
  // No folder: missing, at once.
  CHECK(channel::Connection::open({g_base + "/absent/sdk-v1.sock", false}, &c, &why) == channel::OpenResult::Missing && !c);
  // A private folder without a socket: missing.
  const std::string empty = fresh("e", 0700);
  CHECK(channel::Connection::open({empty + "/sdk-v1.sock", false}, &c, &why) == channel::OpenResult::Missing);
  // A socket file nobody listens on (the app quit): missing.
  const std::string stale = fresh("s", 0700);
  { const int fd = listen_at(stale + "/sdk-v1.sock"); close(fd); }
  CHECK(channel::Connection::open({stale + "/sdk-v1.sock", false}, &c, &why) == channel::OpenResult::Missing);
  // A listening host in a folder that is not 0700: refused before connecting.
  const std::string wide = fresh("w", 0700);
  const int wide_fd = listen_at(wide + "/sdk-v1.sock");
  CHECK(chmod(wide.c_str(), 0755) == 0);
  why.clear();
  CHECK(channel::Connection::open({wide + "/sdk-v1.sock", false}, &c, &why) == channel::OpenResult::Refused && !c);
  CHECK(why == "folder is not private (0700)");
  close(wide_fd);
  // The folder is a link to a private folder with a listening host: refused.
  const std::string target = fresh("t", 0700);
  const int target_fd = listen_at(target + "/sdk-v1.sock");
  const std::string link = g_base + "/l";
  unlink(link.c_str());
  CHECK(symlink(target.c_str(), link.c_str()) == 0);
  why.clear();
  CHECK(channel::Connection::open({link + "/sdk-v1.sock", false}, &c, &why) == channel::OpenResult::Refused);
  CHECK(why == "folder is a link");
  close(target_fd);
  // Wrong owner: only the pure rule is testable without root (folder_rules); the live check reads st_uid.
  bool missing = false;
  CHECK(channel::check_folder(target + "/sdk-v1.sock", me + 1, &missing) == "folder belongs to another user" && !missing);
  CHECK(channel::check_folder(target + "/sdk-v1.sock", me, &missing).empty() && !missing);
  ++groups;
}

// A fake host: checks the hello, answers playerPending, sends an unknown push and then playerArrived.
void fake_host() {
  const std::string dir = fresh("h", 0700);
  const std::string path = dir + "/sdk-v1.sock";
  const int server = listen_at(path);
  std::string hello_line;
  std::thread host([&] {
    const int peer = accept(server, nullptr, nullptr);
    CHECK(peer >= 0);
    hello_line = read_line(peer);
    send_all(peer, "yg {\"push\":{\"type\":\"neverHeardOf\",\"x\":[1,2]}}\n");
    send_all(peer, "noise that is not a bridge line\n");
    send_all(peer, "yg {\"re\":1,\"ok\":true,\"result\":{\"started\":\"outside\",\"host\":\"app\",\"verified\":false,\"playerPending\":true}}\r\n");
    send_all(peer, "yg {\"push\":{\"type\":\"playerArrived\",\"result\":{\"player\":{\"id\":\"p1\",\"name\":\"A\"}}}}\n");
    const std::string second = read_line(peer);
    CHECK(second.find("\"type\":\"mpTicket\"") != std::string::npos);
    send_all(peer, "yg {\"re\":2,\"ok\":false,\"error\":\"later\",\"code\":\"notYet\"}\n");
    close(peer);   // the host goes away: the reader ends
  });
  std::unique_ptr<channel::Connection> c;
  std::string why;
  CHECK(channel::Connection::open({path, false}, &c, &why) == channel::OpenResult::Connected && c);
  const json hello = channel::hello_request({{"type", "hello"}, {"id", 1}}, "0.9.96");
  CHECK(c->write_line(channel::request_line(hello)));
  CHECK(!c->write_line("yg " + std::string(channel::kLineLimit, 'x')));   // never sent, connection kept
  channel::PlayerWait wait;
  std::vector<channel::LineAction> seen;
  json answer, refusal;
  c->read_lines([&](const std::string& line) {
    const auto action = channel::classify(line);
    seen.push_back(action);
    if (action == channel::LineAction::Answer) {
      const json j = json::parse(line.substr(3));
      if (j["re"] == 1) {
        answer = j;
        wait.answered(channel::hello_pending(j["result"]), channel::hello_player(j["result"]), 0);
        CHECK(c->write_line(channel::request_line({{"id", 2}, {"type", "mpTicket"}})));
      } else {
        refusal = j;
      }
    }
    if (action == channel::LineAction::PlayerArrived) wait.pushed();
  });
  host.join();
  close(server);
  const json sent = json::parse(hello_line.substr(3));
  CHECK(hello_line.rfind("yg ", 0) == 0);
  CHECK(sent["type"] == "hello" && sent["id"] == 1 && sent["game"] == "melee-unlocked" && sent["channel"] == 1 &&
        sent["version"] == "0.9.96");
  using A = channel::LineAction;
  CHECK((seen == std::vector<A>{A::Push, A::Drop, A::Answer, A::PlayerArrived, A::Answer}));
  CHECK(answer["result"]["started"] == "outside");
  CHECK(wait.take_arrival() && !wait.take_arrival());
  CHECK(refusal["code"] == "notYet" && !channel::refusal_text(refusal).empty());
  CHECK(c->closed() && !c->write_line("yg {}"));
  ++groups;
}

// H2: a probe counts a connection up only while it is still the current one and still open. The host
// closes during hello; the reader ends; the probe must then see it as no host.
void promote_race() {
  const std::string dir = fresh("p", 0700);
  const std::string path = dir + "/sdk-v1.sock";
  const int server = listen_at(path);
  for (int round = 0; round < 20; ++round) {
    int peer = -1;
    std::thread host([&] { peer = accept(server, nullptr, nullptr); });
    std::unique_ptr<channel::Connection> opened;
    std::string why;
    CHECK(channel::Connection::open({path, false}, &opened, &why) == channel::OpenResult::Connected);
    host.join();
    std::shared_ptr<channel::Connection> conn(std::move(opened)), current = conn;
    CHECK(channel::may_promote(current, conn));
    std::atomic<bool> reader_done{false};
    std::thread reader([&] { conn->read_lines([](const std::string&) {}); reader_done = true; });
    // The hello answer arrives and the host goes away straight after (accountChanged, quit).
    send_all(peer, "yg {\"re\":1,\"ok\":true,\"result\":{}}\n");
    close(peer);
    reader.join();
    CHECK(reader_done && conn->closed());
    CHECK(!channel::may_promote(current, conn));        // never up after its reader ended
    std::shared_ptr<channel::Connection> none;
    CHECK(!channel::may_promote(none, conn));           // channel_closed already dropped it
    CHECK(!channel::may_promote(current, nullptr));
  }
  {  // replaced by a later candidate
    int peer_a = -1, peer_b = -1;
    std::thread host([&] { peer_a = accept(server, nullptr, nullptr); peer_b = accept(server, nullptr, nullptr); });
    std::unique_ptr<channel::Connection> a, b;
    std::string why;
    CHECK(channel::Connection::open({path, false}, &a, &why) == channel::OpenResult::Connected);
    CHECK(channel::Connection::open({path, false}, &b, &why) == channel::OpenResult::Connected);
    host.join();
    std::shared_ptr<channel::Connection> first(std::move(a)), second(std::move(b));
    CHECK(!channel::may_promote(second, first) && channel::may_promote(second, second));
    close(peer_a);
    close(peer_b);
  }
  close(server);
  ++groups;
}

// W2: a write the host can no longer take closes the connection; it is never written again.
void failed_write_closes() {
  const std::string dir = fresh("f", 0700);
  const std::string path = dir + "/sdk-v1.sock";
  const int server = listen_at(path);
  int peer = -1;
  std::thread host([&] { peer = accept(server, nullptr, nullptr); });
  std::unique_ptr<channel::Connection> c;
  std::string why;
  CHECK(channel::Connection::open({path, false}, &c, &why) == channel::OpenResult::Connected);
  host.join();
  close(peer);   // the host is gone
  bool failed = false;
  for (int i = 0; i < 1000 && !failed; ++i) failed = !c->write_line("yg {\"type\":\"hello\"}");
  CHECK(failed && c->closed());
  CHECK(!c->write_line("yg {}"));
  close(server);
  ++groups;
}

// close() from another thread wakes a reader that is waiting on a silent host.
void close_wakes_reader() {
  const std::string dir = fresh("c", 0700);
  const std::string path = dir + "/sdk-v1.sock";
  const int server = listen_at(path);
  int peer = -1;
  std::thread host([&] { peer = accept(server, nullptr, nullptr); });
  std::unique_ptr<channel::Connection> c;
  std::string why;
  CHECK(channel::Connection::open({path, false}, &c, &why) == channel::OpenResult::Connected);
  host.join();
  std::thread closer([&] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); c->close(); });
  int lines = 0;
  c->read_lines([&](const std::string&) { ++lines; });
  closer.join();
  CHECK(lines == 0 && c->closed());
  close(peer);
  close(server);
  ++groups;
}
#endif

int main(int argc, char** argv) {
  names();
  linux_order();
  hello_lines();
  lines_and_pushes();
  player_wait();
  publisher_rule();
  read_loop_close();
#ifndef _WIN32
  if (argc < 2) { std::fprintf(stderr, "usage: %s <scratch folder>\n", argv[0]); return 2; }
  g_base = argv[1];
  mkdir(g_base.c_str(), 0700);
  if (g_base.size() > 60) {
    std::printf("socket cases skipped: %s is too long for a socket path\n", g_base.c_str());
  } else {
    folder_rules();
    open_refusals();
    fake_host();
    close_wakes_reader();
    promote_race();
    failed_write_closes();
  }
#else
  (void)argc; (void)argv;
#endif
  std::printf("yougame_channel: %d groups passed\n", groups);
  return 0;
}
