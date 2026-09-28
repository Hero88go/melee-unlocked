/* 20XX TE menu and cosmetic features, native (N5): extra shield colors.
 *
 * The shield bubble's color is the color the game spawns its effect with (ftCo_Guard.c, from the
 * port or team color). With "Extra shield colors" on, a player picks one of 18 colors with L and R on
 * the character select screen; the choice holds for the session and only changes that color.
 * Visual only: it never touches the game state the simulation reads. */
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/mn/types.h>

#include "mu_native.h"

static const u8 shield_rgb[][3] = {
    {0xF2, 0x59, 0x59}, {0x66, 0x66, 0xFF}, {0xFF, 0xBF, 0x40}, {0x4D, 0xE6, 0x4D}, {0x4D, 0xE6, 0xE6},
    {0xF2, 0x59, 0xF2}, {0xFF, 0x9C, 0x00}, {0xAF, 0xFF, 0x25}, {0xFF, 0x76, 0xFA}, {0xA0, 0x49, 0xE2},
    {0x5E, 0x1C, 0xF2}, {0x65, 0x00, 0x00}, {0x18, 0x44, 0x18}, {0x14, 0x23, 0x94}, {0x50, 0x37, 0x06},
    {0xDD, 0xDD, 0xDD}, {0x33, 0x33, 0x33}, {0x80, 0x80, 0x80},
};
static const char* const shield_names[] = {
    "Red", "Blue", "Yellow", "Green", "Cyan", "Magenta", "Orange", "Lime", "Pink",
    "Purple", "Indigo", "Dark red", "Dark green", "Dark blue", "Brown", "White", "Dark gray", "Gray",
};
#define SHIELD_COUNT ((int) (sizeof shield_rgb / sizeof shield_rgb[0]))

/* Per port: -1 = the game's own color, else an index into the table. */
static signed char shield_choice[4] = {-1, -1, -1, -1};

int mu_te_shield_color(int slot, unsigned int* rgb)
{
    if (slot < 0 || slot >= 4 || shield_choice[slot] < 0 || !mu_te2(MU_TE2_SHIELD_COLORS)) {
        return 0;
    }
    {
        const u8* c = shield_rgb[(int) shield_choice[slot]];
        *rgb = (unsigned int) c[0] << 16 | (unsigned int) c[1] << 8 | c[2];
    }
    return 1;
}

/* L (-1) or R (+1) on the character select screen. The first press starts from the port's own
 * color, like the retail default (red, blue, yellow, green). Returns the new index. */
int mu_te_shield_cycle(int port, int dir)
{
    int index;
    if (port < 0 || port >= 4) {
        return -1;
    }
    index = shield_choice[port] < 0 ? port : shield_choice[port];
    index = (index + dir + SHIELD_COUNT) % SHIELD_COUNT;
    shield_choice[port] = (signed char) index;
    return index;
}

int mu_te_shield_choice(int port)
{
    return port >= 0 && port < 4 ? shield_choice[port] : -1;
}

const char* mu_te_shield_name(int index)
{
    return index >= 0 && index < SHIELD_COUNT ? shield_names[index] : "";
}

void mu_te_shield_rgb(int index, unsigned char* r, unsigned char* g, unsigned char* b)
{
    if (index < 0 || index >= SHIELD_COUNT) {
        *r = *g = *b = 0xFF;
        return;
    }
    *r = shield_rgb[index][0];
    *g = shield_rgb[index][1];
    *b = shield_rgb[index][2];
}

/* 20XX TE hand-warmer mode: on in a 1-minute time match (the match's own rules, so a replay agrees). */
int mu_te_handwarmers(void)
{
    const struct StartMeleeRules* rules;
    if (!mu_te2(MU_TE2_HANDWARMERS)) {
        return 0;
    }
    rules = gm_GetStartMeleeRules();
    return rules != NULL && !rules->is_stock && rules->timer_enabled && rules->time_limit == 60;
}

/* The rules 20XX TE sets: stock, 4 stocks, 8-minute limit, friendly fire on (TE's boot defaults). */
static void te_rules(void)
{
    GameRules* rules = gmMainLib_GetGameRules();
    rules->mode = 1;
    rules->time_limit = 0;
    rules->stock_count = 4;
    rules->handicap = 0;
    rules->damage_ratio = 10;
    rules->stage_sel = 0;
    rules->stock_time_limit = 8;
    rules->friendly_fire = 1;
}

/* At every VS character select: TE's boot defaults once per session; in Tournament Mode with the
 * reset option, the tournament rules again, items off and the tournament stage list (Fountain of
 * Dreams left out in doubles). */
void mu_te_css_rules(int doubles)
{
    static int booted;
    if (!mu_te_general()) {
        return;
    }
    if (!booted) {
        booted = 1;
        te_rules();
    }
    if ((mu_game_options() & MU_OPTION_TE_TOURNAMENT) && mu_te2(MU_TE2_RESET_TOURNAMENT)) {
        struct GamePrefs* prefs = gmMainLib_GetGamePrefs();
        te_rules();
        prefs->item_freq = 0xFF;
        prefs->stage_mask = doubles ? 0xE7000090u : 0xE70000B0u;
    }
}
