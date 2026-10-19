// Linux build: the synchronous WinHTTP subset the host and launcher use (crash reports, the updater,
// the lobby), over libcurl. A request is performed in full at WinHttpReceiveResponse; ReadData then
// hands back the buffered body. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "windows.h"

typedef void* HINTERNET;
typedef WORD INTERNET_PORT;
#define INTERNET_DEFAULT_HTTP_PORT 80
#define INTERNET_DEFAULT_HTTPS_PORT 443
#define INTERNET_SCHEME_HTTP 1
#define INTERNET_SCHEME_HTTPS 2
typedef int INTERNET_SCHEME;
#define WINHTTP_ACCESS_TYPE_DEFAULT_PROXY 0u
#define WINHTTP_ACCESS_TYPE_NO_PROXY 1u
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4u
#define WINHTTP_NO_PROXY_NAME nullptr
#define WINHTTP_NO_PROXY_BYPASS nullptr
#define WINHTTP_NO_REFERER nullptr
#define WINHTTP_DEFAULT_ACCEPT_TYPES nullptr
#define WINHTTP_NO_ADDITIONAL_HEADERS nullptr
#define WINHTTP_NO_REQUEST_DATA nullptr
#define WINHTTP_HEADER_NAME_BY_INDEX nullptr
#define WINHTTP_NO_HEADER_INDEX nullptr
#define WINHTTP_FLAG_SECURE 0x00800000u
#define WINHTTP_QUERY_STATUS_CODE 19u
#define WINHTTP_QUERY_CONTENT_LENGTH 5u
#define WINHTTP_QUERY_FLAG_NUMBER 0x20000000u
#define WINHTTP_OPTION_REDIRECT_POLICY 88u
#define WINHTTP_OPTION_REDIRECT_POLICY_NEVER 0u
#define WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP 1u
#define WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS 2u
#define WINHTTP_ADDREQ_FLAG_ADD 0x20000000u
#define WINHTTP_ADDREQ_FLAG_REPLACE 0x80000000u

typedef struct _URL_COMPONENTS {
  DWORD dwStructSize;
  LPWSTR lpszScheme; DWORD dwSchemeLength;
  INTERNET_SCHEME nScheme;
  LPWSTR lpszHostName; DWORD dwHostNameLength;
  INTERNET_PORT nPort;
  LPWSTR lpszUserName; DWORD dwUserNameLength;
  LPWSTR lpszPassword; DWORD dwPasswordLength;
  LPWSTR lpszUrlPath; DWORD dwUrlPathLength;
  LPWSTR lpszExtraInfo; DWORD dwExtraInfoLength;
} URL_COMPONENTS;

HINTERNET WinHttpOpen(LPCWSTR agent, DWORD access, LPCWSTR proxy, LPCWSTR bypass, DWORD flags);
HINTERNET WinHttpConnect(HINTERNET session, LPCWSTR host, INTERNET_PORT port, DWORD reserved);
HINTERNET WinHttpOpenRequest(HINTERNET connect, LPCWSTR verb, LPCWSTR path, LPCWSTR version, LPCWSTR referrer,
                             LPCWSTR* accept, DWORD flags);
BOOL WinHttpSetTimeouts(HINTERNET handle, int resolve, int connect, int send, int receive);
BOOL WinHttpSetOption(HINTERNET handle, DWORD option, LPVOID buffer, DWORD length);
BOOL WinHttpAddRequestHeaders(HINTERNET request, LPCWSTR headers, DWORD length, DWORD modifiers);
BOOL WinHttpSendRequest(HINTERNET request, LPCWSTR headers, DWORD headers_length, LPVOID optional,
                        DWORD optional_length, DWORD total_length, DWORD_PTR context);
BOOL WinHttpReceiveResponse(HINTERNET request, LPVOID reserved);
BOOL WinHttpQueryHeaders(HINTERNET request, DWORD level, LPCWSTR name, LPVOID buffer, LPDWORD length, LPDWORD index);
BOOL WinHttpQueryDataAvailable(HINTERNET request, LPDWORD available);
BOOL WinHttpReadData(HINTERNET request, LPVOID buffer, DWORD size, LPDWORD read);
BOOL WinHttpCloseHandle(HINTERNET handle);
BOOL WinHttpCrackUrl(LPCWSTR url, DWORD length, DWORD flags, URL_COMPONENTS* parts);
