// Discord Rich Presence over the Discord client's local IPC socket.
//
// Protocol: https://docs.discord.com/developers/topics/rpc. A frame is a little-endian u32 opcode
// and u32 length followed by a JSON body; opcode 0 opens with {"v":1,"client_id":...}, opcode 1
// carries commands and dispatches, opcode 3 is a ping we answer with opcode 4. We send SET_ACTIVITY
// and subscribe to ACTIVITY_JOIN, which is what puts a Join button on the player's profile and
// hands us the host's connect code when a friend presses it.
//
// Deliberately no SDK: the Social SDK and the legacy Game SDK are closed-source redistributable
// DLLs whose terms forbid the redistribution and modification GPL-2 obliges us to permit, so this
// speaks the published protocol itself. See scratchpad/discord_invite_design.md.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "discord_presence.h"
#include "host.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace host::discord {
namespace {

using json = nlohmann::json;

enum Op : uint32_t { OP_HANDSHAKE = 0, OP_FRAME = 1, OP_CLOSE = 2, OP_PING = 3, OP_PONG = 4 };

constexpr uint64_t RECONNECT_MS = 15000;   // how long to leave Discord alone after a failure
constexpr uint64_t REFUSED_MS = 300000;    // longer, when Discord actively refused us (bad app id)
constexpr uint64_t UPDATE_MIN_MS = 4000;   // Discord rate limits SET_ACTIVITY to 5 per 20 seconds
constexpr uint64_t HANDSHAKE_MS = 2000;    // bounded wait for READY, in 20 ms interruptible slices
constexpr uint32_t FRAME_MAX = 64 * 1024;  // larger than Discord ever sends; anything bigger is junk

std::mutex g_mutex;                        // guards everything below except the atomics
std::condition_variable g_wake;
std::string g_app_id, g_status = "Off", g_join_code;
// The most recent invite, kept for the on-screen notice. Separate from g_join_code because that
// one is consumed by the Direct code list the moment the game asks for it, which would leave
// nothing to show.
std::string g_invite_notice;
uint64_t g_invite_notice_until = 0;
constexpr uint64_t INVITE_NOTICE_MS = 15000;   // long enough to read and act on, not a permanent banner
Presence g_wanted;
std::thread g_thread;
std::atomic<bool> g_enabled{false}, g_stop{false}, g_has_join{false};

uint64_t now_ms() { return GetTickCount64(); }
void set_status(std::string text) { std::lock_guard<std::mutex> lk(g_mutex); g_status = std::move(text); }
bool should_quit() { return g_stop.load(std::memory_order_acquire) || !g_enabled.load(std::memory_order_acquire); }

// json::value() throws when the key is present with the wrong type, and Discord sends "evt": null
// on ordinary command replies, so every field read off the wire goes through this.
std::string str_field(const json& obj, const char* key) {
  const auto it = obj.find(key);
  return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// Slippi connect codes are TAG#1234: 1-8 uppercase letters or digits, '#', then 1-8 digits. A join
// secret arrives from someone else's machine, so nothing that is not one of these is ever used.
bool valid_connect_code(const std::string& code) {
  if (code.size() < 3 || code.size() > 18) return false;
  const size_t hash = code.find('#');
  if (hash == std::string::npos || hash < 1 || hash > 8) return false;
  for (size_t i = 0; i < hash; ++i)
    if (!((code[i] >= 'A' && code[i] <= 'Z') || (code[i] >= '0' && code[i] <= '9'))) return false;
  const size_t digits = code.size() - hash - 1;
  if (digits < 1 || digits > 8) return false;
  for (size_t i = hash + 1; i < code.size(); ++i)
    if (code[i] < '0' || code[i] > '9') return false;
  return true;
}

// Small payloads into a 64 KB pipe buffer, so this does not meaningfully block; and when it does,
// it blocks the presence thread and nothing else.
bool write_frame(HANDLE pipe, uint32_t op, const std::string& body) {
  const uint32_t header[2] = {op, (uint32_t)body.size()};
  DWORD written = 0;
  if (!WriteFile(pipe, header, sizeof header, &written, nullptr) || written != sizeof header) return false;
  if (body.empty()) return true;
  written = 0;
  return WriteFile(pipe, body.data(), (DWORD)body.size(), &written, nullptr) && written == body.size();
}

// Reads are always gated behind PeekNamedPipe, so the thread never sits in a blocking ReadFile
// waiting on a Discord client that has stopped talking.
struct Reader {
  std::string buf;
  bool bad = false;      // the stream is out of step and cannot be resynchronised
  bool closed = false;   // the pipe went away
  void reset() { buf.clear(); bad = false; closed = false; }
  // Note this reports a dead pipe through `closed` rather than by returning: Discord answers a bad
  // Application ID with a CLOSE frame and then drops the pipe immediately, so the very read that
  // collects the reason is followed by a failing peek. Callers must drain next() before believing
  // the connection is gone, or the one frame that says what went wrong is thrown away.
  void pump(HANDLE pipe) {
    while (!closed) {
      DWORD available = 0;
      if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) { closed = true; return; }
      if (!available) return;
      const size_t at = buf.size();
      if (at + available > FRAME_MAX * 4u) { bad = true; return; }   // runaway peer
      buf.resize(at + available);
      DWORD got = 0;
      if (!ReadFile(pipe, &buf[at], available, &got, nullptr)) { buf.resize(at); closed = true; return; }
      buf.resize(at + got);
      if (!got) return;
    }
  }
  bool next(uint32_t& op, std::string& body) {
    if (buf.size() < 8) return false;
    uint32_t header[2];
    std::memcpy(header, buf.data(), sizeof header);
    if (header[1] > FRAME_MAX) { bad = true; return false; }   // out of step with the stream
    if (buf.size() < 8 + (size_t)header[1]) return false;
    op = header[0];
    body.assign(buf, 8, header[1]);
    buf.erase(0, 8 + (size_t)header[1]);
    return true;
  }
};

// The activity object Discord renders. Returns a null json when there is nothing to show, which is
// how SET_ACTIVITY clears a presence.
json activity_json(const Presence& p) {
  json activity = json::object();
  if (!p.details.empty()) activity["details"] = p.details;
  if (!p.state.empty()) activity["state"] = p.state;
  // The picture beside the presence. Discord looks the name up in the application's Rich Presence
  // art assets, so it stays blank until an asset called this is uploaded there; sending it costs
  // nothing meanwhile.
  activity["assets"] = {{"large_image", "melee_unlocked"}, {"large_text", "Melee Unlocked"}};
  const bool has_party = p.party_size > 0 && p.party_max >= p.party_size;
  if (has_party) {
    // The party id only has to be stable and unique to this player; their own connect code is both,
    // and shows nothing the presence does not already show.
    activity["party"] = {{"id", "melee-unlocked-" + (p.join_code.empty() ? std::string("solo") : p.join_code)},
                         {"size", json::array({p.party_size, p.party_max})}};
  }
  // A Join button is only useful while there is room, and the secret is only ever a connect code:
  // a presence is public, so an IP address must never reach this payload.
  if (!p.join_code.empty() && has_party && p.party_size < p.party_max) {
    activity["secrets"] = {{"join", p.join_code}};
    activity["instance"] = true;
  }
  return activity.empty() ? json(nullptr) : activity;
}

void copy_to_clipboard(const std::string& text) {
  // Slippi Direct is symmetric: the host has to enter the joiner's code too, and Discord gives us
  // no way to send it back. Putting it on the clipboard makes that a single paste into the chat.
  if (text.empty() || !OpenClipboard(nullptr)) return;   // another process owns it; not worth retrying
  EmptyClipboard();
  if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1)) {
    if (void* dst = GlobalLock(mem)) {
      std::memcpy(dst, text.c_str(), text.size() + 1);
      GlobalUnlock(mem);
      if (!SetClipboardData(CF_TEXT, mem)) GlobalFree(mem);
    } else {
      GlobalFree(mem);
    }
  }
  CloseClipboard();
}

// Result of one attempt, so the settings panel can tell "Discord is not running" apart from
// "Discord is running and says your Application ID is wrong", which are the two things a player
// actually needs to know and which look identical if all you report is a failure.
struct ConnectResult {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  bool saw_pipe = false;     // a Discord IPC pipe existed, so Discord is running
  std::string refusal;       // Discord closed the handshake and said why (almost always a bad id)
};

// Opens one of Discord's IPC pipes and completes the handshake. Plain CreateFile and never
// WaitNamedPipe, so "Discord is not installed" costs microseconds rather than a stall.
ConnectResult try_connect(const std::string& app_id, Reader& reader) {
  ConnectResult result;
  for (int n = 0; n < 10; ++n) {
    char name[64];
    std::snprintf(name, sizeof name, "\\\\.\\pipe\\discord-ipc-%d", n);
    HANDLE pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) continue;
    result.saw_pipe = true;
    reader.reset();
    bool ready = false, dead = false;
    if (write_frame(pipe, OP_HANDSHAKE, json({{"v", 1}, {"client_id", app_id}}).dump())) {
      for (uint64_t waited = 0; waited < HANDSHAKE_MS && !ready && !dead && !should_quit(); waited += 20) {
        reader.pump(pipe);
        uint32_t op = 0;
        std::string body;
        while (!ready && !dead && reader.next(op, body)) {
          const json msg = json::parse(body, nullptr, false);
          const bool object = !msg.is_discarded() && msg.is_object();
          if (op == OP_CLOSE) {
            // e.g. {"code":4000,"message":"Invalid Client ID"}
            if (object) result.refusal = str_field(msg, "message");
            if (result.refusal.empty()) result.refusal = "no reason given";
            dead = true;
            break;
          }
          if (op != OP_FRAME || !object) continue;
          if (str_field(msg, "evt") == "READY") ready = true;
        }
        if (reader.bad || reader.closed) dead = true;   // only after the buffer has been drained
        if (!ready && !dead) Sleep(20);
      }
    }
    if (ready) {
      // The one subscription that matters: ACTIVITY_JOIN is how a friend's click reaches us.
      write_frame(pipe, OP_FRAME,
                  json({{"cmd", "SUBSCRIBE"}, {"evt", "ACTIVITY_JOIN"}, {"nonce", "melee-sub-join"}, {"args", json::object()}}).dump());
      result.pipe = pipe;
      result.refusal.clear();
      return result;
    }
    CloseHandle(pipe);
    if (!result.refusal.empty()) break;   // the id is wrong; the other pipes will say the same
  }
  return result;
}

// Drains and acts on whatever Discord has sent.
enum class Pump { Ok, Drop, Refused };
Pump handle_frames(HANDLE pipe, Reader& reader) {
  reader.pump(pipe);   // whatever it collected is still worth reading even if the pipe then died
  uint32_t op = 0;
  std::string body;
  while (reader.next(op, body)) {
    if (op == OP_CLOSE) return Pump::Drop;
    if (op == OP_PING) { if (!write_frame(pipe, OP_PONG, body)) return Pump::Drop; continue; }
    if (op != OP_FRAME) continue;
    const json msg = json::parse(body, nullptr, false);
    if (msg.is_discarded() || !msg.is_object()) continue;
    const std::string evt = str_field(msg, "evt");
    const auto data = msg.find("data");             // the vendored nlohmann predates json::contains
    const bool has_data = data != msg.end() && data->is_object();
    if (evt == "ERROR") {
      // Almost always a bad application id, or Discord declining the app. That will not fix itself
      // in fifteen seconds, so say it once and then leave Discord alone for a long while.
      const std::string text = has_data ? str_field(*data, "message") : std::string();
      host::log("discord: the Discord client refused the request: %s", text.empty() ? "no reason given" : text.c_str());
      set_status("Discord refused the request: " + (text.empty() ? std::string("check the application ID") : text));
      return Pump::Refused;
    }
    if (evt == "ACTIVITY_JOIN" && has_data) {
      const std::string secret = str_field(*data, "secret");
      if (!valid_connect_code(secret)) {
        host::log("discord: ignoring a join secret that is not a Slippi connect code");
        continue;
      }
      std::string mine;
      {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_join_code = secret;
        mine = g_wanted.join_code;
        g_invite_notice = secret;
        g_invite_notice_until = now_ms() + INVITE_NOTICE_MS;
      }
      g_has_join.store(true, std::memory_order_release);
      copy_to_clipboard(mine);
      host::log("discord: join invite accepted for %s", secret.c_str());
      set_status(mine.empty() ? "Joining " + secret + ". Open Online > Direct: the code is waiting there."
                              : "Joining " + secret + ". Open Online > Direct; your code " + mine + " is on the clipboard for them.");
    }
  }
  return reader.bad || reader.closed ? Pump::Drop : Pump::Ok;
}

void worker() {
  Reader reader;
  HANDLE pipe = INVALID_HANDLE_VALUE;
  uint64_t next_connect_ms = 0, last_update_ms = 0, nonce = 1;
  std::string last_activity;
  std::string app_id;
  { std::lock_guard<std::mutex> lk(g_mutex); app_id = g_app_id; }

  while (!should_quit()) {
    if (pipe == INVALID_HANDLE_VALUE) {
      if (now_ms() >= next_connect_ms) {
        const ConnectResult result = try_connect(app_id, reader);
        pipe = result.pipe;
        next_connect_ms = now_ms() + (result.refusal.empty() ? RECONNECT_MS : REFUSED_MS);
        if (pipe != INVALID_HANDLE_VALUE) {
          last_activity.clear();
          last_update_ms = 0;
          host::log("discord: connected to the Discord client");
          set_status("Connected. Friends can see Melee Unlocked and press Join.");
        } else if (should_quit()) {
          // stopping; leave the status alone
        } else if (!result.refusal.empty()) {
          host::log("discord: the Discord client rejected application ID %s: %s", app_id.c_str(), result.refusal.c_str());
          set_status("Discord rejected the Application ID (" + result.refusal + "). Check it at discord.com/developers/applications.");
        } else if (result.saw_pipe) {
          set_status("Discord is running but did not answer. Trying again every 15 seconds.");
        } else {
          set_status("Discord is not running. Checking again every 15 seconds.");
        }
      }
    } else if (const Pump pumped = handle_frames(pipe, reader); pumped != Pump::Ok) {
      CloseHandle(pipe);
      pipe = INVALID_HANDLE_VALUE;
      reader.reset();
      next_connect_ms = now_ms() + (pumped == Pump::Refused ? REFUSED_MS : RECONNECT_MS);
      if (pumped == Pump::Drop) host::log("discord: lost the connection to the Discord client");
    } else if (now_ms() - last_update_ms >= UPDATE_MIN_MS) {
      Presence wanted;
      { std::lock_guard<std::mutex> lk(g_mutex); wanted = g_wanted; }
      const json activity = activity_json(wanted);
      // Only the nonce differs between two otherwise identical frames, so compare the activity.
      std::string dumped = activity.dump();
      if (dumped != last_activity) {
        const json frame = {{"cmd", "SET_ACTIVITY"},
                            {"nonce", "melee-activity-" + std::to_string(nonce++)},
                            {"args", {{"pid", (int)GetCurrentProcessId()}, {"activity", activity}}}};
        if (write_frame(pipe, OP_FRAME, frame.dump())) {
          last_activity = std::move(dumped);
          last_update_ms = now_ms();
        } else {
          CloseHandle(pipe);
          pipe = INVALID_HANDLE_VALUE;
          reader.reset();
          next_connect_ms = now_ms() + RECONNECT_MS;
        }
      }
    }
    std::unique_lock<std::mutex> lk(g_mutex);
    g_wake.wait_for(lk, std::chrono::milliseconds(250));
  }

  if (pipe != INVALID_HANDLE_VALUE) {
    // Take the presence down on the way out rather than leaving a stale "In a match" on the profile.
    write_frame(pipe, OP_FRAME,
                json({{"cmd", "SET_ACTIVITY"}, {"nonce", "melee-activity-clear"},
                      {"args", {{"pid", (int)GetCurrentProcessId()}, {"activity", nullptr}}}}).dump());
    CloseHandle(pipe);
  }
}

// What Discord's own discord-rpc calls Discord_Register: a discord-<application id> URL protocol
// under the current user pointing at this game's launcher. Discord's "Ask to Join" is only offered
// on a machine where the application is registered this way; without it the friend's client says
// the game is not detected, whether or not the game is running. Current user only, no elevation.
void register_launch(const std::string& application_id) {
  wchar_t self[MAX_PATH]{};
  if (!GetModuleFileNameW(nullptr, self, MAX_PATH)) return;
  std::wstring exe = self;
  const size_t slash = exe.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    const std::wstring launcher = exe.substr(0, slash + 1) + L"melee_unlocked.exe";
    if (GetFileAttributesW(launcher.c_str()) != INVALID_FILE_ATTRIBUTES) exe = launcher;
  }
  std::wstring id(application_id.begin(), application_id.end());
  const std::wstring key = L"Software\\Classes\\discord-" + id;
  const std::wstring command = L"\"" + exe + L"\" \"%1\"";
  HKEY h = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return;
  const std::wstring name = L"URL:Run game " + id + L" protocol";
  RegSetValueExW(h, nullptr, 0, REG_SZ, (const BYTE*)name.c_str(), (DWORD)((name.size() + 1) * sizeof(wchar_t)));
  RegSetValueExW(h, L"URL Protocol", 0, REG_SZ, (const BYTE*)L"", sizeof(wchar_t));
  HKEY icon = nullptr;
  if (RegCreateKeyExW(h, L"DefaultIcon", 0, nullptr, 0, KEY_WRITE, nullptr, &icon, nullptr) == ERROR_SUCCESS) {
    RegSetValueExW(icon, nullptr, 0, REG_SZ, (const BYTE*)exe.c_str(), (DWORD)((exe.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(icon);
  }
  HKEY open = nullptr;
  if (RegCreateKeyExW(h, L"shell\\open\\command", 0, nullptr, 0, KEY_WRITE, nullptr, &open, nullptr) == ERROR_SUCCESS) {
    RegSetValueExW(open, nullptr, 0, REG_SZ, (const BYTE*)command.c_str(), (DWORD)((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(open);
  }
  RegCloseKey(h);
}

}  // namespace

void configure(const std::string& application_id) {
  std::lock_guard<std::mutex> lk(g_mutex);
  g_app_id = application_id;
}

void enable(bool on) {
  if (!on) {
    const bool was = g_enabled.exchange(false, std::memory_order_acq_rel);
    g_wake.notify_all();
    if (g_thread.joinable()) g_thread.join();   // bounded: the worker checks for this every 20 ms
    if (was) set_status("Off");
    return;
  }
  if (g_enabled.load(std::memory_order_acquire)) return;
  std::string id;
  { std::lock_guard<std::mutex> lk(g_mutex); id = g_app_id; }
  if (!id.empty()) register_launch(id);
  if (id.empty()) {
    host::log("discord: presence is on but no application ID is set; staying off");
    set_status("No application ID. Create one at discord.com/developers/applications and paste it here.");
    return;
  }
  if (g_thread.joinable()) g_thread.join();     // a previous run that has already stopped
  {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_wanted = Presence{};
    g_wanted.details = "In the menus";
    g_status = "Looking for the Discord client...";
  }
  g_stop.store(false, std::memory_order_release);
  g_enabled.store(true, std::memory_order_release);
  g_thread = std::thread(worker);
}

bool enabled() { return g_enabled.load(std::memory_order_relaxed); }

void publish(const Presence& p) {
  if (!g_enabled.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lk(g_mutex);
  g_wanted = p;
}

void clear() { publish(Presence{}); }

std::string status() {
  std::lock_guard<std::mutex> lk(g_mutex);
  return g_status;
}

std::string take_join_code() {
  if (!g_has_join.load(std::memory_order_acquire)) return {};
  std::lock_guard<std::mutex> lk(g_mutex);
  g_has_join.store(false, std::memory_order_release);
  std::string out;
  out.swap(g_join_code);
  return out;
}

std::string invite_notice() {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (g_invite_notice.empty()) return {};
  if (now_ms() >= g_invite_notice_until) { g_invite_notice.clear(); return {}; }
  return g_invite_notice;
}

void shutdown() {
  g_stop.store(true, std::memory_order_release);
  g_enabled.store(false, std::memory_order_release);
  g_wake.notify_all();
  if (g_thread.joinable()) g_thread.join();
}

}  // namespace host::discord
