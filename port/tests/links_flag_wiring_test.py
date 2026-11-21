"""links_flag 1 rides every mpTicket and mpFriend room hold a search asks for on Windows (app-started or not), where
this copy can share a match with a links:false seat over own UDP; never on other systems, never on invites or any
other mpFriend, and never on the console session's tickets, which have no links:false fallback. The flag itself:
yougame_transport_test.cpp."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class LinksFlagWiring(unittest.TestCase):
    def setUp(self):
        link = read("port/runtime/hle/yougame_link.cpp")
        start = link.index("Match find_match(const Search& search")
        self.find = link[start:link.index("\n}\n", start)]
        self.transport = read("port/runtime/hle/yougame_transport.h")

    def test_only_windows_copies_send_it(self):
        flag = self.transport[self.transport.index("#ifdef _WIN32\ninline constexpr bool kLinksFlag"):]
        flag = flag[:flag.index("#endif")]
        self.assertIn("inline constexpr bool kLinksFlag = true;", flag.split("#else")[0])
        self.assertIn("inline constexpr bool kLinksFlag = false;", flag.split("#else")[1])
        helper = self.transport[self.transport.index("inline json& with_links_flag("):]
        helper = helper[:helper.index("\n}\n")]
        self.assertIn('if (type->get<std::string>() == "mpTicket" || is_hold) request["links_flag"] = 1;', helper)
        self.assertIn('type->get<std::string>() == "mpFriend" && hold != request.end() && hold->is_boolean()', helper)

    def test_every_search_ticket_goes_through_the_flag(self):
        self.assertIn("const auto ticket_bridge = [&](json q, int ms) { return bridge(transport::with_links_flag(q), ms, cancelled); };",
                      self.find)
        for sent in ("ticket = ticket_bridge(request, 30000);", "return ticket_bridge(request, (int)std::min<uint64_t>(3000, until - now));",
                     "const json qt = ticket_bridge(queue_request, 15000);", "auto answer = ticket_bridge(q, ms);",
                     "ticket = ticket_bridge(doubles_home ? friends::doubles_hold(lobby_key) : friends::lobby_hold(lobby_key), 30000);"):
            self.assertIn(sent, self.find)
        # No ticket request leaves the search without it (the plain bridge calls left are directOpen/Close/Attach).
        self.assertIsNone(re.search(r"(?<![_a-z])bridge\((request, |queue_request, |doubles_home \? )", self.find))
        picker = self.find[self.find.index("ticket = friends::request_ticket("):]
        self.assertIn("auto answer = ticket_bridge(q, ms);", picker[:picker.index("cancelled,")])

    def test_console_tickets_never_carry_it(self):
        for path in ("port/runtime/hle/yougame_console_link.cpp", "port/runtime/hle/yougame_friends.h",
                     "port/runtime/hle/yougame_doubles.h"):
            self.assertNotIn("links_flag", read(path), path)


if __name__ == "__main__":
    unittest.main()
