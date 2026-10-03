// Scrub and keep: diagnostic log lines stay in the report after personal details are removed.
// Per line: account, lobby and peer categories are omitted; absolute file locations reduce to their
// last component; connect codes, IP addresses, email addresses and user or computer names are
// replaced; a line that still shows a file location afterwards is omitted. lobby.log is never copied.
// The launcher (port/app/launcher_crash_privacy.h) and tools/crash_report_privacy.py apply the same
// rules, checked by test/privacy_vectors.json.
// SPDX-License-Identifier: GPL-2.0-or-later
const decoder = new TextDecoder();
const encoder = new TextEncoder();
const MAX_LINE = 4096;
const CONTROLS = /[\u0000-\u0008\u000a-\u001f\u007f-\u009f]/g;
const PRIVATE_PREFIXES = [
  "slippi: logged in as", "slippi: logged out", "slippi: not logged in", "slippi: login requested",
  "slippi: matchmaking started", "slippi: cannot create peer", "slippi: disconnect from",
  "slippi: got disconnect from", "slippi: local peer test", "slippi: cannot parse",
  "slippi: using the Slippi Launcher login", "slippi: keeping direct/teams code history",
  "discord:", "peer ", "lobby", "another launcher is using this lobby identity", "add address:"];
const PRIVATE_WORDS = ["displayname", "connectcode", "playkey", "\"uid\"", "password", "token", "secret",
  "authorization"];
const GENERIC_NAMES = new Set(["public", "default", "all users", "default user"]);
const NAME = "([^\\\\/:*?\"<>|\\u0000-\\u001f\\u007f]{1,64})";
const USER_FOLDER = new RegExp("[Uu][Ss][Ee][Rr][Ss][\\\\/]" + NAME + "(?=[\\\\/])", "g");
const HOME_FOLDER = new RegExp("/home/" + NAME + "(?=/)", "g");
const ROOT = /[A-Za-z]:[\\/]|(?<![A-Za-z0-9_])\\\\[A-Za-z0-9_]/;
const CODE = /\b[A-Z0-9]{1,8}#[0-9]{1,6}\b/g;
const IPV4 = /\b[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\b(?::[0-9]{1,5}\b)?/g;
const EMAIL = /[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+/g;
const WORD = /[A-Za-z0-9_]/;
const TOKENS = new Set(["user", "code", "email"]);

export function safeVersion(value) {
  const text = String(value || "");
  return /^\d{1,3}\.\d{1,3}(?:\.\d{1,3}){0,2}$/.test(text) ? text : "unknown";
}
export function safeEngine(value) {
  if (value === "source" || value === "Source Port") return "Source Port";
  if (value === "recomp" || value === "port" || value === "Static Recomp") return "Static Recomp";
  if (value === "playback" || value === "Playback") return "Playback";
  return "unknown";
}

function asciiLower(text) { return text.replace(/[A-Z]+/g, run => run.toLowerCase()); }

// User folder names found in `text` plus the caller's names: lowercase, longest first.
export function reportNames(text, extra = []) {
  const found = [...text.matchAll(USER_FOLDER), ...text.matchAll(HOME_FOLDER)].map(hit => hit[1]);
  const names = new Set();
  for (const name of [...found, ...extra]) {
    const low = asciiLower(String(name || ""));
    if (low && !GENERIC_NAMES.has(low)) names.add(low);
  }
  return [...names].sort((a, b) => b.length - a.length || (a < b ? -1 : a > b ? 1 : 0));
}

// Cut from the first drive or UNC root through the last separator; null when a name would remain.
function redactPath(line) {
  const root = ROOT.exec(line);
  if (!root) return line;
  const start = root.index, last = Math.max(line.lastIndexOf("\\"), line.lastIndexOf("/"));
  if (line[start] === "\\" && last === start + 1) return null;
  let parent = last;
  while (parent > start && line[parent - 1] !== "\\" && line[parent - 1] !== "/") --parent;
  const folder = asciiLower(line.slice(parent, last));
  if (folder === "users" || folder === "home") return null;
  return line.slice(0, start) + line.slice(last + 1);
}

function replaceName(line, name) {
  const low = asciiLower(line);
  let out = "", last = 0, at = 0;
  for (;;) {
    const pos = low.indexOf(name, at);
    if (pos < 0) break;
    const end = pos + name.length;
    const before = pos ? line[pos - 1] : "", after = end < line.length ? line[end] : "";
    if (WORD.test(before) || WORD.test(after) || (TOKENS.has(name) && before === "[" && after === "]")) {
      at = pos + 1;
      continue;
    }
    out += line.slice(last, pos) + "[user]";
    last = at = end;
  }
  return out + line.slice(last);
}

function unsafe(line) {
  const low = asciiLower(line);
  return ROOT.test(line) || ["users\\", "users/", "/home/", "appdata"].some(part => low.includes(part));
}

// One log line with personal details removed, or null when the line must be omitted.
// `names` comes from reportNames().
export function scrubLine(input, names = []) {
  const raw = String(input);
  // The limit counts characters, so a surrogate pair is one. Bound the count before scanning.
  if (raw.length > 2 * MAX_LINE) return null;
  if (raw.length > MAX_LINE && raw.replace(/[\ud800-\udbff][\udc00-\udfff]/g, "x").length > MAX_LINE) return null;
  let line = raw.replace(CONTROLS, "");
  let body = line.replace(/^[ \t]+/, "");
  if (!body) return null;
  if (body.startsWith("[game]")) body = body.slice(6).replace(/^[ \t]+/, "");
  if (PRIVATE_PREFIXES.some(prefix => body.startsWith(prefix))) return null;
  const low = asciiLower(line);
  if (PRIVATE_WORDS.some(word => low.includes(word))) return null;
  line = redactPath(line);
  if (line === null) return null;
  line = line.replace(CODE, "[code]").replace(IPV4, "[ip]").replace(EMAIL, "[email]");
  for (const name of names) if (name.length >= 3) line = replaceName(line, name);
  return unsafe(line) ? null : line;
}

// The newest `cap` bytes of `text` as UTF-8, starting on a line boundary.
export function tailBytes(text, cap) {
  const bytes = encoder.encode(text);
  if (bytes.length <= cap) return text;
  const tail = bytes.subarray(bytes.length - cap), newline = tail.indexOf(10);
  return newline < 0 ? "" : decoder.decode(tail.subarray(newline + 1));
}

// The game log says what the session was once, at its start: the version, the settings, the mods and
// the skins in use. A long session pushed those lines out of the newest `cap` bytes, and the report
// could then not say whether a mod or a skin was involved. So when the log is cut, its first whole
// lines are kept as well (few and short: a long first line is not a start-up line), scrubbed the
// same way as every other line.
const HEAD_BYTES = 6 * 1024, HEAD_LINES = 60, HEAD_LINE_BYTES = 400;
function sessionStart(bytes, start, names) {
  const region = decoder.decode(bytes.subarray(0, Math.min(start, 4 * HEAD_BYTES))).replace(/\r+\n?/g, "\n");
  const lines = region.split("\n");
  lines.pop();   // the last piece may be half a line
  const kept = [];
  let used = 0, omitted = 0;
  for (const line of lines.slice(0, HEAD_LINES)) {
    if (/^[ \t]*$/.test(line)) continue;
    const size = encoder.encode(line).length + 1;
    if (size > HEAD_LINE_BYTES || used + size > HEAD_BYTES) break;
    const safe = scrubLine(line, names);
    if (safe === null) { ++omitted; continue; }
    kept.push(safe); used += size;
  }
  return { text: kept.length ? kept.join("\n") + "\n" : "", omitted };
}

export function sanitizedText(bytes, cap, filename, extraNames = []) {
  const start = Math.max(0, bytes.length - cap);
  const text = decoder.decode(bytes.subarray(start)).replace(/\r+\n?/g, "\n");
  const names = reportNames(text, extraNames), lines = text.split("\n");
  const kept = [];
  let omitted = 0;
  for (let index = 0; index < lines.length; ++index) {
    if (/^[ \t]*$/.test(lines[index])) continue;
    // A byte cap can cut through a line, and the remaining half of a file location is not recognizable.
    const safe = filename === "lobby.log" || (start && index === 0) ? null : scrubLine(lines[index], names);
    if (safe === null) ++omitted; else kept.push(safe);
  }
  const head = start && filename === "melee_port.log" ? sessionStart(bytes, start, names) : { text: "", omitted: 0 };
  omitted += head.omitted;
  const header = `[Privacy: ${omitted} private lines omitted]\n` + head.text + (start ? "[Earlier input bytes omitted]\n" : "");
  const body = tailBytes(kept.join("\n"), Math.max(0, cap - encoder.encode(header).length - 1));
  return encoder.encode(header + body + "\n");
}
