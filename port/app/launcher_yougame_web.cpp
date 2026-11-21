// SPDX-License-Identifier: GPL-2.0-or-later
#include "launcher_yougame_web.h"
#if defined(_WIN32) && defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <ole2.h>
#include <wrl.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "WebView2.h"
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <urlmon.h>
#include <shlobj.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using json = nlohmann::json;

namespace launcher::yougame::web {
namespace {

// A page of the site with little on it: the view sits here between requests.
constexpr const char* kRestPage = "/terms";
// The sign-in form alone (no site header or menus); once signed in it stays put and the check below closes the window.
constexpr const char* kSignInPage = "/login?bare=1";
// The view (and its browser processes) ends after this long unused. Long enough that a play session's
// requests (searches, ticket renewals) find it open: starting it costs a second or two of processor
// time, which a match in progress should not pay; resting on its page it costs none.
constexpr uint64_t kIdleCloseMs = 30 * 60000;
constexpr UINT WM_JOB = WM_APP + 1;
constexpr UINT WM_RAISE = WM_APP + 2;   // the sign-in window comes to the front (Connect pressed again)
// The sign-in window's own colour and words until YouGame's page has drawn: never a black window.
constexpr COLORREF kLoadingBack = RGB(244, 245, 247), kLoadingText = RGB(70, 74, 82);

std::wstring wide(const std::string& s) {
  if (s.empty()) return {};
  std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), (int)w.size());
  return w;
}
std::string narrow(const wchar_t* w) {
  if (!w || !*w) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n - 1 : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}
// "https://host[:port]" of a URL, "" when it has none.
std::string origin_of(const std::string& url) {
  const size_t scheme = url.find("://");
  if (scheme == std::string::npos) return {};
  const size_t end = url.find_first_of("/?#", scheme + 3);
  return url.substr(0, end);
}

struct Job {
  enum Kind { kRequest, kSignIn, kClose, kSignOut } kind = kRequest;
  std::string site, method, path, body, script;
  std::wstring title;
  HWND owner = nullptr;
  uint64_t deadline = 0;        // GetTickCount64; 0 = none
  std::promise<Reply> done;     // sign-in: status 200 signed in, else body says why not
  bool answered = false;
  void answer(int status, std::string body) {
    if (answered) return;
    answered = true;
    done.set_value(Reply{status, std::move(body)});
  }
  ~Job() { answer(0, "The browser view closed."); }
};

std::mutex g_mutex;              // the folder, and starting the thread

// The runtime folder handed to WebView2's loader. Empty means the loader finds the runtime itself, from
// EdgeUpdate's registry record. That record can name a version folder that is gone while a newer one sits
// beside it (seen on fresh Windows Sandbox images, even right after Microsoft's installer ran), so when the
// loader finds nothing, the newest version folder on disk that holds the runtime is passed in instead.
std::mutex g_browser_mutex;
std::wstring g_browser_folder;

std::wstring browser_folder() {
  std::lock_guard<std::mutex> lock(g_browser_mutex);
  return g_browser_folder;
}

std::wstring known_folder(REFKNOWNFOLDERID id) {
  PWSTR path = nullptr;
  std::wstring out;
  if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path) out = path;
  CoTaskMemFree(path);
  return out;
}

// "157.0.4341.60" -> {157, 0, 4341, 60}; anything else -> empty.
std::vector<unsigned> version_parts(const std::wstring& name) {
  std::vector<unsigned> parts;
  std::wstring cur;
  for (wchar_t c : name + L".") {
    if (c == L'.') {
      if (cur.empty() || cur.size() > 6) return {};
      parts.push_back((unsigned)std::wcstoul(cur.c_str(), nullptr, 10));
      cur.clear();
    } else if (c >= L'0' && c <= L'9') {
      cur += c;
    } else {
      return {};
    }
  }
  return parts.size() == 4 ? parts : std::vector<unsigned>{};
}

// The newest <base>\Microsoft\EdgeWebView\Application\<version> folder that holds both the browser and
// this architecture's runtime dll, per-machine install first, then per-user. Empty when there is none.
std::wstring newest_runtime_folder() {
#if defined(_M_ARM64) || defined(__aarch64__)
  const wchar_t* arch = L"arm64";
#elif defined(_WIN64)
  const wchar_t* arch = L"x64";
#else
  const wchar_t* arch = L"x86";
#endif
  for (REFKNOWNFOLDERID id : {FOLDERID_ProgramFilesX86, FOLDERID_LocalAppData}) {
    const std::wstring base = known_folder(id);
    if (base.empty()) continue;
    const std::wstring app = base + L"\\Microsoft\\EdgeWebView\\Application\\";
    std::vector<unsigned> best;
    std::wstring best_dir;
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileW((app + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) continue;
    do {
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
      const std::vector<unsigned> parts = version_parts(fd.cFileName);
      if (parts.empty() || (!best_dir.empty() && parts <= best)) continue;
      const std::wstring dir = app + fd.cFileName;
      const std::wstring dll = dir + L"\\EBWebView\\" + arch + L"\\EmbeddedBrowserWebView.dll";
      if (GetFileAttributesW((dir + L"\\msedgewebview2.exe").c_str()) == INVALID_FILE_ATTRIBUTES ||
          GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES)
        continue;
      best = parts;
      best_dir = dir;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!best_dir.empty()) return best_dir;
  }
  return L"";
}
std::wstring g_folder;
std::atomic<HWND> g_window{nullptr};
bool g_thread_started = false;

// ---- everything below runs on the view's own thread

struct Engine {
  ComPtr<ICoreWebView2Controller> controller;
  ComPtr<ICoreWebView2> view;
  bool creating = false, navigating = false, ready = false;
  bool page_shown = false;                            // the sign-in window shows the page (else "Loading YouGame...")
  std::string site;                                   // the site the view is (or is going) on
  std::vector<std::unique_ptr<Job>> waiting;          // requests until the page is ready
  std::map<int, std::unique_ptr<Job>> flying;         // requests the page is making
  std::unique_ptr<Job> signin;
  int signin_check = 0;                               // the id of the sign-in check in flight, 0 none
  int next_id = 1;
  uint64_t last_use = 0, last_check = 0;
};
Engine* g = nullptr;

std::string current_origin() {
  if (!g->view) return {};
  LPWSTR source = nullptr;
  if (FAILED(g->view->get_Source(&source))) return {};
  const std::string s = narrow(source);
  CoTaskMemFree(source);
  return origin_of(s);
}

void navigate(const std::string& url) {
  g->navigating = true;
  g->ready = false;
  if (FAILED(g->view->Navigate(wide(url).c_str()))) g->navigating = false;
}

// The page asks the site and posts the answer back: {id, status, body}.
void ask(int id, const std::string& method, const std::string& path, const std::string& body) {
  json options{{"method", method}, {"credentials", "same-origin"}, {"cache", "no-store"}, {"headers", {{"Accept", "application/json"}}}};
  if (!body.empty()) {
    options["body"] = body;
    options["headers"]["Content-Type"] = "application/json";
  }
  const std::string script = "(function(){var id=" + std::to_string(id) + ";fetch(" + json(path).dump() + "," + options.dump() +
      ").then(function(r){return r.text().then(function(t){return{id:id,status:r.status,body:t};});})"
      ".catch(function(e){return{id:id,status:0,body:String(e)};})"
      ".then(function(m){window.chrome.webview.postMessage(JSON.stringify(m));});})()";
  g->view->ExecuteScript(wide(script).c_str(), nullptr);
}

// The sign-in window shows YouGame's page once a page of it has loaded; until then it draws its own
// light "Loading YouGame..." (WM_PAINT), so a slow start never shows a black window.
void show_page(bool on) {
  if (g->page_shown == on) return;
  g->page_shown = on;
  if (g->controller) g->controller->put_IsVisible(on ? TRUE : FALSE);
  InvalidateRect(g_window, nullptr, TRUE);
}

void destroy_view() {
  if (g->controller) g->controller->Close();
  g->controller.Reset();
  g->view.Reset();
  g->page_shown = false;
  g->creating = g->navigating = g->ready = false;
  g->signin_check = 0;
  g->flying.clear();    // each answers "closed"
}

void finish_sign_in(bool ok, const std::string& why) {
  if (!g->signin) return;
  std::unique_ptr<Job> job = std::move(g->signin);
  g->signin_check = 0;
  ShowWindow(g_window, SW_HIDE);
  show_page(false);
  job->answer(ok ? 200 : 0, why);
  // Off the sign-in page (and whatever the site opened after it): rest on the light page.
  if (g->view && ok) navigate(job->site + kRestPage);
  else if (g->view) destroy_view();
}

void check_sign_in() {
  if (!g->signin || !g->view || g->signin_check || current_origin() != g->signin->site) return;
  if (!g->signin->script.empty()) g->view->ExecuteScript(wide(g->signin->script).c_str(), nullptr);
  g->signin_check = g->next_id++;
  g->last_check = GetTickCount64();
  ask(g->signin_check, "GET", g->signin->path, "");
}

void create_view();

// Moves waiting requests on: makes the view, takes it to the site, then lets the page ask.
void pump() {
  if (g->waiting.empty() && !g->signin) return;
  if (!g->view) { create_view(); return; }
  if (g->signin) {
    if (!g->navigating && current_origin().empty()) navigate(g->signin->site + kSignInPage);
    return;   // requests wait until the sign-in window is done
  }
  const std::string site = g->waiting.front()->site;
  if (!g->ready || g->site != site) {
    g->site = site;
    if (!g->navigating) navigate(site + kRestPage);
    return;
  }
  for (auto& job : g->waiting) {
    if (job->site != site) { job->answer(0, "Another YouGame site is open."); continue; }
    const int id = g->next_id++;
    ask(id, job->method, job->path, job->body);
    g->flying[id] = std::move(job);
  }
  g->waiting.clear();
  g->last_use = GetTickCount64();
}

void fail_all(const std::string& why) {
  for (auto& job : g->waiting) job->answer(0, why);
  g->waiting.clear();
  for (auto& kv : g->flying) kv.second->answer(0, why);
  g->flying.clear();
  if (g->signin) finish_sign_in(false, why);
}

void on_view(ICoreWebView2Controller* controller) {
  g->creating = false;
  g->controller = controller;
  g->controller->get_CoreWebView2(&g->view);
  RECT r;
  GetClientRect(g_window, &r);
  g->controller->put_Bounds(r);
  // Shown once the sign-in page has loaded (show_page); the window's own "Loading YouGame..." until then.
  g->page_shown = false;
  g->controller->put_IsVisible(FALSE);
  ComPtr<ICoreWebView2Controller2> controller2;
  if (SUCCEEDED(g->controller.As(&controller2)) && controller2)
    controller2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255, GetRValue(kLoadingBack), GetGValue(kLoadingBack), GetBValue(kLoadingBack)});
  ComPtr<ICoreWebView2Settings> settings;
  if (SUCCEEDED(g->view->get_Settings(&settings)) && settings) {
    settings->put_AreDevToolsEnabled(FALSE);
    settings->put_IsStatusBarEnabled(FALSE);
    settings->put_AreDefaultContextMenusEnabled(FALSE);
  }
  g->view->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
      [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) {
        BOOL ok = FALSE;
        args->get_IsSuccess(&ok);
        g->navigating = false;
        if (g->signin) {
          // The page (or the view's own page saying it could not load) is drawn now: show it.
          if (!current_origin().empty()) show_page(true);
          check_sign_in();
          return S_OK;
        }
        if (!ok || current_origin() != g->site) {
          g->ready = false;
          for (auto& job : g->waiting) job->answer(0, "YouGame's site did not open. Check your connection.");
          g->waiting.clear();
          return S_OK;
        }
        g->ready = true;
        pump();
        return S_OK;
      }).Get(), nullptr);
  // The site's own page moved on without a load (after its sign-in form): look again.
  g->view->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
      [](ICoreWebView2*, ICoreWebView2SourceChangedEventArgs*) { check_sign_in(); return S_OK; }).Get(), nullptr);
  g->view->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
      [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) {
        // Only a page of the site this view is on may answer.
        LPWSTR source = nullptr, text = nullptr;
        args->get_Source(&source);
        const std::string from = origin_of(narrow(source));
        CoTaskMemFree(source);
        const std::string site = g->signin ? g->signin->site : g->site;
        if (from.empty() || from != site || FAILED(args->TryGetWebMessageAsString(&text))) return S_OK;
        const json m = json::parse(narrow(text), nullptr, false);
        CoTaskMemFree(text);
        if (!m.is_object() || !m.count("id") || !m["id"].is_number_integer()) return S_OK;
        const int id = m["id"].get<int>();
        const int status = m.value("status", 0);
        std::string body = m.count("body") && m["body"].is_string() ? m["body"].get<std::string>() : std::string();
        if (g->signin && id == g->signin_check) {
          g->signin_check = 0;
          if (status == 200) finish_sign_in(true, body);
          return S_OK;
        }
        const auto it = g->flying.find(id);
        if (it == g->flying.end()) return S_OK;
        it->second->answer(status, std::move(body));
        g->flying.erase(it);
        g->last_use = GetTickCount64();
        return S_OK;
      }).Get(), nullptr);
  // A link that wants its own window opens in this one: nothing of the site ever leaves the view.
  g->view->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
      [](ICoreWebView2* view, ICoreWebView2NewWindowRequestedEventArgs* args) {
        LPWSTR uri = nullptr;
        args->put_Handled(TRUE);
        if (SUCCEEDED(args->get_Uri(&uri)) && uri && g->signin) view->Navigate(uri);
        CoTaskMemFree(uri);
        return S_OK;
      }).Get(), nullptr);
  g->view->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
      [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) {
        PostMessageW(g_window, WM_JOB, 1, 0);   // not inside the view's own callback
        return S_OK;
      }).Get(), nullptr);
  g->last_use = GetTickCount64();
  pump();
}

void create_view() {
  if (g->creating) return;
  std::wstring folder;
  { std::lock_guard<std::mutex> lock(g_mutex); folder = g_folder; }
  if (folder.empty()) { fail_all("No folder for YouGame's sign-in."); return; }
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  g->creating = true;
  const std::wstring browser = browser_folder();
  const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(browser.empty() ? nullptr : browser.c_str(), folder.c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [](HRESULT hr, ICoreWebView2Environment* env) {
            if (FAILED(hr) || !env) { g->creating = false; fail_all("YouGame's sign-in could not start (Microsoft Edge WebView2)."); return S_OK; }
            env->CreateCoreWebView2Controller(g_window, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [](HRESULT hr, ICoreWebView2Controller* controller) {
                  if (FAILED(hr) || !controller) { g->creating = false; fail_all("YouGame's sign-in could not start (Microsoft Edge WebView2)."); return S_OK; }
                  on_view(controller);
                  return S_OK;
                }).Get());
            return S_OK;
          }).Get());
  if (FAILED(hr)) { g->creating = false; fail_all("YouGame's sign-in could not start (Microsoft Edge WebView2)."); }
}

void take(std::unique_ptr<Job> job) {
  switch (job->kind) {
    case Job::kRequest:
      g->waiting.push_back(std::move(job));
      pump();
      break;
    case Job::kSignIn: {
      if (g->signin) { job->answer(0, "YouGame's sign-in is already open."); break; }
      // A view on another page starts over on the sign-in page.
      g->signin = std::move(job);
      g->site = g->signin->site;
      RECT area{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
      // Placed over the launcher. A minimized launcher (it minimizes while the game runs) reports -32000,-32000,
      // which put this window off screen: then where the launcher sits when restored.
      if (g->signin->owner) {
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (IsIconic(g->signin->owner) && GetWindowPlacement(g->signin->owner, &wp)) area = wp.rcNormalPosition;
        else GetWindowRect(g->signin->owner, &area);
      }
      const UINT dpi = GetDpiForWindow(g_window) ? GetDpiForWindow(g_window) : 96;
      const int w = MulDiv(520, dpi, 96), h = MulDiv(760, dpi, 96);
      SetWindowTextW(g_window, g->signin->title.c_str());
      // Never owned by the launcher's window: an owner on another thread joins the two threads' input
      // (and through the view's child windows, the browser's), so a slow browser start froze the
      // launcher with it (Windows closed it as not responding). The owner only places the window.
      show_page(false);
      SetWindowPos(g_window, HWND_TOP, (area.left + area.right - w) / 2, std::max<LONG>(0, (area.top + area.bottom - h) / 2), w, h, SWP_SHOWWINDOW);
      UpdateWindow(g_window);   // "Loading YouGame..." at once
      SetForegroundWindow(g_window);
      if (g->controller) {
        RECT r;
        GetClientRect(g_window, &r);
        g->controller->put_Bounds(r);
      }
      if (g->view) navigate(g->signin->site + kSignInPage);
      else create_view();
      break;
    }
    case Job::kSignOut: {
      // The session's cookies go first: the folder itself can stay locked a moment after the view ends.
      ComPtr<ICoreWebView2_2> view2;
      ComPtr<ICoreWebView2CookieManager> cookies;
      if (g->view && SUCCEEDED(g->view.As(&view2)) && view2 && SUCCEEDED(view2->get_CookieManager(&cookies)) && cookies)
        cookies->DeleteAllCookies();
      fail_all("Signed out.");
      destroy_view();
      job->answer(200, {});
      break;
    }
    case Job::kClose:
      fail_all("The browser view closed.");
      destroy_view();
      job->answer(200, {});
      break;
  }
}

// The window's own drawing: light, and "Loading YouGame..." until YouGame's page shows over it.
void paint_loading(HWND h, HDC dc) {
  RECT r;
  GetClientRect(h, &r);
  HBRUSH back = CreateSolidBrush(kLoadingBack);
  FillRect(dc, &r, back);
  DeleteObject(back);
  if (g && g->page_shown) return;
  NONCLIENTMETRICSW m{sizeof m};
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0);
  const UINT dpi = GetDpiForWindow(h) ? GetDpiForWindow(h) : 96;
  m.lfMessageFont.lfHeight = -MulDiv(11, dpi, 72);   // 11 pt
  HFONT font = CreateFontIndirectW(&m.lfMessageFont);
  HGDIOBJ old = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, kLoadingText);
  DrawTextW(dc, L"Loading YouGame...", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, old);
  DeleteObject(font);
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_JOB:
      if (w == 1) { fail_all("YouGame's browser view stopped."); destroy_view(); return 0; }
      take(std::unique_ptr<Job>((Job*)l));
      return 0;
    case WM_RAISE:
      if (g && g->signin && IsWindowVisible(h)) {
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
      }
      return 0;
    case WM_ERASEBKGND:
      return 1;   // WM_PAINT fills the whole window
    case WM_PRINTCLIENT:   // PrintWindow (the hidden checks' capture): the same drawing
      paint_loading(h, (HDC)w);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      paint_loading(h, dc);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_SIZE:
      if (g && g->controller) {
        RECT r;
        GetClientRect(h, &r);
        g->controller->put_Bounds(r);
      }
      return 0;
    case WM_TIMER: {
      const uint64_t now = GetTickCount64();
      for (auto it = g->flying.begin(); it != g->flying.end();)
        if (it->second->deadline && now > it->second->deadline) { it->second->answer(0, "YouGame is not answering. Check your connection."); it = g->flying.erase(it); }
        else ++it;
      for (auto it = g->waiting.begin(); it != g->waiting.end();)
        if ((*it)->deadline && now > (*it)->deadline) { (*it)->answer(0, "YouGame is not answering. Check your connection."); it = g->waiting.erase(it); }
        else ++it;
      if (g->signin) {
        // A check that never came back (the page went elsewhere meanwhile) is dropped after 10 s.
        if (g->signin_check && now - g->last_check > 10000) g->signin_check = 0;
        if (now - g->last_check > 2000) check_sign_in();
      } else if (g->view && g->waiting.empty() && g->flying.empty() && now - g->last_use > kIdleCloseMs) {
        destroy_view();
      }
      return 0;
    }
    case WM_CLOSE:
      finish_sign_in(false, "closed");
      ShowWindow(h, SW_HIDE);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

void thread_main(std::promise<void> up) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  static Engine engine;
  g = &engine;
  WNDCLASSW c{};
  c.lpfnWndProc = proc;
  c.hInstance = GetModuleHandleW(nullptr);
  c.lpszClassName = L"MeleeUnlockedYouGameWeb";
  c.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));   // IDC_ARROW, whatever UNICODE is
  c.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
  c.hbrBackground = nullptr;   // WM_PAINT: light, with "Loading YouGame..." until the page shows
  RegisterClassW(&c);
  // Its own top-level window with its own taskbar button, never owned by the launcher's (take()).
  const HWND window = CreateWindowExW(WS_EX_APPWINDOW, c.lpszClassName, L"Sign in to YouGame", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                      520, 760, nullptr, nullptr, c.hInstance, nullptr);
  g_window = window;
  SetTimer(window, 1, 1000, nullptr);
  up.set_value();
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

bool ensure_thread() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_thread_started) {
    std::promise<void> up;
    std::future<void> ready = up.get_future();
    std::thread(thread_main, std::move(up)).detach();
    ready.wait();
    g_thread_started = true;
  }
  return g_window.load() != nullptr;
}

Reply run(std::unique_ptr<Job> job, int wait_ms) {
  std::future<Reply> done = job->done.get_future();
  if (!ensure_thread()) return {0, "YouGame's sign-in could not start."};
  if (!PostMessageW(g_window, WM_JOB, 0, (LPARAM)job.get())) return {0, "YouGame's sign-in could not start."};
  job.release();   // the view's thread owns it now
  if (wait_ms >= 0 && done.wait_for(std::chrono::milliseconds(wait_ms)) != std::future_status::ready)
    return {0, "YouGame is not answering. Check your connection."};
  return done.get();
}

}  // namespace

static bool runtime_answers(const wchar_t* browser) {
  LPWSTR version = nullptr;
  const bool ok = SUCCEEDED(GetAvailableCoreWebView2BrowserVersionString(browser, &version)) && version && *version;
  CoTaskMemFree(version);
  return ok;
}
// The loader's own lookup first; when it finds nothing, the newest runtime folder on disk (browser_folder).
static bool runtime_present() {
  if (runtime_answers(nullptr)) return true;
  const std::wstring dir = newest_runtime_folder();
  if (dir.empty() || !runtime_answers(dir.c_str())) return false;
  std::lock_guard<std::mutex> lock(g_browser_mutex);
  g_browser_folder = dir;
  return true;
}
// Asked again until it answers yes: an install made while the launcher runs counts at once.
bool supported() {
  static std::atomic<bool> yes{false};
  if (!yes && runtime_present()) yes = true;
  return yes;
}

// The file's Authenticode signature is valid (WinVerifyTrust) and its signer is Microsoft Corporation.
static bool signed_by_microsoft(const std::wstring& file) {
  WINTRUST_FILE_INFO info{sizeof info};
  info.pcwszFilePath = file.c_str();
  WINTRUST_DATA data{sizeof data};
  data.dwUIChoice = WTD_UI_NONE;
  data.fdwRevocationChecks = WTD_REVOKE_NONE;
  data.dwUnionChoice = WTD_CHOICE_FILE;
  data.pFile = &info;
  data.dwStateAction = WTD_STATEACTION_VERIFY;
  GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  const LONG trust = WinVerifyTrust(nullptr, &action, &data);
  data.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust(nullptr, &action, &data);
  if (trust != ERROR_SUCCESS) return false;
  HCERTSTORE store = nullptr;
  HCRYPTMSG msg = nullptr;
  if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, file.c_str(), CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                        CERT_QUERY_FORMAT_FLAG_BINARY, 0, nullptr, nullptr, nullptr, &store, &msg, nullptr))
    return false;
  bool microsoft = false;
  DWORD size = 0;
  if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &size) && size) {
    std::vector<BYTE> buf(size);
    if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, buf.data(), &size)) {
      const auto* signer = reinterpret_cast<CMSG_SIGNER_INFO*>(buf.data());
      CERT_INFO ci{};
      ci.Issuer = signer->Issuer;
      ci.SerialNumber = signer->SerialNumber;
      if (PCCERT_CONTEXT cert = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                                                           CERT_FIND_SUBJECT_CERT, &ci, nullptr)) {
        wchar_t name[256] = {};
        CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
        microsoft = std::wstring(name) == L"Microsoft Corporation";
        CertFreeCertificateContext(cert);
      }
    }
  }
  CryptMsgClose(msg);
  CertCloseStore(store, 0);
  return microsoft;
}

bool install_runtime(std::string* why) {
  const auto fail = [&](const char* step) { if (why) *why = step; return false; };
  if (supported()) return true;
  wchar_t tmp[MAX_PATH] = {};
  if (!GetTempPathW(MAX_PATH, tmp)) return fail("no temporary folder");
  const std::wstring file = std::wstring(tmp) + L"MeleeUnlocked-MicrosoftEdgeWebview2Setup.exe";
  DeleteFileW(file.c_str());
  // Microsoft's own link to the WebView2 Evergreen bootstrapper (learn.microsoft.com, "Download the WebView2 Runtime").
  if (FAILED(URLDownloadToFileW(nullptr, L"https://go.microsoft.com/fwlink/p/?LinkId=2124703", file.c_str(), 0, nullptr)))
    return fail("the download from Microsoft failed");
  if (!signed_by_microsoft(file)) { DeleteFileW(file.c_str()); return fail("the downloaded installer is not signed by Microsoft"); }
  std::wstring cmd = L"\"" + file + L"\" /silent /install";
  STARTUPINFOW si{sizeof si};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    DeleteFileW(file.c_str());
    return fail("the installer did not start");
  }
  WaitForSingleObject(pi.hProcess, 5 * 60 * 1000);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  DeleteFileW(file.c_str());
  for (int i = 0; i < 20 && !supported(); ++i) Sleep(500);
  return supported() || fail("the installer finished but WebView2 is still missing");
}

void set_folder(const std::wstring& folder) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_folder = folder;
}

bool remembered() {
  std::wstring folder;
  { std::lock_guard<std::mutex> lock(g_mutex); folder = g_folder; }
  std::error_code ec;
  return !folder.empty() && std::filesystem::is_directory(folder, ec);
}

bool sign_in(void* owner, const std::string& site, const std::string& signed_in_path, std::string* error,
             const std::string& test_script, const std::wstring& title) {
  if (!supported()) {
    if (error) *error = "Signing in to YouGame needs Microsoft Edge WebView2, which this computer does not have.";
    return false;
  }
  auto job = std::make_unique<Job>();
  job->kind = Job::kSignIn;
  job->site = site;
  job->path = signed_in_path;
  job->owner = (HWND)owner;
  job->script = test_script;
  job->title = title;
  const Reply r = run(std::move(job), -1);
  if (r.status == 200) return true;
  if (error) *error = r.body == "closed" ? std::string() : r.body;
  return false;
}

void raise_sign_in() {
  { std::lock_guard<std::mutex> lock(g_mutex); if (!g_thread_started) return; }
  if (const HWND w = g_window.load()) PostMessageW(w, WM_RAISE, 0, 0);
}

Reply request(const std::string& site, const std::string& method, const std::string& path, const std::string& body,
              int timeout_ms) {
  if (!supported()) return {0, "Microsoft Edge WebView2 is not on this computer."};
  auto job = std::make_unique<Job>();
  job->kind = Job::kRequest;
  job->site = site;
  job->method = method;
  job->path = path;
  job->body = body;
  job->deadline = GetTickCount64() + (uint64_t)timeout_ms;
  return run(std::move(job), timeout_ms + 3000);
}

void close() {
  { std::lock_guard<std::mutex> lock(g_mutex); if (!g_thread_started) return; }
  auto job = std::make_unique<Job>();
  job->kind = Job::kClose;
  run(std::move(job), 5000);
}

void sign_out() {
  bool started;
  std::wstring folder;
  { std::lock_guard<std::mutex> lock(g_mutex); started = g_thread_started; folder = g_folder; }
  if (started) {
    auto job = std::make_unique<Job>();
    job->kind = Job::kSignOut;
    run(std::move(job), 5000);
  }
  if (folder.empty()) return;
  // The browser processes let go of the folder a moment after the view ends.
  for (int i = 0; i < 40; ++i) {
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    if (!ec && !std::filesystem::exists(folder, ec)) return;
    Sleep(250);
  }
}

}  // namespace launcher::yougame::web
#endif
