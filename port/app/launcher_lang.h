// Launcher text in the player's language.
//
// English is compiled in (launcher_lang_en.inl) and is what shows for any line a language file does
// not have. Other languages are plain UTF-8 files, lang/<code>.txt next to the launcher: one
// "key = value" per line, # starts a comment, \n in a value is a line break. {name} placeholders
// are filled in by name, so a translation can put the parts of a sentence in its own order.
//
// Header-only, so the launcher, the lobby and their tests all get it without extra build steps.
// Thread-safe: the lobby worker thread asks for text while the window thread may switch language.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fstream>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace launcher::lang {

struct Language {
  const char* code;          // file name in lang/, and the launcher.ini value
  const wchar_t* native;     // how the picker names it, in that language
  const wchar_t* face;       // a font that shows the script, when the system one may not ("" keeps it)
  const wchar_t* locale;     // for dates and times
};
inline const std::vector<Language>& languages() {
  static const std::vector<Language> list = {
    {"en", L"English", L"", L""},
    {"es", L"Español", L"", L"es-ES"},
    {"pt-BR", L"Português (Brasil)", L"", L"pt-BR"},
    {"fr", L"Français", L"", L"fr-FR"},
    {"de", L"Deutsch", L"", L"de-DE"},
    {"ru", L"Русский", L"", L"ru-RU"},
    {"ja", L"日本語", L"Yu Gothic UI", L"ja-JP"},
    {"zh-Hans", L"简体中文", L"Microsoft YaHei UI", L"zh-CN"},
    {"ko", L"한국어", L"Malgun Gothic", L"ko-KR"},
  };
  return list;
}

struct Entry { const char* key; const char* text; };
inline const Entry english_entries[] = {
#include "launcher_lang_en.inl"
};

using Table = std::map<std::string, std::string>;
using Args = std::initializer_list<std::pair<const char*, std::string>>;

namespace detail {
struct State {
  std::mutex mutex;
  std::string code = "en";
  std::shared_ptr<const Table> table;   // null while English is chosen
};
inline State& state() { static State s; return s; }
inline const Table& english() {
  static const Table table = [] {
    Table t;
    for (const auto& e : english_entries) t.emplace(e.key, e.text);
    return t;
  }();
  return table;
}
inline std::string trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t");
  if (a == std::string::npos) return {};
  const size_t b = s.find_last_not_of(" \t");
  return s.substr(a, b - a + 1);
}
inline std::string unescape(const std::string& s) {
  std::string out; out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char n = s[i + 1];
      if (n == 'n') { out += '\n'; ++i; continue; }
      if (n == 't') { out += '\t'; ++i; continue; }
      if (n == '\\') { out += '\\'; ++i; continue; }
    }
    out += s[i];
  }
  return out;
}
}  // namespace detail

// Replaces {name} with the argument called name, in one pass: text that came from another player
// (a name like "{code}") is never read as a placeholder itself.
inline std::string fill(const std::string& text, const std::vector<std::pair<std::string, std::string>>& args) {
  std::string out; out.reserve(text.size() + 32);
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '{') {
      const size_t close = text.find('}', i + 1);
      if (close != std::string::npos && close - i <= 32) {
        const std::string name = text.substr(i + 1, close - i - 1);
        bool found = false;
        for (const auto& a : args) if (a.first == name) { out += a.second; found = true; break; }
        if (found) { i = close + 1; continue; }
      }
    }
    out += text[i++];
  }
  return out;
}
inline std::string fill(const std::string& text, Args args) {
  std::vector<std::pair<std::string, std::string>> list;
  for (const auto& a : args) list.emplace_back(a.first, a.second);
  return fill(text, list);
}

// English, whatever language is picked: for text that goes to other players or into logs.
inline std::string en(const char* key) {
  const auto& t = detail::english();
  const auto it = t.find(key);
  return it == t.end() ? std::string(key) : it->second;
}
inline std::string en(const char* key, Args args) { return fill(en(key), args); }
inline bool has_key(const std::string& key) { return detail::english().count(key) != 0; }

inline std::string tr(const char* key) {
  std::shared_ptr<const Table> table;
  { std::lock_guard<std::mutex> lock(detail::state().mutex); table = detail::state().table; }
  if (table) {
    const auto it = table->find(key);
    if (it != table->end() && !it->second.empty()) return it->second;
  }
  return en(key);
}
inline std::string tr(const std::string& key) { return tr(key.c_str()); }
inline std::string tr(const char* key, Args args) { return fill(tr(key), args); }
inline std::string tr(const std::string& key, Args args) { return fill(tr(key.c_str()), args); }
// A line that may not exist in the table (a reason code from a newer launcher): the fallback then.
inline std::string tr_or(const std::string& key, const std::string& fallback) {
  return has_key(key) ? tr(key) : fallback;
}

inline std::wstring wide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
inline std::string utf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
// Fixed launcher text is translated by its English wording: language files hold "@English = ..."
// lines. Text with no such line (or English chosen) comes back unchanged, so nothing can go blank.
inline std::string tx(const std::string& english) {
  if (english.empty()) return english;
  std::shared_ptr<const Table> table;
  { std::lock_guard<std::mutex> lock(detail::state().mutex); table = detail::state().table; }
  if (!table) return english;
  const auto it = table->find("@" + english);
  return it != table->end() && !it->second.empty() ? it->second : english;
}
inline std::wstring txw(const std::wstring& english) {
  if (english.empty()) return english;
  return wide(tx(utf8(english)));
}
inline std::wstring trw(const char* key) { return wide(tr(key)); }
inline std::wstring trw(const char* key, Args args) { return wide(tr(key, args)); }

// Reads lang/<code>.txt. Unknown keys are kept (harmless); a missing file leaves the table empty.
inline bool read_file(const std::wstring& path, Table& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
        (unsigned char)line[2] == 0xBF) line.erase(0, 3);
    first = false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string trimmed = detail::trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;
    const size_t eq = trimmed.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = detail::unescape(detail::trim(trimmed.substr(0, eq)));
    const std::string value = detail::unescape(detail::trim(trimmed.substr(eq + 1)));
    if (!key.empty()) out[key] = value;
  }
  return true;
}

inline const Language* find(const std::string& code) {
  for (const auto& l : languages()) if (code == l.code) return &l;
  return nullptr;
}

// Switches every later tr() to `code`, reading lang/<code>.txt from the first folder that has it.
// Returns false (and keeps English) when the language is unknown or its file is missing.
inline bool set_language(const std::string& code, const std::vector<std::wstring>& folders) {
  auto& s = detail::state();
  if (!find(code) || code == "en") {
    std::lock_guard<std::mutex> lock(s.mutex);
    s.code = "en"; s.table.reset();
    return code == "en";
  }
  auto table = std::make_shared<Table>();
  bool loaded = false;
  for (const auto& folder : folders) {
    if (folder.empty()) continue;
    if (read_file(folder + L"\\" + wide(code) + L".txt", *table)) { loaded = true; break; }
  }
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!loaded) { s.code = "en"; s.table.reset(); return false; }
  s.code = code; s.table = std::move(table);
  return true;
}
inline std::string current() {
  std::lock_guard<std::mutex> lock(detail::state().mutex);
  return detail::state().code;
}

// The language the launcher starts in when launcher.ini has none: Windows' display language, when
// there is a translation for it.
inline std::string system_default() {
  const LANGID id = GetUserDefaultUILanguage();
  switch (PRIMARYLANGID(id)) {
    case LANG_SPANISH: return "es";
    case LANG_PORTUGUESE: return "pt-BR";
    case LANG_FRENCH: return "fr";
    case LANG_GERMAN: return "de";
    case LANG_RUSSIAN: return "ru";
    case LANG_JAPANESE: return "ja";
    case LANG_CHINESE: return "zh-Hans";
    case LANG_KOREAN: return "ko";
    default: return "en";
  }
}

inline bool font_installed(const std::wstring& face) {
  static std::mutex mutex;
  static std::map<std::wstring, bool> known;
  std::lock_guard<std::mutex> lock(mutex);
  const auto it = known.find(face);
  if (it != known.end()) return it->second;
  LOGFONTW query{}; query.lfCharSet = DEFAULT_CHARSET;
  wcsncpy_s(query.lfFaceName, face.c_str(), _TRUNCATE);
  bool found = false;
  HDC dc = GetDC(nullptr);
  EnumFontFamiliesExW(dc, &query, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) -> int {
    *(bool*)found = true; return 0;
  }, (LPARAM)&found, 0);
  ReleaseDC(nullptr, dc);
  known[face] = found;
  return found;
}
// The font face for launcher text in the current language, or "" to keep the system's UI font.
// Japanese, Chinese and Korean get a face made for their script: font linking would otherwise draw
// kanji with Chinese shapes (or the reverse), depending on the PC's own language.
inline std::wstring ui_face() {
  const Language* l = find(current());
  if (!l || !*l->face) return {};
  return font_installed(l->face) ? std::wstring(l->face) : std::wstring();
}
// Applies ui_face() to a font description (the system message font, usually).
inline void apply_face(LOGFONTW& font) {
  const std::wstring face = ui_face();
  if (!face.empty()) wcsncpy_s(font.lfFaceName, face.c_str(), _TRUNCATE);
}
// For GetDateFormatEx/GetTimeFormatEx: the chosen language's locale, or the user's own for English.
inline std::wstring locale_name() {
  const Language* l = find(current());
  return l ? std::wstring(l->locale) : std::wstring();
}
inline std::wstring format_date(const SYSTEMTIME& t, const wchar_t* pattern) {
  const std::wstring locale = locale_name();
  wchar_t out[128]{};
  if (!GetDateFormatEx(locale.empty() ? LOCALE_NAME_USER_DEFAULT : locale.c_str(), 0, &t, pattern, out, 128, nullptr))
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &t, pattern, out, 128, nullptr);
  return out;
}
inline std::wstring format_time(const SYSTEMTIME& t) {
  const std::wstring locale = locale_name();
  wchar_t out[64]{};
  if (!GetTimeFormatEx(locale.empty() ? LOCALE_NAME_USER_DEFAULT : locale.c_str(), TIME_NOSECONDS, &t, nullptr, out, 64))
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &t, nullptr, out, 64);
  return out;
}

}  // namespace launcher::lang
