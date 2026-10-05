/* ACE's Toad: load, the events the file exports and the state table (m-ex "ftFunction" of
 * PlTd.dat).
 *
 * PlTd.dat exports 13 ftFunction slots: 0 onload, 1 ondeath, 2 onunknown (the destroy event),
 * 3 move_logic, 4 to 11 the special entries, 23 onframe (a lone return). Every other slot MxDt
 * sets is Mario's function (the item and knockback events, the attribute reload, the common
 * double jump, the two demo hooks and the demo state table), which the registry fills from
 * Mario's rows because the fields are left NULL here. The file carries no CPU helper. */
#include "toad.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftMario/ftmario.h>
#include <melee/ft/kinds/ftMario/ftmariospecialhi.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>

/* ftData->x48_items: an array of article pointers (disc data). */
typedef DISC_PTR(void) ftTd_ArticleSlot;

/* +0000 (slot 0, onload). In the file's order: the meter object (+075C), the seven common
 * attributes saved to the file's block at +08A0, the attribute copy (0x70 bytes into fp+2D8,
 * which becomes fp+2D4), article 0 handed to the item code by index (m-ex MEX_IndexFighterItem
 * at 0x803D7058), the first model part override cleared.
 *
 * One native addition, not in the file: the wall jump bit. On the console the m-ex code at
 * ftWallJump_8008169C+28 reads MxDt's byte of the fighter (1 for internal 57) in place of the
 * bit, and the file's load does not set the bit. The native check reads the bit, so the byte is
 * copied into it (as ACE's Metal Mario does). */
static void ftTd_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTd_DatAttrs* src = (ftTd_DatAttrs*) DP(fp->ft_data->ext_attr);
    ftTd_ArticleSlot* items;

    ftTd_Meter(fp)->meter = ftTd_Meter_Create(gobj);
    ftTd_Meter_SaveBase(fp);

    *(ftTd_DatAttrs*) fp->dat_attrs_backup = *src;
    fp->dat_attrs = fp->dat_attrs_backup;

    items = (ftTd_ArticleSlot*) DISC_GET(void, fp->ft_data->x48_items);
    mu_ak_register_article(fp->kind, DP(items[ftTd_Article_Shot]), ftTd_Article_Shot);

    ftParts_80074A4C(gobj, 0, 0);

    if (mu_mex_fighter_walljump(mu_ak_mex_internal(fp->kind)) > 0) {
        fp->can_walljump = true;
    }

    {
        /* Bring-up evidence (once). */
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[ak] Toad load: shot article kind %d, meter object %s, wall jump %d\n",
                     mu_ak_item_kind(fp->kind, ftTd_Article_Shot),
                     ftTd_Meter(fp)->meter != NULL ? "made" : "NOT made",
                     (int) fp->can_walljump);
        }
    }
}

/* +00BC (slot 1, ondeath): Mario's retail routine, called by address (0x800E08CC), then the
 * meter emptied. */
static void ftTd_Init_OnDeath(HSD_GObj* gobj)
{
    Fighter* fp;

    ftMr_Init_OnDeath(gobj);
    fp = GET_FIGHTER(gobj);
    ftTd_Meter(fp)->level = 0;
    ftTd_Meter(fp)->timer = 0;
}

/* The meter object is made with process link 0 (the fourth argument of the retail helper). */
#define FTTD_METER_PLINK 0

/* +0104 (slot 2, the destroy event): the meter object freed. The console frees the pointer as
 * it is. Two native guards, neither in the file: the pointer is NULL when the object could not
 * be made, and when a scene is torn down the objects of link 0 go before the fighters, so the
 * meter may be gone already: it is freed only while it is still in its list. */
static void ftTd_Init_OnUserDataRemove(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTd_MeterVars* mv = ftTd_Meter(fp);
    HSD_GObj* it;

    if (mv->meter != NULL) {
        for (it = HSD_GObjPLinkHead[FTTD_METER_PLINK]; it != NULL; it = it->next) {
            if (it == mv->meter) {
                HSD_GObjFree(mv->meter);
                break;
            }
        }
    }
    OSReport("Cleared meter in on destroy, meter gobj = %p\n", (void*) mv->meter);
    mv->meter = NULL;
}

/* +0758 (slot 23, onframe): a lone return. */
static void ftTd_Init_OnFrame(HSD_GObj* gobj)
{
    (void) gobj;
}

/* +0144 (slot 3, move_logic): ten rows. Rows 0 and 1 are empty, as Mario's. Animation ids (295
 * to 302), flag words and move ids are the words of the file and equal Mario's rows, so Mario's
 * names stand for them (tables.txt compares every word with retail ftMr_Init_MotionStateTable).
 * The callbacks are the file's own; where a routine makes the same calls as Mario's function
 * the retail function is named. */
const MotionState ftTd_Init_MotionStateTable[ftMr_MS_SelfCount] = {
    {
        // ftMr_MS_AppealSR = 341: empty
        ftCo_SM_None,
        Ft_MF_None,
        FtMoveId_Default << 24,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
    },
    {
        // ftMr_MS_AppealSL = 342: empty
        ftCo_SM_None,
        Ft_MF_None,
        FtMoveId_Default << 24,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
    },
    {
        // ftMr_MS_SpecialN = 343
        ftMr_SM_SpecialN,
        0x00340111,
        FtMoveId_SpecialN << 24,
        ftTd_SpecialN_Anim, /* +08EC */
        ftTd_SpecialN_IASA, /* +0938 */
        ftTd_SpecialN_Phys, /* +093C */
        ftTd_SpecialN_Coll, /* +0940 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialAirN = 344
        ftMr_SM_SpecialAirN,
        0x00340511,
        FtMoveId_SpecialN << 24,
        ftTd_SpecialAirN_Anim, /* +09D0 */
        ftTd_SpecialN_IASA,    /* +0A1C: a lone return */
        ftTd_SpecialAirN_Phys, /* +0A20 */
        ftTd_SpecialAirN_Coll, /* +0A24 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialS = 345
        ftMr_SM_SpecialS,
        0x00341012,
        FtMoveId_SpecialS << 24,
        ftTd_SpecialS_Anim, /* +0AB4 */
        ftTd_SpecialN_IASA, /* +0B18: a lone return */
        ftTd_SpecialS_Phys, /* +0B1C */
        ftTd_SpecialS_Coll, /* +0C8C */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialAirS = 346
        ftMr_SM_SpecialAirS,
        0x00341012,
        FtMoveId_SpecialS << 24,
        ftTd_SpecialAirS_Anim, /* +0E10 */
        ftTd_SpecialN_IASA,    /* +0E94: a lone return */
        ftTd_SpecialAirS_Phys, /* +0E98 */
        ftTd_SpecialAirS_Coll, /* +0F70 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialHi = 347
        ftMr_SM_SpecialHi,
        0x00340213,
        FtMoveId_SpecialHi << 24,
        ftTd_SpecialHi_Anim, /* +10F0 */
        ftTd_SpecialHi_IASA, /* +1164 */
        ftMr_SpecialHi_Phys, /* +128C: the same two calls as retail 0x800E1E74 */
        ftTd_SpecialHi_Coll, /* +12A4 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialAirHi = 348
        ftMr_SM_SpecialAirHi,
        0x00340613,
        FtMoveId_SpecialHi << 24,
        ftTd_SpecialHi_Anim,    /* +12FC: +10F0 again */
        ftTd_SpecialHi_IASA,    /* +1370: a branch to +1164 */
        ftTd_SpecialAirHi_Phys, /* +1374 */
        ftTd_SpecialHi_Coll,    /* +140C: +12A4 again */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialLw = 349
        ftMr_SM_SpecialLw,
        0x00340214,
        FtMoveId_SpecialLw << 24,
        ftTd_SpecialLw_Anim, /* +1464 */
        ftTd_SpecialN_IASA,  /* +14F0: a lone return */
        ftTd_SpecialLw_Phys, /* +14F4 */
        ftTd_SpecialLw_Coll, /* +14F8 */
        ftCamera_UpdateCameraBox,
    },
    {
        // ftMr_MS_SpecialAirLw = 350
        ftMr_SM_SpecialAirLw,
        0x00340614,
        FtMoveId_SpecialLw << 24,
        ftTd_SpecialAirLw_Anim, /* +1544 */
        ftTd_SpecialN_IASA,     /* +1590: a lone return */
        ftTd_SpecialAirLw_Phys, /* +1594 */
        ftTd_SpecialAirLw_Coll, /* +1598 */
        ftCamera_UpdateCameraBox,
    },
};

/* The 13 slots PlTd.dat exports. Every other field is NULL: the registry takes MxDt's default,
 * Mario's function (run-source/rel09-ace-native/toad/mxdt.txt). Written and never run, so not
 * MU_AK_READY. */
const MuAkFighter mu_ak_toad = {
    .name = "Toad",
    .file = "PlTd.dat",

    .onload = ftTd_Init_OnLoad,
    .ondeath = ftTd_Init_OnDeath,
    .onunknown = ftTd_Init_OnUserDataRemove,
    .specialn = ftTd_SpecialN_Enter,
    .specialairn = ftTd_SpecialAirN_Enter,
    .specials = ftTd_SpecialS_Enter,
    .specialairs = ftTd_SpecialAirS_Enter,
    .specialhi = ftTd_SpecialHi_Enter,
    .specialairhi = ftTd_SpecialAirHi_Enter,
    .speciallw = ftTd_SpecialLw_Enter,
    .specialairlw = ftTd_SpecialAirLw_Enter,
    .onframe = ftTd_Init_OnFrame,

    .move_logic = ftTd_Init_MotionStateTable,
    .move_logic_count = ftMr_MS_SelfCount,

    .flags = 0,
    .articles = itTd_Articles,
    .article_count = ftTd_Article_Count,
};
