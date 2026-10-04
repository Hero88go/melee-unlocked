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

/* ---- training displays (ledger_M6.md, shim/mu_hp_train.c) ---- */

/* Settings bytes past the first 24 (the host's block has to be MU_HP_TRAIN_SETTINGS bytes long for
 * them to be kept; with a shorter block they read 0, the pack's own defaults, and their menu rows
 * show as not available). All of them are displays: nothing here changes what a match plays, so
 * they are not recorded in a replay. */
#define MU_HP_SET_LCANCEL_OFF     24u   /* bits 0..3: no white flash for P1..P4; bits 4..7: no red flash */
#define MU_HP_SET_BUBBLE_FLAGS    25u   /* bit 0: hitbox colors by hitbox id */
#define MU_HP_SET_HITBOX_ALPHA    26u   /* the hitbox bubble's alpha, stored xor 0x80 (0 is the pack's 128) */
#define MU_HP_SET_OVERLAY_PLAYERS 27u   /* bits 0..3: the pack's color overlays for P1..P4 */
#define MU_HP_SET_OVERLAY_COLOR   28u   /* 9 bytes, a color 0 (none) to 22, in the order of MU_HP_OV_* */
#define MU_HP_SET_OVERLAY_ALPHA   37u   /* the overlays' alpha, stored xor 0xFF (0 is the pack's 255) */
#define MU_HP_TRAIN_SETTINGS      48u
enum {
    MU_HP_OV_HITLAG,       /* HITLAG (DEFENDER), 803FA4A4 */
    MU_HP_OV_HITSTUN,      /* HITSTUN, 803FA4A8 */
    MU_HP_OV_AUTOCANCEL,   /* AUTO-CANCEL ENABLED, 803FA4B0 */
    MU_HP_OV_IASA,         /* IASA ENABLED, 803FA4B4 */
    MU_HP_OV_SMASH_TURN,   /* MISSED SMASH TURN, 803FAEC8 */
    MU_HP_OV_NANA,         /* NANA CPU TYPE DESYNCED, 803FA328 */
    MU_HP_OV_WD_TIMING,    /* PERFECT WAVEDASH TIMING, 803FA4AC */
    MU_HP_OV_WD_WINDOW,    /* WAVEDASH JOYSTICK WINDOW, 803FA4BC */
    MU_HP_OV_WD_BOTH,      /* PERFECT TIMING AND IN WINDOW, 803FA4C0 */
    MU_HP_OV_COUNT
};
/* One settings byte, 0 unless the pack is loaded, offline, the normal game, outside replay playback
 * (shim/mu_hp_menu.c, read from the host once and again after the menu saves). */
int mu_hp_setting(unsigned int index);

struct Fighter;
/* "L-Cancel Options", 8008D698 (ftCo_LandingAir_EnterWithLag): the pack's flash switches are per
 * player. 1 when this player's flash for this result (in time or missed) is switched off. */
int mu_hp_lcancel_flash_off(const struct Fighter* fp, int in_time);
/* "Hitbox Color IDs", 80009F60 (lbColl_80009F54, entry): the hitbox bubble's color by hitbox id and
 * its alpha. ft/ftdrawcommon.c names the id of the fighter hitbox about to be drawn (-1 after its
 * loop); lb/lbcollision.c hands over the color the game is about to use (four bytes: r, g, b, a). */
void mu_hp_hitbox_id(int id);
void mu_hp_hitbox_color(unsigned char* rgba);
/* "20XX Color Overlays", 800BF550 (ftMaterial_800BF534, entry): a color over the fighter while it
 * is in hitlag, hitstun, an aerial that would auto-cancel, IASA frames, and so on. */
void mu_hp_overlay(struct Fighter* fp);

/* ---- extra characters (ledger_M7.md, shim/mu_hp_css.c) ---- */

/* The pack's character select features are in effect: the pack is loaded, the normal game,
 * offline, outside replay playback. */
int mu_hp_css_live(void);
/* The pack's every frame code on the character select screen (the branch at 80263350, the part at
 * 80FD03EC): Z pressed alone by a player who holds a coin over one of seven icons swaps that
 * icon's character with its hidden one. `icon` is the icon's index in the game's table, `ckind`
 * the character it shows now; returns the character to write into the icon, -1 for none. */
int mu_hp_css_swap(int icon, int ckind);
/* The character an icon goes back to when the screen opens without the pack's features (an
 * online screen must never offer a hidden fighter): the pair's normal character for the six non
 * playable ones, `ckind` itself for anything else. Works whatever the mode. */
int mu_hp_css_base(int ckind);
/* "Disable X/Y Alt Costumes for Extra Characters", 802600D8 (mnCharSel_CostumeChange), and "CSS B
 * Button Return Cursor Fix", 8025FE50 (mnCharSel_8025FDEC): 1 for a character past the playable
 * ones (Master Hand to Popo) while the pack's features are in effect. */
int mu_hp_css_extra(int ckind);
/* "Extra Chars Don't Reset Port If Unavailable", 80264EEC (mnCharSel_802640A0, where a port whose
 * character is on no icon is emptied): the icon the port's coin goes to instead, -1 for the game's
 * own way. `slot_type` is the port's (3 is closed). */
int mu_hp_css_keep_icon(int ckind, int slot_type);
/* "Extra Character Nametag Changes" (data, 803D4F7C and on): the pack's name for a character, NULL
 * for the game's own. `english` is the saved language. gm/gm_1601.c, where a character's name is
 * looked up. */
const char* mu_hp_ckind_name(int ckind, int english);
/* "Fighting Wireframes And Popo Announcer", 80168C70 (gm_80168C5C): the announcer call for a
 * character the game has none for, 0 for the game's own. */
int mu_hp_announce(int ckind);
/* "Giga-Bowser & Sandbag Always Fall On Match Start", 80069328 (Fighter_Create, where the entry
 * is picked): 1 when this fighter starts the match falling instead of on its entry. */
int mu_hp_spawn_falling(const struct Fighter* fp);
/* "MasterHand & CrazyHand Controlled By All Ports", 801508B8 (ftMh_MS_341_80150894) and 80156AFC
 * (ftCh_Init_80156AD8): the buttons a hand's player commands come from. `pad` is what the game
 * reads (the third controller for Master Hand, the fourth for Crazy Hand); with the pack's rules
 * in effect the fighter's own held buttons come back instead. */
unsigned int mu_hp_boss_buttons(const struct Fighter* fp, unsigned int pad);
/* "CrazyHand - Disable D-Pad Up+B Attack", 80156CE0, and "Crazy Hand Tag Team Fix", 8015C2F8
 * (ftBossLib_8015C2E0): 1 while the pack's match rules are in effect (mu_hp_stage_on). */
int mu_hp_boss_rules(void);

#endif
