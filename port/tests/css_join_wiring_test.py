"""Small-state Friends joins: scene ownership, ABI gates and the real native setter scope."""
from pathlib import Path
import re
import unittest
from yougame_console_wiring_test import body
ROOT = Path(__file__).resolve().parents[2]
def read(p): return (ROOT / p).read_text()

class CssJoinWiring(unittest.TestCase):
    def test_fixed_byte_abi_and_version_gates(self):
        h=read('port/runtime/abi/mu_host.h')
        self.assertIn('#define MU_HOST_API_VERSION 34',h)   # 34: control options (tap jump, tilt stick); 33: main's pro AI tag robot (upstream 26)
        self.assertIn('#define MU_GAME_API_VERSION 10',h)
        self.assertIn('int32_t (*css_state)(int32_t operation, void* bytes, uint32_t size)',h)
        self.assertIn('host->version != MU_HOST_API_VERSION',read('sourceport/game/shim/mu_entry.c'))
        self.assertIn('mu_host->version >= 30',body(read('sourceport/game/shim/mu_pad.c'),'mu_css_join_locked'))
        self.assertIn('!g_game.css_state',read('port/app/source_host.cpp'))

    def test_decomp_private_types_are_only_used_in_the_native_patch_scope(self):
        patch=read('sourceport/patches/melee-native.patch')
        css=patch.split('diff --git a/src/melee/mn/mncharsel.c b/src/melee/mn/mncharsel.c\n')[1].split('diff --git ')[0]
        self.assertIn('+#include <mu_css_transfer.inc>',css)
        self.assertLess(css.index('+#include <mu_css_transfer.inc>'),css.index('void mnCharSel_Scene_OnEnter'))
        self.assertIn('+    if (mu_css_join_locked()) { mnCharSel_804D6CF6 = 0; return; }',css)
        # Optional Squad Strike adds 11 guarded hook hunks (including budget enumeration) to the native patch.
        # Build 53: upstream v0.9.97's community codes add 4 (V-cancel sound 1, full FoD reflections 3).
        # Build 56: upstream v0.9.99 adds 6 (stage skins in mnstagesw 4, Training fixes in gmtrainingmode 2).
        # Build 57: upstream v0.9.101 adds 3 net (lbcollision +3, itfreeze +1, hsd_3A76 -1).
        # Build 58: upstream v0.9.102 adds 1 (grizumi, the live Fountain of Dreams scenery).
        # Build 65: upstream v0.9.104 adds 9 (render semantics in cobj/dobj/lobj/tobj 6, Kirby hats in ftparts 2, gm_1601 1).
        # The 4-player Friends select adds 2 (the host-only Rules and Teams guards in mncharsel).
        self.assertEqual(len(re.findall(r'^@@ ',patch,re.M)),10171)  # 1.0.30 build-tree patch sync: three more hunks; tap jump off and tilt stick: 14 more (the mid-match toggle's pause check, gmvs.c); 20XX Melee 1.03: one more (its sites sit in hunks already there but for one) (gmvs.c match start, ftCo_Jump/JumpAerial/JumpAerialF1/PassiveWall, ftCo_AttackS3/S4/Hi3/Hi4/Lw3/Lw4/Dash and their headers); 1.0.15: one more (a fighter's swinging parts with a bone missing, lb_00F9.c); 1.0.3: one more (added items start with no command script, item.c); 1.0.1: one more (the Training HUD switch, gm_1884.c, and the item script guard, itanimlist.c); 0.9.992: three more (Final Destination's background guard for skins without its animations, grlast.c); 0.9.990: one more for the rogueMelee Disable fix (main becad61b), one more (single player stock icon lists on an m-ex disc's icon table); 8 more (an m-ex disc's own menu under VS Mode: mnmain 7, gmvsmode 1); one more (TObj palette index past 255); 19 more (the 0.9.216 merge and the online menu description hook); build 103: two more (results screen and main menu); build 101: v0.9.120's two new patch hunks (97: v0.9.117's itzako hunk)

    def test_apply_validates_all_ports_before_any_setter(self):
        apply=body(read('sourceport/game/include/mu_css_transfer.inc'),'mu_css_transfer')
        self.assertLess(apply.index('op != MU_CSS_APPLY || !mu_css_valid(s)'),apply.index('MU_CSS_WRITE_RULE'))
        self.assertLess(apply.index('if (tags[i] == 120) return -1;'),apply.index('MU_CSS_WRITE_RULE'))
        for setter in ['gmMainLib_SetRumbleEnabled(i,in->flags & 1)', 'GetPersistentNameData(tags[i])',
                       'gm_80167BC8(&css->vs)', 'mnCharSel_8025DB34(i)', 'HSD_JObjSetTranslateX',
                       'door->p_kind = door->p_kind_prev', 'cursor->x6 = in->held_token',
                       'token->x5 = in->token_state', 'mnCharSel_804D6CF6 = s->ready']:
            self.assertIn(setter,apply)
        self.assertNotIn('memcpy(css',apply)
        self.assertIn('GetNameText(j) != NULL',apply) # refuse a full name bank, never overwrite tags

    def test_hold_is_before_begin_and_lock_is_before_frozen_pick(self):
        start=body(read('port/runtime/hle/yougame_console_link.cpp'),'start_session')
        self.assertLess(start.index('css-join hold: host in match'),start.index('json begin ='))
        self.assertLess(start.index('i.css_lock(true)'),start.index('i.freeze_pick(name)'))
        production=body(read('port/runtime/hle/yougame_link.cpp'),'friends_entry')
        self.assertIn('joins when this match ends',production)
        self.assertIn('friends::events.notice(line,transport::now_ms())',production)
        self.assertIn('css-join lock: %s',production)
        pad=body(read('port/app/source_host.cpp'),'h_pad_read')
        self.assertIn('css_join::on_css.load(std::memory_order_acquire)',pad)

    def test_immutable_commands_only_game_thread_calls_setters_and_full_span_hash(self):
        host=read('port/app/source_host.cpp')
        tick=body(host,'css_join_tick')
        self.assertIn('if (q->host && !css) return 0',tick)
        self.assertIn('queue_css_origin_reset(q->card)',tick)
        self.assertIn('css-join origin: refused (%s); relaunch',tick)
        self.assertIn('"guest save differs"',tick)
        self.assertIn('native_savestate::hash_spans(g_css_origin.spans)',tick)
        self.assertIn('g_css_origin.frame < q->frame',tick)
        self.assertIn('channel->working.store(false',tick)
        self.assertIn('console_engine().adopt_css_clock',tick)
        self.assertIn('!cj::locked.exchange(true',tick)
        self.assertIn('g_game.css_state(MU_CSS_CAPTURE,&probe,sizeof probe)',tick)
        self.assertNotIn('css_state(',read('port/runtime/hle/yougame_console_link.cpp'))
        start=body(read('port/runtime/hle/yougame_console_link.cpp'),'start_session')
        self.assertIn('css_join::synchronize',start)
        self.assertIn('return relaunch();',start)

    def test_hash_and_own_identity_gate_plug_in(self):
        proto=read('port/runtime/hle/yougame_css_join.h')
        self.assertLess(proto.index('identity(state.ports[local_port]'),proto.index('Operation::Apply,state,k'))
        self.assertLess(proto.index('css-join hash mismatch frame='),proto.index('Kind::PlugIn,k'))
        self.assertIn('css-join state bytes=',proto)
        self.assertIn('css-join hash ok frame=',proto)
        self.assertIn('bytes.size() > 10+MU_CSS_SIZE',proto)
        self.assertIn('get_ref<const std::string&>().size()>352',proto)
        self.assertIn('m.value("from",json())!=peer',proto)

    def test_offline_proof_compiles_out_and_uses_real_spans(self):
        source=read('port/app/source_host.cpp')
        at=source.index('const char* state_test =')
        self.assertGreater(source.rfind('#ifdef YOUGAME_AGENT_TEST_HOOK',0,at),source.rfind('#endif',0,at))
        tick=body(source,'css_origin_tick')
        self.assertIn('g_game.css_state(MU_CSS_CAPTURE',tick)
        self.assertIn('g_game.css_state(MU_CSS_APPLY',tick)
        self.assertIn('run.state_hash = native_savestate::hash_spans(run.spans)',tick)
        self.assertIn('css-state roundtrip hash_before=%016llx hash_after=%016llx',tick)
        self.assertIn('css-state bytes=%u data=%s',tick)
        self.assertIn('after == run.state_hash ? 0 : 1',tick)
        self.assertIn('YOUGAME_TEST_CSS_STATE',read('port/tests/agent_test_hook_absent.sh'))

    def test_untrusted_names_use_a_literal_format_and_new_slot_reset(self):
        setter=read('sourceport/game/include/mu_css_transfer.inc')
        self.assertIn('text,0,"%s",GetNameText(tags[i])',setter)
        self.assertLess(setter.index('InitializePersistentNameData(tags[i])'),setter.index('tag->namedata[j] = in->tag[j]'))
        abi=read('port/runtime/abi/mu_css_join.h')
        self.assertIn('mu_css_identity_check(p->tag',abi)
        self.assertIn('mu_css_identity_check(m.bytes.data()+1',read('port/runtime/hle/yougame_css_join.h'))

    def test_adoption_and_fallback_remain_inside_start_session(self):
        start=body(read('port/runtime/hle/yougame_console_link.cpp'),'start_session')
        self.assertIn('link->room_message(m)',start)
        self.assertNotIn('provisional->',start)
        self.assertLess(start.index('std::atomic_store(&css_join::launch,launch)'),start.index('css_join::synchronize'))
        self.assertIn('return relaunch();',start[start.index('if (!joined)'):])
        proto=read('port/runtime/hle/yougame_css_join.h')
        self.assertLess(proto.index('command(Operation::Prepare'),proto.index('emit({Kind::PlugIn'))
        self.assertLess(proto.index('command(Operation::Plug'),proto.index('emit({Kind::Installed'))
        self.assertLess(proto.index('next(Kind::Installed'),proto.index('command(Operation::Release'))
        self.assertIn('css-join abort: ',proto)
        self.assertLess(start.index('link->prepare_service()'),start.index('css_join::synchronize'))
        self.assertIn('service_enabled_.load(std::memory_order_acquire)',read('port/runtime/hle/yougame_console_link.cpp'))
        entry=body(read('port/runtime/hle/yougame_link.cpp'),'friends_entry')
        self.assertNotIn('std::atomic_store(&css_join::launch',entry)
        self.assertIn('if (css_adopted)',entry)

    def test_setup_never_dereferences_a_link_after_publication(self):
        start=body(read('port/runtime/hle/yougame_console_link.cpp'),'start_session')
        moved=start[start.index('std::atomic_store(&css_join::launch,launch)'):start.index('if (live) {\n          handoff_join::transfer')]
        success=moved[moved.index('config = launch->config;'):]
        self.assertNotIn('link->',success)
        self.assertNotIn('provisional->',moved)
        proto=read('port/runtime/hle/yougame_css_join.h')
        sync=proto[proto.index('bool synchronize'):]
        self.assertNotIn('SessionLink',sync)
        self.assertNotIn('room_message(m)',sync)
        self.assertIn('channel->queue_room(m)',sync)
        self.assertIn('ownership(channel->main_mutex)',body(read('port/app/source_host.cpp'),'css_join_tick'))
        self.assertIn('ownership(css_live->main_mutex)',start)
        tick=body(read('port/app/source_host.cpp'),'css_join_tick')
        release=tick[tick.index('q->operation == cj::Operation::Release'):tick.index('q->operation == cj::Operation::Apply')]
        self.assertLess(release.index('console_engine().css_join_release()'),release.index('publish(released)'))
        link=read('port/runtime/hle/yougame_console_link.cpp')
        self.assertIn('std::lock_guard<std::mutex> lock(mutex_); room_message(m);',link)
        self.assertIn('if (op!=Operation::Release) pump(0);',proto)
        self.assertIn('release_until=io.clock()+3000',proto)

    def test_runtime_cursor_and_costumes_report_the_first_field(self):
        import os, subprocess, tempfile
        native=read('sourceport/game/include/mu_css_transfer.inc')
        fixture=r'''
#include "mu_css_join.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define CSS_NONE 3
#define MEX_CSS_CURSOR_RIGHT_29 2
static unsigned mu_mex_css_codes;
static struct { int char_kind; } icons[CSS_NONE]={{0},{2},{30}};
static unsigned gm_GetNumCostumesForCKind(unsigned fighter) { return fighter==30 ? 9 : 4; }
static char line[256];
static void OSReport(const char* fmt,...) { va_list args; va_start(args,fmt); vsnprintf(line,sizeof line,fmt,args); va_end(args); }
static float mu_css_float_read(const unsigned char* src) {'''+body(native,'mu_css_float_read')+r'''}
static int mu_css_resources_valid(const MuCssState* s,int report) {'''+body(native,'mu_css_resources_valid')+r'''}
int main(void) {
  MuCssState s={0}; unsigned i;
  for (i=0;i<4;++i) s.ports[i].fighter=255;
  mu_css_write_le(s.ports[1].cursor_x,0x41e40000,4);
  assert(!mu_css_resources_valid(&s,0) && !line[0]);
  assert(!mu_css_resources_valid(&s,1));
  assert(!strcmp(line,"css-join invalid: ports[1].cursor_x=28.5\n"));
  mu_mex_css_codes=MEX_CSS_CURSOR_RIGHT_29; assert(mu_css_resources_valid(&s,1));
  s.ports[1].fighter=30; s.ports[1].costume=8; assert(mu_css_resources_valid(&s,1));
  s.ports[1].costume=9; assert(!mu_css_resources_valid(&s,1));
  assert(!strcmp(line,"css-join invalid: ports[1].costume=9\n"));
  s.ports[1].fighter=31; assert(!mu_css_resources_valid(&s,1));
  assert(!strcmp(line,"css-join invalid: ports[1].fighter=31\n"));
}
'''
        with tempfile.TemporaryDirectory(prefix='css-runtime-') as work:
            path=Path(work)/'runtime.c'; exe=Path(work)/'runtime'; path.write_text(fixture)
            subprocess.run([os.environ.get('CC') or 'cc','-std=c11','-I'+str(ROOT/'port/runtime/abi'),str(path),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_relaunch_lock_and_quitting_use_the_production_prepared_callback(self):
        import os, subprocess, tempfile
        entry=body(read('port/runtime/hle/yougame_link.cpp'),'friends_entry')
        start=entry.index('io.prepared = [&,handoff_io]')
        callback=entry[start:entry.index('\n    };',start)+7]
        cleanup_start=entry.index('css_join::relaunching.store(false')
        cleanup=entry[cleanup_start:entry.index('if ((b->interrupted',cleanup_start)]
        self.assertLess(entry.index('relaunch_wait->timed_out'),cleanup_start)
        self.assertIn('!css_join::relaunching.load',read('port/runtime/hle/yougame_console_link.cpp'))
        fixture=r'''
#include "yougame_css_join.h"
#include "yougame_console_entry.h"
#include <cassert>
#include <chrono>
#include <cstring>
using namespace yougame;
using namespace yougame::console;
static bool quitting, handoff_ok;
static int calls, flushes, flushed_at_handoff;
namespace gx::cache_store { bool flush_all(const char* why, std::chrono::milliseconds) { assert(!strcmp(why,"relaunch")); ++flushes; return true; } }
static std::shared_ptr<RelaunchWait> g_relaunch_wait;
namespace host { bool exit_requested() { return quitting; } void log(const char*,...) {} }
static int64_t epoch_now() { return 123; }
static std::string current_build() { return "test"; }
static void write_line(const std::string&) { assert(false); }
template<class... Args> bool fake_handoff(Args&&...) { ++calls; flushed_at_handoff=flushes; return handoff_ok; }
struct Backend { std::mutex mutex; std::string session; bool handoff_pending=false; };
void run(bool relaunching,bool ok,bool exit) {
  quitting=exit; handoff_ok=ok; calls=0; flushes=0; flushed_at_handoff=-1; friends::lobby.begin(false); entry.begin();
  css_join::locked=false; css_join::relaunching=false;
  bool prepared=false; std::string prepared_session; int64_t handed_at=0;
  std::shared_ptr<RelaunchWait> relaunch_wait;
  auto b=std::make_shared<Backend>(); HandoffIO handoff_io;
  auto css_lock=[](bool on) { css_join::locked=on; };
  struct { std::function<void(const SessionConfig&)> prepared; } io;
'''+callback.replace('prepared = handoff(','prepared = fake_handoff(')+r'''
  SessionConfig c; c.session_id="session"; io.prepared(c);
  assert(calls==(exit?0:1));
  // The new game reads the shader and pipeline caches as it starts: they are written before the ask.
  assert(flushes==(relaunching && !exit ? 1 : 0));
  if (!exit) assert(flushed_at_handoff==(relaunching ? 1 : 0));
  assert(bool(css_join::locked)==(ok && relaunching && !exit));
  assert(bool(css_join::relaunching)==(ok && relaunching && !exit));
  if (ok && relaunching && !exit) {
    assert(relaunch_wait && !relaunch_wait->timed_out(123+44999));
    assert(css_join::locked); assert(relaunch_wait->timed_out(123+45000));
  }
'''+cleanup+r'''
  assert(!css_join::locked && !css_join::relaunching && !std::atomic_load(&g_relaunch_wait));
}
int main() { for (bool relaunch:{false,true}) for (bool ok:{false,true}) for (bool exit:{false,true}) run(relaunch,ok,exit); }
'''
        with tempfile.TemporaryDirectory(prefix='css-relaunch-') as work:
            path=Path(work)/'relaunch.cpp'; exe=Path(work)/'relaunch'; path.write_text(fixture)
            subprocess.run([os.environ.get('CXX') or 'c++','-std=c++17','-I'+str(ROOT/'port/runtime/hle'),'-I'+str(ROOT/'port/third_party'),str(path),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_paused_loop_exit_retires_its_channel_before_the_relaunch_can_lock(self):
        # The link thread starts the relaunch (and takes the lock for its wait) once working is false;
        # a later tick must not find the cancelled channel still pending and unlock it.
        tick=body(read('port/app/source_host.cpp'),'css_join_tick')
        tail=tick[tick.rindex('channel->cancelled.store(true,std::memory_order_release);'):]
        retire=tail.index('std::atomic_compare_exchange_strong(&cj::pending,&self,std::shared_ptr<cj::Channel>{});')
        self.assertLess(retire,tail.index('channel->working.store(false,std::memory_order_release);'))
        self.assertLess(tail.index('unlock();'),retire)

    def test_fresh_offline_boot_keeps_title_and_reset_alone_requests_vs(self):
        source=read('port/app/source_host.cpp')
        startup=body(source,'run')
        self.assertNotIn('g_css_origin.join_boot = true',startup)
        self.assertIn('run.join_boot = true',body(source,'css_origin_tick'))
        self.assertIn('g_css_origin.armed && g_css_origin.boot.ready()',body(source,'css_join_tick'))
        # Exercise the actual game-side boot selector with the experimental switch armed:
        # checkpoint arming alone does not change the title/main-menu mode.
        import os, subprocess, tempfile
        boot=body(read('sourceport/game/shim/mu_console_boot.c'),'mu_css_origin_boot')
        fixture = '#include <assert.h>\nstatic int join_boot, loaded;\nint mu_css_origin_host(int op) { return op==0 && join_boot; }\nint mu_console_active(void) { return 0; }\nvoid mu_console_load_save(void) { ++loaded; }\nvoid boot(unsigned char* mode) {'+boot+'}\nint main(void) { unsigned char mode=0; boot(&mode); assert(mode==0 && !loaded); join_boot=1; boot(&mode); assert(mode==2 && loaded==1); }'
        with tempfile.TemporaryDirectory(prefix='css-title-') as work:
            path=Path(work)/'boot.c'; exe=Path(work)/'boot'; path.write_text(fixture)
            subprocess.run([os.environ.get('CC') or 'cc',str(path),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__=='__main__': unittest.main()
