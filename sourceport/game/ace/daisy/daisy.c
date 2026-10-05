/* ACE's Daisy: load, the per-fighter events, the float, the trail block and the move table
 * (m-ex "ftFunction" of PlDa.dat).
 *
 * PlDa.dat exports twelve ftFunction slots: 0 onload, 1 ondeath, 3 move_logic, 4 specialn,
 * 5 specialairn, 6 specials, 7 specialairs, 10 speciallw, 11 specialairlw, 31 enterfloat,
 * 34 onlanding and 45 (the trail block). Every other slot that is set in MxDt is Peach's
 * function (the up special on the ground and in the air, the item and knockback events, the
 * attribute reload, Peach's double jump and her forward smash), which the registry fills from
 * MxDt when a field here is NULL.
 *
 * The file has no m-ex CPU helper: a CPU Daisy runs the game's CPU step under her own kind,
 * as on the console. */
#include "daisy.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_ItemParasolFallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_ItemParasolOpen.h>
#include <melee/ft/kinds/ftMars/types.h>
#include <melee/ft/kinds/ftPeach/ftpeach.h>
#include <melee/ft/kinds/ftPeach/ftpeachattacks4.h>
#include <melee/ft/kinds/ftPeach/ftpeachfloat.h>
#include <melee/ft/kinds/ftPeach/ftpeachfloatattack.h>
#include <melee/ft/kinds/ftPeach/ftpeachfloatfall.h>
#include <melee/ft/kinds/ftPeach/ftpeachspecialhi.h>
#include <melee/ft/kinds/ftPeach/ftpeachspeciallw.h>
#include <melee/ft/kinds/ftPeach/ftpeachspecialn.h>
#include <melee/ft/kinds/ftPeach/ftpeachspecials.h>
#include <melee/ft/types.h>
#include <melee/it/it_26B1.h>
#include <melee/lb/lbanim.h>
#include <sysdolphin/baselib/gobj.h>

/* ---- load ---------------------------------------------------------------------------------------
 * code+0x0, [onload]. Retail ftPe_Init_OnLoad with these differences: only two of Peach's five
 * retail item kinds get an article (the side special's explosion and the parasol, the two
 * items Peach's retail code still creates for her), and the first five articles are handed to
 * the item code by index (m-ex MEX_IndexFighterItem at 0x803D7058). Articles 0 and 2 are so
 * registered twice, under Peach's kind and under her own.
 *
 * As on the console, a Peach and a Daisy in one match share the two retail kinds: the one
 * loaded last gives both of them its explosion and its parasol. */
static void ftDa_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftPe_DatAttrs* ext_attrs = DP(fp->ft_data->ext_attr);
    DISC_PTR(void)* items = DP(fp->ft_data->x48_items);

    ext_attrs->floatfallf_anim_start = lbAnim_8001E8F8(ftData_80085E50(fp, 18));
    ext_attrs->floatfallb_anim_start = lbAnim_8001E8F8(ftData_80085E50(fp, 19));
    PUSH_ATTRS(fp, ftPe_DatAttrs);

    it_8026B3F8(DP(items[ftDa_Article_Explode]), It_Kind_Peach_Explode);
    it_8026B3F8(DP(items[ftDa_Article_Parasol]), It_Kind_Peach_Parasol);

    mu_ak_register_article(fp->kind, DP(items[ftDa_Article_Explode]), ftDa_Article_Explode);
    mu_ak_register_article(fp->kind, DP(items[ftDa_Article_Veg]), ftDa_Article_Veg);
    mu_ak_register_article(fp->kind, DP(items[ftDa_Article_Parasol]), ftDa_Article_Parasol);
    mu_ak_register_article(fp->kind, DP(items[ftDa_Article_Toad]), ftDa_Article_Toad);
    mu_ak_register_article(fp->kind, DP(items[ftDa_Article_Spore]), ftDa_Article_Spore);

    {
        /* Bring-up evidence (once). */
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[ak] Daisy load: article kinds veg %d, toad %d, spore %d; retail kinds "
                     "%d (explosion) and %d (parasol) take her articles 0 and 2\n",
                     mu_ak_item_kind(fp->kind, ftDa_Article_Veg),
                     mu_ak_item_kind(fp->kind, ftDa_Article_Toad),
                     mu_ak_item_kind(fp->kind, ftDa_Article_Spore),
                     (int) It_Kind_Peach_Explode, (int) It_Kind_Peach_Parasol);
        }
    }
}

_Static_assert(It_Kind_Peach_Explode == 0x62 && It_Kind_Peach_Parasol == 0x67,
               "the two retail item kinds PlDa.dat registers at code+0x64 and +0x80");

/* code+0xF4, [ondeath] is ftPe_Init_OnDeath instruction for instruction (the seven fighter
 * variables, the four model parts, the three parts chosen by costume 1), so Peach's function
 * is named in the descriptor. */

/* ---- the float ----------------------------------------------------------------------------------
 * code+0x93C, [enterfloat] (m-ex onFloat; called from ftPe_8011BA54 and ftPe_8011BAD8 once the
 * input is there). With the float available it is ftPe_8011BB6C: state 341, the float used
 * up, the timer from the attributes, the upward speed cleared, the flag at fp+2219 and
 * Peach's effect 1236 on the TransN joint.
 *
 * The m-ex caller returns whatever this leaves in r3 as "entered" (codes at 0x8011BA54 and
 * 0x8011BAD8). With no float left the routine returns at once with the GObj still in r3, so
 * the caller reports "entered" although nothing happened; with the float it ends in a jump to
 * efAsync_Spawn, which leaves the current proc in r3 when called from a fighter proc. Both
 * are not zero, so this returns true on both paths. */
static bool ftDa_Float_Enter(HSD_GObj* gobj, int arg1)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->u.pe.has_float != true) {
        return true;
    }
    ftPe_8011BB6C(gobj, arg1);
    return true;
}

/* code+0xA24, [onlanding]: the float is back. */
static void ftDa_Init_OnLanding(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->u.pe.has_float = true;
}

/* code+0xA54, the collision callback of state 341. Peach's ftPe_Float_Coll passes
 * ftCo_80096CC8 as the first callback; hers passes none. */
void ftDa_Float_Coll(HSD_GObj* gobj)
{
    ft_800831CC(gobj, NULL, ft_80082B1C);
}

/* ---- the Toad, the parasol and the pulled item going away -----------------------------------------
 * code+0x1BA4, the death and damage callback her own specials install (Peach's
 * ftPe_Init_OnDeath2 with her own Toad removal): the parasol (code+0x1F08, the same body as
 * ftPe_8011D598), the Toad, then Peach's ftPe_SpecialLw_8011CFA0, which only removes a retail
 * turnip and so does nothing to her own pulled article. */
void ftDa_Init_OnDeath2(HSD_GObj* gobj)
{
    Fighter* fp;

    ftPe_8011D598(gobj);
    fp = GET_FIGHTER(gobj);
    if (fp->u.pe.toad_gobj != NULL) {
        itDa_Toad_Remove(fp->u.pe.toad_gobj);
        ftDa_SpecialN_ToadGone(gobj);
    }
    ftPe_SpecialLw_8011CFA0(gobj);
}

/* ---- the trail block ----------------------------------------------------------------------------
 * code+0x148C, 32 bytes, and code+0xA34 (slot 45, GetTrailData), which returns its address.
 * Console byte order, read through the type the afterimage code uses:
 *   widths 0.0 and 0.0, alpha 255 to 0, inner color F5 D4 42, outer color 00 F0 7D, the byte
 *   at +10 is 0x0A, bone 74, and 0.0 and 2.2 for fp+20F8 and fp+20FC. */
static const struct SwordAttrs ftDa_Init_TrailData = {
    0.0f, 0.0f, 0xFF, 0x00, 0xF5, 0xD4, 0x42, 0x00, 0xF0, 0x7D, 0x0A,
    { 0x00, 0xFF, 0xFF }, 74, 0.0f, 2.2f,
};

_Static_assert(sizeof(struct SwordAttrs) == 0x20, "the trail block is 32 bytes");

static struct SwordAttrs* ftDa_Init_GetTrailData(HSD_GObj* gobj)
{
    (void) gobj;
    return (struct SwordAttrs*) &ftDa_Init_TrailData;
}

/* ---- the state table ----------------------------------------------------------------------------
 * code+0x208, [move_logic]: 30 rows, compared word for word with retail
 * ftPe_Init_MotionStateTable (main.dol 0x803CCCB8, tables.txt). The animation ids, flags and
 * move ids are Peach's in every row. 26 callbacks in 8 rows are code of the file: each one is
 * marked with its offset. Where that code makes the same calls as Peach's function, Peach's
 * function is named (15); the eleven that differ are hers. */
const MotionState ftDa_Init_MotionStateTable[ftDa_MS_SelfCount] = {
    {
        // ftPe_MS_Float = 341
        ftPe_SM_Float,
        Ft_MF_None,
        FtMoveId_Default << 24,
        ftPe_Float_Anim,
        ftPe_Float_IASA,
        ftPe_Float_Phys, /* code+0xA40: the same call */
        ftDa_Float_Coll, /* code+0xA54 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_FloatFallF = 342
        ftPe_SM_FloatFallF,
        Ft_MF_None,
        FtMoveId_Default << 24,
        ftPe_FloatFall_Anim,
        ftPe_FloatFall_IASA,
        ftPe_FloatFall_Phys,
        ftPe_FloatFall_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_FloatFallB = 343
        ftPe_SM_FloatFallB,
        Ft_MF_None,
        FtMoveId_Default << 24,
        ftPe_FloatFall_Anim,
        ftPe_FloatFall_IASA,
        ftPe_FloatFall_Phys,
        ftPe_FloatFall_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_FloatAttackAirN = 344
        ftCo_SM_AttackAirN,
        ftPe_MF_FloatAttackAirN,
        FtMoveId_AttackAirN << 24,
        ftPe_FloatAttackAir_Anim,
        ftPe_FloatAttackAir_IASA,
        ftPe_FloatAttackAir_Phys,
        ftPe_FloatAttackAir_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_FloatAttackAirF = 345
        ftCo_SM_AttackAirF,
        ftPe_MF_Move_14,
        FtMoveId_AttackAirF << 24,
        ftPe_FloatAttackAir_Anim,
        ftPe_FloatAttackAir_IASA,
        ftPe_FloatAttackAir_Phys,
        ftPe_FloatAttackAir_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_FloatAttackAirB = 346
        ftCo_SM_AttackAirB,
        ftPe_MF_FloatAttackAirB,
        FtMoveId_AttackAirB << 24,
        ftPe_FloatAttackAir_Anim,
        ftPe_FloatAttackAir_IASA,
        ftPe_FloatAttackAir_Phys,
        ftPe_FloatAttackAir_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_FloatAttackAirHi = 347
        ftCo_SM_AttackAirHi,
        ftPe_MF_FloatAttackAirHi,
        FtMoveId_AttackAirHi << 24,
        ftPe_FloatAttackAir_Anim,
        ftPe_FloatAttackAir_IASA,
        ftPe_FloatAttackAir_Phys,
        ftPe_FloatAttackAir_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_FloatAttackAirLw = 348
        ftCo_SM_AttackAirLw,
        ftPe_MF_Move_17,
        FtMoveId_AttackAirLw << 24,
        ftPe_FloatAttackAir_Anim,
        ftPe_FloatAttackAir_IASA,
        ftPe_FloatAttackAir_Phys,
        ftPe_FloatAttackAir_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_AttackS4Club = 349
        ftPe_SM_AttackS4_0,
        ftPe_MF_AttackS4,
        FtMoveId_AttackS4 << 24,
        ftPe_AttackS4_Anim,
        ftPe_AttackS4_IASA,
        ftPe_AttackS4_Phys,
        ftPe_AttackS4_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_AttackS4Pan = 350
        ftPe_SM_AttackS4_1,
        ftPe_MF_AttackS4,
        FtMoveId_AttackS4 << 24,
        ftPe_AttackS4_Anim,
        ftPe_AttackS4_IASA,
        ftPe_AttackS4_Phys,
        ftPe_AttackS4_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_AttackS4Racket = 351
        ftPe_SM_AttackS4_2,
        ftPe_MF_AttackS4,
        FtMoveId_AttackS4 << 24,
        ftPe_AttackS4_Anim,
        ftPe_AttackS4_IASA,
        ftPe_AttackS4_Phys,
        ftPe_AttackS4_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialLw = 352
        ftPe_SM_SpecialLw,
        ftPe_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftPe_SpecialLw_Anim, /* code+0xA64: the same body */
        NULL,
        ftPe_SpecialLw_Phys, /* code+0xAD4: the same call */
        ftDa_SpecialLw_Coll, /* code+0xAD8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialAirLw = 353
        ftPe_SM_SpecialLw,
        ftPe_MF_SpecialLw,
        FtMoveId_SpecialLw << 24,
        ftPe_SpecialAirLw_Anim, /* code+0xAF0: the same body */
        NULL,
        ftPe_SpecialAirLw_Phys, /* code+0xB60: the same call */
        ftDa_SpecialAirLw_Coll, /* code+0xB70 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialSStart = 354
        ftPe_SM_SpecialSStart,
        ftPe_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftPe_SpecialSStart_Anim, /* code+0xB7C: the same body (NOTES.md, assumed 2) */
        ftPe_SpecialSStart_IASA, /* code+0xC5C: empty, as Peach's */
        ftPe_SpecialSStart_Phys,
        ftPe_SpecialSStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialSEnd = 355
        ftPe_SM_SpecialSEnd,
        ftPe_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        ftPe_SpecialSEnd_Anim,
        ftPe_SpecialSEnd_IASA,
        ftPe_SpecialSEnd_Phys,
        ftPe_SpecialSEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialSJump = 356
        ftPe_SM_SpecialSJump,
        ftPe_MF_SpecialS,
        FtMoveId_SpecialS << 24,
        NULL,
        NULL,
        NULL,
        NULL,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialAirSStart = 357
        ftPe_SM_SpecialAirSStart,
        ftPe_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftPe_SpecialAirSStart_Anim,
        ftPe_SpecialAirSStart_IASA,
        ftPe_SpecialAirSStart_Phys,
        ftPe_SpecialAirSStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialAirSEnd_0 = 358
        ftPe_SM_SpecialAirSEnd_0,
        ftPe_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftPe_SpecialAirSEnd_Anim,
        ftPe_SpecialAirSEnd_IASA,
        ftPe_SpecialAirSEnd_Phys,
        ftPe_SpecialAirSEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialAirSEnd_1 = 359
        ftPe_SM_SpecialAirSEnd_1,
        ftPe_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftPe_SpecialAirSEnd_Anim,
        ftPe_SpecialAirSEnd_IASA,
        ftPe_SpecialAirSEnd_Phys,
        ftPe_SpecialAirSEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialAirSJump = 360
        ftPe_SM_SpecialSJump,
        ftPe_MF_SpecialAirS,
        FtMoveId_SpecialS << 24,
        ftPe_SpecialAirSJump_Anim,
        ftPe_SpecialAirSJump_IASA,
        ftPe_SpecialAirSJump_Phys,
        ftPe_SpecialAirSJump_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialHiStart = 361
        ftPe_SM_SpecialHiStart,
        ftPe_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftPe_SpecialHiStart_Anim,
        ftPe_SpecialHiStart_IASA,
        ftPe_SpecialHiStart_Phys,
        ftPe_SpecialHiStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialHiEnd = 362
        ftPe_SM_SpecialHiEnd,
        ftPe_MF_SpecialHi,
        FtMoveId_SpecialHi << 24,
        ftPe_SpecialHiEnd_Anim,
        ftPe_SpecialHiEnd_IASA,
        ftPe_SpecialHiEnd_Phys,
        ftPe_SpecialHiEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialAirHiStart = 363
        ftPe_SM_SpecialAirHiStart,
        ftPe_MF_SpecialAirHi,
        FtMoveId_SpecialHi << 24,
        ftPe_SpecialAirHiStart_Anim,
        ftPe_SpecialAirHiStart_IASA,
        ftPe_SpecialAirHiStart_Phys,
        ftPe_SpecialAirHiStart_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialAirHiEnd = 364
        ftPe_SM_SpecialAirHiEnd,
        ftPe_MF_SpecialAirHi,
        FtMoveId_SpecialHi << 24,
        ftPe_SpecialAirHiEnd_Anim,
        ftPe_SpecialAirHiEnd_IASA,
        ftPe_SpecialAirHiEnd_Phys,
        ftPe_SpecialAirHiEnd_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialN = 365
        ftPe_SM_SpecialN,
        ftPe_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftDa_SpecialN_Anim, /* code+0xC60 */
        ftPe_SpecialN_IASA, /* code+0xD70: empty */
        ftPe_SpecialN_Phys, /* code+0xD74: the same body */
        ftDa_SpecialN_Coll, /* code+0xDB0 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialNHit = 366
        ftPe_SM_SpecialNHit,
        ftPe_MF_SpecialN,
        FtMoveId_SpecialN << 24,
        ftDa_SpecialNHit_Anim, /* code+0xDFC */
        ftPe_SpecialNHit_IASA, /* code+0xE70: empty */
        ftPe_SpecialNHit_Phys, /* code+0xE74: the same call */
        ftDa_SpecialNHit_Coll, /* code+0xE78 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialAirN = 367
        ftPe_SM_SpecialAirN,
        ftPe_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftDa_SpecialAirN_Anim, /* code+0xEC4 */
        ftPe_SpecialAirN_IASA, /* code+0xFE0: empty */
        ftPe_SpecialAirN_Phys, /* code+0xFE4: the same body */
        ftDa_SpecialAirN_Coll, /* code+0x10D8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_SpecialAirNHit = 368
        ftPe_SM_SpecialAirNHit,
        ftPe_MF_SpecialAirN,
        FtMoveId_SpecialN << 24,
        ftDa_SpecialAirNHit_Anim, /* code+0x1124 */
        ftPe_SpecialAirNHit_IASA, /* code+0x1198: empty */
        ftPe_SpecialAirNHit_Phys, /* code+0x119C: the same body */
        ftDa_SpecialAirNHit_Coll, /* code+0x11E8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_ItemParasolOpen = 369
        ftPe_SM_ItemParasolOpen,
        ftPe_MF_ParasolOpen,
        (FtMoveId_Parasol << 24) | (1 << 23),
        ftCo_ItemParasolOpen_Anim,
        ftCo_ItemParasolOpen_IASA,
        ftCo_ItemParasolOpen_Phys,
        ftCo_ItemParasolOpen_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        // ftPe_MS_ItemParasolFall = 370
        ftPe_SM_ItemParasolFall,
        ftPe_MF_ParasolFallSpecial,
        (FtMoveId_Parasol << 24) | (1 << 23),
        ftCo_ItemParasolFallSpecial_Anim,
        ftCo_ItemParasolFallSpecial_IASA,
        ftCo_ItemParasolFallSpecial_Phys,
        ftCo_ItemParasolFallSpecial_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* Every slot not named here is NULL: the registry then takes MxDt's default, which is Peach's
 * function for all of them (run-source/rel09-ace-native/daisy/mxdt.txt): the up special, the
 * six item events, the two knockback events, the attribute reload, ftPe_JumpAerial_Enter and
 * ftPe_AttackS4_Enter. Two exported slots name Peach's function because the file's own code
 * is the same: ondeath and specialairs. Nothing has run yet, so she is not marked
 * MU_AK_READY. */
const MuAkFighter mu_ak_daisy = {
    .name = "Daisy",
    .file = "PlDa.dat",
    .onload = ftDa_Init_OnLoad,
    .ondeath = ftPe_Init_OnDeath,          /* code+0xF4: the same body */
    .specialn = ftDa_SpecialN_Enter,
    .specialairn = ftDa_SpecialAirN_Enter,
    .specials = ftDa_SpecialS_Enter,
    .specialairs = ftPe_SpecialAirS_Enter, /* code+0x778: the same body */
    .speciallw = ftDa_SpecialLw_Enter,
    .specialairlw = ftDa_SpecialAirLw_Enter,
    .enterfloat = (MuAkEvent) (void (*)(void)) ftDa_Float_Enter,
    .onlanding = ftDa_Init_OnLanding,
    .move_logic = ftDa_Init_MotionStateTable,
    .move_logic_count = ftDa_MS_SelfCount,
    .flags = 0,
    .article_tables = itDa_ArticleTables,
    .article_count = ftDa_Article_Count,
    .gettraildata = (MuAkEvent) (void (*)(void)) ftDa_Init_GetTrailData,
};
