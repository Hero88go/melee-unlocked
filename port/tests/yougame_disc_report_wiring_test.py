#!/usr/bin/env python3
"""The disc report is wired: once the game opened its disc (the vanilla one under a mod disc), it tells a
YouGame app found on the local channel the disc's absolute path, once per connection and only after
hello, by the app's path rules (absolute, at most 1,024 bytes, no control characters, no network path,
plain .iso or .gcm). Never for a game the app started.

argv[1]: the source root (default: the repository this file is in).
SPDX-License-Identifier: GPL-2.0-or-later
"""
import pathlib
import sys

root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2]
failures = []


def read(rel):
    return (root / rel).read_text(encoding="utf-8-sig")


def need(ok, what):
    if not ok:
        failures.append(what)


link = read("port/runtime/hle/yougame_link.cpp")
header = read("port/runtime/hle/yougame_link.h")
main = read("port/app/main.cpp")

need("void disc_opened(const std::string& path);" in header, "disc_opened in yougame_link.h")
open_at = main.find("if (!host::disc_open(o.iso))")
report_at = main.find("yougame::disc_opened(o.mod_base_iso.empty() ? o.iso : o.mod_base_iso);")
need(open_at >= 0 and report_at > open_at, "main reports the disc after disc_open (the vanilla disc under a mod)")

body = link[link.find("void disc_opened(const std::string& path) {"):]
body = body[: body.find("\n}\n")]
need("if (!g_channel_allowed) return;" in body, "no report for a game the app started")
need('ext != ".iso" && ext != ".gcm"' in body, "plain .iso / .gcm only")
need("disc_path_ok(full)" in body, "path rules checked")
need("std::filesystem::absolute" in body, "absolute path")

rules = link[link.find("bool disc_path_ok(const std::string& p) {"):]
rules = rules[: rules.find("\n}\n")]
need("p.size() > 1024" in rules, "1,024-byte cap")
need("c < 0x20 || c == 0x7f" in rules, "no control characters")
need("(p[1] == '\\\\' || p[1] == '/')) return false;" in rules, "no network path")

send = link[link.find("void report_disc() {"):]
send = send[: send.find("\n}\n")]
need("g_channel_up.load()" in send, "only after hello (channel up)")
need("g_disc_epoch == epoch" in send and "g_disc_epoch = epoch;" in send, "once per connection")
need('{{"type", "disc"}, {"path", path}}' in send, "the disc message")

probe = link[link.find("bool probe_channel(bool quiet"):]
probe = probe[: probe.find("\n}\n")]
need("report_disc();\n    return true;" in probe, "reported when a connection comes up")

if failures:
    print("yougame_disc_report_wiring_test: FAIL")
    for f in failures:
        print("  missing:", f)
    sys.exit(1)
print("yougame_disc_report_wiring_test: ok")
