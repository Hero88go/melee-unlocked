/* 20XX Hack Pack content features, as native C (shim/mu_hp.c): milestones 1 to 3 of
 * run-source/rel09-hackpack/PLAN.md section 3. One row per code library mod and data file in
 * run-source/rel09-hackpack/ledger_M1.md, ledger_M2.md and ledger_M3.md.
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
 * step through the four stage select pages. Returns 1 when the page changed. */
int mu_hp_sss_input(unsigned int pressed);
/* The stage file name to open for a stage: the pack's variant for the picked stage, else the
 * pack's default name for that stage, else `name` itself. gr/ground.c, where the stage file is
 * preloaded and loaded. */
const char* mu_hp_stage_file(int grkind, const char* name);
/* The custom flag byte of the stage in play (the pack's byte at 803FA2E5), 0 when none. */
int mu_hp_stage_flags(void);
/* Pokemon Stadium's fixed transformation sets the 0x80 bit once it has transformed. */
void mu_hp_stage_flags_or(int bits);
/* gm/gmvs.c, start of a match: tells the host the stage state before the recording starts. */
void mu_hp_match_begin(int stkind);
/* gm/gm_1A3F.c, 801A4160: a match state (id 2) was left for another state through an explicit
 * next state. The pack puts its patched file name and flag byte back there. */
void mu_hp_stage_leave(void);
/* Scripted runs (--match): the stage after the table, when MELEE_TEST_HP_PAGE asks for a page. */
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

/* ---- music (ledger_M3.md) ---- */

/* "20XX Music Playlist Code" and "Audio File Name Changes", 80023F28 (lbAudioAx_80023F28, entry):
 * the file inside /audio/ to play for a song id, or NULL for the game's own name. Ids with a high
 * word are the pack's numbered tracks (its stage files ask for them). */
const char* mu_hp_music_file(int song);
/* "Music Playlist Code Supplements", 80225180 (Stage_80225074, end) and 801A1C30 (title screen):
 * the menu playlist draws a new track the next time a song starts. */
void mu_hp_music_mark(void);

#endif
