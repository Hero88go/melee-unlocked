"""Engine publication wiring for platform hosts (no app or game launch)."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class LocalBuildEngineWiringTest(unittest.TestCase):
    def test_static_startup_precedes_bridge(self):
        main = source("port/app/main.cpp").split("static int melee_main(int argc, char** argv) {", 1)[1]
        startup = main.split("yougame::start();", 1)[0]
        static = startup.rsplit("#ifndef MELEE_SOURCE_PORT", 1)[1].split("#endif", 1)[0]
        self.assertIn("build.static_engine = true;", static)
        self.assertIn("slippi::online::set_local_build(build);", static)

    def test_every_static_build_replacement_retains_engine(self):
        host = source("port/runtime/host/host.cpp")
        builds = re.findall(r"slippi::online::LocalBuild build;(.*?)slippi::online::set_local_build\(build\);", host, re.S)
        self.assertEqual(len(builds), 2)  # clean mod and verified online mod
        for build in builds:
            self.assertIn("build.static_engine = true;", build)
        self.assertEqual(host.count("slippi::online::set_local_build("), len(builds))

    def test_source_content_views_keep_default_engine(self):
        host = source("port/app/source_host.cpp")
        factory = host.split("slippi::online::LocalBuild content_build_for_mode(int mode) {", 1)[1].split("return build;", 1)[0]
        self.assertIn("slippi::online::LocalBuild build;", factory)
        self.assertNotIn("build.static_engine = true", host)
        self.assertIn("set_local_build(content_build_for_mode(mode))", host)
        self.assertIn("const auto build = content_build_for_mode(mode);", host)

    def test_shared_room_key_reads_engine(self):
        link = source("port/runtime/hle/yougame_link.cpp")
        self.assertIn("compatibility_key(v, build.mod_view, build.fingerprint, build.static_engine,\n"
                      "                           slippi::online::ranked_rules_digest(mode))", link)
        self.assertIn("const std::string compat = compatibility(search.mode);", link)

    def test_peer_engine_uses_ignored_capability_bit(self):
        net = source("port/runtime/hle/slippi_net.cpp")
        self.assertIn("(early ? 1 : 0) | 2 | (me.static_engine ? 4 : 0)", net)
        self.assertIn("(caps & 1) != 0, (caps & 2) != 0, (caps & 4) != 0", net)
        self.assertIn("if (!(packet >> caps)) caps = 0;", net)
        self.assertIn("version != 1", net)


if __name__ == "__main__":
    unittest.main()
