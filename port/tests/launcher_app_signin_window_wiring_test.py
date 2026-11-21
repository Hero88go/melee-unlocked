#!/usr/bin/env python3
"""Connect signs in through the YouGame app's own sign-in window: when the running app (YouGame Online) is signed
out, the launcher asks it for its sign-in (signIn, mode code, as a game asks), waits for its signedIn push and says
hello again, so the session is the app's and carries over to the full app. A window closed without a sign-in ends
Connect with no second sign-in; an app with no window of its own falls back to the launcher's own sign-in.

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

need("bool app_window = false;" in app_h, "Answer.app_window")
need("int sign_in_wait_ms = 0);" in app_h, "ask takes sign_in_wait_ms")
ask = app   # the body may sit in a helper ask calls
need('type == "signedIn" || type == "signInExpired" || type == "signInFailed"' in ask, "the reader keeps sign-in pushes")
need('request({{"type", "signIn"}, {"mode", "code"}}, 8000)' in ask, "asks the app's sign-in as a game does")
need('r.value("window", false) == true' in ask and 'r["where"] == "window"' in ask, "the app's window answer")
need("out.app_window = true;" in ask, "marks the app's window")
need('if (pushed == "signedIn") {\n        say_hello();' in ask, "hello again on signedIn")
need('refused == "alreadySignedIn"' in ask, "alreadySignedIn says hello again")

need("kAppWindow" in ly_h and "bool sign_in_there = false);" in ly_h, "sign_in_app's app-window path")
need("app::ask(staging, version, 12000, sign_in_there ? 10 * 60 * 1000 : 0)" in ly, "sign_in_app waits in the app's window")
need("if (a.app_window) {" in ly and "return AppSignIn::kAppWindow;" in ly, "closed window is kAppWindow")

conn = launcher[launcher.find("void yougame_sign_in_thread(bool use_app) {"):]
conn = conn[: conn.find("\n}\n")]
need("sign_in_app(false, version, &detail, true)" in conn, "Connect after the hook signs in there")
need("sign_in_app(listed, version, &detail, listed)" in conn, "Connect without the hook signs in there when listed")
need(conn.count("AppSignIn::kAppWindow") == 2, "a closed app window opens no second sign-in")
need("launcher::yougame::sign_in_app(false, version, &detail) ==" in launcher, "the quiet start check never signs in")

if failures:
    print("launcher_app_signin_window_wiring_test: FAIL")
    for f in failures:
        print("  missing:", f)
    sys.exit(1)
print("launcher_app_signin_window_wiring_test: ok")
