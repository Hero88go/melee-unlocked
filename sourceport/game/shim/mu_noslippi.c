/* The game without the Slippi layer (built only with MU_NO_SLIPPI).
 *
 * The game sources call into the online menus, replay playback, the replay recording and practice
 * matchmaking at many sites. Those files are not in this build; this one gives every site the answer
 * the real function gives when the menus are off and no replay is playing or being recorded, so the
 * game runs as the retail game there. Three things are more than a fixed answer and are kept as the
 * project's own code: the per-match rules of a network match, the behaviours a rollback match needs
 * to stay in step (mu_replay_code), and two helpers the rollback engine calls.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <string.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/types.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/gr/stage.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/sislib.h>

/* ---- behaviours of a rollback match ----
 * A rollback match re-runs frames without drawing them and runs on two machines whose cameras,
 * loading times and leftover memory differ. These are the places where the retail game lets one of
 * those reach the simulation; each is switched to its machine-independent form for the length of a
 * network match (and while one is being set up, since the stage and fighters are created then):
 *   INIT_STAGE_DATA, INIT_PLAYER_DATA   stage and fighter blocks start zeroed, not as the heap left them
 *   OFFSCREEN_DAMAGE                    off-screen damage from the stage's camera limits, not from
 *                                       whether the magnifier bubble is being drawn
 *   DEAD_UP_FALL, WHISPY_FIX            a star-KO fall follows the camera; its physics position is
 *                                       frozen and a dead fighter no longer steers Whispy's wind
 *   FD_BG_SEED                          Final Destination's background leaves the random seed alone
 *   PS_FILE_LOAD, PS_IS_VALID,          Pokemon Stadium loads a transformation at once instead of
 *   PS_ZERO_BUFFER                      whenever the disc read finishes
 *   PS_MONITOR                          the Stadium monitor picks its view from a fixed box, not the camera
 *   NANA_DETERMINISM                    kept with the others; no site reads it today
 * Not kept, because they are rules and not determinism: the wobbling limit, the frozen Stadium
 * choice, and everything in the General Codes (UCF, neutral spawns, the freeze glitch fix). */
#define MU_ROLLBACK_CODES                                                                          \
    (MU_RC_INIT_STAGE_DATA | MU_RC_INIT_PLAYER_DATA | MU_RC_OFFSCREEN_DAMAGE | MU_RC_DEAD_UP_FALL | \
     MU_RC_FD_BG_SEED | MU_RC_PS_ZERO_BUFFER | MU_RC_PS_IS_VALID | MU_RC_PS_FILE_LOAD |            \
     MU_RC_WHISPY_FIX | MU_RC_NANA_DETERMINISM | MU_RC_PS_MONITOR)

int mu_replay_code(unsigned int code)
{
    return (mu_online_active() || mu_online_pending()) && (code & MU_ROLLBACK_CODES) != 0;
}

/* Off-screen damage (fighter.c, under MU_RC_OFFSCREEN_DAMAGE): nonzero when the fighter is outside
 * the stage's camera limits. */
int mu_offscreen_damage_zone(Fighter* fp)
{
    float x = fp->cur_pos.x, y = fp->cur_pos.y;

    if (gm_GetCurrentGameMode() == GM_HOME_RUN_CONTEST) {
        return 0;   /* the Sandbag takes no off-screen damage */
    }
    if (fp->x221F_b1) {
        return 0;   /* dead */
    }
    if (fp->motion_id == 4 || fp->motion_id == 6) {
        return 0;   /* star KO, screen KO */
    }
    return x < Stage_GetCamBoundsLeftOffset() || x > Stage_GetCamBoundsRightOffset() ||
           y > Stage_GetCamBoundsTopOffset() || y < Stage_GetCamBoundsBottomOffset();
}

/* The console's draw pass sets up every skinning bone's world matrix each frame, and gameplay then
 * reads those matrices raw. The native renderer does not do that for bones it does not draw, and a
 * re-simulated frame is not drawn at all, so the part joints are refreshed here (ftdrawcommon.c at
 * the end of a fighter's draw, mu_online.c between re-simulated frames). */
void mu_refresh_part_matrices(Fighter* fp)
{
    u32 n, i;
    if (fp->parts == NULL || (unsigned) fp->kind >= FT_KIND_TABLE_MAX ||
        DP(ftPartsTable[fp->kind]) == NULL) {
        return;
    }
    n = DP(ftPartsTable[fp->kind])->parts_num;
    for (i = 0; i < n; i++) {
        HSD_JObj* j = fp->parts[i].joint;
        if (j != NULL) {
            HSD_JObjSetupMatrix(j);
        }
    }
}

/* ---- per-match rules of a network match (set by mu_online_start_melee, constant for the match) ---- */

static int rules_mode = -1;
static int rules_frozen_stadium;
static int rules_is_teams;
static int rules_local_port = -1;

void mu_online_rules_set(int mode, int frozen_stadium, int is_teams, int local_port)
{
    rules_mode = mode;
    rules_frozen_stadium = frozen_stadium != 0;
    rules_is_teams = is_teams != 0;
    rules_local_port = local_port;
}

void mu_online_rules_clear(void)
{
    rules_mode = -1;
    rules_frozen_stadium = 0;
    rules_is_teams = 0;
    rules_local_port = -1;
}

int mu_online_rules_mode(void) { return rules_mode; }
int mu_online_frozen_stadium(void) { return rules_frozen_stadium; }
int mu_online_rules_is_teams(void) { return rules_is_teams; }
int mu_online_rules_local_port(void) { return rules_local_port; }
/* Read only under MU_RC_PS_FROZEN_CHECK, which this build never turns on. */
int mu_ps_frozen_toggle(void) { return mu_online_active() ? rules_frozen_stadium : 0; }

/* ---- replay playback and recording: none ---- */

int mu_replay_on(void) { return 0; }
int mu_replay_allows(unsigned int code) { (void) code; return 1; }   /* no replay restricts anything */
int mu_replay_frame_index(void) { return -123; }                      /* the value before any match */
void mu_replay_frame_begin(void) {}
int mu_replay_terminated(void) { return 0; }
int mu_replay_stock_steal(int pad_port) { (void) pad_port; return 0; }
void mu_replay_input(Fighter* fp) { (void) fp; }
void mu_replay_post_frame(Fighter* fp) { (void) fp; }
int mu_replay_ps_frozen(void) { return 0; }
void mu_replay_online_start(StartMeleeData* data, const unsigned char* msrb) { (void) data; (void) msrb; }
void mu_replay_flush_frame(void) {}
void mu_replay_lcancel_reset(Fighter* fp) { (void) fp; }
void mu_replay_lcancel_landing(Fighter* fp) { (void) fp; }
void mu_replay_record_whispy(int direction) { (void) direction; }
void mu_replay_record_fountain(const HSD_JObj* platform, float height) { (void) platform; (void) height; }
void mu_replay_record_stadium(int state, int kind) { (void) state; (void) kind; }
int mu_replay_abi_active(void) { return 0; }
/* mu_exclusions.c lists this provider; there is no replay data cache to leave out here. */
unsigned int mu_exclusions_replay_abi(const MuExclusion** out) { *out = NULL; return 0; }

/* ---- practice matchmaking: none ---- */

int mu_practice_resolve_pending_mode(int requested) { return requested; }
void mu_practice_enter_mode(int mode) { (void) mode; }

/* The game API's scene query stays (the host asks which mode is running); the operations that
 * moved a practice match into the online menus are refused. */
int mu_practice_bridge(int op, int* a, int n)
{
    if (op == 0 && n >= 2) {   /* MU_PRACTICE_SCENE */
        a[0] = gm_GetCurrentGameMode();
        a[1] = gm_GetCurrentSceneIndex();
        return 0;
    }
    return -1;
}

/* ---- online menus: off ---- */

static MuSlippiMenuState menu_state;   /* read by name entry and stage select; stays zero */

int mu_slippi_menus_enabled(void) { return 0; }
MuSlippiMenuState* mu_slippi_state(void) { return &menu_state; }
int mu_slippi_code_suggestion(const MuCodeSuggestionReq* in, MuCodeSuggestion* out)
{
    (void) in;
    if (out != NULL) {
        memset(out, 0, sizeof *out);
    }
    return -1;
}
int mu_slippi_in_online_mode(void) { return 0; }
int mu_slippi_in_online_game(void) { return 0; }
int mu_slippi_on_online_sss(void) { return 0; }
int mu_slippi_on_online_splash(void) { return 0; }
int mu_slippi_zelda_is_sheik(void) { return 0; }
int mu_slippi_css_text_heap(void) { return 0; }                       /* 0: the retail text heap */
unsigned char mu_slippi_team_costume(int team, int ckind) { (void) team; (void) ckind; return 0; }
int mu_slippi_option_unlocked(int sel) { (void) sel; return 0; }
unsigned char mu_slippi_boot_mode(unsigned char mode) { return mode; }   /* the retail boot */
void mu_slippi_menu_prep(void) {}
int mu_slippi_menu_enter(int previous_mode, unsigned char* menu_kind, unsigned char* hovered)
{
    (void) previous_mode; (void) menu_kind; (void) hovered;
    return 0;   /* the retail menu picks its own page */
}
void mu_slippi_main_menu_loaded(void) {}
void mu_slippi_switch_to_online_menu(void) {}
int mu_slippi_menu_clear_check(int menus_differ) { return menus_differ; }
void mu_slippi_online_menu_think(HSD_GObj* gp) { (void) gp; }
void mu_slippi_scene_install(void) {}

int mu_slippi_css_online(void) { return 0; }
int mu_slippi_css_teams(void) { return 0; }
void mu_slippi_css_enter(struct CSSData** css, unsigned char* char_chosen, signed char* ctrl_port,
                         unsigned char* scene_request, signed char* name_entry_port,
                         struct HSD_JObj** single_menu_root)
{
    (void) css; (void) char_chosen; (void) ctrl_port; (void) scene_request; (void) name_entry_port;
    (void) single_menu_root;
}
void mu_slippi_css_poll(void) {}
int mu_slippi_css_inputs(unsigned int trigger) { (void) trigger; return 0; }
void mu_slippi_css_text_init(void) {}
int mu_slippi_css_skip_return_sound(void) { return 0; }
int mu_slippi_css_block_unselect(void) { return 0; }
int mu_slippi_css_block_costume_change(void) { return 0; }
int mu_slippi_css_local_ready(void) { return 0; }
int mu_slippi_css_team_idx(void) { return 0; }
void mu_slippi_css_set_team_idx(int team) { (void) team; }
void mu_slippi_chat_enter(void) {}
void mu_slippi_chat_frame(void) {}
int mu_slippi_css_zelda_icon_reset(void) { return 1; }                /* 1: the retail icon kind */

int mu_slippi_splash_active(void) { return 0; }
void mu_slippi_splash_text(void) {}
unsigned int mu_slippi_splash_announce_char(unsigned int vanilla) { return vanilla; }
void mu_slippi_splash_hide_letters(HSD_JObj* jobj) { (void) jobj; }
int mu_slippi_splash_skip_stage_number(void) { return 0; }
int mu_slippi_splash_hide_stage(HSD_JObj* jobj) { (void) jobj; return 0; }
void mu_slippi_ingame_hud_init(void) {}
/* NULL: the rollback engine's on-screen notices (disconnected, desync) are skipped; it still logs
 * them and ends the match. */
HSD_Text* mu_slippi_hud_text(void) { return NULL; }
int mu_slippi_digits_no_kerning(void) { return 0; }
void mu_slippi_results_control_all_panels(void) {}
unsigned char mu_slippi_results_slot_type(int port, unsigned char slot_type) { (void) port; return slot_type; }

void* mu_slippi_sss_slpcss(void) { return NULL; }
int mu_slippi_sss_frozen(void) { return 0; }
void mu_slippi_sss_frozen_flip(void) {}
void mu_slippi_sss_run_callback(int stage_behavior) { (void) stage_behavior; }
