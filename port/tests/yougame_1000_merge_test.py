"""Source checks for YouGame on Melee Unlocked 1.0 (the launcher bridge and the split online menu)."""
from pathlib import Path
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class YouGame1000MergeTest(unittest.TestCase):
    def test_menu_command_has_its_own_id(self):
        bridge = source("port/runtime/hle/native_slippi_bridge.h")
        menu = source("sourceport/game/shim/mu_slippi_menu.c")
        host_id = int(re.search(r"CMD_YOUGAME_RANKED = (0x[0-9A-Fa-f]+)", bridge)[1], 16)
        game_id = int(re.search(r"CMD_YOUGAME_RANKED = (0x[0-9A-Fa-f]+)", menu)[1], 16)
        self.assertEqual(host_id, game_id)
        # The Hack Pack, the replay gate and voice mods own 0xFA; the host catches it first.
        taken = set()
        for path in ("port/app/source_host.cpp", "port/runtime/abi/mu_host.h"):
            text = source(path)
            taken |= {int(v, 16) for v in re.findall(r"constexpr uint8_t CMD_[A-Z0-9_]+ = (0x[0-9A-Fa-f]{2})", text)}
            taken |= {int(v, 16) for v in re.findall(r"#define MU_[A-Z0-9_]*COMMAND\s+(0x[0-9A-Fa-f]{2})", text)}
            taken |= {int(v, 16) for v in re.findall(r"\bc == (0x[0-9A-Fa-f]{2})", text)}
        self.assertNotIn(host_id, taken)

    def test_github_build_keeps_its_sys_files(self):
        manifest = json.loads(source("port/sys_deltas/manifest.json"))
        for folder in ("slippi_sys", "slippi_sys_general", "slippi_sys_playback"):
            for f in manifest["files"]:
                self.assertTrue((ROOT / "port" / folder / "GameFiles/GALE01" / f["target"]).is_file(), (folder, f["target"]))
        self.assertNotIn("sys_deltas", source("tools/package_linux_release.py"))

    def test_launcher_is_not_an_app_session(self):
        link = source("port/runtime/hle/yougame_link.cpp")
        self.assertIn('g_launcher = take("MELEE_YOUGAME_LAUNCHER") == "1" && !g_stdio;', link)
        self.assertIn("bool active() { return g_stdio || (!g_launcher && !g_bridge_url.empty()", link)
        self.assertIn("bool session() { return active() || (launcher() && g_launcher_session.load()); }", link)
        online = source("port/runtime/hle/slippi_online.cpp")
        # Slippi's account, start-up and the app's friends page stay on the app check.
        for kept in ("if (!yougame::active()) report::init(", "case CMD_LOGOUT: if (!yougame::active())",
                     "if (yougame::active()) { yougame::refresh_identity(); return true; }"):
            self.assertIn(kept, online)
        # A YouGame match is reported through its room, a Slippi one to Slippi.
        self.assertIn("else if (!yougame::session()) report::log_game(r);", online)

    def test_build_message_order(self):
        net = source("port/runtime/hle/slippi_net.cpp")
        send = net.split("const std::string rules_digest = online::ranked_rules_digest", 1)[1].split("host::log(\"slippi: sending build", 1)[0]
        self.assertLess(send.index("shield_color_online()"), send.index("for (char ch : rules_digest)"))
        read = net.split("// Their own shield colour, which builds before it do not send.", 1)[1].split("remote_build_[pidx] =", 1)[0]
        self.assertLess(read.index("has_shield"), read.index("if (caps & 8)"))

    def test_host_api_and_practice_ops(self):
        api = source("port/runtime/abi/mu_host.h")
        self.assertIn("#define MU_HOST_API_VERSION 23", api)
        self.assertLess(api.index("screen_shake_scale"), api.index("card_boot_confirm"))
        self.assertIn("MU_PRACTICE_PAGE_PAUSE = 15", api)
        self.assertIn("mu_host->version >= 23 && mu_host->card_boot_confirm", source("sourceport/game/shim/mu_card.c"))
        self.assertIn("if (op == 15) return mu_yougame_page_pause(a, n);", source("sourceport/game/shim/mu_noslippi.c"))

    def test_split_menu_rows(self):
        menu = source("sourceport/game/shim/mu_slippi_menu.c")
        self.assertIn("{ MU_SLP_MODE_RANKED, MU_SLP_MODE_UNRANKED, MU_SLP_MODE_DIRECT, -1,", menu)
        self.assertIn("{ 0x645, 0x646, 0x647, 0x644, 0x646, 0x647, 0x64B, 0x64C }", menu)
        rows = source("port/runtime/hle/online_rows.cpp")
        # Friends and Slippi set from the menu's own letters; Slippi's own four labels moved unchanged.
        for line in ("kRankedLine", "kUnrankedLine", "kFriendsLine", "kSlippiWord", "make_y(", "make_f(", "std::memcpy(base + data[63], unranked.data(), kBytes);",
                     "std::memcpy(base + data[59], direct.data(), kBytes);", "std::memcpy(base + data[60], teams.data(), kBytes);",
                     "std::memcpy(base + data[61], party.data(), kBytes);"):
            self.assertIn(line, rows)
        host = source("port/app/source_host.cpp")
        self.assertIn("online_rows::apply(data, &why, yougame::coming_soon())", host)


if __name__ == "__main__":
    unittest.main()
