// The launcher's copy of the game's Slippi profile refresh (slippi_net.cpp User::RefreshFromServer):
// Slippi Launcher rewrites user.json only at login, so a name changed on slippi.gg since then is stale
// in it, while Slippi Dolphin and the game show the server's (GitHub #139).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "slippi_profile_fetch.h"
#include <windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <cctype>
#include <string>

namespace launcher::slippi_profile {

bool fetch(const std::string& uid, std::string* name, std::string* code) {
  if (uid.empty() || uid.size() > 128) return false;
  for (char c : uid) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return false;
  HINTERNET s = WinHttpOpen(L"MeleeUnlocked launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s) return false;
  WinHttpSetTimeouts(s, 5000, 5000, 8000, 8000);
  HINTERNET c = WinHttpConnect(s, L"users-rest-dot-slippi.uc.r.appspot.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
  const std::wstring path = L"/user/" + std::wstring(uid.begin(), uid.end());
  HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
  std::string body;
  DWORD status = 0, len = sizeof status;
  if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr) &&
      WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX) &&
      status == 200) {
    char buf[8192];
    for (DWORD got = 0; WinHttpReadData(r, buf, sizeof buf, &got) && got; ) {
      body.append(buf, got);
      if (body.size() > 256 * 1024) { body.clear(); break; }
    }
  }
  if (r) WinHttpCloseHandle(r);
  if (c) WinHttpCloseHandle(c);
  WinHttpCloseHandle(s);
  const nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
  if (!j.is_object()) return false;
  const auto text = [&](const char* k) -> std::string {
    const auto it = j.find(k);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
  };
  *name = text("displayName");
  *code = text("connectCode");
  return !name->empty() || !code->empty();
}

}  // namespace launcher::slippi_profile
