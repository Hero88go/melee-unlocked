#!/usr/bin/env python3
"""The disc report is wired: once the game opened its disc (the vanilla one under a mod disc), it tells a
YouGame app found on the local channel the disc's absolute path, once per connection and only after
hello, by the app's path rules (absolute, at most 1,024 bytes, no control characters, no network path,
plain .iso or .gcm). Never for a game the app started. A launcher start on the launcher's bridge (the
game is not on the app's channel then) has the launcher tell the running app instead, over its own channel.

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
rules = read("port/runtime/hle/yougame_disc.h")
launcher = read("port/app/launcher.cpp")
ly = read("port/app/launcher_yougame.cpp")
ly_h = read("port/app/launcher_yougame.h")
app = read("port/app/launcher_yougame_app.cpp")
app_h = read("port/app/launcher_yougame_app.h")

need("void disc_opened(const std::string& path);" in header, "disc_opened in yougame_link.h")
open_at = main.find("if (!host::disc_open(o.iso))")
report_at = main.find("yougame::disc_opened(o.mod_base_iso.empty() ? o.iso : o.mod_base_iso);")
need(open_at >= 0 and report_at > open_at, "main reports the disc after disc_open (the vanilla disc under a mod)")

body = link[link.find("void disc_opened(const std::string& path) {"):]
body = body[: body.find("\n}\n")]
need("if (!g_channel_allowed) return;" in body, "no report for a game the app started")
need("::yougame::disc::plain_image(full)" in body, "plain .iso / .gcm only")
need("::yougame::disc::path_ok(full)" in body, "path rules checked")
need("std::filesystem::absolute" in body, "absolute path")

need("p.size() > 1024" in rules, "1,024-byte cap")
need("c < 0x20 || c == 0x7f" in rules, "no control characters")
need("(p[1] == '\\\\' || p[1] == '/')) return false;" in rules, "no network path")
need('ext == ".iso" || ext == ".gcm"' in rules, "plain .iso / .gcm only")

send = link[link.find("void report_disc() {"):]
send = send[: send.find("\n}\n")]
need("g_channel_up.load()" in send, "only after hello (channel up)")
need("g_disc_epoch == epoch" in send and "g_disc_epoch = epoch;" in send, "once per connection")
need('{{"type", "disc"}, {"path", path}}' in send, "the disc message")

probe = link[link.find("bool probe_channel(bool quiet"):]
probe = probe[: probe.find("\n}\n")]
need("report_disc();\n    return true;" in probe, "reported when a connection comes up")

# The launcher: on its bridge (no extension), the launcher tells the running app the vanilla disc itself.
start = launcher[launcher.find("void launch_game_now() {"):]
need("!vanilla_active() && !hybrid && !yougame_bridge.empty() && !g_iso.empty()" in start, "launcher reports on bridge starts only")
need("launcher::yougame::report_disc(version, iso)" in start, "launcher calls report_disc")
need("std::string report_disc(const std::string& version, const std::string& path);" in ly_h, "report_disc declared")
rd = ly[ly.find("std::string report_disc(const std::string& version, const std::string& path) {"):]
rd = rd[: rd.find("\n}\n")]
need("::yougame::disc::plain_image(full)" in rd and "::yougame::disc::path_ok(full)" in rd, "launcher checks the rules")
need("if (!app::running(staging))" in rd, "launcher never starts the app for it")
need("app::tell_disc(staging, version, full)" in rd, "launcher tells the app")
need("std::string tell_disc(bool staging" in app_h, "tell_disc declared")
talk = app[app.find("Answer talk("):]
need('request({{"type", "disc"}, {"path", *disc}}, 5000)' in talk, "the launcher's disc message after hello")

if failures:
    print("yougame_disc_report_wiring_test: FAIL")
    for f in failures:
        print("  missing:", f)
    sys.exit(1)
print("yougame_disc_report_wiring_test: ok")
