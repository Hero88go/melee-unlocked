#!/usr/bin/env python3
"""The launcher's YouGame choice reaches the game: launcher.ini yougame_env=staging starts a game that looks for
YouGame Staging (its app, its channel, its site), not only the build's own compile-time choice.

argv[1]: the source root.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
failures = []


def read(rel):
    return (root / rel).read_text(encoding="utf-8")


def need(ok, what):
    if not ok:
        failures.append(what)


launcher = read("port/app/launcher.cpp")
link = read("port/runtime/hle/yougame_link.cpp")

# Launcher: set from its own environment right before the game starts, cleared right after.
set_at = launcher.find('SetEnvironmentVariableW(L"MELEE_YOUGAME_ENV", widen(launcher::yougame::env().id).c_str());')
start_at = launcher.find("launcher::start_process(widen(exe), widen(cmd), widen(cwd), 0, pi);")
clear_at = launcher.find('SetEnvironmentVariableW(L"MELEE_YOUGAME_ENV", nullptr);')
need(set_at != -1, "launcher sets MELEE_YOUGAME_ENV from launcher::yougame::env().id")
need(-1 < set_at < start_at < clear_at, "MELEE_YOUGAME_ENV is set before the game starts and cleared after")

# Game: one runtime flag, seeded from the build, switched only by "staging" or "production".
need("std::atomic<bool> g_start_staging{kStartStaging};" in link, "g_start_staging starts at the build's choice")
read_at = link.find('std::getenv("MELEE_YOUGAME_ENV")')
need(read_at != -1, "start() reads MELEE_YOUGAME_ENV")
need('std::strcmp(env, "staging") == 0) g_start_staging = true;' in link, '"staging" switches to staging')
need('std::strcmp(env, "production") == 0) g_start_staging = false;' in link, '"production" switches to production')
need(not re.search(r'_putenv_s\("MELEE_YOUGAME_ENV"', link), "MELEE_YOUGAME_ENV stays set for a game this one restarts")
channel_at = link.find("g_channel_allowed = !kNoApp")
need(-1 < read_at < channel_at, "the choice is read before the channel decision in start()")

# Every runtime use goes through start_staging(); kStartStaging is only the definition and the seeds.
define_at = link.find("bool start_staging()")
need(define_at != -1, "start_staging() is defined")
later = link[define_at:]
uses = [m.start() for m in re.finditer(r"\bkStartStaging\b", later)]
seeds = later.count("g_app_staging{kStartStaging}")
need(len(uses) == seeds, "no runtime use of kStartStaging after start_staging() (probe_channel, look_for_app, app link)")
for what in ("candidate.staging != start_staging()", "app_registered(start_staging())",
             'start_staging() ? "https://staging.yougame.co/desktop"'):
    need(what in link, f"uses start_staging(): {what}")

if failures:
    for f in failures:
        print("FAIL:", f)
    sys.exit(1)
print("launcher_env_wiring: all checks passed")
