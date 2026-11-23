"""122.20: the ranked setup's same-colour hint ("Same color selected. Press X/Y to change", disc art), the
online select's "Press START to ..." and the Doubles panel's START/B lines name the keyboard's own keys too.

122.19: the online character select's prompts name a keyboard player's own keys, and a closed Doubles
home lets B out.

- "Hold Z to disconnect" and the other Z (and B) prompts: Melee's Z is E on the keyboard by default, so the
  prompt asks the host for the key bound to that button on a keyboard port (private command 0xDF), and
  keeps the GameCube name for a controller. What the keys do is unchanged: the keyboard's Z key is still A.
- A Doubles home whose room closed while it searched kept MU_LOBBY_M_SEARCHING (Rooms never sends the end
  of a search once the room is gone). The select takes the whole pad while it searches (mu_lobby_pads), so
  "Lobby closed. Hold B to exit" could not be left. The behaviour is unit tested in
  port/tests/yougame_lobby_screen_test.cpp; here, the wiring.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def body(source, name):
    at = source.index(name)
    at = source.index('{', at)
    depth, end = 1, at + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[at:end]


CLOSED_BACK_TEST = r"""
#include <stdio.h>
#include <string.h>
typedef unsigned int u32;
#define HSD_PAD_A 0x100u
#define HSD_PAD_B 0x200u
#define HSD_PAD_START 0x1000u
#define MU_LOBBY_HINT_CLOSED 18
typedef struct { unsigned char block_hint; } MuLobbyView;
#define CHECK(x) do { if (!(x)) { printf("FAIL %d %s\n", __LINE__, #x); return 1; } } while (0)
@FUNC@
int main(void)
{
    MuLobbyView closed, open;
    memset(&closed, 0, sizeof closed); closed.block_hint = MU_LOBBY_HINT_CLOSED;
    memset(&open, 0, sizeof open);
    CHECK(mu_f4_closed_back(&closed, HSD_PAD_B, -31.0f, -2.5f));          /* a B press, the hand anywhere */
    CHECK(mu_f4_closed_back(&closed, HSD_PAD_A, 20.0f, 24.0f));           /* A on BACK, coin in hand or not */
    CHECK(!mu_f4_closed_back(&closed, HSD_PAD_A, 0.0f, 10.0f));           /* A elsewhere stays Melee's */
    CHECK(!mu_f4_closed_back(&closed, HSD_PAD_START, 20.0f, 24.0f));
    CHECK(!mu_f4_closed_back(&closed, 0, 20.0f, 24.0f));                  /* only a press, never a held button */
    CHECK(!mu_f4_closed_back(&open, HSD_PAD_B, 20.0f, 24.0f));            /* an open lobby keeps Melee's B */
    CHECK(!mu_f4_closed_back(NULL, HSD_PAD_B, 20.0f, 24.0f));
    printf("ok\n");
    return 0;
}
"""


class KeyboardPromptTest(unittest.TestCase):
    def setUp(self):
        self.css = read("sourceport/game/shim/mu_slippi_css.c")
        self.host = read("port/app/source_host.cpp")
        self.window = read("port/runtime/host/window.cpp")

    def test_shim_asks_the_host(self):
        self.assertIn("#define CMD_BUTTON_LABEL 0xDF", self.css)
        fn = body(self.css, "int mu_host_button_label(int port, unsigned button_mask, char* out, int n)")
        self.assertIn("mu_online_abi_command(CMD_BUTTON_LABEL", fn)
        self.assertIn("reply[4096]", fn)   # the host refuses a reply buffer under its minimum capacity
        self.assertIn("got >= (unsigned int) n", fn)
        self.assertIn("int mu_host_button_label(int port, unsigned button_mask, char* out, int n);",
                      read("sourceport/game/include/mu_native.h"))

    def test_prompts_use_the_bound_keys(self):
        think = body(self.css, "static void text_think(HSD_GObj* gobj)")
        self.assertIn('char z_key[16] = "Z", b_key[16] = "B"', think)
        self.assertIn("mu_host_button_label(*ctrl_port, HSD_PAD_Z, z_key, sizeof z_key);", think)
        self.assertIn("mu_host_button_label(*ctrl_port, HSD_PAD_B, b_key, sizeof b_key);", think)
        for line in ('"Hold %s to disconnect", z_key', '"Press %s, then hold %s to update", z_key, b_key',
                     '"Press %s to clear error", z_key', '"Press %s to cancel", z_key',
                     '"%s: Practice vs CPU", z_key', '"Match closed. Hold %s to exit", b_key'):
            self.assertIn(line, think)
        for old in ('"Hold Z to %s"', '"Press Z, then hold B to update"', '"Press Z to %s"',
                    '"Z: Practice vs CPU"', '"Match closed. Hold B to exit"'):
            self.assertNotIn(old, think)

    def test_inputs_unchanged(self):
        # Only the words change: Z is still the pad's Z bit, read from the CSS's own port.
        inputs = body(self.css, "int mu_slippi_css_inputs(unsigned int trigger)")
        self.assertIn("gm_GetButtonsPressed((u8) *ctrl_port) & HSD_PAD_Z", inputs)
        self.assertNotIn("mu_host_button_label", inputs)

    def test_host_answers_keyboard_ports_only(self):
        self.assertIn("constexpr uint8_t CMD_BUTTON_LABEL = 0xDF;", self.host)
        handler = self.host[self.host.index("if (c == CMD_BUTTON_LABEL && n == 3)"):]
        handler = handler[:handler.index("return true;")]
        self.assertIn("host::input_keyboard_button_label(p[0], (uint16_t)(p[1] << 8 | p[2])", handler)
        # Console sessions answer nothing: every machine draws the same select there.
        self.assertRegex(self.host, r"command == CMD_BUTTON_LABEL\)\) \{   // the same on every console machine")
        fn = body(self.window, "bool input_keyboard_button_label(int port, uint16_t pad_button, char* out, size_t n)")
        self.assertIn("g_keyboard_port_mask.load", fn)
        self.assertIn("kActionPadBit[i] == pad_button", fn)
        self.assertIn("g_key_bindings.vk[i]", fn)   # the live binding, never a fixed key
        self.assertNotIn("'E'", fn)
        # The mask is the same keyboard_ports both reads publish, set before the UI lock.
        snap = self.window[self.window.index("struct UiSnapshot {"):self.window.index("void input_poll(PadState out[4]) {")]
        self.assertIn("if (keyboard_ports[i]) keyboard_mask |= (uint8_t)(1u << i);", snap)
        self.assertIn("g_keyboard_port_mask.store(keyboard_mask", snap)
        self.assertIn("bool input_keyboard_button_label(int port, uint16_t pad_button, char* out, size_t n);",
                      read("port/runtime/host/host.h"))


PANEL_KEYS_TEST = r"""
#include <stdio.h>
#include <string.h>
#define MU_LOBBY_NOTE 48
#define CHECK(x) do { if (!(x)) { printf("FAIL %d %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct {
    char line[3][32]; unsigned char spin[3]; const char* gray; char error[MU_LOBBY_NOTE]; char gray_text[40];
} MuF4Panel;
@FUNCS@
int main(void)
{
    MuF4Panel p;
    memset(&p, 0, sizeof p);
    strcpy(p.line[1], "Press START to search"); strcpy(p.error, "Lobby closed. Press B to leave");
    p.gray = "Press B to cancel";
    mu_f4_panel_keys(&p, "X", "Enter");   /* the keyboard's default B and START */
    CHECK(strcmp(p.line[1], "Press Enter to search") == 0);
    CHECK(strcmp(p.error, "Lobby closed. Press X to leave") == 0);
    CHECK(strcmp(p.gray, "Press X to cancel") == 0);
    memset(&p, 0, sizeof p);   /* a controller: no labels, the GameCube names stay */
    strcpy(p.line[1], "Press START to invite"); p.gray = "Press B to cancel";
    strcpy(p.error, "Lobby closed. Press B to leave");
    mu_f4_panel_keys(&p, "", "");
    CHECK(strcmp(p.line[1], "Press START to invite") == 0 && strcmp(p.gray, "Press B to cancel") == 0);
    CHECK(strcmp(p.error, "Lobby closed. Press B to leave") == 0);
    memset(&p, 0, sizeof p);   /* a name too long for the line keeps the old word, never overruns */
    strcpy(p.line[1], "Press START to search"); p.gray = "";
    mu_f4_panel_keys(&p, "B", "Right Shift Key");
    CHECK(strlen(p.line[1]) < sizeof p.line[1]);
    printf("ok\n");
    return 0;
}
"""


class KeyboardKeysOnTheOnlineScreensTest(unittest.TestCase):
    def setUp(self):
        self.css = read("sourceport/game/shim/mu_slippi_css.c")
        self.widgets = read("sourceport/game/shim/mu_ranked_widgets.c")
        self.setup = read("sourceport/game/shim/mu_ranked_setup.c")
        self.f4 = read("sourceport/game/include/mu_friends4_css.inc")

    def test_ranked_same_colour_hint_names_the_keys(self):
        keys = body(self.widgets, "void mu_ranked_widgets_keys(MuRankedWidgets* w, int port)")
        for bit in ("0x100,w->key_a", "0x200,w->key_b", "0x400,x", "0x800,y"):
            self.assertIn(bit, keys)
        self.assertIn("w->keyboard_xy=kx || ky;", keys)   # a controller keeps "X/Y" and the disc's art
        draw = body(self.widgets, "void mu_ranked_widgets_draw(MuRankedWidgets* w, const MuRankedSetupModel* m, const MuRankedSetupView* v)")
        self.assertIn("s->type == MU_SETUP_COLOR", draw[draw.index("color_hint="):])
        self.assertIn("visibility(&w->description,!color_hint &&", draw)
        self.assertIn('"Same color selected. Press %s to change",w->key_xy', draw)
        # The readable fallback lines name the keys too; no fixed "X/Y:" or "A:" left there.
        for old in ('"Arrows: character  X/Y: costume  A: choose  B: cancel"', '"Arrows: move  A: select  B: clear  X/Y: costume"',
                    '"OK   >Redo    A: confirm  B: redo"'):
            self.assertNotIn(old, draw)
        self.assertIn("void mu_ranked_widgets_keys(MuRankedWidgets*, int port);",
                      read("sourceport/game/shim/mu_ranked_widgets.h"))
        # The screen asks for its own pad's keys on load and every frame, before drawing.
        think = body(self.setup, "void mu_ranked_setup_think(void)")
        self.assertLess(think.index("mu_ranked_widgets_keys(widgets, port);"), think.rindex("mu_ranked_widgets_draw("))
        self.assertIn("mu_ranked_widgets_keys(widgets, gm_801677F0());", body(self.setup, "void mu_ranked_setup_load(void* data)"))

    def test_css_start_prompt_names_the_key(self):
        think = body(self.css, "static void text_think(HSD_GObj* gobj)")
        self.assertIn('start_key[16] = "START"', think)
        self.assertIn("mu_host_button_label(*ctrl_port, HSD_PAD_START, start_key, sizeof start_key);", think)
        self.assertIn('"Press %s to %s", start_key, action', think)
        self.assertNotIn('"Press START to %s"', think)

    def test_chat_hint_names_keyboard_keys(self):
        think = body(self.css, "static void text_think(HSD_GObj* gobj)")
        for bit in ("HSD_PAD_DPADUP", "HSD_PAD_DPADLEFT", "HSD_PAD_DPADRIGHT", "HSD_PAD_DPADDOWN"):
            self.assertIn("mu_host_button_label(*ctrl_port, %s," % bit, think)
        self.assertIn('"Chat: %s %s %s %s", du, dl, dr, dd', think)
        self.assertIn('set_text(ST_PRESS_D, "Use D-Pad to Chat");', think)

    def test_doubles_panel_names_the_keys(self):
        panel = body(self.f4, "static void mu_f4_panel(const MuLobbyView* v, int doubles, unsigned hint, int noted, const char* note, int mine)")
        at = panel.index("mu_f4_panel_text(v, hint, noted, mu_lobby_partner_want(), note, &p);")
        keys = panel.index("mu_f4_panel_keys(&p, b_key, start_key);")
        self.assertLess(at, keys)
        self.assertLess(keys, panel.index('mu_f4_panel_put(MU_F4_ST_HEADER, "Doubles", &mu_f4_white);'))
        self.assertIn("mu_host_button_label(port, HSD_PAD_B, b_key, sizeof b_key);", panel)
        self.assertIn("mu_host_button_label(port, HSD_PAD_START, start_key, sizeof start_key);", panel)

    def test_doubles_panel_keys_behave(self):
        # The game build sees the decomp MSL string.h, which has no strstr: the search is our own.
        self.assertNotIn("strstr(", self.f4)
        funcs = (self.f4[self.f4.index("static char* mu_f4_find("):self.f4.index("static void mu_f4_panel(const MuLobbyView* v")])
        source = PANEL_KEYS_TEST.replace("@FUNCS@", funcs)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "t.c"
            src.write_text(source)
            exe = Path(tmp) / "t"
            build = subprocess.run(["cc", "-std=c99", "-Wall", "-Werror", str(src), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout.strip(), "ok")


class ClosedHomeBackTest(unittest.TestCase):
    def test_closed_room_is_never_searching(self):
        screen = read("port/runtime/hle/yougame_lobby_screen.h")
        fn = body(screen, "inline MuLobbyView screen_view(const Control::View& v, const ScreenFlags& f)")
        self.assertIn("doubles && v.searching && !f.closed ? MU_LOBBY_M_SEARCHING : 0", fn)
        self.assertIn("f.armed && f.closed ? MU_LOBBY_HINT_CLOSED", fn)

    def test_searching_takes_the_pad(self):
        # Why it mattered: the searching branch zeroes the player's pad, B included.
        pads = body(read("sourceport/game/shim/mu_slippi_menu.c"), "void mu_lobby_pads(PADStatus* status)")
        search = pads[pads.index("if (lobby.view_ok && (lobby.view.mode & MU_LOBBY_M_SEARCHING))"):]
        search = search[:search.index("} else if")]
        self.assertIn("memset(&mine, 0, sizeof mine);", search)

    def test_unit_test_pins_it(self):
        test = read("port/tests/yougame_lobby_screen_test.cpp")
        self.assertIn("nor keep a closed Doubles home searching", test)

    def test_closed_back_leaves_on_b_or_a_on_back(self):
        # 11-22: on "Lobby closed" nothing left the select. Melee's B exit is a long hold and its BACK only
        # takes A with an empty hand, and the closed lobby leaves the coin in the hand. A B press, or A on
        # BACK, now leaves (the select's own exit value 2: lobby_css_decide sends it to the Online menu).
        f4 = read("sourceport/game/include/mu_friends4_css.inc")
        at = f4.index("static int mu_f4_closed_back(")
        func = f4[at:f4.index("\n}\n", at) + 3]
        source = CLOSED_BACK_TEST.replace("@FUNC@", func)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "t.c"
            src.write_text(source)
            exe = Path(tmp) / "t"
            build = subprocess.run(["cc", "-std=c99", "-Wall", "-Werror", str(src), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout.strip(), "ok")

    def test_closed_back_is_wired_into_the_frame(self):
        f4 = read("sourceport/game/include/mu_friends4_css.inc")
        frame = body(f4, "static void mu_friends4_css_frame(void)")
        call = frame.index("mu_f4_closed_back(mu_f4.view_ok ? &mu_f4.view : NULL, HSD_PadCopyStatus[mine].trigger,")
        self.assertLess(frame.index("mine = mu_f4_mine();"), call)
        self.assertLess(frame.index("if (v) mu_f4.view = *v;"), call)
        exit_ = frame[call:frame.index("mu_f4_log_a(mine);")]
        self.assertIn("mnCharSel_804A0BC0[mine]->xC, mnCharSel_804A0BC0[mine]->x10", exit_)
        self.assertIn("mnCharSel_804D6CF6 = 2;", exit_)
        self.assertIn("if (!mu_f4.leaving && mnCharSel_804D6CF6 == 0 && mnCharSel_804A0BC0[mine] &&", frame)
        text = body(f4, "static void mu_f4_panel_text(")
        self.assertIn('strcpy(p->error, "Lobby closed. Press B to leave");', text)
        self.assertNotIn("Hold B to exit", f4)
        decide = body(read("sourceport/game/shim/mu_slippi_menu.c"), "static int lobby_css_decide(GameModeState* state)")
        self.assertIn("if (css->pending_scene_change == 2) {", decide)
        self.assertIn("gm_ChangeGameModeAfterCurrentScene(GM_MENU);", decide)

    def test_closed_inviting_lobby_keeps_b(self):
        # A closed lobby refuses CANCEL, so the inviting branch would mask B until the picker timed out.
        pads = body(read("sourceport/game/shim/mu_slippi_menu.c"), "void mu_lobby_pads(PADStatus* status)")
        self.assertIn("(lobby.view.flags & MU_LOBBY_F_INVITING) && lobby.view.block_hint != MU_LOBBY_HINT_CLOSED) {", pads)

    def test_closed_lobby_doors_stop_offering_invite(self):
        # Once the lobby closed, the empty doors drop their "Invite" label (its hint, under the doors, sat on
        # P2 and P3's labels) and A on an open door no longer opens the invite picker.
        f4 = read("sourceport/game/include/mu_friends4_css.inc")
        texts = body(f4, "static void mu_f4_texts(int mine)")
        guard = texts.index("&& !doubles && v->block_hint != MU_LOBBY_HINT_CLOSED) {")
        self.assertLess(guard, texts.index('"Inviting..." : "Invite"'))
        self.assertLess(guard, texts.index('"CPU Lv %u"'))
        self.assertLess(texts.index("buf[0] = 0;"), guard)   # the label is blank, not last frame's
        invite = body(f4, "static void mu_f4_invite(int mine)")
        closed = invite.index('else if (mu_f4.view.block_hint == MU_LOBBY_HINT_CLOSED) why = "the lobby closed";')
        self.assertLess(closed, invite.index("for (i = 0; why == NULL && i < MU_LOBBY_SEATS; ++i) {"))


if __name__ == "__main__":
    unittest.main()
