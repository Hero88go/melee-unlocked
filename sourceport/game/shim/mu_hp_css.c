/* The 20XX Hack Pack's extra characters, native: milestone 7 of run-source/rel09-hackpack/PLAN.md
 * section 3. One row per mod and per menu row in run-source/rel09-hackpack/ledger_M7.md.
 *
 * How the pack does it. The seven hidden fighters are not new icons: each shares the icon of a
 * normal character. The pack's every frame code on the character select screen (it lives in its
 * code carrying character select file, reached through the branch at 80263350) watches for Z
 * pressed alone by a player who holds a coin over one of seven icons, and then writes the other
 * character of the pair into that icon's character byte in the game's icon table (803F0B24) and
 * swaps the icon's picture. Picking the icon with A then gives the hidden fighter. The pairs:
 *   Zelda / Sheik, Bowser / Giga Bowser, Ice Climbers / Popo alone, Captain Falcon / the male
 *   wireframe, Peach / the female wireframe, Pichu / Master Hand, Pikachu / Crazy Hand.
 * Sandbag has no icon. Around that sit the guards that keep those fighters working where the
 * game never expected them: no costume change on them, a port that comes back to the screen with
 * one keeps it, their names and announcer calls, Giga Bowser and Sandbag enter a match falling,
 * the two hands are driven by their own player's controller.
 *
 * Here the same decisions are plain functions. The icon table is the game's own (mn/mncharsel.c),
 * so the code that reads the controllers and writes the table sits there under MU_NATIVE and asks
 * these functions what to do. The native game shows the retail character select file, which has no
 * pictures for the hidden fighters: an icon keeps its picture and the name under the portrait
 * changes.
 *
 * Gates. The character select part (the swap, the costume lock, the kept port) answers only with
 * the pack loaded, in the normal game, offline, outside replay playback; mn/mncharsel.c puts the
 * six icons that can hold a non playable fighter back whenever the screen opens without that, so
 * an online screen never offers one. The match part (the falling entry, the hands' controls)
 * follows mu_hp_stage_on(): offline with the pack, or playback of a replay recorded with it, so a
 * replay plays the same rules it was recorded with and nothing changes for a match without the
 * pack. The names and announcer calls are text and sound only and need just the pack loaded. */
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>

#include "mu_hp.h"
#include "mu_native.h"

int mu_online_active(void);
int mu_online_pending(void);
int mu_slippi_in_online_mode(void);
int mu_replay_abi_active(void);

/* Character ids as the character select screen and the match start block use them
 * (CharacterKind, ft/forward.h), kept as numbers so the tables below read like the pack's. */
enum {
    HPC_FALCON = 0x00,
    HPC_BOWSER = 0x05,
    HPC_PEACH = 0x0C,
    HPC_PIKACHU = 0x0D,
    HPC_ICE_CLIMBERS = 0x0E,
    HPC_ZELDA = 0x12,
    HPC_SHEIK = 0x13,
    HPC_PICHU = 0x18,
    HPC_MASTER_HAND = 0x1A,   /* also the screen's own "no character yet" value */
    HPC_WIRE_MALE = 0x1B,
    HPC_WIRE_FEMALE = 0x1C,
    HPC_GIGA_BOWSER = 0x1D,
    HPC_CRAZY_HAND = 0x1E,
    HPC_SANDBAG = 0x1F,
    HPC_POPO = 0x20,
};

/* The pairs, with the icon each sits on in the game's icon table (the pack's code names the
 * icons by their index: 80FD045C and on compare the door's icon bytes with 0F, 03, 0C, 07, 04,
 * 12, 13). */
static const struct {
    unsigned char base, extra, icon;
} hpc_pairs[] = {
    {HPC_ZELDA, HPC_SHEIK, 0x0F},
    {HPC_BOWSER, HPC_GIGA_BOWSER, 0x03},
    {HPC_ICE_CLIMBERS, HPC_POPO, 0x0C},
    {HPC_FALCON, HPC_WIRE_MALE, 0x07},
    {HPC_PEACH, HPC_WIRE_FEMALE, 0x04},
    {HPC_PICHU, HPC_MASTER_HAND, 0x12},
    {HPC_PIKACHU, HPC_CRAZY_HAND, 0x13},
};
#define HPC_PAIRS ((int) (sizeof hpc_pairs / sizeof hpc_pairs[0]))

int mu_hp_css_live(void)
{
    return mu_hp_loaded() && !(mu_game_options() & MU_OPTION_VANILLA) && !mu_online_active() &&
           !mu_online_pending() && !mu_slippi_in_online_mode() && !mu_replay_abi_active();
}

/* ---- the every frame code, 80FD03EC: Z over an icon swaps its character ---- */

int mu_hp_css_swap(int icon, int ckind)
{
    int k;
    if (!mu_hp_css_live()) {
        return -1;
    }
    for (k = 0; k < HPC_PAIRS; k++) {
        if (hpc_pairs[k].icon == icon) {
            /* As the pack: the base character gives the hidden one, anything else the base. */
            const int next = ckind == hpc_pairs[k].base ? hpc_pairs[k].extra : hpc_pairs[k].base;
            OSReport("[20xx-hp] character select: icon %d, character %d -> %d\n", icon, ckind, next);
            return next;
        }
    }
    return -1;
}

int mu_hp_css_base(int ckind)
{
    int k;
    /* Sheik is a normal character and the online screens own the Zelda icon: left alone. */
    for (k = 1; k < HPC_PAIRS; k++) {
        if (hpc_pairs[k].extra == ckind) {
            OSReport("[20xx-hp] character select: hidden character %d off its icon, %d again\n", ckind,
                     hpc_pairs[k].base);
            return hpc_pairs[k].base;
        }
    }
    return ckind;
}

/* ---- "Disable X/Y Alt Costumes for Extra Characters", 802600D8, and the "CSS B Button Return
 * Cursor Fix", 8025FE50: both turn on the character being one past the playable ones ---- */

int mu_hp_css_extra(int ckind)
{
    return ckind >= HPC_MASTER_HAND && ckind <= HPC_POPO && mu_hp_css_live();
}

/* ---- "Extra Chars Don't Reset Port If Unavailable", 80264EEC ---- */

int mu_hp_css_keep_icon(int ckind, int slot_type)
{
    int k;
    /* A closed port goes the game's own way, as in the pack. Master Hand's id is also what the
     * game writes into a port with no character at this very place, so it is not kept: a port
     * that held Master Hand while his icon was switched back to Pichu starts empty. */
    if (slot_type == 3 || ckind == HPC_MASTER_HAND || !mu_hp_css_live()) {
        return -1;
    }
    for (k = 0; k < HPC_PAIRS; k++) {
        if (hpc_pairs[k].base == ckind || hpc_pairs[k].extra == ckind) {
            return hpc_pairs[k].icon;
        }
    }
    /* The pack sends every other character here to Bowser's icon (801913CC). Not kept. */
    return -1;
}

/* ---- "Extra Character Nametag Changes", 803D4F7C, 803D4F88, 803D4F94, 803D505C ---- */

const char* mu_hp_ckind_name(int ckind, int english)
{
    if (!mu_hp_loaded()) {
        return 0;
    }
    /* The pack's own strings, which it put into the English table (the pack has no Japanese
     * mode). The game's English table has the wireframes' Japanese names in Latin letters and
     * neither table has anything for Popo, so his name is given in both languages. */
    switch (ckind) {
    case HPC_WIRE_MALE:
        return english ? "M Wireframe" : 0;
    case HPC_WIRE_FEMALE:
        return english ? "F Wireframe" : 0;
    case HPC_POPO:
        return "Ice Climber";
    default:
        return 0;
    }
}

/* ---- "Fighting Wireframes And Popo Announcer", 80168C70, with "Extra Chars Announcer SFX
 * Help", 80168C6C ---- */

int mu_hp_announce(int ckind)
{
    if (!mu_hp_loaded()) {
        return 0;
    }
    switch (ckind) {
    case HPC_WIRE_MALE:
    case HPC_WIRE_FEMALE:
        return 0x7C833;
    case HPC_POPO:
        return 0x7C83B;
    case HPC_SANDBAG:
        return 0x7531;
    case HPC_CRAZY_HAND:
        return 0x7C849;   /* Master Hand's call; the game has none for Crazy Hand */
    default:
        return 0;
    }
}

/* ---- the match: "Giga-Bowser & Sandbag Always Fall On Match Start", 80069328 ---- */

int mu_hp_spawn_falling(const struct Fighter* fp)
{
    static int said;
    if (!mu_hp_stage_on() || (fp->kind != Ft_Kind_GKoops && fp->kind != Ft_Kind_Sandbag)) {
        return 0;
    }
    if (!said) {
        said = 1;
        OSReport("[20xx-hp] fighter kind %d enters falling\n", (int) fp->kind);
    }
    return 1;
}

/* ---- "MasterHand & CrazyHand Controlled By All Ports", 801508B8 and 80156AFC ---- */

unsigned int mu_hp_boss_buttons(const struct Fighter* fp, unsigned int pad)
{
    static int said;
    if (!mu_hp_stage_on()) {
        return pad;
    }
    if (!said) {
        said = 1;
        OSReport("[20xx-hp] hand of player %d takes its own controller\n", fp->player_idx + 1);
    }
    /* The fighter's held buttons (fp + 0x65C), in place of the third or fourth controller. */
    return fp->input.held_buttons[0];
}

/* ---- "CrazyHand - Disable D-Pad Up+B Attack", 80156CE0, and "Crazy Hand Tag Team Fix",
 * 8015C2F8: both hold whenever the pack's rules are in effect ---- */

int mu_hp_boss_rules(void)
{
    return mu_hp_stage_on();
}
