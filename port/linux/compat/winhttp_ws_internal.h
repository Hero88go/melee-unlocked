// Linux WinHTTP's private WebSocket bridge. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "winhttp.h"
#include <string>
#include <vector>

BOOL winhttp_ws_open(const std::string& host, INTERNET_PORT port, bool secure, const std::string& path,
                    const std::vector<std::string>& headers, int connect_ms, int receive_ms, long& status, HINTERNET& socket);
bool winhttp_ws_close(HINTERNET socket);
bool winhttp_ws_timeout(HINTERNET socket, DWORD timeout);
