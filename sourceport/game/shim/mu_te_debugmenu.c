/* 20XX TE's in-game settings menu, native.
 *
 * 20XX TE turns Tournament Melee into its settings menu, drawn by the game's own debug menu engine
 * (if/textlib_1.c). The native build does the same: with 20XX TE on (offline), Tournament Melee
 * already leads to the debug menu mode (General Codes, mn/mnmain.c), and gmdebugmode.c's first
 * menu asks here for its table. Each 20XX TE feature is an Off/On row with the F1 panel's name;
 * leaving the menu hands both feature words to the host (command 0xF8), which applies and saves
 * them, so the F1 panel and this menu always agree. */
#include <melee/if/types.h>

#include "mu_native.h"
#include "mu_lcancel_flash.h"

int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);
void sfxBack(void);

#define TE_CMD_SETTINGS 0xF8

typedef struct TeRow {
    int word;               /* 1 or 2 */
    unsigned int bit;
    const char* label;
} TeRow;

/* Same features and order as the F1 panel (port/runtime/gx/pc_settings.cpp), labels shortened for
 * the debug menu's column. */
static const TeRow te_rows[] = {
    {1, MU_OPTION_TE_TOURNAMENT, "Tournament Mode :"},
    {1, MU_TE_HOLD_START_PAUSE, "Hold Start to pause :"},
    {1, MU_TE_FROZEN_STAGES, "Frozen stages :"},
    {1, MU_TE_CPU_ZELDA_SHEIK, "CPU Zelda is Sheik :"},
    {1, MU_TE_HANDICAP_STOCKS, "Handicap = stocks :"},
    {1, MU_TE_NO_STAR_KO, "Disable Star KO :"},
    {1, MU_TE_UNFREEZE_ENDGAME, "Play after GAME! :"},
    {1, MU_TE_TAUNT_CANCEL, "Taunt cancelling :"},
    {1, MU_TE_INFINITE_SHIELDS, "Infinite shields :"},
    {1, MU_TE_FIXED_CAMERA, "Fixed camera :"},
    {2, MU_TE2_STAGE_STRIKE, "Stage striking :"},
    {2, MU_TE2_FROZEN_TOGGLE, "Frozen Mode toggle :"},
    {2, MU_TE2_RESET_TOURNAMENT, "Reset rules :"},
    {2, MU_TE2_V100, "v1.00 rules :"},
    {2, MU_TE2_DL64_QUIET, "Quiet DL64 music :"},
    {2, MU_TE2_HANDWARMERS, "Hand-warmer mode :"},
    {2, MU_TE2_RANDOM_MUSIC, "Random stage music :"},
    {2, MU_TE2_SHIELD_COLORS, "Shield colors :"},
    {2, MU_TE2_NO_SCREEN_RUMBLE, "No screen rumble :"},
    {3, 0, "L-cancel flash :"},
    {4, 0, "Success flash :"},
    {2, MU_TE2_SPOOF_PLUGINS, "Spoof plugins :"},
    {2, MU_TE2_BUBBLES, "Collision bubbles :"},
    {2, MU_TE2_INPUT_DISPLAY, "Input display :"},
    {2, MU_TE2_CPU_SMART_DI, "CPU smart DI :"},
    /* ("20XX CPUs" is a plain option on the Game tab, not a TE row: one switch, not two.) */
    {2, MU_TE2_COLOR_OVERLAYS, "Color overlays :"},
    {2, MU_TE2_LOCK, "Lock settings :"},
};
#define TE_ROWS ((int) (sizeof te_rows / sizeof te_rows[0]))

static char te_title[] = "20XX TE Settings";
static char te_locked_title[] = "20XX TE Settings (locked)";
static char te_off[] = "Off";
static char te_on[] = "On";
static char* te_names[2] = {te_off, te_on};
static char* te_flash_names[4] = {"Off", "Missed (red)", "Successful", "Both"};
static char* te_success_names[3] = {"Off", "White", "Green"};

static int te_values[TE_ROWS];
static char te_locked_labels[TE_ROWS][64];
static struct un_80304138_objalloc_t_x8 te_menu[TE_ROWS + 2];

static unsigned int be32(const unsigned char* p)
{
    return (unsigned int) p[0] << 24 | (unsigned int) p[1] << 16 | (unsigned int) p[2] << 8 | p[3];
}

static void put_be32(unsigned char* p, unsigned int v)
{
    p[0] = (unsigned char) (v >> 24);
    p[1] = (unsigned char) (v >> 16);
    p[2] = (unsigned char) (v >> 8);
    p[3] = (unsigned char) v;
}

/* The root table for the debug menu mode, or NULL to keep the game's own (20XX TE off). */
void* mu_te_debug_menu(void)
{
    static unsigned char reply[4096];   /* the host needs room for any reply (MU_SLIPPI_RESPONSE_CAPACITY) */
    unsigned int got = 0, w1, w2;
    int i;

    if (!mu_te_general()) {
        return NULL;
    }
    if (mu_online_abi_command(TE_CMD_SETTINGS, NULL, 0, reply, sizeof reply, &got) != 0 || got != 8) {
        return NULL;   /* an older host: the game's own debug menu */
    }
    w1 = be32(reply);
    w2 = be32(reply + 4);
    te_menu[0].x0 = 0;   /* heading */
    te_menu[0].x8 = (w2 & MU_TE2_LOCK) ? te_locked_title : te_title;
    for (i = 0; i < TE_ROWS; i++) {
        struct un_80304138_objalloc_t_x8* item = &te_menu[i + 1];
        if (te_rows[i].word == 3) {
            const int flash = mu_lcancel_flash_mode(w2, 0);
            te_values[i] = flash >= MU_LCFLASH_TE_MISSED ? flash - 1 : 0;
        } else if (te_rows[i].word == 4) {
            te_values[i] = mu_lcancel_success_color(w2);
        } else {
            te_values[i] = ((te_rows[i].word == 1 ? w1 : w2) & te_rows[i].bit) != 0;
        }
        item->x0 = 2;   /* a choice from xC, the value at x10, x14..x18 its range */
        item->x4 = NULL;
        item->x8 = (char*) te_rows[i].label;
        item->xC = te_rows[i].word == 3 ? te_flash_names : te_rows[i].word == 4 ? te_success_names : te_names;
        item->x10 = &te_values[i];
        item->x14 = 0.0f;
        item->x18 = te_rows[i].word == 3 ? 4.0f : te_rows[i].word == 4 ? 3.0f : 2.0f;
        item->x1C = 0.0f;
        if ((w2 & MU_TE2_LOCK) && te_rows[i].bit != MU_TE2_LOCK) {
            /* textlib type 1 is a label: selectable, but neither left nor right edits it.
             * Include its value in the label, since only type 2 draws xC[x10]. Keep the
             * lock row editable; exit and reopen after unlocking to change other rows. */
            char* dst = te_locked_labels[i];
            const char* label = te_rows[i].label;
            const char* value = item->xC[te_values[i]];
            int at = 0;
            while (*label && at < 62) dst[at++] = *label++;
            while (at < 25) dst[at++] = ' ';
            while (*value && at < 62) dst[at++] = *value++;
            dst[at] = '\0';
            item->x0 = 1;
            item->x8 = dst;
        }
    }
    te_menu[TE_ROWS + 1].x0 = 9;   /* end */
    return te_menu;
}

/* Leaving the menu: the choices go to the host, which applies and saves them. */
void mu_te_debug_menu_save(void)
{
    static unsigned char reply[4096];
    unsigned char payload[8];
    unsigned int got = 0, w1 = MU_OPTION_TE, w2 = 0;
    int i;

    for (i = 0; i < TE_ROWS; i++) {
        if (te_rows[i].word == 3) {
            w2 = mu_lcancel_with_flash_mode(w2, te_values[i] > 0 ? te_values[i] + 1 : MU_LCFLASH_OFF);
        } else if (te_rows[i].word == 4) {
            w2 = mu_lcancel_with_success_color(w2, te_values[i]);
        } else if (te_values[i]) {
            if (te_rows[i].word == 1) {
                w1 |= te_rows[i].bit;
            } else {
                w2 |= te_rows[i].bit;
            }
        }
    }
    put_be32(payload, w1);
    put_be32(payload + 4, w2);
    mu_online_abi_command(TE_CMD_SETTINGS, payload, sizeof payload, reply, sizeof reply, &got);
}
