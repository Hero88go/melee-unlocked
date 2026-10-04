// Scrub and keep: shared reports keep diagnostic log lines after personal details are removed.
// Per line: account, lobby and peer categories are omitted; absolute file locations reduce to their
// last component; connect codes, IP addresses, email addresses and user or computer names are
// replaced; a line that still shows a file location afterwards is omitted. lobby.log is never
// copied, and binary dumps never leave the device.
// tools/crash_relay/src/privacy.js and tools/crash_report_privacy.py apply the same rules; all three
// are checked against tools/crash_relay/test/privacy_vectors.json.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace launcher::crash {

inline std::string report_version(const std::string& value) {
  if (value.size() > 32) return "unknown";
  static const std::regex version("[0-9]+(?:\\.[0-9]+){1,3}");
  return std::regex_match(value, version) ? value : "unknown";
}
inline std::string report_engine(const std::string& value) {
  if (value == "source" || value == "Source Port") return "Source Port";
  if (value == "recomp" || value == "port" || value == "Static Recomp") return "Static Recomp";
  if (value == "playback") return "Playback";
  return "unknown";
}

// Byte scans instead of std::regex: the same result as the relay's patterns, with no backtracking.
// All classes are ASCII; bytes of a multi-byte UTF-8 character are never letters, digits or separators.
namespace scrub {

inline constexpr size_t kMaxLine = 4096;   // characters
inline constexpr const char* kPrivatePrefixes[] = {
#ifndef MELEE_NO_SLIPPI   // lines only the build with that online service writes
  "slippi: logged in as", "slippi: logged out", "slippi: not logged in", "slippi: login requested",
  "slippi: matchmaking started", "slippi: cannot create peer", "slippi: disconnect from",
  "slippi: got disconnect from", "slippi: local peer test", "slippi: cannot parse",
  "slippi: using the Slippi Launcher login", "slippi: keeping direct/teams code history",
#endif
  "discord:", "peer ", "lobby", "another launcher is using this lobby identity", "add address:"};
inline constexpr const char* kPrivateWords[] = {"displayname", "connectcode", "playkey", "\"uid\"", "password",
  "token", "secret", "authorization"};

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }
inline bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
inline bool is_alpha(char c) { return is_upper(c) || (c >= 'a' && c <= 'z'); }
inline bool is_word(char c) { return is_alpha(c) || is_digit(c) || c == '_'; }
inline bool is_sep(char c) { return c == '\\' || c == '/'; }
inline std::string lower(std::string s) {
  for (char& c : s) if (is_upper(c)) c = (char)(c - 'A' + 'a');
  return s;
}

// One folder name: no separator, no character Windows forbids in a name, no control character.
inline bool is_name_char(char c) {
  const unsigned char u = (unsigned char)c;
  return u >= 0x20 && u != 0x7F && !is_sep(c) && c != ':' && c != '*' && c != '?' && c != '"' &&
         c != '<' && c != '>' && c != '|';
}

// Collect <name> from Users\<name>\ (either separator, any case) or from /home/<name>/.
inline void folder_names(const std::string& text, bool users, std::vector<std::string>& out) {
  const std::string needle = users ? "users" : "/home/";
  const std::string lowered = users ? lower(text) : std::string();
  const std::string& hay = users ? lowered : text;
  size_t at = 0, pos;
  while ((pos = hay.find(needle, at)) != std::string::npos) {
    at = pos + 1;
    size_t begin = pos + needle.size();
    if (users) {
      if (begin >= text.size() || !is_sep(text[begin])) continue;
      ++begin;
    }
    size_t end = begin;
    while (end < text.size() && end - begin <= 64 && is_name_char(text[end])) ++end;
    const size_t length = end - begin;
    if (length < 1 || length > 64 || end >= text.size()) continue;
    if (users ? !is_sep(text[end]) : text[end] != '/') continue;
    out.push_back(text.substr(begin, length));
    at = end;
  }
}

// User folder names found in `text` plus the caller's names: lowercase, longest first.
inline std::vector<std::string> report_names(const std::string& text, const std::vector<std::string>& extra) {
  std::vector<std::string> found, names;
  folder_names(text, true, found);
  folder_names(text, false, found);
  found.insert(found.end(), extra.begin(), extra.end());
  for (const std::string& raw : found) {
    const std::string name = lower(raw);
    if (name.empty() || name == "public" || name == "default" || name == "all users" || name == "default user") continue;
    if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
  }
  std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
    return a.size() != b.size() ? a.size() > b.size() : a < b;
  });
  return names;
}

// A drive root (X:\ or X:/) or a UNC root (\\server), or npos.
inline size_t find_root(const std::string& s) {
  for (size_t i = 0; i + 2 < s.size(); ++i) {
    if (is_alpha(s[i]) && s[i + 1] == ':' && is_sep(s[i + 2])) return i;
    if (s[i] == '\\' && s[i + 1] == '\\' && is_word(s[i + 2]) && (i == 0 || !is_word(s[i - 1]))) return i;
  }
  return std::string::npos;
}

// Cut from the first root through the last separator on the line, leaving the final component.
// False when what would remain is a server or user folder name: the line is then omitted.
inline bool redact_path(std::string& line) {
  const size_t start = find_root(line);
  if (start == std::string::npos) return true;
  const size_t last = line.find_last_of("\\/");
  if (line[start] == '\\' && last == start + 1) return false;
  size_t parent = last;
  while (parent > start && !is_sep(line[parent - 1])) --parent;
  const std::string folder = lower(line.substr(parent, last - parent));
  if (folder == "users" || folder == "home") return false;
  line.erase(start, last + 1 - start);
  return true;
}

// Connect codes, \b[A-Z0-9]{1,8}#[0-9]{1,6}\b, become [code].
inline std::string redact_codes(const std::string& s) {
  std::string out;
  size_t last = 0;
  for (size_t hash = 0; hash < s.size(); ++hash) {
    if (s[hash] != '#') continue;
    size_t begin = hash, end = hash + 1;
    while (begin > last && (is_upper(s[begin - 1]) || is_digit(s[begin - 1]))) --begin;
    while (end < s.size() && is_digit(s[end])) ++end;
    const size_t letters = hash - begin, digits = end - hash - 1;
    if (letters < 1 || letters > 8 || digits < 1 || digits > 6) continue;
    if ((begin > 0 && is_word(s[begin - 1])) || (end < s.size() && is_word(s[end]))) continue;
    out.append(s, last, begin - last);
    out += "[code]";
    last = end;
    hash = end - 1;
  }
  out.append(s, last, std::string::npos);
  return out;
}

// One to `most` digits ending at a word boundary; advances `at` past them.
inline bool digit_group(const std::string& s, size_t& at, size_t most) {
  const size_t begin = at;
  while (at < s.size() && is_digit(s[at])) ++at;
  return at > begin && at - begin <= most;
}

// IPv4 addresses with an optional :port become [ip]. Three dotted groups (a version) are kept.
inline std::string redact_ips(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    if (is_digit(s[i]) && (i == 0 || !is_word(s[i - 1]))) {
      size_t end = i;
      bool ok = true;
      for (int group = 0; group < 4 && ok; ++group) {
        ok = digit_group(s, end, 3);
        if (ok && group < 3) {
          ok = end < s.size() && s[end] == '.';
          if (ok) ++end;
        }
      }
      if (ok && (end == s.size() || !is_word(s[end]))) {
        if (end < s.size() && s[end] == ':') {
          size_t port = end + 1;
          if (digit_group(s, port, 5) && (port == s.size() || !is_word(s[port]))) end = port;
        }
        out += "[ip]";
        i = end;
        continue;
      }
    }
    out += s[i++];
  }
  return out;
}

inline bool is_label(char c) { return is_alpha(c) || is_digit(c) || c == '-'; }
inline bool is_local(char c) { return is_label(c) || c == '.' || c == '_' || c == '%' || c == '+'; }

// Email addresses, [A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+, become [email].
inline std::string redact_emails(const std::string& s) {
  std::string out;
  size_t last = 0;
  for (size_t at = 0; at < s.size(); ++at) {
    if (s[at] != '@') continue;
    size_t begin = at, end = at + 1, dots = 0;
    while (begin > last && is_local(s[begin - 1])) --begin;
    while (end < s.size() && is_label(s[end])) ++end;
    if (begin == at || end == at + 1) continue;
    while (end + 1 < s.size() && s[end] == '.' && is_label(s[end + 1])) {
      ++end;
      while (end < s.size() && is_label(s[end])) ++end;
      ++dots;
    }
    if (!dots) continue;
    out.append(s, last, begin - last);
    out += "[email]";
    last = end;
    at = end - 1;
  }
  out.append(s, last, std::string::npos);
  return out;
}

// `name` (lowercase), in any case, becomes [user] unless it sits inside a longer identifier, so an
// engine symbol that happens to contain a user name survives.
inline std::string replace_name(const std::string& s, const std::string& name) {
  const std::string low = lower(s);
  const bool token = name == "user" || name == "code" || name == "email";   // our own [user] [code] [email]
  std::string out;
  size_t last = 0, at = 0, pos;
  while ((pos = low.find(name, at)) != std::string::npos) {
    const size_t end = pos + name.size();
    const bool inside = (pos > 0 && is_word(s[pos - 1])) || (end < s.size() && is_word(s[end]));
    const bool marker = token && pos > 0 && end < s.size() && s[pos - 1] == '[' && s[end] == ']';
    if (inside || marker) { at = pos + 1; continue; }
    out.append(s, last, pos - last);
    out += "[user]";
    last = at = end;
  }
  out.append(s, last, std::string::npos);
  return out;
}

// Safety net: anything that still looks like a file location after the redactions.
inline bool shows_location(const std::string& line) {
  if (find_root(line) != std::string::npos) return true;
  const std::string low = lower(line);
  for (const char* part : {"users\\", "users/", "/home/", "appdata"})
    if (low.find(part) != std::string::npos) return true;
  return false;
}

}  // namespace scrub

// One log line with personal details removed, or nothing when the line must be omitted.
// `names` comes from scrub::report_names().
inline std::optional<std::string> scrub_line(const std::string& raw, const std::vector<std::string>& names) {
  size_t characters = 0;
  for (const char c : raw) characters += ((unsigned char)c & 0xC0) != 0x80;
  if (characters > scrub::kMaxLine) return std::nullopt;
  std::string line;
  for (size_t i = 0; i < raw.size(); ++i) {
    const unsigned char c = (unsigned char)raw[i];
    // Control characters other than tab, including U+0080 to U+009F (C2 80 to C2 9F in UTF-8).
    if (c == 0xC2 && i + 1 < raw.size() && (unsigned char)raw[i + 1] >= 0x80 && (unsigned char)raw[i + 1] <= 0x9F) { ++i; continue; }
    if ((c < 0x20 && c != '\t') || c == 0x7F) continue;
    line += (char)c;
  }
  size_t body = line.find_first_not_of(" \t");
  if (body == std::string::npos) return std::nullopt;
  if (line.compare(body, 6, "[game]") == 0) {
    body = line.find_first_not_of(" \t", body + 6);
    if (body == std::string::npos) body = line.size();
  }
  for (const char* prefix : scrub::kPrivatePrefixes)
    if (line.compare(body, std::strlen(prefix), prefix) == 0) return std::nullopt;
  const std::string low = scrub::lower(line);
  for (const char* word : scrub::kPrivateWords)
    if (low.find(word) != std::string::npos) return std::nullopt;
  if (!scrub::redact_path(line)) return std::nullopt;
  line = scrub::redact_emails(scrub::redact_ips(scrub::redact_codes(line)));
  for (const std::string& name : names)
    if (name.size() >= 3) line = scrub::replace_name(line, name);
  if (scrub::shows_location(line)) return std::nullopt;
  return line;
}

// An environment variable as UTF-8, empty when unset.
inline std::string environment_name(const char* key) {
#ifdef _WIN32
  const std::wstring wide_key(key, key + std::strlen(key));
  wchar_t* value = nullptr;
  size_t count = 0;
  if (_wdupenv_s(&value, &count, wide_key.c_str()) != 0 || !value) return {};
  const std::wstring wide(value);
  std::free(value);
  std::string out;
  for (size_t i = 0; i < wide.size(); ++i) {
    uint32_t c = (uint16_t)wide[i];
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < wide.size()) {
      const uint32_t low = (uint16_t)wide[i + 1];
      if (low >= 0xDC00 && low < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00); ++i; }
    }
    if (c < 0x80) out += (char)c;
    else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
    else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
  }
  return out;
#else
  const char* value = std::getenv(key);
  return value ? value : "";
#endif
}

// The Windows user name and computer name: removed from every line even where no path gives them away.
inline std::vector<std::string> environment_names() {
  return {environment_name("USERNAME"), environment_name("COMPUTERNAME")};
}

// Scrub one collected text file. `cut_start` drops a first line that a byte cap cut through, because
// the remaining half of a file location cannot be recognized.
inline std::vector<uint8_t> private_report_text(const std::string& name, const std::vector<uint8_t>& data,
                                                bool cut_start = false,
                                                const std::vector<std::string>& extra_names = environment_names()) {
  const std::string raw(data.begin(), data.end());
  // Coalesce CR/CR/LF so each log line is scrubbed once.
  std::string normalized;
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '\r') {
      while (i + 1 < raw.size() && raw[i + 1] == '\r') ++i;
      if (i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
      normalized += '\n';
    } else normalized += raw[i];
  }
  const std::vector<std::string> names = scrub::report_names(normalized, extra_names);
  std::istringstream lines(normalized);
  std::string line, out;
  size_t omitted = 0;
  bool first = true;
  while (std::getline(lines, line)) {
    const bool cut = cut_start && first;
    first = false;
    if (line.find_first_not_of(" \t") == std::string::npos) continue;
    std::optional<std::string> safe;
    if (name != "lobby.log" && !cut) safe = scrub_line(line, names);
    if (safe) out += *safe + '\n';
    else ++omitted;
  }
  if (omitted) out += "[" + std::to_string(omitted) + " lines omitted for privacy]\n";
  return std::vector<uint8_t>(out.begin(), out.end());
}

}  // namespace launcher::crash
