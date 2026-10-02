/* 20XX Hack Pack content features, as native C: milestones 1 to 3 of
 * run-source/rel09-hackpack/PLAN.md section 3. None of the pack's code runs.
 *
 * State of this file: milestone 1 only. mu_hp_loaded() is live (lb/lbheap.c sizes one heap by it).
 * The stage (milestone 2) and music (milestone 3) functions are declared in mu_hp.h and defined
 * here so the game links, but they are inert: no decompiled statement calls them yet, and each one
 * leaves the game exactly as it is. The rewrites replace these bodies. */
#include "mu_shim.h"
#include "mu_hp.h"

unsigned int mu_mod_flags(void);   /* mu_entry.c */

int mu_hp_loaded(void)
{
    return (mu_mod_flags() & MU_MOD_HACKPACK) != 0;
}

/* ---- stages (milestone 2): inert ---- */

int mu_hp_sss_pick(int stkind) { return stkind; }
int mu_hp_sss_input(unsigned int pressed) { (void) pressed; return 0; }
const char* mu_hp_stage_file(int grkind, const char* name) { (void) grkind; return name; }
int mu_hp_stage_flags(void) { return 0; }
void mu_hp_stage_flags_or(int bits) { (void) bits; }
void mu_hp_match_begin(int stkind) { (void) stkind; }
void mu_hp_stage_leave(void) {}
int mu_hp_match_stage(int stkind) { return stkind; }
int mu_hp_stadium_no_transform(void) { return 0; }
int mu_hp_stadium_fixed(void) { return -1; }
int mu_hp_stadium_run_frozen(void) { return 0; }

/* ---- music (milestone 3): inert ---- */

const char* mu_hp_music_file(int song) { (void) song; return 0; }
void mu_hp_music_mark(void) {}
