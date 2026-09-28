/* Native Slippi online menus: the core.
 *
 * Legacy gets the online menus from Slippi's codes in GALE01r2.ini (the Online/Menus, Online/Slippi
 * Online Scene and Online/Static folders). This file is their native form for the parts that are not
 * screen code: the state Slippi parks in r13 slots and code caves, the host-command wrappers and
 * codecs, the boot and main-menu entry, the online submenu, and the online major (GM_HANYU_CSS
 * turned into CSS / SSS / VS / Results / Splash). Each function names the code it stands for.
 * The public contract is run-source/s6-core-api.md.
 *
 * Nothing here runs unless the host set the Slippi-menus game option; the predicates answer 0
 * and the hook sites keep their original code.
 */
#include <string.h>
#include <dolphin/pad.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gm_1B03.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmresult.h>
#include <melee/gm/gmscdata.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/gmvsmode.h>
#include <melee/gm/types.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>
#include <melee/mn/inlines.h>
#include <melee/mn/mnmain.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/random.h>
#include <mu_native.h>

int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);
void mu_online_abi_log(const char* text);
void mu_replay_apply_game_info(StartMeleeData* data, const unsigned char* info);
extern MenuKindData mn_803EB6B0[];

enum {
    CMD_GET_MATCH_STATE = 0xB3,
    CMD_FIND_OPPONENT = 0xB4,
    CMD_SET_MATCH_SELECTIONS = 0xB5,
    CMD_OPEN_LOGIN = 0xB6,
    CMD_UPDATE_APP = 0xB8,
    CMD_GET_ONLINE_STATUS = 0xB9,
    CMD_CLEANUP_CONNECTIONS = 0xBA,
    CMD_SEND_CHAT = 0xBB,
    CMD_GET_NEW_SEED = 0xBC,
    CMD_NAME_ENTRY_INDEX = 0xBE,
    REPLY_CAPACITY = 4096,   /* the bridge refuses a smaller reply buffer */
    MSRB_SIZE = 962,
    OSB_SIZE = 42,
    FIGHTER_NUM = 33,        /* GetFighterNum: "no fighter" */
    ICON_ZELDA = 0x12,
    ICON_SHEIK = 0x13,
};

/* ---- flag ---- */

static int menus_flag = -1;

int mu_slippi_menus_enabled(void)
{
    if (menus_flag < 0) {
        menus_flag = (mu_game_options() & MU_OPTION_SLIPPI_MENUS) != 0;
    }
    return menus_flag;
}

/* ---- state (Slippi's r13 slots and cave bytes) ---- */

static MuSlippiMenuState slp;
static MuMatchState last_match;
static unsigned char user_name[32];
static unsigned char user_code[11];
static unsigned char reply[REPLY_CAPACITY];
static int installed;         /* the online major replaced GM_HANYU_CSS (main.asm) */
static int zelda_sheik;       /* between the online major's OnLoad and OnUnload */
static unsigned online_loads;  /* OnLoad count: the chat shim's Sheik selector keeps its icon until the next one */
static int boot_oneshot;      /* boot.asm's one-shot "first menu = online submenu" */
static int first_boot = 1;    /* OnMenuLoad's DOFST_IS_FIRST_BOOT */

MuSlippiMenuState* mu_slippi_state(void)
{
    return &slp;
}

/* ---- host commands (FN_EXITransferBuffer) ---- */

int mu_slippi_cmd(unsigned int cmd, const void* payload, unsigned int size, void* out,
                  unsigned int out_cap, unsigned int* out_size)
{
    unsigned int got = 0;
    int result = mu_online_abi_command(cmd, (const unsigned char*) payload, size, reply,
                                       REPLY_CAPACITY, &got);
    if (result != 0) {
        got = 0;
    }
    if (out != NULL) {
        unsigned int n = got < out_cap ? got : out_cap;
        memcpy(out, reply, n);
    }
    if (out_size != NULL) {
        *out_size = got;
    }
    return result;
}

static u32 be32(const u8* p) { return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3]; }
static void put32(u8* p, u32 v) { p[0] = (u8) (v >> 24); p[1] = (u8) (v >> 16); p[2] = (u8) (v >> 8); p[3] = (u8) v; }

/* UserDisplayFunctions FetchSlippiAppState: B9, 42 bytes {app state, name[31], code[10]}; the
 * app state goes to r13-0x505F. */
int mu_slippi_fetch_app_state(void)
{
    u8 osb[OSB_SIZE];
    memset(osb, 0, sizeof osb);
    mu_slippi_cmd(CMD_GET_ONLINE_STATUS, NULL, 0, osb, sizeof osb, NULL);
    slp.app_state = osb[0];
    memcpy(user_name, osb + 1, 31);
    user_name[31] = 0;
    memcpy(user_code, osb + 32, 10);
    user_code[10] = 0;
    return slp.app_state;
}

int mu_slippi_app_state(void) { return slp.app_state; }
const char* mu_slippi_user_name(void) { return (const char*) user_name; }
const char* mu_slippi_user_code(void) { return (const char*) user_code; }

/* LoadMatchState: B3 into a Match State Response Buffer. */
static void parse_match_state(MuMatchState* m, const u8* r)
{
    int i;
    memcpy(m->raw, r, MSRB_SIZE);
    m->connection_state = r[0];
    m->local_ready = r[1];
    m->remote_ready = r[2];
    m->local_index = r[3];
    m->remote_index = r[4];
    m->rng_offset = be32(r + 5);
    m->delay_frames = r[9];
    m->user_chat_msg = r[10];
    m->opp_chat_msg = r[11];
    m->chat_player_index = r[12];
    m->user_rank = r[13];
    m->opp_rank = r[14];
    memcpy(m->local_name, r + 15, 31);
    for (i = 0; i < 4; i++) {
        memcpy(m->names[i], r + 46 + 31 * i, 31);
        memcpy(m->codes[i], r + 201 + 10 * i, 10);
        memcpy(m->uids[i], r + 241 + 29 * i, 29);
    }
    memcpy(m->opp_name, r + 170, 31);
    memcpy(m->error, r + 357, 241);
    memcpy(m->game_info, r + 598, 312);
    memcpy(m->match_id, r + 910, 51);
    m->alt_stage_mode = r[961];
}

int mu_slippi_load_match_state(MuMatchState* out)
{
    u8 msrb[MSRB_SIZE];
    unsigned int got = 0;
    int result;
    memset(msrb, 0, sizeof msrb);
    result = mu_slippi_cmd(CMD_GET_MATCH_STATE, NULL, 0, msrb, sizeof msrb, &got);
    parse_match_state(&last_match, msrb);
    if (out != NULL && out != &last_match) {
        *out = last_match;
    }
    return result != 0 || got < MSRB_SIZE ? -1 : 0;
}

const MuMatchState* mu_slippi_match_state(void)
{
    return &last_match;
}

/* Find Match Transfer Buffer: mode, 18 Shift-JIS bytes. */
void mu_slippi_find_opponent(int mode, const unsigned char* code_sjis18)
{
    u8 b[19];
    memset(b, 0, sizeof b);
    b[0] = (u8) mode;
    if (code_sjis18 != NULL) {
        memcpy(b + 1, code_sjis18, 18);
    }
    mu_slippi_cmd(CMD_FIND_OPPONENT, b, sizeof b, NULL, 0, NULL);
}

/* Player Selections Transfer Buffer. */
void mu_slippi_set_selections(const MuSlippiSelections* s)
{
    u8 b[9];
    b[0] = s->team_id;
    b[1] = s->char_id;
    b[2] = s->char_color;
    b[3] = s->char_opt;
    b[4] = (u8) (s->stage_id >> 8);
    b[5] = (u8) s->stage_id;
    b[6] = s->stage_opt;
    b[7] = s->online_mode;
    b[8] = s->alt_stage_mode;
    mu_slippi_cmd(CMD_SET_MATCH_SELECTIONS, b, sizeof b, NULL, 0, NULL);
}

void mu_slippi_cleanup_connections(void)
{
    mu_slippi_cmd(CMD_CLEANUP_CONNECTIONS, NULL, 0, NULL, 0, NULL);
}

unsigned int mu_slippi_new_seed(void)
{
    u8 b[4] = { 0, 0, 0, 0 };
    mu_slippi_cmd(CMD_GET_NEW_SEED, NULL, 0, b, sizeof b, NULL);
    return be32(b);
}

/* The bridge expects two payload bytes for BB. */
void mu_slippi_send_chat(int message_id)
{
    u8 b[2];
    b[0] = (u8) message_id;
    b[1] = 0;
    mu_slippi_cmd(CMD_SEND_CHAT, b, sizeof b, NULL, 0, NULL);
}

/* AutoComplete FetchSuggestion: 8x3 input, length, index u32, scroll, mode; the reply is found,
 * 8x3 suggestion, length, index u32. */
int mu_slippi_code_suggestion(const MuCodeSuggestionReq* in, MuCodeSuggestion* out)
{
    u8 b[31], r[30];
    int result;
    memcpy(b, in->input, 24);
    b[24] = in->length;
    put32(b + 25, in->index);
    b[29] = in->scroll;
    b[30] = in->mode;
    memset(r, 0, sizeof r);
    result = mu_slippi_cmd(CMD_NAME_ENTRY_INDEX, b, sizeof b, r, sizeof r, NULL);
    if (out != NULL) {
        out->found = r[0];
        memcpy(out->text, r + 1, 24);
        out->length = r[25];
        out->index = be32(r + 26);
    }
    return result;
}

/* ---- scene predicates ---- */

int mu_slippi_in_online_mode(void)
{
    return mu_slippi_menus_enabled() && installed && gm_GetCurrentGameMode() == GM_HANYU_CSS;
}

int mu_slippi_online_state(void)
{
    return mu_slippi_in_online_mode() ? gm_GetCurrentSceneIndex() : -1;
}

int mu_slippi_on_online_css(void) { return mu_slippi_online_state() == MU_SLP_STATE_CSS; }
int mu_slippi_on_online_sss(void) { return mu_slippi_online_state() == MU_SLP_STATE_SSS; }
int mu_slippi_in_online_game(void) { return mu_slippi_online_state() == MU_SLP_STATE_VS; }
int mu_slippi_on_online_results(void) { return mu_slippi_online_state() == MU_SLP_STATE_RESULTS; }
int mu_slippi_on_online_splash(void) { return mu_slippi_online_state() == MU_SLP_STATE_SPLASH; }
int mu_slippi_zelda_is_sheik(void) { return mu_slippi_menus_enabled() && zelda_sheik; }
unsigned mu_slippi_online_load_count(void) { return online_loads; }

/* IncreaseTextHeap: the online CSS gets a 0x4800-byte text heap instead of 0x2400. */
int mu_slippi_css_text_heap(void)
{
    return mu_slippi_on_online_css() ? 0x4800 : 0;
}

/* ---- helpers (Online/Static) ---- */

/* GetTeamCostumeIndex. */
unsigned char mu_slippi_team_costume(int team, int ckind)
{
    switch (team) {
    case 1:
        return gm_80169264((u8) ckind);
    case 3:
        return gm_80169290((u8) ckind);
    default:
        return gm_801692BC((u8) ckind);
    }
}

/* HandleOnlineLockedOptions (Melee Unlocked edit: Ranked hidden when logged in). Logged out (or an
 * unknown state): only Log in. Logged in: Unranked, Direct, Teams, Party. Update required: only
 * Update. */
int mu_slippi_option_unlocked(int sel)
{
    switch (slp.app_state) {
    case MU_SLP_APP_LOGGED_IN:
        return sel >= MU_SLP_MODE_UNRANKED && sel <= MU_SLP_MODE_PARTY;
    case MU_SLP_APP_UPDATE:
        return sel != MU_SLP_MODE_RANKED && sel != MU_SLP_MODE_UNRANKED &&
               sel != MU_SLP_MODE_DIRECT && sel != MU_SLP_MODE_TEAMS &&
               sel != MU_SLP_MODE_PARTY && sel != MU_SLP_OPT_LOGIN && sel != MU_SLP_OPT_LOGOUT;
    default:
        return sel != MU_SLP_MODE_RANKED && sel != MU_SLP_MODE_UNRANKED &&
               sel != MU_SLP_MODE_DIRECT && sel != MU_SLP_MODE_TEAMS &&
               sel != MU_SLP_MODE_PARTY && sel != MU_SLP_OPT_LOGOUT && sel != MU_SLP_OPT_UPDATE;
    }
}

/* GetFirstUnlocked: scan from Unranked (never Ranked by default). */
int mu_slippi_first_unlocked(void)
{
    int i;
    int count = mn_803EB6B0[MENU_KIND_8].selection_count;
    for (i = MU_SLP_MODE_UNRANKED; i < count; i++) {
        if (mn_80229938(MENU_KIND_8, i)) {
            break;
        }
    }
    return i;
}

/* ---- boot (boot.asm) and the main menu (Online/Menus/TitleMenu) ---- */

/* boot.asm: boot leaves straight to the main menu (no title, no opening movie), and the first
 * menu entry opens the online submenu. The Stadium transformation preloads are for the EXI
 * loader and have no native counterpart (disc reads are synchronous). */
unsigned char mu_slippi_boot_mode(unsigned char mode)
{
    if (!mu_slippi_menus_enabled()) {
        return mode;
    }
    boot_oneshot = 1;
    return GM_MENU;
}

/* OnMenuPrep, every main-menu entry: the online submenu table, the 1P description of row 2, and
 * the app state (InitBuffers + FetchSlippiAppState). */
void mu_slippi_menu_prep(void)
{
    if (!mu_slippi_menus_enabled()) {
        return;
    }
    mu_mnmain_install_online_menu();
    mu_slippi_fetch_app_state();
}

/* The jump-table entries OnMenuPrep and boot.asm replace: previous mode 8 (OnReturnFromOnline)
 * and, once after boot, previous mode GM_BOOT (OnFirstMenuLoad). Returns 1 when it set the menu. */
int mu_slippi_menu_enter(int previous_mode, unsigned char* menu_kind, unsigned char* hovered)
{
    if (!mu_slippi_menus_enabled()) {
        return 0;
    }
    if (previous_mode == GM_HANYU_CSS) {
        mu_content_mode(-1);   /* back from online: the offline view again */
        *menu_kind = MENU_KIND_8;
        *hovered = mn_80229938(MENU_KIND_8, slp.mode) ? slp.mode : (u8) mu_slippi_first_unlocked();
        return 1;
    }
    if (previous_mode == GM_BOOT && boot_oneshot) {
        boot_oneshot = 0;
        mu_slippi_fetch_app_state();
        *menu_kind = MENU_KIND_8;
        *hovered = (u8) mu_slippi_first_unlocked();
        return 1;
    }
    return 0;
}

/* OnMenuLoad, end of mnMain_Scene_OnEnter: the "Melee" voice on the first boot, then BA on every
 * entry. The title-screen user text is intentionally off in this build. */
void mu_slippi_main_menu_loaded(void)
{
    if (!mu_slippi_menus_enabled()) {
        return;
    }
    if (first_boot) {
        lbAudioAx_80026F2C(first_boot);
        lbAudioAx_8002702C(2, 8);
        lbAudioAx_80027168();
        lbAudioAx_80027648();
        lbAudioAx_800237A8(30005, 127, 64);
        first_boot = 0;
    }
    mu_slippi_cleanup_connections();
}

/* OnlineModeOptionSelected -> SwitchToOnlineMenu: open the online submenu from the 1P menu and
 * force the old menu to clear (AllowSwapToSameSubmenu). Runs from the 1P menu's think. */
void mu_slippi_switch_to_online_menu(void)
{
    mn_80229894(MENU_KIND_8, (u16) mu_slippi_first_unlocked(), 1);
    slp.force_clear = 1;
}

/* AllowSwapToSameSubmenu: the menu clears when the kinds differ or the force flag is set; either
 * way the flag is consumed. */
int mu_slippi_menu_clear_check(int menus_differ)
{
    if (!mu_slippi_menus_enabled()) {
        return menus_differ;
    }
    if (menus_differ || slp.force_clear) {
        slp.force_clear = 0;
        return 1;
    }
    return 0;
}

static u8 confirm_port(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (gm_GetButtonsTriggered((u8) i) & PAD_CONFIRM) {
            return (u8) i;
        }
    }
    return 0;
}

/* OnMenuPrep FN_OnlineSubmenuThink (ported from the regular-match think). The logout dialog is
 * unreachable: Log out is locked in every app state of this build. */
void mu_slippi_online_menu_think(HSD_GObj* gp)
{
    u32 buttons;
    MenuExitData* data;
    (void) gp;

    buttons = mn_80229624(4);
    mn_804A04F0.buttons = buttons;
    if (buttons & MenuInput_Confirm) {
        mn_804D6BC8.cooldown = 5;
        mn_804A04F0.entering_menu = 1;
        gm_801677E8((s8) confirm_port());
        switch (mn_804A04F0.hovered_selection) {
        case MU_SLP_MODE_RANKED:
        case MU_SLP_MODE_UNRANKED:
        case MU_SLP_MODE_DIRECT:
        case MU_SLP_MODE_TEAMS:
        case MU_SLP_MODE_PARTY:
            slp.mode = (u8) mn_804A04F0.hovered_selection;
            /* The files of the next scene follow the mode: retail online, the mod's in Direct when
             * the player uses it there (mu_content.c). Asked before the online major loads. */
            mu_content_mode(slp.mode);
            sfxForward();
            data = gm_GetCurrentSceneExitData();
            data->pending_mode = GM_HANYU_CSS;
            gm_801A4B60();
            break;
        case MU_SLP_OPT_LOGIN:
            sfxForward();
            mu_slippi_cmd(CMD_OPEN_LOGIN, NULL, 0, NULL, 0, NULL);
            break;
        case MU_SLP_OPT_UPDATE:
            sfxForward();
            mu_slippi_cmd(CMD_UPDATE_APP, NULL, 0, NULL, 0, NULL);
            break;
        }
    } else if (buttons & MenuInput_Back) {
        sfxBack();
        mn_804A04F0.entering_menu = 0;
        mn_80229894(MENU_KIND_1P, SEL_1P_2, 3);
    } else if (buttons & MenuInput_Up) {
        sfxMove();
        do {
            if (mn_804A04F0.hovered_selection == 0) {
                mn_804A04F0.hovered_selection = MU_SLP_OPT_COUNT - 1;
            } else {
                mn_804A04F0.hovered_selection--;
            }
        } while (!mn_80229938(MENU_KIND_8, mn_804A04F0.hovered_selection));
    } else if (buttons & MenuInput_Down) {
        sfxMove();
        do {
            if (mn_804A04F0.hovered_selection == MU_SLP_OPT_COUNT - 1) {
                mn_804A04F0.hovered_selection = 0;
            } else {
                mn_804A04F0.hovered_selection++;
            }
        } while (!mn_80229938(MENU_KIND_8, mn_804A04F0.hovered_selection));
    }
}

/* ---- the online major (Slippi Online Scene/main.asm) ----
 * GM_HANYU_CSS (unused in retail) becomes the online mode: states 0 CSS, 1 SSS, 2 VS, 3 Results,
 * 4 Splash. Slippi writes the raw next-state byte (id + 1); gm_SetNextGameModeStateId(id) is the
 * same write. The Ranked-only GameSetup state is not ported (Ranked is hidden). */

static CSSData online_css_data;   /* Slippi reuses the event CSS buffer; the choice is saved in vs.unk_530 */

/* SplashSceneInit: the negotiated match into the VS mode data (AdjustNullCharID is a no-op). */
void mu_slippi_splash_init(void)
{
    mu_slippi_load_match_state(NULL);
    mu_replay_apply_game_info(&gmVsMelee_GetVsData()->start, last_match.game_info);
}

/* MajorSceneLoad. */
static void online_on_load(void)
{
    gmMainLib_804D3EE0->vs.unk_530.x6 = gm_801677F0();   /* the 1P port for the event CSS slot */
    zelda_sheik = 1;                                       /* the Zelda icon picks Sheik */
    online_loads++;
}

/* MajorSceneUnload. */
static void online_on_unload(void)
{
    zelda_sheik = 0;
}

/* CSSScenePrep: event-style CSS with the saved choice, then clear the preload cache. */
static void css_prep(GameModeState* state)
{
    struct EventData* ev = &gmMainLib_804D3EE0->vs.unk_530;
    CSSData* css = gm_GetGameModeStateEnterData(state);
    gm_801B06B0(css, 0xE, ev->x2, 0, ev->x3, ev->nametag, 0, ev->x6);
    lbDvd_SetupVsPreloadCache();
}

/* CSSSceneDecide (runs the event CSS decide first to save the choice). */
static void css_decide(GameModeState* state)
{
    struct EventData* ev = &gmMainLib_804D3EE0->vs.unk_530;
    CSSData* css = gm_GetGameModeStateExitData(state);

    /* gmevent.c onExitCss */
    if (css->pending_scene_change == 2) {
        gm_ChangeGameModeAfterCurrentScene(GM_MENU);
        return;
    }
    gm_801B0730(css, &ev->x2, NULL, &ev->x3, &ev->nametag, NULL);
    ev->x8 = -1;
    ev->x9 = -1;
    ev->xA = -1;

    switch (slp.mode) {
    case MU_SLP_MODE_DIRECT:
    case MU_SLP_MODE_TEAMS:
        /* The first match and the last winner go to the splash; the loser picks the stage unless
         * it already did. Any other value stalls on the console. */
        if (slp.is_winner == MU_SLP_WINNER_LOST && slp.chose_stage == 0) {
            gm_SetNextGameModeStateId(MU_SLP_STATE_SSS);
            return;
        }
        break;
    default:
        /* Unranked, Party (and Ranked, which this build never reaches) */
        break;
    }
    mu_slippi_splash_init();
    gm_SetNextGameModeStateId(MU_SLP_STATE_SPLASH);
}

/* SSSScenePrep: the vanilla VS-mode SSS prep. */
static void sss_prep(GameModeState* state)
{
    gmVsMelee_EnterSss(state, gmVsMelee_GetVsData());
}

/* SSSSceneDecide. */
static void sss_decide(GameModeState* state)
{
    SSSData* sss = gm_GetGameModeStateExitData(state);
    if (!sss->start_game) {
        gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);
        return;
    }
    slp.chose_stage = 1;
    mu_slippi_load_match_state(NULL);
    if (last_match.local_ready == last_match.remote_ready) {
        mu_slippi_splash_init();
        gm_SetNextGameModeStateId(MU_SLP_STATE_SPLASH);
    } else {
        gm_SetNextGameModeStateId(MU_SLP_STATE_CSS);
    }
}

/* VS ScenePrep (vanilla 801b1588), then InitOnlinePlay arms the scene as an online match. */
static void vs_prep(GameModeState* state)
{
    gmVsMelee_EnterVs(state, gmVsMelee_GetVsData(), NULL, NULL);
    mu_online_arm(slp.mode, gm_801677F0());
}

/* CheckIfWonLastGame. */
static int won_last_game(int slot)
{
    MatchEnd* me = &gmVsMelee_VsExitInfo.match_end;
    struct MatchPlayerData* p = &me->player_standings[slot];

    if (me->outcome == 0) {
        return 0;
    }
    if (gm_GetStartMeleeRules()->is_teams != me->is_teams) {
        return 0;
    }
    if (p->pkind == Gm_PKind_NA) {
        return 0;
    }
    if (me->outcome == 7) {   /* LRA-Start: byte 0 is the player who pressed it */
        int lras = (int) (me->x0 >> 24);
        if (me->is_teams == 1) {
            if (lras >= GM_MAX_PLAYERS) {
                return 1;
            }
            return me->player_standings[lras].team != p->team;
        }
        return lras != slot;
    }
    if (me->is_teams == 1) {
        return fn_801654A0(me) == p->team;
    }
    return p->is_big_loser == 0;
}

/* VSSceneDecide (runs the vanilla VS decide first). */
static void vs_decide(GameModeState* state)
{
    MatchExitInfo* mei;
    MatchEnd* me = &gmVsMelee_VsExitInfo.match_end;
    int next = MU_SLP_STATE_CSS;
    int local;
    ssize_t i;

    /* gmvsmode.c onExitVs */
    gmVsMelee_ExitVs(state, gmVsMode_State_Results, gmVsMode_State_SuddenDeath);
    mei = gm_GetGameModeStateExitData(state);
    for (i = 0; i < GM_MAX_PLAYERS; i++) {
        if (mei->match_end.player_standings[i].pkind != Gm_PKind_NA) {
            gm_80162A98(mei->match_end.player_standings[i].x20);
            gm_RecordSelfDestructs(mei->match_end.player_standings[i].self_destructs);
            gm_80162A4C(mei->match_end.player_standings[i].x44);
        }
    }

    mu_slippi_load_match_state(NULL);
    local = last_match.local_index & 3;
    slp.game_local_index = (u8) local;   /* StaticData: read by the Party results screen */

    if (slp.mode == MU_SLP_MODE_PARTY) {
        next = MU_SLP_STATE_RESULTS;
        mu_slippi_cleanup_connections();   /* always disconnect after Party */
    }
    gm_SetNextGameModeStateId((u8) next);

    slp.is_winner = (s8) won_last_game(local);
    if (slp.mode == MU_SLP_MODE_TEAMS) {
        /* Port 0 always picks the stage. */
        slp.is_winner = local != 0 ? 1 : 0;
    } else {
        int winners = 0;
        for (i = 0; i < 4; i++) {
            winners += won_last_game((int) i) != 0;
        }
        if (winners != 1) {
            slp.is_winner = MU_SLP_WINNER_LOST;   /* a draw: both pick */
        }
    }

    /* The gold winner text: fake who LRA-started (byte 0) and player 1's loser flag. Not for
     * Party (its results screen breaks); forced off in Teams and Ranked. */
    if (slp.mode != MU_SLP_MODE_PARTY) {
        u32 who = won_last_game(local) ? 1 : 0;
        me->x0 = (me->x0 & 0x00FFFFFFu) | (who << 24);
        me->player_standings[0].is_big_loser = who ? 0 : 1;
        if (slp.mode == MU_SLP_MODE_TEAMS || slp.mode == MU_SLP_MODE_RANKED) {
            me->outcome = 0;
        }
    }

    slp.chose_stage = 0;
    /* A new seed, so both games do not random the same character after a match. */
    *HSD_RandSeedPtr = mu_slippi_new_seed();
}

/* Results: the vanilla VS-mode results prep and decide (back to CSS, state 0). */
static void results_prep(GameModeState* state)
{
    gmVsMelee_EnterResults(state);
}

static void results_decide(GameModeState* state)
{
    gmVsMelee_ExitResults(state, gmVsMelee_GetVsData(), MU_SLP_STATE_CSS);
    if (!gm_WasMatchCanceled(gmVsMelee_ResultsEnterData.match_end.outcome)) {
        gm_801623A4(&gmVsMelee_ResultsEnterData.match_end);
    }
}

/* The Classic splash's enter data, as bytes (gmclassic.c gmClassicIntroData: two s32, then u8s
 * at the console offsets). */
typedef struct MuSplashData {
    s32 x00;   /* pvp type (Slippi writes the low byte of the big-endian word) */
    s32 x04;   /* event mode */
    u8 b[0x18];
} MuSplashData;
/* Classic's intro buffer is the first part of gmClassic_rt natively (gm/gmclassic.c). */
extern unsigned char gmClassic_rt[];
#define gmClassicIntroDataBuffer (*(MuSplashData*) gmClassic_rt)

/* SplashScenePrep. */
static void splash_prep(GameModeState* state)
{
    static const u8 layout[16] = { 0x01, 0x78, 0x01, 0x01, 0x01, 0xFF, 0x21, 0x21,
                                   0xFF, 0x21, 0x21, 0xEE, 0x00, 0x00, 0xEE, 0x00 };
    StartMeleeData* start = &gmVsMelee_GetVsData()->start;
    MuSplashData* sp = &gmClassicIntroDataBuffer;
    u8* d = (u8*) sp;    /* console offsets from the start of the struct */
    PreloadedGameModeState* cache;
    const u8* gi;
    int i, is_teams, local, local_team, left = 0, right = 0;
    u64 mask = 0;
    (void) state;

    mu_slippi_load_match_state(NULL);
    gi = last_match.game_info;

    if (slp.mode == MU_SLP_MODE_TEAMS) {
        for (i = 0; i < 4; i++) {
            start->players[i].color =
                mu_slippi_team_costume(start->players[i].team + 1, start->players[i].ckind);
        }
    }

    memcpy(d + 8, layout, sizeof layout);
    sp->x04 = 0;
    sp->x00 = 0;
    is_teams = gi[8];
    d[0x0A] = 2;
    d[0x0E] = 1;
    d[0x0F] = 1;
    d[0x11] = 1;
    d[0x12] = 1;
    d[0x14] = 1;
    d[0x15] = 1;
    d[0x17] = 1;
    sp->x00 = is_teams;   /* the announcer says "Team ..." */

    local = last_match.local_index;
    local_team = gi[0x60 + 0x24 * (local & 3) + 9];
    for (i = 0; i < 4; i++) {
        PlayerInitData* p = &start->players[i];
        int go_left;
        if (p->slot_type >= 3) {
            continue;
        }
        go_left = i == local || (is_teams && p->team == local_team);
        if (go_left) {
            d[0x0D + left] = (u8) p->ckind;
            d[0x13 + left] = p->color;
            left++;
        } else {
            d[0x10 + right] = (u8) p->ckind;
            d[0x16 + right] = p->color;
            right++;
        }
    }
    d[0x0B] = (u8) left;
    d[0x0C] = (u8) right;

    /* Preload the fighters (p3/p4 only when the negotiated slot is used) and the stage. */
    cache = lbDvd_GetPreloadCacheScene();
    for (i = 0; i < 4; i++) {
        if (i >= 2 && gi[0x60 + 0x24 * i + 1] >= 3) {
            continue;
        }
        cache->game_cache.entries[i].char_id = start->players[i].ckind;
        cache->game_cache.entries[i].color = start->players[i].color;
    }
    cache->game_cache.stkind = start->rules.stkind;
    lbDvd_80018254();
    lbDvd_80018C2C(199);
    lbDvd_80017700(4);

    /* The fighters' and the stage's sound banks. */
    lbAudioAx_80026F2C(0x1C);
    for (i = 0; i < 6; i++) {
        if (start->players[i].ckind != FIGHTER_NUM) {
            mask |= lbAudioAx_80026E84((CharacterKind) start->players[i].ckind);
        }
    }
    mask |= lbAudioAx_80026EBC((StKind) start->rules.stkind);
    lbAudioAx_8002702C(4, mask);
    lbAudioAx_80027168();
}

/* SplashSceneDecide: on to VS. */
static void splash_decide(GameModeState* state)
{
    (void) state;
    gm_SetNextGameModeStateId(MU_SLP_STATE_VS);
}

extern UNK_T gmClassic_804D68D0;

static GameModeState online_states[] = {
    { MU_SLP_STATE_CSS, lbDvdPreload_3, 0, css_prep, css_decide,
      { GS_CSS, &online_css_data, &online_css_data } },
    { MU_SLP_STATE_SSS, lbDvdPreload_3, 0, sss_prep, sss_decide,
      { GS_SSS, &gmVsMelee_SssData, &gmVsMelee_SssData } },
    { MU_SLP_STATE_VS, lbDvdPreload_3, 0, vs_prep, vs_decide,
      { GS_VS, &gmVsMelee_StartData, &gmVsMelee_VsExitInfo } },
    { MU_SLP_STATE_RESULTS, lbDvdPreload_3, 0, results_prep, results_decide,
      { GS_RESULTS, &gmVsMelee_ResultsEnterData, NULL } },
    { MU_SLP_STATE_SPLASH, lbDvdPreload_3, 0, splash_prep, splash_decide,
      { GS_INTRO_EASY, &gmClassicIntroDataBuffer, &gmClassic_804D68D0 } },
    { GM_GAMEMODESTATE_TERMINATE },
};

/* main.asm, once at boot: the online major, and the online bytes cleared. The '#' key of the name
 * entry belongs to the name-entry owner (see s6-core-api.md). */
void mu_slippi_scene_install(void)
{
    GameMode* mode;
    if (!mu_slippi_menus_enabled()) {
        return;
    }
    slp.code_entry = 0;
    slp.pause = 0;
    for (mode = gm_GetAllGameModes(); mode->kind != GM_COUNT; mode++) {
        if (mode->kind == GM_HANYU_CSS) {
            mode->preloaded = true;
            mode->on_load = online_on_load;
            mode->on_unload = online_on_unload;
            mode->states = online_states;
            installed = 1;
            break;
        }
    }
}
