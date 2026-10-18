/* The Lossless adapter's Melee lag test, natively (the launch the host starts with --lag-test).
 *
 * The adapter's manager app sets up Slippi Dolphin with a fixed list of Gecko codes, boots vanilla
 * NTSC 1.02, and lets the adapter take over: a 10-second rumble when the character select loads
 * tells it the game is ready, it plays its own input sequence into a Dream Land match, then holds X
 * (the background turns white) and A (black) while its photodiode, held beside Dream Land, times
 * the change. This file is that list in C, for the codes that patch game code (the Static Recomp
 * runs the list itself through the console's Gecko handler):
 *
 *   044D4A08 00000004  Enable Develop Mode              gm/gmmain.c (DbLevel, assert skipped)
 *   0415FE84 48000014  Skip debug level assert [Fizzi]  gm/gmmain.c
 *   041BFA20 38600002  Boot to CSS                      gm/gmboot.c
 *   043D4A48.. 043D4A78 rules and stage list            mu_lag_test_default_rules
 *   C22668B0           Rumble upon loading the CSS      mu_lag_test_css_enter (mn/mncharsel.c)
 *   0622873C           X white / A black background     mu_lag_test_visual_effects (db/dbeffect.c)
 *
 * The rest of the list (unlock everything, skip the memory card prompt, faster character select
 * load) is already in the General Codes this game runs (mu_gecko.c). Nothing here runs unless the
 * host started the lag test; it is offline only. */
#include <melee/cm/camera.h>
#include <melee/db/db.h>
#include <dolphin/os.h>
#include <melee/gm/types.h>
#include <melee/if/ifall.h>
#include <melee/lb/lb_013B.h>
#include <sysdolphin/baselib/controller.h>

extern char* getenv(const char* name);   /* the decomp's C library headers do not declare it */

extern GameRules gmMainLib_DefaultGameRules;
extern struct GamePrefs gmMainLib_DefaultGamePrefs;

int mu_lag_test(void)
{
    static int state = -1;
    if (state < 0) {
        state = getenv("MELEE_LAG_TEST") != NULL;
        if (state) {
            OSReport("[lagtest] Lossless adapter lag test: develop mode, boot to the character select, "
                     "rumble on its load, X white / A black background\n");
        }
    }
    return state;
}

/* 043D4A48 00340000, 043D4A4C 63000A01, 043D4A50 00010100: time mode with no time limit, 99 stocks,
 * handicap off, damage ratio 1.0, random stage select, friendly fire and pause on, no score display.
 * 043D4A60 FF000000: no items. 043D4A78 E4000000: Dream Land the only stage on random. Written after
 * the General Codes' own defaults, as the user codes run after them on Dolphin. */
void mu_lag_test_default_rules(void)
{
    if (!mu_lag_test()) {
        return;
    }
    gmMainLib_DefaultGameRules.force_main_menu = 0;
    gmMainLib_DefaultGameRules.bgm = 0x34;
    gmMainLib_DefaultGameRules.mode = 0;
    gmMainLib_DefaultGameRules.time_limit = 0;
    gmMainLib_DefaultGameRules.stock_count = 0x63;
    gmMainLib_DefaultGameRules.handicap = 0;
    gmMainLib_DefaultGameRules.damage_ratio = 10;
    gmMainLib_DefaultGameRules.stage_sel = 1;
    gmMainLib_DefaultGameRules.stock_time_limit = 0;
    gmMainLib_DefaultGameRules.friendly_fire = 1;
    gmMainLib_DefaultGameRules.pause = 1;
    gmMainLib_DefaultGameRules.score_display = 0;
    gmMainLib_DefaultGamePrefs.item_freq = 0xFF;
    gmMainLib_DefaultGamePrefs.stage_mask = 0xE4000000;
}

/* C22668B0 (External/Arte Lag Tester/ForcefullyRumble.asm): at the start of the character select's
 * OnEnter, lb_80014574(port, 0, 0, 600) for ports 0 to 3. */
void mu_lag_test_css_enter(void)
{
    int port;
    if (!mu_lag_test()) {
        return;
    }
    for (port = 0; port < 4; port++) {
        lb_80014574((u8) port, 0, 0, 0x258);
    }
}

/* 0622873C (Arte): replaces fn_CheckMiscVisualEffects, which develop mode calls for each port every
 * frame. X held: the background colour white. A held: the HUD hidden, the stage drawn, the background
 * colour black. Returns 1 when it took the call. */
int mu_lag_test_visual_effects(int player)
{
    HSD_Pad buttons;
    if (!mu_lag_test()) {
        return 0;
    }
    buttons = db_ButtonsDown(player);
    if (buttons & HSD_PAD_X) {
        Camera_SetBackgroundColor(0xFF, 0xFF, 0xFF);
    } else if (buttons & HSD_PAD_A) {
        ifAll_HideHUD();
        Camera_SetStageVisible(1);
        Camera_SetBackgroundColor(0, 0, 0);
    }
    return 1;
}
