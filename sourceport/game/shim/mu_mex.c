/* m-ex content, read natively (M4).
 *
 * A disc built with m-ex carries MxDt.dat: an HSD archive whose one public object, "mexData", holds
 * the counts and tables m-ex's engine patches read instead of the game's fixed tables (fighter and
 * stage counts, names, costume files, team colors, the character select icons). The native game
 * reads that file directly and points its own tables at the content, so no m-ex code runs here:
 * each place the retail game reads a fixed table asks this file first.
 *
 * Only content the retail game logic can use is enabled. Fighters m-ex adds run their own code
 * (compiled PowerPC in their fighter file) and are not playable natively. Extra costumes of the
 * original fighters are plain model files: each m-ex costume names the retail costume whose parts
 * tables it reuses (its visibility index), so the retail fighter data serves them unchanged. The
 * fighters with more per-costume tables: Kirby's copy hats go through the same parts index
 * (ftkirby.c), Jigglypuff's added costumes are built on her costume without an accessory (ftpurin.c
 * bounds the accessory table), the Ice Climbers widen as a pair, and Mr. Game & Watch keeps his
 * retail count (his colors are a four-entry table in his fighter file). */
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <melee/ft/forward.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/types.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbfile.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/archive.h>

#include <string.h>

#include "mu_disc.h"

extern Fighter_CostumeStrings* ftData_803C2360[FT_KIND_TABLE_MAX];

/* ---- MxDt.dat layout (big-endian, relocated in place by the archive loader) ---- */
typedef DISC_PTR(char) MexStr;

typedef struct MexMeta {
    u8 major, minor;
    u16 flags;
    u32 ft_internal, ft_external, css_icons, gr_internal, gr_external, sss_icons, ssm, bgm, effects,
        boot_scene, last_major, last_minor, trophies, trophy_sd;
} DISC_STRUCT MexMeta;

typedef struct MexCostumeFile {
    MexStr file;
    MexStr joint;
    MexStr matanim;
    u32 visibility;   /* the retail costume whose parts tables this costume reuses */
} DISC_STRUCT MexCostumeFile;

typedef struct MexCostumeInfo {
    u8 count, red, blue, green;   /* the same layout as the retail per-character table */
} MexCostumeInfo;

typedef struct MexExtMap {
    u8 internal, sub, flags;   /* external id -> internal fighter kind */
} MexExtMap;

typedef DISC_PTR(MexCostumeFile) MexCostumeFiles;

typedef struct MexPlFile {
    MexStr file;     /* "PlWf.dat" */
    MexStr symbol;   /* "ftDataWolf" */
} DISC_STRUCT MexPlFile;

typedef DISC_PTR(u8) MexWords;   /* a table of big-endian words */

typedef struct MexItemLookup {
    u32 count;
    DISC_PTR(u8) ids;   /* count big-endian u16 item kinds */
} DISC_STRUCT MexItemLookup;

/* The demo fighter's animation symbols (results, intro, ending, wait). */
typedef struct MexFtDemo {
    MexStr result, intro, ending, wait;
} DISC_STRUCT MexFtDemo;
typedef DISC_PTR(MexFtDemo) MexFtDemoPtr;

typedef struct MexSsmFile {
    u8 ssm_id;
    u8 pad[3];
    u32 x04, x08, x0C;
} DISC_STRUCT MexSsmFile;

/* m-ex MexTK/include/mxdt.h MexData.fighter; the index of each field is its m-ex table number. */
typedef struct MexFighter {
    DISC_PTR(MexStr) names;              /* 0 [external] display names */
    DISC_PTR(MexPlFile) pl_files;        /* 1 [internal] */
    DISC_PTR(u8) insignia;               /* 2 [external] emblem frame */
    DISC_PTR(MexExtMap) ext_map;         /* 3 [external] */
    DISC_PTR(MexCostumeInfo) costume_info;   /* 4 [external] */
    DISC_PTR(MexCostumeFiles) costume_files; /* 5 [internal] -> [costume] */
    DISC_PTR(MexFtDemoPtr) ftdemo;       /* 6 [internal] */
    DISC_PTR(MexStr) anim_files;         /* 7 [internal] PlXxAJ.dat */
    DISC_PTR(u8) anim_num;               /* 8 [internal] two big-endian words each: {0, count} */
    DISC_PTR(u8) effect_index;           /* 9 [internal] the fighter's effect file */
    DISC_PTR(MexStr) result_file;        /* 10 [external] */
    DISC_PTR(u8) result_scale;           /* 11 [external] big-endian floats */
    DISC_PTR(u8) victory_theme;          /* 12 [external] big-endian words */
    DISC_PTR(u8) announcer_call;         /* 13 [external] big-endian words */
    DISC_PTR(MexSsmFile) ssm_files;      /* 14 [internal] */
    DISC_PTR(void) costume_pointers;     /* 15 runtime */
    DISC_PTR(void) ft_archives;          /* 16 runtime */
    DISC_PTR(u8) walljump;               /* 17 [internal] */
    DISC_PTR(void) rst_runtime;          /* 18 runtime */
    DISC_PTR(MexItemLookup) item_lookup; /* 19 [internal] the fighter's articles */
} DISC_STRUCT MexFighter;

typedef struct MexMenu {
    DISC_PTR(void) param;
    DISC_PTR(u8) css;   /* the character select data block; icons at +0xDC, 0x1C bytes each */
    DISC_PTR(void) sss;
} DISC_STRUCT MexMenu;

/* One effect file: the game's own table entry (file, table symbol, loaded data). */
typedef struct MexEffectFile {
    MexStr file;     /* "EfWfData.dat" */
    MexStr symbol;   /* "effWolfDataTable" */
    u32 runtime;
} DISC_STRUCT MexEffectFile;

/* The sound bank table: one entry per bank in each list. */
typedef struct MexSsm {
    DISC_PTR(MexStr) files;   /* file name, no folder ("wolf.ssm") */
    DISC_PTR(u8) sizes;       /* {sample data bytes, flag}, two big-endian words per bank */
    DISC_PTR(u8) rows;        /* {group, load priority, unload priority, pitch threshold} */
    DISC_PTR(void) runtime;
} DISC_STRUCT MexSsm;

typedef struct MexEffect {
    DISC_PTR(MexEffectFile) files;
} DISC_STRUCT MexEffect;

typedef struct MexData {
    DISC_PTR(MexMeta) metadata;
    DISC_PTR(MexMenu) menu;
    DISC_PTR(MexFighter) fighter;
    /* [m-ex ftFunction index] -> [internal] console addresses of the default callbacks (retail
     * functions, or 0); a fighter file's ftFunction export overrides its own entries. */
    DISC_PTR(MexWords) fighter_function;
    DISC_PTR(MexSsm) ssm;         /* the sound banks, by bank id */
    DISC_PTR(void) music;
    DISC_PTR(MexEffect) effect;   /* the effect files, by effect file index */
} DISC_STRUCT MexData;

/* ---- state, fixed after boot ---- */
#define MEX_MAX_COSTUMES 16
#define MEX_MAX_EXTERNAL 64

static MexData* mex;          /* the active tables: NULL in the retail view and on a retail disc */
static MexData* mex_loaded;   /* MxDt.dat once read (the mod view's tables) */
static HSD_Archive mex_archive;
static u8 mex_file[1024 * 1024] __attribute__((aligned(32)));
static int mex_ext_count;
/* The kinds with costume tables here: the retail ones, and in the experimental build the kinds of
 * the fighters m-ex adds (akaneia/CREATION_LAYER_PLAN.md step 4). */
#ifdef MU_AKANEIA_FIGHTERS
#define MEX_KINDS FT_KIND_TABLE_MAX
#else
#define MEX_KINDS Ft_Kind_Max
#endif
static u8 mex_widened[MEX_KINDS];
static u8 mex_costume_count[MEX_KINDS];
static u8 mex_vis[MEX_KINDS][MEX_MAX_COSTUMES];
static UnkCostumeStruct mex_costume_pool[MEX_KINDS][MEX_MAX_COSTUMES];
static Fighter_CostumeStrings mex_costume_strings[MEX_KINDS][MEX_MAX_COSTUMES];
/* The retail tables a widened kind replaced, put back in the retail view. */
static struct UnkCostumeList mex_retail_lists[MEX_KINDS];
static Fighter_CostumeStrings* mex_retail_strings[MEX_KINDS];

int mu_mex_active(void)
{
    return mex != NULL;
}

static const char* mex_name(int ext)
{
    MexStr* names = DP(DP(mex->fighter)->names);
    const char* name = names != NULL ? DP(names[ext]) : NULL;
    return name != NULL ? name : "?";
}

/* Retail fighters whose extra m-ex costumes need no table beyond the parts tables. The Ice Climbers
 * are widened as a pair (Nana takes the player's costume, like Popo). */
static int mex_costume_safe(int kind)
{
    switch (kind) {
    case Ft_Kind_Nana:
    case Ft_Kind_GameWatch:
        return 0;
    }
    return kind < Ft_Kind_MasterH;
}

/* How many of the first `count` m-ex costumes of `kind` are usable (their files are on the disc);
 * fills the name and parts tables for them. */
static int mex_prepare_costumes(int kind, int count, int ext)
{
    MexCostumeFiles* files = DP(DP(mex->fighter)->costume_files);
    MexCostumeFile* list = DP(files[kind]);
    int c;
    if (list == NULL) {
        return 0;
    }
    if (count > MEX_MAX_COSTUMES) {
        count = MEX_MAX_COSTUMES;
    }
    for (c = 0; c < count; c++) {
        const u32 vis = list[c].visibility;
        char* file = DP(list[c].file);
        /* Resolved as the game opens it: a name ending in "." takes the language's extension. */
        if (file == NULL || DVDConvertPathToEntrynum(lbFileGetFullName(file)) < 0) {
            OSReport("[mex] %s costume %d: file %s is not on the disc; stopping at %d costumes\n",
                     mex_name(ext), c, file ? file : "(none)", c);
            break;
        }
        mex_costume_strings[kind][c].dat_filename = file;
        mex_costume_strings[kind][c].joint_name = DP(list[c].joint);
        mex_costume_strings[kind][c].matanim_joint_name = DP(list[c].matanim);
        mex_vis[kind][c] = vis < CostumeListsForeachCharacter[kind].numCostumes ? (u8) vis : 0;
    }
    return c;
}

static void mex_commit_costumes(int kind, int count)
{
    mex_retail_lists[kind] = CostumeListsForeachCharacter[kind];
    mex_retail_strings[kind] = ftData_803C2360[kind];
    mex_costume_count[kind] = (u8) count;
    CostumeListsForeachCharacter[kind].costume_list = mex_costume_pool[kind];
    CostumeListsForeachCharacter[kind].numCostumes = (u8) count;
    ftData_803C2360[kind] = mex_costume_strings[kind];
    mex_widened[kind] = 1;
}

static void mex_widen_costumes(void)
{
    MexFighter* ft = DP(mex->fighter);
    MexExtMap* map = DP(ft->ext_map);
    MexCostumeInfo* info = DP(ft->costume_info);
    int ext;

    for (ext = 0; ext < mex_ext_count && ext < CKind_Playable_Count; ext++) {
        const int kind = map[ext].internal;
        int count;
        if (kind >= Ft_Kind_Max || !mex_costume_safe(kind) || mex_widened[kind]) {
            continue;
        }
        count = mex_prepare_costumes(kind, info[ext].count, ext);
        if (kind == Ft_Kind_Popo) {
            /* Nana wears the player's costume too: both lists or neither. */
            const int nana = mex_prepare_costumes(Ft_Kind_Nana, info[ext].count, ext);
            if (nana < count) {
                count = nana;
            }
            if (count <= CostumeListsForeachCharacter[Ft_Kind_Nana].numCostumes) {
                continue;
            }
            mex_commit_costumes(Ft_Kind_Nana, count);
        }
        if (count <= CostumeListsForeachCharacter[kind].numCostumes) {
            continue;   /* nothing added */
        }
        mex_commit_costumes(kind, count);
        OSReport("[mex] %s: %d costumes\n", mex_name(ext), count);
    }
}

#ifdef MU_AKANEIA_FIGHTERS
/* The costumes of a fighter m-ex adds: native kind `kind`, m-ex internal id `mex_internal`. The
 * costume count is the external fighter's, the files are the internal fighter's. The parts
 * index of each costume is the disc's own: it indexes the tables of the fighter's own file.
 * Returns the usable costume count, 0 when the fighter has none (it must then stay locked). */
int mu_mex_ak_costumes(int kind, int mex_internal)
{
    MexFighter* ft;
    MexCostumeFiles* files;
    MexCostumeFile* list;
    MexCostumeInfo* info;
    int ext, count, c;
    if (mex == NULL || kind < Ft_Kind_Max || kind >= MEX_KINDS || mex_internal < 0 ||
        mex_internal >= mu_mex_fighter_internal_count() || mex_widened[kind])
    {
        return 0;
    }
    ft = DP(mex->fighter);
    files = DP(ft->costume_files);
    info = DP(ft->costume_info);
    ext = mu_mex_external_of_internal(mex_internal);
    if (files == NULL || info == NULL || ext < 0 || (list = DP(files[mex_internal])) == NULL) {
        return 0;
    }
    count = info[ext].count < MEX_MAX_COSTUMES ? info[ext].count : MEX_MAX_COSTUMES;
    for (c = 0; c < count; c++) {
        const u32 vis = list[c].visibility;
        char* file = DP(list[c].file);
        if (file == NULL || DP(list[c].joint) == NULL ||
            DVDConvertPathToEntrynum(lbFileGetFullName(file)) < 0)
        {
            OSReport("[mex] %s costume %d: file %s is not on the disc; stopping at %d costumes\n",
                     mex_name(ext), c, file ? file : "(none)", c);
            break;
        }
        mex_costume_strings[kind][c].dat_filename = file;
        mex_costume_strings[kind][c].joint_name = DP(list[c].joint);
        mex_costume_strings[kind][c].matanim_joint_name = DP(list[c].matanim);
        mex_vis[kind][c] = vis < 256 ? (u8) vis : 0;
    }
    if (c == 0) {
        return 0;
    }
    memset(mex_costume_pool[kind], 0, sizeof mex_costume_pool[kind]);
    mex_commit_costumes(kind, c);
    return c;
}
#endif

/* The retail view: every widened table back to its retail list. */
static void mex_restore_retail(void)
{
    int kind;
    for (kind = 0; kind < MEX_KINDS; kind++) {
        if (!mex_widened[kind]) {
            continue;
        }
        CostumeListsForeachCharacter[kind] = mex_retail_lists[kind];
        ftData_803C2360[kind] = mex_retail_strings[kind];
        mex_widened[kind] = 0;
        mex_costume_count[kind] = 0;
    }
}

static void mex_load(void);

/* Each major mode load: the tables of the view the host serves (mu_content.c). */
void mu_mex_boot(void)
{
    if (mu_content_vanilla()) {
        if (mex != NULL) {
            mex_restore_retail();
            mex = NULL;
            mu_ak_apply();
            OSReport("[mex] retail view: retail tables\n");
        }
        return;
    }
    if (mex != NULL) {
        return;
    }
    if (mex_loaded == NULL) {
        mex_load();
        return;
    }
    mex = mex_loaded;
    mex_widen_costumes();
    mu_ak_apply();
    OSReport("[mex] mod view: m-ex tables again\n");
}

int mu_mex_special_kind_shift(void)
{
    if (mex == NULL) {
        return 0;
    }
    {
        const int shift = (int) DP(mex->metadata)->ft_internal - Ft_Kind_Max;
        return shift > 0 ? shift : 0;
    }
}

static void mex_load(void)
{
    MexMeta* meta;
    static int tried;
    if (tried) {
        return;
    }
    if (DVDConvertPathToEntrynum("MxDt.dat") < 0) {
        return;   /* a retail disc, or the retail view: asked again at the next load */
    }
    tried = 1;
    /* The game's heaps belong to the current scene; m-ex keeps its tables for the whole session,
     * so the file lives in this library's own memory (32-bit addressable, like the heaps). */
    {
        size_t length = lbFileGetSize("MxDt.dat");
        if (length == 0 || length > sizeof mex_file) {
            OSReport("[mex] MxDt.dat is %u bytes (at most %u supported); the retail tables stay\n",
                     (unsigned) length, (unsigned) sizeof mex_file);
            return;
        }
        lbFile_8001668C("MxDt.dat", mex_file, &length);
        lbArchive_InitializeDAT(&mex_archive, mex_file, length);
        mex = HSD_ArchiveGetPublicAddress(&mex_archive, "mexData");
    }
    if (mex == NULL || DP(mex->metadata) == NULL || DP(mex->fighter) == NULL) {
        OSReport("[mex] MxDt.dat has no usable mexData; the retail tables stay\n");
        mex = NULL;
        return;
    }
    meta = DP(mex->metadata);
    if (meta->major != 1 || meta->ft_external == 0 || meta->ft_external > MEX_MAX_EXTERNAL ||
        meta->ft_internal > MEX_MAX_EXTERNAL)
    {
        OSReport("[mex] MxDt.dat version %d.%d with %u fighters is not supported; the retail tables stay\n",
                 meta->major, meta->minor, (unsigned) meta->ft_external);
        mex = NULL;
        return;
    }
    mex_ext_count = (int) meta->ft_external;
    mex_loaded = mex;
    OSReport("[mex] MxDt.dat %d.%d: %u fighters, %u select icons, %u stages, %u select stage icons, %u songs\n",
             meta->major, meta->minor, (unsigned) meta->ft_external, (unsigned) meta->css_icons,
             (unsigned) meta->gr_external, (unsigned) meta->sss_icons, (unsigned) meta->bgm);
    mex_widen_costumes();
    mu_ak_apply();
}

/* m-ex's character select icons, converted to the game's icon table (big-endian disc words to host
 * order). An icon for a fighter the native game cannot play (one m-ex added, with its own code)
 * stays visible but locked. Returns the icon count, 0 when there is no m-ex table. */
int mu_mex_css_icons(CSSIcon* out, int max)
{
    MexMenu* menu;
    u8* table;
    int count, i;
    if (mex == NULL || DP(mex->menu) == NULL) {
        return 0;
    }
    menu = DP(mex->menu);
    table = DP(menu->css);
    count = (int) DP(mex->metadata)->css_icons;
    if (table == NULL || count <= 0 || count > max) {
        return 0;
    }
    table += 0xDC;   /* the icons follow the name and mode blocks, as in the retail data */
    for (i = 0; i < count; i++) {
        const u8* in = table + i * 0x1C;
        const u32* words = (const u32*) (in + 8);
        u32 w[5];
        int k;
        for (k = 0; k < 5; k++) {
            const u8* b = (const u8*) &words[k];
            w[k] = (u32) b[0] << 24 | (u32) b[1] << 16 | (u32) b[2] << 8 | b[3];
        }
        out[i].ft_hudindex = in[0];
        out[i].char_kind = in[1];
        out[i].state = in[1] < CKind_Playable_Count || mu_ak_css_selectable(in[1]) ? 2 : 0;
#ifdef MU_AKANEIA_FIGHTERS
        {
            const int native = mu_ak_ckind_from_mex(in[1]);
            out[i].char_kind = native >= 0 ? (u8) native : ChKind_None;
            if (native < 0) out[i].state = 0;
        }
#endif
        out[i].anim_timer = 0;
        out[i].joint_id_vs = in[4];
        out[i].joint_id_1p = in[5];
        out[i].sfx = (int) w[0];
        __builtin_memcpy(&out[i].bound_l, &w[1], 4);
        __builtin_memcpy(&out[i].bound_r, &w[2], 4);
        __builtin_memcpy(&out[i].bound_u, &w[3], 4);
        __builtin_memcpy(&out[i].bound_d, &w[4], 4);
    }
    return count;
}

/* The emblem frame m-ex gives an external fighter id. */
int mu_mex_insignia(int ext)
{
    MexFighter* ft;
    if (mex == NULL || ext < 0 || ext >= mex_ext_count) {
        return 0;
    }
    ft = DP(mex->fighter);
    return DP(ft->insignia) != NULL ? DP(ft->insignia)[ext] : 0;
}

/* The retail costume whose parts tables costume `costume` of `kind` uses. */
int mu_mex_parts_costume(int kind, int costume)
{
    if (kind >= 0 && kind < MEX_KINDS && mex_widened[kind] && costume >= 0 && costume < mex_costume_count[kind]) {
        return mex_vis[kind][costume];
    }
    return costume;
}

/* Costume count and team colors for a retail character select kind whose costumes m-ex widened;
 * -1 when the retail table applies. which: 0 count, 1 red, 2 blue, 3 green. */
int mu_mex_costume_info(int ckind, int which)
{
    MexFighter* ft;
    MexCostumeInfo* info;
    int kind;
#ifdef MU_AKANEIA_FIGHTERS
    if (MU_AK_CKIND(ckind)) {
        /* An added character: its costumes are the ones mu_mex_ak_costumes found, its team
         * colors the disc's, by its external id. 0 costumes when it has none. */
        const int ext = mu_ak_mex_external(ckind);
        kind = MU_AK_KIND_BASE + (ckind - MU_AK_CKIND_BASE);
        if (mex == NULL || ext < 0 || ext >= mex_ext_count || !mex_widened[kind]) {
            return 0;
        }
        info = &DP(DP(mex->fighter)->costume_info)[ext];
        switch (which) {
        case 0: return mex_costume_count[kind];
        case 1: return info->red < mex_costume_count[kind] ? info->red : 0;
        case 2: return info->blue < mex_costume_count[kind] ? info->blue : 0;
        default: return info->green < mex_costume_count[kind] ? info->green : 0;
        }
    }
#endif
    if (mex == NULL || ckind < 0 || ckind >= mex_ext_count || ckind >= CKind_Playable_Count) {
        return -1;
    }
    ft = DP(mex->fighter);
    kind = DP(ft->ext_map)[ckind].internal;
    if (kind >= Ft_Kind_Max || !mex_widened[kind]) {
        return -1;
    }
    info = &DP(ft->costume_info)[ckind];
    switch (which) {
    case 0: return mex_costume_count[kind];
    case 1: return info->red < mex_costume_count[kind] ? info->red : 0;
    case 2: return info->blue < mex_costume_count[kind] ? info->blue : 0;
    default: return info->green < mex_costume_count[kind] ? info->green : 0;
    }
}

/* ---- the tables behind the fighters m-ex adds (sourceport/game/akaneia/mu_ak_fighters.c) ---- */

static u32 mex_be32(const u8* p)
{
    return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3];
}

int mu_mex_fighter_internal_count(void)
{
    return mex != NULL ? (int) DP(mex->metadata)->ft_internal : 0;
}

/* The fighter file of an m-ex internal fighter id, NULL when the slot is empty. */
const char* mu_mex_fighter_file(int mex_internal)
{
    MexPlFile* files;
    const char* name;
    if (mex == NULL || mex_internal < 0 || mex_internal >= mu_mex_fighter_internal_count()) {
        return NULL;
    }
    files = DP(DP(mex->fighter)->pl_files);
    name = files != NULL ? DP(files[mex_internal].file) : NULL;
    return name != NULL && name[0] != '\0' ? name : NULL;
}

/* m-ex's default for ftFunction slot `table` of an internal fighter: a console address (a retail
 * function or table), 0 when empty. Only compared, never called. */
unsigned int mu_mex_fighter_function(int table, int mex_internal)
{
    MexWords* tables;
    const u8* words;
    if (mex == NULL || table < 0 || table >= 64 || mex_internal < 0 ||
        mex_internal >= mu_mex_fighter_internal_count())
    {
        return 0;
    }
    tables = DP(mex->fighter_function);
    words = tables != NULL ? DP(tables[table]) : NULL;
    return words != NULL ? mex_be32(words + 4 * mex_internal) : 0;
}

/* ---- the creation layer's tables (CREATION_LAYER_PLAN.md) ---- */

static int mex_internal_ok(int mex_internal)
{
    return mex != NULL && mex_internal >= 0 && mex_internal < mu_mex_fighter_internal_count();
}

static int mex_external_ok(int ext)
{
    return mex != NULL && ext >= 0 && ext < mex_ext_count;
}

/* The fighter file's ftData symbol ("ftDataWolf"), NULL when the slot is empty. */
const char* mu_mex_fighter_symbol(int mex_internal)
{
    MexPlFile* files;
    if (!mex_internal_ok(mex_internal) || (files = DP(DP(mex->fighter)->pl_files)) == NULL) {
        return NULL;
    }
    return DP(files[mex_internal].symbol);
}

/* The animation file (PlXxAJ.dat) and its animation count. */
const char* mu_mex_fighter_anim_file(int mex_internal)
{
    MexStr* files;
    if (!mex_internal_ok(mex_internal) || (files = DP(DP(mex->fighter)->anim_files)) == NULL) {
        return NULL;
    }
    return DP(files[mex_internal]);
}

int mu_mex_fighter_anim_count(int mex_internal)
{
    const u8* words;
    if (!mex_internal_ok(mex_internal) || (words = DP(DP(mex->fighter)->anim_num)) == NULL) {
        return 0;
    }
    /* Each entry is the retail pair of words {loaded data (0 on the disc), count}, eight bytes
     * per fighter, not one word. Read on the Akaneia 1.0.1 disc: Wolf (internal 27) has 327. */
    return (int) mex_be32(words + 8 * mex_internal + 4);
}

/* The fighter's effect file index (into mexData.effect.files), -1 when none. */
int mu_mex_fighter_effect_file(int mex_internal)
{
    const u8* bytes;
    if (!mex_internal_ok(mex_internal) || (bytes = DP(DP(mex->fighter)->effect_index)) == NULL) {
        return -1;
    }
    return bytes[mex_internal];
}

/* Effect file `index` of the disc's effect table (the index mu_mex_fighter_effect_file gives):
 * its file and the symbol of its table. Returns 0 when the index is out of range, the entry is
 * empty or the file is not on the disc. */
int mu_mex_effect_file(int index, const char** file, const char** symbol)
{
    MexEffect* effect;
    MexEffectFile* files;
    const char* name;
    const char* table;
    if (mex == NULL || index < 0 || index >= (int) DP(mex->metadata)->effects ||
        (effect = DP(mex->effect)) == NULL || (files = DP(effect->files)) == NULL)
    {
        return 0;
    }
    name = DP(files[index].file);
    table = DP(files[index].symbol);
    if (name == NULL || name[0] == '\0' || table == NULL || table[0] == '\0' ||
        DVDConvertPathToEntrynum(lbFileGetFullName((char*) name)) < 0)
    {
        return 0;
    }
    *file = name;
    *symbol = table;
    return 1;
}

/* The demo fighter's animation symbols: which 0 result, 1 intro, 2 ending, 3 wait. */
const char* mu_mex_fighter_demo(int mex_internal, int which)
{
    MexFtDemoPtr* demos;
    MexFtDemo* demo;
    if (!mex_internal_ok(mex_internal) || which < 0 || which > 3 ||
        (demos = DP(DP(mex->fighter)->ftdemo)) == NULL || (demo = DP(demos[mex_internal])) == NULL)
    {
        return NULL;
    }
    switch (which) {
    case 0: return DP(demo->result);
    case 1: return DP(demo->intro);
    case 2: return DP(demo->ending);
    default: return DP(demo->wait);
    }
}

/* The sound bank (SSM id) of a fighter, -1 when none. The table is indexed by the EXTERNAL
 * fighter id, like the retail table it replaces (the m-ex header says internal; the disc and
 * every m-ex reader say external: Wolf, external 26, has bank 62, wolf.ssm). */
int mu_mex_fighter_ssm(int ext)
{
    MexSsmFile* files;
    if (!mex_external_ok(ext) || (files = DP(DP(mex->fighter)->ssm_files)) == NULL) {
        return -1;
    }
    return files[ext].ssm_id;
}

/* The disc's sound bank table (mexData.ssm): how many banks, and for bank `id` its file name
 * (no folder), its sample data size and its row {group, load priority, unload priority, pitch
 * threshold}. Returns 0 when there is no such bank. */
int mu_mex_ssm_count(void)
{
    return mex != NULL ? (int) DP(mex->metadata)->ssm : 0;
}

int mu_mex_ssm_bank(int id, const char** file, unsigned int* size, signed char row[4])
{
    MexSsm* ssm;
    MexStr* files;
    const u8* sizes;
    const u8* rows;
    const char* name;
    if (mex == NULL || id < 0 || id >= mu_mex_ssm_count() || (ssm = DP(mex->ssm)) == NULL ||
        (files = DP(ssm->files)) == NULL || (sizes = DP(ssm->sizes)) == NULL ||
        (rows = DP(ssm->rows)) == NULL)
    {
        return 0;
    }
    name = DP(files[id]);
    if (name == NULL || name[0] == 0) {
        return 0;
    }
    *file = name;
    *size = mex_be32(sizes + 8 * id);
    row[0] = (signed char) rows[4 * id];
    row[1] = (signed char) rows[4 * id + 1];
    row[2] = (signed char) rows[4 * id + 2];
    row[3] = (signed char) rows[4 * id + 3];
    return 1;
}

/* Wall jump ability of an internal fighter (0 or 1), -1 when the table is missing. */
int mu_mex_fighter_walljump(int mex_internal)
{
    const u8* bytes;
    if (!mex_internal_ok(mex_internal) || (bytes = DP(DP(mex->fighter)->walljump)) == NULL) {
        return -1;
    }
    return bytes[mex_internal];
}

/* External-id tables: the display name, results file and scale, victory theme, announcer call. */
const char* mu_mex_fighter_name(int ext)
{
    return mex_external_ok(ext) ? mex_name(ext) : NULL;
}

const char* mu_mex_fighter_result_file(int ext)
{
    MexStr* files;
    const char* file;
    if (!mex_external_ok(ext) || (files = DP(DP(mex->fighter)->result_file)) == NULL) {
        return NULL;
    }
    /* Only a file the disc has: the results screen opens it without a check. */
    file = DP(files[ext]);
    if (file == NULL || file[0] == 0 || DVDConvertPathToEntrynum(lbFileGetFullName(file)) < 0) {
        return NULL;
    }
    return file;
}

float mu_mex_fighter_result_scale(int ext)
{
    const u8* words;
    u32 bits;
    float value;
    if (!mex_external_ok(ext) || (words = DP(DP(mex->fighter)->result_scale)) == NULL) {
        return 1.0f;
    }
    bits = mex_be32(words + 4 * ext);
    __builtin_memcpy(&value, &bits, 4);
    return value;
}

int mu_mex_fighter_victory_theme(int ext)
{
    const u8* words;
    if (!mex_external_ok(ext) || (words = DP(DP(mex->fighter)->victory_theme)) == NULL) {
        return -1;
    }
    return (int) mex_be32(words + 4 * ext);
}

int mu_mex_fighter_announcer(int ext)
{
    const u8* words;
    if (!mex_external_ok(ext) || (words = DP(DP(mex->fighter)->announcer_call)) == NULL) {
        return -1;
    }
    return (int) mex_be32(words + 4 * ext);
}

/* The m-ex external id of an internal fighter (the first external id that maps to it), -1 if none. */
int mu_mex_external_of_internal(int mex_internal)
{
    MexExtMap* map;
    int ext;
    if (!mex_internal_ok(mex_internal) || (map = DP(DP(mex->fighter)->ext_map)) == NULL) {
        return -1;
    }
    for (ext = 0; ext < mex_ext_count; ext++) {
        if (map[ext].internal == mex_internal) {
            return ext;
        }
    }
    return -1;
}

/* The m-ex internal fighter of an external id, -1 when out of range. */
int mu_mex_internal_of_external(int ext)
{
    MexExtMap* map;
    if (!mex_external_ok(ext) || (map = DP(DP(mex->fighter)->ext_map)) == NULL) {
        return -1;
    }
    return map[ext].internal;
}

/* The game's item kind for article `local` of an internal fighter, -1 when it has none. */
int mu_mex_fighter_item(int mex_internal, int local)
{
    MexItemLookup* lookup;
    const u8* ids;
    if (mex == NULL || mex_internal < 0 || mex_internal >= mu_mex_fighter_internal_count() || local < 0) {
        return -1;
    }
    lookup = DP(DP(mex->fighter)->item_lookup);
    if (lookup == NULL || (u32) local >= lookup[mex_internal].count) {
        return -1;
    }
    ids = DP(lookup[mex_internal].ids);
    return ids != NULL ? (int) (ids[2 * local] << 8 | ids[2 * local + 1]) : -1;
}
