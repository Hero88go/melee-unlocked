"""Host/game isolation boundaries which the window-free engine fake cannot execute."""
from pathlib import Path
import re
import subprocess
import shutil
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def body(text, name):
    # Balanced braces, excluding comments and strings from the brace scan.
    mask = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/',
                  lambda m: ' ' * len(m[0]), text, flags=re.S)
    # A definition starts its line; a call such as "switch (g_warmup.tick(o)) {" does not. Fall back to
    # the loose form for definitions written indented (inside a namespace or class).
    match = (re.search(r'(?m)^[A-Za-z_][^\n;=]*?\b' + name + r'\([^;]*?\)\s*(?:const\s*)?\{', mask) or
             re.search(r'\b' + name + r'\([^;]*?\)\s*\{', mask))
    if not match:
        raise AssertionError('Missing function: ' + name)
    depth, at = 1, match.end()
    while depth:
        depth += (mask[at] == '{') - (mask[at] == '}')
        at += 1
    return text[match.end():at - 1]


class ConsoleWiring(unittest.TestCase):
    def setUp(self):
        self.host = read('port/app/source_host.cpp')

    def test_boot_intake_precedes_source_run_and_uses_window_only_pump(self):
        main = read('port/app/main.cpp')
        at = main.index('yougame::console::boot(')
        self.assertLess(main.index('yougame::wait_identity(10000)'), at)
        self.assertLess(main.index('yougame::console::take_pending_launch()'), at)
        self.assertLess(at, main.index('source_port::run(shutdown)'))
        boot = main[at:main.index('source_port::run(shutdown)')]
        for text in ('take_request()', 'start_session(q, c, e)', 'console_begin(std::move(link), c)',
                     'console_boot_ready()', 'host::window_pump()', 'screen_label::show'):
            self.assertIn(text, boot)
        self.assertNotIn('host::retrace()', boot)
        helper = read('port/runtime/hle/yougame_console_boot.h')
        self.assertLess(helper.index('ready();'), helper.index('auto link = responsive_wait('))

    def test_app_route_caps_and_rejoin_metadata_reach_console(self):
        link = read('port/runtime/hle/yougame_link.cpp')
        self.assertIn('console::direct_notice(push)', link)
        self.assertIn('console::read_caps(j, g_relaunch)', link)
        self.assertIn('console::read_caps(hello, g_relaunch)', link)
        bar = (ROOT / 'port/runtime/gx/replay_bar.cpp').read_text()
        self.assertIn('page_lobby_lines.load(std::memory_order_acquire) ? std::string()', bar)
        practice = (ROOT / 'port/runtime/hle/native_practice.cpp').read_text()
        self.assertIn('!leave_line.empty() && !yougame::friends::page_lobby_lines.load', practice)
        self.assertIn('broker_->open(ticket, room, timeout, session)', link)
        stream = read('port/runtime/hle/yougame_transport.h')
        self.assertIn('if (!session.empty()) q["session"] = session;', stream)

    def test_relaunch_drop_is_an_atomic_matching_session_signal(self):
        production = read('port/runtime/hle/yougame_link.cpp')
        entry = body(production, 'friends_entry')
        policy = read('port/runtime/hle/yougame_console_entry.h')
        self.assertIn('std::atomic_load(&g_relaunch_wait)', production)
        self.assertIn('wait->push(push)', production)
        self.assertLess(entry.index('std::atomic_store(&g_relaunch_wait,relaunch_wait)'), entry.index('prepared = handoff('))
        self.assertIn('json m; b->next(m,50);', entry)
        self.assertLess(entry.index('relaunch_wait->reason()'), entry.index('relaunch_wait->timed_out(epoch_now())'))
        dropped = entry[entry.index('if (relaunch_wait && relaunch_wait->reason())'):]
        self.assertIn('entry.status(kRelaunchDropped); break;', dropped)
        self.assertIn('console: relaunch dropped by the app: %s', dropped)
        self.assertIn('sessionId",64) != session', policy)
        self.assertIn('now - handed_at >= 45000', policy)
        self.assertIn("Couldn't start playing together. Press Play again.", policy)

    def test_live_relink_owns_membership_and_room_rejoin_publication(self):
        production = read('port/runtime/hle/yougame_link.cpp')
        backend = production[production.index('struct ConsoleBackend'):production.index('SessionIO console_io(')]
        self.assertIn('if (session_live && !handoff_pending && (type == "back" || type == "join" || type == "welcome"))', backend)
        self.assertIn('control.erase("players"); control.erase("participants");', backend)
        self.assertLess(backend.index('incoming.push_back(std::move(m))'), backend.index('if (type == "welcome") rejoining = false;'))
        self.assertIn('io.room_rejoining = [b] { return b->rejoining.load(); };', production)
        transport = read('port/runtime/hle/yougame_console_link.cpp')
        self.assertIn('link->open_peer(peer,budget())', transport)
        self.assertIn('reply = open_link(attempt->request,conn,peer,1000)', transport)
        engine = read('port/runtime/hle/yougame_console_engine.cpp')
        self.assertIn('recovering ? kReconnecting : kWaitingForFriend', engine)
        self.assertIn('hooks_.notice(notice)', engine)
        self.assertIn('liveness_clock() - it->second.since >= 8000', engine)

    def test_links_are_serviced_during_boot_and_loading_and_relaunch_has_no_leave(self):
        link = read('port/runtime/hle/yougame_console_link.cpp')
        production = read('port/runtime/hle/yougame_link.cpp')
        self.assertIn('io.background_service = true;', production)
        self.assertIn('link->service(); return link;', link)
        service = body(link, 'service')
        self.assertIn('prepare_service()', service)
        self.assertIn('activate_service()', service)
        self.assertIn('service_enabled_.store(true', body(link.replace('() noexcept', '()'), 'activate_service'))
        self.assertIn('pump(0)', body(read('port/runtime/hle/yougame_console_link.cpp'), 'prepare_service'))
        for forbidden in ('retrace(', 'advance(', 'poll('):
            self.assertNotIn(forbidden, service)
        engine = read('port/runtime/hle/yougame_console_engine.cpp')
        self.assertIn('link_->pump(0)', body(engine, 'poll'))
        backend = production[production.index('struct ConsoleBackend'):production.index('SessionIO console_io(')]
        self.assertIn('room->close(1000)', backend)
        self.assertNotIn('"leave"', body(backend, 'close'))
        self.assertIn('if (!handoff_pending) room->send({{"t","leave"}});', backend)
        self.assertIn('broker_->close(1000, code)', production)
        entry = body(production, 'friends_entry')
        # Explicit cancellation leaves; a retire owns cleanup after a page Leave.
        condition = 'if (!b->retiring && (!prepared || friends::lobby.cancelled() || entry.cancelled()))'
        self.assertIn(condition, entry)
        cleanup = entry[entry.index(condition):]
        self.assertIn('{"t","leave"}', cleanup)
        self.assertLess(cleanup.index('{"t","leave"}'), cleanup.index('b->close()'))
        self.assertIn('if (!b->retiring) b->close();', cleanup)
        self.assertNotIn('close_room(', entry)
        self.assertIn('str(m,"sessionId") == prepared_session', entry)
        self.assertIn('io.close = [] {};', entry)
        main = read('port/app/main.cpp')
        shutdown = main[main.index('  auto shutdown = [](int) {'):]
        self.assertLess(shutdown.index('console_engine().end("window closed")'),
                        shutdown.index('backend_at_exit->reset()'))
        self.assertIn('b->session = friends::text(request,"sessionId",64);', production)
        self.assertIn('open_session("",ticket,code,&error,ms,session)', backend)

    def test_handoff_leaves_savestate_exclusions_and_pad_padding_to_each_process(self):
        # Gate 1 (Linux test PC, 11-02): the only bytes that differed after an equal-frame load were host
        # state (sound synth, command queues) and PADStatus padding copied into gmMain_8046B108.
        host = read('port/app/source_host.cpp')
        start = body(host, 'handoff_start')
        self.assertIn('append_sound_driver_exclusions(exclusions);', start)
        self.assertIn('run.spans = native_savestate::subtract(std::vector<MuStateRegion>(regions, regions+count), exclusions);', start)
        self.assertIn('validate_keep(run.keep, whole, why)', start)
        self.assertIn('handoff_start(game_regions, region_count, exclusions, &api);', host)
        self.assertIn('append_sound_driver_exclusions(exclusions);', body(host, 'begin_native_savestates'))
        self.assertIn('memset(&status[i], 0, sizeof status[i]);', body(read('sourceport/game/shim/mu_pad.c'), 'PADRead'))

    def test_main_build_friends_default_uses_local_menus(self):
        policy = read('port/runtime/hle/' + 'yougame_mode_policy.h')
        self.assertIn('#define YOUGAME_FRIENDS_ROW 1', policy)
        self.assertIn('constexpr bool friends_shared_menus = false;', policy)
        practice = body(read('port/runtime/hle/native_practice.cpp'), 'void tick')
        # The console handoff stays compiled behind the policy; Friends joins as known peers.
        handoff = practice.index('yougame::console::friends_entry(room);')
        self.assertIn('if (yougame::mode_policy::friends_shared_menus && g_bridge) {', practice[handoff - 120:handoff])
        self.assertIn('slippi::online::native_start_match(2, {}, 0, 0, &error, room);', practice[handoff:])
        self.assertIn('yougame::friends::events.take_host()', practice)
        events = read('port/runtime/hle/yougame_friends.h')
        self.assertIn('friend_entry_ = visible && selected;', events)

    def test_body_boundary_replaces_queue_before_game_consumes_it(self):
        # mu_pad.c is the host side (mu_shim.h); mu_console_pad.c the game side (controller/VI headers):
        # the game's C library and the host's stdint collide (ssize_t) in one file.
        pad = read('sourceport/game/shim/mu_pad.c')
        game = read('sourceport/game/shim/mu_console_pad.c')
        self.assertNotIn('#include "mu_shim.h"', game)
        self.assertNotIn('controller.h', pad)
        self.assertIn('return mu_host->console_frame_begin ? mu_host->console_frame_begin() : 0;',
                      body(pad, 'mu_console_host_frame_begin'))
        hook = body(game, 'mu_console_frame_begin')
        self.assertLess(hook.index('mu_console_host_frame_begin()'), hook.index('mu_console_replace_raw()'))
        replace = body(game, 'mu_console_replace_raw')
        self.assertIn('p->qread = p->qwrite;', replace)
        self.assertIn('p->qcount = 0;', replace)
        self.assertIn('HSD_PadRenewRawStatus(0)', replace)
        self.assertIn('mu_console_frame_begin();',
                      body(read('sourceport/game/shim/mu_lab.c'), 'mu_te_frame_begin'))
        patch = read('sourceport/patches/melee-native.patch')
        start = patch.index('+            mu_te_frame_begin();')
        self.assertLess(start, patch.index('             lb_800198E0();', start))
        self.assertLess(start, patch.index('if (gm_801A46B8(0) || !gm_GetDbPauseFlag(0))', start))
        self.assertIn('if (!g_console_boot) return 0;', body(self.host, 'h_console_frame_begin'))

    def test_scene_gate_requests_one_retrace_before_one_body(self):
        gate = body(read('sourceport/game/shim/mu_online.c'), 'mu_online_engine_gate')
        self.assertLess(gate.index('mu_console_engine_gate(count)'), gate.index('if (!hooks_on())'))
        console = body(read('sourceport/game/shim/mu_console_pad.c'), 'mu_console_engine_gate')
        self.assertRegex(console, r'if \(!mu_console_active\(\)\)\s*return 0;')
        self.assertLess(console.index('return 0;'), console.index('HSD_PadLibData.qcount = 0'))
        self.assertLess(console.index('VIWaitForRetrace()'), console.index('*count = 1'))
        self.assertIn('VIGetRetraceCount() == body_retrace', console)
        self.assertIn('mu_console_replace_raw()', console)  # fallback when no pad alarm is armed
        patch = read('sourceport/patches/melee-native.patch')
        at = patch.index('+            pad_queue_count = lb_80019894();')
        self.assertLess(patch.index('mu_online_engine_gate(&pad_queue_count)', at),
                        patch.index('for (i = 0; i < pad_queue_count; i++)', at))

    def test_hardware_bypasses_local_game_mutators(self):
        sample = body(self.host, 'console_hardware')
        self.assertIn('host::input_read_console_devices(raw)', sample)
        self.assertNotIn('input_poll(', sample)
        devices = body(read('port/runtime/host/window.cpp'), 'input_read_console_devices')
        self.assertIn('out[i].err = -1;', devices)
        self.assertIn('debug.feeding[i].kind != DeviceKind::Keyboard', devices)
        self.assertIn('if (controller)', devices)
        pad = body(self.host, 'h_pad_read')
        boundary = pad[:pad.index('if (g_online_test_mode == 0)')]
        self.assertIn('if (g_console_boot)', boundary)
        self.assertIn('engine.read(engine.active() ? yougame::console::Pads{} : console_hardware()', boundary)
        self.assertIn('return;', boundary)
        for mutator in ('user_gecko::apply()', 'offline_delay::apply(', 'lcancel::apply('):
            self.assertNotIn(mutator, boundary)

    def test_clock_and_seed_are_session_gated(self):
        for name in ('h_ticks', 'h_boot_time', 'h_vi_retrace_count', 'h_vi_next_field'):
            self.assertIn('if (yougame::console::console_active())', body(self.host, name))
        ticks = body(self.host, 'h_ticks')
        self.assertIn('return host::cpu->tb;', ticks)
        self.assertIn('return host::console_epoch_ticks();', body(self.host, 'h_boot_time'))
        poll = body(self.host, 'h_poll')
        console = poll[poll.index('if (yougame::console::console_active())'):]
        console = console[:console.index('return;')]
        self.assertIn('engine.poll()', console)
        self.assertIn('engine.retrace_due() && !g_console_in_retrace', console)
        self.assertIn('fire_alarms(engine.clock_ticks())', console)
        self.assertNotIn('host::retrace_due()', console)
        retrace = body(self.host, 'native_retrace')
        self.assertLess(retrace.index('engine.service_retrace()'), retrace.index('fire_alarms(engine.alarm_ticks())'))
        self.assertLess(retrace.index('fire_alarms(engine.alarm_ticks())'), retrace.index('g_game.retrace()'))
        self.assertIn('retrace_count()', body(self.host, 'h_vi_retrace_count'))
        self.assertIn('retrace_count() & 1', body(self.host, 'h_vi_next_field'))
        entry = read('sourceport/game/shim/mu_entry.c')
        self.assertLess(body(entry, 'mu_run').index('mu_apply_seed(seed)'),
                        body(entry, 'mu_run').index('return main()'))
        pad = read('sourceport/game/shim/mu_pad.c')
        self.assertIn('!seeded && mu_host->console_seed', body(pad, 'mu_console_host_frame_begin'))

    def test_card_cannot_import_or_prepare_players_save(self):
        self.assertIn('if (yougame::console::console_active()) return;', body(self.host, 'apply_detected_mods'))
        self.assertIn('if (!g_console_boot) {\n    const fs::path ordinary', self.host)
        mount = body(self.host, 'card_mount_files')
        self.assertIn('g_console_boot ? g_console_card', mount)
        self.assertIn('if (g_console_boot) break;', mount)
        self.assertIn('if (!g_console_boot) host::backup_card_folder', mount)
        begin = body(self.host, 'begin_console')
        self.assertIn('g_game.run', begin)  # reject a late entry after game initialization
        self.assertIn('prepare_card(', begin)

    def test_friends_save_hint_and_stall_wiring(self):
        link = read('port/runtime/hle/yougame_link.cpp')
        self.assertIn('start_session({{"room",room},{"players",2},{"inputDelay",2}}', link)
        for call in ('host_save_reader.load(', 'stage_card(', 'load_card('):
            self.assertIn(call, link)
        # The runtime library is built without MELEE_SOURCE_PORT: the save read can't hide behind it.
        self.assertNotIn('MELEE_SOURCE_PORT', body(link, 'console_io'))
        self.assertIn('host_save_reader.store(&source_port::console_host_save', self.host)
        # 30.x seats (console1: no shared save, intro boot) must never share a session with these.
        self.assertEqual(body(link, 'console_io').count('+ "-console5";'), 1)
        self.assertIn('compatibility_key(room_version(),false,"",false) + "-console5";', body(link, 'console_io'))
        self.assertNotIn('-console1"', link)
        self.assertIn('atomic_load(&g_console_host_snapshot)', body(self.host, 'console_host_save'))
        self.assertIn('card_find("SuperSmashBros0110290334")', body(self.host, 'card_refresh_snapshot'))
        # The snapshot outlives Melee's unmount (Friends reads it at START); every card change refreshes it.
        self.assertIn('card_refresh_snapshot();', body(self.host, 'card_mount_files'))
        self.assertNotIn('g_console_host_snapshot', body(self.host, 'card_reset_mount'))
        for function in ('h_card_delete', 'h_card_rename', 'h_card_format'):
            self.assertIn('card_refresh_snapshot();', body(self.host, function))
        save = body(self.host, 'card_save')
        self.assertLess(save.index('if (g_console_boot) return;'), save.index('_wfopen('))
        for function in ('h_card_delete', 'h_card_rename', 'h_card_format'):
            self.assertIn('if (!g_console_boot) std::filesystem::remove', body(self.host, function))
        engine = read('port/runtime/hle/yougame_console_engine.cpp')
        advance = body(engine, 'advance')
        self.assertIn('honest_menu(menus_lockstep(),rollback_enabled_', advance)
        self.assertIn('if (menus_lockstep() || frame_ == match_origin_', advance)
        self.assertIn('const bool menu = menus_lockstep();', advance)
        self.assertIn('console: menu stalls ', advance)
        draw = body(read('port/runtime/gx/replay_bar.cpp').split('namespace screen_label {')[1], 'draw')
        self.assertIn('console_menu_visible()', draw)
        self.assertIn('menu_notice(width,height,game_aspect,size)', draw)
        self.assertIn('g_overlay_game_aspect);', read('port/runtime/gx/pc_settings.cpp'))
        self.assertIn('IM_COL32(255,239,170', draw)

    def test_settings_and_page_cannot_mutate_console_simulation(self):
        for name in ('h_game_options', 'h_game_options2', 'h_game_options3',
                     'h_audio_options', 'h_display_options', 'h_mod_flags', 'h_match_override',
                     'h_rng_seed_override', 'h_online_test_match'):
            self.assertIn('if (yougame::console::console_active())', body(self.host, name))
        self.assertIn('if (yougame::console::console_active()) return 0;',
                      body(self.host, 'h_game_options3'))  # full roster, never the card's
        # Shared console menus are off (mode_policy::friends_shared_menus): tick() only reads the
        # console state to close a held Friends room; nothing in it drives the console simulation.
        tick = body(read('port/runtime/hle/native_practice.cpp'), 'tick')
        self.assertIn('const bool shared_active = yougame::console::console_active();', tick)
        self.assertIn('yougame::mode_policy::friends_shared_menus && !shared_active', tick)
        bridge = body(self.host, 'console_practice_bridge')
        self.assertIn('args[0] == -1', bridge)  # only query may reach page pause
        self.assertIn('return -1;', bridge)

    def test_notice_pump_and_snapshot_coverage(self):
        begin = body(self.host, 'begin_console')
        self.assertIn('host::window_pump();', begin)
        self.assertIn('screen_label::show(text, 8.0)', begin)
        self.assertNotIn('host::retrace()', re.sub(r'//[^\n]*', '', begin))
        snap = body(read('sourceport/game/shim/mu_snapshot.c'), 'mu_state_snapshot_words')
        for field in ('HSD_RandSeedPtr', 'gm_GetCurrentSceneIndex()', 'fp->cur_pos.x',
                      'mn_804A04F0.cur_menu', 'mn_804A04F0.hovered_selection',
                      'state->match_result', 'state->timer_seconds', 'rules->item_freq',
                      'rules->stkind', 'Player_GetDamage(slot)'):
            self.assertIn(field, snap)

    def test_console_rollback_is_explicit_and_preserves_slippi_setup(self):
        begin = body(self.host, 'begin_console')
        self.assertIn('std::getenv("MELEE_CONSOLE_ROLLBACK")', begin)
        self.assertIn('rollback_setting(std::getenv("MELEE_CONSOLE_ROLLBACK"))', begin)
        self.assertIn('config, std::move(hooks), rollback)', begin)
        native = read('sourceport/game/shim/mu_online.c')
        self.assertEqual(body(native, 'mu_online_active').strip(), 'return mu_online.active;')
        self.assertEqual(body(native, 'mu_online_pending').strip(), 'return mu_online.pending;')
        self.assertIn('mu_online.console_active ? mu_gm_engine_state()->unk_0 : mu_gm_engine_state()->unk_8',
                      body(native, 'global_frame'))
        for name in ('mu_online_start_melee', 'mu_online_arm', 'mu_online_codes'):
            self.assertNotIn('mu_online.console_', body(native, name))
            self.assertNotIn('mu_console_', body(native, name))
        console = read('sourceport/game/shim/mu_console_rollback.inc')
        for forbidden in ('sync_rng_proc(', 'mu_replay_online_start(', 'report_game(',
                          'mu_online_rules_set(', 'CMD_GET_MATCH_STATE', 'HSD_RandSeedPtr'):
            self.assertNotIn(forbidden, console)
        codes = body(console, 'mu_console_rollback_codes')
        for rule in ('PREVENT_WOBBLING', 'PS_FROZEN_CHECK'):
            self.assertNotIn(rule, codes)
        setup = body(console, 'mu_console_match_setup')
        self.assertIn('GS_VS', setup)
        self.assertIn('GS_SUDDEN_DEATH', setup)
        patch = read('sourceport/patches/melee-native.patch')
        at = patch.index('+    mu_console_match_setup();')
        self.assertLess(at, patch.index('     fn_8016DCC0(arg0);', at))

    def test_console_replay_admission_capture_and_failure_order(self):
        console = read('sourceport/game/shim/mu_console_rollback.inc')
        begin = body(console, 'console_frame_begin')
        self.assertLess(begin.index('CONSOLE_FORWARD'), begin.index('check_predictions(frame)'))
        self.assertLess(begin.index('load_state(ss)'), begin.index('CONSOLE_REPLAY'))
        self.assertLess(begin.index('CONSOLE_REPLAY'), begin.index('mu_console_replace_raw()'))
        self.assertLess(begin.index('mu_console_replace_raw()'), begin.index('capture_state(frame)'))
        shim = body(read('sourceport/game/shim/mu_console_pad.c'), 'mu_console_frame_begin')
        self.assertLess(shim.index('mu_console_rollback_active()'), shim.index('mu_console_host_frame_begin()'))
        host = body(self.host, 'h_console_rollback')
        self.assertIn('begin_native_savestates()', host)
        replay = host[host.index('case MU_CONSOLE_REPLAY:'):host.index('case MU_CONSOLE_INPUTS:')]
        self.assertIn('engine.replay(body)', replay)
        for forbidden in ('advance(', 'console_hardware(', 'retrace(', 'send_pad('):
            self.assertNotIn(forbidden, replay)
        commands = body(self.host, 'h_slippi_command')
        before_slippi = commands[:commands.index('return slippi::online::native_command(')]
        self.assertIn('!g_savestates.has(frame)', before_slippi)
        self.assertIn('required snapshot missing: epoch %u body %d', before_slippi)
        self.assertIn('return -1;', before_slippi)
        self.assertNotIn('abort(', before_slippi)
        self.assertNotIn('die(', before_slippi)


    def test_terminal_commit_precedes_scene_break_and_lockstep_teardown(self):
        patch = read('sourceport/patches/melee-native.patch')
        at = patch.index('+                if (mu_online_scene_terminal())')
        self.assertLess(at, patch.index('                 break;', at))
        self.assertIn('goto mu_online_body;', patch[at:at + 160])
        console = read('sourceport/game/shim/mu_console_rollback.inc')
        terminal = body(console, 'mu_online_scene_terminal')
        for first, second in [('console_body_finish(current)', 'CONSOLE_CONFIRM'),
                              ('CONSOLE_CONFIRM', 'check_predictions(current)'),
                              ('mu_online_abi_resim_phase(0)', 'CONSOLE_COMMIT'),
                              ('console_audio_finish(1)', 'CONSOLE_COMMIT')]:
            self.assertLess(terminal.index(first), terminal.index(second))
        self.assertIn('return 1;', terminal)  # correction re-enters the retained scene loop
        self.assertIn('scene left without terminal commitment', body(console, 'console_match_exit'))
        host = body(self.host, 'h_console_rollback')
        self.assertIn('engine.commit_match(body, state)', host)

    def test_replay_clock_and_hardware_alarm_domains(self):
        poll = body(self.host, 'h_poll')
        self.assertLess(poll.index('engine.poll()'), poll.index('if (g_resim)'))
        self.assertIn('if (engine.body_active() && !engine.servicing()) return;', poll)
        self.assertIn('ConsoleServiceScope service;', poll)
        self.assertIn('engine.logical_retrace()', poll)
        commands = body(self.host, 'h_slippi_command')
        self.assertIn('console.capture_clock(frame)', commands)
        self.assertIn('console.restore_clock(frame)', commands)
        alarm = read('sourceport/game/shim/mu_os.c')
        for name in ('OSSetAlarm', 'OSSetAbsAlarm', 'OSSetPeriodicAlarm'):
            self.assertIn('console_alarm_clock(&simulation, &hardware)', body(alarm, name))
        load = body(read('sourceport/game/shim/mu_online.c'), 'load_state')
        self.assertLess(load.index('mu_alarms_hold()'), load.index('command('))
        self.assertLess(load.index('command('), load.index('mu_alarms_release()'))

    def test_presentation_and_diagnostics_keep_console_identity(self):
        audio = read('sourceport/game/shim/mu_online_audio.c')
        self.assertIn('mu_gm_engine_state()->unk_0', body(audio, 'engine_frame'))
        for name in ('sfx_hooks_on', 'mu_online_audio_frame_begin', 'mu_online_audio_frame_end',
                     'mu_online_music_defer', 'mu_online_motor_port', 'mu_online_audio_rollback_end',
                     'mu_online_audio_match_exit'):
            self.assertIn('mu_console_rollback_active()', body(audio, name))
        console = read('sourceport/game/shim/mu_console_rollback.inc')
        self.assertIn('mu_online_audio_match_start(local_port, local_port)', body(console, 'console_frame_begin'))
        self.assertIn('camera_tasks()', body(console, 'console_frame_end'))
        self.assertIn('gx::mark_discontinuity(true)', body(self.host, 'native_load'))
        self.assertIn('if (!yougame::console::console_engine().match_active()) slippi::online::note_rollback(frame)',
                      body(self.host, 'native_load'))


GIT = '/Library/Developer/CommandLineTools/usr/bin/git' if sys.platform == 'darwin' else (shutil.which('git') or 'git')
BASE = 'eb04f3c'
# The Ranked merge (yg-ranked-port) changed these Slippi routines; they are pinned to its tip instead.
RANKED_BASE = '9152358'
RANKED_ROUTINES = ('mu_online_start_melee', 'report_game')
# Upstream v0.9.207 (Slippi 3.7.0) changed these Slippi routines; they are pinned to its tag.
UPSTREAM_BASE = 'e6478b848'
UPSTREAM_ROUTINES = ('mu_online_codes', 'mu_online_match_exit', 'mu_online_test_wait', 'sync_rng_proc')
# v0.9.207's edits to mu_online_start_melee, applied over the Ranked pin.
UPSTREAM_START_MELEE = (
    ('    if (fetch_match_state(&size) != 0 || size < MSRB_SIZE) {\n'
     '        mu_online_abi_log("online: no match state at match start; playing offline");\n'
     '        return;\n    }\n',
     '    if (fetch_match_state(&size) != 0 || size < MSRB_MIN_SIZE) {\n'
     '        mu_online_abi_log("online: no match state at match start; playing offline");\n'
     '        return;\n    }\n    /* A 3.6.4-sized reply (the peer-to-peer host\'s) has no ranked info or bot flags: the recording\n'
     '     * gets zeroes there, not what an earlier reply left in this buffer. */\n'
     '    if (size < MSRB_SIZE) {\n        memset(mu_online.match_state + size, 0, MSRB_SIZE - size);\n    }\n'),
    ('    /* Per-match rules the online codes read: mode, the frozen Stadium choice (last match state\n'
     '     * byte), teams, the local port. */\n'
     '    mu_online_rules_set(mu_online.mode, mu_online.match_state[MSRB_SIZE - 1],',
     '    /* Per-match rules the online codes read: mode, the frozen Stadium choice (the match state\'s\n'
     '     * alt stage mode byte; 3.7.0 appends ranked info and bot flags after it), teams, the local port. */\n'
     '    mu_online_rules_set(mu_online.mode, mu_online.match_state[MSRB_ALT_STAGE_MODE],'),
)


def baseline(path, base=BASE):
    if not (ROOT / '.git').exists():
        raise unittest.SkipTest('history checks require a git checkout')
    available = subprocess.run([GIT, 'show', '--format=', '--no-patch', base + '^{commit}'], cwd=ROOT,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if available.returncode:
        raise unittest.SkipTest('history checks require revision ' + base)
    return subprocess.check_output([GIT, 'show', base + ':' + path], cwd=ROOT).decode().replace('\r\n', '\n')


def tokens(text):
    text = re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
    return re.findall(r'[A-Za-z_][A-Za-z_0-9]*|[0-9]+|[^\s]', text)


def functions(text):
    return re.findall(r'(?m)^(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w* ]*?\b(\w+)\([^;{}]*\)\s*\{', text)


def drop_block(text, condition):
    # Remove only a specifically named console-only if, with balanced braces.
    start = text.index(condition)
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[:start] + text[end:]


class ConsoleOffEquivalence(unittest.TestCase):
    def assert_functions(self, path, specialize):
        old, new = baseline(path), specialize(read(path))
        names = functions(old)
        self.assertGreater(len(names), 1)
        for name in names:
            with self.subTest(path=path, function=name):
                self.assertEqual(tokens(body(new, name)), tokens(body(old, name)))

    def test_slippi_audio_functions_equal_pre_rollback_when_console_false(self):
        def off(text):
            for old, new in (
                ('(mu_console_rollback_active() ? mu_gm_engine_state()->unk_0 :\n'
                 '                                                 mu_gm_engine_state()->unk_8)', 'mu_gm_engine_state()->unk_8'),
                ('(mu_online_active() || mu_console_rollback_active())', 'mu_online_active()'),
                ('(mu_gm_engine_state()->unk_C == 0 || mu_console_rollback_active())', 'mu_gm_engine_state()->unk_C == 0'),
                ('(mu_gm_engine_state()->unk_C != 0 && !mu_console_rollback_active())', 'mu_gm_engine_state()->unk_C != 0'),
                ('(!mu_online_resim_active() && !(mu_console_rollback_active() && audio.in_body))', '!mu_online_resim_active()'),
            ):
                self.assertIn(old, text)
                text = text.replace(old, new)
            return drop_block(text, 'if (mu_console_rollback_active()) {')
        self.assert_functions('sourceport/game/shim/mu_online_audio.c', off)

    def test_os_alarm_and_callback_functions_equal_pre_rollback_when_console_false(self):
        def off(text):
            text = text.replace('    int console_clock;\n', '')
            text = re.sub(r'    console_clock = mu_host->version >= 25 && mu_host->console_rollback &&\s*'
                          r'mu_host->console_rollback\(MU_CONSOLE_SERVICE, 1, NULL\);', '', text)
            text = text.replace('    if (console_clock) mu_host->console_rollback(MU_CONSOLE_SERVICE, 0, NULL);', '')
            for _ in range(2):
                text = drop_block(text, 'if (mu_host->version >= 25 && mu_host->console_alarm_clock &&')
            text = text.replace('    uint64_t simulation, hardware;\n', '')
            text = re.sub(r'    int console = mu_host->version >= 25 && mu_host->console_alarm_clock &&\s*'
                          r'mu_host->console_alarm_clock\(&simulation, &hardware\);', '', text)
            text = text.replace('console ? (OSTime) hardware : (OSTime) mu_host->ticks()', '(OSTime) mu_host->ticks()')
            text = text.replace('    if (console) start += (OSTime) hardware - (OSTime) simulation;', '')
            self.assertNotIn('console_clock', text)
            self.assertNotIn('console_alarm_clock', text)
            return text
        self.assert_functions('sourceport/game/shim/mu_os.c', off)

    def test_snapshot_prefix_and_all_nonconsole_functions_equal_pre_rollback(self):
        def off(text):
            text = drop_block(text, 'if (mu_console_rollback_active()) {')
            # The enlarged zeroed suffix has console_valid=0. The first 80 words are identical.
            self.assertIn('__builtin_memset(out, 0, 103 * sizeof *out);', text)
            return text.replace('103 * sizeof *out', '(2 + 6 * 12 + 2 + 4) * sizeof *out')
        self.assert_functions('sourceport/game/shim/mu_snapshot.c', off)

    def test_entry_only_changes_pod_size_assertion_and_preserves_field_offsets(self):
        path = 'sourceport/game/shim/mu_entry.c'
        # v0.9.87 adds only this guarded presentation reader; reverse its exact bytes.
        shake = """int mu_console_active(void);

/* The camera's shake is the viewer's own. Online it may only be lowered: the camera decides which fighters count as
 * on screen, and only those get their bones refreshed for the next frame's gameplay (ftLib_80086A8C, mu_online.c
 * camera_tasks), so a shake past the game's own could show a fighter at the edge that the other player's game does
 * not. Lowering it is the same as "Disable screen shake" (MU_GAME_OPTION_NO_SCREEN_SHAKE) and Slippi's own code,
 * both always allowed online. A YouGame console session keeps the game's own camera, as it keeps its options. */
float mu_screen_shake_scale(void)
{
    if (!mu_host || mu_host->version < 27 || !mu_host->screen_shake_scale) return 1.0f;
    if (mu_console_active()) return 1.0f;
    const float scale = mu_host->screen_shake_scale();
    if (!(scale >= 0.0f && scale <= 4.0f)) return 1.0f;
    return scale > 1.0f && (mu_online_active() || mu_online_pending()) ? 1.0f : scale;
}

"""
        # v0.9.89 adds this display-option reader (KO explosions out of sight); reverse its exact bytes too.
        ko = """int mu_ko_explosions_hidden(void)
{
    static int logged = -1;
    const int on = (mu_display_options() & MU_DISPLAY_NO_KO_EXPLOSIONS) != 0;
    if (on != logged) {
        logged = on;
        OSReport("[ko] blast-zone explosions %s\\n", on ? "out of sight" : "shown");
    }
    return on;
}
_Static_assert(MU_DISPLAY_NO_KO_EXPLOSIONS == MU_DISPLAY_OPTION_NO_KO_EXPLOSIONS, "display option bits");

"""
        # v0.9.96 adds the hide-emblems-behind-percents reader the same way.
        emblems = """int mu_hud_emblems_hidden(void)
{
    return (mu_display_options() & MU_DISPLAY_NO_HUD_EMBLEMS) != 0;
}
_Static_assert(MU_DISPLAY_NO_HUD_EMBLEMS == MU_DISPLAY_OPTION_NO_HUD_EMBLEMS, "display option bits");

"""
        # v0.9.97 adds the community codes' bit checks after it and numbers the page-pause op; reverse both.
        codes = """/* The community codes (shim/mu_code_features.c). */
_Static_assert(MU_DISPLAY_HUD_FADE == MU_DISPLAY_OPTION_HUD_FADE, "display option bits");
_Static_assert(MU_DISPLAY_ANGEL_STOCK_COLORS == MU_DISPLAY_OPTION_ANGEL_STOCK_COLORS, "display option bits");
_Static_assert(MU_DISPLAY_NANA_PERCENT == MU_DISPLAY_OPTION_NANA_PERCENT, "display option bits");
_Static_assert(MU_DISPLAY_ALLY_INDICATORS == MU_DISPLAY_OPTION_ALLY_INDICATORS, "display option bits");
_Static_assert(MU_DISPLAY_NO_GO_TEXT == MU_DISPLAY_OPTION_NO_GO_TEXT, "display option bits");
_Static_assert(MU_DISPLAY_HITBOX_ID_COLORS == MU_DISPLAY_OPTION_HITBOX_ID_COLORS, "display option bits");
_Static_assert(MU_DISPLAY_HITBOX_ID_ORDER == MU_DISPLAY_OPTION_HITBOX_ID_ORDER, "display option bits");
_Static_assert(MU_DISPLAY_HITBOX_ANGLES == MU_DISPLAY_OPTION_HITBOX_ANGLES, "display option bits");
_Static_assert(MU_DISPLAY_HQ_FOD_REFLECTIONS == MU_DISPLAY_OPTION_HQ_FOD_REFLECTIONS, "display option bits");
_Static_assert(MU_DISPLAY_VCANCEL_SOUND == MU_DISPLAY_OPTION_VCANCEL_SOUND, "display option bits");
_Static_assert(MU_DISPLAY_CODES_ALL == MU_DISPLAY_OPTION_CODES_ALL, "display option bits");
"""
        # v0.9.119 adds the hide-fighter-shadows bit check right after the codes block; reverse it too.
        codes += '_Static_assert(MU_DISPLAY_NO_FIGHTER_SHADOWS == MU_DISPLAY_OPTION_NO_FIGHTER_SHADOWS, "display option bits");\n'
        # v0.9.120 adds the Center 2P HUD and Boot to CSS bit checks after it; reverse them too.
        codes += '_Static_assert(MU_DISPLAY_CENTER_2P_HUD == MU_DISPLAY_OPTION_CENTER_2P_HUD, "display option bits");\n'
        codes += '_Static_assert(MU_DISPLAY_BOOT_TO_CSS == MU_DISPLAY_OPTION_BOOT_TO_CSS, "display option bits");\n'
        page_pause = ' && MU_PRACTICE_PAGE_PAUSE == 15'
        entry = read(path)
        self.assertEqual(entry.count(codes), 1)
        self.assertEqual(entry.count(page_pause), 1)
        entry = entry.replace(codes, '').replace(page_pause, '')
        self.assertEqual(entry.count(shake), 1)
        self.assertEqual(entry.count(ko), 1)
        self.assertEqual(entry.count(emblems), 1)
        self.assertEqual(entry.count('MU_PRACTICE_BOOT_MATCH == 35,'), 1)   # build 51 bootMatch op
        # Upstream 0.9.98: the pose copy sent to the host is stepped for the view (render only).
        pose_step = ('void mu_native_pose_step_view(MuNativePoseSnapshot* snapshot);\n', '')
        pose_send = ('    if (mu_host != NULL && mu_host->native_pose_snapshot != NULL) {\n'
                     '        MuNativePoseSnapshot stepped = *snapshot;\n'
                     '        mu_native_pose_step_view(&stepped);\n'
                     '        mu_host->native_pose_snapshot(&stepped);\n'
                     '    }\n',
                     '    if (mu_host != NULL && mu_host->native_pose_snapshot != NULL)\n'
                     '        mu_host->native_pose_snapshot(snapshot);\n')
        # Upstream 0.9.104: the ray-traced renderer's render-semantics query (host API 32 here, its 25).
        rtx = ('''/* Render semantics for the host's ray-traced renderer (tokens 0xF3-0xF6, mu_rtx_tokens.h): the HSD
 * hooks in tobj.c, dobj.c, lobj.c, cobj.c and eflib.c write them only while this is nonzero. */
int mu_native_rtx_semantics(void)
{
    return mu_host && mu_host->version >= 32 && mu_host->native_rtx_semantics
               ? mu_host->native_rtx_semantics() != 0
               : 0;
}

''', '')
        for old, new in (pose_step, pose_send, rtx):
            self.assertEqual(entry.count(old), 1)
            entry = entry.replace(old, new)
        self.assertEqual(entry.replace('    extern int mu_css_transfer(int, void*, unsigned);\n    game->css_state = mu_css_transfer;\n', '').replace(shake, '').replace(ko, '').replace(emblems, '').replace(
                         ' &&\n               MU_PRACTICE_BOOT_MATCH == 35,', ',').replace('103 * sizeof(uint32_t)',
                         '80 * sizeof(uint32_t)'), baseline(path))
        path = 'port/runtime/abi/mu_host.h'
        old = re.search(r'typedef struct MuStatePod \{(.*?)\} MuStatePod;', baseline(path), re.S)[1]
        new = re.search(r'typedef struct MuStatePod \{(.*?)\} MuStatePod;', read(path), re.S)[1]
        legacy = tokens(old)
        self.assertEqual(tokens(new)[:len(legacy)], legacy)
        self.assertEqual(body(read('port/runtime/hle/yougame_console_engine.cpp'), 'state_digest').count(
            'offsetof(MuStatePod, console_valid)'), 2)

    def test_original_online_bodies_remain_identical(self):
        path = 'sourceport/game/shim/mu_online.c'
        old, new = baseline(path), read(path)
        # Round A moved the predictor verbatim into an include, preserving its OFF branches.
        new += read('sourceport/game/shim/mu_online_prediction.inc')
        # Tournament play opts into the same online gameplay codes. With that
        # explicit feature branch disabled, all preexisting bodies stay identical.
        tourney = "\n#ifndef MU_NO_SLIPPI\n        && !mu_tourney_playing()\n#endif\n    "
        self.assertEqual(new.count(tourney), 1)
        new = new.replace(tourney, "")
        # Optional Squad Strike hashes fighter kind only while a squad match is live; with
        # MELEE_SQUAD_STRIKE off the guarded line is compiled out and the checksum is unchanged.
        squad = ("#if MU_SQUAD\n            /* Based on Squad Strike by Chris (x9x9x9x9x9), "
                 "https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's "
                 "permission (11-03-2026) */\n            if (gmSquad_Live()) acc ^= (u32) fp->kind;\n"
                 "#endif\n")
        self.assertEqual(new.count(squad), 1)
        new = new.replace(squad, "")
        from squad_wiring_test import without_squad
        new = without_squad(new)  # online setup restoration is also optional
        # Round A's explicit console branches are already independently pinned above; these
        # pin the untouched Slippi routines, including extracted predictor helpers.
        names = (
            'axis_crossed be32 camera_tasks clamp_stick_at_rest command command_into compute_checksum '
            'correction_capture correction_measure describe_address direct_renew early_contradicts '
            'early_pad early_rb_any early_rollback_on early_trace_on end_game fetch_match_state '
            'find_in_jobj_tree float_bits hud_message inside jobj_field launch_kind logf_ mu_online_active '
            'mu_online_after_pad_renew mu_online_arm mu_online_codes mu_online_is_test_run '
            'mu_online_match_exit mu_online_p2p_menus mu_online_p2p_port mu_online_pending '
            'mu_online_pending_game_info mu_online_record_state mu_online_reset_game_index '
            'mu_online_resim_active mu_online_start_melee mu_online_state_address mu_online_state_size '
            'mu_online_test_wait pad_to_wire pads_contradict_zone put32 record_used_pads report_game '
            'selftest_attribute selftest_command side_of singles_winner store_recovery sync_rng_proc '
            'test_inputs_enabled test_pad trace_pads trace_pads_line trace_renew trace_state_line used_slot '
            'wire_to_pad '
        ).split()
        self.assertEqual(len(names), 61)
        ranked = baseline(path, RANKED_BASE)
        upstream = baseline(path, UPSTREAM_BASE)
        for name in names:
            want = body(upstream if name in UPSTREAM_ROUTINES else ranked if name in RANKED_ROUTINES else old, name)
            if name == 'mu_online_start_melee':
                for before, after in UPSTREAM_START_MELEE:
                    self.assertEqual(want.count(before), 1, before)
                    want = want.replace(before, after)
            self.assertEqual(body(new, name), want)

    def test_frontier_and_rumble_ordering_and_alarm_guard(self):
        console = read('sourceport/game/shim/mu_console_rollback.inc')
        begin = body(console, 'console_frame_begin')
        self.assertLess(begin.index('check_predictions(frame)'), begin.index('console_validate(frame)'))
        self.assertLess(begin.index('console_validate(frame)'), begin.index('CONSOLE_BOUNDARY'))
        self.assertLess(begin.index('capture_state(frame)'), begin.index('mu_console_rumble_step()'))
        terminal = body(console, 'mu_online_scene_terminal')
        self.assertLess(terminal.index('check_predictions(current)'), terminal.index('console_validate(current + 1)'))
        engine = read('port/runtime/hle/yougame_console_engine.cpp')
        self.assertIn('it->first > validated_boundary_', body(engine, 'publish'))
        self.assertNotIn('confirmed_through()', body(engine, 'publish'))
        self.assertNotIn('validated_boundary_', body(engine, 'receive'))
        game = read('sourceport/game/shim/mu_console_pad.c')
        replace = body(game, 'mu_console_replace_raw')
        rollback = replace[:replace.index('    p->qread = p->qwrite;\n    p->qcount = 0;')]
        self.assertNotIn('HSD_PadRenewRawStatus', rollback)
        self.assertNotIn('HSD_PadRumbleInterpret', rollback)
        self.assertIn('mu_console_rumble_owned()', body(game, 'mu_console_engine_gate'))
        self.assertEqual(body(console, 'mu_console_rumble_owned').strip(),
                         'return mu_online.console_setup || mu_online.console_active;')
        patch = read('sourceport/patches/melee-native.patch')
        self.assertIn('+    if (!mu_console_rumble_owned())\n+#endif\n     HSD_PadRumbleInterpret();', patch)

    def test_sdk_renewal_off_keeps_pre_rollback_text_and_order(self):
        path = 'sourceport/patches/melee-native.patch'
        def controller(text):
            start = text.index('diff --git a/src/sysdolphin/baselib/controller.c ')
            end = text.index('\ndiff --git ', start + 1)
            # Hunk headers and the index line are presentation: build 56's regenerated patch names the
            # true post-image blob where 55's still carried upstream's.
            return re.sub(r'^(?:@@|index )[^\n]*\n', '', text[start:end], flags=re.M)
        now = controller(read(path))
        added = ('+#ifdef MU_NATIVE\n'
                 '+    /* Console rollback steps scripts once per body, after its checkpoint, never on alarms. */\n'
                 '+    if (!mu_console_rumble_owned())\n+#endif\n')
        self.assertEqual(now.count(added), 1)
        self.assertEqual(now.replace(added, ''), controller(baseline(path)))

    def test_patch_hunk_counts(self):
        patch = read('sourceport/patches/melee-native.patch')
        hunks = list(re.finditer(r'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@[^\n]*\n', patch, re.M))
        # Build 28 + v0.9.87: 10067; Phase B adds 6; Build 49.1 GX layouts add 2.
        # Optional Squad Strike adds 11 guarded hook hunks.
        # Build 53: upstream v0.9.97's community codes add 4 (V-cancel sound 1, full FoD reflections 3).
        # Build 56: upstream v0.9.99 adds 6 (stage skins in mnstagesw 4, Training fixes in gmtrainingmode 2).
        # Build 57: upstream v0.9.101 adds 3 net (lbcollision +3, itfreeze +1, hsd_3A76 -1).
        # Build 58: upstream v0.9.102 adds 1 (grizumi, the live Fountain of Dreams scenery).
        # Build 65: upstream v0.9.104 adds 9 (render semantics in cobj/dobj/lobj/tobj 6, Kirby hats in ftparts 2, gm_1601 1).
        # The 4-player Friends select adds 2 (the host-only Rules and Teams guards in mncharsel).
        self.assertEqual(len(hunks), 10168)  # 20XX Melee 1.03 and the mid-match toggle: 2 more; tap jump off and tilt stick: 13 more (gmvs.c match start, ftCo_Jump/JumpAerial/JumpAerialF1/PassiveWall, ftCo_AttackS3/S4/Hi3/Hi4/Lw3/Lw4/Dash and their headers); 1.0.15: one more (a fighter's swinging parts with a bone missing, lb_00F9.c); 1.0.3: one more (added items start with no command script, item.c); 1.0.1: one more (the Training HUD switch, gm_1884.c, and the item script guard, itanimlist.c); 0.9.992: three more (Final Destination's background guard for skins without its animations, grlast.c); 0.9.990: one more for the rogueMelee Disable fix (main becad61b), one more (single player stock icon lists on an m-ex disc's icon table); 8 more (an m-ex disc's own menu under VS Mode: mnmain 7, gmvsmode 1); 19 more (the 0.9.216 merge and the online menu description hook); build 103: two more (results screen and main menu); build 101: v0.9.120's two new patch hunks (97: v0.9.117's itzako hunk)
        for i, h in enumerate(hunks):
            after = patch[h.end():hunks[i + 1].start() if i + 1 < len(hunks) else len(patch)]
            after = after.split('diff --git ')[0]
            lines = after.splitlines()
            old = sum(line.startswith((' ', '-')) for line in lines)
            new = sum(line.startswith((' ', '+')) for line in lines)
            self.assertEqual((old, new), (int(h[2] or 1), int(h[4] or 1)), h[0])

    def test_drop_room_once_runs_in_the_console_worker_outside_its_lock(self):
        link = read('port/runtime/hle/yougame_link.cpp')
        worker = link[link.index('worker = std::thread([this,code] {'):link.index('trying held-seat rejoin')]
        hook = worker[worker.index('const bool drop_once'):worker.index('if (host::exit_requested())')]
        self.assertIn('std::getenv("MELEE_ONLINE_TEST_DROP_ROOM_ONCE")', hook)
        self.assertIn('console::console_css_frames() >= 300', hook)
        due = hook.index('agent_drop_room_once_due(')
        lock_end = hook.index('); }', due)
        self.assertLess(hook.index('std::lock_guard<std::mutex> lock(mutex)'), due)
        self.assertGreater(hook.index('room->agent_test_drop_room()'), lock_end)
        self.assertLess(link.rindex('#ifdef YOUGAME_AGENT_TEST_HOOK', 0, link.index('const bool drop_once')),
                        link.index('const bool drop_once'))
        engine = read('port/runtime/hle/yougame_console_engine.cpp')
        self.assertIn('state.scene_major == 2 && state.scene == 0', engine)
        end = body(engine, 'Engine::end')
        self.assertIn('global_css_frames.store(0', end)

if __name__ == '__main__':
    unittest.main()
