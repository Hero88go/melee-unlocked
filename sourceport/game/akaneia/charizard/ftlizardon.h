/* Akaneia's Charizard ("Lizardon", PlLz.dat), native.
 *
 * Hand-written from the m-ex code in PlLz.dat (ftFunction + move_logic + three item modules in
 * itFunction). Charizard is built on vanilla parts: Neutral B is Bowser's Fire Breath with its own
 * flame item (a clone of itKoopaFlame), up to two midair jumps use the multi-jump machinery of
 * Kirby and Jigglypuff, Rock Smash spawns a held rock item and a burst of rock pieces.
 *
 * Offsets in comments are console offsets (Fighter at fp+, Item at ip+, attributes at +). The
 * Fighter/Item structs are host-laid-out natively, so the per-fighter and per-motion variables
 * below are overlays of fp->u / fp->mv / ip->xDD4_itemVar and only their order matters. */
#ifndef MU_AK_CHARIZARD_FTLIZARDON_H
#define MU_AK_CHARIZARD_FTLIZARDON_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>
#include <melee/it/forward.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/forward.h>

#include <mu_disc.h>

/* ---- Special attributes (ftData->ext_attr, 0xB0 bytes, disc data) ------------------------- */

typedef struct ftLz_DatAttrs {
    /* +00 */ int tail_fire_part;       ///< fp->parts index the tail flame follows (11)
    /* +04 */ int tail_fire_gfx;        ///< tail flame effect id (0x1779)
    /* +08 */ int x8;                   ///< unused by the code
    /* +0C */ int xC;                   ///< unused by the code
    /* +10 */ int x10;                  ///< unused by the code
    /* +14 */ int specialn_min_frames;  ///< Flamethrower runs at least this long (40)
    /* +18 */ float fire_speed_regen;   ///< fighter var speed reserve regained per frame (0.7)
    /* +1C */ float fire_size_regen;    ///< fighter var size reserve regained per frame (0.7)
    /* +20 */ float fire_speed_max;     ///< (360)
    /* +24 */ float fire_speed_min;     ///< (40)
    /* +28 */ float fire_size_max;      ///< (380)
    /* +2C */ float fire_size_min;      ///< (60)
    /* +30 */ int specialn_quake_period; ///< Flamethrower camera rumble period (30)
    /* +34 */ float fire_offset_x;      ///< flame spawn offset from the mouth bone (0)
    /* +38 */ float fire_offset_y;      ///< (0)
    /* +3C */ int fire_part;            ///< fp->parts index flames spawn from (27)
    /* +40 */ float specialairs_start_vel_x_mul; ///< (0.4)
    /* +44 */ float specialairs_start_gravity;   ///< (0.01)
    /* +48 */ float specialairs_start_terminal_vel; ///< (0.2)
    /* +4C */ float specials_speed;     ///< Flame Wheel roll speed (2.8)
    /* +50 */ float specials_friction;  ///< ground/air deceleration while cmd var 0 is set (0.1)
    /* +54 */ int specials_frames;      ///< Flame Wheel roll duration (15)
    /* +58 */ float specials_end_friction; ///< (0.16)
    /* +5C */ float specials_end_vel_mul;  ///< horizontal speed kept entering the end (0.55)
    /* +60 */ float specialairs_end_friction; ///< (0.012)
    /* +64 */ float specialairs_end_gravity;  ///< (0.12)
    /* +68 */ float x68;                     ///< unused by the code
    /* +6C */ float specialairs_end_landing_lag; ///< (30)
    /* +70 */ float specialhi_drift_mul;  ///< Fly: air drift acceleration multiplier (0.6)
    /* +74 */ float specialhi_drift_max;  ///< Fly: air drift max speed multiplier (0.35)
    /* +78 */ float specialhi_landing_lag; ///< (30)
    /* +7C */ Fighter_x2D0_t multijump; ///< fp->x2D0: 12 turn frames, 2 jumps from state 341
} DISC_STRUCT ftLz_DatAttrs;

/* ---- Fighter variables (fp->u, console fp+222C) ------------------------------------------- */

typedef struct ftLz_FighterVars {
    /* fp+222C */ float fire_speed;   ///< Flamethrower speed reserve, drains while flaming
    /* fp+2230 */ float fire_size;    ///< Flamethrower size reserve
    /* fp+2234 */ int tail_fire_timer; ///< counts 5..0 in the tail flame proc (no other reader)
    /* fp+2248 */ HSD_GObj* rock;     ///< Rock Smash's held rock item, NULL when none
    /* fp+224C */ int rock_variant;   ///< floor material the last rock was made from
} ftLz_FighterVars;

/* ---- Motion variables (fp->mv, console fp+2340) ------------------------------------------- */

typedef union ftLz_MotionVars {
    struct ftLz_SpecialNVars {
        /* fp+2340 */ int fire_cycle;   ///< 0..2, a flame spawns when 0
        /* fp+2344 */ u32 hit_id;       ///< Item_8026AE60 id shared by a batch of flames
        /* fp+2348 */ int gfx;          ///< flame effect variant 0..3 (Bowser's table)
        /* fp+234C */ int frames;       ///< frames in the loop, capped at specialn_min_frames
        /* fp+2350 */ int loops_left;   ///< set to 1, counts down on each animation loop
        /* fp+2354 */ int sfx_cycle;    ///< 0..11: new hit id at 0, sound every 3rd flame
        /* fp+2358 */ int quake_timer;  ///< 0..specialn_quake_period
    } specialn;
    struct ftLz_SpecialHiVars {
        /* fp+2340 */ Vec3 vel;         ///< drift velocity carried between frames
    } specialhi;
    struct ftLz_SpecialSVars {
        /* fp+2340 */ int charged;      ///< only ever 0 here (slows the roll when set)
        /* fp+2344 */ int frames;       ///< frames rolled
    } specials;
} ftLz_MotionVars;

static inline ftLz_DatAttrs* ftLz_Attrs(Fighter* fp)
{
    return (ftLz_DatAttrs*) fp->dat_attrs;
}

static inline ftLz_FighterVars* ftLz_Vars(Fighter* fp)
{
    return (ftLz_FighterVars*) &fp->u;
}

static inline ftLz_MotionVars* ftLz_MV(Fighter* fp)
{
    return (ftLz_MotionVars*) &fp->mv;
}

/* ---- Motion states (move_logic, numbered from ftCo_MS_Count) -------------------------------- */

typedef enum ftLizardon_MotionState {
    ftLz_MS_JumpAerialF1 = ftCo_MS_Count, // 341
    ftLz_MS_JumpAerialF2,                 // 342
    ftLz_MS_SpecialNStart,                // 343
    ftLz_MS_SpecialN,                     // 344
    ftLz_MS_SpecialNEnd,                  // 345
    ftLz_MS_SpecialAirNStart,             // 346
    ftLz_MS_SpecialAirN,                  // 347
    ftLz_MS_SpecialAirNEnd,               // 348
    ftLz_MS_SpecialHi,                    // 349
    ftLz_MS_SpecialAirHi,                 // 350
    ftLz_MS_SpecialLw,                    // 351
    ftLz_MS_SpecialAirLw,                 // 352
    ftLz_MS_SpecialSStart,                // 353
    ftLz_MS_SpecialS,                     // 354
    ftLz_MS_SpecialSBlown,                // 355, never entered by Charizard's code
    ftLz_MS_SpecialSEnd,                  // 356
    ftLz_MS_SpecialAirSStart,             // 357
    ftLz_MS_SpecialAirS,                  // 358
    ftLz_MS_SpecialAirSBlown,             // 359, never entered by Charizard's code
    ftLz_MS_SpecialAirSEnd,               // 360
    ftLz_MS_SelfCount = ftLz_MS_SpecialAirSEnd - ftCo_MS_Count + 1,
} ftLizardon_MotionState;

/* Animation ids of the states (the fighter's own animation table, from 295). */
typedef enum ftLizardon_Submotion {
    ftLz_SM_JumpAerialF1 = 0x127,
    ftLz_SM_JumpAerialF2,
    ftLz_SM_SpecialNStart,
    ftLz_SM_SpecialN,
    ftLz_SM_SpecialNEnd,
    ftLz_SM_SpecialAirNStart,
    ftLz_SM_SpecialAirN,
    ftLz_SM_SpecialAirNEnd,
    ftLz_SM_SpecialHi,
    ftLz_SM_SpecialAirHi,
    ftLz_SM_SpecialLw,
    ftLz_SM_SpecialAirLw,
    ftLz_SM_SpecialSStart,
    ftLz_SM_SpecialS,
    ftLz_SM_SpecialSBlown,
    ftLz_SM_SpecialSEnd,
    ftLz_SM_SpecialAirSStart,
    ftLz_SM_SpecialAirS,
    ftLz_SM_SpecialAirSBlown,
    ftLz_SM_SpecialAirSEnd, // 0x13A
} ftLizardon_Submotion;

/* Motion flags of the table (0x00340010 plus per-move bits), named like Bowser's. */
static MotionFlags const ftLz_MF_Special =
    Ft_MF_SkipModel | Ft_MF_SkipItemVis | Ft_MF_UnkUpdatePhys | Ft_MF_FreezeState;
static MotionFlags const ftLz_MF_JumpAerial = ftLz_MF_Special | Ft_MF_KeepFastFall; // 0x340011
static MotionFlags const ftLz_MF_SpecialN = ftLz_MF_Special | Ft_MF_KeepFastFall; // 0x340011
static MotionFlags const ftLz_MF_SpecialNLoop = ftLz_MF_SpecialN | Ft_MF_Unk19;   // 0x3C0011
static MotionFlags const ftLz_MF_SpecialAirN = ftLz_MF_SpecialN | Ft_MF_SkipParasol; // 0x340411
static MotionFlags const ftLz_MF_SpecialAirNLoop =
    ftLz_MF_SpecialNLoop | Ft_MF_SkipParasol; // 0x3C0411
static MotionFlags const ftLz_MF_SpecialS =
    ftLz_MF_Special | Ft_MF_KeepSfx | Ft_MF_KeepGfx; // 0x340212
static MotionFlags const ftLz_MF_SpecialHi =
    ftLz_MF_SpecialS | Ft_MF_KeepFastFall; // 0x340213
static MotionFlags const ftLz_MF_SpecialLw =
    ftLz_MF_Special | Ft_MF_KeepSfx | Ft_MF_KeepColAnimHitStatus; // 0x340214

/* Ground/air switches inside Flame Wheel keep hit status and effects (0x0C4C508B). */
static MotionFlags const ftLz_MF_SpecialS_Coll =
    ftCommon_GroundAirColl_MF | Ft_MF_SkipHit | Ft_MF_KeepGfx | Ft_MF_KeepFastFall;

/* ---- m-ex services (provided by the integration layer) -------------------------------------- */

/* MEX_IndexFighterItem (console 803D7058): registers the fighter's article @p index as one of its
 * items, the m-ex counterpart of it_8026B3F8(article, It_Kind_*). */
void mu_ak_register_article(FighterKind kind, void* article, int index);
/* MEX_GetFtItemID (console 803D7088): the ItemKind the fighter's item @p index was given. */
ItemKind mu_ak_article_kind(HSD_GObj* fighter_gobj, int index);

/* Charizard's items, in the order of ftData->x48_items and of mu_ak_article_kind. */
enum {
    ftLz_Item_Fire = 0,      ///< Flamethrower flame (clone of Bowser's flame)
    ftLz_Item_Rock = 1,      ///< Rock Smash's held rock
    ftLz_Item_RockBurst = 2, ///< the pieces the rock breaks into
    ftLz_Item_Count = 3,
};

/* Item logic tables in the m-ex itFunction layout (== ItemLogicTable), for the integration layer. */
extern ItemLogicTable mu_ak_charizard_item_logic[ftLz_Item_Count];

/* ---- Routines ------------------------------------------------------------------------------ */

/* ftlz_init.c */
void ftLz_Init_OnLoad(HSD_GObj* gobj);
void ftLz_Init_OnDeath(HSD_GObj* gobj);
void ftLz_Init_OnUserDataRemove(HSD_GObj* gobj);
void ftLz_Init_OnItemPickup(HSD_GObj* gobj, bool catch_item);
void ftLz_Init_OnItemInvisible(HSD_GObj* gobj);
void ftLz_Init_OnItemVisible(HSD_GObj* gobj);
void ftLz_Init_OnItemDrop(HSD_GObj* gobj, bool drop_item);
void ftLz_Init_OnItemCatch(HSD_GObj* gobj, bool catch_item);
void ftLz_Init_OnItemUnknown(HSD_GObj* gobj, bool drop_item);
void ftLz_Init_OnKnockbackEnter(HSD_GObj* gobj);
void ftLz_Init_OnKnockbackExit(HSD_GObj* gobj);
void ftLz_Init_OnFrame(HSD_GObj* gobj);
void ftLz_Init_LoadSpecialAttrs(HSD_GObj* gobj);
void ftLz_Init_EnterDoubleJump(HSD_GObj* gobj);
void ftLz_Init_TailFireProc(HSD_GObj* gobj);

/* ftlz_jumpaerial.c */
void ftLz_JumpAerial_Anim(HSD_GObj* gobj);
void ftLz_JumpAerial_IASA(HSD_GObj* gobj);
void ftLz_JumpAerial_Phys(HSD_GObj* gobj);
void ftLz_JumpAerial_Coll(HSD_GObj* gobj);

/* ftlz_specialn.c */
void ftLz_SpecialN_Enter(HSD_GObj* gobj);
void ftLz_SpecialAirN_Enter(HSD_GObj* gobj);
void ftLz_SpecialN_RefuelFire(HSD_GObj* gobj);
void ftLz_SpecialNStart_Anim(HSD_GObj* gobj);
void ftLz_SpecialNStart_IASA(HSD_GObj* gobj);
void ftLz_SpecialNStart_Phys(HSD_GObj* gobj);
void ftLz_SpecialNStart_Coll(HSD_GObj* gobj);
void ftLz_SpecialN_Anim(HSD_GObj* gobj);
void ftLz_SpecialN_IASA(HSD_GObj* gobj);
void ftLz_SpecialN_Phys(HSD_GObj* gobj);
void ftLz_SpecialN_Coll(HSD_GObj* gobj);
void ftLz_SpecialNEnd_Anim(HSD_GObj* gobj);
void ftLz_SpecialNEnd_IASA(HSD_GObj* gobj);
void ftLz_SpecialNEnd_Phys(HSD_GObj* gobj);
void ftLz_SpecialNEnd_Coll(HSD_GObj* gobj);
void ftLz_SpecialAirNStart_Anim(HSD_GObj* gobj);
void ftLz_SpecialAirNStart_IASA(HSD_GObj* gobj);
void ftLz_SpecialAirNStart_Phys(HSD_GObj* gobj);
void ftLz_SpecialAirNStart_Coll(HSD_GObj* gobj);
void ftLz_SpecialAirN_Anim(HSD_GObj* gobj);
void ftLz_SpecialAirN_IASA(HSD_GObj* gobj);
void ftLz_SpecialAirN_Phys(HSD_GObj* gobj);
void ftLz_SpecialAirN_Coll(HSD_GObj* gobj);
void ftLz_SpecialAirNEnd_Anim(HSD_GObj* gobj);
void ftLz_SpecialAirNEnd_IASA(HSD_GObj* gobj);
void ftLz_SpecialAirNEnd_Phys(HSD_GObj* gobj);
void ftLz_SpecialAirNEnd_Coll(HSD_GObj* gobj);

/* ftlz_specials.c */
void ftLz_SpecialS_Enter(HSD_GObj* gobj);
void ftLz_SpecialAirS_Enter(HSD_GObj* gobj);
void ftLz_SpecialSStart_Anim(HSD_GObj* gobj);
void ftLz_SpecialSStart_IASA(HSD_GObj* gobj);
void ftLz_SpecialSStart_Phys(HSD_GObj* gobj);
void ftLz_SpecialSStart_Coll(HSD_GObj* gobj);
void ftLz_SpecialS_Anim(HSD_GObj* gobj);
void ftLz_SpecialS_IASA(HSD_GObj* gobj);
void ftLz_SpecialS_Phys(HSD_GObj* gobj);
void ftLz_SpecialS_Coll(HSD_GObj* gobj);
void ftLz_SpecialSBlown_Anim(HSD_GObj* gobj);
void ftLz_SpecialSBlown_IASA(HSD_GObj* gobj);
void ftLz_SpecialSBlown_Phys(HSD_GObj* gobj);
void ftLz_SpecialSBlown_Coll(HSD_GObj* gobj);
void ftLz_SpecialSEnd_Anim(HSD_GObj* gobj);
void ftLz_SpecialSEnd_IASA(HSD_GObj* gobj);
void ftLz_SpecialSEnd_Phys(HSD_GObj* gobj);
void ftLz_SpecialSEnd_Coll(HSD_GObj* gobj);
void ftLz_SpecialAirSStart_Anim(HSD_GObj* gobj);
void ftLz_SpecialAirSStart_IASA(HSD_GObj* gobj);
void ftLz_SpecialAirSStart_Phys(HSD_GObj* gobj);
void ftLz_SpecialAirSStart_Coll(HSD_GObj* gobj);
void ftLz_SpecialAirS_Anim(HSD_GObj* gobj);
void ftLz_SpecialAirS_IASA(HSD_GObj* gobj);
void ftLz_SpecialAirS_Phys(HSD_GObj* gobj);
void ftLz_SpecialAirS_Coll(HSD_GObj* gobj);
void ftLz_SpecialAirSBlown_Anim(HSD_GObj* gobj);
void ftLz_SpecialAirSBlown_IASA(HSD_GObj* gobj);
void ftLz_SpecialAirSBlown_Phys(HSD_GObj* gobj);
void ftLz_SpecialAirSBlown_Coll(HSD_GObj* gobj);
void ftLz_SpecialAirSEnd_Anim(HSD_GObj* gobj);
void ftLz_SpecialAirSEnd_IASA(HSD_GObj* gobj);
void ftLz_SpecialAirSEnd_Phys(HSD_GObj* gobj);
void ftLz_SpecialAirSEnd_Coll(HSD_GObj* gobj);

/* ftlz_specialhi.c */
void ftLz_SpecialHi_Enter(HSD_GObj* gobj);
void ftLz_SpecialAirHi_Enter(HSD_GObj* gobj);
void ftLz_SpecialHi_Anim(HSD_GObj* gobj);
void ftLz_SpecialHi_IASA(HSD_GObj* gobj);
void ftLz_SpecialHi_Phys(HSD_GObj* gobj);
void ftLz_SpecialHi_Coll(HSD_GObj* gobj);
void ftLz_SpecialAirHi_Anim(HSD_GObj* gobj);
void ftLz_SpecialAirHi_IASA(HSD_GObj* gobj);
void ftLz_SpecialAirHi_Phys(HSD_GObj* gobj);
void ftLz_SpecialAirHi_Coll(HSD_GObj* gobj);

/* ftlz_speciallw.c */
void ftLz_SpecialLw_Enter(HSD_GObj* gobj);
void ftLz_SpecialAirLw_Enter(HSD_GObj* gobj);
void ftLz_SpecialLw_Anim(HSD_GObj* gobj);
void ftLz_SpecialLw_IASA(HSD_GObj* gobj);
void ftLz_SpecialLw_Phys(HSD_GObj* gobj);
void ftLz_SpecialLw_Coll(HSD_GObj* gobj);
void ftLz_SpecialAirLw_Anim(HSD_GObj* gobj);
void ftLz_SpecialAirLw_IASA(HSD_GObj* gobj);
void ftLz_SpecialAirLw_Phys(HSD_GObj* gobj);
void ftLz_SpecialAirLw_Coll(HSD_GObj* gobj);
void ftLz_SpecialLw_DestroyRock(HSD_GObj* gobj);

/* itlizardon.c: the three items and their spawn functions */
HSD_GObj* itLzFire_Spawn(HSD_GObj* owner, Vec3* pos, u32 hit_id, int gfx, int kind,
                         float facing_dir, float speed_scale, float size_scale);
HSD_GObj* itLzRock_Spawn(HSD_GObj* owner, Vec3* pos, int part, int kind, float facing_dir,
                         int variant);
HSD_GObj* itLzRockBurst_Spawn(HSD_GObj* owner, Vec3* pos, int part, int kind,
                              float facing_dir);

#endif
