"""Scrub and keep: shared crash reports keep diagnostic log lines after removing personal details.

Per line: account, lobby and peer categories are omitted; absolute file locations reduce to their
last component; connect codes, IP addresses, email addresses and user or computer names are
replaced; a line that still shows a file location afterwards is omitted. lobby.log is never copied.
The launcher (port/app/launcher_crash_privacy.h) and the relay (tools/crash_relay/src/privacy.js)
apply the same rules, checked by tools/crash_relay/test/privacy_vectors.json.
"""
import re

MAX_LINE = 4096
CONTROLS = re.compile("[\\x00-\\x08\\x0a-\\x1f\\x7f-\\x9f]")
PRIVATE_PREFIXES = (
    "slippi: logged in as", "slippi: logged out", "slippi: not logged in", "slippi: login requested",
    "slippi: matchmaking started", "slippi: cannot create peer", "slippi: disconnect from",
    "slippi: got disconnect from", "slippi: local peer test", "slippi: cannot parse",
    "slippi: using the Slippi Launcher login", "slippi: keeping direct/teams code history",
    "discord:", "peer ", "lobby", "another launcher is using this lobby identity", "add address:")
PRIVATE_WORDS = ("displayname", "connectcode", "playkey", "\"uid\"", "password", "token", "secret",
                 "authorization")
GENERIC_NAMES = ("public", "default", "all users", "default user")
NAME = "([^\\\\/:*?\"<>|\\x00-\\x1f\\x7f]{1,64})"
USER_FOLDER = re.compile("[Uu][Ss][Ee][Rr][Ss][\\\\/]" + NAME + "(?=[\\\\/])")
HOME_FOLDER = re.compile("/home/" + NAME + "(?=/)")
ROOT = re.compile("[A-Za-z]:[\\\\/]|(?<![A-Za-z0-9_])\\\\\\\\[A-Za-z0-9_]")
CODE = re.compile(r"\b[A-Z0-9]{1,8}#[0-9]{1,6}\b", re.ASCII)
IPV4 = re.compile(r"\b[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\b(?::[0-9]{1,5}\b)?", re.ASCII)
EMAIL = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+")
WORD = frozenset("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
LOWER = {code: code + 32 for code in range(65, 91)}
TOKENS = ("user", "code", "email")


def ascii_lower(text):
    return text.translate(LOWER)


def report_names(text, extra=()):
    """User folder names found in `text` plus the caller's names: lowercase, longest first."""
    found = [m[1] for m in USER_FOLDER.finditer(text)] + [m[1] for m in HOME_FOLDER.finditer(text)]
    names = {ascii_lower(str(name)) for name in [*found, *extra] if name}
    return sorted(names - set(GENERIC_NAMES), key=lambda name: (-len(name), name))


def redact_path(line):
    """Cut from the first drive or UNC root through the last separator; None when a name would remain."""
    root = ROOT.search(line)
    if not root:
        return line
    start, last = root.start(), max(line.rfind("\\"), line.rfind("/"))
    if line[start] == "\\" and last == start + 1:
        return None
    parent = last
    while parent > start and line[parent - 1] not in "\\/":
        parent -= 1
    if ascii_lower(line[parent:last]) in ("users", "home"):
        return None
    return line[:start] + line[last + 1:]


def replace_name(line, name):
    low, out, last, at = ascii_lower(line), [], 0, 0
    while True:
        pos = low.find(name, at)
        if pos < 0:
            break
        end = pos + len(name)
        before, after = line[pos - 1] if pos else "", line[end] if end < len(line) else ""
        if before in WORD or after in WORD or (name in TOKENS and before == "[" and after == "]"):
            at = pos + 1
            continue
        out.append(line[last:pos] + "[user]")
        last = at = end
    return "".join(out) + line[last:]


def unsafe(line):
    low = ascii_lower(line)
    return bool(ROOT.search(line)) or any(x in low for x in ("users\\", "users/", "/home/", "appdata"))


def scrub_line(raw, names=()):
    """One log line with personal details removed, or None when the line must be omitted.

    `names` comes from report_names()."""
    if len(raw) > MAX_LINE:
        return None
    line = CONTROLS.sub("", raw)
    body = line.lstrip(" \t")
    if not body:
        return None
    if body.startswith("[game]"):
        body = body[6:].lstrip(" \t")
    if body.startswith(PRIVATE_PREFIXES):
        return None
    low = ascii_lower(line)
    if any(word in low for word in PRIVATE_WORDS):
        return None
    line = redact_path(line)
    if line is None:
        return None
    line = EMAIL.sub("[email]", IPV4.sub("[ip]", CODE.sub("[code]", line)))
    for name in names:
        if len(name) >= 3:
            line = replace_name(line, name)
    return None if unsafe(line) else line


def private_text(name, data, names=(), cut_start=False):
    """Scrub one collected text file. `cut_start` drops a first line that a byte cap cut through."""
    text = re.sub(r"\r+\n?", "\n", data.decode("utf-8", "replace"))
    known = report_names(text, names)
    result, omitted = [], 0
    for index, line in enumerate(text.split("\n")):
        if not line.strip(" \t"):
            continue
        safe = None if name == "lobby.log" or (cut_start and index == 0) else scrub_line(line, known)
        if safe is None:
            omitted += 1
        else:
            result.append(safe)
    if omitted:
        result.append(f"[{omitted} lines omitted for privacy]")
    return "\n".join(result) + ("\n" if result else "")
