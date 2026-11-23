#!/usr/bin/env python3
"""YouGame's start hook window, from Connect: with no YouGame app on this computer, the hook's own window shows
(Get YouGame / Play offline). Get YouGame installs the full YouGame app in the background and starts it in the
tray (YouGame's public header, YG_START_GETTING); Connect then takes that app's account. Nothing here goes past
YouGame's public SDK: the launcher never asks the app for a sign-in window of its own, and carries no small
add-on ("mini", "YouGame Online") download or naming of its own. The vendored headers are YouGame's public ones.

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


app = read("port/app/launcher_yougame_app.cpp")
app_h = read("port/app/launcher_yougame_app.h")
ly = read("port/app/launcher_yougame.cpp")
ly_h = read("port/app/launcher_yougame.h")
launcher = read("port/app/launcher.cpp")
start_h = read("port/third_party/yougame_start/yougame_start.h")

# The public start hook offers the background install the window's Get YouGame uses.
need("YG_START_GETTING = 8" in start_h and "int yg_start_get_progress(int *percent);" in start_h,
     "yougame_start.h is YouGame's public header with Get YouGame's background install")
need("int start_get_progress(int* percent) { return yg_start_get_progress(percent); }" in app, "the launcher reads the install's progress")
need("inline int start_get_progress(int*) { return 0; }" in app_h, "elsewhere there is no install to read")

# Get YouGame from Connect: once installed and started, its account; else Connect ends with no second sign-in.
getting = ly[ly.find("case YG_START_GETTING: {"):]
getting = getting[: getting.find("default:")]
need("std::chrono::minutes(10)" in getting, "Connect waits for the install (10 minutes at most)")
need("if (state == YG_START_GET_READY) {" in getting and "return AppHook::kRunning;" in getting, "installed: take its account")

# Only public calls: no sign-in window asked of the app, no add-on of the launcher's own.
need("sign_in_there" not in ly and "sign_in_there" not in ly_h and "kAppWindow" not in ly_h, "no app sign-in window path")
# The app's public signIn, with no provider (YouGame's overlay or its sign-in page in the browser): never a window
# of the app's own and never a provider the launcher picks.
need('request({{"type", "signIn"}}, 8000)' in app and '"provider"' not in app and "app_window" not in app_h,
     "the launcher asks only the app's public signIn, with no provider")
for name, text in (("launcher.cpp", launcher), ("launcher_yougame.cpp", ly), ("launcher_yougame.h", ly_h),
                   ("launcher_yougame_app.cpp", app), ("launcher_yougame_app.h", app_h)):
    need("YouGame Online" not in text and "mini app" not in text, name + " names no small add-on")

conn = launcher[launcher.find("void yougame_sign_in_thread(bool use_app) {"):]
conn = conn[: conn.find("\n}\n")]
need("sign_in_app(false, version, &detail);" in conn, "Connect after the hook takes the running app's account")
need("sign_in_app(listed, version, &detail);" in conn, "Connect without the hook asks the app as before")
need("launcher::yougame::sign_in_app(false, version, &detail) ==" in launcher, "the quiet start check never signs in")

if failures:
    print("launcher_app_signin_window_wiring_test: FAIL")
    for f in failures:
        print("  missing:", f)
    sys.exit(1)
print("launcher_app_signin_window_wiring_test: ok")
