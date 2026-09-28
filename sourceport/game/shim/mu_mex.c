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

extern Fighter_CostumeStrings* ftData_803C2360[Ft_Kind_Max];

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

typedef struct MexFighter {
    DISC_PTR(MexStr) names;              /* [external] display names */
    DISC_PTR(void) pl_files;             /* [internal] {file, symbol} */
    DISC_PTR(u8) insignia;               /* [external] emblem frame */
    DISC_PTR(MexExtMap) ext_map;         /* [external] */
    DISC_PTR(MexCostumeInfo) costume_info;   /* [external] */
    DISC_PTR(MexCostumeFiles) costume_files; /* [internal] -> [costume] */
} DISC_STRUCT MexFighter;

typedef struct MexMenu {
    DISC_PTR(void) param;
    DISC_PTR(u8) css;   /* the character select data block; icons at +0xDC, 0x1C bytes each */
    DISC_PTR(void) sss;
} DISC_STRUCT MexMenu;

typedef struct MexData {
    DISC_PTR(MexMeta) metadata;
    DISC_PTR(MexMenu) menu;
    DISC_PTR(MexFighter) fighter;
} DISC_STRUCT MexData;

/* ---- state, fixed after boot ---- */
#define MEX_MAX_COSTUMES 16
#define MEX_MAX_EXTERNAL 64

static MexData* mex;          /* the active tables: NULL in the retail view and on a retail disc */
static MexData* mex_loaded;   /* MxDt.dat once read (the mod view's tables) */
static HSD_Archive mex_archive;
static u8 mex_file[1024 * 1024] __attribute__((aligned(32)));
static int mex_ext_count;
static u8 mex_widened[Ft_Kind_Max];
static u8 mex_costume_count[Ft_Kind_Max];
static u8 mex_vis[Ft_Kind_Max][MEX_MAX_COSTUMES];
static UnkCostumeStruct mex_costume_pool[Ft_Kind_Max][MEX_MAX_COSTUMES];
static Fighter_CostumeStrings mex_costume_strings[Ft_Kind_Max][MEX_MAX_COSTUMES];
/* The retail tables a widened kind replaced, put back in the retail view. */
static struct UnkCostumeList mex_retail_lists[Ft_Kind_Max];
static Fighter_CostumeStrings* mex_retail_strings[Ft_Kind_Max];

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

/* The retail view: every widened table back to its retail list. */
static void mex_restore_retail(void)
{
    int kind;
    for (kind = 0; kind < Ft_Kind_Max; kind++) {
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
        out[i].state = in[1] < CKind_Playable_Count ? 2 : 0;
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
    if (kind >= 0 && kind < Ft_Kind_Max && mex_widened[kind] && costume >= 0 && costume < mex_costume_count[kind]) {
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
