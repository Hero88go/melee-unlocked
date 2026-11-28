// The local channel's transport and rules (yougame_channel.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_channel.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

#ifdef _WIN32
// GetNamedPipeServerProcessId, LABEL_SECURITY_INFORMATION, QueryFullProcessImageNameW: Vista and later.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <softpub.h>
#include <wintrust.h>
#include <wincrypt.h>
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")
#endif
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace yougame {
namespace channel {

using json = nlohmann::json;

// ------------------------------------------------------------------ names
std::string pipe_name(const std::string& sid, bool staging) {
  return std::string("\\\\.\\pipe\\yougame-") + (staging ? "staging-" : "") + "sdk-v1-" + sid;
}

std::string mac_socket(uint32_t uid, bool staging) {
  return std::string("/tmp/yougame-") + (staging ? "staging-" : "") + std::to_string(uid) + "/sdk-v1.sock";
}

std::vector<std::string> linux_sockets(const char* xdg_runtime_dir, uint32_t uid, bool staging) {
  const std::string folder = staging ? "yougame-staging" : "yougame";
  const std::string run = "/run/user/" + std::to_string(uid) + "/" + folder + "/sdk-v1.sock";
  std::vector<std::string> out;
  // Only an absolute XDG_RUNTIME_DIR counts (the XDG spec's rule); a relative one is ignored.
  if (xdg_runtime_dir && xdg_runtime_dir[0] == '/') {
    std::string xdg = xdg_runtime_dir;
    while (xdg.size() > 1 && xdg.back() == '/') xdg.pop_back();
    const std::string path = (xdg == "/" ? std::string() : xdg) + "/" + folder + "/sdk-v1.sock";
    if (path != run) out.push_back(path);
  }
  out.push_back(run);
  out.push_back("/tmp/" + folder + "-" + std::to_string(uid) + "/sdk-v1.sock");
  return out;
}

#ifdef _WIN32
namespace {
struct Handle {
  HANDLE h = nullptr;
  ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};
// The token's user (TOKEN_USER), in `buffer`; its Sid points into it.
bool token_user(HANDLE token, std::vector<unsigned char>& buffer) {
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  if (!size) return false;
  buffer.resize(size);
  return GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != FALSE;
}
PSID user_sid(std::vector<unsigned char>& buffer) {
  return reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid;
}
bool own_user(std::vector<unsigned char>& buffer) {
  Handle token;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.h)) return false;
  return token_user(token.h, buffer);
}
// The mandatory label's RID (SECURITY_MANDATORY_*_RID), or 0 when it cannot be read.
DWORD integrity_rid(HANDLE token) {
  DWORD size = 0;
  GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
  if (!size) return 0;
  std::vector<unsigned char> buffer(size);
  if (!GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), size, &size)) return 0;
  PSID sid = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data())->Label.Sid;
  if (!sid || !IsValidSid(sid)) return 0;
  const UCHAR count = *GetSidSubAuthorityCount(sid);
  if (!count) return 0;
  return *GetSidSubAuthority(sid, (DWORD)(count - 1));
}
std::string sid_string(PSID sid) {
  LPWSTR text = nullptr;
  if (!ConvertSidToStringSidW(sid, &text) || !text) return {};
  std::string out;
  for (const wchar_t* c = text; *c; ++c) out.push_back((char)(*c & 0x7F));   // S-1-5-...: ASCII
  LocalFree(text);
  return out;
}
std::wstring widen(const std::string& ascii) { return std::wstring(ascii.begin(), ascii.end()); }
}  // namespace
#endif

std::vector<Candidate> candidates(std::string* why) {
  std::vector<Candidate> out;
#ifdef _WIN32
  std::vector<unsigned char> me;
  const std::string sid = own_user(me) ? sid_string(user_sid(me)) : std::string();
  if (sid.empty()) {
    if (why) *why = "own user unreadable";
    return out;
  }
  for (bool staging : {false, true}) out.push_back({pipe_name(sid, staging), staging});
#elif defined(__APPLE__)
  for (bool staging : {false, true}) out.push_back({mac_socket((uint32_t)getuid(), staging), staging});
#else
  const char* xdg = std::getenv("XDG_RUNTIME_DIR");
  for (bool staging : {false, true})
    for (const auto& path : linux_sockets(xdg, (uint32_t)getuid(), staging)) out.push_back({path, staging});
#endif
  return out;
}

// ------------------------------------------------------------------ the folder check
#ifndef _WIN32
std::string folder_reason(const struct stat& st, uid_t uid) {
  if (S_ISLNK(st.st_mode)) return "folder is a link";
  if (!S_ISDIR(st.st_mode)) return "not a folder";
  if (st.st_uid != uid) return "folder belongs to another user";
  if ((st.st_mode & 07777) != 0700) return "folder is not private (0700)";
  return {};
}

std::string check_folder(const std::string& socket_path, uid_t uid, bool* missing) {
  if (missing) *missing = false;
  const size_t slash = socket_path.rfind('/');
  if (slash == std::string::npos || slash == 0) return "bad path";
  const std::string folder = socket_path.substr(0, slash);
  struct stat st;
  if (lstat(folder.c_str(), &st) != 0) {
    if (errno == ENOENT) {
      if (missing) *missing = true;
      return {};
    }
    return errno == ENOTDIR ? "not a folder" : "folder unreadable";
  }
  return folder_reason(st, uid);
}
#endif

// ------------------------------------------------------------------ lines
json hello_request(json body, const std::string& version, const std::string& slug) {
  if (!body.is_object() || body.value("type", "") != "hello") return body;
  body["game"] = slug;
  body["version"] = version;
  body["channel"] = 1;
  return body;
}

bool line_fits(const std::string& line_without_newline) {
  return line_without_newline.size() + 1 <= kLineLimit;
}

std::string request_line(const json& request) {
  std::string line = "yg " + request.dump();
  return line_fits(line) ? line : std::string();
}

LineAction classify(const std::string& line) {
  if (line.rfind("yg ", 0) != 0) return LineAction::Drop;
  const json j = json::parse(line.substr(3), nullptr, false);
  if (j.is_discarded() || !j.is_object()) return LineAction::Drop;
  if (j.count("re")) return j["re"].is_number_integer() ? LineAction::Answer : LineAction::Drop;
  // caps: what the app that started the game lets it do (relaunch, console boot): never from the channel.
  if (j.count("caps")) return LineAction::AppOnly;
  if (!j.count("push")) return LineAction::Push;   // other non-push lines: the stdio handler decides
  const json& push = j["push"];
  if (!push.is_object()) return LineAction::Drop;
  const std::string type = push.count("type") && push["type"].is_string() ? push["type"].get<std::string>() : "";
  if (type == "playerArrived") return LineAction::PlayerArrived;
  // The pad mirror's handle is a section handle (Windows) or a descriptor the app gave a child it
  // started: meaningless and unsafe from a host that did not start this process.
  if (type == "pads") return LineAction::AppOnly;
  return LineAction::Push;
}

std::string answer_code(const std::string& line) {
  if (line.rfind("yg ", 0) != 0) return {};
  const json j = json::parse(line.substr(3), nullptr, false);
  if (j.is_discarded() || !j.is_object() || !j.count("re") || j.value("ok", false)) return {};
  return j.count("code") && j["code"].is_string() ? j["code"].get<std::string>() : std::string();
}

uint64_t retry_after_ms(const json& answer) {
  double seconds = 2;
  if (answer.is_object() && answer.count("retryAfter") && answer["retryAfter"].is_number()) seconds = answer["retryAfter"].get<double>();
  if (!(seconds >= 1)) seconds = 1;   // NaN too
  if (seconds > 10) seconds = 10;
  return (uint64_t)(seconds * 1000);
}

std::string log_text(const std::string& text) {
  std::string out = text.substr(0, 120);
  for (char& c : out) if ((unsigned char)c < 0x20 || c == 0x7f) c = ' ';
  return out;
}

const Publisher& windows_publisher() {
  static const Publisher p{L"Fluo Inc.", L"Fluo Inc.", L"Orlando", L"Florida", L"US"};
  return p;
}

bool publisher_matches(const Publisher& seen) {
  const Publisher& want = windows_publisher();
  return seen.simple_name == want.simple_name && seen.organization == want.organization &&
         seen.locality == want.locality && seen.state == want.state && seen.country == want.country;
}

TrustVerdict trust_verdict(int32_t status) {
  switch ((uint32_t)status) {
    case 0: return TrustVerdict::Trusted;
    case 0x800B010Cu:   // CERT_E_REVOKED
    case 0x80092010u:   // CRYPT_E_REVOKED
      return TrustVerdict::Revoked;
    case 0x80092013u:   // CRYPT_E_REVOCATION_OFFLINE: nothing in the cache, and no network asked
    case 0x80092012u:   // CRYPT_E_NO_REVOCATION_CHECK
    case 0x800B010Eu:   // CERT_E_REVOCATION_FAILURE: how WinVerifyTrust reports the two above
      return TrustVerdict::RevocationUnknown;
    default: return TrustVerdict::Untrusted;
  }
}

std::string refusal_text(const json& answer) {
  if (!answer.is_object() || answer.value("ok", false)) return {};
  const std::string code = answer.count("code") && answer["code"].is_string() ? answer["code"].get<std::string>() : "";
  const bool has_words = answer.count("error") && answer["error"].is_string() && !answer["error"].get<std::string>().empty();
  if (code == "notYet") return "YouGame can't do this here yet.";
  if (code == "notHere") return "YouGame is still getting ready. Try again in a moment.";
  // A copy YouGame doesn't recognise: the app's own words win over ours. Ours never call it out of date,
  // because the newest build is refused the same way when the app can't look builds up.
  if (code == "unknown_build")
    return has_words ? answer["error"].get<std::string>()
                     : "YouGame couldn't recognise this copy of Melee. Reinstall it from YouGame, then try again.";
  // Recognised but replaced by a newer build (old_build today; build_replaced once YouGame remembers replaced files).
  // build_replaced may carry `current`, the live build's version: named when present (printable, short).
  if (code == "old_build" || code == "build_replaced") {
    std::string current;
    if (answer.count("current") && answer["current"].is_string())
      for (const char c : answer["current"].get<std::string>())
        if (c > 0x20 && c < 0x7F && current.size() < 40) current += c;
    return current.empty() ? "This copy of Melee is out of date. Update it to play online."
                           : "This copy of Melee is out of date (the latest is " + current + "). Update it to play online.";
  }
  return {};
}

bool hello_player(const json& result) {
  if (!result.is_object() || !result.count("player") || !result["player"].is_object()) return false;
  const json& p = result["player"];
  return p.count("id") && p["id"].is_string() && !p["id"].get<std::string>().empty();
}

bool hello_pending(const json& result) {
  return result.is_object() && result.value("playerPending", false) == true && !hello_player(result);
}

// ------------------------------------------------------------------ playerPending
void PlayerWait::answered(bool pending, bool player, uint64_t now_ms) {
  switch (state_) {
    case State::Idle:
      if (pending && !player) { state_ = State::Waiting; since_ = now_ms; sent_ = 0; }
      break;
    case State::Waiting:
      if (player) state_ = State::Arrived;
      else if (!pending || sent_ >= 2) state_ = State::GaveUp;   // signed out, or both retries used
      break;
    case State::Arrived:
    case State::GaveUp:
      break;
  }
}

void PlayerWait::pushed() {
  // The push can be read before the pending answer it follows has been looked at: Idle counts too.
  if (state_ == State::Idle || state_ == State::Waiting) state_ = State::Arrived;
}

uint64_t PlayerWait::due_at() const {
  if (state_ != State::Waiting || sent_ >= 2) return 0;
  return since_ + (sent_ == 0 ? kFirstRetryMs : kSecondRetryMs);
}

void PlayerWait::sent() { if (sent_ < 2) ++sent_; }

bool PlayerWait::take_arrival() {
  if (state_ != State::Arrived || told_) return false;
  told_ = true;
  return true;
}

// ------------------------------------------------------------------ the transport
namespace {
// Splits a byte stream into lines; a line over kIncomingLimit is dropped through its newline.
class LineSplitter {
 public:
  template <class F>
  void feed(const char* data, size_t size, const F& on_line) {
    for (size_t i = 0; i < size; ++i) {
      const char c = data[i];
      if (c == '\n') {
        if (!discard_) {
          if (!pending_.empty() && pending_.back() == '\r') pending_.pop_back();
          on_line(pending_);
        }
        pending_.clear();
        discard_ = false;
      } else if (!discard_) {
        if (pending_.size() >= kIncomingLimit) { pending_.clear(); discard_ = true; }
        else pending_.push_back(c);
      }
    }
  }
 private:
  std::string pending_;
  bool discard_ = false;
};
}  // namespace

#ifdef _WIN32
namespace {
std::wstring subject_attribute(PCCERT_CONTEXT cert, const char* oid) {
  wchar_t text[256] = {};
  const DWORD got = CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, const_cast<char*>(oid), text, 256);
  return got > 1 ? std::wstring(text) : std::wstring();
}
}  // namespace

bool verify_host_publisher(const std::wstring& image_path, std::string* why) {
  WINTRUST_FILE_INFO file{};
  file.cbStruct = sizeof file;
  file.pcwszFilePath = image_path.c_str();
  GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  WINTRUST_DATA data{};
  data.cbStruct = sizeof data;
  data.dwUIChoice = WTD_UI_NONE;
  data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;   // the root is excluded below (its state is rarely readable)
  data.dwUnionChoice = WTD_CHOICE_FILE;
  data.pFile = &file;
  data.dwStateAction = WTD_STATEACTION_VERIFY;
  data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;   // no network on the probe
  LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
  bool ok = false;
  TrustVerdict verdict = trust_verdict((int32_t)status);
  if (verdict == TrustVerdict::RevocationUnknown) {
    // Revocation unreadable: the one code may hide another chain error, so check again with
    // revocation off and require a clean 0.
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    data.hWVTStateData = nullptr;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    verdict = status == 0 ? TrustVerdict::Trusted : TrustVerdict::Untrusted;
  }
  if (verdict == TrustVerdict::Revoked) {
    if (why) *why = "host certificate revoked";
  } else if (verdict == TrustVerdict::Untrusted) {
    if (why) *why = "host is not signed";
  } else {
    CRYPT_PROVIDER_DATA* provider = WTHelperProvDataFromStateData(data.hWVTStateData);
    CRYPT_PROVIDER_SGNR* signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
    CRYPT_PROVIDER_CERT* leaf = signer ? WTHelperGetProvCertFromChain(signer, 0) : nullptr;
    if (leaf && leaf->pCert) {
      Publisher seen;
      wchar_t name[256] = {};
      if (CertGetNameStringW(leaf->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256) > 1) seen.simple_name = name;
      seen.organization = subject_attribute(leaf->pCert, szOID_ORGANIZATION_NAME);
      seen.locality = subject_attribute(leaf->pCert, szOID_LOCALITY_NAME);
      seen.state = subject_attribute(leaf->pCert, szOID_STATE_OR_PROVINCE_NAME);
      seen.country = subject_attribute(leaf->pCert, szOID_COUNTRY_NAME);
      ok = publisher_matches(seen);
      if (!ok && why) *why = "host is signed by another publisher";
    } else if (why) {
      *why = "host signer unreadable";
    }
  }
  data.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
  return ok;
}

OpenResult Connection::open(const Candidate& where, std::unique_ptr<Connection>* out, std::string* why) {
  const std::wstring name = widen(where.path);
  HANDLE pipe = INVALID_HANDLE_VALUE;
  for (int attempt = 0; attempt < 2; ++attempt) {
    // Identification only: a program holding the name can see who we are, never act as us.
    pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) break;
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return OpenResult::Missing;
    if (error == ERROR_PIPE_BUSY && attempt == 0 && WaitNamedPipeW(name.c_str(), kPipeBusyWaitMs)) continue;
    if (why) *why = error == ERROR_PIPE_BUSY ? "busy" : "open failed (" + std::to_string(error) + ")";
    return OpenResult::Refused;
  }
  if (pipe == INVALID_HANDLE_VALUE) {
    if (why) *why = "busy";
    return OpenResult::Refused;
  }
  std::unique_ptr<Connection> c(new Connection());
  c->pipe_ = pipe;   // closed by the destructor on every refusal below
  auto refuse = [&](const char* reason) { if (why) *why = reason; return OpenResult::Refused; };

  std::vector<unsigned char> me;
  if (!own_user(me)) return refuse("own user unreadable");
  // The process behind the pipe runs as us, at medium integrity or higher.
  ULONG pid = 0;
  if (!GetNamedPipeServerProcessId(pipe, &pid) || !pid) return refuse("server process unknown");
  Handle process;
  process.h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process.h) return refuse("server process unreadable");
  Handle token;
  if (!OpenProcessToken(process.h, TOKEN_QUERY, &token.h)) return refuse("server token unreadable");
  std::vector<unsigned char> server;
  if (!token_user(token.h, server)) return refuse("server user unreadable");
  if (!EqualSid(user_sid(server), user_sid(me))) return refuse("server runs as another user");
  if (integrity_rid(token.h) < SECURITY_MANDATORY_MEDIUM_RID) return refuse("server integrity below medium");
  // The pipe itself is ours (a program that took the name first would own it).
  PSID owner = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const DWORD got = GetSecurityInfo(pipe, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr,
                                    nullptr, &descriptor);
  const bool owned = got == ERROR_SUCCESS && owner && EqualSid(owner, user_sid(me));
  if (descriptor) LocalFree(descriptor);
  if (!owned) return refuse("pipe owned by another user");
  // The pipe object's own mandatory label: a low-integrity program that took the name first gets a
  // low label on what it creates. No label means medium.
  PACL sacl = nullptr;
  PSECURITY_DESCRIPTOR label_descriptor = nullptr;
  const DWORD label_read = GetSecurityInfo(pipe, SE_KERNEL_OBJECT, LABEL_SECURITY_INFORMATION, nullptr, nullptr,
                                           nullptr, &sacl, &label_descriptor);
  DWORD label = SECURITY_MANDATORY_MEDIUM_RID;
  if (label_read == ERROR_SUCCESS && sacl) {
    for (DWORD i = 0; i < sacl->AceCount; ++i) {
      void* ace = nullptr;
      if (!GetAce(sacl, i, &ace) || !ace) continue;
      if (static_cast<ACE_HEADER*>(ace)->AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE) continue;
      PSID sid = &static_cast<SYSTEM_MANDATORY_LABEL_ACE*>(ace)->SidStart;
      const UCHAR count = IsValidSid(sid) ? *GetSidSubAuthorityCount(sid) : 0;
      label = count ? *GetSidSubAuthority(sid, count - 1) : 0;
      break;
    }
  }
  if (label_descriptor) LocalFree(label_descriptor);
  if (label_read != ERROR_SUCCESS) return refuse("pipe label unreadable");
  if (label < SECURITY_MANDATORY_MEDIUM_RID) return refuse("pipe label below medium");
#ifndef YOUGAME_AGENT_TEST_HOOK
  // Signed by YouGame's publisher (agent test builds talk to unsigned dev apps, so they skip it).
  wchar_t image[MAX_PATH * 4] = {};
  DWORD length = (DWORD)(sizeof image / sizeof image[0]);
  if (!QueryFullProcessImageNameW(process.h, 0, image, &length) || !length) return refuse("server image unknown");
  std::string signer_why;
  if (!verify_host_publisher(std::wstring(image, length), &signer_why)) {
    if (why) *why = signer_why;
    return OpenResult::Refused;
  }
#endif
  c->read_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  c->write_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!c->read_event_ || !c->write_event_) return refuse("no events");
  *out = std::move(c);
  return OpenResult::Connected;
}

Connection::~Connection() {
  close();
  if (pipe_ && pipe_ != INVALID_HANDLE_VALUE) CloseHandle(static_cast<HANDLE>(pipe_));
  if (read_event_) CloseHandle(static_cast<HANDLE>(read_event_));
  if (write_event_) CloseHandle(static_cast<HANDLE>(write_event_));
}

bool Connection::write_line(const std::string& line) {
  if (!line_fits(line)) return false;
  std::lock_guard<std::mutex> lock(write_mutex_);
  if (closed_) return false;
  const std::string data = line + "\n";
  HANDLE pipe = static_cast<HANDLE>(pipe_);
  HANDLE event = static_cast<HANDLE>(write_event_);
  OVERLAPPED ov{};
  ov.hEvent = event;
  ResetEvent(event);
  // A failed or partial write leaves the host mid-line: the connection is closed, never written again.
  if (!WriteFile(pipe, data.data(), (DWORD)data.size(), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) {
    close();
    return false;
  }
  DWORD wrote = 0;
  if (WaitForSingleObject(event, kWriteTimeoutMs) != WAIT_OBJECT_0) {
    CancelIoEx(pipe, &ov);
    GetOverlappedResult(pipe, &ov, &wrote, TRUE);   // the OVERLAPPED must outlive the I/O
    close();
    return false;
  }
  if (!GetOverlappedResult(pipe, &ov, &wrote, FALSE) || wrote != data.size()) {
    close();
    return false;
  }
  return true;
}

void Connection::read_lines(const std::function<void(const std::string&)>& on_line) {
  HANDLE pipe = static_cast<HANDLE>(pipe_);
  HANDLE event = static_cast<HANDLE>(read_event_);
  LineSplitter lines;
  char buffer[4096];
  while (!closed_) {
    OVERLAPPED ov{};
    ov.hEvent = event;
    ResetEvent(event);
    DWORD got = 0;
    const bool read = detail::read_once(closed_,
        [&] {
          if (ReadFile(pipe, buffer, sizeof buffer, nullptr, &ov)) return detail::Issued::Done;
          const DWORD error = GetLastError();
          if (error == ERROR_IO_PENDING) return detail::Issued::Pending;
          return error == ERROR_MORE_DATA ? detail::Issued::Done : detail::Issued::Failed;
        },
        // close() may have cancelled before this read was issued: cancel it here instead.
        [&] { CancelIoEx(pipe, &ov); },
        [&] { return GetOverlappedResult(pipe, &ov, &got, TRUE) || GetLastError() == ERROR_MORE_DATA; });
    if (!read) break;
    if (got) lines.feed(buffer, got, on_line);
  }
  closed_ = true;
}

void Connection::close() {
  // The flag first, then the cancel (detail::read_once depends on this order).
  if (closed_.exchange(true)) return;
  // Wakes the reader's pending ReadFile (and a stuck write) with ERROR_OPERATION_ABORTED.
  if (pipe_ && pipe_ != INVALID_HANDLE_VALUE) CancelIoEx(static_cast<HANDLE>(pipe_), nullptr);
}

#else  // POSIX

OpenResult Connection::open(const Candidate& where, std::unique_ptr<Connection>* out, std::string* why) {
  auto refuse = [&](const char* reason) { if (why) *why = reason; return OpenResult::Refused; };
  const uid_t me = geteuid();
  bool missing = false;
  const std::string folder = check_folder(where.path, me, &missing);
  if (missing) return OpenResult::Missing;
  if (!folder.empty()) { if (why) *why = folder; return OpenResult::Refused; }
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (where.path.size() >= sizeof address.sun_path) return refuse("path too long");
  std::memcpy(address.sun_path, where.path.c_str(), where.path.size() + 1);
  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return refuse("no socket");
  std::unique_ptr<Connection> c(new Connection());
  c->fd_ = fd;   // closed by the destructor on every refusal below
  fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
  const int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  // Non-blocking connect, so a host whose backlog is full cannot hold the probe.
  const int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0) {
    const int error = errno;
    // No socket, or a socket file nobody listens on (the app quit without removing it).
    if (error == ENOENT || error == ECONNREFUSED) return OpenResult::Missing;
    if (error != EINPROGRESS) return refuse(error == EAGAIN ? "busy" : "connect failed");
    pollfd p{fd, POLLOUT, 0};
    int soerror = 0;
    socklen_t length = sizeof soerror;
    if (poll(&p, 1, kPipeBusyWaitMs) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerror, &length) != 0 || soerror)
      return refuse("busy");
  }
  fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
  // The process at the other end is ours.
#if defined(__APPLE__)
  uid_t peer = (uid_t)-1;
  gid_t group = (gid_t)-1;
  if (getpeereid(fd, &peer, &group) != 0) return refuse("peer unknown");
#elif defined(__linux__)
  ucred credentials{};
  socklen_t size = sizeof credentials;
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0 || size != sizeof credentials)
    return refuse("peer unknown");
  const uid_t peer = credentials.uid;
#else
  const uid_t peer = (uid_t)-1;
  return refuse("no peer check on this system");
#endif
  if (peer != me) return refuse("peer is another user");
  timeval timeout{kWriteTimeoutMs / 1000, (kWriteTimeoutMs % 1000) * 1000};
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
  *out = std::move(c);
  return OpenResult::Connected;
}

Connection::~Connection() {
  close();
  if (fd_ >= 0) ::close(fd_);
}

bool Connection::write_line(const std::string& line) {
  if (!line_fits(line)) return false;
  std::lock_guard<std::mutex> lock(write_mutex_);
  if (closed_) return false;
  const std::string data = line + "\n";
  size_t sent = 0;
#ifdef MSG_NOSIGNAL
  constexpr int kFlags = MSG_NOSIGNAL;
#else
  constexpr int kFlags = 0;   // SO_NOSIGPIPE is set on the socket (Mac)
#endif
  while (sent < data.size()) {
    const ssize_t n = send(fd_, data.data() + sent, data.size() - sent, kFlags);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {
      // Failed or timed out part way: the host would read half a line. Closed, never written again.
      close();
      return false;
    }
    sent += (size_t)n;
  }
  return true;
}

void Connection::read_lines(const std::function<void(const std::string&)>& on_line) {
  LineSplitter lines;
  char buffer[4096];
  while (!closed_) {
    const ssize_t n = recv(fd_, buffer, sizeof buffer, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    lines.feed(buffer, (size_t)n, on_line);
  }
  closed_ = true;
}

void Connection::close() {
  if (closed_.exchange(true)) return;
  if (fd_ >= 0) shutdown(fd_, SHUT_RDWR);   // wakes the reader; the descriptor closes with the object
}
#endif

}  // namespace channel
}  // namespace yougame
