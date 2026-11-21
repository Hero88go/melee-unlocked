#!/usr/bin/env python3
"""Sign in by code (local-channel-contract.md section 8) is wired: a YouGame row picked while the app
is signed out opens the panel; the panel asks the app once for a code, and every answer, push and
refusal the contract names has its fallback (the browser sign-in, waiting, a new code, hello again).
The QR comes from the vendored Nayuki qrcodegen with its licence, built through qr_code.cpp.

argv[1]: the source root (default: the repository this file is in).
SPDX-License-Identifier: GPL-2.0-or-later
"""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2]
failures = []


def read(rel):
    return (root / rel).read_text(encoding="utf-8")


def need(ok, what):
    if not ok:
        failures.append(what)


model = read("port/runtime/hle/yougame_signin.h")
link = read("port/runtime/hle/yougame_link.cpp")
header = read("port/runtime/hle/yougame_link.h")
online = read("port/runtime/hle/slippi_online.cpp")
overlay = read("port/runtime/gx/signin_overlay.cpp")
settings = read("port/runtime/gx/pc_settings.cpp")
qr = read("port/runtime/gx/qr_code.cpp")

# The two requests, exactly as the contract spells them.
need('{"type", "signIn"}, {"mode", "code"}' in model, "the code request")
need('{"type", "signIn"}, {"mode", "browser"}' in model, "the browser request")

# The code answer's fields, and the words around them.
for field in ("user_code", "verify_url", "qr_text", "expires_in"):
    need(f'"{field}"' in model, f"the code answer's {field}")
need('"where"' in model and '"code"' in model, "the answer's where")
need('"Go to"' in model and '"and enter"' in model, "the Go to ... and enter words")
need('"Sign in on this PC"' in model, "the browser button")
need('"Get a new code"' in model, "the expired screen's button")
need('"Try again"' in model, "the failed screen's button")
need('"Finish signing in in your browser"' in model, "the no-code fallback words")
need('"https://"' in model, "the address shown without https://")

# Pushes: the three the contract names, and anything else ignored.
for push in ("signedIn", "signInExpired", "signInFailed"):
    need(f'type == "{push}"' in model, f"the {push} push")
push_body = model.split("bool push(const nlohmann::json& push")[1].split("\n  }\n")[0]
need(push_body.rstrip().endswith("return false;"), "unknown pushes ignored")
need("sign_in_push(push)" in link, "pushes reach the sign-in model")

# Refusals and their fallbacks.
need('code == "notYet"' in model and "Screen::NotYet" in model, "notYet shows its words")
need('code == "busy"' in model and '"retryAfter"' in model, "busy waits retryAfter")
need('code == "alreadySignedIn"' in model and "rehello_ = true" in model, "alreadySignedIn says hello again")
need('code == "badRequest" && asked == Ask::Code) return ask(Ask::Browser)' in model,
     "an app with no code mode falls back to the browser")
need("Screen::Browser" in model, "an ok answer with no code tells the player to finish in the browser")

# One code per app: the open panel draws the live code again, never asks on a redraw.
need("if (!code_.empty() && now < expires_at_) { screen_ = Screen::Code; return Ask::None; }" in model,
     "a live code is drawn again, not asked again")
need("in_flight_" in model, "one ask in flight at a time")

# Hello again after signing in, and the dead end it fixes.
need("sign_in_rehello" in link and "player_arrived()" in link, "signedIn says hello again")
need("signin::hello_signed_out(result)" in link, "hello records a signed-out app")
need("bool sign_in_needed()" in link and "bool sign_in_needed();" in header, "sign_in_needed")
need("yougame::sign_in_needed()) yougame::sign_in_open();" in online, "a picked row opens the panel")
# A row pressed before the app could say "signed out" (cold start): the shim's once-a-second poll opens
# the card when it knows, and the mpCode signedOut answer marks it at once (11-17).
need("if (!yougame::sign_in_needed()) s_yg_pick = now_pick_ms();" in online, "an early press is remembered")
need("else if (yougame::sign_in_needed()) { s_yg_pick = 0; yougame::sign_in_open(); }" in online,
     "the menu's poll opens the card for a remembered press")
need('g_channel_code == "signedOut") g_channel_signed_out = true;' in link, "mpCode signedOut marks the app signed out")
need("yougame::sign_in_needed()) && !yougame::active() && (q[0] & 2)) q[0] |= 64;" in online,
     "the guest's wait-and-confirm bit while signed out")
need("user_code" not in re.sub(r"//.*", "", link.split("void sign_in_send")[1].split("\n}\n")[0]).split("log(")[-1],
     "the user code never logged")

# The panel: input captured, drawn over the game, Esc and B are Back.
need('#include "signin_overlay.h"' in settings, "pc_settings includes the panel")
need("signin_overlay::open()" in settings and "signin_overlay::draw(" in settings, "pc_settings draws the panel")
need("signin_overlay::back()" in settings, "Esc is Back")
need("ImGuiKey_GamepadFaceRight" in overlay, "B is Back")
need("SetKeyboardFocusHere" in overlay, "the first button takes focus")
need("qr_code::encode" in overlay and "quiet = 4" in overlay, "the QR with its quiet zone")
# 122.20: the box owns every key while it shows, Training included (Tab used to open the Practice menu
# over it there, and the menu took A, Enter and Esc).
tab_gate = settings.split("if (tab_pressed && !state.open")[1].split("{")[0]
need("!signin_up" in tab_gate, "Tab and the Practice chord do nothing while the box shows")
need("if (signin_up && !state.open && !state.menu_open) state.practice_open = false;" in settings,
     "the box opened over the Practice menu shuts the menu")
need("if (appearing) ImGui::SetNextWindowFocus();" not in overlay and "  ImGui::SetNextWindowFocus();" in overlay,
     "the box keeps the focus every frame it shows")
need("practice_force_capture" in settings and "|| signin_up;" in settings, "the game gets no controllers while it shows")

# qrcodegen, vendored with its licence and built through one TU in a globbed folder.
qrdir = root / "port/third_party/qrcodegen"
for name in ("qrcodegen.cpp", "qrcodegen.hpp", "LICENSE", "README.md"):
    need((qrdir / name).is_file(), f"third_party/qrcodegen/{name}")
if (qrdir / "LICENSE").is_file():
    need("Permission is hereby granted, free of charge" in (qrdir / "LICENSE").read_text(encoding="utf-8"),
         "qrcodegen's MIT licence")
need('#include "../../third_party/qrcodegen/qrcodegen.cpp"' in qr, "qr_code.cpp builds qrcodegen")
for build in ("port/CMakeLists.txt", "port/linux/full/CMakeLists.txt", "port/mac/build_full.py"):
    text = read(build)
    need("runtime/gx/*.cpp" in text or "runtime/gx" in text, f"{build} builds runtime/gx")

# YouGame Online signs the player in in its own window: a code answer with window:true (or where "window")
# draws no code or QR, says where to finish, and the overlay shows that line.
need('true_of(result, "window")' in model and 'text_of(result, "where") == "window"' in model, "the window answer read")
need('"Finish signing in in YouGame Online"' in model and "online_window_ ? kWindowOnline" in model, "the YouGame Online line")
need("words::kWindow) : v.message" in overlay, "the overlay draws the window line")

if failures:
    print("yougame_signin_wiring_test: FAIL")
    for f in failures:
        print("  missing:", f)
    sys.exit(1)
print("yougame_signin_wiring_test: ok")
