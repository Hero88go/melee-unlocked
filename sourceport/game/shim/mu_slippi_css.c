/* Native Slippi online character select screen (the online CSS, major 8 state 0).
 *
 * Native form of the Online/Menus/CSS codes of GALE01r2.ini that are not tied to mncharsel.c's
 * file-static state: SceneLoadCSS (the CSS data table and slpCSS.dat), FetchMatchInfo (B3 once
 * per cursor think), HandleInputsOnCSS (the lock-in / search / cancel machine and its host
 * commands), LoadCSSText (user display, status text, spinners, error lines, mode title),
 * SkipReturnToCssSound and the unselect / colour-change guards. The B5/B4 senders and the
 * r13-0x5018 callback body are shared with name entry and the SSS (mu_slippi_sss.c).
 * mncharsel.c calls in from #ifdef MU_NATIVE blocks; the Teams button, cursor and token colour
 * and CSP overrides live there because they touch its statics.
 *
 * Nothing here runs unless mu_slippi_menus_enabled() and the online CSS predicate hold.
 * Chat (SlippiCSS.dat) lives in mu_slippi_chat.c: it reads this table's MSRB and slpCSS and sets
 * the chat-window byte through the accessors at the end of this file.
 */
#include <string.h>
#include <dolphin/dvd.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/types.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/sislib.h>
#include <mu_native.h>

extern char mnNameNew_CurrentNameText[];

enum {
    SB_RAND = -2,   /* stage behavior: random */
    SB_NOTSEL = -1, /* stage behavior: use the opponent's */
    DISCONNECT_HOLD_DELAY = 0x30,
    SPINNER_TRANSITION_FRAMES = 15,
    FRAME_MAX = 2 * SPINNER_TRANSITION_FRAMES,
};

/* slpCSS symbol (Online.s "slpCSS Symbol Structure"). */
typedef struct SlpCss {
    DISC_PTR(void) chat_select;
    DISC_PTR(void) chat_msg;
    DISC_PTR(HSD_MatAnimJoint) mode;
    DISC_PTR(void) connect_help;
} DISC_STRUCT SlpCss;

/* The CSS data table (CSSDT). A new, zeroed one per CSS scene load, as SceneLoadCSS allocates. */
static struct {
    MuMatchState msrb; /* CSSDT_MSRB_ADDR: only the per-think B3 writes it */
    SlpCss* slpcss;
    HSD_Text* text;
    u8 spinner[3];
    u16 frame_counter;
    u8 prev_lock_in;
    u8 prev_connected;
    u8 z_hold_timer;
    u8 chat_window_opened; /* set by the chat window (mu_slippi_chat.c) */
    u8 team_idx;
} dt;

/* mncharsel.c state the codes read through r13 (addresses of its statics). */
static CSSData** css_ptr;         /* -0x49F0 mnCharSel_804D6CB0 */
static u8* char_chosen;           /* -0x49A9 mnCharSel_804D6CF7 */
static s8* ctrl_port;             /* -0x49B0 mnCharSel_804D6CF0 */
static u8* scene_request;         /* -0x49AA mnCharSel_804D6CF6 */
static s8* name_entry_port;       /* -0x49A7 mnCharSel_804D6CF9 */
static HSD_JObj** single_menu;    /* -0x49E0 mnCharSel_804D6CC0 */

/* LoadCSSText's user display (FG_UserDisplay LDB). */
static HSD_Text* user_text;
static u8 user_display_mode;

static const GXColor color_white = { 0xFF, 0xFF, 0xFF, 0xFF };
static const GXColor color_gray = { 0x8E, 0x91, 0x96, 0xFF };
static const GXColor color_red = { 0xFF, 0x00, 0x00, 0xFF };
static const GXColor spinner_done_color = { 0x33, 0xFF, 0x2F, 0xFF };
static const GXColor spinner_wait_color = { 0x3C, 0xBC, 0xFF, 0xFF };

static const char spinner_str[2][3] = { { (char) 0x81, 0x7B, 0 }, { (char) 0x81, 0x7E, 0 } };
static const char spinner_done_str[3] = { (char) 0x81, 0x7C, 0 };

int mu_slippi_css_online(void)
{
    return mu_slippi_menus_enabled() && mu_slippi_on_online_css();
}

int mu_slippi_css_teams(void)
{
    return mu_slippi_css_online() && mu_slippi_state()->mode == MU_SLP_MODE_TEAMS;
}

int mu_slippi_css_local_ready(void)
{
    return dt.msrb.local_ready;
}

int mu_slippi_css_team_idx(void)
{
    return dt.team_idx;
}

/* InitTeamToggleButton: the data table and the injection's own byte (kept across CSS loads). */
void mu_slippi_css_set_team_idx(int team)
{
    dt.team_idx = (u8) team;
    mu_slippi_state()->team_idx = (u8) team;
}

/* 2 of every 3 bytes of the name-entry text: FMTB_OPP_CONNECT_CODE and the "--//--" template. */
static void code_sjis18(u8 out[18])
{
    const u8* src = (const u8*) mnNameNew_CurrentNameText;
    int i;
    for (i = 0; i < 9; i++) {
        out[i * 2] = src[i * 3];
        out[i * 2 + 1] = src[i * 3 + 1];
    }
}

/* ---- SceneLoadCSS (mnCharSel_Scene_OnEnter, after css_models) ---- */

void mu_slippi_css_enter(CSSData** css, u8* chosen, s8* port, u8* request, s8* name_port,
                         HSD_JObj** menu_root)
{
    MuSlippiMenuState* st = mu_slippi_state();
    css_ptr = css;
    char_chosen = chosen;
    ctrl_port = port;
    scene_request = request;
    name_entry_port = name_port;
    single_menu = menu_root;

    /* FN_InitBuffers: nothing to allocate natively. CSS data table zeroed. */
    memset(&dt, 0, sizeof dt);

    /* The injection byte starts at 1 (red) and only ever holds 1-3. */
    if (st->team_idx == 0) {
        st->team_idx = 1;
    }
    if (st->mode == MU_SLP_MODE_TEAMS) {
        dt.team_idx = st->team_idx;
    }

    /* slpCSS.dat, symbol slpCSS. The host's system-file layer serves it; without it the mode title
     * keeps the CSS's own texture instead of taking the game down. */
    if (DVDConvertPathToEntrynum("slpCSS.dat") >= 0) {
        HSD_Archive* ar = lbArchive_LoadArchive("slpCSS.dat");
        dt.slpcss = ar != NULL ? HSD_ArchiveGetPublicAddress(ar, "slpCSS") : NULL;
    }
    mu_slippi_sss_set_slpcss(dt.slpcss);   /* name entry's connect help */
}

/* ---- FetchMatchInfo (mnCharSel_CursorThink, inside the no-scene-request branch) ---- */

void mu_slippi_css_poll(void)
{
    mu_slippi_load_match_state(NULL);
    dt.msrb = *mu_slippi_match_state();
}

/* ---- HandleInputsOnCSS helpers ---- */

/* FN_TX_LOCK_IN and FN_TX_FIND_MATCH are shared with name entry and the SSS (mu_slippi_sss.c),
 * so every B5/B4 payload comes from one place. */
static void tx_lock_in(int sb)
{
    mu_slippi_sss_lock_in(sb);
}

/* FN_LOCK_IN_AND_SEARCH */
static void lock_in_and_search(int sb)
{
    mu_slippi_sss_lock_in(sb);
    mu_slippi_sss_find_match();
}

/* FN_LOAD_CODE_ENTRY */
static void load_code_entry(void)
{
    MuSlippiMenuState* st = mu_slippi_state();
    st->code_entry = 1;
    st->callback = MU_SLP_CB_CODE_LOCK_IN;
    *name_entry_port = *ctrl_port;
    *scene_request = 4;
}

/* The stage the loser already picked: the asm reads the u16 at +0x1E of the VS data's match struct
 * (gmMainLib_804D3EE0 + 1424 + 8), in the console's byte order. */
static int picked_stage(void)
{
    const u8* b = (const u8*) &gmVsMelee_GetVsData()->start.rules;
    return (b[0x1E] << 8) | b[0x1F];
}

/* HandleInputsOnCSS at fn_80262F44's `mnCharSel_804D6CF2 == 0 && START` test (only called when
 * mnCharSel_804D6CF2 == 0). Returns 1 when both players are ready: the caller continues into the
 * vanilla START path. */
int mu_slippi_css_inputs(unsigned int trigger)
{
    MuSlippiMenuState* st = mu_slippi_state();
    const MuMatchState* m = &dt.msrb;
    u8 prev, cur;

    /* Sounds: lock-in 1 -> 0 "back"; ANY -> ERROR "error"; CONNECTED -> ANY "back". */
    prev = dt.prev_lock_in;
    cur = m->local_ready;
    dt.prev_lock_in = cur;
    if (prev == 1 && cur == 0) {
        lbAudioAx_80024030(0);
    } else {
        prev = dt.prev_connected;
        cur = m->connection_state;
        dt.prev_connected = cur;
        if (prev != MU_SLP_MM_ERROR && cur == MU_SLP_MM_ERROR) {
            lbAudioAx_80024030(3);
        } else if (prev == MU_SLP_MM_CONNECTION_SUCCESS && cur != MU_SLP_MM_CONNECTION_SUCCESS) {
            lbAudioAx_80024030(0);
        }
    }

    switch (m->connection_state) {
    case MU_SLP_MM_IDLE:
        if (dt.chat_window_opened || !(trigger & HSD_PAD_START) || gm_801A4BB8() == 0) {
            return 0;
        }
        st->is_winner = MU_SLP_WINNER_NULL;
        st->chose_stage = 0;
        if (*char_chosen == 0) {
            return 0;
        }
        switch (st->mode) {
        case MU_SLP_MODE_RANKED:
        case MU_SLP_MODE_UNRANKED:
        case MU_SLP_MODE_PARTY:
            lock_in_and_search(SB_RAND);
            break;
        case MU_SLP_MODE_DIRECT:
        case MU_SLP_MODE_TEAMS:
            load_code_entry();
            break;
        default:
            break;   /* the console stalls (b 0x0) */
        }
        return 0;

    case MU_SLP_MM_INITIALIZING:
    case MU_SLP_MM_MATCHMAKING:
    case MU_SLP_MM_OPPONENT_CONNECTING:
    case MU_SLP_MM_ERROR:
        if (trigger & HSD_PAD_Z) {
            mu_slippi_cleanup_connections();
        }
        return 0;

    case MU_SLP_MM_CONNECTION_SUCCESS:
        break;

    default:
        return 0;
    }

    /* Connected: hold Z to disconnect. */
    if ((u32) gm_GetButtonsPressed((u8) *ctrl_port) & HSD_PAD_Z) {
        dt.z_hold_timer++;
        if (dt.z_hold_timer > DISCONNECT_HOLD_DELAY) {
            dt.z_hold_timer = 0;
            mu_slippi_cleanup_connections();
            return 0;
        }
    } else {
        dt.z_hold_timer = 0;
    }

    if (m->local_ready == 0) {
        int advance = (trigger & HSD_PAD_START) != 0;
        if (!advance && (st->mode == MU_SLP_MODE_DIRECT || st->mode == MU_SLP_MODE_TEAMS) &&
            st->is_winner == MU_SLP_WINNER_LOST && st->chose_stage == 1)
        {
            advance = 1;
        }
        if (advance && *char_chosen != 0 && gm_801A4BB8() != 0) {
            switch (st->mode) {
            case MU_SLP_MODE_UNRANKED:
            case MU_SLP_MODE_PARTY:
                tx_lock_in(SB_RAND);
                break;
            case MU_SLP_MODE_DIRECT:
            case MU_SLP_MODE_TEAMS:
                if (st->is_winner == MU_SLP_WINNER_LOST) {
                    if (st->chose_stage != 0) {
                        tx_lock_in(picked_stage());
                    } else {
                        /* Teams bit for the doubles FoD rule, then leave for the SSS. */
                        CSSData* css = *css_ptr;
                        css->vs.start.rules.is_teams = st->mode == MU_SLP_MODE_TEAMS ? 1 : 0;
                        *scene_request = 1;
                        st->callback = MU_SLP_CB_SSS_LOCK_IN;
                        return 0;
                    }
                } else if (st->is_winner == MU_SLP_WINNER_WON) {
                    tx_lock_in(SB_NOTSEL);
                }
                /* any other value stalls on the console (b 0x0) */
                break;
            default:
                break;   /* Ranked: the console stalls (b 0x0) */
            }
        }
    }

    /* Both ready: start. Reads the table's MSRB, which the B5 above did not refresh. */
    return (m->local_ready & m->remote_ready) != 0;
}

/* PreventA/BPressCharUnselect: no unselect while chat is open or locked in. */
int mu_slippi_css_block_unselect(void)
{
    return mu_slippi_css_online() && (dt.chat_window_opened != 0 || dt.msrb.local_ready != 0);
}

/* PreventColorChange: never in Teams (the colour follows the team), else not while locked in. */
int mu_slippi_css_block_costume_change(void)
{
    if (!mu_slippi_css_online()) {
        return 0;
    }
    return mu_slippi_state()->mode == MU_SLP_MODE_TEAMS || dt.msrb.local_ready != 0;
}

/* SkipReturnToCssSound (mnCharSel_802640A0, before the voice of any CSS with match_type != 0,
 * whenever the menus flag is on, as on Legacy). Returns 1 to skip the voice. */
int mu_slippi_css_skip_return_sound(void)
{
    MuSlippiMenuState* st = mu_slippi_state();
    MuMatchState tmp;
    if (st->code_entry != 0) {
        /* Back from connect-code entry. The autocomplete buffers are name entry's own
         * (code_scratch); nothing to free natively. */
        st->code_entry = 0;
        return 1;
    }
    /* Back from the SSS locked in: "choose your character" would be wrong. Its own B3 buffer. */
    mu_slippi_load_match_state(&tmp);
    return tmp.local_ready != 0;
}

/* ---- LoadCSSText ---- */

/* FG_CreateSubtext, no outlines. */
static int create_subtext(HSD_Text* t, const GXColor* color, f32 scale, f32 x, f32 y)
{
    GXColor c = *color;
    int idx = HSD_SisLib_803A6B98(t, x, y, "");
    HSD_SisLib_803A7548(t, idx, scale, scale);
    HSD_SisLib_803A74F0(t, idx, &c);
    HSD_SisLib_803A70A0(t, idx, (char*) "");
    return idx;
}

static void set_text(int idx, const char* s)
{
    HSD_SisLib_803A70A0(dt.text, idx, (char*) "%s", s);
}

static void set_text_fmt(int idx, const char* fmt, const char* arg)
{
    HSD_SisLib_803A70A0(dt.text, idx, (char*) fmt, arg);
}

static HSD_Text* text_struct(f32 z, f32 scale)
{
    HSD_Text* t = HSD_SisLib_803A6754(0, 0);
    t->default_kerning = 1;
    t->default_alignment = 0;
    t->pos_z = z;
    t->font_size.x = scale;
    t->font_size.y = scale;
    return t;
}

/* FN_UserTextUpdate (one B9 per CSS load). The major is 8 here, so no submenu test. */
static void user_text_update(void)
{
    char buf[32];
    int i;
    mu_slippi_fetch_app_state();
    for (i = 0; i < 4; i++) {
        HSD_SisLib_803A70A0(user_text, i, (char*) "");
    }
    if (mu_slippi_app_state() != MU_SLP_APP_LOGGED_IN) {
        return;
    }
    HSD_SisLib_803A70A0(user_text, 0, (char*) "User");
    memcpy(buf, mu_slippi_user_name(), 31);
    buf[31] = 0;
    HSD_SisLib_803A70A0(user_text, 1, (char*) "%s", buf);
    if (user_display_mode == 2) {
        HSD_SisLib_803A70A0(user_text, 2, (char*) "Connect Code");
        memcpy(buf, mu_slippi_user_code(), 10);
        buf[10] = 0;
        HSD_SisLib_803A70A0(user_text, 3, (char*) "%s", buf);
    }
}

/* FN_InitUserDisplay at (-112, 20), z 0, scale 0.1. */
static void user_display_init(int mode)
{
    f32 x = -112.0f, y = 20.0f;
    static const f32 steps[3] = { 20.0f, 25.0f, 20.0f };
    int i;
    user_display_mode = (u8) mode;
    user_text = text_struct(0.0f, 0.1f);
    for (i = 0; i < 4; i++) {
        int is_label = (i & 1) == 0;
        int idx = HSD_SisLib_803A6B98(user_text, x, y, "");
        GXColor c = is_label ? color_gray : color_white;
        f32 size = is_label ? 0.4f : 0.5f;
        HSD_SisLib_803A7548(user_text, idx, size, size);
        HSD_SisLib_803A74F0(user_text, idx, &c);
        if (i < 3) {
            y += steps[i];
        }
    }
    user_text_update();
}

enum {
    ST_HEADER = 0,
    ST_LINE1 = 1,
    ST_SPIN1 = 2,
    ST_LINE3 = 5,
    ST_PRESS_Z = 7,
    ST_PRESS_D = 8,
    ST_PLAYING_LABEL = 9,
    ST_PLAYING_OPP = 10,
    ST_ERR1 = 11,
    ST_ERR4 = 14,
};

static void text_think(HSD_GObj* gobj)
{
    MuSlippiMenuState* st = mu_slippi_state();
    MuMatchState* m = &dt.msrb;
    char opp_code[19] = "--//--//--//--//00";
    char opp_name[32];
    const char* s;
    int idx, i;
    u8 conn = m->connection_state;

    (void) gobj;
    code_sjis18((u8*) opp_code);

    /* Mode title: slpCSS mode matanim on SingleMenu child 36, MELEE (0) or TEAM MATCH (16). */
    if (dt.slpcss != NULL && single_menu != NULL && *single_menu != NULL) {
        HSD_JObj* j = NULL;
        lb_80011E24(*single_menu, &j, 36, -1);
        HSD_JObjRemoveAnim(j);
        HSD_JObjAddAnim(j, NULL, DP(dt.slpcss->mode), NULL);
        HSD_JObjReqAnim(j, st->mode == MU_SLP_MODE_TEAMS ? 16.0f : 0.0f);
        HSD_JObjAnim(j);
    }

    /* Header */
    if (conn > MU_SLP_MM_CONNECTION_SUCCESS) {
        set_text(ST_HEADER, "Error");
    } else {
        switch (st->mode) {
        case MU_SLP_MODE_UNRANKED: set_text_fmt(ST_HEADER, "%s Mode", "Unranked"); break;
        case MU_SLP_MODE_DIRECT: set_text_fmt(ST_HEADER, "%s Mode", "Direct"); break;
        case MU_SLP_MODE_RANKED: set_text_fmt(ST_HEADER, "%s Mode", "Ranked"); break;
        case MU_SLP_MODE_TEAMS: set_text_fmt(ST_HEADER, "%s Mode", "Teams"); break;
        case MU_SLP_MODE_PARTY: set_text_fmt(ST_HEADER, "%s Mode", "Party"); break;
        default: set_text(ST_HEADER, "Error"); break;
        }
    }

    /* Playing label, opponent name, D-pad hint */
    memcpy(opp_name, m->opp_name, 31);
    opp_name[31] = 0;
    set_text(ST_PLAYING_LABEL, conn == MU_SLP_MM_CONNECTION_SUCCESS ? "Playing:" : "");
    set_text(ST_PLAYING_OPP, conn == MU_SLP_MM_CONNECTION_SUCCESS ? opp_name : "");
    set_text(ST_PRESS_D, conn == MU_SLP_MM_CONNECTION_SUCCESS ? "Use D-Pad to Chat" : "");

    /* Press / hold Z */
    if (conn == MU_SLP_MM_CONNECTION_SUCCESS) {
        set_text_fmt(ST_PRESS_Z, "Hold Z to %s", "disconnect");
    } else if (conn == MU_SLP_MM_ERROR) {
        set_text_fmt(ST_PRESS_Z, "Press Z to %s", "clear error");
    } else if (conn > MU_SLP_MM_IDLE) {
        set_text_fmt(ST_PRESS_Z, "Press Z to %s", "cancel");
    } else {
        set_text(ST_PRESS_Z, "");
    }

    /* Clear lines, spinners and error lines */
    for (idx = ST_LINE1; idx <= ST_LINE3; idx += 2) {
        set_text(idx, "");
    }
    dt.spinner[0] = dt.spinner[1] = dt.spinner[2] = 0;
    for (idx = ST_ERR1; idx <= ST_ERR4; idx++) {
        set_text(idx, "");
    }

    if (conn == MU_SLP_MM_ERROR) {
        /* Wrap the error at 30 letters on the last space (Shift-JIS pairs count as one). The line
         * index state carries over between lines exactly as the asm's registers do. */
        u8 buf[241 + 4];
        int sub = ST_ERR1, line_idx = 0, line_len = 0, last_space = 0, start = 0;
        u8 c;
        memset(buf, 0, sizeof buf);
        memcpy(buf, m->error, 241);
        for (;;) {
            int set = 0;
            int pos = start + line_idx;
            c = pos < (int) sizeof buf ? buf[pos] : 0;
            if (c & 0x80) {
                line_idx += 2;
                line_len++;
            } else {
                if (c == 0x20) {
                    last_space = line_idx;
                }
                line_idx++;
                line_len++;
            }
            if (c == 0) {
                set = 1;
            } else if (line_len > 30) {
                if (start + last_space < (int) sizeof buf) {
                    buf[start + last_space] = 0;
                }
                set = 1;
            }
            if (set) {
                set_text(sub, start < (int) sizeof buf ? (const char*) &buf[start] : "");
                sub++;
                start += last_space + 1;
                line_idx = 0;
                line_len = 0;
            }
            if (c == 0 || start + line_idx >= 241 || sub > ST_ERR4) {
                break;
            }
        }
    } else {
        idx = ST_LINE1;
        if (*char_chosen == 0) {
            set_text(idx, "Select your character");
            dt.spinner[0] = 1;
            goto spinners;
        }
        set_text(idx, "Character selected");
        idx += 2;
        dt.spinner[0] = 2;

        if (m->local_ready == 0) {
            const char* action;
            if ((st->mode == MU_SLP_MODE_DIRECT || st->mode == MU_SLP_MODE_TEAMS) &&
                conn == MU_SLP_MM_CONNECTION_SUCCESS && st->is_winner == MU_SLP_WINNER_LOST &&
                st->chose_stage == 0)
            {
                action = "select stage";
            } else if (conn == MU_SLP_MM_CONNECTION_SUCCESS) {
                action = "lock in";
            } else if (st->mode == MU_SLP_MODE_DIRECT || st->mode == MU_SLP_MODE_TEAMS) {
                action = "enter code";
            } else {
                action = "search";
            }
            set_text_fmt(idx, "Press START to %s", action);
            dt.spinner[1] = 1;
            goto spinners;
        }
        set_text(idx, "Locked in");
        idx += 2;
        dt.spinner[1] = 2;

        {
            int generic = st->mode == MU_SLP_MODE_UNRANKED || st->mode == MU_SLP_MODE_RANKED ||
                          st->mode == MU_SLP_MODE_PARTY;
            if (conn == MU_SLP_MM_CONNECTION_SUCCESS) {
                set_text_fmt(idx, "Waiting on %s", "opponent");
            } else if (conn == MU_SLP_MM_OPPONENT_CONNECTING) {
                set_text_fmt(idx, "Connecting to %s", generic ? "opponent" : opp_code);
            } else {
                set_text_fmt(idx, "Searching for %s", generic ? "opponent" : opp_code);
            }
        }
        dt.spinner[2] = 1;
    }

spinners:
    for (i = 0, idx = ST_SPIN1; i < 3; i++, idx += 2) {
        if (dt.spinner[i] == 1) {
            GXColor c = spinner_wait_color;
            s = spinner_str[(dt.frame_counter / SPINNER_TRANSITION_FRAMES) & 1];
            set_text(idx, s);
            HSD_SisLib_803A74F0(dt.text, idx, &c);
        } else if (dt.spinner[i] == 2) {
            GXColor c = spinner_done_color;
            set_text(idx, spinner_done_str);
            HSD_SisLib_803A74F0(dt.text, idx, &c);
        } else {
            set_text(idx, "");
        }
    }

    dt.frame_counter++;
    if (dt.frame_counter >= FRAME_MAX) {
        dt.frame_counter = 0;
    }
}

/* LoadCSSText's init (mnCharSel_802640A0, after the SingleMenu joint loads). The portrait BG word
 * (mnCharSel_804D50D8) is reset by the caller. */
void mu_slippi_css_text_init(void)
{
    HSD_GObj* gobj;
    void* ud;
    HSD_Text* t;
    int i;

    user_display_init(mu_slippi_state()->mode == MU_SLP_MODE_DIRECT ? 2 : 1);

    gobj = GObj_Create(4, 5, 0x80);
    ud = HSD_MemAlloc(4);
    GObj_InitUserData(gobj, 4, HSD_Free, ud);
    HSD_GObj_SetupProc(gobj, text_think, 4);

    t = text_struct(0.0f, 0.1f);
    dt.text = t;

    create_subtext(t, &color_white, 0.5f, 70.0f, 23.0f);                  /* header */
    {
        static const f32 line_y[3] = { 52.0f, 75.0f, 98.0f };
        for (i = 0; i < 3; i++) {
            create_subtext(t, &color_white, 0.4f, 90.0f, line_y[i]);      /* line */
            create_subtext(t, &color_white, 0.45f, 70.0f, line_y[i]);     /* spinner */
        }
    }
    create_subtext(t, &color_gray, 0.4f, 70.0f, 132.5f);                  /* press Z */
    create_subtext(t, &color_gray, 0.4f, 70.0f, 152.5f);                  /* use D-pad */
    create_subtext(t, &color_gray, 0.5f, -130.0f, -246.0f);               /* Playing: */
    create_subtext(t, &color_white, 0.5f, -50.0f, -246.0f);               /* opponent */
    {
        static const f32 err_y[4] = { 52.0f, 70.0f, 88.0f, 106.0f };
        for (i = 0; i < 4; i++) {
            create_subtext(t, &color_red, 0.4f, 70.0f, err_y[i]);
        }
    }
}

/* ---- chat (mu_slippi_chat.c): the CSS data table as SlippiCSS.dat reads it ---- */

const MuMatchState* mu_slippi_css_msrb(void)
{
    return &dt.msrb;
}

void* mu_slippi_css_slpcss(void)
{
    return dt.slpcss;
}

void mu_slippi_css_set_chat_open(int open)
{
    dt.chat_window_opened = (u8) (open != 0);
}

/* mnCharSel_804D6CF6: nonzero while the CSS leaves or hands over to name entry. */
int mu_slippi_css_scene_request(void)
{
    return scene_request != NULL ? *scene_request : 0;
}

/* mnCharSel_804D6CF0 read as a byte, as the module does. */
int mu_slippi_css_port(void)
{
    return ctrl_port != NULL ? (u8) *ctrl_port : 0;
}
