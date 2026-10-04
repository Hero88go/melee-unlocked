/* 20XX Hack Pack content features, as native C: milestones 1 to 3 of
 * run-source/rel09-hackpack/PLAN.md section 3. None of the pack's code runs.
 *
 * State of this file: milestones 1 to 3. mu_hp_loaded() is live (lb/lbheap.c sizes one heap by it).
 * The stage functions are the native rewrite of the pack's stage mods (one row per mod in
 * run-source/rel09-hackpack/ledger_M2.md), the music functions of its music mods (ledger_M3.md).
 *
 * Music, how the pack does it and what is kept here:
 *   - The pack renamed most songs to numbered tracks (/audio/00.hps to CE.hps), as strings in its
 *     main.dol behind lbAudioAx_80023F28's name table (803BC314, song ids 0 to 0x62). The host
 *     reads those names (MU_HP_OP_DOL_READ) once per boot.
 *   - Eleven of those strings are playlist slots (pointers at 803FA8E0): the main menu song, nine
 *     stage songs, and testnz (0x62), the global playlist. On each call of lbAudioAx_80023F28 the
 *     pack draws a track for each slot from its playlist (803FA9B0, 0x60 bytes each) by the slot's
 *     playlist type and writes the track's two hex digits into the string. Here a copy of the names
 *     is patched instead. The types, the custom track count and the two global switches are the
 *     host's settings (MU_HP_SET_PLAYLIST_TYPE to MU_HP_SET_GLOBAL_MENUS).
 *   - A stage music id with 0x10000 set is a hex track: track (id & 0xFFFF) under testnz's name.
 *   - Draws use a private copy of the game's generator, never HSD_Randi (see hp_rand).
 *
 * Stages, how the pack does it and what is kept here:
 *   - The Stage Swap Engine (8025BB40) runs when a stage is picked. It reads the stage swap table's
 *     row for the picked stage and the page the stage select screen is on, may replace the stage,
 *     may write one variant letter into a stage file name inside its main.dol (the name stays
 *     patched until the match is left, 801A4160), and stores the row's custom flag byte at 803FA2E5,
 *     which the per-stage hooks test. Here the pick is kept in hp_pick (stage, which name, which
 *     letter where, flag byte) and nothing is patched: mu_hp_stage_file() builds the name when the
 *     game asks for the stage file, and mu_hp_stage_flags() returns the byte.
 *   - The pack's default stage file names (its "Default File Name Changes", GrNBa.0at, GrP0 and so
 *     on) are data in its main.dol; the host reads them by console address (MU_HP_OP_DOL_READ) from
 *     the pack's stage data table at 803DFEDC (one pointer per GrKind, the name pointer at +8).
 *   - The stage swap table is the pack's StageSwapTable.bin, from the host (MU_HP_OP_STAGE_TABLE),
 *     once per boot. Layout, from the engine's code (the pack loads the file to 803FBC80 and indexes
 *     803FBC20 + StKind * 0x30, so row 0 is StKind 2, Fountain of Dreams): 0x30 bytes per StKind,
 *       +0x00  8 bytes   a label (ASCII, unused by the code)
 *       +0x08  4 bytes   per page: the StKind to play instead, 0 none (0x1A: a target test stage,
 *                        0x21 plus the word at 8040A280; 0x15: StKind 0)
 *       +0x0C  4 bytes   per page: the custom flag byte for the match
 *       +0x10  4 words   per page: console address of the one letter to replace in a stage file
 *                        name (the first letter after the '.', or index 4 of a name without one)
 *       +0x20  4 bytes   per page: that letter, 0 none, 0xFF one of the page's list at random
 *       +0x20  4 bytes   per page p (p >= 1): from +0x20 + p * 4, the random list, up to 4 letters
 *                        ending at the first 0 (the list of page p overlaps the letters above;
 *                        page 0 never draws, the engine goes to the default path instead)
 *     A pick with no letter goes the default path: for the six legal stages the player's variant
 *     choice (MU_HP_SET_LEGAL_VARIANT: 0..14 a letter, 15 random from 0..14), checked against the
 *     disc, '0' when the file is missing, and never 'D' on Battlefield when the flag byte's top two
 *     bits are set.
 *   - Pages: D-pad down and up on the stage select screen step through four pages (8025BAFC), and
 *     the screen loads again on the new page with that page's icons (MnSlMap.1sd to .4sd). The page
 *     is saved by the host (MU_HP_SET_STAGE_PAGE).
 *   - The match's state is sent to the host at match start (MU_HP_OP_STAGE_STATE) and rides in
 *     option word 3, so a replay records it; playback takes the letter and the flag byte from the
 *     replay's word instead of the pick.
 *
 * Offline only. Online, while online is pending, in Slippi's online menus, and with the pack not
 * loaded, every function here leaves the game exactly as it is. In replay playback only the
 * replay's own word counts (a replay recorded without the pack has none). */
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbfile.h>
#include <sysdolphin/baselib/random.h>

#include "mu_hp.h"

/* The host ABI's values this file uses (port/runtime/abi/mu_host.h). That header brings in the C
 * library's types, which clash with the game's, so game-side files keep their own copy. */
#define MU_MOD_HACKPACK                      0x2u
#define MU_HP_COMMAND                        0xFAu
#define MU_HP_OP_DOL_READ                    0x01u
#define MU_HP_OP_GET_SETTINGS                0x02u
#define MU_HP_OP_SET_SETTING                 0x03u
#define MU_HP_OP_STAGE_STATE                 0x04u
#define MU_HP_OP_STAGE_TABLE                 0x05u
#define MU_HP_SETTINGS_SIZE                  24u
#define MU_HP_SET_LEGAL_VARIANT              14u
#define MU_HP_SET_STAGE_PAGE                 20u
#define MU_GAME_OPTION3_HP_STAGE_ON          0x00008000u
#define MU_GAME_OPTION3_HP_STAGE_CHAR_MASK   0x003F0000u
#define MU_GAME_OPTION3_HP_STAGE_CHAR_SHIFT  16
#define MU_GAME_OPTION3_HP_STAGE_FLAGS_MASK  0x3FC00000u
#define MU_GAME_OPTION3_HP_STAGE_FLAGS_SHIFT 22

#include <string.h>

unsigned int mu_mod_flags(void);       /* mu_entry.c */
unsigned int mu_game_options3(void);   /* mu_entry.c */
int mu_online_active(void);
int mu_online_pending(void);
int mu_slippi_in_online_mode(void);
int mu_replay_abi_active(void);
int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);
char* getenv(const char* name);

int mu_hp_loaded(void)
{
    return (mu_mod_flags() & MU_MOD_HACKPACK) != 0;
}

/* ---- stages (milestone 2) ---- */

enum {
    HP_GRKINDS = 0x47,          /* the pack's stage data table, GrKind 0 to 0x46 */
    HP_NAME_MAX = 20,
    HP_ROW = 0x30,              /* one StageSwapTable.bin row */
    HP_TABLE_MAX = 0x1000,
    HP_REPLY = 4096,            /* the bridge refuses a smaller reply buffer */
};

#define HP_STAGE_DATAS 0x803DFEDCu   /* the pack's stage data pointers, by GrKind */
#define HP_TARGET_STAGE 0x8040A280u  /* debug menu word: which target test stage StKind 0x1A means */

static unsigned char hp_reply[HP_REPLY];

/* The pick of the stage select screen (or of a scripted run), for the match it starts. */
static struct {
    int valid;
    int stkind;                 /* the StKind the match plays */
    unsigned int name_addr;     /* console address of the stage file name the letter goes into */
    int pos;                    /* the letter's index in that name */
    char letter;                /* 0 none, else lower case ('0'..'9', 'a'..'z') */
    unsigned char flags;        /* the custom flag byte */
} hp_pick;

/* The running match: the pack's names in use, and the flag byte (Stadium sets 0x80 while playing). */
static int hp_on;
static int hp_flags;

static unsigned int hp_be32(const unsigned char* p)
{
    return (unsigned int) p[0] << 24 | (unsigned int) p[1] << 16 | (unsigned int) p[2] << 8 | p[3];
}

/* The pack is loaded and its stage rules may change this game: offline, not in playback. */
static int hp_live(void)
{
    return mu_hp_loaded() && !mu_online_active() && !mu_online_pending() && !mu_slippi_in_online_mode() &&
           !mu_replay_abi_active();
}

static int hp_command(const unsigned char* payload, unsigned int size, unsigned int* got)
{
    *got = 0;
    return mu_online_abi_command(MU_HP_COMMAND, payload, size, hp_reply, sizeof hp_reply, got) == 0 && *got != 0;
}

/* Bytes of the pack's main.dol data at a console address. */
static int hp_dol_read(unsigned int address, void* dst, unsigned int size)
{
    unsigned char p[7];
    unsigned int got;
    p[0] = MU_HP_OP_DOL_READ;
    p[1] = (unsigned char) (address >> 24);
    p[2] = (unsigned char) (address >> 16);
    p[3] = (unsigned char) (address >> 8);
    p[4] = (unsigned char) address;
    p[5] = (unsigned char) (size >> 8);
    p[6] = (unsigned char) size;
    if (!hp_command(p, sizeof p, &got) || got != size) {
        return 0;
    }
    memcpy(dst, hp_reply, size);
    return 1;
}

static int hp_settings(unsigned char* out)
{
    const unsigned char op = MU_HP_OP_GET_SETTINGS;
    unsigned int got;
    /* A longer block is the same one with the training display bytes after it (mu_hp.h,
     * MU_HP_TRAIN_SETTINGS); this file uses the first 24 only. */
    if (!hp_command(&op, 1, &got) || got < MU_HP_SETTINGS_SIZE) {
        return 0;
    }
    memcpy(out, hp_reply, MU_HP_SETTINGS_SIZE);
    return 1;
}

/* StageSwapTable.bin, once per boot. */
static unsigned char hp_table[HP_TABLE_MAX];
static unsigned int hp_table_size;
static int hp_table_tried;

static int hp_load_table(void)
{
    if (!hp_table_tried) {
        const unsigned char op = MU_HP_OP_STAGE_TABLE;
        unsigned int got;
        hp_table_tried = 1;
        if (hp_command(&op, 1, &got) && got <= sizeof hp_table) {
            memcpy(hp_table, hp_reply, got);
            hp_table_size = got;
        }
        OSReport("[20xx-hp] stage swap table: %u bytes\n", hp_table_size);
    }
    return hp_table_size != 0;
}

static const unsigned char* hp_row(int stkind)
{
    if (stkind < 2 || !hp_load_table() || (unsigned int) (stkind - 1) * HP_ROW > hp_table_size) {
        return NULL;
    }
    return hp_table + (stkind - 2) * HP_ROW;
}

/* The pack's default stage file names, by GrKind, with their console addresses, once per boot. */
static char hp_names[HP_GRKINDS][HP_NAME_MAX];
static unsigned int hp_name_addrs[HP_GRKINDS];
static int hp_names_tried;

static void hp_load_names(void)
{
    unsigned char ptrs[HP_GRKINDS * 4];
    int k;
    if (hp_names_tried) {
        return;
    }
    hp_names_tried = 1;
    if (!hp_dol_read(HP_STAGE_DATAS, ptrs, sizeof ptrs)) {
        OSReport("[20xx-hp] stage file names: not readable\n");
        return;
    }
    for (k = 0; k < HP_GRKINDS; k++) {
        static const unsigned int sizes[] = { 16, 12, 8 };
        unsigned char word[4];
        char text[16];
        unsigned int data = hp_be32(ptrs + k * 4), name;
        int s;
        if (data == 0 || !hp_dol_read(data + 8, word, 4) || (name = hp_be32(word)) == 0) {
            continue;
        }
        /* A shorter read where the name sits near the end of its section. */
        for (s = 0; s < 3; s++) {
            memset(text, 0, sizeof text);
            if (hp_dol_read(name, text, sizes[s]) && memchr(text, 0, sizes[s]) != NULL) {
                memcpy(hp_names[k], text, sizes[s]);
                hp_name_addrs[k] = name;
                break;
            }
        }
    }
}

/* Where the variant letter goes in a stage file name: after the '.', or index 4 ("/GrP0"). */
static int hp_letter_pos(const char* name)
{
    const char* dot = strchr(name, '.');
    return dot != NULL ? (int) (dot - name) + 1 : 4;
}

static int hp_exists(const char* name)
{
    return DVDConvertPathToEntrynum(lbFileGetFullName(name)) >= 0;
}

/* Option word 3's letter code: 0 none, 1..10 '0'..'9', 11..36 'a'..'z'. */
static unsigned int hp_letter_code(char c)
{
    if (c >= '0' && c <= '9') {
        return (unsigned int) (c - '0') + 1;
    }
    if (c >= 'a' && c <= 'z') {
        return (unsigned int) (c - 'a') + 11;
    }
    return 0;
}

static char hp_code_letter(unsigned int code)
{
    return code >= 1 && code <= 10 ? (char) ('0' + code - 1) : code >= 11 && code <= 36 ? (char) ('a' + code - 11) : 0;
}

static char hp_lower(int c)
{
    return (char) (c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
}

/* The stage the default path's legal variant choice is for: its index in MU_HP_SET_LEGAL_VARIANT
 * (the pack's words 8040A1D4 to 8040A1E8). */
static int hp_legal_index(int stkind)
{
    switch (stkind) {
    case 0x03: return 0;   /* Pokemon Stadium */
    case 0x1C: return 1;   /* Dream Land */
    case 0x02: return 2;   /* Fountain of Dreams */
    case 0x08: return 3;   /* Yoshi's Story */
    case 0x20: return 4;   /* Final Destination */
    case 0x1F: return 5;   /* Battlefield */
    default: return -1;
    }
}

/* The Stage Swap Engine's default path: the legal stage's variant from the player's choice, checked
 * against the disc. `v` is the choice, 0..14, 15 and up random. Returns the letter, 0 for none. */
static char hp_default_letter(int stkind, int v, char* name, int pos)
{
    const char keep = name[pos];
    int from = v;   /* the engine's r26: the choice the letter came from */
    int tries;
    char letter = 0;
    for (tries = 0; tries < 64; tries++) {
        if (from >= 15) {
            v = HSD_Randi(15);
        }
        letter = (char) (v < 10 ? '0' + v : 'A' + v - 10);
        name[pos] = letter;
        if (!hp_exists(name)) {
            /* missing: random draws again, a fixed choice falls back to '0' */
            if (from < 15) {
                if (v == 0) {
                    letter = 0;
                    break;
                }
                from = v = 0;
            }
            continue;
        }
        if (stkind == 0x1F && ((hp_pick.flags >> 6) & 3) != 0 && letter == 'D') {
            /* Battlefield with a custom platform flag never takes the 'D' file */
            from = v = 0;
            continue;
        }
        break;
    }
    if (tries == 64) {
        letter = 0;
    }
    name[pos] = keep;
    return hp_lower(letter);
}

/* The Stage Swap Engine for one pick on one page. Returns the StKind to play. */
static int hp_do_pick(int stkind, int page, const unsigned char* set)
{
    const unsigned char* row = hp_row(stkind);
    int st = stkind;
    unsigned int ptr = 0;
    int k;

    memset(&hp_pick, 0, sizeof hp_pick);
    hp_pick.valid = 1;
    hp_load_names();
    if (row != NULL) {
        hp_pick.flags = row[0x0C + page];
        if (row[0x08 + page] != 0) {
            st = row[0x08 + page];
            if (st == 0x1A) {
                unsigned char word[4];
                const unsigned int target = hp_dol_read(HP_TARGET_STAGE, word, 4) ? hp_be32(word) : 0;
                st = 0x21 + (target < 0x20 ? (int) target : 0);
            } else if (st == 0x15) {
                st = 0;
            }
        }
        ptr = hp_be32(row + 0x10 + page * 4);
    }
    if (row != NULL && ptr != 0 && row[0x20 + page] != 0 && !(row[0x20 + page] == 0xFF && page == 0)) {
        int letter = row[0x20 + page];
        if (letter == 0xFF) {
            int n = 0;
            while (n < 4 && row[0x20 + page * 4 + n] != 0) {
                n++;
            }
            letter = n != 0 ? row[0x20 + page * 4 + HSD_Randi(n)] : 0;
        }
        /* The letter goes into the name the address falls in. */
        for (k = 0; k < HP_GRKINDS && letter != 0; k++) {
            const unsigned int a = hp_name_addrs[k];
            if (a != 0 && ptr >= a && ptr < a + strlen(hp_names[k])) {
                hp_pick.name_addr = a;
                hp_pick.pos = (int) (ptr - a);
                hp_pick.letter = hp_lower(letter);
                break;
            }
        }
        if (hp_pick.letter == 0 && letter != 0) {
            OSReport("[20xx-hp] swap table name address %08X is not a stage file name\n", ptr);
        }
    } else {
        const int legal = hp_legal_index(st);
        const int grkind = legal >= 0 ? (int) Stage_8022519C((StKind) st) : -1;
        if (legal >= 0 && grkind >= 0 && grkind < HP_GRKINDS && hp_names[grkind][0] != 0) {
            char name[HP_NAME_MAX];
            const int pos = hp_letter_pos(hp_names[grkind]);
            memcpy(name, hp_names[grkind], sizeof name);
            if (pos < HP_NAME_MAX - 1) {
                hp_pick.letter = hp_default_letter(st, set[MU_HP_SET_LEGAL_VARIANT + legal], name, pos);
                hp_pick.name_addr = hp_name_addrs[grkind];
                hp_pick.pos = pos;
            }
        }
    }
    hp_pick.stkind = st;
    OSReport("[20xx-hp] pick: stage %d page %d -> %d, letter '%c' at %08X+%d, flags %02X\n", stkind, page + 1, st,
             hp_pick.letter != 0 ? hp_pick.letter : '-', hp_pick.name_addr, hp_pick.pos, hp_pick.flags);
    return st;
}

int mu_hp_sss_pick(int stkind)
{
    unsigned char set[MU_HP_SETTINGS_SIZE];
    if (!hp_live() || !hp_settings(set)) {
        hp_pick.valid = 0;
        return stkind;
    }
    return hp_do_pick(stkind, set[MU_HP_SET_STAGE_PAGE] & 3, set);
}

int mu_hp_sss_input(unsigned int pressed)
{
    unsigned char set[MU_HP_SETTINGS_SIZE];
    unsigned char p[3];
    unsigned int got;
    int page, next;
    if (!(pressed & 0xC) || !hp_live() || !hp_settings(set)) {
        return 0;
    }
    page = set[MU_HP_SET_STAGE_PAGE] & 3;
    if (page != 3 && (pressed & 0x4)) {        /* D-pad down */
        next = page + 1;
    } else if (page != 0 && (pressed & 0x8)) { /* D-pad up */
        next = page - 1;
    } else {
        return 0;
    }
    p[0] = MU_HP_OP_SET_SETTING;
    p[1] = MU_HP_SET_STAGE_PAGE;
    p[2] = (unsigned char) next;
    if (!hp_command(p, sizeof p, &got) || got < MU_HP_SETTINGS_SIZE || hp_reply[MU_HP_SET_STAGE_PAGE] != next) {
        return 0;
    }
    OSReport("[20xx-hp] stage select page %d\n", next + 1);
    return 1;
}

const char* mu_hp_sss_file(void)
{
    static char name[] = "MnSlMap.1sd";
    unsigned char set[MU_HP_SETTINGS_SIZE];
    if (!hp_live() || !hp_settings(set)) {
        return NULL;
    }
    name[8] = (char) ('1' + (set[MU_HP_SET_STAGE_PAGE] & 3));
    return hp_exists(name) ? name : NULL;
}

const char* mu_hp_stage_file(int grkind, const char* name)
{
    static char out[HP_GRKINDS][HP_NAME_MAX];
    const int playback = mu_replay_abi_active();
    unsigned int word = 0;
    char letter = 0;
    int pos;
    if (grkind < 0 || grkind >= HP_GRKINDS || name == NULL || !mu_hp_loaded()) {
        return name;
    }
    if (playback) {
        word = mu_game_options3();
        if (!(word & MU_GAME_OPTION3_HP_STAGE_ON)) {
            return name;
        }
    } else if (!hp_live()) {
        return name;
    }
    hp_load_names();
    if (hp_names[grkind][0] == 0) {
        return name;
    }
    memcpy(out[grkind], hp_names[grkind], HP_NAME_MAX);
    pos = hp_letter_pos(out[grkind]);
    if (playback) {
        letter = hp_code_letter((word & MU_GAME_OPTION3_HP_STAGE_CHAR_MASK) >> MU_GAME_OPTION3_HP_STAGE_CHAR_SHIFT);
    } else if (hp_pick.valid && hp_pick.letter != 0 && hp_pick.name_addr == hp_name_addrs[grkind]) {
        letter = hp_pick.letter;
        pos = hp_pick.pos;
    }
    if (letter != 0 && pos < HP_NAME_MAX - 1 && out[grkind][pos] != 0) {
        out[grkind][pos] = letter;
        if (hp_exists(out[grkind])) {
            return out[grkind];
        }
        OSReport("[20xx-hp] %s is not on the disc, the stage's default file instead\n", out[grkind]);
        memcpy(out[grkind], hp_names[grkind], HP_NAME_MAX);
    }
    return hp_exists(out[grkind]) ? out[grkind] : name;
}

int mu_hp_stage_flags(void)
{
    return hp_flags;
}

void mu_hp_stage_flags_or(int bits)
{
    hp_flags |= bits & 0xFF;
}

int mu_hp_stage_on(void)
{
    return hp_on;
}

void mu_hp_match_begin(int stkind)
{
    hp_on = 0;
    hp_flags = 0;
    if (mu_replay_abi_active()) {
        /* playback: the state the replay was recorded with */
        const unsigned int word = mu_game_options3();
        if (mu_hp_loaded() && (word & MU_GAME_OPTION3_HP_STAGE_ON)) {
            hp_on = 1;
            hp_flags = (int) ((word & MU_GAME_OPTION3_HP_STAGE_FLAGS_MASK) >> MU_GAME_OPTION3_HP_STAGE_FLAGS_SHIFT);
        }
        return;
    }
    if (!hp_live()) {
        hp_pick.valid = 0;
        return;
    }
    if (hp_pick.valid && hp_pick.stkind != stkind) {
        hp_pick.valid = 0;   /* this match was not started by the pick (a mode with its own stage) */
    }
    hp_on = 1;
    hp_flags = hp_pick.valid ? hp_pick.flags : 0;
    {
        const unsigned int bits =
            MU_GAME_OPTION3_HP_STAGE_ON |
            (hp_letter_code(hp_pick.valid ? hp_pick.letter : 0) << MU_GAME_OPTION3_HP_STAGE_CHAR_SHIFT &
             MU_GAME_OPTION3_HP_STAGE_CHAR_MASK) |
            ((unsigned int) hp_flags << MU_GAME_OPTION3_HP_STAGE_FLAGS_SHIFT & MU_GAME_OPTION3_HP_STAGE_FLAGS_MASK);
        unsigned char p[5];
        unsigned int got;
        p[0] = MU_HP_OP_STAGE_STATE;
        p[1] = (unsigned char) (bits >> 24);
        p[2] = (unsigned char) (bits >> 16);
        p[3] = (unsigned char) (bits >> 8);
        p[4] = (unsigned char) bits;
        hp_command(p, sizeof p, &got);
        OSReport("[20xx-hp] match on stage %d: state %08X\n", stkind, bits);
    }
}

void mu_hp_stage_leave(void)
{
    /* The pack writes the saved letter back into its name and clears the flag byte here. */
    hp_pick.valid = 0;
    hp_on = 0;
    hp_flags = 0;
}

int mu_hp_match_stage(int stkind)
{
    static int page = -2;
    unsigned char set[MU_HP_SETTINGS_SIZE];
    if (page == -2) {
        const char* v = getenv("MELEE_TEST_HP_PAGE");
        page = v != NULL && v[0] >= '1' && v[0] <= '4' && v[1] == 0 ? v[0] - '1' : -1;
    }
    if (page < 0 || stkind < 0 || !hp_live()) {
        return stkind;
    }
    if (!hp_settings(set)) {
        memset(set, 0, sizeof set);
    }
    return hp_do_pick(stkind, page, set);
}

int mu_hp_stadium_no_transform(void)
{
    const int f = hp_flags;
    if (!(f & 0xF8)) {
        return 0;   /* the pack then tests its debug menu's Freeze Pokemon Stadium, not ported */
    }
    if (!(f & MU_HP_FLAG_OFF)) {
        return 0;   /* a fixed transformation that has not happened yet */
    }
    if (!(f & MU_HP_FLAG_FIXED)) {
        return 1;   /* transformations off */
    }
    return gm_GetFrameCount() != 0;   /* transformed: the fixed form stays */
}

int mu_hp_stadium_fixed(void)
{
    const int f = hp_flags;
    int r;
    if (f & 0x40) {
        r = 0;   /* fire */
    } else if (f & 0x20) {
        r = 1;   /* grass */
    } else if (f & 0x10) {
        r = 2;   /* rock */
    } else if (f & 0x08) {
        r = 3;   /* water */
    } else {
        return -1;
    }
    if (f < 0x80) {
        mu_hp_stage_flags_or(0x80);
    }
    return r;
}

int mu_hp_stadium_run_frozen(void)
{
    return (hp_flags & MU_HP_FLAG_FIXED) != 0 && gm_GetFrameCount() == 0;
}

const void* mu_hp_stadium_param(const void* param, unsigned int size)
{
    /* The engine's file patch (80018130) zeroes the first transformation's countdown (words +0
     * and +4) and the three phase lengths (+0x10, +0x14, +0x18) in the loaded stage file. The
     * file is left alone here and the stage reads a copy. */
    static unsigned int copy[0x100 / 4];
    if (param == NULL || !(hp_flags & MU_HP_FLAG_FIXED) || size > sizeof copy || size < 0x1C) {
        return param;
    }
    memcpy(copy, param, size);
    copy[0x00 / 4] = 0;
    copy[0x04 / 4] = 0;
    copy[0x10 / 4] = 0;
    copy[0x14 / 4] = 0;
    copy[0x18 / 4] = 0;
    return copy;
}

/* ---- music (milestone 3) ---- */

enum {
    HP_SONGS = 0x63,            /* song ids 0 to 0x62: the pack also plays 0x62 (testnz) */
    HP_SONG_NAME = 16,
    HP_SLOTS = 11,              /* the menu, the nine stage playlists, the global playlist */
    HP_LIST_WORDS = 0x60 / 4,   /* one playlist: song ids up to the first 0, then a label */
    HP_VANILLA_FIRST = 0x18,    /* types 2 and 4: the 0x19 original tracks from 0x18 */
    HP_VANILLA_COUNT = 0x19,
    HP_CUSTOM_FIRST = 0x31,     /* types 3 and 4: the custom tracks after them */
    HP_SONG_MENU = 0x34,        /* menu01, the main menu song (slot 0) */
    HP_SONG_MENU_ALT = 0x36,    /* menu3, its alternate */
    HP_SONG_YSTORY = 0x60,
    HP_SONG_TESTNZ = 0x62,      /* the global playlist's and the hex tracks' name (slot 10) */
    HP_SONG_NONE = 0x7FFF,      /* "no song" in the pack's stage music tables */
};

#define HP_SONG_NAMES   0x803BC314u   /* lbAudioAx_80023F28's name pointers, by song id */
#define HP_SLOT_NAMES   0x803FA8E0u   /* the 11 names the playlists write two digits into */
#define HP_PLAYLISTS    0x803FA9B0u   /* the 11 playlists, 0x60 bytes each */
#define HP_NO_SONG_NAME 0x803BC2F0u   /* song 0x60's name, which song 0x7FFF turns into 00.hps */

/* The settings bytes this section reads (MU_HP_SET_* in port/runtime/abi/mu_host.h). */
#define MU_HP_SET_PLAYLIST_TYPE 0u
#define MU_HP_SET_CUSTOM_SONGS  11u
#define MU_HP_SET_GLOBAL_ON     12u
#define MU_HP_SET_GLOBAL_MENUS  13u

/* The pack's names by song id, as its main.dol has them ("Audio File Name Changes"), with the
 * digits the playlists wrote since boot. The pack writes the digits into the strings in its DOL; a
 * song whose name pointer is the same string sees the same digits here. */
static char hp_song_name[HP_SONGS][HP_SONG_NAME];
static unsigned int hp_song_addr[HP_SONGS];
static unsigned int hp_slot_addr[HP_SLOTS];
static unsigned int hp_lists[HP_SLOTS * HP_LIST_WORDS];
static int hp_custom_on_disc;   /* numbered tracks from 0x31 on the disc, the pack's word at 800032F0 */
static int hp_music_tried;
static int hp_music_ok;
static int hp_reload = 1;       /* the pack's menu reload byte at 803C772A, 1 at boot */
static unsigned int hp_seed;
static int hp_seeded;

/* The pack's music runs offline with the pack loaded, and in playback of a replay recorded with
 * it. A music pick has no effect on the match (hp_rand below never touches the game's seed). */
static int hp_music_live(void)
{
    if (!mu_hp_loaded() || mu_online_active() || mu_online_pending() || mu_slippi_in_online_mode()) {
        return 0;
    }
    return !mu_replay_abi_active() || (mu_game_options3() & MU_GAME_OPTION3_HP_STAGE_ON) != 0;
}

/* The pack draws with HSD_Randi. Here the same generator runs on its own seed: a scene change can
 * come after the match seed is set (Stage_80225074), and a draw there must not move the match's
 * random numbers, in play or in a replay. */
static int hp_rand(int n)
{
    if (!hp_seeded) {
        hp_seed = (unsigned int) OSGetTick();
        hp_seeded = 1;
    }
    hp_seed = hp_seed * 214013u + 2531011u;
    return n <= 0 ? 0 : (int) ((hp_seed >> 16) * (unsigned int) n >> 16);
}

static char hp_hex_digit(int v)
{
    const int c = v + '0';
    return (char) (c >= ':' ? c + 7 : c);   /* as the pack converts: no range check */
}

static int hp_audio_exists(const char* name)
{
    char path[7 + HP_SONG_NAME];
    memcpy(path, "/audio/", 7);
    memcpy(path + 7, name, HP_SONG_NAME);
    path[sizeof path - 1] = 0;
    return DVDConvertPathToEntrynum(path) >= 0;
}

/* The names, the slot table and the playlists from the pack's main.dol, once per boot. */
static int hp_load_music(void)
{
    unsigned char buf[HP_SLOTS * HP_LIST_WORDS * 4];
    int k, named = 0;
    if (hp_music_tried) {
        return hp_music_ok;
    }
    hp_music_tried = 1;
    if (!hp_dol_read(HP_SONG_NAMES, buf, HP_SONGS * 4)) {
        OSReport("[20xx-hp] music names: not readable\n");
        return 0;
    }
    for (k = 0; k < HP_SONGS; k++) {
        static const unsigned int sizes[] = { 16, 12, 8 };
        const unsigned int name = hp_be32(buf + k * 4);
        char text[HP_SONG_NAME];
        int s;
        /* A shorter read where the name sits near the end of its section. */
        for (s = 0; name != 0 && s < 3; s++) {
            memset(text, 0, sizeof text);
            if (hp_dol_read(name, text, sizes[s]) && memchr(text, 0, sizes[s]) != NULL) {
                memcpy(hp_song_name[k], text, sizeof text);
                hp_song_addr[k] = name;
                named++;
                break;
            }
        }
    }
    if (!hp_dol_read(HP_SLOT_NAMES, buf, HP_SLOTS * 4)) {
        OSReport("[20xx-hp] music playlist slots: not readable\n");
        return 0;
    }
    for (k = 0; k < HP_SLOTS; k++) {
        hp_slot_addr[k] = hp_be32(buf + k * 4);
    }
    if (!hp_dol_read(HP_PLAYLISTS, buf, sizeof buf)) {
        OSReport("[20xx-hp] music playlists: not readable\n");
        return 0;
    }
    for (k = 0; k < HP_SLOTS * HP_LIST_WORDS; k++) {
        hp_lists[k] = hp_be32(buf + k * 4);
    }
    /* The pack counts its custom tracks from a song name table at boot; the disc's numbered files
     * from 0x31 up to the first missing one are the same tracks. */
    for (k = HP_CUSTOM_FIRST; k <= 0xFF; k++) {
        char name[HP_SONG_NAME] = "00.hps";
        name[0] = hp_hex_digit(k >> 4);
        name[1] = hp_hex_digit(k & 15);
        if (!hp_audio_exists(name)) {
            break;
        }
    }
    hp_custom_on_disc = k - HP_CUSTOM_FIRST;
    hp_music_ok = 1;
    OSReport("[20xx-hp] music: %d song names, slots %08X..%08X, %d custom tracks on the disc\n", named,
             hp_slot_addr[0], hp_slot_addr[HP_SLOTS - 1], hp_custom_on_disc);
    return 1;
}

/* Writes a track number as two hex digits into the name at a console address (every song id
 * whose name is that string). */
static void hp_song_digits(unsigned int addr, int v)
{
    const int hi = v / 16;
    int k;
    for (k = 0; k < HP_SONGS && addr != 0; k++) {
        if (hp_song_addr[k] == addr && hp_song_name[k][0] != 0 && hp_song_name[k][1] != 0) {
            hp_song_name[k][0] = hp_hex_digit(hi);
            hp_song_name[k][1] = hp_hex_digit(v - hi * 16);
        }
    }
}

/* One playlist slot draws its track by its playlist type (the settings: 0 the list's first track,
 * 1 random from the list, 2 random original, 3 random custom, 4 random of both). Returns 0 when
 * the slot has no name (the pack then stops). */
static int hp_draw(int k, const unsigned char* set)
{
    const unsigned int* list = hp_lists + k * HP_LIST_WORDS;
    int custom = set[MU_HP_SET_CUSTOM_SONGS];
    int v, n;
    if (hp_slot_addr[k] == 0) {
        return 0;
    }
    if (custom == 0 || custom > hp_custom_on_disc) {
        custom = hp_custom_on_disc;
    }
    switch (set[MU_HP_SET_PLAYLIST_TYPE + k]) {
    case 0:
        v = (int) list[0];
        break;
    case 1:
        /* the pack reads up to the first 0, past the list's own end if it has none */
        for (n = 0; k * HP_LIST_WORDS + n < HP_SLOTS * HP_LIST_WORDS && list[n] != 0; n++) {
        }
        v = (int) list[hp_rand(n)];
        break;
    case 2:
        v = HP_VANILLA_FIRST + hp_rand(HP_VANILLA_COUNT);
        break;
    case 3:
        v = HP_CUSTOM_FIRST + hp_rand(custom);
        break;
    default:
        v = HP_VANILLA_FIRST + hp_rand(HP_VANILLA_COUNT + custom);
        break;
    }
    if (v == 0) {
        v = 1;   /* NONE plays track 01 */
    }
    hp_song_digits(hp_slot_addr[k], v);
    return 1;
}

const char* mu_hp_music_file(int song)
{
    unsigned char set[MU_HP_SETTINGS_SIZE];
    int global, menus, play = song;
    if (song < 0 || !hp_music_live() || !hp_settings(set) || !hp_load_music()) {
        return NULL;
    }
    global = set[MU_HP_SET_GLOBAL_ON] != 0;
    menus = set[MU_HP_SET_GLOBAL_MENUS] != 0;
    if (song == HP_SONG_NONE) {
        /* no song: Yoshi's Story's name becomes 00.hps, and that plays */
        hp_song_digits(HP_NO_SONG_NAME, 0);
        play = HP_SONG_YSTORY;
    } else if ((song >> 16) != 0 && !global) {
        /* a hex track: the stage asks for track (song & 0xFFFF) under testnz's name */
        hp_song_digits(hp_song_addr[HP_SONG_TESTNZ], song & 0xFFFF);
        play = HP_SONG_TESTNZ;
    } else {
        int k, drawn = 1;
        /* Every call draws the nine stage slots again. The menu slot draws only after a match
         * started or the title screen ran (mu_hp_music_mark), and not when the global playlist
         * also covers the menus. */
        for (k = 0; k < HP_SLOTS - 1 && drawn; k++) {
            if (k == 0) {
                if ((global && menus) || !hp_reload) {
                    continue;
                }
                hp_reload = 0;
            }
            drawn = hp_draw(k, set);
        }
        if (drawn && song != HP_SONG_TESTNZ && global) {
            if (song != HP_SONG_MENU && song != HP_SONG_MENU_ALT) {
                play = HP_SONG_TESTNZ;
                hp_draw(HP_SLOTS - 1, set);
            } else if (menus) {
                play = HP_SONG_TESTNZ;
                if (hp_reload) {
                    hp_reload = 0;
                    hp_draw(HP_SLOTS - 1, set);
                }
            }
        }
    }
    if (play < 0 || play >= HP_SONGS || hp_song_name[play][0] == 0) {
        return NULL;
    }
    if (!hp_audio_exists(hp_song_name[play])) {
        OSReport("[20xx-hp] music %X: /audio/%s is not on the disc, the game's own song\n", song,
                 hp_song_name[play]);
        return NULL;
    }
    OSReport("[20xx-hp] music %X -> %s\n", song, hp_song_name[play]);
    return hp_song_name[play];
}

void mu_hp_music_mark(void)
{
    hp_reload = 1;
}

void mu_hp_music_dpad(unsigned int pressed)
{
    if ((pressed & 0x8) && gm_GetFrameCount() == 0 && hp_music_live()) {   /* D-pad up */
        OSReport("[20xx-hp] music reload, song %X\n", stage_info.x98);
        lbAudioAx_80023F28(stage_info.x98);
    }
}
