"""Build 50 offline reset/continuation contracts; no game, device or account is opened."""
from pathlib import Path
import os
import platform
import shutil
import subprocess
import tempfile
import unittest
from yougame_console_wiring_test import body

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


class CssOriginWiring(unittest.TestCase):
    def run_cpp(self, fixture):
        with tempfile.TemporaryDirectory(prefix='css-origin-review-') as work:
            path, exe = Path(work) / 'fixture.cpp', Path(work) / 'fixture'
            path.write_text(fixture)
            subprocess.run([os.environ.get('CXX') or 'c++', '-std=c++17', str(path), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_unarmed_path_has_no_capture_or_allocation_bookkeeping(self):
        host = read('port/app/source_host.cpp')
        tick = body(host, 'css_origin_tick')
        self.assertLess(tick.index('if (!run.armed) return 0;'), tick.index('g_game.state_regions('))
        startup = body(host, 'run')
        self.assertNotIn('g_css_origin.spans =', startup)
        self.assertIn('if (g_css_origin.proof) { css_origin_arm();', startup)
        self.run_cpp(r'''
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <map>
struct Runtime { bool armed=false; std::map<void*,uint32_t> live_allocations, boot_allocations; } g_css_origin;
namespace host { void log(const char*) {} }
void css_origin_disarm() { assert(false); }
void css_origin_arm() {
''' + body(host, 'css_origin_arm') + r'''
}
void* h_native_alloc(uint32_t size) {
''' + body(host, 'h_native_alloc') + r'''
}
void h_native_free(void* ptr) {
''' + body(host, 'h_native_free') + r'''
}
int main() {
  auto* plain=h_native_alloc(64);
  assert(plain && g_css_origin.live_allocations.empty()); h_native_free(plain);
  css_origin_arm(); auto* boot=h_native_alloc(64);
  assert(g_css_origin.live_allocations.at(boot)==64);
  g_css_origin.boot_allocations=g_css_origin.live_allocations; h_native_free(boot);
  assert(g_css_origin.live_allocations.empty());
  *static_cast<char*>(boot)=42; // Retained only when armed and in the boot map.
  auto* later=h_native_alloc(32); assert(g_css_origin.live_allocations.at(later)==32);
  h_native_free(later); assert(g_css_origin.live_allocations.empty()); std::free(boot);
}
''')

    def test_capture_failure_disarms_and_entry_contains_exceptions(self):
        host = read('port/app/source_host.cpp')
        tick = body(host, 'css_origin_tick')
        capture = tick[tick.index('if (!run.boot.ready())'):tick.index('if (run.pending)')]
        self.assertIn('try {', capture)
        self.assertIn('catch (...)', capture)
        self.assertIn('css_origin_disarm();', capture)
        entry = body(host, 'h_console_frame_begin')
        self.assertIn('catch (const ExitRequested& stop)', entry)
        self.assertIn('guarded([&] { throw stop; });', entry)
        self.assertIn('catch (const std::exception& error)', entry)
        self.assertIn('host::die("console frame begin: %s", error.what());', entry)
        self.assertIn('catch (...)', entry)
        self.assertIn('catch (...) { host::die("css-origin: cannot mount proof card"); }',
                      body(host, 'h_console_origin'))
        # Execute the real host boundary with each new reset failure, without entering game C.
        self.run_cpp(r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <new>
#include <stdexcept>
struct ExitRequested { int code; };
struct Stopped { int code; };
struct Died {};
struct MuStatePod {};
static int fault;
static bool g_console_boot;
constexpr int MU_PRACTICE_SCENE=0;
struct Game { int (*practice)(int,int32_t*,int)=nullptr; void (*state_snapshot)(MuStatePod*)=nullptr; } g_game;
namespace host {
uint32_t retrace_count() { return 0; }
void retrace() {}
[[noreturn]] void die(const char*, ...) { throw Died{}; }
}
template<class F> void guarded(F f) {
  try { f(); } catch (const ExitRequested& stop) { throw Stopped{stop.code}; }
}
namespace yougame::console {
struct Pads {};
struct Engine {
  bool active() { return false; } bool match_active() { return false; }
  bool needs_sample() { return false; } void advance(Pads,MuStatePod) {}
} engine;
Engine& console_engine() { return engine; }
bool friends_handoff_join_enabled() { return false; }
namespace handoff_join { std::atomic<bool> host_css{false}; }
}
yougame::console::Pads console_hardware() { return {}; }
void friends_handoff_tick() {}
int css_join_tick() { return 0; }
int css_origin_tick() {
  if (fault==1) throw ExitRequested{7};
  if (fault==2) throw std::bad_alloc{};
  if (fault==3) throw 42;
  return -50;
}
int32_t h_console_frame_begin() {
''' + entry + r'''
}
int main() {
  assert(h_console_frame_begin()==-50);
  fault=1;
  try { h_console_frame_begin(); assert(false); } catch (const Stopped& stop) { assert(stop.code==7); }
  for (fault=2; fault<=3; ++fault) {
    try { h_console_frame_begin(); assert(false); } catch (const Died&) {}
  }
}
''')

    def test_card_session_end_allows_the_next_real_mount_to_write(self):
        host = read('port/app/source_host.cpp')
        self.assertIn('origin_end_card_session();', body(host, 'css_origin_tick'))
        self.run_cpp(r'''
#include <cassert>
bool g_card_mounted=true, g_origin_card_readonly=true;
void card_reset_mount() { g_card_mounted=false; }
void origin_end_card_session() {
''' + body(host, 'origin_end_card_session') + r'''
}
int main() {
  origin_end_card_session(); assert(!g_card_mounted && !g_origin_card_readonly);
}
''')

    def test_render_loop_only_locks_for_a_published_reset(self):
        threaded = read('port/runtime/gx/threaded_backend.cpp')
        loop = threaded[threaded.index('for (;;) {'):]
        self.assertLess(loop.index('if (reset_pending_.load(std::memory_order_acquire))'),
                        loop.index('std::lock_guard<std::mutex> lock(reset_mutex_)'))
        self.assertIn('reset_pending_.store(true, std::memory_order_release)',
                      body(threaded.replace('() override', '()'), 'reset_game_state'))

    def test_frame_zero_capture_precedes_console_digest_and_input(self):
        host = read('port/app/source_host.cpp')
        hook = body(host, 'h_console_frame_begin')
        self.assertLess(hook.index('css_origin_tick()'), hook.index('engine.advance('))
        tick = body(host, 'css_origin_tick')
        self.assertIn('if (!run.boot.ready())', tick)
        self.assertIn('run.boot.capture(run.spans)', tick)
        self.assertIn('native_savestate::hash_spans(run.spans)', tick)

    def test_disc_gate_and_order(self):
        reset = body(read('port/runtime/hle/yougame_css_origin.h'), 'reset_to_boot')
        operations = ['services.disc_quiet()', 'boot.restore()', 'services.restore_excluded()',
                      'services.flush_gpu()', 'services.reset_audio()', 'services.mount_card()',
                      'services.reset_host()']
        positions = [reset.index(x) for x in operations]
        self.assertEqual(positions, sorted(positions))
        host = body(read('port/app/source_host.cpp'), 'css_origin_tick')
        self.assertIn('!g_handoff_disc_depth && g_handoff_callbacks_pending && !g_handoff_callbacks_pending()', host)
        self.assertIn('retain request; retry next engine body', host)

    def test_every_card_mutator_refuses_readonly_before_mutation(self):
        host = read('port/app/source_host.cpp')
        for name in ['h_card_create', 'h_card_delete', 'h_card_rename', 'h_card_write',
                     'h_card_set_stat', 'h_card_format']:
            with self.subTest(name=name):
                fn = body(host, name).strip()
                self.assertTrue(fn.startswith('if (g_origin_card_readonly) return CARD_NOPERM;'))
        mount = body(host, 'origin_mount_card')
        self.assertIn('g_origin_card_readonly = true', mount)
        self.assertNotIn('ofstream', mount)
        self.assertNotIn('card_save(', mount)
        self.assertIn('origin_mount_card(g_css_origin.card)', body(host, 'card_mount_files'))

    def test_hook_guard_and_exact_logs(self):
        host = read('port/app/source_host.cpp')
        # Evaluate conditional inclusion, preserving nested directives without depending on headers.
        for enabled in [False, True]:
            stack, output = [True], []
            for line in host.splitlines():
                if line.startswith('#ifdef YOUGAME_AGENT_TEST_HOOK'):
                    stack.append(stack[-1] and enabled)
                elif line.startswith(('#if ', '#ifdef ', '#ifndef ')):
                    stack.append(stack[-1])
                elif line.startswith('#else'):
                    # The origin proof has no #else; unrelated platform branches are immaterial.
                    pass
                elif line.startswith('#endif'):
                    stack.pop()
                elif stack[-1]:
                    output.append(line)
            text = '\n'.join(output)
            for marker in ['YOUGAME_TEST_CSS_ORIGIN',
                           'css-origin hash=%016llx frame=%llu path=fresh',
                           'css-origin hash=%016llx frame=%llu path=reset',
                           'css-origin reset_ms=%.3f catchup_ms=%.3f']:
                self.assertEqual(marker in text, enabled, marker)
        self.assertIn('run.boot.differences(16)', host)
        self.assertIn('css-origin diff region=%zu offset=0x%llx size=%u first=%02x/%02x', host)

    def test_renderer_reset_is_synchronous_and_all_present_backends_opt_in(self):
        core = body(read('port/runtime/gx/gx_core.cpp'), 'capture_boot_state')
        for marker in ['reset_game_state()', 'g_texture_snapshots.clear()', 'g_tmem', 'g_buf_pos',
                       'g_native_draw_scopes.clear()', 'mark_discontinuity()']:
            self.assertIn(marker, core)
        for path in ['port/runtime/gx/gx_d3d11.cpp', 'port/runtime/gx/gx_d3d12.cpp',
                     'port/runtime/gx/gx_vulkan.cpp', 'port/linux/renderer_gl.cpp']:
            text = read(path)
            self.assertIn('supports_game_reset() const override { return true; }', text)
            self.assertIn('void reset_game_state() override', text)
        threaded = read('port/runtime/gx/threaded_backend.cpp')
        for marker in ['queue.discard_pending()', 'solver = SubFrameSolver{}',
                       'done.get()', 'renderer->reset_game_state()']:
            self.assertIn(marker, threaded)

    def test_scene_reentry_skips_booted_initializers_and_returns_through_c(self):
        patch = read('sourceport/patches/melee-native.patch')
        for marker in ['__builtin_setjmp(origin_context)', 'mu_css_origin_resume(1)',
                       'mu_css_origin_resume(2)', 'mu_css_origin_resume(4)',
                       'goto mu_origin_scene_dispatch', 'goto mu_origin_scene_body',
                       'goto mu_origin_body', 'mu_css_origin_skip_draw()']:
            self.assertIn(marker, patch)
        shim = body(read('sourceport/game/shim/mu_console_pad.c'), 'mu_console_frame_begin')
        self.assertLess(shim.index('mu_console_host_frame_begin()'), shim.index('mu_css_origin_restart()'))
        self.assertIn('result == -50', shim)

    def test_host_clock_restores_boundaries_and_discards_old_completions(self):
        clock = body(read('port/runtime/host/host.cpp'), 'capture_native_boot_clock')
        fixture = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
struct Cpu { uint64_t tb=17; } cpu_value, *cpu=&cpu_value;
static uint64_t g_next_retrace_tb=33;
static uint32_t g_retraces=4;
static std::atomic<uint32_t> g_profiler_frame{4};
static std::chrono::steady_clock::time_point g_next_frame;
static bool g_in_retrace=false, g_in_interrupt=false, g_frame_submitted=false;
static std::deque<int> g_completions;
static bool g_pe_finish_pending=false, g_pe_token_pending=false;
std::function<void()> capture_native_boot_clock() {
''' + clock + r'''
}
int main() {
  auto reset=capture_native_boot_clock();
  cpu->tb=900; g_next_retrace_tb=1000; g_retraces=30;
  g_in_retrace=g_in_interrupt=g_frame_submitted=true;
  g_completions.push_back(1); g_pe_finish_pending=g_pe_token_pending=true;
  reset();
  assert(cpu->tb==17 && g_next_retrace_tb==33 && g_retraces==4 && g_profiler_frame.load()==4);
  assert(!g_in_retrace && !g_in_interrupt && !g_frame_submitted);
  assert(g_completions.empty() && !g_pe_finish_pending && !g_pe_token_pending);
  return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='css-origin-clock-') as work:
            path = Path(work) / 'clock.cpp'
            path.write_text(fixture)
            exe = Path(work) / 'clock'
            subprocess.run([os.environ.get('CXX') or 'c++', '-std=c++17', str(path), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_live_dispatch_anchor_survives_multiple_scene_calls(self):
        source = read('sourceport/game/shim/mu_console_boot.c')
        self.assertIn('X(console_boot)', read('sourceport/game/shim/mu_exclusions.c'))
        self.assertIn('MU_EXCLUSIONS(console_boot, MU_EXCLUDE(mu_css_origin_context))', source)
        continuation = source[source.index('static void** mu_css_origin_context;'):]
        native = read('sourceport/game/include/mu_native.h')
        exclusions = native[native.index('typedef struct MuExclusion {'):native.index('/* Slippi online play, shim/mu_online.c:')]
        fixture = r'''
#include <assert.h>
static int flags, phase;
int mu_console_active(void) { return 1; }
void mu_console_load_save(void) {}
int mu_css_origin_host(int op) { int yes = flags & op; flags &= ~op; return yes != 0; }
''' + exclusions + continuation + r'''
static void another_scene(void) { flags=7; phase=1; mu_css_origin_restart(); assert(0); }
static void first_scene(void) { another_scene(); assert(0); }
int main(void) {
    void* origin_context[5];
    mu_css_origin_arm(origin_context);
    const MuExclusion* exclusions;
    assert(mu_exclusions_console_boot(&exclusions)==1);
    assert(exclusions[0].address==&mu_css_origin_context && exclusions[0].size==sizeof(void**));
    if (__builtin_setjmp(origin_context) == 0) first_scene();
    assert(phase==1);
    assert(mu_css_origin_resume(1) && mu_css_origin_resume(2) && mu_css_origin_resume(4));
    assert(!mu_css_origin_resume(1) && !mu_css_origin_resume(2) && !mu_css_origin_resume(4));
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='css-origin-') as work:
            path = Path(work) / 'continuation.c'
            path.write_text(fixture)
            exe = Path(work) / 'continuation'
            target = ['-arch', 'x86_64'] if platform.system() == 'Darwin' else []
            subprocess.run([os.environ.get('CC') or __import__('shutil').which('cc') or 'gcc', *target, '-std=c11', '-O2', str(path), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    @unittest.skipUnless(shutil.which('clang'), 'needs clang for cross-target syntax checks')
    def test_unsupported_continuation_targets_fail_with_a_clear_diagnostic(self):
        source = read('sourceport/game/shim/mu_console_boot.c')
        guard = source[:source.index('#include <mu_native.h>')]
        with tempfile.TemporaryDirectory(prefix='css-origin-target-') as work:
            path = Path(work) / 'target.c'
            path.write_text(guard)
            for target in ['arm64-apple-macos15', 'aarch64-linux-gnu']:
                result = subprocess.run(['clang', '-target', target, '-fsyntax-only', str(path)],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('CSS origin scene continuation requires x86-64', result.stderr)


if __name__ == '__main__':
    unittest.main()
