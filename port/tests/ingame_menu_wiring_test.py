"""Execute the production in-game menu with real ImGui keyboard/gamepad navigation."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from online_menu_test import compiler_command
from yougame_console_wiring_test import body

ROOT = Path(__file__).resolve().parents[2]


class IngameMenuTest(unittest.TestCase):
    def test_focused_activation_safe_settings_return_and_truthful_logs(self):
        ui = (ROOT / 'port/runtime/gx/pc_settings.cpp').read_text()
        def between(start, end):
            return ui[ui.index(start):ui.index(end, ui.index(start))]
        agent = between('  ImGuiID agent_menu_focus = 0;', '\n#endif\n  slippi::native_practice::Snapshot')
        opening = between('  bool menu_main_focus_pending = false;', '  if (g_guest_options_open_request.exchange(')
        toggle = between('  if (host::window_take_settings_toggle()) {', '  if (host::window_take_legacy_settings_toggle()')
        escape = between('  if (phob_native_open() && host::window_take_escape()) {', '\n#ifdef YOUGAME_AGENT_TEST_HOOK\n  agent_menu_state();')
        draw = between('  // ---- the Esc menu ----', '  const bool practice_was_open = state.practice_open;')
        # The agent hook's focus block ends at its own #endif (test-only lines may follow it).
        focus = between('  agent_menu_state();\n  {\n    static ImGuiID last_focus', '\n#endif\n')
        visuals = (ROOT / 'port/runtime/gx/pc_settings_visuals.inl').read_text()
        helpers = ('static bool settings_nav_frame_on_item() {' + body(visuals, 'settings_nav_frame_on_item') + '}\n' +
                   'static ImU32 settings_mix_color(ImU32 a, ImU32 b, float amount) {' + body(visuals, 'settings_mix_color') + '}\n' +
                   'static ImFont* settings_heading_font() {' + body(visuals, 'settings_heading_font') + '}\n')
        harness = r'''
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
static int checks, exits;
static bool app, escape_pressed, settings_toggle, phob, signin;
static std::vector<std::string> logs;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); for (auto& l:logs) std::fprintf(stderr,"%s\n",l.c_str()); std::abort(); } } while(0)
namespace host {
void log(const char* fmt, ...) { char b[512]; va_list a; va_start(a,fmt); vsnprintf(b,sizeof b,fmt,a); va_end(a); logs.emplace_back(b); }
void request_user_exit() { ++exits; logs.emplace_back("exit requested"); }
bool window_take_escape() { bool b=escape_pressed; escape_pressed=false; return b; }
bool window_take_settings_toggle() { bool b=settings_toggle; settings_toggle=false; return b; }
namespace test_env { const char* getenv(const char*) { return nullptr; } }   // no test variables in this run
}
namespace yougame {
bool stdio_bridge() { return app; }
namespace parity { const char* kMenuHint="F1 MENU"; }
}
static bool phob_native_open() { return phob; }
static void phob_native_close() { phob=false; }
struct State {
  bool open=false, menu_open=false, menu_quit=false, menu_quit_focus_pending=false;
  bool practice_open=false, practice_release_capture=false, fill_window=false;
  bool legacy_presentation=false;
  int legacy_saved_appearance=0, rebind_action=-1, active_tab=0;
  bool clean_detail_open=false, dashboard_detail_open=false, gd_detail_open=false, radial_detail_open=false, wide_detail_open=false;
} state;
struct Options {
  int overlay_style=0, overlay_palettes[1]{};
  bool settings_custom_color_enabled=false;
  float settings_custom_color[3]{};
} options;
static bool g_settings_custom_color_enabled, g_hud_match, g_hud_fight;
static int g_settings_palette;
static ImVec4 g_settings_custom_color;
static ImFont* g_settings_heading_font;
static ImU32 settings_accent(int) { return IM_COL32(120,60,180,255); }
''' + helpers + r'''
static void reset_settings_home(const char*) {}
static void settings_back_page(State& state, int appearance) {
  if (appearance == 0 && state.clean_detail_open) state.clean_detail_open = false;
  else if (appearance == 1 && state.dashboard_detail_open) state.dashboard_detail_open = false;
  else if (appearance == 2 && state.gd_detail_open) state.gd_detail_open = false;
  else if (appearance == 3 && state.radial_detail_open) state.radial_detail_open = false;
  else if (appearance == 4 && state.wide_detail_open) state.wide_detail_open = false;
  else state.open = false;
}
namespace settings_view {
struct Saved { int tab = -1; bool page = false; } g_saved;
void restore_nodes() {}
void save(const Options&, int, bool) {}
}
static std::atomic<int> g_initial_tab{-1};
static bool settings_fills_window() { return false; }
struct Close { void store(bool, std::memory_order) {} } g_close_requested;
static void frame(State& state=::state, Options& options=::options) {
  ImGui::NewFrame();
  const bool launcher_window=state.fill_window;
  const bool signin_up=signin;   // 122.31: YouGame's Sign in by code box (signin_overlay::open())
''' + agent + '\n' + opening + toggle + escape + r'''
  agent_menu_state();
''' + draw + focus + r'''
  ImGui::Render();
}
static void settle() { frame(); frame(); }
static void key(ImGuiKey k, bool down) { ImGui::GetIO().AddKeyEvent(k,down); frame(); }
static void tap(ImGuiKey k) { key(k,true); key(k,false); }
static ImGuiID id(const char* label) {
  auto* w=ImGui::FindWindowByName("##esc_menu"); CHECK(w); return w->GetID(label);
}
static void focused(const char* label) { CHECK(ImGui::GetCurrentContext()->NavId==id(label)); }
static bool has(const char* line) { return std::find(logs.begin(),logs.end(),line)!=logs.end(); }
static int activations() { int n=0; for(auto& l:logs) if(l.find("agent test: menu activated")!=std::string::npos) ++n; return n; }
static void open_main() {
  state={}; state.open=!app; options.overlay_style=0; escape_pressed=true; frame(); settle();
  CHECK(state.menu_open && !state.menu_quit && !state.menu_quit_focus_pending);
  focused("##resume");
}
static void open_confirm() {
  open_main(); tap(ImGuiKey_RightArrow); settle(); focused("##settings");
  tap(ImGuiKey_RightArrow); settle(); focused("##quit");
  tap(ImGuiKey_Enter); settle();
  CHECK(state.menu_open && state.menu_quit && !state.menu_quit_focus_pending);
  focused("##confirm_quit"); CHECK(exits==0);
}
static int vertices(ImU32 color) {
  int count=0; auto* w=ImGui::FindWindowByName("##esc_menu"); CHECK(w);
  for (const auto& v:w->DrawList->VtxBuffer) if (v.col==color) ++count;
  return count;
}
int main() {
  ImGui::CreateContext(); auto& io=ImGui::GetIO();
  io.IniFilename=nullptr; io.LogFilename=nullptr;
  io.DisplaySize=ImVec2(1280,720); io.DeltaTime=1.f/60;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  io.ConfigInputTrickleEventQueue=false;
  unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
  const auto nav_color=IM_COL32(20,200,40,255);
  ImGui::GetStyle().Colors[ImGuiCol_NavCursor]=ImGui::ColorConvertU32ToFloat4(nav_color);
  app=true; open_main();
  const auto outline=settings_mix_color(settings_accent(0),IM_COL32_WHITE,.75f);
  CHECK(ImGui::GetCurrentContext()->NavCursorVisible);
  CHECK(vertices(nav_color)==0); CHECK(vertices(outline)>0);
  // Hover still lifts/fills, with no outline when keyboard/pad cursor visibility is off.
  auto* menu=ImGui::FindWindowByName("##esc_menu");
  ImGui::SetNavCursorVisible(false);
  io.AddMousePosEvent(menu->Pos.x+65,menu->Pos.y+90); frame();
  CHECK(!ImGui::GetCurrentContext()->NavCursorVisible);
  CHECK(vertices(nav_color)==0 && vertices(outline)==0);
  io.AddMousePosEvent(-100,-100); frame();
  // Confirm and cancel use the same slanted focus rendering.
  open_confirm(); CHECK(vertices(nav_color)==0 && vertices(outline)>0);
  tap(ImGuiKey_RightArrow); settle(); focused("##cancel_quit");
  CHECK(vertices(nav_color)==0 && vertices(outline)>0);
  // Each app/standalone case starts in-game and reaches confirmation through the real main rows.
  for (bool under_app : {true,false}) {
    app=under_app;
    // Standalone Esc starts in Settings, as in production; the next Esc opens the main menu.
    if (!app) { state={}; escape_pressed=true; frame(); CHECK(state.open && !state.menu_open); }
    // Main-menu entry below uses the actual settings-return path for standalone.
    for (auto input : {ImGuiKey_Enter,ImGuiKey_KeypadEnter,ImGuiKey_GamepadFaceDown}) {
      open_confirm();
      logs.clear(); tap(ImGuiKey_RightArrow); settle(); focused("##cancel_quit");
      key(input,true);
      CHECK(exits==0 && !state.menu_open && !state.menu_quit);
      CHECK(activations()==1);
      CHECK(has("[yougame] agent test: menu activated CANCEL (##cancel_quit)"));
      CHECK(has("menu: activated CANCEL"));
      CHECK(has("[yougame] agent test: menu closed"));
      CHECK(std::find(logs.begin(),logs.end(),"[yougame] agent test: menu focused CANCEL") <
            std::find(logs.begin(),logs.end(),"[yougame] agent test: menu activated CANCEL (##cancel_quit)"));
      CHECK(!has("exit requested"));
      CHECK(std::find(logs.begin(),logs.end(),"menu: activated CANCEL") <
            std::find(logs.begin(),logs.end(),"[yougame] agent test: menu closed"));
      key(input,false);
      open_confirm();
      logs.clear(); key(input,true);
      CHECK(exits==1 && !state.menu_open);
      CHECK(activations()==1);
      CHECK(has(under_app ? "[yougame] agent test: menu activated BACK TO YOUGAME (##confirm_quit)" :
                            "[yougame] agent test: menu activated QUIT (##confirm_quit)"));
      CHECK(!has("menu: activated CANCEL"));
      CHECK(std::find(logs.begin(),logs.end(),under_app ? "menu: activated BACK TO YOUGAME" : "menu: activated QUIT") <
            std::find(logs.begin(),logs.end(),"exit requested"));
      key(input,false); exits=0;
    }
    // Mouse-only Enter fallback, with neither quit button carrying a navigation ID.
    for (auto input : {ImGuiKey_Enter,ImGuiKey_KeypadEnter}) {
      open_confirm();
      ImGui::GetCurrentContext()->NavId=0; logs.clear(); key(input,true);
      CHECK(exits==1 && !state.menu_open && activations()==1);
      key(input,false); exits=0;
    }
    // A stale confirmation must never survive an opening, from Settings or gameplay.
    for (int style=0;style<8;++style) for (bool stale : {false,true}) {
      app=under_app; state={}; state.open=true; state.menu_quit=stale;
      state.menu_quit_focus_pending=stale; options.overlay_style=style;
      escape_pressed=true; frame(); settle();
      CHECK(!state.open && !state.menu_quit && !state.menu_quit_focus_pending);
      if (style<5) { CHECK(state.menu_open); focused("##resume"); }
      else CHECK(!state.menu_open);
      CHECK(exits==0);
    }
    app=true; options.overlay_style=0;
    for (bool chord : {false,true}) {
      state={}; state.menu_quit=true; state.menu_quit_focus_pending=true;
      if(chord) settings_toggle=true; else escape_pressed=true;
      frame(); settle(); CHECK(state.menu_open && !state.menu_quit && !state.menu_quit_focus_pending);
      focused("##resume");
      // BACK TO GAME, activated from the reset focus, cannot quit.
      tap(ImGuiKey_Enter); CHECK(!state.menu_open && exits==0);
    }
  }
  // Build 103: the bar scales with the window height (720p units, 1.5x at 1080p), stays inside the
  // window, and its background is near-opaque so the game does not show through it.
  for (bool quit : {false,true}) {
    struct Size { float w,h, menu_w,menu_h; } sizes[] = {{1280,720,760,172},{1920,1080,1140,258},{1000,1080,976,220},
                                                       {640,480,456,103}};   // 0.9.218: a small window shrinks it (0.6 of full size)
    for (const auto& z : sizes) {
      io.DisplaySize=ImVec2(z.w,z.h); app=true;
      if (quit) open_confirm(); else open_main();
      auto* bar=ImGui::FindWindowByName("##esc_menu");
      CHECK(std::fabs(bar->Size.x-z.menu_w)<0.6f && std::fabs(bar->Size.y-z.menu_h)<0.6f);
      CHECK(bar->Pos.x>=0 && bar->Pos.y>=0 && bar->Pos.x+bar->Size.x<=z.w && bar->Pos.y+bar->Size.y<=z.h);
      CHECK(vertices(IM_COL32(8,10,20,242))>0);
      for (const auto& v:bar->DrawList->VtxBuffer) CHECK(v.pos.x>=bar->Pos.x-1 && v.pos.x<=bar->Pos.x+bar->Size.x+1 &&
                                                          v.pos.y>=bar->Pos.y-1 && v.pos.y<=bar->Pos.y+bar->Size.y+1);
      tap(ImGuiKey_Escape); state={}; frame();
    }
  }
  io.DisplaySize=ImVec2(1280,720);
  // 122.31: the sign-in box opened over the Practice menu takes over and shuts that menu; without the
  // box the Practice menu stays open.
  state={}; state.practice_open=true; signin=false; frame(); CHECK(state.practice_open);
  state={}; state.practice_open=true; signin=true; frame(); CHECK(!state.practice_open && !state.menu_open && !state.open);
  signin=false; state={}; frame();
  ImGui::DestroyContext();
  std::printf("in-game menu: %d checks; real ImGui Right/Enter/KeypadEnter/pad A, safe Settings return, stale-state reset, activation log order\n",checks);
}
'''
        # The production launcher-close branch uses the standard memory-order enum.
        harness = '#include <atomic>\n' + harness
        with tempfile.TemporaryDirectory(prefix='ingame-menu-') as directory:
            cpp, exe = Path(directory) / 'menu.cpp', Path(directory) / 'menu'
            cpp.write_text(harness)
            sdk = ['-isysroot', '/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk'] if sys.platform == 'darwin' else []
            imgui = ROOT / 'port/third_party/imgui'
            subprocess.run([*compiler_command(), '-std=c++17', *sdk, '-DYOUGAME_AGENT_TEST_HOOK',
                            '-I', str(imgui), str(cpp),
                            *(str(imgui / f'imgui{part}.cpp') for part in ('', '_draw', '_widgets', '_tables')),
                            '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, cwd=directory)


if __name__ == '__main__':
    unittest.main()
