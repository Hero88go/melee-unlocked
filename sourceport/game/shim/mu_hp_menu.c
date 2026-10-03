/* The 20XX Hack Pack's debug menu, native: milestone 4 of run-source/rel09-hackpack/PLAN.md section 3.
 * One row per page and option of the pack's menu, with what each maps to, in
 * run-source/rel09-hackpack/ledger_M4.md.
 *
 * The pack's menu is the game's own debug menu engine (if/textlib_1.c) driven by tables that live in
 * the pack's code carrying character select file (MnSlChr.0sd, which the pack's code loads at
 * 80BEC720); the native game never loads that file. Here the same engine draws a native table with
 * the pack's page and option names, holding only the rows whose feature is native today. It is
 * reached the way the pack's "Debug Menu" entry is: VS Mode, Tournament Melee, which with the General
 * Codes on already leads to the debug menu mode (mn/mnmain.c). gmdebugmode.c's first menu asks here
 * first, then 20XX TE's menu (shim/mu_te_debugmenu.c), then keeps the game's own.
 *
 * Moving around is the engine's own: up and down with the stick (or Y and X), a value with the stick
 * left and right (or L and R), A on a "PAGE >" row opens that page in place of the one it is on, B
 * goes back a page. B on the first page leaves the menu and saves; like the pack, leaving goes to
 * the character select screen.
 *
 * Where each choice goes, always through a store the host already has (the game keeps nothing):
 *   - the pack's settings block (host command 0xFA, MU_HP_SET_*): stage select page, legal stage
 *     variants, playlist types, the two global playlist switches, the SAVE STATES/REPLAYS switches;
 *   - 20XX TE's two feature words (host command 0xF8, as TE's own menu): the TE features the pack also
 *     has. Without a TE save the host changes and passes only the pack's share (MU_HP_TE_OPTIONS and
 *     MU_HP_TE_OPTIONS2 in mu_hp.h);
 *   - option word 3 (host command 0xFA, MU_HP_OP_CPU_WORD): the CPU training options, the Game tab's
 *     own rows, which the settings panel saves.
 * Offline only, never with the vanilla game; the host refuses changes online and in playback. */
#include <dolphin/os.h>
#include <melee/if/textlib.h>
#include <melee/if/types.h>
#include <melee/lb/lbaudio_ax.h>

#include "mu_hp.h"
#include "mu_lcancel_flash.h"
#include "mu_native.h"

#include <string.h>

/* The host ABI's values this file uses (port/runtime/abi/mu_host.h). That header clashes with the
 * game's types, so game-side files keep their own copy (as shim/mu_hp.c does). */
#define HPM_COMMAND           0xFAu
#define HPM_OP_GET_SETTINGS   0x02u
#define HPM_OP_SET_SETTING    0x03u
#define HPM_OP_CPU_WORD       0x06u
#define HPM_SETTINGS_SIZE     24
#define HPM_SET_PLAYLIST_TYPE 0
#define HPM_SET_GLOBAL_ON     12
#define HPM_SET_GLOBAL_MENUS  13
#define HPM_SET_LEGAL_VARIANT 14
#define HPM_SET_STAGE_PAGE    20
#define HPM_SET_TRAINING      21
#define HPM_TE_COMMAND        0xF8u

int mu_online_active(void);
int mu_online_pending(void);
int mu_slippi_in_online_mode(void);
int mu_replay_abi_active(void);
int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);

/* The row whose callback the engine is running (if/textlib_1.c, set before each call). */
extern struct un_80304138_objalloc_t_x8* un_804D6E48;

enum {
    HPM_REPLY = 4096,   /* the bridge refuses a smaller reply buffer */
    HPM_ROWS = 20,      /* rows per page at most, not counting the end row */
};

/* What a row is. Every choice row is a list of names (engine type 2). */
enum {
    K_END,
    K_HEAD,         /* the page's title in the pack's brackets (engine type 0) */
    K_TEXT,         /* a line of information (engine type 0) */
    K_PAGE,         /* opens page `arg` (engine type 1) */
    K_TE1,          /* a 20XX TE feature bit `bits` in the first word */
    K_TE2,          /* a 20XX TE feature bit `bits` in the second word */
    K_FLASH_WHITE,  /* the L-cancel flash on a successful L-cancel (TE's flash choices, word 2) */
    K_FLASH_RED,    /* the L-cancel flash on a missed one */
    K_SET,          /* the settings byte `arg` */
    K_TRAIN,        /* the bit `arg` of the settings byte MU_HP_SET_TRAINING */
    K_CPU,          /* the field `bits` (shift `arg`) of option word 3 */
};

typedef struct HpmRow {
    unsigned char kind;
    unsigned char count;   /* how many names */
    unsigned short arg;
    unsigned int bits;
    const char* label;
    char** names;
} HpmRow;

enum {
    P_ROOT, P_GENERAL, P_TRAINING, P_TOGGLES, P_SAVESTATES, P_BUBBLES, P_LCANCEL, P_OVERLAYS, P_STAGE,
    P_CPU, P_DI, P_TECH, P_MUSIC, P_PLAYLISTS, P_MECHANICS, P_TEXTURES, P_COUNT
};

static char* hpm_off_on[] = {"OFF", "ON"};
static char* hpm_pages_names[] = {"PAGE 1", "PAGE 2", "PAGE 3", "PAGE 4"};
static char* hpm_play_types[] = {
    "SINGLE SONG", "RANDOM - PLAYLIST", "RANDOM - VANILLA (GOOD) ONLY", "RANDOM - CUSTOM ONLY",
    "RANDOM - VANILLA AND CUSTOM",
};
static char* hpm_di_names[] = {"VANILLA", "NO DI", "RANDOM", "SURVIVAL"};
static char* hpm_sdi_names[] = {"VANILLA", "NONE", "RANDOM", "TOWARD", "AWAY", "UP", "DOWN"};
static char* hpm_tech_names[] = {"OFF", "TECH IN PLACE", "TECH ROLL FORWARD", "TECH ROLL BACKWARD", "MISS", "RANDOM"};
static char* hpm_getup_names[] = {"OFF", "STANDUP", "GETUP ROLL FORWARD", "GETUP ROLL BACKWARD", "GETUP ATTACK",
                                  "RANDOM"};
/* The pack's names for its legal stage variants (TEXTURES page), 0..14 a variant, 15 random. */
static char* hpm_bf_names[] = {
    "DEFAULT", "ART DECO", "MARIO PARTY", "MATRIX", "BRAWL", "AESTHETIC", "COMPLIMENTARY", "ICE PATH",
    "CHOZO RUINS", "GREAT BAY BATTLEFIELD", "SPOOKY PUMPKIN", "CAVE STORY, SAND ZONE", "COCONUT CREAM PIES",
    "ANIMELEE BATTLEFIELD", "DUEL ZONE", "RANDOM",
};
static char* hpm_fd_names[] = {
    "DEFAULT", "HIGH QUALITY", "UNDERTALE", "RAINBOW REDUX", "GIYGAS", "CRYSTAL FD", "BLUE GLASS FD",
    "CUSTOM 7", "BLUE", "AUTUMN", "HYPERBOLIC TIME CHAMBER", "ANIMELEE FD", "CAVE STORY, OUTER WALL",
    "CUSTOM 13", "CUSTOM 14", "RANDOM",
};
static char* hpm_ys_names[] = {
    "DEFAULT", "NEON NIGHT-TIME", "DEAL WITH IT", "BEACH STORY", "PAPER MARIO", "GREEN HILL ZONE", "WINTER",
    "AUTUMN SUNSET", "ROUTE 30", "SPOOKY STORY", "YOSHIS FORTUNE", "KANTO", "CAVE STORY, SACRED GROUND",
    "CAVE STORY, WINTER SNOW", "YOSHIS SURF", "RANDOM",
};
static char* hpm_fod_names[] = {
    "DEFAULT", "THE LOOKOUT", "NORTHERN LIGHTS", "ICY FOUNTAIN", "FFCC", "ANIMAL CROSSING",
    "AUTUMN ANIMAL CROSSING", "WINTER ANIMAL CROSSING", "SPACE ANIMAL CROSSING", "WITCHES BREW", "SPACE FoD",
    "BEACH FoD", "CAVE STORY, CORE", "LUCID DREAMS", "LUIGIS MANSION", "RANDOM",
};
static char* hpm_dl_names[] = {
    "DEFAULT", "STREETS OF RAGE", "TALES OF SYMPHONIA", "AUTUMN", "CHERRY BLOSSOM", "METAL GEAR SOLID",
    "RETURN TO DREAMLAND (NIGHT)", "LINK'S AWAKENING", "CAVE STORY, BALCONY", "PEACEFUL REST VALLEY", "WINTER",
    "GAMEBOY", "BEACH DREAMLAND", "CUSTOM 13", "CUSTOM 14", "RANDOM",
};
static char* hpm_ps_names[] = {
    "DEFAULT", "HIGH QUALITY", "GRAY MASTERBALL", "ORANGE v2", "REMASTERED", "SMASH COURT v2", "GLASS STADIUM",
    "CAVE STORY, EGG CORRIDOR", "20XX STADIUM", "ANIMELEE STADIUM", "POKEFLOATS STADIUM", "CUSTOM 11",
    "CUSTOM 12", "CUSTOM 13", "CUSTOM 14", "RANDOM",
};

#define ONOFF 2, 0   /* an OFF/ON row; `bits` follows (TE rows), `arg` stays 0 */
#define PAGE(p, label) {K_PAGE, 0, p, 0, label, 0}
#define HEAD(label) {K_HEAD, 0, 0, 0, label, 0}
#define TEXT(label) {K_TEXT, 0, 0, 0, label, 0}
#define END {K_END, 0, 0, 0, 0, 0}

static const HpmRow hpm_root[] = {
    HEAD("<20XX HACK PACK>"),
    TEXT("VERSION 5.0.2, NATIVE"),
    TEXT(""),
    PAGE(P_GENERAL, "GENERAL CODES >"),
    PAGE(P_TRAINING, "TRAINING CODES >"),
    PAGE(P_STAGE, "STAGE MOD CODES >"),
    PAGE(P_CPU, "CPU CODES >"),
    PAGE(P_MUSIC, "MUSIC CODES >"),
    PAGE(P_MECHANICS, "MECHANICS CODES >"),
    PAGE(P_TEXTURES, "TEXTURES >"),
    TEXT(""),
    TEXT("B TO SAVE AND EXIT"),
    END,
};
static const HpmRow hpm_general[] = {
    HEAD("<GENERAL CODES>"),
    TEXT("NEUTRAL SPAWN POINTS : ALWAYS ON"),
    {K_TE2, ONOFF, MU_TE2_NO_SCREEN_RUMBLE, "DISABLE SCREEN RUMBLE :", hpm_off_on},
    {K_TE1, ONOFF, MU_TE_NO_STAR_KO, "DISABLE STAR KO :", hpm_off_on},
    {K_TE1, ONOFF, MU_TE_TAUNT_CANCEL, "TAUNT CANCELING :", hpm_off_on},
    TEXT(""),
    TEXT("ALL UNLOCKED, RULE DEFAULTS : ALWAYS ON"),
    END,
};
static const HpmRow hpm_training[] = {
    HEAD("<TRAINING CODES>"),
    PAGE(P_TOGGLES, "IN-GAME CODE TOGGLES >"),
    PAGE(P_LCANCEL, "L-CANCEL OPTIONS >"),
    PAGE(P_OVERLAYS, "COLOR OVERLAYS >"),
    END,
};
static const HpmRow hpm_toggles[] = {
    HEAD("<IN-GAME CODE TOGGLES>"),
    PAGE(P_SAVESTATES, "SAVE STATES/REPLAYS >"),
    PAGE(P_BUBBLES, "COLLISION BUBBLES >"),
    TEXT(""),
    {K_TE2, ONOFF, MU_TE2_INPUT_DISPLAY, "20XXTE INPUT DISPLAY :", hpm_off_on},
    {K_TE1, ONOFF, MU_TE_FIXED_CAMERA, "20XXTE FIXED CAMERA :", hpm_off_on},
    END,
};
static const HpmRow hpm_savestates[] = {
    HEAD("<SAVE STATES>"),
    {K_TRAIN, 2, MU_HP_TRAINING_ON, 0, "SAVE STATES AND REPLAYS :", hpm_off_on},
    TEXT("TRAINING MODE, D-PAD RIGHT SAVE, LEFT LOAD"),
    TEXT(""),
    HEAD("<REPLAY OPTIONS>"),
    TEXT("D-PAD DOWN RECORD, D-PAD UP PLAY BACK"),
    {K_TRAIN, 2, MU_HP_TRAINING_LOOP, 0, "LOOP :", hpm_off_on},
    END,
};
static const HpmRow hpm_bubbles[] = {
    HEAD("<COLLISION BUBBLE CODES>"),
    {K_TE2, ONOFF, MU_TE2_BUBBLES, "COLLISION BUBBLES :", hpm_off_on},
    TEXT("HITBOXES AND HURTBOXES OVER THE MODEL"),
    END,
};
static const HpmRow hpm_lcancel[] = {
    HEAD("<L-CANCEL OPTIONS>"),
    TEXT("<ALL PLAYERS>"),
    {K_FLASH_WHITE, ONOFF, 0, "FLASH WHITE ON SUCCESSFUL :", hpm_off_on},
    {K_FLASH_RED, ONOFF, 0, "FLASH RED ON UNSUCCESSFUL :", hpm_off_on},
    END,
};
static const HpmRow hpm_overlays[] = {
    HEAD("<CHARACTER COLOR OVERLAYS>"),
    {K_TE2, ONOFF, MU_TE2_COLOR_OVERLAYS, "ALL PLAYERS :", hpm_off_on},
    TEXT("GREEN ON EVERY FRAME A CHARACTER CAN ACT"),
    END,
};
static const HpmRow hpm_stage[] = {
    HEAD("<STAGE CODES>"),
    {K_SET, 4, HPM_SET_STAGE_PAGE, 0, "STAGE SELECT PAGE :", hpm_pages_names},
    TEXT("D-PAD UP/DOWN ON STAGE SELECT CHANGES IT"),
    TEXT(""),
    TEXT("FOUNTAIN OF DREAMS - LAGLESS : ALWAYS ON"),
    END,
};
static const HpmRow hpm_cpu[] = {
    HEAD("<CPU CODES>"),
    PAGE(P_DI, "DIRECTIONAL INFLUENCE >"),
    PAGE(P_TECH, "GROUND TECH OPTIONS >"),
    END,
};
static const HpmRow hpm_di[] = {
    HEAD("<DIRECTIONAL INFLUENCE CODES>"),
    {K_CPU, 4, 6, 0x000000C0u, "DIRECTIONAL INFLUENCE :", hpm_di_names},
    TEXT(""),
    {K_CPU, 7, 8, 0x00000700u, "SMASH DI :", hpm_sdi_names},
    END,
};
static const HpmRow hpm_tech[] = {
    HEAD("<TECH OPTIONS>"),
    {K_CPU, 6, 0, 0x00000007u, "CUSTOM TECH OPTIONS :", hpm_tech_names},
    TEXT(""),
    HEAD("<GETUP OPTIONS>"),
    {K_CPU, 6, 3, 0x00000038u, "CUSTOM GETUP OPTIONS :", hpm_getup_names},
    END,
};
static const HpmRow hpm_music[] = {
    HEAD("<MUSIC CODES>"),
    PAGE(P_PLAYLISTS, "CUSTOM MUSIC PLAYLISTS >"),
    END,
};
/* Settings bytes MU_HP_SET_PLAYLIST_TYPE + k, in the pack's slot order (its play type words at
 * 803FA940 + k * 4): menu, nine stages, global. Shown in the pack's page order. */
static const HpmRow hpm_playlists[] = {
    HEAD("<CUSTOM MUSIC PLAYLISTS>"),
    {K_SET, 5, HPM_SET_PLAYLIST_TYPE + 10, 0, "GLOBAL :", hpm_play_types},
    {K_SET, 2, HPM_SET_GLOBAL_ON, 0, "GLOBAL PLAYLIST :", hpm_off_on},
    {K_SET, 2, HPM_SET_GLOBAL_MENUS, 0, "GLOBAL AFFECTS MENU :", hpm_off_on},
    {K_SET, 5, HPM_SET_PLAYLIST_TYPE + 0, 0, "MENU :", hpm_play_types},
    {K_SET, 5, HPM_SET_PLAYLIST_TYPE + 1, 0, "YOSHIS STORY :", hpm_play_types},
    {K_SET, 5, HPM_SET_PLAYLIST_TYPE + 2, 0, "FOUNTAIN OF DREAMS :", hpm_play_types},
    {K_SET, 5, HPM_SET_PLAYLIST_TYPE + 3, 0, "POKEMON STADIUM :", hpm_play_types},
    {K_SET, 5, HPM_SET_PLAYLIST_TYPE + 4, 0, "DREAM LAND :", hpm_play_types},
    {K_SET, 5, HPM_SET_PLAYLIST_TYPE + 5, 0, "BATTLEFIELD :", hpm_play_types},
    {K_SET, 5, HPM_SET_PLAYLIST_TYPE + 6, 0, "FINAL DESTINATION :", hpm_play_types},
    {K_SET, 5, HPM_SET_PLAYLIST_TYPE + 7, 0, "GREEN GREENS :", hpm_play_types},
    {K_SET, 5, HPM_SET_PLAYLIST_TYPE + 8, 0, "TROPHY :", hpm_play_types},
    {K_SET, 5, HPM_SET_PLAYLIST_TYPE + 9, 0, "HYRULE TEMPLE/MAZE :", hpm_play_types},
    END,
};
static const HpmRow hpm_mechanics[] = {
    HEAD("<MECHANICS CODES>"),
    TEXT("UNIVERSAL CONTROLLER FIX : ALWAYS ON (UCF 0.84)"),
    END,
};
/* Settings bytes MU_HP_SET_LEGAL_VARIANT + n: Stadium, Dream Land, Fountain, Yoshi's Story, Final
 * Destination, Battlefield. Shown in the pack's order. */
static const HpmRow hpm_textures[] = {
    HEAD("<TEXTURE HACKS>"),
    {K_SET, 16, HPM_SET_LEGAL_VARIANT + 5, 0, "BATTLEFIELD :", hpm_bf_names},
    {K_SET, 16, HPM_SET_LEGAL_VARIANT + 4, 0, "FINAL DESTINATION :", hpm_fd_names},
    {K_SET, 16, HPM_SET_LEGAL_VARIANT + 3, 0, "YOSHIS STORY :", hpm_ys_names},
    {K_SET, 16, HPM_SET_LEGAL_VARIANT + 2, 0, "FOUNTAIN :", hpm_fod_names},
    {K_SET, 16, HPM_SET_LEGAL_VARIANT + 1, 0, "DREAM LAND :", hpm_dl_names},
    {K_SET, 16, HPM_SET_LEGAL_VARIANT + 0, 0, "POKEMON STADIUM :", hpm_ps_names},
    END,
};

#undef ONOFF
#undef PAGE
#undef HEAD
#undef TEXT
#undef END

static const HpmRow* const hpm_pages[P_COUNT] = {
    hpm_root, hpm_general, hpm_training, hpm_toggles, hpm_savestates, hpm_bubbles, hpm_lcancel, hpm_overlays,
    hpm_stage, hpm_cpu, hpm_di, hpm_tech, hpm_music, hpm_playlists, hpm_mechanics, hpm_textures,
};

/* What the menu was opened with, and the engine's tables built from it. */
static unsigned char hpm_reply[HPM_REPLY];
static unsigned char hpm_set[HPM_SETTINGS_SIZE];
static unsigned int hpm_w1, hpm_w2, hpm_cpu_word;
static int hpm_te_ok, hpm_cpu_ok, hpm_locked;
static struct un_80304138_objalloc_t_x8 hpm_items[P_COUNT][HPM_ROWS + 1];
static int hpm_values[P_COUNT][HPM_ROWS];
static char hpm_text[P_COUNT][HPM_ROWS][64];
static int hpm_training_cache = -1;   /* MU_HP_SET_TRAINING as last read or saved, -1 not read yet */

static unsigned int hpm_be32(const unsigned char* p)
{
    return (unsigned int) p[0] << 24 | (unsigned int) p[1] << 16 | (unsigned int) p[2] << 8 | p[3];
}

static void hpm_put_be32(unsigned char* p, unsigned int v)
{
    p[0] = (unsigned char) (v >> 24);
    p[1] = (unsigned char) (v >> 16);
    p[2] = (unsigned char) (v >> 8);
    p[3] = (unsigned char) v;
}

/* The pack is loaded, this is the normal game, offline, not replay playback. */
static int hpm_live(void)
{
    return mu_hp_loaded() && !(mu_game_options() & MU_OPTION_VANILLA) && !mu_online_active() &&
           !mu_online_pending() && !mu_slippi_in_online_mode() && !mu_replay_abi_active();
}

static unsigned int hpm_command(unsigned int command, const unsigned char* payload, unsigned int size)
{
    unsigned int got = 0;
    if (mu_online_abi_command(command, payload, size, hpm_reply, sizeof hpm_reply, &got) != 0) {
        return 0;
    }
    return got;
}

static int hpm_read_settings(unsigned char* out)
{
    const unsigned char op = HPM_OP_GET_SETTINGS;
    if (hpm_command(HPM_COMMAND, &op, 1) != HPM_SETTINGS_SIZE) {
        return 0;
    }
    memcpy(out, hpm_reply, HPM_SETTINGS_SIZE);
    return 1;
}

int mu_hp_training(unsigned int bit)
{
    if (!hpm_live()) {
        return 0;
    }
    if (hpm_training_cache < 0) {
        unsigned char set[HPM_SETTINGS_SIZE];
        if (!hpm_read_settings(set)) {
            return 0;
        }
        hpm_training_cache = set[HPM_SET_TRAINING];
    }
    return (hpm_training_cache & bit) != 0;
}

static int hpm_flash_white(unsigned int w2)
{
    const int mode = mu_lcancel_flash_mode(w2, 0);
    return (mode == MU_LCFLASH_TE_SUCCESS || mode == MU_LCFLASH_TE_BOTH) &&
           mu_lcancel_success_color(w2) != MU_LCFLASH_SUCCESS_OFF;
}

static int hpm_flash_red(unsigned int w2)
{
    const int mode = mu_lcancel_flash_mode(w2, 0);
    return mode == MU_LCFLASH_TE_MISSED || mode == MU_LCFLASH_TE_BOTH;
}

static int hpm_read(const HpmRow* row)
{
    switch (row->kind) {
    case K_TE1:
        return (hpm_w1 & row->bits) != 0;
    case K_TE2:
        return (hpm_w2 & row->bits) != 0;
    case K_FLASH_WHITE:
        return hpm_flash_white(hpm_w2);
    case K_FLASH_RED:
        return hpm_flash_red(hpm_w2);
    case K_SET:
        return hpm_set[row->arg];
    case K_TRAIN:
        return (hpm_set[HPM_SET_TRAINING] & row->arg) != 0;
    case K_CPU:
        return (int) ((hpm_cpu_word & row->bits) >> row->arg);
    default:
        return 0;
    }
}

/* The row's store answered when the menu opened. */
static int hpm_available(const HpmRow* row)
{
    switch (row->kind) {
    case K_TE1:
    case K_TE2:
    case K_FLASH_WHITE:
    case K_FLASH_RED:
        return hpm_te_ok;
    case K_CPU:
        return hpm_cpu_ok;
    default:
        return 1;
    }
}

/* The row can change: its store answered, and 20XX TE's "Lock settings" is off for TE's rows. */
static int hpm_editable(const HpmRow* row)
{
    if (!hpm_available(row)) {
        return 0;
    }
    if (hpm_locked && (row->kind == K_TE1 || row->kind == K_TE2 || row->kind == K_FLASH_WHITE ||
                       row->kind == K_FLASH_RED)) {
        return 0;
    }
    return 1;
}

/* "LABEL : VALUE" for a row shown but not changed here. */
static void hpm_compose(char* dst, const char* label, const char* value)
{
    int at = 0;
    while (*label && at < 50) {
        dst[at++] = *label++;
    }
    dst[at++] = ' ';
    while (*value && at < 62) {
        dst[at++] = *value++;
    }
    dst[at] = '\0';
}

static bool hpm_open(enum soundtest_callback_arg0 event);

static void hpm_build(void)
{
    int p, i;
    for (p = 0; p < P_COUNT; p++) {
        const HpmRow* rows = hpm_pages[p];
        for (i = 0; i < HPM_ROWS && rows[i].kind != K_END; i++) {
            const HpmRow* row = &rows[i];
            struct un_80304138_objalloc_t_x8* item = &hpm_items[p][i];
            int value;
            item->x4 = NULL;
            item->x8 = (char*) row->label;
            item->xC = NULL;
            item->x10 = NULL;
            item->x14 = item->x18 = item->x1C = 0.0f;
            switch (row->kind) {
            case K_HEAD:
            case K_TEXT:
                item->x0 = 0;   /* a line of text, not selectable */
                break;
            case K_PAGE:
                item->x0 = 1;   /* selectable; A runs x4 */
                item->x4 = hpm_open;
                break;
            default:
                value = hpm_read(row);
                if (value < 0 || value >= row->count) {
                    value = 0;
                }
                hpm_values[p][i] = value;
                if (hpm_editable(row)) {
                    item->x0 = 2;   /* a choice from xC, the value at x10, x14..x18 its range */
                    item->xC = row->names;
                    item->x10 = &hpm_values[p][i];
                    item->x18 = (float) row->count;
                } else {
                    /* Type 1 is a label: selectable, but neither left nor right edits it, and only
                     * type 2 draws xC[x10], so the value goes into the label (as TE's locked rows). */
                    hpm_compose(hpm_text[p][i], row->label,
                                hpm_available(row) ? row->names[value] : "(NOT AVAILABLE)");
                    item->x0 = 1;
                    item->x8 = hpm_text[p][i];
                }
                break;
            }
        }
        hpm_items[p][i].x0 = 9;   /* end */
        hpm_items[p][i].x4 = NULL;
        hpm_items[p][i].x8 = NULL;
    }
}

/* The page a K_PAGE row opens, from the row the engine is running. */
static int hpm_target(const struct un_80304138_objalloc_t_x8* item)
{
    int p;
    for (p = 0; p < P_COUNT; p++) {
        if (item >= hpm_items[p] && item < hpm_items[p] + HPM_ROWS + 1) {
            const HpmRow* row = &hpm_pages[p][item - hpm_items[p]];
            return row->kind == K_PAGE && row->arg < P_COUNT ? row->arg : -1;
        }
    }
    return -1;
}

/* B on a page: back to the page that opened it. */
static bool hpm_page_back(enum soundtest_callback_arg0 event)
{
    struct un_80304138_objalloc_t* page = un_80302DF0();
    if (event != 0 || page == NULL || page->next == NULL) {
        return false;
    }
    lbAudioAx_80024030(0);   /* the menu's back sound */
    page->next->x1 &= ~0x20;   /* show the page under it again */
    un_80304334(page);         /* the engine removes it at the end of the frame */
    return true;
}

/* A on a "PAGE >" row: the page opens at the same place, the one under it hides until B. */
static bool hpm_open(enum soundtest_callback_arg0 event)
{
    struct un_80304138_objalloc_t* cur = un_80302DF0();
    struct un_80304138_objalloc_t* page;
    int target;
    if (event != 1 || cur == NULL || cur->x4 == NULL) {
        return false;
    }
    target = hpm_target(un_804D6E48);
    if (target < 0) {
        return false;
    }
    lbAudioAx_80024030(1);   /* the menu's forward sound */
    page = un_80304210(cur, hpm_items[target], 0, -(int) (cur->x4->scale_x * (float) cur->x4->w),
                       -(int) (cur->x4->scale_y * (float) cur->x0));
    if (page != NULL) {
        page->xC = hpm_page_back;
        cur->x1 |= 0x20;   /* hidden, and no input, while the page is open */
    }
    return true;
}

void* mu_hp_debug_menu(void)
{
    unsigned int got;
    unsigned char op;
    if (!hpm_live() || !hpm_read_settings(hpm_set)) {
        return NULL;
    }
    got = hpm_command(HPM_TE_COMMAND, NULL, 0);
    hpm_te_ok = got == 8;
    hpm_w1 = hpm_w2 = 0;
    if (hpm_te_ok) {
        hpm_w1 = hpm_be32(hpm_reply);
        hpm_w2 = hpm_be32(hpm_reply + 4);
    }
    /* With a TE save and TE's "Lock settings" on, the host keeps TE's words as they are. */
    hpm_locked = hpm_te_ok && (mu_game_options() & MU_OPTION_TE) && (hpm_w2 & MU_TE2_LOCK);
    op = HPM_OP_CPU_WORD;
    got = hpm_command(HPM_COMMAND, &op, 1);
    hpm_cpu_ok = got == 4;
    hpm_cpu_word = 0;
    if (hpm_cpu_ok) {
        hpm_cpu_word = hpm_be32(hpm_reply);
    }
    hpm_build();
    OSReport("[20xx-hp] debug menu: settings ok, TE words %s%s, CPU word %s\n", hpm_te_ok ? "ok" : "missing",
             hpm_locked ? " (locked)" : "", hpm_cpu_ok ? "ok" : "missing");
    return hpm_items[P_ROOT];
}

void mu_hp_debug_menu_save(void)
{
    unsigned char set[HPM_SETTINGS_SIZE];
    unsigned char payload[8];
    unsigned int w1 = hpm_w1, w2 = hpm_w2, cpu = hpm_cpu_word;
    int white = hpm_flash_white(hpm_w2), red = hpm_flash_red(hpm_w2);
    int p, i, sent = 0;

    if (!hpm_live()) {
        return;
    }
    memcpy(set, hpm_set, sizeof set);
    for (p = 0; p < P_COUNT; p++) {
        const HpmRow* rows = hpm_pages[p];
        for (i = 0; i < HPM_ROWS && rows[i].kind != K_END; i++) {
            const HpmRow* row = &rows[i];
            const int v = hpm_values[p][i];
            if (row->kind < K_TE1 || !hpm_editable(row)) {
                continue;
            }
            switch (row->kind) {
            case K_TE1:
                w1 = v ? (w1 | row->bits) : (w1 & ~row->bits);
                break;
            case K_TE2:
                w2 = v ? (w2 | row->bits) : (w2 & ~row->bits);
                break;
            case K_FLASH_WHITE:
                white = v;
                break;
            case K_FLASH_RED:
                red = v;
                break;
            case K_SET:
                set[row->arg] = (unsigned char) v;
                break;
            case K_TRAIN:
                set[HPM_SET_TRAINING] = (unsigned char) (v ? (set[HPM_SET_TRAINING] | row->arg)
                                                           : (set[HPM_SET_TRAINING] & ~row->arg));
                break;
            case K_CPU:
                cpu = (cpu & ~row->bits) | (((unsigned int) v << row->arg) & row->bits);
                break;
            }
        }
    }
    if (white != hpm_flash_white(hpm_w2) || red != hpm_flash_red(hpm_w2)) {
        w2 = mu_lcancel_with_flash_mode(w2, white && red ? MU_LCFLASH_TE_BOTH
                                            : white      ? MU_LCFLASH_TE_SUCCESS
                                            : red        ? MU_LCFLASH_TE_MISSED
                                                         : MU_LCFLASH_OFF);
        if (white && mu_lcancel_success_color(w2) == MU_LCFLASH_SUCCESS_OFF) {
            w2 = mu_lcancel_with_success_color(w2, MU_LCFLASH_SUCCESS_WHITE);   /* the pack's flash is white */
        }
    }
    for (i = 0; i < HPM_SETTINGS_SIZE; i++) {
        if (set[i] != hpm_set[i]) {
            payload[0] = HPM_OP_SET_SETTING;
            payload[1] = (unsigned char) i;
            payload[2] = set[i];
            hpm_command(HPM_COMMAND, payload, 3);
            sent++;
        }
    }
    if (hpm_te_ok && !hpm_locked && (w1 != hpm_w1 || w2 != hpm_w2)) {
        hpm_put_be32(payload, w1);
        hpm_put_be32(payload + 4, w2);
        hpm_command(HPM_TE_COMMAND, payload, 8);
        sent++;
    }
    if (hpm_cpu_ok && cpu != hpm_cpu_word) {
        payload[0] = HPM_OP_CPU_WORD;
        hpm_put_be32(payload + 1, cpu);
        hpm_command(HPM_COMMAND, payload, 5);
        sent++;
    }
    /* What the host now has, read back for the lab's switches. */
    hpm_training_cache = -1;
    OSReport("[20xx-hp] debug menu saved: %d change(s), TE %08X %08X, CPU %08X\n", sent, w1, w2, cpu);
}
