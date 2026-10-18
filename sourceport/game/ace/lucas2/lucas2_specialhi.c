/* PlLc2.dat native C. Ness layouts, local callbacks. Review status in PROGRESS.md. */
#include "lucas2_specialhi.h"

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/mp/forward.h>

#include <math.h>
#include <placeholder.h>

#include "lucas2.h"
#include <melee/ft/kinds/ftNess/ftnessspecialhi.h>
#include <melee/ft/kinds/ftNess/types.h>
#include <dolphin/mtx.h>
#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_DownBound.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/itnesspkthunderball.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbvector.h>

#define FTNESS_SPECIALHI_COLL_FLAG                                            \
    Ft_MF_SkipMatAnim | Ft_MF_SkipColAnim | Ft_MF_UpdateCmd |                 \
        Ft_MF_SkipItemVis | Ft_MF_Unk19 | Ft_MF_SkipModelPartVis |            \
        Ft_MF_SkipModelFlags | Ft_MF_Unk27

#define FTNESS_JIBAKU_COLL_FLAG                                               \
    Ft_MF_KeepGfx | Ft_MF_SkipHit | Ft_MF_SkipMatAnim | Ft_MF_SkipColAnim |   \
        Ft_MF_UpdateCmd | Ft_MF_SkipItemVis | Ft_MF_Unk19 |                   \
        Ft_MF_SkipModelPartVis | Ft_MF_SkipModelFlags | Ft_MF_Unk27

#ifdef MUST_MATCH
static void order_sdata2(void)
{
    (void) 5.0f;
    (void) 0.0f;
    (void) 8.333333015441895f;
    (void) 12.333333015441895f;
    (void) M_PI_2_F;
    (void) M_TAU;
    (void) M_PI;
    (void) M_PI_2;
    (void) 1.0f;
    (void) 0.0;
    (void) MTXDegToRad(1);
    (void) 90.0f;
    (void) -1.0f;
    (void) 0.5f;
    (void) 0.5;
    (void) 3.0;
    (void) 1e-4f;
    (void) -1e-4f;
    (void) -M_PI_2;
}
#endif

void ftLc2_SpecialHiStopGFX(HSD_GObj* gobj) 
{
    Fighter* fp = GET_FIGHTER(gobj);

    switch (fp->motion_id) {
    case ftNs_MS_SpecialHiStart:
    case ftNs_MS_SpecialHiHold:
    case ftNs_MS_SpecialHiEnd:
    case ftNs_MS_SpecialHi:
    case ftNs_MS_SpecialAirHiStart:
    case ftNs_MS_SpecialAirHiHold:
    case ftNs_MS_SpecialAirHiEnd:
    case ftNs_MS_SpecialAirHi:
    case ftNs_MS_SpecialAirHiRebound:
        efLib_DestroyAll(gobj);
        fp->u.ns.pkthunder_gfx = false;
    }
}

static inline void startGFX(HSD_GObj* gobj, int id)
{
    Fighter* fp = gobj->user_data;
    ftLc2_SpecialHiStopGFX(gobj);
    efSync_Spawn(id, gobj, fp->parts[FtPart_HipN].joint);
    fp->u.ns.pkthunder_gfx = true;
}

static inline bool check_distance(Vec3* pos, Vec3* pair)
{
    if ((ABS(pos->x - pair->x) < 8.333333015441895f) &&
        (ABS(pos->y - pair->y) < 12.333333015441895f))
    {
        return true;
    }
    return false;
}

bool ftLc2_SpecialHi_ItemPKThunder_CheckNessCollide(HSD_GObj* gobj)
{
    u8 unused0[4];

    Vec3 pair;
    Vec3 pos;

    u8 unused1[8];

    Vec3 pair2;
    Vec3 pos2;

    u8 unused2[36];

    Fighter* fp = GET_FIGHTER(gobj);
    s32 ret = false;

    if (!fp->u.ns.pkthunder_gobj) {
        return false;
    }

    switch (fp->mv.ns.specialhi.thunderColl) {
    case 0:
        pos = fp->cur_pos;
        pos.y += 5.0f * fp->x34_scale.y;
        it_802AB3F0(fp->u.ns.pkthunder_gobj, &pair, 0);
        if (check_distance(&pos, &pair) == true) {
            fp->mv.ns.specialhi.thunderColl = 2;
            it_802AB3F0(fp->u.ns.pkthunder_gobj, &fp->mv.ns.specialhi.collPos1,
                        0);
            it_802AB3F0(fp->u.ns.pkthunder_gobj, &fp->mv.ns.specialhi.collPos2,
                        1);
            ret = true;
        }
        break;

    case 1:
        pos2 = fp->cur_pos;
        pos2.y += 5.0f * fp->x34_scale.y;
        it_802AB3F0(fp->u.ns.pkthunder_gobj, &pair2, 0);
        if (!check_distance(&pos2, &pair2)) {
            fp->mv.ns.specialhi.thunderColl = 0;
        }
    }
    return ret;
}

static void ftLc2_SpecialAirHi_CollisionModVel(
    HSD_GObj* gobj,
    CollData* coll_data) 
                         
{
    Vec3 sp14;
    Fighter* fp;
    float phi_f1;
    float phi_f3;

    fp = GET_FIGHTER(gobj);
    while (fp->mv.ns.specialhi.aerialVel < 0.0f) {
        fp->mv.ns.specialhi.aerialVel += M_TAU;
    }
    while (fp->mv.ns.specialhi.aerialVel > M_TAU) {
        fp->mv.ns.specialhi.aerialVel -= M_TAU;
    }
    if (coll_data->env_flags & Collide_LeftWallMask) {
        phi_f1 = atan2f(coll_data->left_facing_wall.normal.y,
                        coll_data->left_facing_wall.normal.x);
        while (phi_f1 < 0.0f) {
            phi_f1 += M_TAU;
        }

        while (phi_f1 > M_TAU) {
            phi_f1 -= M_TAU;
        }
        phi_f3 = M_PI + fp->mv.ns.specialhi.aerialVel;

        while (phi_f3 < 0.0f) {
            phi_f3 += M_TAU;
        }

        while (phi_f3 > M_TAU) {
            phi_f3 -= M_TAU;
        }
        if ((phi_f3 - phi_f1) < 0.0f) {
            phi_f1 += M_PI / 2;
        } else {
            phi_f1 -= M_PI / 2;
        }
    }
    if (coll_data->env_flags & Collide_RightWallMask) {
        phi_f1 = atan2f(coll_data->right_facing_wall.normal.y,
                        coll_data->right_facing_wall.normal.x);
        phi_f3 = M_PI + phi_f1;

        while (phi_f3 < 0.0f) {
            phi_f3 += M_TAU;
        }

        while (phi_f3 > M_TAU) {
            phi_f3 -= M_TAU;
        }
        if ((fp->mv.ns.specialhi.aerialVel - phi_f3) < 0.0f) {
            phi_f1 += M_PI / 2;
        } else {
            phi_f1 -= M_PI / 2;
        }
    }
    sp14.y = 0.0f;
    sp14.x = 0.0f;
    sp14.z = 1.0f;
    lbVector_RotateAboutUnitAxis(&fp->self_vel, &sp14,
                                 phi_f1 - fp->mv.ns.specialhi.aerialVel);
    fp->mv.ns.specialhi.aerialVel = atan2f(fp->self_vel.y, fp->self_vel.x);
}

void ftLc2_SpecialHiStart_Enter(HSD_GObj* gobj) 
                                               
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNessAttributes* ness_attr = fp->dat_attrs;

    u8 _[4];

    ftNessAttributes* temp_attr;
    f64 phi_f0;

    Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialHiStart, 0, 0.0f, 1.0f,
                              0.0f, NULL);
    Fighter_ClearCmdVars(fp);

    {
        Fighter* temp_fp;
        temp_fp = gobj->user_data;
        temp_attr = getFtSpecialAttrs(temp_fp);
        temp_fp->mv.ns.specialhi.thunderTimerLoop1 =
            (s32) temp_attr->x40_PK_THUNDER_LOOP1;
        temp_fp->mv.ns.specialhi.thunderTimerLoop2 =
            (s32) temp_attr->x44_PK_THUNDER_LOOP2;
        temp_fp->mv.ns.specialhi.gravityDelay =
            (s32) temp_attr->x48_PK_THUNDER_GRAVITY_DELAY;
        {
            float temp_f1_2 = 0.0f;
        }
        temp_fp->mv.ns.specialhi.fallAccel = 0.0f;
        temp_fp->mv.ns.specialhi.unkVector1.z = 0.0f;
        temp_fp->mv.ns.specialhi.unkVector1.y = 0.0f;
        temp_fp->mv.ns.specialhi.unkVector1.x = 0.0f;
        temp_fp->mv.ns.specialhi.unkVar4 = 0.0f;
        temp_fp->mv.ns.specialhi.unkVar3 = 0.0f;
        temp_fp->death2_cb = NULL;
        temp_fp->take_dmg_cb = NULL;
        ftPartSetRotX(temp_fp, 0, 0.0F);
        fp->mv.ns.specialhi.thunderColl = 1;
        fp->mv.ns.specialhi.gravityDelay =
            (s32) ness_attr->x48_PK_THUNDER_GRAVITY_DELAY;
        fp->mv.ns.specialhi.jibakuGFX = 0;
        fp->mv.ns.specialhi.collPos1.x = 0.0f;
        fp->mv.ns.specialhi.collPos1.x = 0.0f;
        fp->mv.ns.specialhi.collPos1.x = 0.0f;
        fp->mv.ns.specialhi.collPos2.x = 0.0f;
        fp->mv.ns.specialhi.collPos2.x = 0.0f;
        fp->mv.ns.specialhi.collPos2.x = 0.0f;
        if (1.0f == fp->facing_dir) {
            phi_f0 = (f64) 0.0;
        } else {
            phi_f0 = M_PI;
        }
        fp->mv.ns.specialhi.aerialVel = ftLc2_FloatBits(fp->facing_dir == 1.0f ? 0 : 3);
        fp->mv.ns.specialhi.facingDir = ftLc2_FloatBits(1);
        fp->mv.ns.specialhi.unkVar = 0.0f;
        ftAnim_8006EBA4(gobj);
    }
}

void ftLc2_SpecialAirHiStart_Enter(
    HSD_GObj* gobj) 
{
    Fighter* fp;
    ftNessAttributes* ness_attr;
    Fighter* temp_fp;
    ftNessAttributes* temp_attr;
    f64 phi_f0;

    u8 _[12];
    {
        float _ = 0.0f;
    }
    fp = GET_FIGHTER(gobj);
    ness_attr = fp->dat_attrs;
    Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialAirHiStart, 0, 0.0f, 1.0f,
                              0.0f, NULL);
    Fighter_ClearCmdVars(fp);
    temp_fp = gobj->user_data;
    temp_attr = temp_fp->dat_attrs;
    temp_fp->mv.ns.specialhi.thunderTimerLoop1 =
        (s32) temp_attr->x40_PK_THUNDER_LOOP1;
    temp_fp->mv.ns.specialhi.thunderTimerLoop2 =
        (s32) temp_attr->x44_PK_THUNDER_LOOP2;
    temp_fp->mv.ns.specialhi.gravityDelay =
        (s32) temp_attr->x48_PK_THUNDER_GRAVITY_DELAY;
    {
        float _ = 0.0f;
    }
    temp_fp->mv.ns.specialhi.fallAccel = 0.0f;
    temp_fp->mv.ns.specialhi.unkVector1.z = 0.0f;
    temp_fp->mv.ns.specialhi.unkVector1.y = 0.0f;
    temp_fp->mv.ns.specialhi.unkVector1.x = 0.0f;
    temp_fp->mv.ns.specialhi.unkVar4 = 0.0f;
    temp_fp->mv.ns.specialhi.unkVar3 = 0.0f;
    temp_fp->death2_cb = NULL;
    temp_fp->take_dmg_cb = NULL;
    ftPartSetRotX(temp_fp, 0, 0.0F);
    fp->mv.ns.specialhi.thunderColl = 1;
    fp->mv.ns.specialhi.gravityDelay =
        (s32) ness_attr->x48_PK_THUNDER_GRAVITY_DELAY;
    fp->mv.ns.specialhi.jibakuGFX = false;
    fp->mv.ns.specialhi.collPos1.x = 0.0f;
    fp->mv.ns.specialhi.collPos1.x = 0.0f;
    fp->mv.ns.specialhi.collPos1.x = 0.0f;
    fp->mv.ns.specialhi.collPos2.x = 0.0f;
    fp->mv.ns.specialhi.collPos2.x = 0.0f;
    fp->mv.ns.specialhi.collPos2.x = 0.0f;
    if (1.0f == fp->facing_dir) {
        phi_f0 = (f64) 0.0;
    } else {
        phi_f0 = M_PI;
    }
    fp->mv.ns.specialhi.aerialVel = ftLc2_FloatBits(fp->facing_dir == 1.0f ? 0 : 3);
    fp->mv.ns.specialhi.facingDir = ftLc2_FloatBits(1);
    fp->mv.ns.specialhi.unkVar = 0.0f;
    fp->self_vel.y = 0.0f;
    ftAnim_8006EBA4(gobj);
}

void ftLc2_SpecialHiStart_Anim(HSD_GObj* gobj)
{
    Vec3 pkt_pos;

    u8 _[32];

    Fighter* fp = gobj->user_data;

    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialHiHold, 0, 0.0f, 1.0f,
                                  0.0f, NULL);

        {
            Fighter* fighter_data2 = gobj->user_data;
            HSD_GObj* pkt_ptr = fighter_data2->u.ns.pkthunder_gobj;
            if (pkt_ptr == NULL) {
                lb_8000B1CC(fighter_data2->parts[FtPart_L2ndNa].joint, NULL,
                            &pkt_pos);

                pkt_pos.z = 0.0f;

                pkt_ptr =
                    ftLc2_Thunder_Spawn(gobj, &pkt_pos, fighter_data2->facing_dir);

                fighter_data2->u.ns.pkthunder_gobj = pkt_ptr;

                if (pkt_ptr != NULL) {
                    fighter_data2->death2_cb = ftLc2_Init_OnDamage;
                    fighter_data2->take_dmg_cb = ftLc2_Init_OnDamage;
                }
            }
        }

        fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;

        startGFX(gobj, 5000);
    }
}

void ftLc2_SpecialHiHold_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int timer;

    timer = fp->mv.ns.specialhi.thunderTimerLoop1;
    if (timer > 0) {
        fp->mv.ns.specialhi.thunderTimerLoop1 = timer - 1;
    }
    if (fp->u.ns.pkthunder_gobj == NULL) {
        timer = fp->mv.ns.specialhi.thunderTimerLoop2;
        if (timer > 0) {
            fp->mv.ns.specialhi.thunderTimerLoop2 = timer - 1;
        }
    }

    if (fp->u.ns.pkthunder_gobj == NULL) {
        if (fp->mv.ns.specialhi.thunderTimerLoop1 <= 0 &&
            fp->mv.ns.specialhi.thunderTimerLoop2 <= 0)
        {
            Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialHiEnd, 0, 0.0f,
                                      1.0f, 0.0f, NULL);
            ftLc2_SpecialHiStopGFX(gobj);
        }
    } else if (it_802AB568(fp->u.ns.pkthunder_gobj) == gobj) {
        if (ftLc2_SpecialHi_ItemPKThunder_CheckNessCollide(gobj) == true) {
            ftNs_SpecialHi_Enter(gobj);
        }
    } else {
        fp->u.ns.pkthunder_gobj = NULL;
        Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialHiEnd, 0, 0.0f, 1.0f,
                                  0.0f, NULL);
        ftLc2_SpecialHiStopGFX(gobj);
    }
}

void ftLc2_SpecialHiEnd_Anim(HSD_GObj* gobj)
{
    u8 _[8];

    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftLc2_SpecialHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->mv.ns.specialhi.jibakuGFX += 1;
    if (fp->mv.ns.specialhi.jibakuGFX == 1) {
        startGFX(gobj, 5001);
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialHiEnd, 0, 0.0f, 1.0f,
                                  0.0f, NULL);
        ftLc2_SpecialHiStopGFX(gobj);
    }
}

void ftLc2_SpecialAirHiStart_Anim(HSD_GObj* gobj)
{
    Vec3 pkt_pos;

    u8 _[32];

    HSD_GObj* pkt_ptr;
    Fighter* fp = gobj->user_data;

    if (!ftAnim_IsFramesRemaining(gobj)) {
        Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialAirHiHold, 0, 0.0f,
                                  1.0f, 0.0f, NULL);

        {
            Fighter* fp2 = gobj->user_data;
            pkt_ptr = fp2->u.ns.pkthunder_gobj;

            if (pkt_ptr == 0) {
                lb_8000B1CC(fp2->parts[FtPart_L2ndNa].joint, NULL, &pkt_pos);

                pkt_pos.z = 0.0f;

                pkt_ptr = ftLc2_Thunder_Spawn(gobj, &pkt_pos, fp2->facing_dir);

                fp2->u.ns.pkthunder_gobj = pkt_ptr;

                if (pkt_ptr != NULL) {
                    fp2->death2_cb = ftLc2_Init_OnDamage;
                    fp2->take_dmg_cb = ftLc2_Init_OnDamage;
                }
            }
        }

        fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;

        startGFX(gobj, 5000);
    }
}

void ftLc2_SpecialAirHiHold_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int timer;

    timer = fp->mv.ns.specialhi.thunderTimerLoop1;
    if (timer > 0) {
        fp->mv.ns.specialhi.thunderTimerLoop1 = timer - 1;
    }
    if (fp->u.ns.pkthunder_gobj == NULL) {
        timer = fp->mv.ns.specialhi.thunderTimerLoop2;
        if (timer > 0) {
            fp->mv.ns.specialhi.thunderTimerLoop2 = timer - 1;
        }
    }

    if (fp->u.ns.pkthunder_gobj == NULL) {
        if (fp->mv.ns.specialhi.thunderTimerLoop1 <= 0 &&
            fp->mv.ns.specialhi.thunderTimerLoop2 <= 0)
        {
            Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialAirHiEnd, 0, 0.0f,
                                      1.0f, 0.0f, NULL);
            ftLc2_SpecialHiStopGFX(gobj);
        }
    } else if (it_802AB568(fp->u.ns.pkthunder_gobj) == gobj) {
        if (ftLc2_SpecialHi_ItemPKThunder_CheckNessCollide(gobj) == true) {
            ftNs_SpecialAirHi_Enter(gobj);
        }
    } else {
        fp->u.ns.pkthunder_gobj = NULL;
        Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialAirHiEnd, 0, 0.0f, 1.0f,
                                  0.0f, NULL);
        ftLc2_SpecialHiStopGFX(gobj);
    }
    PAD_STACK(8);
}

void ftLc2_SpecialAirHiEnd_Anim(HSD_GObj* gobj)
{
    u8 _[8];

    Fighter* fp = gobj->user_data;
    ftNessAttributes* ness_attr = fp->dat_attrs;

    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->x1968_jumpsUsed = fp->co_attrs.max_jumps;
        ftCommon_8007D60C(fp);

        if (ness_attr->x70_PK_THUNDER_2_LANDING_LAG == 0) {
            ftCo_Fall_Enter(gobj);
            return;
        }

        /* +288C: the file loads no f3 (the blend), so the console passes what the frame's
         * animation update last loaded there: FObjUpdateAnim's spline p0 (8036AFD0) of part 46's
         * translation Z track, still open when this 30 frame animation (315) ends, -13993/8192.
         * The same for every entry frame. A negative blend applies the fall pose at once. */
        ftCo_800969D8(gobj, 1, 0, 1, 1,
                      ness_attr->x70_PK_THUNDER_2_LANDING_LAG, -1.7081298828125f);
    }
}

void ftLc2_SpecialAirHi_Anim(HSD_GObj* gobj)
{
    u8 _[8];

    Fighter* fp0;
    Fighter* fp1 = gobj->user_data;
    ftNessAttributes* ness_attr = fp1->dat_attrs;

    fp1->mv.ns.specialhi.jibakuGFX = fp1->mv.ns.specialhi.jibakuGFX + 1;
    if (fp1->mv.ns.specialhi.jibakuGFX == 1) {
        startGFX(gobj, 5001);
    }

    if (!ftAnim_IsFramesRemaining(gobj)) {
        s32 fall_bits;
        memcpy(&fall_bits, &fp1->mv.ns.specialhi.fallAccel, sizeof fall_bits);
        float temp_f1 = (float) fall_bits; /* +29F0: lwz, not lfs */
        float phi_f1 = temp_f1;

        if (temp_f1 < 0) {
            phi_f1 = -temp_f1;
        }

        fp1->self_vel.y = (-phi_f1);
        ftCommon_8007D60C(fp1);

        if (ness_attr->x70_PK_THUNDER_2_LANDING_LAG == 0) {
            ftCo_Fall_Enter(gobj);
            return;
        }

        /* +2A80: the file loads no f3 (the blend). Every track of animation 316 is on its last,
         * zero-wait key here, so nothing in this fighter's own frame writes f3: the console
         * passes what an earlier object's update left there, which no local value reproduces.
         * Ness's attribute, the value the retail code passes, stands in. */
        ftCo_800969D8(gobj, 1, 0, 1, 1,
                      ness_attr->x70_PK_THUNDER_2_LANDING_LAG,
                      ness_attr->x6C_PK_THUNDER_2_FREEFALL_ANIM_BLEND);
    }
}

void ftLc2_SpecialAirHiRebound_Anim(HSD_GObj* gobj)
{
    u8 _[8];

    Fighter* fp0 = gobj->user_data;

    {
        Fighter* fp1 = fp0;
        ftNessAttributes* ness_attr = fp0->dat_attrs;

        if (!ftAnim_IsFramesRemaining(gobj)) {
            ftCommon_8007D60C(fp1);

            if (ness_attr->x70_PK_THUNDER_2_LANDING_LAG == 0) {
                ftCo_Fall_Enter(gobj);
                return;
            }

            /* +312C: no f3 loaded, as at +2A80 (animation 317 ends on zero-wait keys): the
             * console's blend is another object's leftover; Ness's attribute stands in. */
            ftCo_800969D8(gobj, 1, 0, 1, 1.0f,
                          ness_attr->x70_PK_THUNDER_2_LANDING_LAG,
                          ness_attr->x6C_PK_THUNDER_2_FREEFALL_ANIM_BLEND);
        }
    }
}

void ftLc2_SpecialHiStart_IASA(
    HSD_GObj* gobj) 
{
    return;
}

void ftLc2_SpecialHiHold_IASA(HSD_GObj* gobj)
{
    return;
}

void ftLc2_SpecialHiEnd_IASA(HSD_GObj* gobj)
{
    return;
}

void ftLc2_SpecialHi_IASA(HSD_GObj* gobj)
{
    return;
}

void ftLc2_SpecialAirHiStart_IASA(HSD_GObj* gobj)
{
    return;
}

void ftLc2_SpecialAirHiHold_IASA(HSD_GObj* gobj)
{
    return;
}

void ftLc2_SpecialAirHiEnd_IASA(HSD_GObj* gobj)
{
    return;
}

void ftLc2_SpecialAirHi_IASA(HSD_GObj* gobj)
{
    return;
}

void ftLc2_SpecialAirHiRebound_IASA(HSD_GObj* gobj)
{
    return;
}

static inline void ThunderPhysTimer(HSD_GObj* gobj)
{
    Fighter* temp_fp;
    s32 thunderPhysTimer;

    temp_fp = gobj->user_data;
    thunderPhysTimer = temp_fp->mv.ns.specialhi.gravityDelay;
    if (thunderPhysTimer != 0) {
        temp_fp->mv.ns.specialhi.gravityDelay = (s32) (thunderPhysTimer - 1);
    }
}

void ftLc2_SpecialHiStart_Phys(
    HSD_GObj* gobj) 
{
    ThunderPhysTimer(gobj);
    ft_80084F3C(gobj);
}

void ftLc2_SpecialHiHold_Phys(HSD_GObj* gobj) 
                                             
{
    ft_80084F3C(gobj);
}

void ftLc2_SpecialHiEnd_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

static const float vel_epsilon = 0.0001f;

/* +222C: preserve the mod's integer write to the saved velocity word. */
void ftLc2_SpecialHi_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftNessAttributes* attr = fp->dat_attrs;
    float candidate = __builtin_fmaf(attr->x5C_PK_THUNDER_2_DECELERATION_RATE,
                           fp->facing_dir, -fp->gr_vel);
    s32 saved_x;
    if ((fp->facing_dir == 1.0f && !(candidate >= -0.0001f)) ||
        (fp->facing_dir != 1.0f && !(candidate <= 0.0001f))) {
        fp->gr_vel = -candidate;
    }
    saved_x = (s32) fp->self_vel.x;
    memcpy(&fp->mv.ns.specialhi.unkVector1.x, &saved_x, sizeof saved_x);
    ftCommon_SetSelfMovementFromGroundedMovement(gobj);
    ftPartSetRotX(fp, 0, __builtin_fmaf(fp->facing_dir,
        atan2f(fp->self_vel.x, fp->self_vel.y), -1.5707963705062866f));
}

void ftLc2_SpecialAirHiStart_Phys(HSD_GObj* gobj)
{
    u8 _[8];

    Fighter* fp = gobj->user_data;
    int gravity_timer = fp->mv.ns.specialhi.gravityDelay;
    ftNessAttributes* ness_attr = fp->dat_attrs;

    if (gravity_timer != 0) {
        fp->mv.ns.specialhi.gravityDelay = gravity_timer - 1;
    } else {
        ftCommon_Fall(fp, ness_attr->x50_PK_THUNDER_FALL_ACCEL,
                      fp->co_attrs.terminal_velocity);
    }

    {
        float friction = fp->co_attrs.aerial_friction;
        ftCommon_CalcSelfAccel_Deaccel(fp, friction);
    }
}

void ftLc2_SpecialAirHiHold_Phys(
    HSD_GObj* gobj) 
                    
{
    u8 _[8];

    Fighter* fp = gobj->user_data;
    int gravity_timer = fp->mv.ns.specialhi.gravityDelay;
    ftNessAttributes* ness_attr = fp->dat_attrs;

    if (gravity_timer != 0) {
        fp->mv.ns.specialhi.gravityDelay = gravity_timer - 1;
    } else {
        ftCommon_Fall(fp, ness_attr->x50_PK_THUNDER_FALL_ACCEL,
                      fp->co_attrs.terminal_velocity);
    }

    {
        float friction = fp->co_attrs.aerial_friction;
        ftCommon_CalcSelfAccel_Deaccel(fp, friction);
    }
}

void ftLc2_SpecialAirHiEnd_Phys(HSD_GObj* gobj)
{
    u8 _[8];

    Fighter* fp = gobj->user_data;
    int gravity_timer = fp->mv.ns.specialhi.gravityDelay;
    ftNessAttributes* ness_attr = fp->dat_attrs;

    if (gravity_timer != 0) {
        fp->mv.ns.specialhi.gravityDelay = gravity_timer - 1;
    } else {
        ftCommon_Fall(fp, ness_attr->x50_PK_THUNDER_FALL_ACCEL,
                      fp->co_attrs.terminal_velocity);
    }

    {
        float friction = fp->co_attrs.aerial_friction;
        ftCommon_CalcSelfAccel_Deaccel(fp, friction);
    }
}

void ftLc2_SpecialAirHiRebound_Phys(
    HSD_GObj* gobj) 
{
    ftCo_DatAttrs* fighter_attr;
    Fighter* fp = gobj->user_data;

    fighter_attr = &fp->co_attrs;
    fighter_attr == NULL;
    ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
    ftCommon_CalcSelfAccel_Deaccel(fp, fighter_attr->aerial_friction);
}

void ftLc2_SpecialHiStart_Coll(
    HSD_GObj* gobj) 
{
    Fighter* fp;

    fp = gobj->user_data;
    if (ft_80082708(gobj) == false) {
        ftCommon_8007D60C(fp);
        Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialAirHiStart,
                                  FTNESS_SPECIALHI_COLL_FLAG,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}

void ftLc2_SpecialHiHold_Coll(HSD_GObj* gobj) 
                                             
{
    Fighter* fp;

    fp = gobj->user_data;
    if (ft_80082708(gobj) == false) {
        ftCommon_8007D60C(fp);
        Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialAirHiHold,
                                  FTNESS_SPECIALHI_COLL_FLAG,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}

void ftLc2_SpecialHiEnd_Coll(
    HSD_GObj* gobj) 
{
    Fighter* fp;

    fp = gobj->user_data;
    if (ft_80082708(gobj) == false) {
        ftCommon_8007D60C(fp);
        Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialAirHiEnd,
                                  FTNESS_SPECIALHI_COLL_FLAG,
                                  fp->cur_anim_frame, 1.0f, 0.0f, NULL);
    }
}

void ftLc2_SpecialAirHiStart_Coll(
    HSD_GObj* gobj) 
{
    Fighter* fp;

    fp = gobj->user_data;
    if (ft_80081D0C(gobj) != false) {
        ftCommon_AirToGroundStateChange(gobj, fp, ftNs_MS_SpecialHiStart,
                                        FTNESS_SPECIALHI_COLL_FLAG);
    }
}

void ftLc2_SpecialAirHiHold_Coll(
    HSD_GObj* gobj) 
                    
{
    Fighter* fp;

    fp = gobj->user_data;
    if (ft_80081D0C(gobj) != false) {
        ftCommon_AirToGroundStateChange(gobj, fp, ftNs_MS_SpecialHiHold,
                                        FTNESS_SPECIALHI_COLL_FLAG);
    }
}

void ftLc2_SpecialAirHiEnd_Coll(
    HSD_GObj* gobj) 
{
    Fighter* fp;

    fp = gobj->user_data;
    if (ft_80081D0C(gobj) != false) {
        ftCommon_AirToGroundStateChange(gobj, fp, ftNs_MS_SpecialHiEnd,
                                        FTNESS_SPECIALHI_COLL_FLAG);
    }
}

static inline void scaleXY(Vec3* v, float s)
{
    v->x *= s;
    v->y *= s;
}

static inline Fighter* rebound(HSD_GObj* gobj, Vec3* normal)
{
    Fighter* fp = gobj->user_data;
    float x;
    float y;
    float xx;
    float yy;
    float dir;
    float max;

    lbVector_Mirror(&fp->self_vel, normal);
    scaleXY(&fp->self_vel, 0.5f);
    x = fp->self_vel.x;
    y = fp->self_vel.y;
    xx = x * x;
    yy = y * y;
    lbVector_sqrtf_accurate(__builtin_fmaf(x, x, yy));
    max = fp->co_attrs.air_drift_max;
    ftCommon_ClampSelfVelX(fp, max);
    if (fp->self_vel.x >= 0.0f) {
        dir = 1.0f;
    } else {
        dir = -1.0f;
    }
    fp->facing_dir = dir;
    ftLc2_SpecialHiStopGFX(gobj);
    Fighter_ChangeMotionState(gobj, ftNs_MS_SpecialAirHiRebound, Ft_MF_KeepGfx,
                              0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    return fp;
}

void ftLc2_SpecialAirHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = gobj->user_data;
    ftNessAttributes* ness_attr = fp->dat_attrs;
    u8 _[4];
    s32 ledge_dir;
    s32 env_flags;


    if (1.0f == fp->facing_dir) {
        ledge_dir = 1;
    } else {
        ledge_dir = -1;
    }
    if (ft_CheckGroundAndLedge(gobj, ledge_dir) != false) {
        if (lbVector_Angle(&fp->coll_data.floor.normal, &fp->self_vel) >
            (0.01745329238474369f *
             (90.0f + ness_attr->x64_PK_THUNDER_2_WALLHUG_ANGLE)))
        {
            fp->self_vel.z = 0.0f;
            fp->self_vel.y = 0.0f;
            fp->self_vel.x = 0.0f;
            ftLc2_SpecialHiStopGFX(gobj);
            ftPartSetRotX(fp, 0, 0);
            ftCo_80097D40(gobj);
            return;
        }

        ftCommon_AirToGroundStateChange(gobj, fp, ftNs_MS_SpecialHi,
                                        FTNESS_JIBAKU_COLL_FLAG);
        return;
    }

    if (ftCliffCommon_80081298(gobj)) {
        ftCliffCommon_80081370(gobj);
        return;
    }

    env_flags = fp->coll_data.env_flags;
    if (env_flags & Collide_CeilingMask) {
        if (lbVector_Angle(&fp->coll_data.ceiling.normal, &fp->self_vel) >
            (0.01745329238474369f *
             (90.0f + ness_attr->x64_PK_THUNDER_2_WALLHUG_ANGLE)))
        {
            Vec3 rot;
            Fighter* fp1 = rebound(gobj, &fp->coll_data.ceiling.normal);
            rot.x = atan2f(-fp->coll_data.ceiling.normal.x,
                           fp->coll_data.ceiling.normal.y);
            efSync_Spawn(1030, gobj, &fp1->cur_pos, &rot);
        }
    } else {
        if (env_flags & Collide_LeftWallMask) {
            if (lbVector_Angle(&fp->coll_data.left_facing_wall.normal,
                               &fp->self_vel) >
                (0.01745329238474369f *
                 (90.0f + ness_attr->x64_PK_THUNDER_2_WALLHUG_ANGLE)))
            {
                Vec3 rot;
                Fighter* fp1 =
                    rebound(gobj, &fp->coll_data.left_facing_wall.normal);
                rot.x = atan2f(-fp->coll_data.left_facing_wall.normal.x,
                               fp->coll_data.left_facing_wall.normal.y);
                efSync_Spawn(1030, gobj, &fp1->cur_pos, &rot);
                return;
            }
            ftLc2_SpecialAirHi_CollisionModVel(gobj, &fp->coll_data);
            return;
        }
        if (env_flags & Collide_RightWallMask) {
            if (lbVector_Angle(&fp->coll_data.right_facing_wall.normal,
                               &fp->self_vel) >
                (0.01745329238474369f *
                 (90.0f + ness_attr->x64_PK_THUNDER_2_WALLHUG_ANGLE)))
            {
                Vec3 rot;
                Fighter* fp1 =
                    rebound(gobj, &fp->coll_data.right_facing_wall.normal);
                rot.x = atan2f(-fp->coll_data.right_facing_wall.normal.x,
                               fp->coll_data.right_facing_wall.normal.y);
                efSync_Spawn(1030, gobj, &fp1->cur_pos, &rot);
                return;
            }
            ftLc2_SpecialAirHi_CollisionModVel(gobj, &fp->coll_data);
        }
    }
    PAD_STACK(144);
}

void ftLc2_SpecialAirHiRebound_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ft_CheckGroundAndLedge(gobj, 0) == true) {
        fp->self_vel.z = 0.0f;
        fp->self_vel.y = 0.0f;
        fp->self_vel.x = 0.0f;
        ftLc2_SpecialHiStopGFX(gobj);
        ftPartSetRotX(fp, 0, 0.0F);
        ftCo_80097D40(gobj);
        return;
    }
    if (ftCliffCommon_80081298(gobj) != false) {
        ftCliffCommon_80081370(gobj);
    }
}
