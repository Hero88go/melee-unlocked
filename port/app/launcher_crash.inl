// Send crash report: after the game exits, when it left a crash file newer than the launch, the
// launcher offers to send the crash files. Nothing is sent until the player says yes. The files go
// to our relay (tools/crash_relay), which stores them for tools/pull_reports.py; no Discord webhook
// or token lives in the launcher. "Send logs" (below) uses the same relay with a player's note. Without a relay, or when it fails, the launcher opens the folder with the
// readable Markdown, sanitized ZIP and a prefilled GitHub issue instead. No binary dump is sent.
// What the zip holds, and its 8 MB cap, is
// launcher_crash_zip.h (shared with port_launcher_crash_zip_test).
// SPDX-License-Identifier: GPL-2.0-or-later
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace crash_report {

// The relay (tools/crash_relay, deployed 2026-09-30): zip only, 8 MB, one report per IP per 10 minutes,
// 50 a day. Empty = GitHub fallback only.
constexpr const wchar_t* kCrashRelayUrl = L"https://melee-crash-relay.firescribe-share-worker.workers.dev/report";

FILETIME g_launch_time{};
void note_launch() { GetSystemTimeAsFileTime(&g_launch_time); }

// A header value cannot hold line breaks or anything outside printable ASCII.
std::string url_safe_header(const std::string& s) {
  std::string out;
  for (unsigned char c : s) out += (c >= 32 && c < 127) ? (char)c : ' ';
  return out;
}

bool newer_than_launch(const std::string& path) {
  WIN32_FILE_ATTRIBUTE_DATA a{};
  if (!GetFileAttributesExW(widen(path).c_str(), GetFileExInfoStandard, &a)) return false;
  return CompareFileTime(&a.ftLastWriteTime, &g_launch_time) > 0;
}

std::string first_line(const std::vector<uint8_t>& text) {
  std::string s(text.begin(), text.end());
  const size_t nl = s.find_first_of("\r\n");
  s = s.substr(0, nl == std::string::npos ? s.size() : nl);
  return s.substr(0, 200);
}

std::string url_encode(const std::string& s);

bool post(const std::vector<uint8_t>& zip, const std::string& engine, const std::string& where,
          const char* kind = "crash", const std::string& note = std::string()) {
  if (!*kCrashRelayUrl) return false;
  URL_COMPONENTS parts{}; parts.dwStructSize = sizeof parts;
  wchar_t host[256]{}, path[512]{};
  parts.lpszHostName = host; parts.dwHostNameLength = 256; parts.lpszUrlPath = path; parts.dwUrlPathLength = 512;
  if (!WinHttpCrackUrl(kCrashRelayUrl, 0, 0, &parts)) return false;
  HINTERNET session = WinHttpOpen(L"MeleeUnlocked-CrashReport", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
  if (!session) return false;
  bool ok = false;
  if (HINTERNET connect = WinHttpConnect(session, host, parts.nPort, 0)) {
    if (HINTERNET req = WinHttpOpenRequest(connect, L"POST", path, nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE)) {
      const std::wstring headers = L"Content-Type: application/zip\r\nX-MU-Version: " + widen(MELEE_PORT_VERSION) +
                                   L"\r\nX-MU-Engine: " + widen(engine) + L"\r\nX-MU-Crash: " + widen(url_safe_header(where)) +
                                   L"\r\nX-MU-Kind: " + widen(kind) + L"\r\nX-MU-Note: " + widen(url_encode(note.substr(0, 2000))) + L"\r\n";
      if (WinHttpSendRequest(req, headers.c_str(), (DWORD)-1, (LPVOID)zip.data(), (DWORD)zip.size(), (DWORD)zip.size(), 0) &&
          WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, len = sizeof status;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, nullptr);
        ok = status == 200;
      }
      WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(connect);
  }
  WinHttpCloseHandle(session);
  return ok;
}

std::string url_encode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

// Called whenever the game has exited, whatever its exit code: a crash can end in a clean-looking
// exit, so the crash file being newer than the launch is what counts. Returns true when a crash
// was handled (offered or sent).
bool offer(HWND owner, const std::string& dir, const std::string& engine) {
  if (!newer_than_launch(dir + "\\melee_port_crash.txt")) return false;
  // Built before asking, so the list the player reads is exactly what the zip holds.
  auto files = launcher::crash::collect(dir, g_dir);
  const auto zip = launcher::crash::capped_zip(files);
  // Always save a readable report locally, including when the player declines transmission.
  const std::string markdown_path = dir + "\\melee_crash_report.md";
  const auto markdown = launcher::crash::make_markdown(files, MELEE_PORT_VERSION, engine);
  const bool markdown_saved = launcher::crash::save_report(markdown_path, markdown.data(), markdown.size());
  // Automated launcher runs keep the local copy as evidence but never ask and never send: a test
  // crash must not open a box nobody answers, or reach the real relay.
  if (g_launcher_test) return true;
  const std::string where = !files.empty() && files[0].first == "melee_port_crash.txt" ? first_line(files[0].second) : std::string();
  std::string list;
  for (const auto& f : files) list += "  " + f.first + "\n";
  // One template per language, so a translation can place the file list where its grammar wants it.
  const std::string question = launcher::lang::tr("crash.offer", {{"files", list}});
  if (MessageBoxW(owner, widen(question).c_str(), L"Send crash report", MB_YESNO | MB_ICONQUESTION) != IDYES) return true;
  const std::string zip_path = dir + "\\melee_crash_report.zip";
  const bool zip_saved = launcher::crash::save_report(zip_path, (const char*)zip.data(), zip.size());
  if (post(zip, engine, where)) {
    MessageBoxW(owner, markdown_saved ?
        L"Crash report sent. A readable copy is saved as melee_crash_report.md beside the game; you can attach it to your assistant." :
        L"Crash report sent. A readable copy could not be saved in the game folder.",
        L"Send crash report", MB_OK | MB_ICONINFORMATION);
    return true;
  }
  // Fallback: the zip in its folder, and a prefilled issue the player attaches it to.
  if (markdown_saved || zip_saved) {
    const std::wstring select = L"/select,\"" + widen(markdown_saved ? markdown_path : zip_path) + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", select.c_str(), nullptr, SW_SHOWNORMAL);
  }
  const std::string body = "Version: " + std::string(MELEE_PORT_VERSION) + "\nGame Build: " + engine + "\nError: " + where +
      (markdown_saved ? std::string("\n\nPlease attach melee_crash_report.md from the folder that opened.") +
                       (zip_saved ? " The companion ZIP contains the same sanitized diagnostic text." : "") :
       zip_saved ? "\n\nPlease attach melee_crash_report.zip from the folder that opened. The readable copy could not be saved." :
                   "\n\nThe report files could not be saved in the game folder. Please copy the error above into the issue.");
  const std::string issue = "https://github.com/Hero88go/melee-unlocked/issues/new?title=" + url_encode("Crash: " + where.substr(0, 80)) +
                            "&body=" + url_encode(body);
  ShellExecuteW(nullptr, L"open", widen(issue).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  MessageBoxW(owner, markdown_saved ?
      (zip_saved ? L"The report could not be sent automatically. A GitHub issue page and the folder with melee_crash_report.md are open. Attach the Markdown report to the issue or your assistant; the ZIP contains sanitized diagnostic text." :
                   L"The report could not be sent automatically. A GitHub issue page and the folder with melee_crash_report.md are open. Attach the Markdown report to the issue or your assistant. The ZIP could not be saved.") :
      zip_saved ? L"The report could not be sent automatically. A GitHub issue page and the folder with melee_crash_report.zip are open. The readable copy could not be saved; attach the ZIP to the issue." :
      L"The report could not be sent or saved in the game folder. A GitHub issue page is open with the error. The original crash files are still in the game folder.",
              L"Send crash report", MB_OK | MB_ICONINFORMATION);
  return true;
}

// "Send logs": a small window with a box for what happened. Nothing is collected or sent until
// the player presses Send; the note goes through the relay's privacy scrub like the logs.
namespace send_logs_ui {
enum { ID_NOTE = 1, ID_SEND = 2, ID_CANCEL = 3 };
struct State { bool done = false, send = false; std::wstring note; };
LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
  auto* s = (State*)GetWindowLongPtrW(w, GWLP_USERDATA);
  switch (m) {
    case WM_CREATE: {
      s = (State*)((CREATESTRUCTW*)lp)->lpCreateParams;
      SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)s);
      HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
      auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int cx, int cy, int id) {
        HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, cx, cy, w, (HMENU)(INT_PTR)id, nullptr, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
        return h;
      };
      make(L"STATIC", L"What happened? (character, stage, what you saw, roughly when)", 0, 12, 10, 456, 18, 0);
      HWND edit = make(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_TABSTOP,
                       12, 32, 456, 120, ID_NOTE);
      SendMessageW(edit, EM_SETLIMITTEXT, 2000, 0);
      make(L"STATIC", L"Sends your game log, lobby log, this session's crash file and your newest online session trace. "
                      L"Names, accounts, addresses and folder names are removed before anything leaves your PC.",
           0, 12, 160, 456, 34, 0);
      make(L"BUTTON", L"Send", BS_DEFPUSHBUTTON | WS_TABSTOP, 296, 202, 82, 28, ID_SEND);
      make(L"BUTTON", L"Cancel", WS_TABSTOP, 386, 202, 82, 28, ID_CANCEL);
      SetFocus(edit);
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(wp) == ID_SEND || LOWORD(wp) == ID_CANCEL) {
        s->send = LOWORD(wp) == ID_SEND;
        if (s->send) {
          HWND edit = GetDlgItem(w, ID_NOTE);
          std::wstring text(GetWindowTextLengthW(edit) + 1, L'\0');
          text.resize(GetWindowTextW(edit, text.data(), (int)text.size()));
          s->note = text;
        }
        DestroyWindow(w);
      }
      return 0;
    case WM_CLOSE: DestroyWindow(w); return 0;
    case WM_DESTROY: if (s) s->done = true; return 0;
  }
  return DefWindowProcW(w, m, wp, lp);
}
}  // namespace send_logs_ui

void send_logs(HWND owner, const std::string& dir, const std::string& engine, const std::vector<std::string>& replay_dirs) {
  static bool registered = false;
  if (!registered) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = send_logs_ui::proc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MeleeUnlockedSendLogs"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);
    registered = true;
  }
  send_logs_ui::State state;
  RECT r{0, 0, 480, 242};
  AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);
  RECT o{};
  GetWindowRect(owner, &o);
  HWND w = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MeleeUnlockedSendLogs", L"Send logs", WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                           o.left + 60, o.top + 60, r.right - r.left, r.bottom - r.top, owner, nullptr, GetModuleHandleW(nullptr), &state);
  if (!w) return;
  EnableWindow(owner, FALSE);
  MSG msg;
  while (!state.done && GetMessageW(&msg, nullptr, 0, 0)) {
    if (!IsDialogMessageW(w, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }
  EnableWindow(owner, TRUE);
  SetForegroundWindow(owner);
  if (!state.send) return;
  // Only a crash from this session's launch counts; an old crash file would mislead the report.
  auto files = launcher::crash::collect_logs(dir, g_dir, replay_dirs, newer_than_launch(dir + "\\melee_port_crash.txt"));
  if (files.empty()) {
    MessageBoxW(owner, L"No logs were found yet. Play once, then send.", L"Send logs", MB_OK | MB_ICONINFORMATION);
    return;
  }
  const auto zip = launcher::crash::capped_zip(files);
  const std::string where = files[0].first == "melee_port_crash.txt" ? first_line(files[0].second) : std::string();
  const bool sent = post(zip, engine, where, "logs", narrow(state.note));
  MessageBoxW(owner, sent ? L"Logs sent. Thank you." :
                            L"The logs could not be sent (no connection, or sent too recently: wait two minutes and try again).",
              L"Send logs", MB_OK | (sent ? MB_ICONINFORMATION : MB_ICONWARNING));
}

}  // namespace crash_report
