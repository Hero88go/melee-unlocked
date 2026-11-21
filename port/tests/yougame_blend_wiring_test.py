"""Blended Unranked's wiring (yougame_blend.h): which searches blend, agents never reaching Slippi,
and both sides ending cleanly. Source checks only; no game, network or build."""
from pathlib import Path
import re
import unittest

from relay_pads_wiring_test import block, code, function

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def in_order(test, text, steps):
    at = 0
    for step in steps:
        found = text.find(step, at)
        test.assertGreaterEqual(found, 0, step)
        at = found + len(step)


class BlendWiringTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw_net = source("port/runtime/hle/slippi_net.cpp")
        cls.net = code(cls.raw_net)
        cls.raw_link = source("port/runtime/hle/yougame_link.cpp")
        cls.link = code(cls.raw_link)
        cls.search = function(cls.net, "startMatchmaking")
        cls.blend = function(cls.net, "blendMatchmaking")
        cls.ticket = function(cls.net, "openSlippiTicket")
        cls.find = function(cls.link, "find_match")

    def test_only_the_apps_plain_unranked_on_the_base_listing_blends(self):
        # Two ways in: the app's routing block, after the policy says Blend, and the launcher's Both
        # Unranked (test_launcher_both_unranked_blends_with_the_games_own_login).
        self.assertEqual(len(re.findall(r"\bblendMatchmaking\(\);", self.net)), 2)
        app = self.search.split("if (yougame::active() && !local_peer.enabled) {", 1)[1]
        app = app[:app.index("UserInfo me =")]
        self.assertIn("blendMatchmaking();", app)
        route = app.index("yougame::mode_policy::matchmaking_route(")
        call = app[route:app.index(";", route)]
        for arg in ("search_settings_.mode, true, login, agent,", "!search_settings_.friend_room.empty(), vanilla"):
            self.assertIn(arg, call)
        yougame = app.split("if (route == yougame::mode_policy::MatchmakingRoute::YouGame) {", 1)[1]
        refused = block(yougame, 0)
        self.assertIn("startYouGameMatchmaking(", refused)
        self.assertIn("return;", refused)
        blend = app.split("if (route == yougame::mode_policy::MatchmakingRoute::Blend) {", 1)[1]
        self.assertIn("blendMatchmaking();", block(blend, 0))
        self.assertIn("const bool unranked = search_settings_.mode == UNRANKED && search_settings_.friend_room.empty();", app)
        self.assertIn("const bool vanilla = host::gameplay::vanilla_only();", app)
        self.assertIn("if ((slippi_mode || (unranked && vanilla)) && user_)", app)
        self.assertIn("slippi_user_ = User::GrantedSlippiUser(user_->dir());", app)
        self.assertNotIn("std::make_unique<User>", app)
        # The policy keeps Blend exclusive to plain Unranked. Eligible Direct is Slippi alone.
        policy = code(source("port/runtime/hle/yougame_mode_policy.h"))
        route_fn = function(policy, "matchmaking_route")
        self.assertIn("if (agent_script) return MatchmakingRoute::YouGame;", route_fn)
        self.assertIn("if (!yougame_mode) return MatchmakingRoute::Slippi;", route_fn)
        self.assertIn("return mode == 1 && slippi_login && !friend_room && vanilla ? MatchmakingRoute::Blend : MatchmakingRoute::YouGame;",
                      route_fn)

    def test_launcher_both_unranked_blends_with_the_games_own_login(self):
        # Melee Unlocked's launcher (no app): the menu's pick decides; only Both with a Slippi login blends.
        opener = "if (!yougame::active() && yougame::session() && !local_peer.enabled) {"
        launcher = block(self.search, self.search.index(opener) + len(opener))
        in_order(self, launcher, ["yougame::mode_policy::launcher_route(yougame::menu_service(), (int)search_settings_.mode,",
                                  "slippi_login, search_settings_.slippi_gameplay, host::test_env::agent_copy());",
                                  "if (both.route == yougame::mode_policy::LauncherRoute::YouGame) {",
                                  "startYouGameMatchmaking();", "return;",
                                  "slippi_user_ = User::SearchCopy(*user_);",
                                  "if (both.route != yougame::mode_policy::LauncherRoute::Blend) {",
                                  "slippi_user_.reset();", "startYouGameMatchmaking(yougame::blend::kSearchYouGameOnly);", "return;",
                                  "blendMatchmaking();", "return;"])
        self.assertNotIn("GrantedSlippiUser", launcher)
        self.assertNotIn("std::make_unique<User>", launcher)
        self.assertNotIn("openSlippiTicket(", launcher)
        # Agent scripts return to YouGame's search before it; agent copies never blend (the policy and the blend).
        self.assertLess(self.search.index("if (yougame::agent_test::agent_script) {"), self.search.index("launcher_route("))
        # The dry run (tests) is decided before the blend and never enters Slippi's side.
        raw = self.raw_net[self.raw_net.index("void Matchmaking::startMatchmaking() {"):]
        raw = raw[:raw.index("blendMatchmaking();")]
        in_order(self, raw, ['host::test_env::getenv("MELEE_TEST_BOTH_DRY_RUN")', "test dry run: Slippi not entered",
                             'both = {yougame::mode_policy::LauncherRoute::BothYouGameOnly, "test dry run"};',
                             '"yougame: Both Unranked searches YouGame only (%s)"',
                             '"yougame: Both Unranked searches YouGame and Slippi (%s %s)"'])
        # The search's login is a copy of the menu's: no file, no discovery, no server refresh.
        copy = self.net[self.net.index("User::User(Copy, const User& from)"):self.net.index("std::unique_ptr<User> User::SearchCopy(")]
        for call in ("AttemptLogin", "RefreshFromServer", "resolve(", "read("):
            self.assertNotIn(call, copy)
        # The policy: Both on plain Unranked only, YouGame alone without a login or with mods.
        policy = function(code(source("port/runtime/hle/yougame_mode_policy.h")), "launcher_route")
        in_order(self, policy, ["if (service != 2 || mode != 1 || friend_room) return {LauncherRoute::YouGame,",
                                "if (agent) return {LauncherRoute::BothYouGameOnly,",
                                "if (!slippi_login) return {LauncherRoute::BothYouGameOnly,",
                                "if (!slippi_gameplay) return {LauncherRoute::BothYouGameOnly,",
                                "return {LauncherRoute::Blend,"])
        # The online menu's search says whether Slippi's Unranked plays this game's gameplay.
        online = code(source("port/runtime/hle/slippi_online.cpp"))
        in_order(self, online, ["local_matchmaking_error((int)search.mode)", "search.slippi_gameplay = !g_local_build.mod_view &&",
                                "!native_profile_mode_error(effective_profile(), (int)Matchmaking::UNRANKED, false);"])

    def test_agent_scripts_never_reach_slippi(self):
        # The hook's recheck still returns to YouGame's search before any routing.
        hook = self.search.index("if (yougame::agent_test::agent_script) {")
        agent = block(self.search, hook + len("if (yougame::agent_test::agent_script) {"))
        self.assertIn("startYouGameMatchmaking();", agent)
        self.assertIn("return;", agent)
        self.assertLess(hook, self.search.index("blendMatchmaking();"))
        self.assertLess(hook, self.search.index("openSlippiTicket("))
        # The routing call is told about agents too.
        self.assertRegex(self.search, r"#ifdef YOUGAME_AGENT_TEST_HOOK\s+const bool agent = yougame::agent_test::agent_script;")
        # The blend and the Slippi ticket each refuse an agent before any socket or thread.
        self.assertRegex(self.blend, r"^\s*if \(host::test_env::agent_copy\(\)\) \{[^\n]*\n\s*slippi_user_.reset\(\);\s*"
                                     r"yougame::slippi_link\(false, std::string\(\)\);\s*"
                                     r"startYouGameMatchmaking\(yougame::blend::kSearchYouGameOnly\);\s*return;\s*\}\s*"
                                     r"using yougame::blend::Side;\s*#ifdef YOUGAME_AGENT_TEST_HOOK\s+"
                                     r"if \(yougame::agent_test::agent_script\) \{ fail\(\s*\); return; \}")
        self.assertLess(self.blend.index("agent_script"), self.blend.index("std::async("))
        self.assertRegex(self.ticket, r"^\s*if \(agent_lock::blocked\(agent_lock::Operation::Matchmaking\)\) return agent_lock::refusal;\s*"
                                      r"#ifdef YOUGAME_AGENT_TEST_HOOK\s+if \(yougame::agent_test::agent_script\) return\s*;")
        self.assertLess(self.ticket.index("agent_script"), self.ticket.index("enet_host_create("))

    def test_slippis_client_is_native_beside_youGames_links(self):
        self.assertIn("if (yougame::active()) yougame::with_native_udp(create);", self.ticket)
        native = self.ticket.split("native([&] {", 1)[1]
        self.assertLess(native.index("enet_host_create("), native.index("});"))
        self.assertIn("native([&] { lan = local_address_string(&addr); });", self.ticket)
        scope = function(self.link, "with_native_udp")
        self.assertRegex(scope, r"^\s*std::lock_guard<std::mutex> lock\(g_mutex\);")
        self.assertLess(scope.index("yg_enet_select(0);"), scope.index("create();"))
        self.assertEqual(scope.count("yg_enet_select(was);"), 2)   # also when create throws

    def test_youGame_commit_ends_slippi_first_and_takes_its_own_port(self):
        commit = self.blend.split("if (committed && won == Side::YouGame) {", 1)[1]
        commit = block(commit, 0)
        in_order(self, commit, ["dropSlippi();", "yg.result.get();", "if (!searching()) return;", "std::move(slippi_user_)",
                                "host_port_ = yg_port;", "takeYouGameMatch(match);", "return;"])
        take = function(self.net, "takeYouGameMatch")
        self.assertLess(take.index("display_provider_ = display::Provider::YouGame;"), take.index("state_ = OPPONENT_CONNECTING;"))
        # A failed committed YouGame match does what YouGame's search always did.
        self.assertIn("state_ = INITIALIZING;", take)
        self.assertIn("fail(match.error);", take)

    def test_slippi_commit_starts_peer_before_joining_helper(self):
        commit = self.blend.split("won_slippi = coord.matched(Side::Slippi, time_ms());", 1)[1]
        in_order(self, commit, ["dropSlippi();", "yg.quit = true;", "if (!searching()) return;",
                                "yougame::prepare_slippi_match(", "display_provider_ = display::Provider::Slippi;",
                                "ingest_ticket(Ticket{resp})", "state_ = OPPONENT_CONNECTING;", "handleConnecting();"])
        # A refused or blocked ticket never reaches the coordinator.
        queued = self.blend.split("if (slippi == Slippi::Queued) {", 1)[1]
        self.assertLess(queued.index("parse_ticket(resp)"), queued.index("coord.matched(Side::Slippi"))
        self.assertLess(queued.index("IsBlockedMatch("), queued.index("coord.matched(Side::Slippi"))
        # Slippi's version is polled, never waited on, and a refusal stops only Slippi's side.
        check = self.blend.split("slippi_user_->PollOnlineCompatibility(&error)", 1)[1]
        refusal = check.split("if (version < 0) {", 1)[1].split("}", 1)[0]
        self.assertIn("slippi_failed(", refusal)
        self.assertNotIn("CheckOnlineCompatibility", self.blend)
        self.assertLess(check.index("if (version < 0)"), check.index("openSlippiTicket("))

    def test_every_way_out_ends_both_sides(self):
        # YouGame's side is joined by its holder's destructor, declared after what it uses.
        helper = self.blend.split("struct Helper {", 1)[1].split("} yg;", 1)[0]
        self.assertIn("~Helper() { quit = true; if (result.valid()) result.wait(); }", helper)
        self.assertLess(self.blend.index("yougame::blend::Coordinator coord("), self.blend.index("struct Helper {"))
        self.assertLess(self.blend.index("yougame::Search search;"), self.blend.index("struct Helper {"))
        self.assertIn("auto match = yougame::find_match(search, [&] { return yg.quit || !searching() || !live(Side::YouGame); });",
                      self.blend)
        # The player leaving stops the coordinator and Slippi's client before returning.
        left = self.blend.split("if (!searching()) {", 1)[1].split("return;", 1)[0]
        self.assertIn("coord.stop();", left)
        self.assertIn("dropSlippi();", left)
        self.assertIn("dropSlippi();", self.blend.split("if (over) {", 1)[1].split("return;", 1)[0])
        # Slippi's blocking steps stop when it is cancelled.
        self.assertIn("if (stop && stop()) return", self.ticket)
        self.assertIn("mm_receive(client_, resp, 5000, stop)", self.ticket)
        self.assertIn("mm_receive(client_, resp, 250, slippi_stop)", self.blend)
        receive = function(self.net, "mm_receive")
        self.assertRegex(receive, r"for \(int i = 0; i < attempts; \+\+i\) \{\s*if \(stop && stop\(\)\) return -1;")
        # A Slippi connection still being made is reset, not waited out.
        drop = function(self.net, "dropSlippi")
        self.assertLess(drop.index("enet_peer_reset(server_);"), drop.index("terminateMmConnection();"))

    def test_youGame_queue_reports_and_asks_before_taking_a_room(self):
        find = self.find
        self.assertIn("const bool blend = static_cast<bool>(search.claim);", find)
        queue = find.split("if (queued) {", 1)[1]
        matched = queue.index("room_code = m[")
        self.assertLess(matched, queue.index("if (blend && !search.claim()) { decline();"))
        self.assertIn("search.on_searching((int)std::clamp<int64_t>(m[", queue)
        # A room straight from the ticket is asked about too.
        other = find.split("} else {\n    if (blend && !search.claim()) { decline();", 1)
        self.assertEqual(len(other), 2)
        # Cancelled, it leaves the queue at once; the ticket wait stops with it.
        cancelled = queue.split("if (cancelled()) {", 1)[1].split("return fail(", 1)[0]
        self.assertIn("queue.send(", cancelled)
        self.assertIn("ticket = ticket_bridge(request, 30000);", find)
        self.assertIn("const auto ticket_bridge = [&](json q, int ms) { return bridge(transport::with_links_flag(q), ms, cancelled); };",
                      find)
        # An Unranked queue room nobody else arrives in is left (retry), not waited in forever.
        self.assertIn("search_policy::no_show_deadline(host::gameplay::vanilla_only(), queued,", find)
        self.assertIn("search.mode, friends_mode || team_code, now_ms());", find)
        self.assertIn("search_policy::no_show_expired(arrive_by, arrived, now_ms())", find)
        self.assertIn('"searching"', self.raw_link)
        self.assertIn('"Your opponent did not arrive", true', self.raw_link)
        self.assertIn("after 30 s; searching again", self.raw_link)

    def test_resolution_stays_inside_native_scope_and_failure_stops_only_slippi(self):
        # Slippi 3.7.0 resolves the STUN servers too: every lookup stays inside a native() scope.
        native = "".join(block.split("});", 1)[0] for block in self.ticket.split("native([&] {")[1:])
        self.assertIn('enet_address_set_host(&addr,', native)
        self.assertIn('enet_address_set_host(&a, server.first)', native)
        self.assertEqual(native.count('enet_address_set_host('), self.ticket.count('enet_address_set_host('))
        self.assertIn("ENetAddress addr{};", self.ticket)
        self.assertIn('could not resolve the Slippi server', self.raw_net)
        self.assertIn("if (!resolved) return", self.ticket)

    def test_generation_reserved_before_helper_and_fences_global_state(self):
        self.assertLess(self.blend.index("search.epoch = yougame::begin_search();"), self.blend.index("std::async("))
        self.assertIn("search.epoch ? search.epoch : begin_search(search.friends_lobby)", self.find)
        self.assertIn("!g_search_generation.current(epoch) || cancel_requested()", self.find)
        self.assertIn("if (search_stopped()) return;", function(self.link, "set_status"))
        self.assertIn("if (search_stopped()) return m;", function(self.link, "fail"))
        self.assertIn("fetch_identity(cancelled)", self.find)
        self.assertIn("if (search_stopped()) return id;", self.link[self.link.index("Identity fetch_identity("):self.link.index("namespace relay_pads", self.link.index("Identity fetch_identity("))])

    def test_all_tickets_share_busy_retry_and_cancel_wait(self):
        bridge = self.link[self.link.index("json bridge("):self.link.index("class Socket")]
        self.assertIn("g_ticket_retry ? *g_ticket_retry : local_retry", bridge)
        self.assertIn('retry.delay(j)', bridge)
        self.assertIn("search_policy::wait(", bridge)
        self.assertIn("yougame: %s, retrying the ticket in %d s (try %d/3)", self.raw_link)
        self.assertIn('"the app has no page to ask yet" : "site busy"', self.raw_link)
        self.assertIn("search_policy::blend_port((uint16_t)forced_port, rng())", self.blend)
        self.assertNotIn("yg.result.wait();", self.blend.split("won_slippi = coord.matched", 1)[1])

    def test_mac_granted_slippi_port_is_forced_and_never_random(self):
        # The Mac app's sandbox grants one UDP port (YOUGAME_SLIPPI_UDP_PORT); --netplay-port wins.
        env = function(self.net, "slippi_port_from_env")
        in_order(self, env, ["std::getenv(", "n < 41000 || n > 50999", "return (uint16_t)n;"])
        self.assertIn('std::getenv("YOUGAME_SLIPPI_UDP_PORT")', self.raw_net)
        ctor = self.net[self.net.index("Matchmaking::Matchmaking(User* user)"):self.net.index("Matchmaking::~Matchmaking()")]
        in_order(self, ctor, ["if (!forced_port && env_port)", "forced_port = env_port;"])
        self.assertIn('"slippi: forced port %u from YOUGAME_SLIPPI_UDP_PORT"', self.raw_net)
        ticket = function(self.net, "openSlippiTicket")
        in_order(self, ticket, ["search_policy::native_port((uint16_t)forced_port", "if (forced_port && host_port_ == forced_port)",
                                "if (!client_) return"])
        self.assertIn('"slippi: forced port %d bound" : "slippi: forced port %d bind failed"', self.raw_net)
        # A forced port is never swapped for a random one on the YouGame-only path either.
        # Main's own port override (a player-set Slippi port) comes first; a forced port is still never random.
        self.assertIn("host_port_ = port_override_ ? port_override_ : forced_port ? forced_port : 41000 + (int)(rng() % 10000);", self.net)

    def test_coordinator_is_pure_and_logs_the_agreed_lines(self):
        header = source("port/runtime/hle/yougame_blend.h")
        includes = re.findall(r'#include ([<"][^>"]+[>"])', header)
        self.assertEqual(sorted(includes), ["<cstdint>", "<string>", "<utility>", "<vector>"])
        for text in ('"yougame: unranked blend: "', '"YouGame queue waiting "', '" -> YouGame first"', '" -> both at once"',
                     '"Slippi search started after "', '"matched on "', '"cancelled the other"', '"declined a late "'):
            self.assertIn(text, header)
        for text in ('host::log("slippi: no usable login (%s)", why);', 'host::log("slippi: app login file found");',
                     'host::log("slippi: app login file not found (%s)", why);'):
            self.assertIn(text, self.raw_net)
        account = source("port/runtime/hle/slippi_account.h")
        for reason in ('"file missing"', '"unreadable"', '"no connect code"'):
            self.assertIn(reason, account)

    def test_app_login_file_is_the_only_one_read(self):
        account = code(source("port/runtime/hle/slippi_account.h"))
        resolve = function(account, "resolve")
        self.assertRegex(resolve, r"^\s*if \(host::test_env::agent_copy\(\)\) return \{\};\s*if \(const auto app = app_sign_in\(\); !app.empty\(\)\) return read\(app\);")
        unusable = function(account, "why_unusable")
        self.assertIn("app.empty() ? sign_in_files(local_user_dir, roots) : std::vector<std::filesystem::path>{app}", unusable)
        # Read only: nothing in the account code writes a file.
        self.assertNotIn("ofstream", account)
        self.assertIn('YOUGAME_SLIPPI_USER_JSON', source("port/runtime/hle/slippi_account.h"))

    def test_app_login_diagnostics_and_startup_never_discover(self):
        from yougame_release_wiring_test import literal_function
        login = literal_function(self.raw_net, "AttemptLogin")
        app = login.split("if (yougame::active()) {", 1)[1].split("} else if", 1)[0]
        self.assertIn("const auto grant = account::app_sign_in();", app)
        self.assertIn("if (!grant.empty()) profile = account::read(grant);", app)
        self.assertNotIn("resolve(", app)
        self.assertNotIn("account_file_", app)
        announce = function(self.net, "announce_slippi_link")
        self.assertIn("User::GrantedSlippiUser(user_dir)", announce)
        self.assertNotIn("User user(", announce)
        self.assertIn("account::why_unusable_grant()", self.search)
        self.assertNotIn("account::why_unusable(", self.search)

    def test_cancel_after_account_load_precedes_state_and_generation(self):
        in_order(self, self.search, ["User::GrantedSlippiUser(user_->dir());",
                                   "if (is_mm_terminated_ || state_ != INITIALIZING) return;",
                                   "const bool login =", "blendMatchmaking();"])
        in_order(self, self.blend, ["if (is_mm_terminated_ || state_ != INITIALIZING) return;",
                                  "state_ = MATCHMAKING;", "search.epoch = yougame::begin_search();"])

    def test_consumed_assignment_is_declined_if_cancelled_during_next(self):
        from yougame_release_wiring_test import literal_function
        find = literal_function(self.raw_link, "find_match")
        after_next = find.split("if (!queue.next(m, 250)) {", 1)[1]
        cancelled = after_next[after_next.index('if (cancelled()) {'):]
        in_order(self, cancelled, ['m.value("t", "") == "matched"', 'room_code = m[', 'decline();', 'return fail("Cancelled");'])

    def test_link_and_search_lines_only_for_the_base_listing(self):
        app = self.search.split("if (yougame::active() && !local_peer.enabled) {", 1)[1]
        app = app[:app.index("UserInfo me =")]
        known = app.split("if (unranked && vanilla && user_) {", 1)
        self.assertEqual(len(known), 2)
        decided = block(known[1], 0)
        self.assertIn("yougame::slippi_link(login, login ? slippi_user_->GetUserInfo().connect_code : std::string());", decided)
        self.assertEqual(self.net.count("yougame::slippi_link("), 4)
        # Agent copies send "not linked" only, on the base listing's Unranked (the blend is reached only there).
        self.assertEqual(self.net.count("yougame::slippi_link(false, std::string());"), 2)
        self.assertIn("if (vanilla_unranked) yougame::slippi_link(false, std::string());", self.search)
        self.assertIn("search_settings_.friend_room.empty() && host::gameplay::vanilla_only();", self.search)
        # Also once when the online menu first asks, off the game thread, base listing only.
        announce = function(self.net, "announce_slippi_link")
        self.assertRegex(announce, r"if \(!yougame::active\(\) \|\| !host::gameplay::vanilla_only\(\) \|\| asked.exchange\(true\)\) return;")
        self.assertLess(announce.index("std::thread("), announce.index("yougame::slippi_link("))
        self.assertIn("announce_slippi_link(g_user->dir());", code(source("port/runtime/hle/slippi_online.cpp")))
        self.assertLess(app.index("const bool login ="), app.index("yougame::slippi_link("))
        self.assertIn("startYouGameMatchmaking(unranked && vanilla ? yougame::blend::kSearchYouGameOnly : nullptr);", app)
        self.assertIn("search.status = yougame::blend::kSearchBoth;", self.blend)
        self.assertIn("if (status) search.status = status;", function(self.net, "startYouGameMatchmaking"))
        self.assertIn("const std::string searching = search.status.empty() ? std::string( ) : search.status;", self.find)
        self.assertIn("set_status(team_code ? friends::kTeamWaiting : friends_mode ? friends::kWaiting :", self.find)
        self.assertIn("repeated_link_failure ? search_policy::kLinkRetry : searching", self.find)
        link = function(self.link, "slippi_link")
        self.assertIn("const std::string line = blend::slippi_link_line(linked, tag);", link)
        self.assertIn("if (line == sent) return;", link)
        self.assertIn("if (g_stdio) write_line(line);", link)
        overlay = code(source("port/runtime/gx/pc_settings.cpp"))
        self.assertIn("status == yougame::blend::kSearchBoth || status == yougame::blend::kSearchYouGameOnly", overlay)

    def test_tests_are_registered_before_linux_returns(self):
        root = source("CMakeLists.txt")
        stop = root.index("  return()")
        self.assertLess(root.index("add_executable(port_yougame_blend_test port/tests/yougame_blend_test.cpp)"), stop)
        self.assertLess(root.index("add_test(NAME port_yougame_blend COMMAND port_yougame_blend_test)"), stop)
        self.assertLess(root.index("add_test(NAME port_yougame_blend_wiring "), stop)
        self.assertIn("'port_yougame_blend_test'", source("tools/build_linux.py"))


if __name__ == "__main__":
    unittest.main()
