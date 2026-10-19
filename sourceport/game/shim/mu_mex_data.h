/* MxDt.dat, the m-ex data file, as it sits in memory: big-endian, its pointer words relocated in
 * place by the archive loader (shim/mu_mex.c reads it). The layout of m-ex's MexTK mxdt.h and
 * stage.h, checked on Akaneia 1.0.1 and ACE 2.0.0. Shared by shim/mu_mex.c and the native m-ex stage
 * services (mexstages/common/mu_gr_mex.c), which hand stage code these tables as MEX_GetData does
 * on the console. Never include it next to the TM-CE MexTK headers (tmce/MexTK): the names are
 * theirs. */
#ifndef MU_MEX_DATA_H
#define MU_MEX_DATA_H

#include <Runtime/platform.h>

#include "mu_disc.h"


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
    DISC_PTR(u8) target_stage;           /* 20 [external] big-endian u16: the Target Test stage */
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

/* MxDt "kirby": what Kirby needs per copied fighter. Read on Akaneia 1.0.1 (41 internal ids). */
typedef struct MexKirby {
    DISC_PTR(MexPlFile) cap_files;    /* +0  [internal] {hat file "PlKbCpWf.dat", "ftDataKirbyCopyWolf"} */
    DISC_PTR(void) x4;                /* +4  [internal] words, all zero on the disc (runtime) */
    DISC_PTR(void) costume_files;     /* +8  [internal] per-costume hat files; only retail kinds have any */
    DISC_PTR(void) costume_runtime;   /* +C  [internal] */
    DISC_PTR(u8) effect_ids;          /* +10 [internal] the effect file loaded with the hat, 0xFF none */
    DISC_PTR(void) x14;               /* +14 [internal] words, all zero on the disc (runtime) */
} DISC_STRUCT MexKirby;

/* ---- MxDt "stage" (word 10) and the stage descriptor rows (word 11), m-ex MexTK stage.h ----
 * run-source/rel09-stages/PLAN.md 1.1. Every word that holds a callback, the map gobj table, the
 * joints or a collision table is a raw address of the retail main.dol: never relocated, never
 * dereferenced here. shim/mu_mex_stage.c turns them into native objects by matching MxDt rows 0
 * to 70 against the native retail rows. */
typedef struct MexStageExt {
    u32 internal;   /* external stage id -> internal id; then two words, 0 on both discs */
    u32 x4, x8;
} DISC_STRUCT MexStageExt;

typedef struct MexStageColl {
    u32 internal;
    u32 table;      /* raw retail address of the mpLib_803BF248 row's table */
} DISC_STRUCT MexStageColl;

typedef struct MexPlaylistSong {
    u16 song;       /* -1 (0xFFFF) is an error on the console */
    u16 chance;
} DISC_STRUCT MexPlaylistSong;

typedef struct MexPlaylist {
    u32 count;
    DISC_PTR(MexPlaylistSong) songs;
} DISC_STRUCT MexPlaylist;

typedef struct MexStage {
    DISC_PTR(MexStageExt) ext;            /* +0  [external] 12 bytes each */
    DISC_PTR(u8) sound;                   /* +4  [internal] 3 bytes each: bank, reverb, third byte */
    DISC_PTR(MexStageColl) coll;          /* +8  [internal] */
    DISC_PTR(MexItemLookup) items;        /* +C  [internal] stage item kinds (m-ex 237 to 252) */
    DISC_PTR(MexStr) names;               /* +10 [internal] */
    DISC_PTR(MexPlaylist) playlists;      /* +14 [internal] */
} DISC_STRUCT MexStage;

/* The retail StageData layout, 13 words. */
typedef struct MexGrDesc {
    u32 grkind;
    u32 map_gobjs;                        /* raw retail address */
    MexStr file;                          /* relocated string */
    u32 on_init, on_demo_init, on_load, on_start, callback4, on_touch_line, on_check_shadow_render;
    u32 flags2;
    u32 joints;                           /* raw retail address */
    u32 joint_count;
} DISC_STRUCT MexGrDesc;
typedef DISC_PTR(MexGrDesc) MexGrDescPtr;

/* MxDt "music" (word 5), m-ex MexTK MexMusicTable; +C read on both discs: one label per song
 * id ("2PM (Evening)", Shift JIS), the same count as +0. */
typedef struct MexMusic {
    DISC_PTR(MexStr) files;        /* +0 [song id] the .hps file name, no folder */
    DISC_PTR(u8) menu_playlist;    /* +4 {u16 song, u16 chance} each */
    u32 menu_playlist_count;       /* +8 */
    DISC_PTR(MexStr) labels;       /* +C [song id] the song's display name */
} DISC_STRUCT MexMusic;

/* One item logic table of an item kind m-ex adds (kind 237 on): the 15 words of the game's
 * ItemLogicTable (it/kinds/types.h, the same order). Word 0 is relocated (a state table in
 * MxDt.dat) or 0; the callbacks are raw retail main.dol addresses, or 0: never called. An itFunction
 * block's exports replace words of it at load on the console (m-ex 803D7070). */
typedef struct MexItemLogic {
    DISC_PTR(u8) states;
    u32 callbacks[14];
} DISC_STRUCT MexItemLogic;

/* MxDt "item" (word 7): the raw retail addresses of the four retail logic tables (common, fighter,
 * Pokemon, stage items), then the tables of the kinds m-ex adds, by kind - 237 (m-ex
 * Item_80267978+18, it_8026B40C). The article words are filled at run time on the console (0 on
 * the disc). */
#define MEX_ITEM_ADDED_BASE 237
typedef struct MexItem {
    u32 common, fighter, pokemon, stage;
    DISC_PTR(MexItemLogic) added;      /* +10 [kind - 237] */
    DISC_PTR(u8) added_articles;       /* +14 [kind - 237] run time words */
} DISC_STRUCT MexItem;

typedef struct MexData {
    DISC_PTR(MexMeta) metadata;
    DISC_PTR(MexMenu) menu;
    DISC_PTR(MexFighter) fighter;
    /* [m-ex ftFunction index] -> [internal] console addresses of the default callbacks (retail
     * functions, or 0); a fighter file's ftFunction export overrides its own entries. */
    DISC_PTR(MexWords) fighter_function;
    DISC_PTR(MexSsm) ssm;         /* the sound banks, by bank id */
    DISC_PTR(MexMusic) music;     /* +14 */
    DISC_PTR(MexEffect) effect;   /* the effect files, by effect file index */
    DISC_PTR(MexItem) item;       /* +1C the item tables */
    DISC_PTR(MexKirby) kirby;     /* +20 */
    /* +24 [kbFunction index] -> [internal]: retail functions, 0 for every added fighter (their hat
     * files export their own). Not read: the added abilities are C (akaneia/common/mu_ak_kirby.c). */
    DISC_PTR(MexWords) kirby_function;
    DISC_PTR(MexStage) stage;               /* +28 */
    DISC_PTR(MexGrDescPtr) stage_desc;      /* +2C [internal] */
} DISC_STRUCT MexData;

/* The mexData root while the mod view is served, NULL in the retail view and on a retail disc
 * (shim/mu_mex.c). */
MexData* mu_mex_root(void);

#endif
