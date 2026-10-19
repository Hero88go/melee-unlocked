/* m-ex stages, native (run-source/rel09-stages/PLAN.md section 3 items 2 and 10; LAYER.md).
 *
 * MxDt describes every internal stage with a row in the retail StageData layout (MexTK GrDesc).
 * Its callback words, its map gobj table word and its collision table word are raw addresses of
 * the retail main.dol: an added stage that has no code of its own names the retail stage code it
 * runs by those addresses (Awakening Wood names Battlefield's callbacks, Baby Bowser's Castle
 * Battlefield's map gobj table with Sheik's target stage callbacks). This file never dereferences
 * one of them. It builds, once when MxDt.dat is read, a native StageData for each added row by
 * matching each word against the same word of MxDt rows 0 to 70 and taking the native retail row's
 * field (gr/ground.c stage_datas[], mp/mplib.c mpLib_803BF248[]). A retail row the mod changed
 * (checked against the retail main.dol by a checksum per row) never serves a match.
 *
 * A row is locked, logged once, never offered to a match, when any of: its file is not on the
 * disc, a word matches no retail row, a word is 0 or -1 (the stage file's grFunction fills it) and
 * no native descriptor (mu_mex_stage.h) fills it, it names joints, it names stage items, or its
 * descriptor is not marked ready. The retail rows 0 to 70 are never replaced: the game keeps its
 * native retail rows there in both views (PLAN.md 1.3). Nothing here is read in the retail view. */
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/it/types.h>
#include <melee/it/kinds/types.h>
#include <sysdolphin/baselib/debug.h>
#include <melee/lb/lbfile.h>

#include <string.h>

#include "mu_mex_stage.h"

/* ---- the native stage descriptors this build has (phase B) ----
 * With MU_GR_REGISTRY_GENERATED the list is mu_gr_registry.inc, written at configure time: one
 * MU_GR_STAGE(symbol) line per "const MuGrStage mu_gr_<name> = " line in the stage sources (the
 * CMake change is proposed in run-source/rel09-stages/LAYER.md). Without it the list is empty and
 * every row that needs its own code stays locked. */
#ifdef MU_GR_REGISTRY_GENERATED
#define MU_GR_STAGE(symbol) extern const MuGrStage symbol;
#include "mu_gr_registry.inc"
#undef MU_GR_STAGE
static const MuGrStage* const mex_gr_registry[] = {
#define MU_GR_STAGE(symbol) &symbol,
#include "mu_gr_registry.inc"
#undef MU_GR_STAGE
    NULL,
};
#else
static const MuGrStage* const mex_gr_registry[] = { NULL };
#endif

/* CRC-32 (zlib) of the retail main.dol row of each retail internal id: StageData words 1, 3 to 9,
 * 11, 12 (map gobj table, the seven callbacks, joints, joint count) and the collision table
 * address of mpLib_803BF248[id], each big-endian. 0: the retail table has no row (ids 23, 26).
 * Read from melee-102.iso with py -3.12; both Akaneia 1.0.1 and ACE 2.0.0 give the same values
 * for their MxDt rows 0 to 70. */
static const unsigned int mex_retail_crc[MU_MEX_STAGE_RETAIL] = {
    0xBCE7CA5Du, 0x6B0B95E6u, 0x76A0124Eu, 0xD351634Eu, 0x67FB57E3u, 0x060B2315u,
    0x4502941Du, 0x34ABCF10u, 0x3B1AD02Eu, 0x79B3F993u, 0xF1DC1F35u, 0xF9CAB273u,
    0x467C9EF2u, 0x2814E72Cu, 0xA858BE45u, 0x10C93A5Du, 0xB885CA30u, 0x7E47F59Du,
    0x9E066541u, 0x4A3A4A17u, 0xDA759BB1u, 0xE42E0063u, 0x53001180u, 0x00000000u,
    0xE5E63114u, 0x63387E99u, 0x00000000u, 0x444C4E97u, 0x52F6A8C2u, 0x4674627Au,
    0xF2C97A71u, 0xBEC98EA7u, 0x24634854u, 0x820F666Cu, 0xE3D4B920u, 0x1A0B1B86u,
    0x619971C3u, 0x2352760Eu, 0xB102B264u, 0x9CD003A1u, 0x53562C80u, 0x0D97520Du,
    0x3DE892F5u, 0xB2269D9Fu, 0x900D590Du, 0xF1D49AF8u, 0x651544C8u, 0x11BC2B8Cu,
    0x346E58E4u, 0x8C17C29Cu, 0xA7B80FA8u, 0x09729020u, 0x28ECD975u, 0xB5D8B381u,
    0xBC8A4F2Cu, 0x25C1D8A3u, 0xD7399C5Bu, 0xE9E5284Au, 0x344CF675u, 0xF174B372u,
    0xBC7067F8u, 0xB62E37E8u, 0x5C2F35B4u, 0x2E771A3Fu, 0x183B91C5u, 0xC5FEB817u,
    0x4AE769B1u, 0x300FEB81u, 0x5CCB468Du, 0x0F75AFF7u, 0x7696F2EDu,
};

/* The descriptor words a row's fields come from (StageData order). */
enum {
    W_GRKIND = 0, W_MAP = 1, W_FILE = 2, W_INIT = 3, W_DEMO = 4, W_LOAD = 5, W_START = 6,
    W_CB4 = 7, W_TOUCH = 8, W_SHADOW = 9, W_FLAGS2 = 10, W_JOINTS = 11, W_JOINT_COUNT = 12,
    W_COUNT = 13,
};

typedef struct MexStageRow {
    StageData data;
    void* coll;           /* the native mpLib_803BF248 table (mp/mplib.c), opaque here */
    const char* lock;     /* NULL: the row may be loaded */
    const MuGrStage* desc;   /* its native code, NULL for a data-only row */
    u8 built;
} MexStageRow;

/* ---- map gobj tables read past their end ----
 * A row may take its map gobj table from one retail stage and its code from another (Donkey Kong
 * Arcade: Sheik's target table, Battlefield's code). The code creates its own map gobj ids, and
 * Ground_GetStageGObj reads flags_b2 of the row's table at that id: past the end of a shorter table
 * (Battlefield creates id 6, Sheik's table has 4 rows; Dream Land 64 creates 7 and 8 over Green
 * Greens' 7; Yoshi's Island 64 creates 4 and 5 over Sheik's). The console reads the words that
 * follow the table in main.dol (its StageData, its file name string); natively that would read past
 * a static array. Such a row gets its own copy: the retail rows, then rows whose flag word is the
 * word the console reads there (callbacks NULL: no code reaches them through the table). The words
 * are from melee-102.iso main.dol (rel09-stages WORKPACKAGES.md, "map table overreads"); each
 * table's length was checked against the file name string that follows it. */
#define MEX_MAP_ROWS 16
#define MEX_MAP_COPIES 32
static const struct {
    u8 id, len;
    unsigned int tail[MEX_MAP_ROWS];
} mex_map_tails[] = {
    {  8, 12, { 0x803E1A30u, 0x801D852Cu, 0x803E1A10u, 0x6E207420u } },
    { 13,  7, { 0x803E7638u, 0x802134F8u, 0x00000000u, 0x6E207420u, 0x00000000u, 0x00000001u, 0x00000000u, 0x00000000u, 0x00000000u } },
    { 28,  9, { 0x803E6688u, 0x80210888u, 0x00000000u, 0x6E207420u, 0x00000000u, 0xFF010001u, 0x0100000Bu } },
    { 29,  6, { 0x803E6488u, 0x8020E828u, 0x00000000u, 0x6E207420u, 0x00000000u, 0x00000001u, 0x00030002u, 0x00000000u, 0x00000000u, 0x00000000u } },
    { 36,  7, { 0x803E7DA0u, 0x80219D58u, 0x00000000u, 0x6E207420u, 0x00000000u, 0x54544C45u, 0x73742E63u, 0x8021A918u, 0x8021A96Cu } },
    { 49,  4, { 0x803E8C70u, 0x802216C0u, 0x00000000u, 0x6E207420u, 0x00000000u, 0x80221AE4u, 0x80221BD4u, 0x80221B44u, 0x00000000u, 0x4C6B2E64u, 0x80221930u, 0x80221C00u } },
    { 57,  4, { 0x803E9270u, 0x80222EF4u, 0x00000000u, 0x6E207420u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xC0000000u, 0x00000000u, 0x803E9338u, 0x802231E0u } },
    { 59,  4, { 0x803E93F8u, 0x802235F8u, 0x00000000u, 0x6E207420u, 0x00000000u, 0x80223A1Cu, 0x80223B0Cu, 0x80223A7Cu, 0x00000000u, 0x536B2E64u, 0x80223868u, 0x80223B38u } },
    { 60,  4, { 0x803E94B8u, 0x802238DCu, 0x00000000u, 0x6E207420u, 0x00000000u, 0x80223D00u, 0x80223DF0u, 0x80223D60u, 0x00000000u, 0x59732E64u, 0x80223B4Cu, 0x80223E1Cu } },
    { 68,  3, { 0x803E6278u, 0x8020E054u, 0x00000000u, 0x6E207420u, 0x00000000u, 0x8020E3FCu, 0x8020E438u, 0x8020E494u, 0x2F477245u, 0x803E6364u, 0x8020E30Cu, 0x00000000u, 0x67657420u } },
    { 69,  3, { 0x803E6328u, 0x8020E2E8u, 0x00000000u, 0x6E207420u, 0x00000000u, 0x8020E690u, 0x8020E6CCu, 0x8020E728u, 0x2F477245u, 0x803E6414u, 0x8020E5A0u, 0x00000000u, 0x67657420u } },
};
static StageCallbacks mex_map_copies[MEX_MAP_COPIES][MEX_MAP_ROWS];
static int mex_map_copy_count;

/* The row's table when its code is not its table's own stage: a copy with the console's tail, or
 * the retail table itself when no tail is known (logged). */
static StageCallbacks* mex_map_with_tail(int retail_id, int id)
{
    StageCallbacks* src = mu_ground_retail_stage(retail_id)->callbacks;
    StageCallbacks* copy;
    int t, i;
    for (t = 0; t < (int) (sizeof mex_map_tails / sizeof mex_map_tails[0]); t++) {
        if (mex_map_tails[t].id == retail_id) {
            break;
        }
    }
    if (t == (int) (sizeof mex_map_tails / sizeof mex_map_tails[0]) || mex_map_copy_count >= MEX_MAP_COPIES) {
        OSReport("[mex] stage %d: the map gobj table of %d has no known tail; its own end stands\n", id,
                 retail_id);
        return src;
    }
    copy = mex_map_copies[mex_map_copy_count++];
    memset(copy, 0, sizeof(StageCallbacks) * MEX_MAP_ROWS);
    for (i = 0; i < MEX_MAP_ROWS; i++) {
        if (i < mex_map_tails[t].len) {
            copy[i] = src[i];
        } else {
            copy[i].flags = mex_map_tails[t].tail[i - mex_map_tails[t].len];
        }
    }
    return copy;
}

static MexStageRow mex_rows[MU_MEX_STAGE_MAX];
static int mex_row_count;   /* the disc's internal count, at most MU_MEX_STAGE_MAX */
/* MxDt rows 0 to 70 as matched: their words, their collision word, and whether they may serve. */
static unsigned int mex_retail_words[MU_MEX_STAGE_RETAIL][W_COUNT];
static unsigned int mex_retail_coll[MU_MEX_STAGE_RETAIL];
static u8 mex_retail_ok[MU_MEX_STAGE_RETAIL];

static unsigned int mex_crc32(unsigned int crc, const unsigned char* p, int n)
{
    int i, k;
    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

static unsigned int mex_row_crc(const unsigned int words[W_COUNT], unsigned int coll)
{
    static const int fields[10] = { W_MAP, W_INIT, W_DEMO, W_LOAD, W_START, W_CB4,
                                    W_TOUCH, W_SHADOW, W_JOINTS, W_JOINT_COUNT };
    unsigned int crc = 0xFFFFFFFFu;
    unsigned char be[4];
    int i;
    for (i = 0; i <= 10; i++) {
        const unsigned int v = i < 10 ? words[fields[i]] : coll;
        be[0] = (unsigned char) (v >> 24);
        be[1] = (unsigned char) (v >> 16);
        be[2] = (unsigned char) (v >> 8);
        be[3] = (unsigned char) v;
        crc = mex_crc32(crc, be, 4);
    }
    return ~crc;
}

/* The retail id whose MxDt word `field` is `value`, -1 when none. */
static int mex_match(int field, unsigned int value)
{
    int i;
    for (i = 0; i < MU_MEX_STAGE_RETAIL; i++) {
        if (mex_retail_ok[i] && mex_retail_words[i][field] == value) {
            return i;
        }
    }
    return -1;
}

static int mex_match_coll(unsigned int value)
{
    int i;
    for (i = 0; i < MU_MEX_STAGE_RETAIL; i++) {
        if (mex_retail_ok[i] && mex_retail_coll[i] == value && mu_mplib_retail_coll(i) != NULL) {
            return i;
        }
    }
    return -1;
}

static const MuGrStage* mex_descriptor(const char* file)
{
    int i;
    for (i = 0; mex_gr_registry[i] != NULL; i++) {
        if (mex_gr_registry[i]->file != NULL && strcmp(mex_gr_registry[i]->file, file) == 0) {
            return mex_gr_registry[i];
        }
    }
    return NULL;
}

/* ---- does a stage file export code? ----
 * Reads the HSD archive's header and its tail (relocation table end to file end: the public and
 * extern tables and their names), never the data, and looks for the public names m-ex loads code
 * from. 1 yes, 0 no, -1 unreadable. Synchronous, like lbFile_8001668C (the disc wait polls). */
static u8 mex_probe_buf[0x10000] __attribute__((aligned(32)));
static volatile int mex_probe_state;

static void mex_probe_done(s32 result, DVDFileInfo* info)
{
    (void) info;
    mex_probe_state = result < 0 ? -1 : 1;
}

static int mex_probe_read(DVDFileInfo* info, void* dst, s32 length, s32 offset)
{
    mex_probe_state = 0;
    if (!DVDReadAsyncPrio(info, dst, length, offset, mex_probe_done, 2)) {
        return 0;
    }
    while (mex_probe_state == 0) {
        mu_poll();
    }
    return mex_probe_state > 0;
}

static unsigned int mex_be32_at(const u8* p)
{
    return (unsigned int) p[0] << 24 | (unsigned int) p[1] << 16 | (unsigned int) p[2] << 8 | p[3];
}

static int mex_file_has_code(const char* file)
{
    static const char* const names[2] = { "grFunction", "itFunction" };
    DVDFileInfo info;
    unsigned int file_size, data_size, relocs, publics, externs, tail, length, strings, i;
    int result = -1;
    const s32 entry = DVDConvertPathToEntrynum(lbFileGetFullName(file));
    if (entry < 0 || !DVDFastOpen(entry, &info)) {
        return -1;
    }
    if (!mex_probe_read(&info, mex_probe_buf, 0x20, 0)) {
        goto done;
    }
    file_size = mex_be32_at(mex_probe_buf);
    data_size = mex_be32_at(mex_probe_buf + 4);
    relocs = mex_be32_at(mex_probe_buf + 8);
    publics = mex_be32_at(mex_probe_buf + 12);
    externs = mex_be32_at(mex_probe_buf + 16);
    if (file_size > info.length || data_size > file_size || relocs > file_size / 4 ||
        publics > 0x1000 || externs > 0x1000)
    {
        goto done;
    }
    tail = 0x20 + data_size + 4 * relocs;
    if (tail >= file_size || file_size - tail > sizeof mex_probe_buf ||
        8 * (publics + externs) > file_size - tail)
    {
        goto done;
    }
    length = file_size - tail;
    if (!mex_probe_read(&info, mex_probe_buf, (s32) length, (s32) tail)) {
        goto done;
    }
    strings = 8 * (publics + externs);
    result = 0;
    for (i = 0; i < publics && result == 0; i++) {
        const unsigned int at = strings + mex_be32_at(mex_probe_buf + 8 * i + 4);
        int n;
        for (n = 0; n < 2; n++) {
            const unsigned int size = (unsigned int) strlen(names[n]) + 1;
            if (at < length && length - at >= size && memcmp(mex_probe_buf + at, names[n], size) == 0) {
                result = 1;
            }
        }
    }
done:
    DVDClose(&info);
    return result;
}

static const char* const mex_field_name[W_COUNT] = {
    "grkind", "map gobj table", "file", "on_init", "on_demo_init", "on_load", "on_start",
    "callback4", "on_touch_line", "on_check_shadow_render", "flags2", "joints", "joint count",
};

/* Reads MxDt rows 0 to 70 and checks each against the retail main.dol. */
static void mex_read_retail_rows(void)
{
    int i, bad = 0;
    for (i = 0; i < MU_MEX_STAGE_RETAIL; i++) {
        const char* file = NULL;
        mex_retail_ok[i] = 0;
        if (!mu_mex_stage_desc(i, mex_retail_words[i], &file)) {
            continue;
        }
        mex_retail_coll[i] = mu_mex_stage_coll(i);
        if (mex_retail_crc[i] == 0 || mu_ground_retail_stage(i) == NULL) {
            continue;   /* no retail row there (ids 23, 26) */
        }
        if (mex_row_crc(mex_retail_words[i], mex_retail_coll[i]) != mex_retail_crc[i]) {
            OSReport("[mex] stage row %d differs from the retail game; no added stage may take its code\n", i);
            bad++;
            continue;
        }
        mex_retail_ok[i] = 1;
    }
    if (bad != 0) {
        OSReport("[mex] %d retail stage rows changed by the mod\n", bad);
    }
}

/* One added row. Returns NULL when it may be loaded, else why not. `from` gets the retail ids
 * whose map gobj table and on_init it takes (-1: its own code). */
static const char* mex_build_row(int id, MexStageRow* row, int from[2], unsigned int* bad_word)
{
    unsigned int w[W_COUNT];
    const char* file = NULL;
    const MuGrStage* desc;
    int src[W_COUNT];
    int k;
    StageData* sd = &row->data;

    from[0] = from[1] = -1;
    *bad_word = 0;
    if (!mu_mex_stage_desc(id, w, &file)) {
        return "MxDt has no descriptor row";
    }
    if (file == NULL || file[0] == '\0') {
        return "no stage file";
    }
    if (DVDConvertPathToEntrynum(lbFileGetFullName(file)) < 0) {
        return "its file is not on the disc";
    }
    if (w[W_JOINTS] != 0 || w[W_JOINT_COUNT] != 0) {
        return "names joints (not supported)";
    }
    desc = mex_descriptor(file);
    if (desc != NULL && !(desc->flags & MU_GR_READY)) {
        return "its native code is not ready";
    }
    /* Stage items: the console fills their logic tables from the file's itFunction block
     * (803D7070, called from grDatFiles_801C6038+90), one block per entry of the row's item list.
     * Here the descriptor's .items do, in the same order. */
    if (mu_mex_stage_item_count(id) > 0) {
        int i;
        if (desc == NULL || desc->items == NULL || desc->item_count < mu_mex_stage_item_count(id)) {
            return "names stage items and its native code supplies none";
        }
        for (i = 0; i < mu_mex_stage_item_count(id); i++) {
            const int kind = mu_mex_stage_item(id, i);
            if (kind < MU_GR_ITEM_FIRST || kind > MU_GR_ITEM_LAST) {
                return "names an item kind outside the stage item range";
            }
        }
    }
    row->desc = desc;
    for (k = W_MAP; k <= W_SHADOW; k++) {
        src[k] = -2;   /* the descriptor fills it */
        if (k == W_FILE) {
            continue;
        }
        switch (k) {
        case W_MAP: if (desc != NULL && desc->map_gobjs != NULL) continue; break;
        case W_INIT: if (desc != NULL && desc->on_init != NULL) continue; break;
        case W_DEMO: if (desc != NULL && desc->on_demo_init != NULL) continue; break;
        case W_LOAD: if (desc != NULL && desc->on_load != NULL) continue; break;
        case W_START: if (desc != NULL && desc->on_start != NULL) continue; break;
        case W_CB4: if (desc != NULL && desc->callback4 != NULL) continue; break;
        case W_TOUCH: if (desc != NULL && desc->on_touch_line != NULL) continue; break;
        default: if (desc != NULL && desc->on_check_shadow_render != NULL) continue; break;
        }
        if (w[k] == 0 || w[k] == 0xFFFFFFFFu) {
            *bad_word = (unsigned int) k;
            return desc != NULL ? "its native code leaves a slot the file's own code fills"
                                : "it runs its own code (grFunction) and this build has none";
        }
        src[k] = mex_match(k, w[k]);
        if (src[k] < 0) {
            *bad_word = (unsigned int) k;
            return "a word names an address of no retail stage";
        }
    }
    {
        const int c = mex_match_coll(mu_mex_stage_coll(id));
        if (c < 0) {
            return "its collision table is no retail one";
        }
        row->coll = mu_mplib_retail_coll(c);
    }
    /* A row may name retail words only and still run code of its own: its file's grFunction
     * exports replace those words at load on the console (ACE has 14 such rows, Arena Ferox
     * among them). Without a descriptor the file must export none. */
    if (desc == NULL) {
        const int code = mex_file_has_code(file);
        if (code < 0) {
            return "its file could not be read to check for code of its own";
        }
        if (code > 0) {
            return "its file carries its own code (grFunction / itFunction) and this build has none";
        }
    }

    memset(sd, 0, sizeof *sd);
    sd->grkind = id;
    sd->data1 = (char*) file;
    sd->flags2 = w[W_FLAGS2];
    sd->joints = NULL;
    sd->joint_count = 0;
#define MEX_FIELD(k, member, dfield)                                                               \
    sd->member = src[k] == -2 ? desc->dfield : mu_ground_retail_stage(src[k])->member
    MEX_FIELD(W_MAP, callbacks, map_gobjs);
    MEX_FIELD(W_INIT, on_init, on_init);
    MEX_FIELD(W_DEMO, on_demo_init, on_demo_init);
    MEX_FIELD(W_LOAD, on_load, on_load);
    MEX_FIELD(W_START, on_start, on_start);
    MEX_FIELD(W_CB4, callback4, callback4);
    MEX_FIELD(W_TOUCH, on_touch_line, on_touch_line);
    MEX_FIELD(W_SHADOW, on_check_shadow_render, on_check_shadow_render);
#undef MEX_FIELD
    /* A retail table under code that is not its own stage's: the console's tail past its end. */
    if (src[W_MAP] >= 0 && src[W_MAP] != src[W_INIT]) {
        sd->callbacks = mex_map_with_tail(src[W_MAP], id);
    }
    from[0] = src[W_MAP];
    from[1] = src[W_INIT];
    return NULL;
}

void mu_mex_stage_build(void)
{
    int count = mu_mex_stage_count();
    int id, playable = 0, locked = 0;

    memset(mex_rows, 0, sizeof mex_rows);
    mex_row_count = 0;
    mex_map_copy_count = 0;
    if (count <= 0) {
        return;
    }
    if (count > MU_MEX_STAGE_MAX) {
        OSReport("[mex] MxDt has %d stages, room for %d; the rest are locked\n", count, MU_MEX_STAGE_MAX);
        count = MU_MEX_STAGE_MAX;
    }
    if (mu_mex_stage_ext_count() > MU_MEX_STAGE_EXT_MAX) {
        OSReport("[mex] MxDt has %d external stage ids, room for %d; the rest map to no stage\n",
                 mu_mex_stage_ext_count(), MU_MEX_STAGE_EXT_MAX);
    }
    mex_row_count = count;
    mex_read_retail_rows();
    for (id = MU_MEX_STAGE_RETAIL; id < count; id++) {
        MexStageRow* row = &mex_rows[id];
        const char* name = mu_mex_stage_name(id);
        const char* file = NULL;
        unsigned int words[W_COUNT];
        int from[2];
        unsigned int bad = 0;
        if (!mu_mex_stage_desc(id, words, &file)) {
            file = NULL;
        }
        row->lock = mex_build_row(id, row, from, &bad);
        row->built = 1;
        if (row->lock != NULL) {
            locked++;
            if (bad != 0) {
                OSReport("[mex] stage %d %s (%s): locked, %s (%s %08X)\n", id, name ? name : "?",
                         file ? file : "no file", row->lock, mex_field_name[bad], words[bad]);
            } else {
                OSReport("[mex] stage %d %s (%s): locked, %s\n", id, name ? name : "?",
                         file ? file : "no file", row->lock);
            }
            continue;
        }
        playable++;
        OSReport("[mex] stage %d %s (%s): playable, map gobj table of %d, on_init of %d\n", id,
                 name ? name : "?", file, from[0], from[1]);
    }
    OSReport("[mex] stages: %d added, %d playable, %d locked\n", count > MU_MEX_STAGE_RETAIL ?
             count - MU_MEX_STAGE_RETAIL : 0, playable, locked);
}

static MexStageRow* mex_row(int grkind)
{
    if (!mu_mex_active() || grkind < MU_MEX_STAGE_RETAIL || grkind >= mex_row_count) {
        return NULL;
    }
    return mex_rows[grkind].built && mex_rows[grkind].lock == NULL ? &mex_rows[grkind] : NULL;
}

StageData* mu_mex_stage_data(int grkind)
{
    MexStageRow* row = mex_row(grkind);
    return row != NULL ? &row->data : NULL;
}

void* mu_mex_stage_coll_table(int grkind)
{
    MexStageRow* row = mex_row(grkind);
    return row != NULL ? row->coll : NULL;
}

int mu_mex_stage_usable(int grkind)
{
    if (grkind >= 0 && grkind < MU_MEX_STAGE_RETAIL) {
        return mu_ground_retail_stage(grkind) != NULL;
    }
    return mex_row(grkind) != NULL;
}

/* ---- stage items (m-ex item kinds 237 to 252) ----
 * Articles: the stage's itemdata list, through it_8026B40C (Ground load), as for retail stage items,
 * past the retail table's 30 rows. Logic: the current row's descriptor, entry i for row item i. */
static void* gr_item_article[MU_GR_ITEM_LAST - MU_GR_ITEM_FIRST + 1];

void mu_gr_stage_item_store(int kind, void* article)
{
    /* An m-ex stage file names its items 5000 + n in itemdata (as the fighters' articles are
     * 5000 + n of their own lists): item n of the current row's list, the kind 803D708C gives. */
    if (kind >= 5000 && kind < 5000 + mu_mex_stage_item_count(stage_info.grkind)) {
        kind = mu_mex_stage_item(stage_info.grkind, kind - 5000);
    }
    if (kind >= MU_GR_ITEM_FIRST && kind <= MU_GR_ITEM_LAST) {
        gr_item_article[kind - MU_GR_ITEM_FIRST] = article;
    } else {
        OSReport("[mex] stage item kind %d is outside 237 to 252; not stored\n", kind);
    }
}

int mu_gr_stage_item(int kind, void** article, void** logic)
{
    const int grkind = stage_info.grkind;
    MexStageRow* row = mex_row(grkind);
    int i;
    if (kind < MU_GR_ITEM_FIRST || kind > MU_GR_ITEM_LAST || row == NULL || row->desc == NULL) {
        return 0;
    }
    for (i = 0; i < mu_mex_stage_item_count(grkind); i++) {
        if (mu_mex_stage_item(grkind, i) == kind && i < row->desc->item_count) {
            *article = gr_item_article[kind - MU_GR_ITEM_FIRST];
            *logic = (void*) &row->desc->items[i];
            return *article != NULL;
        }
    }
    return 0;
}

/* 803D708C: the item kind of the current stage's item `index` (the row's u16 list); the console
 * reports "stage %d does not have item %d" and stops past its end. */
/* m-ex's spawn hook (codes.gct, a C2 at Item_8026862C+1C): a stage's code spawns its own items as
 * kind 5000 + n, and the hook turns that into the stage's item n before the spawn reads the kind.
 * A kind with no such item is left as it is, so nothing is created, as before. */
int mu_gr_stage_item_kind(int kind)
{
    if (kind < 5000 || kind >= 5000 + 64) {
        return kind;
    }
    {
        const int item = mu_mex_stage_item(stage_info.grkind, kind - 5000);
        return item >= 0 ? item : kind;
    }
}

ItemKind mu_gr_item_kind(int index)
{
    const int grkind = stage_info.grkind;
    const int kind = mu_mex_stage_item(grkind, index);
    if (kind < 0) {
        OSReport("[mex] error: stage %d does not have item %d\n", grkind, index);
        __assert("mu_mex_stage.c", __LINE__, "stage item");
    }
    return (ItemKind) kind;
}

const char* mu_mex_stage_lock_reason(int grkind)
{
    if (grkind >= 0 && grkind < MU_MEX_STAGE_RETAIL) {
        return mu_ground_retail_stage(grkind) != NULL ? NULL : "no retail row";
    }
    if (!mu_mex_active() || grkind < 0 || grkind >= mex_row_count || !mex_rows[grkind].built) {
        return "no such stage on this disc";
    }
    return mex_rows[grkind].lock;
}
