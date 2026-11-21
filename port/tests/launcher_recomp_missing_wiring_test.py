#!/usr/bin/env python3
"""A package without the Static Recomp (melee_port.exe): the launcher picks the Source Port instead of a
Static Recomp that cannot start, shows the segment as not installed and won't let it be picked.

argv[1]: the source root (default: the repository this file is in).
SPDX-License-Identifier: GPL-2.0-or-later
"""
import pathlib
import sys

root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2]
src = (root / "port/app/launcher.cpp").read_text(encoding="utf-8")
failures = []


def need(ok, what):
    if not ok:
        failures.append(what)


need("bool recomp_available() {" in src and '"\\\\melee_port.exe"' in src and '"\\\\melee_port_compat.exe"' in src,
     "recomp_available looks for melee_port.exe and its compatibility build")
load = src[src.index("void load_ini() {"):src.index("void save_ini() {")]
need("if (g_engine == ENGINE_LEGACY && !recomp_available() && source_available()) g_engine = ENGINE_SOURCE;" in load,
     "load_ini picks the Source Port when the Static Recomp is missing")
need("(i == 1 && !recomp_available())" in src, "the Static Recomp segment can't be picked when it is missing")
need('L"Static Recomp (not installed)"' in src and "(i == 1 && !recomp)" in src, "the segment says not installed, greyed")

if failures:
    print("launcher_recomp_missing_wiring_test: FAIL")
    for f in failures:
        print("  missing:", f)
    sys.exit(1)
print("launcher_recomp_missing_wiring_test: ok")
