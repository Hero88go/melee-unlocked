/* 20XX Hack Pack content features, as native C (shim/mu_hp.c, and the debug menu in
 * shim/mu_hp_menu.c): milestones 1 to 4 of run-source/rel09-hackpack/PLAN.md section 3. One row per
 * code library mod and data file in run-source/rel09-hackpack/ledger_M1.md, ledger_M2.md and
 * ledger_M3.md, one per menu page and option in ledger_M4.md.
 *
 * None of the pack's code runs. The host loads the pack's disc as a file overlay and says so
 * (MU_MOD_HACKPACK in mod_flags). The functions here are called from the decompiled statement each
 * of the pack's hook addresses lands on and leave the game alone unless the pack's files are loaded
 * and the game is offline. A replay carries the stage choice (option word 3), so playback loads the
 * same stage file. */
#ifndef MU_HP_H
#define MU_HP_H

/* The pack's files are loaded (whatever the mode). lb/lbheap.c sizes one heap by it at boot. */
int mu_hp_loaded(void);

/* ---- stages (ledger_M2.md) ---- */

/* "Stage Swap Engine", 8025BB40 (mnStageSel_Scene_OnFrame, where the picked stage is stored): the
 * stage the match starts on, after the stage swap table's row for the pick and the current stage
 * select page. Remembers the stage file variant and the custom flag byte for that match. */
int mu_hp_sss_pick(int stkind);
/* "Reload SSS with D-Pad Up/Down", 8025BAFC (same function, before the B test): D-pad down and up
 * step through the four stage select pages. Returns 1 when the page changed (the host saved it);
 * the caller then loads the stage select screen again. */
int mu_hp_sss_input(unsigned int pressed);
/* The pack's stage select file for the current page (its "Default File Name Changes" turn
 * MnSlMap.usd into MnSlMap.1sd; the D-pad steps the digit), or NULL for the game's own.
 * mn/mnstagesel.c, where the screen loads its file. */
const char* mu_hp_sss_file(void);
/* The stage file name to open for a stage: the pack's variant for the picked stage, else the
 * pack's default name for that stage, else `name` itself. gr/ground.c, where the stage file is
 * preloaded and loaded. */
const char* mu_hp_stage_file(int grkind, const char* name);
/* The custom flag byte of the stage in play (the pack's byte at 803FA2E5), 0 when none. */
int mu_hp_stage_flags(void);
/* Pokemon Stadium's fixed transformation sets the 0x80 bit once it has transformed. */
void mu_hp_stage_flags_or(int bits);
/* The pack's stage rules are in effect for the match in play (offline with the pack, or playback of
 * a replay recorded with it). For the stage mods that test the game mode, not the flag byte. */
int mu_hp_stage_on(void);
/* gm/gmvs.c, start of a match: tells the host the stage state before the recording starts. */
void mu_hp_match_begin(int stkind);
/* gm/gm_1A3F.c, 801A4160: a match state (id 2) was left for another state through an explicit
 * next state. The pack puts its patched file name and flag byte back there. */
void mu_hp_stage_leave(void);
/* Scripted runs (--match): the stage after the table, when MELEE_TEST_HP_PAGE asks for a page
 * (1 to 4, as the pack numbers them). gm/gmvsmelee.c, where the request's stage is set. */
int mu_hp_match_stage(int stkind);

/* The pack's stage flag bits, as its hooks test them. */
#define MU_HP_FLAG_OFF     0x80   /* hazard off: lava, ship spawns, bullet, logs, background change */
#define MU_HP_FLAG_GUN     0x40   /* Corneria: Great Fox's gun off */
#define MU_HP_FLAG_FIXED   0x78   /* Stadium: a fixed transformation (0x40 fire, 0x20 grass, 0x10 rock, 0x08 water) */

/* "Pokemon Stadium - Disable Transforms via Custom Flag", 801D45F0 (grStadium_801D4548, the
 * countdown test): 1 when this frame must not start a transformation. */
int mu_hp_stadium_no_transform(void);
/* "Pokemon Stadium - Fixed Transformation Stage", 801D463C (same function, in place of the random
 * pick): 0..3 the transformation to take, -1 for the game's own random pick. */
int mu_hp_stadium_fixed(void);
/* "Pokemon Stadium Frozen Transform Immediately", 801D1538 (grStadium_801D1520): 1 when the
 * transformation logic must run although the stage is frozen. */
int mu_hp_stadium_run_frozen(void);
/* The Stage Swap Engine's stage file patch for a fixed transformation stage (80018130, in
 * lbDvd_GetPreloadedArchive): the transformation timings zeroed so it transforms at once. Returns
 * `param`, or a copy with those words zeroed. gr/grpstadium.c, where OnInit takes the parameters. */
const void* mu_hp_stadium_param(const void* param, unsigned int size);

/* ---- music (ledger_M3.md) ---- */

/* "20XX Music Playlist Code" and "Audio File Name Changes", 80023F28 (lbAudioAx_80023F28, entry):
 * the file inside /audio/ to play for a song id, or NULL for the game's own name. Ids with a high
 * word are the pack's numbered tracks (its stage files ask for them). */
const char* mu_hp_music_file(int song);
/* "Music Playlist Code Supplements", 80225180 (Stage_80225074, end) and 801A1C30 (title screen):
 * the menu playlist draws a new track the next time a song starts. */
void mu_hp_music_mark(void);
/* "Match Start - D-Pad Up to Reload Music", 8006B6A0 (Fighter_procInput, where input is disabled):
 * D-pad up before the match clock starts plays the stage's song again, so its playlist draws anew.
 * `pressed` is the fighter's pressed buttons. */
void mu_hp_music_dpad(unsigned int pressed);

/* ---- the debug menu (ledger_M4.md, shim/mu_hp_menu.c) ---- */

/* The pack's debug menu as a native table for the game's own debug menu engine, reached like the
 * pack's "Debug Menu" entry: VS Mode, Tournament Melee (gm/gmdebugmode.c, onEnterMenu0). NULL unless
 * the pack is loaded, offline, not the vanilla game. Only rows whose feature is native are in it. */
void* mu_hp_debug_menu(void);
/* Leaving the menu (B on its first page): each changed choice goes to the host, which saves it. */
void mu_hp_debug_menu_save(void);
/* The pack's "SAVE STATES/REPLAYS" switches (settings byte MU_HP_SET_TRAINING), in effect with the
 * pack loaded, offline, outside replay playback. shim/mu_lab.c turns its tools on by them. */
#define MU_HP_TRAINING_ON   0x01u   /* savestates and recording on the D-pad in Training mode */
#define MU_HP_TRAINING_LOOP 0x02u   /* a played back recording repeats until a D-pad press */
int mu_hp_training(unsigned int bit);
/* The 20XX TE features the pack's menu switches without a TE save. The host passes these bits alone
 * then (port/app/source_host.cpp, kHpTeOptions and kHpTeOptions2) and mu_te.c accepts them with the
 * pack loaded. The 0x780000 bits are the L-cancel flash choices (mu_lcancel_flash.h). */
#define MU_HP_TE_OPTIONS  (MU_TE_NO_STAR_KO | MU_TE_TAUNT_CANCEL | MU_TE_FIXED_CAMERA)
#define MU_HP_TE_OPTIONS2 (MU_TE2_NO_SCREEN_RUMBLE | MU_TE2_LCANCEL_FLASH | 0x00780000u | MU_TE2_BUBBLES | \
                           MU_TE2_INPUT_DISPLAY | MU_TE2_COLOR_OVERLAYS)

#endif
