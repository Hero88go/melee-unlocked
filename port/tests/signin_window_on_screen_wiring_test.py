#!/usr/bin/env python3
"""The launcher's "Sign in to YouGame" window opens on screen when the launcher is minimized (it minimizes while
the game runs): a minimized window's rect is -32000,-32000, so the window is placed by the launcher's restored
position instead.

SPDX-License-Identifier: GPL-2.0-or-later
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class SignInWindowOnScreen(unittest.TestCase):
    def test_minimized_owner_uses_its_restored_position(self):
        web = (ROOT / "port/app/launcher_yougame_web.cpp").read_text(encoding="utf-8-sig")
        place = web[web.index("RECT area{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};"):]
        place = place[:place.index("SetWindowPos(g_window, HWND_TOP")]
        self.assertIn("if (IsIconic(g->signin->owner) && GetWindowPlacement(g->signin->owner, &wp)) area = wp.rcNormalPosition;", place)
        self.assertIn("else GetWindowRect(g->signin->owner, &area);", place)
        self.assertNotIn("if (g->signin->owner) GetWindowRect(g->signin->owner, &area);", place)


if __name__ == "__main__":
    unittest.main(argv=sys.argv[:1])
