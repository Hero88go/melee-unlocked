/* ACE PlLb.dat ftFunction. Local offsets refer to verified16/listing.txt.
 * Native safety guards for the file's two invalid ABI calls are documented in
 * CODEX18_FIGHTER_REVIEW.md. No guest instructions are executed. */
#include "luigiboo.h"
#include "luigiboo_boo.h"
#include "luigiboo_entries.h"
#include "luigiboo_kirby.h"
#include "../../akaneia/mu_ak_fighter.h"
#include "mu_disc.h"

#include <dolphin/os.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/ef/eflib.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/ftCo_800C70D0.h>
#include <melee/ft/ftCo_800C7178.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackHi4.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackLw4.h>
#include <melee/ft/kinds/ftCommon/ftCo_AttackS4.h>
#include <melee/ft/kinds/ftCommon/ftCo_Guard.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/kinds/ftCommon/ftCo_Wait.h>
#include <melee/ft/kinds/ftCommon/ftCo_AirCatch.h>
#include <melee/ft/kinds/ftLuigi/ftluigi.h>
#include <melee/ft/kinds/ftLuigi/ftluigispecialn.h>
#include <melee/ft/kinds/ftLuigi/ftluigispecials.h>
#include <melee/ft/kinds/ftLuigi/ftluigispecialhi.h>
#include <melee/ft/kinds/ftLuigi/ftluigispeciallw.h>
#include <melee/it/inlines.h>
#include <melee/it/item.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbarchive.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>

#define FTLB_PI 3.14159265358979323846
#define FTLB_HALF_PI 1.57079632679489661923
#define FTLB_PATH_ANGLE 0x1.65718ep-2f /* +15A4: 0x3EB2B8C7 */

_Static_assert(sizeof(ftLb_FighterVars) <= sizeof(((Fighter*) 0)->u), "fighter vars fit");
_Static_assert(sizeof(ftLb_MotionVars) <= sizeof(((Fighter*) 0)->mv), "motion vars fit");
_Static_assert(offsetof(ftLb_MotionVars, path) == 0x24, "path console offset");
_Static_assert(offsetof(ftLb_MotionVars, path_time) == 0x58, "path time console offset");

static HSD_GObj* ftLb_BooOf(Fighter* fp)
{
    ftLb_State* state = ftLb_StateOf(fp);
    return state != NULL ? state->boo : NULL;
}

static void ftLb_Change(HSD_GObj* gobj, int state, int flags)
{
    Fighter_ChangeMotionState(gobj, state, flags, 0.0f, 1.0f, 0.0f, NULL);
}

/* +20EC: copy all four rotation words without dirtying the joint. */
static void ftLb_BooChange(HSD_GObj* gobj, int state, int flags, int before, int after)
{
    Quaternion rotate = it_80272CC0(gobj, before)->rotate;
    Item_80268E5C(gobj, state, flags);
    it_80272CC0(gobj, after)->rotate = rotate;
}

/* +2238: detach a joint's world position into item position. */
static void ftLb_BooDetachJoint(HSD_GObj* gobj, int joint_index)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* joint = it_80272CC0(gobj, joint_index);
    lb_8000B1CC(joint, NULL, &ip->pos);
    joint->translate.x = joint->translate.y = joint->translate.z = 0.0f;
}

/* +2588: the original passes a pointer to a %f format without setting an FP
 * argument. Log the message without reproducing that undefined variadic read. */
void ftLb_Misfire_OnActionStateChange(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    OSReport("Misfire ActionStateChange\n");
    if (fp->allow_sdi) {
        OSReport("hitlag frame unavailable (mod passes an address to %%f)\n");
        fp->x21EC = ftLb_Misfire_OnActionStateChange;
        fp->take_dmg_cb = ftLb_Misfire_OnTakeDamage;
    } else {
        OSReport("No More Hitlag\n");
        fp->x21EC = NULL;
        fp->take_dmg_cb = NULL;
    }
}

/* +2630. */
void ftLb_Misfire_OnTakeDamage(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    OSReport("Misfire OnTakeDamage\n");
    fp->x21EC = NULL;
    fp->take_dmg_cb = NULL;
}

/* +22A8: normalize the three angle components through atan(tan(a/2)), then
 * carry item velocity and both collision top-node samples into released state 7. */
static float ftLb_NormalizeAngle(float angle)
{
    float half = angle * 0.5f;
    float c = cosf(half);
    if (c == 0.0f) {
        return 0x1.921fb6p+1f;
    }
    float s = sinf(half);
    c = cosf(half);
    float result = atanf(s / c);
    return result + result;
}

void ftLb_BooDetach(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itLb_BooVars* iv = itLb_BooVarsOf(ip);
    Fighter* owner = GET_FIGHTER(iv->owner);
    HSD_JObj* joint;
    Vec3 pos;
    ftLb_BooDetachJoint(gobj, 1);
    ftLb_BooChange(gobj, 7, 2, 1, 0);
    joint = it_80272CC0(gobj, 0);
    joint->rotate.x = ftLb_NormalizeAngle(joint->rotate.x);
    joint->rotate.y = (float) __builtin_fma((double) ip->facing_dir * FTLB_PI, 0.5,
                                          (double) ftLb_NormalizeAngle(joint->rotate.y));
    joint->rotate.z = ftLb_NormalizeAngle(joint->rotate.z);
    ip->x40_vel.x = ip->pos.x - iv->previous_pos.x;
    ip->x40_vel.y = ip->pos.y - iv->previous_pos.y;
    ip->x40_vel.z = ip->pos.z - iv->previous_pos.z;
    iv->turn_velocity = 0.0f;
    lb_8000B1CC(it_80272CC0(gobj, 1), NULL, &pos);
    pos.y -= 6.0f;
    ip->x378_itemColl.cur_pos = pos;
    ip->x378_itemColl.last_pos = pos;
    if (owner->x221C_b6) {
        ftLb_MV(owner)->speed = (float) ((double) ftLb_MV(owner)->speed * 1.6);
        OSReport("misfire hit\n");
        owner->x21EC = ftLb_Misfire_OnActionStateChange;
        owner->take_dmg_cb = ftLb_Misfire_OnTakeDamage;
    }
}

/* +21A0: temporarily replace article 1's model root with CostumeDesc[0]. */
static HSD_GObj* ftLb_BooCreateCostume(Fighter* fp, SpawnItem* spawn)
{
    typedef DISC_PTR(Article) ArticleSlot;
    typedef DISC_PTR(HSD_Joint) JointSlot;
    ArticleSlot* items = DISC_GET(ArticleSlot, fp->ft_data->x48_items);
    Article* article = DP(items[1]);
    ItemModelDesc* desc = DP(article->x10_modelDesc);
    HSD_Joint* saved = DP(desc->x0_joint);
    ftLb_State* state = ftLb_StateOf(fp);
    JointSlot* costumes = state->archive != NULL
        ? HSD_ArchiveGetPublicAddress(state->archive, "CostumeDesc") : NULL;
    HSD_GObj* result;
    if (costumes == NULL || DP(costumes[0]) == NULL) {
        static bool logged;
        if (!logged) {
            logged = true;
            OSReport("[ak] Luigi & Boo: unavailable CostumeDesc; using base Boo model\n");
        }
        return mu_ak_item_create(spawn);
    }
    DISC_SET(desc->x0_joint, DP(costumes[0]));
    mu_ak_register_article(fp->kind, article, 1);
    result = mu_ak_item_create(spawn);
    DISC_SET(desc->x0_joint, saved);
    return result;
}

/* +1878. */
static void ftLb_BooSpawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLb_State* state = ftLb_StateOf(fp);
    StaticPlayer* player = Player_GetPtrForSlot(fp->player_idx);
    Vec3 position = player->player_poses.byVecName.transformed_player_pos;
    SpawnItem spawn = { 0 };
    HSD_GObj* boo;
    Item* ip;
    position.x = MU_FMADDS(-fp->facing_dir, 7.0f, position.x);
    position.y += 15.0f;
    position.z = 0.0f;
    spawn.x0_parent_gobj = spawn.x4_parent_gobj2 = gobj;
    spawn.kind = mu_ak_article_kind(gobj, 1);
    spawn.hold_kind = 8;
    spawn.pos = spawn.prev_pos = position;
    spawn.facing_dir = fp->facing_dir;
    spawn.x48_ground_or_air = GA_Ground;
    boo = fp->costume_id == 0 ? mu_ak_item_create(&spawn) : ftLb_BooCreateCostume(fp, &spawn);
    state->boo = boo;
    if (boo == NULL) {
        OSReport("[ak] Luigi & Boo: companion allocation failed\n");
        return;
    }
    ip = GET_ITEM(boo);
    itLb_BooVarsOf(ip)->owner = gobj;
    ip->x520_cameraBox = Camera_80029020();
    if (ip->x520_cameraBox != NULL) {
        ip->x520_cameraBox->state = 0;
        ip->x520_cameraBox->target_ext.h.x = -10.0f;
        ip->x520_cameraBox->target_ext.h.y = 10.0f;
    }
    ip->xDCD_flag.b01 = 1;
    ip->xDCC_flag.b3 = false;
}

/* +0000. Allocation is zero-initialized natively because absent/zero-index
 * archives and a first respawn otherwise read unspecified console allocator bytes. */
static void ftLb_OnLoad(HSD_GObj* gobj)
{
    static const u8 costume_index[] = { 0, 1, 2, 3, 4, 0 };
    static const char* files[] = { "NULL", "PlLgBooWh.dat", "PlLgBooAq.dat",
                                  "PlLgBooPi.dat", "PlLgBooSt.dat" };
    Fighter* fp = GET_FIGHTER(gobj);
    ftLb_State* state;
    int costume = fp->costume_id;
    typedef DISC_PTR(void) ArticleSlot;
    ArticleSlot* items;
    ftLg_Init_OnLoad(gobj);
    state = HSD_MemAlloc(sizeof(*state));
    memset(state, 0, sizeof(*state));
    ((ftLb_FighterVars*) &fp->u)->state = state;
    if (costume <= 5) {
        int index = costume_index[costume];
        if (fp->player_idx == 0) {
            if (index != 0) {
                state->archive = lbArchive_LoadArchive(files[index]);
            }
        } else {
            for (int i = 0; i < fp->player_idx; i++) {
                HSD_GObj* earlier = Player_GetEntity(i);
                Fighter* other = earlier != NULL ? GET_FIGHTER(earlier) : NULL;
                if (other == NULL || other->kind != fp->kind) {
                    if (index != 0) {
                        state->archive = lbArchive_LoadArchive(files[index]);
                    }
                } else if (other->costume_id == costume ||
                           (other->costume_id <= 5 && costume_index[other->costume_id] == index)) {
                    ftLb_State* previous = ftLb_StateOf(other);
                    state->archive = previous != NULL ? previous->archive : NULL;
                    break;
                } else if (index != 0 && i == fp->player_idx - 1) {
                    state->archive = lbArchive_LoadArchive(files[index]);
                }
            }
        }
    }
    items = DISC_GET(ArticleSlot, fp->ft_data->x48_items);
    mu_ak_register_article(fp->kind, DP(items[1]), 1);
}

/* +06B8. */
static void ftLb_OnRespawn(HSD_GObj* gobj)
{
    ftLb_State* state = ftLb_StateOf(GET_FIGHTER(gobj));
    if (state->boo != NULL) {
        Item_8026A8EC(state->boo);
    }
    state->boo = NULL;
    ftLb_BooSpawn(gobj);
    ftLg_Init_LoadSpecialAttrs(gobj);
}

/* +15C0/+1660. The +15F8 call supplies an Item GObj to ftLib_80086A4C,
 * whose only action is a Fighter+1958 store. It is outside the item allocation,
 * so skip that corruption natively while retaining every valid local effect. */
static void ftLb_Pause(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObj* boo = ftLb_BooOf(fp);
    efLib_PauseAll(gobj);
    ftLb_MV(fp)->hitlag = 1;
    if (boo != NULL && GET_ITEM(boo)->msid == 14) {
        HSD_JObj* joint = it_80272CC0(boo, 1);
        HSD_AObjStopAnim(joint->aobj, NULL, NULL);
    }
    static bool logged;
    if (!logged) {
        logged = true;
        OSReport("[ak] Luigi & Boo: +15F8 skips Fighter+1958 store through Boo item (mod bug)\n");
    }
}

static void ftLb_Resume(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObj* boo = ftLb_BooOf(fp);
    efLib_ResumeAll(gobj);
    ftLb_MV(fp)->hitlag = 0;
    if (boo != NULL && GET_ITEM(boo)->msid == 14) {
        HSD_AObjSetRate(it_80272CC0(boo, 1)->aobj, 1.0f);
    }
}

/* +16F0. */
static void ftLb_BooRecall(HSD_GObj* boo)
{
    Item* ip = GET_ITEM(boo);
    Fighter* fp = GET_FIGHTER(itLb_BooVarsOf(ip)->owner);
    ftLb_MotionVars* mv = ftLb_MV(fp);
    Vec3 target;
    float delta = fp->cur_pos.x - ip->pos.x;
    ip->facing_dir = delta <= 0.0f ? -1.0f : 1.0f;
    ftLb_BooChange(boo, 14, 2, 0, 0);
    target = fp->cur_pos;
    target.x = (float) __builtin_fma((double) fp->facing_dir, 5.0, (double) target.x);
    target.y += 30.0f;
    mv->path_time = 0.0f;
    ip->x40_vel.x = ip->x40_vel.y = ip->x40_vel.z = 0.0f;
    ftLb_PathSolve(&mv->path, &ip->pos, &target, 15, ftLb_EaseSquare, 7.0f,
                    FTLB_PATH_ANGLE);
    delta = fp->cur_pos.x - ip->pos.x;
    mv->facing = delta <= 0.0f ? -1.0f : 1.0f;
}

/* +05CC. */
static void ftLb_SpecialAirHi_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObj* boo = ftLb_BooOf(fp);
    ftLg_SpecialAirHi_Enter(gobj);
    if (boo != NULL && (GET_ITEM(boo)->msid == 0 || GET_ITEM(boo)->msid == 6)) {
        ftLb_Change(gobj, 362, 0);
        fp->pre_hitlag_cb = ftLb_Pause;
        fp->post_hitlag_cb = ftLb_Resume;
        ftLb_MV(fp)->hitlag = 0;
        ftLb_BooRecall(boo);
    }
}

/* +19E4/+1BC4/+1CC0 differ only in item state 26/20/19. */
static void ftLb_BooSmash(HSD_GObj* boo, int state)
{
    Item* ip = GET_ITEM(boo);
    Fighter* fp = GET_FIGHTER(itLb_BooVarsOf(ip)->owner);
    HSD_JObj* joint = it_80272CC0(boo, 0);
    joint->rotate.x = joint->rotate.z = joint->rotate.w = 0.0f;
    joint->rotate.y = (float) ((double) fp->facing_dir * FTLB_HALF_PI);
    ip->pos.x = fp->facing_dir < 0.0f ? fp->cur_pos.x - 7.0f : fp->cur_pos.x + 7.0f;
    ip->pos.y = fp->cur_pos.y + 15.0f;
    ip->x40_vel.x = ip->x40_vel.y = ip->x40_vel.z = 0.0f;
    ip->facing_dir = fp->facing_dir;
    Item_80268E5C(boo, state, 2);
    itLb_BooVarsOf(ip)->flags &= ~1u;
    ip->xDAC_itcmd_var0 = 0;
    ip->xDB0_itcmd_var1 = 1;
}

/* +1AE0/+1B4C/+1B88. */
static void ftLb_SmashDamage(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObj* boo = ftLb_BooOf(fp);
    if (boo != NULL) {
        GET_ITEM(boo)->xDB0_itcmd_var1 = 0;
        ftLb_BooDetachJoint(boo, 1);
        ftLb_BooDetach(boo);
    }
    fp->pre_hitlag_cb = fp->post_hitlag_cb = fp->take_dmg_cb = NULL;
}
static void ftLb_SmashPause(HSD_GObj* gobj)
{
    HSD_GObj* boo = ftLb_BooOf(GET_FIGHTER(gobj));
    if (boo != NULL) { HSD_AObjSetRate(it_80272CC0(boo, 0)->aobj, 0.0f); }
}
static void ftLb_SmashResume(HSD_GObj* gobj)
{
    HSD_GObj* boo = ftLb_BooOf(GET_FIGHTER(gobj));
    if (boo != NULL) { HSD_AObjSetRate(it_80272CC0(boo, 0)->aobj, 1.0f); }
}

static void ftLb_Smash(HSD_GObj* gobj, int ordinary, int companion, int boo_state,
                        bool clear_input)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObj* boo = ftLb_BooOf(fp);
    bool available = boo != NULL && (itLb_BooVarsOf(GET_ITEM(boo))->flags & 1);
    fp->allow_interrupt = false;
    if (available) { ftLb_BooSmash(boo, boo_state); }
    ftLb_Change(gobj, available ? companion : ordinary, 0);
    if (available) {
        fp->take_dmg_cb = ftLb_SmashDamage;
        fp->pre_hitlag_cb = ftLb_SmashPause;
        fp->post_hitlag_cb = ftLb_SmashResume;
    }
    if (clear_input) { fp->input_cb = NULL; }
    ftAnim_8006EBA4(gobj);
}
static void ftLb_SmashF(HSD_GObj* gobj) { ftLb_Smash(gobj, 60, 60, 26, true); }
static void ftLb_SmashHi(HSD_GObj* gobj) { ftLb_Smash(gobj, 63, 365, 20, false); }
static void ftLb_SmashLw(HSD_GObj* gobj) { ftLb_Smash(gobj, 64, 364, 19, false); }

/* +0E50: r3 stays the GObj while r9 is its Fighter. Only self_vel.x on
 * the actual Fighter is damped; the tail call's GObj+E4 corruption is skipped. */
static void ftLb_SpecialS_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->self_vel.x = (float) ((double) fp->self_vel.x * 0.95);
    static bool logged;
    if (!logged) {
        logged = true;
        OSReport("[ak] Luigi & Boo: +0E50 skips ground friction through GObj (mod bug)\n");
    }
}

/* +0E78. */
static void ftLb_SpecialAirHi_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->cmd_vars[3] == 1 || !ftAnim_IsFramesRemaining(gobj)) {
        HSD_JObj* joint = gobj->hsd_obj;
        float rotate = joint->rotate.y;
        fp->cmd_vars[0] = fp->cmd_vars[1] = fp->cmd_vars[2] = fp->cmd_vars[3] = 0;
        ftLb_Change(gobj, 363, 0);
        ftAnim_8006EBA4(gobj);
        ftPartSetRotY(fp, 0, rotate);
    }
}

/* +0FB4. The console's index multiplication is the 0x10 bone stride. */
static void ftLb_SpecialAirHi_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->coll_data.env_flags & 0x6000) {
        Vec3 end;
        ftLb_MotionVars* mv = ftLb_MV(fp);
        Vec3 start = { mv->path.start.x, mv->path.start.y, 0.0f };
        lb_8000B1CC(fp->parts[ftParts_GetBoneIndex(fp, 0x28)].joint, NULL, &end);
        ftLb_PathSolve(&mv->path, &start, &end, 15, ftLb_EaseSquare, 7.0f,
                        FTLB_PATH_ANGLE);
    }
    ftLg_SpecialAirHi_Coll(gobj);
}

/* +1484. */
static void ftLb_Recovery_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        ftCommon_8007D7FC(fp);
        ftCo_LandingFallSpecial_Enter(gobj, false, 20.0f);
    }
    if (fp->self_vel.y < 0.0f && !(fp->cur_anim_frame >= 60.0f) &&
        ftCo_800C3A14(gobj) && ft_80082E3C(gobj) == NULL) {
        ftCliffCommon_80081370(gobj);
    }
}

/* +118C. The two stick values are written by the Boo callbacks. */
static void ftLb_Recovery_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLb_MotionVars* mv = ftLb_MV(fp);
    if (fp->cmd_vars[1] == 2) {
        HSD_GObj* boo = ftLb_BooOf(fp);
        if (boo != NULL) {
            HSD_JObj* joint = gobj->hsd_obj;
            Vec3 pos;
            double target = ((double) fp->facing_dir * FTLB_PI) * 0.5;
            lb_8000B1CC(it_80272CC0(boo, 1), NULL, &pos);
            pos.x = MU_FMADDS(-fp->facing_dir, 3.0f, pos.x);
            pos.y -= 20.0f;
            fp->cur_pos = pos;
            if ((double) joint->rotate.y != target) {
                float rounded = (float) target;
                float delta = joint->rotate.y - rounded;
                joint->rotate.y = (float) __builtin_fma((double) delta, 0.8,
                                                       (double) rounded);
            }
        }
    } else if (fp->cmd_vars[1] == 1) {
        float vx, vy, ax, ay;
        fp->self_vel.x = (float) ((double) fp->self_vel.x * 0.93);
        fp->self_vel.y = (float) ((double) fp->self_vel.y * 0.95);
        vx = fp->self_vel.x;
        vy = fp->self_vel.y;
        if (!(vx >= 0.0f)) { vx = -vx; }
        if (!(vx >= 0.5f)) {
            ax = -mv->stick_x;
            if (!(mv->stick_x < 0.0f)) { ax = mv->stick_x; }
            if ((double) ax > 0.2875) {
                fp->self_vel.x = (float) (((double) ax * 0.5) / (double) mv->stick_x);
            }
        }
        if (!(vy >= 0.0f)) { vy = -vy; }
        if (!(vy >= 0.5f)) {
            ay = -mv->stick_y;
            if (!(mv->stick_y < 0.0f)) { ay = mv->stick_y; }
            if (!((double) ay <= 0.2875)) {
                fp->self_vel.y = (float) (((double) ay * 0.5) / (double) mv->stick_y);
            }
        }
    } else {
        ftCommon_Fall(fp, fp->co_attrs.gravity, fp->co_attrs.terminal_velocity);
        ftCommon_CalcSelfAccel_Deaccel(fp, fp->co_attrs.aerial_friction);
        ftCommon_CalcSelfAccel_DriftSimple_NoFriction(fp, 0.0f,
                                                    fp->co_attrs.air_drift_stick_mul,
                                                    fp->co_attrs.air_drift_max);
        ftCommon_CalcSelfAccel_Drift(fp);
        ftCommon_FallBasic(fp);
    }
    if (fp->cmd_vars[0] == 1) {
        if (mv->stick_x != 0.0f || mv->stick_y != 0.0f) {
            fp->self_vel.z = 0.0f;
            fp->self_vel.x = mv->stick_x * 3.2f;
            fp->self_vel.y = mv->stick_y * 3.2f;
        }
        ftPartSetRotY(fp, 0, (float) (((double) fp->facing_dir * FTLB_PI) * 0.5));
        fp->cmd_vars[0] = 0;
    }
}

/* +0A68. Labels retain the file's branch structure, including +0B74's
 * andi. A-button result in cr0 that survives the later cr7 tests at +0D44. */
static void ftLb_SpecialS_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    double f0, f11, f12;
    bool cr0eq;
    int state;
    u32 pressed;
    if (!(fp->cur_anim_frame <= 6.0f)) {
        if (fp->input.held_buttons[0] & 0x60) { goto shield; }
        pressed = fp->input.pressed_buttons;
        if (pressed & 0x60) { goto shield; }
        if (pressed & 0xC00) { goto jump; }
        f0 = fp->input.lstick[0].y;
        if (f0 > 0.2875) { goto jump; }
        cr0eq = (pressed & 0x100) == 0; /* +0B74; later comparisons use cr7 */
        f12 = fp->input.cstick[0].y;
        if ((pressed & 0x100) && !(f0 <= 0.0)) {
            f0 = fp->input.lstick[0].x;
            if (!(f0 >= 0.0)) { f0 = -f0; }
            if (!(f0 >= 0.2875)) {
                if (f12 > 0.5) { goto upsmash; }
                state = 0x38;
                goto change;
            }
        }
        if (!(f12 <= 0.5)) { goto upsmash; }
        f0 = fp->input.lstick[0].x;
        if (!(f0 >= 0.0)) { f0 = -f0; }
        f11 = fp->input.cstick[0].x;
        if (!(f0 > 0.2875)) {
            if (f11 < 0.0) { goto negative_cstick; }
            if (!(f11 <= 0.5)) { goto forward_smash; }
            return;
        }
        if (f11 < 0.0) { goto negative_cstick; }
check_cstick:
        if (f11 > 0.5) { goto forward_smash; }
        if (!cr0eq) { goto tilt; }
        if (fp->active_sticky.lstick.x > 2 || f0 <= 0.5 || fp->motion_id != 0x157) {
            return;
        }
        ftCo_Wait_IASA(gobj);
        return;
negative_cstick:
        /* When |stick x| <= .2875, +0D18 first tests against -.5. */
        if (!(f0 > 0.2875) && !(f11 < -0.5)) { return; }
        f11 = -f11;
        goto check_cstick;
    }
    f11 = fp->input.lstick[0].y;
    f12 = fp->input.cstick[0].y;
    pressed = fp->input.pressed_buttons;
    if (!(f11 >= -0.5)) {
        if (f12 < -0.5) { state = 0x40; goto change; }
        if (!(pressed & 0x100)) { state = 0x27; goto change; }
        if (fp->active_sticky.lstick.x <= 4) { state = 0x40; goto change; }
        state = 0x39;
        goto change;
    }
    if (f12 < -0.5) { state = 0x40; goto change; }
    if (pressed & 0x100) { state = 0x2C; goto change; }
    return;
shield:
    (void) ftCo_80091A4C(gobj);
    return;
jump:
    (void) ftCo_Jump_CheckInput(gobj);
    return;
upsmash:
    state = 0x3F;
    goto change;
forward_smash:
    state = 0x3C;
    goto change;
tilt:
    ftCommon_UpdateFacing(fp);
    if (fp->active_sticky.lstick.x <= 4) {
        state = 0x3C;
    } else if ((double) fp->input.lstick[0].y > 0.15) {
        state = 0x33;
    } else if ((double) fp->input.lstick[0].y < -0.15) {
        state = 0x37;
    } else {
        state = 0x35;
    }
    ftLb_Change(gobj, state, 0x20);
    fp->input_cb = NULL;
    return;
change:
    ftLb_Change(gobj, state, 0x20);
}

/* +01EC. Retail rows are borrowed only by their exact table addresses in
 * tables.txt. Added rows 359/360 intentionally have no camera/collision callback. */
static const MotionState ftLb_MotionStates[26] = {
    {295,0x00340111,0x12000000,ftLg_SpecialN_Anim,ftLg_SpecialN_IASA,ftLg_SpecialN_Phys,ftLg_SpecialN_Coll,ftCamera_UpdateCameraBox},
    {296,0x00340511,0x12000000,ftLg_SpecialAirN_Anim,ftLg_SpecialAirN_IASA,ftLg_SpecialAirN_Phys,ftLg_SpecialAirN_Coll,ftCamera_UpdateCameraBox},
    {297,0,0x13000000,ftLb_SpecialS_Anim,ftLb_SpecialS_IASA,ftLb_SpecialS_Phys,ft_80084104,ftCamera_UpdateCameraBox},
    {298,0x00340212,0x13000000,ftLg_SpecialSHold_Anim,ftLg_SpecialSHold_IASA,ftLg_SpecialSHold_Phys,ftLg_SpecialSHold_Coll,ftCamera_UpdateCameraBox},
    {301,0x00340212,0x13000000,ftLg_SpecialS2_Anim,ftLg_SpecialS2_IASA,ftLg_SpecialS2_Phys,ftLg_SpecialS2_Coll,ftCamera_UpdateCameraBox},
    {302,0x00340212,0x13000000,ftLg_SpecialSEnd_Anim,ftLg_SpecialSEnd_IASA,ftLg_SpecialSEnd_Phys,ftLg_SpecialSEnd_Coll,ftCamera_UpdateCameraBox},
    {299,0x00340212,0x13000000,ftLg_SpecialS_Anim,ftLg_SpecialS_IASA,ftLg_SpecialS_Phys,ftLg_SpecialS_Coll,ftCamera_UpdateCameraBox},
    {300,0x00340212,0x13000000,ftLg_SpecialSMisfire_Anim,ftLg_SpecialSMisfire_IASA,ftLg_SpecialSMisfire_Phys,ftLg_SpecialSMisfire_Coll,ftCamera_UpdateCameraBox},
    {303,0x00340612,0x13000000,ftLg_SpecialAirSStart_Anim,ftLg_SpecialAirSStart_IASA,ftLg_SpecialAirSStart_Phys,ftLg_SpecialAirSStart_Coll,ftCamera_UpdateCameraBox},
    {304,0x00340612,0x13000000,ftLg_SpecialAirSHold_Anim,ftLg_SpecialAirSHold_IASA,ftLg_SpecialAirSHold_Phys,ftLg_SpecialAirSHold_Coll,ftCamera_UpdateCameraBox},
    {301,0x00340612,0x13000000,ftLg_SpecialAirS2_Anim,ftLg_SpecialAirS2_IASA,ftLg_SpecialAirS2_Phys,ftLg_SpecialAirS2_Coll,ftCamera_UpdateCameraBox},
    {307,0x00340612,0x13000000,ftLg_SpecialAirSEnd_Anim,ftLg_SpecialAirSEnd_IASA,ftLg_SpecialAirSEnd_Phys,ftLg_SpecialAirSEnd_Coll,ftCamera_UpdateCameraBox},
    {305,0x00340612,0x13000000,ftLg_SpecialAirS_Anim,ftLg_SpecialAirS_IASA,ftLg_SpecialAirS_Phys,ftLg_SpecialAirS_Coll,ftCamera_UpdateCameraBox},
    {306,0x00340612,0x13000000,ftLg_SpecialAirSMisfire_Anim,ftLg_SpecialAirSMisfire_IASA,ftLg_SpecialAirSMisfire_Phys,ftLg_SpecialAirSMisfire_Coll,ftCamera_UpdateCameraBox},
    {308,0x00340213,0x14000000,ftLg_SpecialHi_Anim,ftLg_SpecialHi_IASA,ftLg_SpecialHi_Phys,ftLg_SpecialHi_Coll,ftCamera_UpdateCameraBox},
    {309,0x00340613,0x14000000,ftLg_SpecialAirHi_Anim,ftLg_SpecialAirHi_IASA,ftLg_SpecialAirHi_Phys,ftLg_SpecialAirHi_Coll,ftCamera_UpdateCameraBox},
    {310,0x00340214,0x15000000,ftLg_SpecialLw_Anim,ftLg_SpecialLw_IASA,ftLg_SpecialLw_Phys,ftLg_SpecialLw_Coll,ftCamera_UpdateCameraBox},
    {311,0x00340614,0x15000000,ftLg_SpecialAirLw_Anim,ftLg_SpecialAirLw_IASA,ftLg_SpecialAirLw_Phys,ftLg_SpecialAirLw_Coll,ftCamera_UpdateCameraBox},
    {14,0,0x01000000,NULL,NULL,ftCo_800C7158,NULL,NULL},
    {15,0,0x01000000,NULL,NULL,ftCo_800C7200,NULL,NULL},
    {312,0x00340213,0x14000000,ftLg_SpecialHi_Anim,ftLg_SpecialHi_IASA,ftLg_SpecialHi_Phys,ftLg_SpecialHi_Coll,ftCamera_UpdateCameraBox},
    {313,0x00340613,0x14000000,ftLb_SpecialAirHi_Anim,ftLb_SpecialAirHi_IASA,ftLb_SpecialAirHi_Phys,ftLb_SpecialAirHi_Coll,ftCamera_UpdateCameraBox},
    {314,0,0x01000000,ftLb_Recovery_Anim,ftLb_Recovery_IASA,ftLb_Recovery_Phys,ftLb_Recovery_Coll,ftCamera_UpdateCameraBox},
    {315,0x00240A0B,0x0C800000,ftCo_AttackLw4_Anim,ftCo_AttackLw4_IASA,ftCo_AttackLw4_Phys,ftCo_AttackLw4_Coll,ftCamera_UpdateCameraBox},
    {66,0x00240A0A,0x0B800000,ftCo_AttackHi4_Anim,ftCo_AttackHi4_IASA,ftCo_AttackHi4_Phys,ftCo_AttackHi4_Coll,ftCamera_UpdateCameraBox},
    {62,0x00240A09,0x0A800000,ftCo_AttackS4_Anim,ftCo_AttackS4_IASA,ftCo_AttackS4_Phys,ftCo_AttackS4_Coll,ftCamera_UpdateCameraBox},
};

static ItemLogicTable* const ftLb_ArticleTables[] = { NULL, (ItemLogicTable*) &itLb_BooLogic };

const MuAkFighter mu_ak_luigiboo = {
    .name = "Luigi & Boo", .file = "PlLb.dat",
    .onload = ftLb_OnLoad,
    .specialn = ftLb_SpecialN_Enter, .specialairn = ftLb_SpecialAirN_Enter,
    .specials = ftLb_SpecialS_Enter, .specialairs = ftLb_SpecialAirS_Enter,
    .specialairhi = ftLb_SpecialAirHi_Enter,
    .speciallw = ftLb_SpecialLw_Enter, .specialairlw = ftLb_SpecialAirLw_Enter,
    .onframe = ftLb_OnFrame, .onrespawn = ftLb_OnRespawn,
    .onsmashf = ftLb_SmashF, .onsmashhi = ftLb_SmashHi, .onsmashlw = ftLb_SmashLw,
    .move_logic = ftLb_MotionStates, .move_logic_count = 26,
    .flags = 0, .article_tables = ftLb_ArticleTables, .article_count = 2,
#ifdef MU_AK_KIRBY_RETAIL_STATES
    .kirby = &ftLb_KirbyCopy,
#else
    .kirby = NULL,
#endif
};
