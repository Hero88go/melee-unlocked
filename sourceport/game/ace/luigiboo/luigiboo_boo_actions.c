/* PlLb.dat article 1 +14F8..+3504. Native item callbacks only. */
#include "luigiboo_boo.h"
#include "luigiboo.h"

#include <math.h>
#include <melee/cm/types.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/mp/mpcoll.h>
#include <sysdolphin/baselib/jobj.h>

#define LB_PI 0x1.921fb54442d18p+1
#define LB_HALF_PI 0x1.921fb54442d18p+0
#define LB_QUARTER_PI 0x1.921fb54442d18p-1
#define LB_DEG 0x1.1df4722d4405fp-6
#define LB_PI_F 0x1.921fb6p+1f
#define LB_HALF_PI_F 0x1.921fb6p+0f

static Fighter* owner_fighter(Item* ip)
{
    return GET_FIGHTER(itLb_BooVarsOf(ip)->owner);
}

static void camera_range(Item* ip, float low, float high, bool current)
{
    ip->x520_cameraBox->target_ext.h.x = low;
    ip->x520_cameraBox->target_ext.h.y = high;
    if (current) {
        ip->x520_cameraBox->ext.h.x = low;
        ip->x520_cameraBox->ext.h.y = high;
    }
}

static void enter_idle(HSD_GObj* gobj)
{
    itLb_Boo_3850(gobj, 1);
    itLb_Boo_41E8(gobj, 1, 2, 0, 0);
}

/* Each of these six original callbacks has the same owner-mask test and
 * early-return body, before its own animation-end work. */
static bool animation_prefix(HSD_GObj* gobj, Item* ip)
{
    if (ip->xDB0_itcmd_var1 == 1 &&
        itLb_BooOwnerMotionBlocked(owner_fighter(ip)->motion_id)) {
        ip->xDB0_itcmd_var1 = 0;
        itLb_Boo_3850(gobj, 1);
        itLb_Boo_45B0(gobj);
        return true;
    }
    lb_8000B1CC(it_80272CC0(gobj, 1), NULL,
               &itLb_BooVarsOf(ip)->previous_pos);
    return it_80272C6C(gobj) != 0;
}

static float facing_half_pi(float facing)
{
    return (float) ((double) facing * LB_HALF_PI);
}

/* +14F8 */
bool itLb_Boo_14F8(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* root;
    camera_range(ip, -10.0f, 10.0f, true);
    if (it_80272C6C(gobj)) return false;
    root = it_80272CC0(gobj, 0);
    root->rotate.x = 0.0f;
    root->rotate.z = 0.0f;
    root->rotate.y = ip->facing_dir * 0.7853984832763672f;
    enter_idle(gobj);
    return false;
}

/* +15D4: half-angle wrapping, angular acceleration, then velocity damping.
 * The double constants and float rounding sites follow the listing. */
void itLb_Boo_15D4(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itLb_BooVars* vars = itLb_BooVarsOf(ip);
    HSD_JObj* root = it_80272CC0(gobj, 0);
    float angle = root->rotate.y;
    float abs_angle = angle < 0.0f ? -angle : angle;
    float half;
    float cosine;
    float delta;
    float abs_delta;
    float omega;
    float abs_omega;
    if ((double) abs_angle > LB_PI) {
        half = angle * 0.5f;
        cosine = cosf(half);
        if (cosine == 0.0f) {
            angle = LB_PI_F;
        } else {
            float sine = sinf(half);
            angle = atanf(sine / cosf(half));
            angle = angle + angle;
        }
        root->rotate.y = angle;
    }
    half = MU_FMADDS(ip->facing_dir, 0.7853984832763672f, -angle);
    half = half * 0.5f;
    cosine = cosf(half);
    omega = vars->turn_velocity;
    if (cosine == 0.0f) {
        /* +16A8 is always taken: 15 * degree < the float pi constant. */
        omega = (float) ((double) omega + LB_DEG);
    } else {
        float sine = sinf(half);
        delta = atanf(sine / cosf(half));
        delta = delta + delta;
        abs_delta = delta < 0.0f ? -delta : delta;
        if (!(abs_delta > 15.0 * LB_DEG)) {
            /* +1928 reaches +16AC with f12 holding abs(delta), not pi. */
            omega = (abs_delta / 15.0f) * omega;
        } else if (delta > 0.0f) {
            omega = (float) ((double) omega + LB_DEG);
        } else {
            omega = (float) ((double) omega - LB_DEG);
        }
    }
    vars->turn_velocity = omega;
    abs_omega = omega < 0.0f ? -omega : omega;
    if ((double) abs_omega > 10.0 * LB_DEG) {
        omega = (float) (((double) omega * (10.0 * LB_DEG)) / (double) abs_omega);
        vars->turn_velocity = omega;
    }
    ip->x40_vel.x = (float) ((double) ip->x40_vel.x * 0.95);
    ip->x40_vel.y = (float) ((double) ip->x40_vel.y * 0.95);
    ip->x40_vel.z = (float) ((double) ip->x40_vel.z * 0.95);
    root->rotate.x = root->rotate.x * 0.5f;
    root->rotate.z = root->rotate.z * 0.5f;
    root->rotate.y = root->rotate.y + omega;
    if (ip->x40_vel.x < 0.0f) {
        if (ip->x40_vel.x < -1.0f) ip->x40_vel.x = -1.0f;
    } else if (ip->x40_vel.x > 1.0f) {
        ip->x40_vel.x = 1.0f;
    }
    if (ip->x40_vel.y < 0.0f) {
        if ((double) -ip->x40_vel.y > 0.4) ip->x40_vel.y = -0.4f;
    } else if ((double) ip->x40_vel.y > 0.4) {
        ip->x40_vel.y = 0.4f;
    }
    if (ip->x40_vel.z < 0.0f) {
        if ((double) -ip->x40_vel.z > 0.4) ip->x40_vel.z = -0.4f;
    } else if ((double) ip->x40_vel.z > 0.4) {
        ip->x40_vel.z = 0.4f;
    }
}

bool itLb_Boo_1A18(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    CollData* coll = &ip->x378_itemColl;
    ftCollisionBox box = { 6.0f, -6.0f, { -6.0f, 0.0f }, { 6.0f, 0.0f } };
    float old_y = coll->cur_pos.y;
    coll->cur_pos.y = ip->pos.y;
    coll->last_pos = coll->cur_pos;
    coll->last_pos.y = old_y;
    coll->cur_pos.x = ip->pos.x;
    coll->cur_pos.z = ip->pos.z;
    coll->cur_pos.y -= 6.0f;
    if (ip->facing_dir < 0.0f) {
        box.left.x = 6.0f;
        box.right.x = -6.0f;
    }
    mpColl_800475F4(coll, &box);
    ip->pos.y = coll->cur_pos.y + 6.0f;
    return false;
}

bool itLb_Boo_1B08(HSD_GObj* gobj) { return false; }
void itLb_Boo_1B10(HSD_GObj* gobj) {}
bool itLb_Boo_1B14(HSD_GObj* gobj) { return false; }

bool itLb_Boo_1B1C(HSD_GObj* gobj)
{
    Fighter* fp = owner_fighter(GET_ITEM(gobj));
    float x;
    float y;
    float abs_y;
    if (!(fp->input.pressed_buttons & HSD_PAD_B)) return false;
    x = fp->input.lstick[0].x;
    if (x < 0.0f) x = -x;
    if ((double) x > 0.2875) {
        itLb_Boo_3850(gobj, 1);
        itLb_Boo_3B14(gobj);
        return false;
    }
    y = fp->input.lstick[0].y;
    if ((double) y < -0.2875) {
        itLb_Boo_3850(gobj, 1);
        itLb_Boo_38C0(gobj);
        return false;
    }
    abs_y = y < 0.0f ? -y : y;
    if ((double) abs_y < 0.2875 && (double) x < 0.2875) {
        itLb_Boo_3850(gobj, 1);
        itLb_Boo_3A60(gobj);
    }
    return false;
}

void itLb_Boo_1C40(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ip->pos = itLb_BooVarsOf(ip)->previous_pos;
}
bool itLb_Boo_1C68(HSD_GObj* gobj) { return false; }

bool itLb_Boo_1C70(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (animation_prefix(gobj, ip)) return false;
    enter_idle(gobj);
    it_80272CC0(gobj, 0)->rotate.y = facing_half_pi(ip->facing_dir);
    return false;
}
void itLb_Boo_1D94(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itLb_Boo_3BC8(gobj, 12, ip->xDAC_itcmd_var0 == 1 ? 4.0f : 0.3f, 10.0f, 20.0f);
}
bool itLb_Boo_1E10(HSD_GObj* gobj) { return false; }
bool itLb_Boo_1E18(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (animation_prefix(gobj, ip)) return false;
    enter_idle(gobj);
    it_80272CC0(gobj, 0)->rotate.y = facing_half_pi(ip->facing_dir);
    camera_range(ip, -10.0f, 10.0f, true);
    return false;
}
void itLb_Boo_1F60(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itLb_Boo_3BC8(gobj, 12, ip->xDAC_itcmd_var0 == 1 ? 4.0f : 0.3f, 10.0f, 20.0f);
    camera_range(ip, -40.0f, 40.0f, false);
}
bool itLb_Boo_2020(HSD_GObj* gobj) { return false; }
bool itLb_Boo_2028(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (animation_prefix(gobj, ip)) return false;
    enter_idle(gobj);
    ip->facing_dir = -ip->facing_dir;
    it_80272CC0(gobj, 0)->rotate.y = (float) __builtin_fma(
        (double) ip->facing_dir, LB_HALF_PI, LB_QUARTER_PI);
    camera_range(ip, -10.0f, 10.0f, true);
    return false;
}
void itLb_Boo_2184(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itLb_Boo_3BC8(gobj, 12, 1.7999999523162842f, 10.0f, 20.0f);
    camera_range(ip, -30.0f, 30.0f, false);
}
bool itLb_Boo_21EC(HSD_GObj* gobj) { return false; }
bool itLb_Boo_21F4(HSD_GObj* gobj) { return false; }
void itLb_Boo_21FC(HSD_GObj* gobj) {}
bool itLb_Boo_2204(HSD_GObj* gobj) { return false; }

bool itLb_Boo_220C(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = owner_fighter(ip);
    Vec3 destination;
    ftLb_Path path;
    HSD_JObj* root;
    if (it_80272C6C(gobj)) return false;
    ip->x40_vel = (Vec3) { 0.0f, 0.0f, 0.0f };
    itLb_Boo_3850(gobj, 1);
    itLb_Boo_41E8(gobj, 15, 2, 0, 0);
    destination = fp->cur_pos;
    destination.x = (float) __builtin_fma((double) fp->facing_dir, 5.0, destination.x);
    destination.y = destination.y + 30.0f;
    ip->pos.z = 0.0f;
    ftLb_PathSolve(&path, &ip->pos, &destination, 15, ftLb_EaseSquare,
                   7.0f, 0.34906598925590515f);
    ftLb_MV(fp)->path = path;
    root = it_80272CC0(gobj, 0);
    root->rotate.x = 0.0f;
    root->rotate.y = (float) (((double) ftLb_MV(fp)->facing * LB_PI) * 0.5);
    return false;
}

void itLb_Boo_2380(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = owner_fighter(ip);
    Vec3 point;
    float dx;
    float dy;
    float angle;
    float frame;
    float end_frame;
    float weight;
    double base;
    HSD_JObj* root;
    ftLb_PathSample(&point, &ftLb_MV(fp)->path, ftLb_EaseSquare,
                    (float) (1.0 / 15.0));
    dy = point.y - ip->pos.y;
    dx = point.x - ip->pos.x;
    if (dy > 0.0f) dy = -dy;
    root = it_80272CC0(gobj, 0);
    base = (double) ftLb_MV(fp)->facing * LB_PI;
    root->rotate.x = (float) ((double) root->rotate.x * 0.7);
    root->rotate.y = (float) __builtin_fma(base, 0.5,
        __builtin_fma(-base, 0.5, (double) root->rotate.y) * 0.7);
    frame = itLb_Boo_4158(root);
    end_frame = lbGetJObjEndFrame(root);
    if (dx != 0.0f) {
        if (ftLb_MV(fp)->facing > 0.0f) angle = atan2f(dy, dx);
        else angle = -atan2f(dy, -dx);
        /* +24D8 fdivs, then double subtraction and multiplication. */
        base = ((double) (frame / end_frame) - 0.5) * LB_PI;
        weight = MU_FMADDS(sinf((float) base), 0.5f, 0.5f);
        root->rotate.z = angle * weight;
    } else {
        /* The original makes dy nonpositive immediately before this test. */
        root->rotate.z = dy > 0.0f ? LB_HALF_PI_F : -LB_HALF_PI_F;
    }
}
bool itLb_Boo_2610(HSD_GObj* gobj) { return false; }

bool itLb_Boo_2618(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = owner_fighter(ip);
    HSD_JObj* child;
    Vec3 scale;
    if (!(ftLb_MV(fp)->path_time >= 1.0f)) return false;
    child = it_80272CC0(gobj, 1);
    scale = child->scale;
    ftLb_MV(fp)->path_time = 0.0f;
    itLb_Boo_41E8(gobj, 16, 2, 0, 0);
    child->scale = scale;
    return false;
}

void itLb_Boo_26D8(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = owner_fighter(ip);
    ftLb_MotionVars* mv = ftLb_MV(fp);
    Vec3 start;
    Vec3 destination;
    Vec3 point;
    ftLb_Path path;
    float dx;
    float dy;
    float length;
    float ratio;
    HSD_JObj* root;
    HSD_JObj* child;
    if (mv->hitlag != 1) {
        mv->path_time = (float) (1.0 / 15.0 + (double) mv->path_time);
        itLb_BooVarsOf(ip)->previous_pos = ip->pos;
    }
    destination = fp->cur_pos;
    destination.x = (float) __builtin_fma((double) fp->facing_dir, 5.0, destination.x);
    destination.y = mv->path.end.y;
    start = (Vec3) { mv->path.start.x, mv->path.start.y, 0.0f };
    ftLb_PathSolve(&path, &start, &destination, 15, ftLb_EaseSquare,
                   7.0f, 0.34906598925590515f);
    mv->path = path;
    ftLb_PathSample(&point, &mv->path, ftLb_EaseSquare, mv->path_time);
    ip->pos = point;
    root = it_80272CC0(gobj, 0);
    dx = ip->pos.x - itLb_BooVarsOf(ip)->previous_pos.x;
    dy = ip->pos.y - itLb_BooVarsOf(ip)->previous_pos.y;
    if (dx != 0.0f) {
        if (mv->facing > 0.0f) root->rotate.z = atan2f(dy, dx);
        else root->rotate.z = -atan2f(dy, -dx);
    } else {
        root->rotate.z = dy > 0.0f ? LB_HALF_PI_F : -LB_HALF_PI_F;
    }
    child = it_80272CC0(gobj, 1);
    length = sqrtf(MU_FMADDS(dx, dx, dy * dy));
    ratio = length / 10.0f;
    ratio = ratio + 1.0f;
    ratio = 1.0f / ratio;
    child->scale.x = child->scale.y = (ratio + 1.0f) * 0.5f;
    child->scale.z = -(ratio - 2.0f);
}
bool itLb_Boo_295C(HSD_GObj* gobj) { return false; }

bool itLb_Boo_2964(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = owner_fighter(ip);
    ftLb_MotionVars* mv = ftLb_MV(fp);
    HSD_JObj* root;
    float abs_x;
    if (fp->motion_id == 362) return false;
    mv->path_time = 0.0f;
    itLb_Boo_41E8(gobj, 17, 2, 0, 0);
    root = it_80272CC0(gobj, 0);
    root->scale = (Vec3) { 1.0f, 1.0f, 1.0f };
    mv->stick_x = fp->input.lstick[0].x;
    mv->stick_y = fp->input.lstick[0].y;
    abs_x = mv->stick_x < 0.0f ? -mv->stick_x : mv->stick_x;
    if ((double) abs_x > 0.2875) {
        fp->facing_dir = ip->facing_dir = mv->stick_x > 0.0f ? 1.0f : -1.0f;
    }
    return false;
}

void itLb_Boo_2A74(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = owner_fighter(ip);
    Vec3 point;
    HSD_JObj* root;
    lb_8000B1CC(fp->parts[ftParts_GetBoneIndex(fp, 0x28)].joint, NULL, &point);
    ip->pos = point;
    root = it_80272CC0(gobj, 0);
    root->rotate.x = (float) ((double) root->rotate.x * 0.7);
    root->rotate.z = (float) ((double) root->rotate.z * 0.7);
    root->scale.x = (float) __builtin_fma((double) (root->scale.x - 1.0f), 0.7, 1.0);
    root->scale.y = (float) __builtin_fma((double) (root->scale.y - 1.0f), 0.7, 1.0);
    root->scale.z = (float) __builtin_fma((double) (root->scale.z - 1.0f), 0.7, 1.0);
}
bool itLb_Boo_2B78(HSD_GObj* gobj) { return false; }

bool itLb_Boo_2B80(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Vec3 unused;
    if (it_80272C6C(gobj)) return false;
    ip->x40_vel = (Vec3) { 0.0f, 0.0f, 0.0f };
    lb_8000B1CC(it_80272CC0(gobj, 0), NULL, &unused);
    lb_8000B1CC(it_80272CC0(gobj, 1), NULL, &unused);
    enter_idle(gobj);
    lb_8000B1CC(it_80272CC0(gobj, 0), NULL, &unused);
    it_80272CC0(gobj, 0)->rotate.y = facing_half_pi(ip->facing_dir);
    return false;
}

void itLb_Boo_2C84(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = owner_fighter(ip);
    ftLb_MotionVars* mv = ftLb_MV(fp);
    HSD_JObj* root;
    double target_y;
    float target_z;
    if (fp->cmd_vars[1] != 2) return;
    root = it_80272CC0(gobj, 0);
    target_y = ((double) ip->facing_dir * LB_PI) * 0.5;
    if ((double) root->rotate.y != target_y) {
        float rounded = (float) target_y;
        root->rotate.y = (float) __builtin_fma((double) (root->rotate.y - rounded), 0.8, rounded);
    }
    if (mv->stick_x == 0.0f) {
        float vertical = -mv->stick_y * mv->facing;
        target_z = vertical > 0.0f ? -LB_HALF_PI_F : LB_HALF_PI_F;
    } else if (ip->facing_dir > 0.0f) {
        target_z = atan2f(mv->stick_y, mv->stick_x);
    } else {
        target_z = -atan2f(mv->stick_y, -mv->stick_x);
    }
    root->rotate.z = (float) __builtin_fma((double) (root->rotate.z - target_z), 0.8, target_z);
}
bool itLb_Boo_2DD0(HSD_GObj* gobj) { return false; }
bool itLb_Boo_2DD8(HSD_GObj* gobj) { return false; }
void itLb_Boo_2DE0(HSD_GObj* gobj) {}
bool itLb_Boo_2DE8(HSD_GObj* gobj) { return false; }

bool itLb_Boo_2DF0(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (animation_prefix(gobj, ip)) return false;
    enter_idle(gobj);
    ip->facing_dir = -ip->facing_dir;
    it_80272CC0(gobj, 0)->rotate.y = (float) __builtin_fma(
        (double) ip->facing_dir, LB_HALF_PI, LB_QUARTER_PI);
    return false;
}

static void follow_smash(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = owner_fighter(ip);
    HSD_JObj* root;
    float x = fp->cur_pos.x;
    if (fp->facing_dir < 0.0f) x -= 7.0f;
    else x += 7.0f;
    ip->pos.x = x;
    ip->pos.y = fp->cur_pos.y + 15.0f;
    root = it_80272CC0(gobj, 0);
    root->rotate.y = (float) (((double) fp->facing_dir * LB_PI) * 0.5);
}

void itLb_Boo_2F28(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (ip->xDAC_itcmd_var0 == 0) {
        itLb_Boo_3BC8(gobj, 12, 1.7999999523162842f, 10.0f, 20.0f);
    } else if (ip->xDAC_itcmd_var0 == 1) {
        follow_smash(gobj);
    } else {
        itLb_Boo_3BC8(gobj, 12, 0.3f, 10.0f, 20.0f);
    }
}
bool itLb_Boo_3050(HSD_GObj* gobj) { return false; }
bool itLb_Boo_3058(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (animation_prefix(gobj, ip)) return false;
    enter_idle(gobj);
    it_80272CC0(gobj, 0)->rotate.y = (float) __builtin_fma(
        (double) ip->facing_dir, LB_HALF_PI, LB_QUARTER_PI);
    return false;
}
void itLb_Boo_3184(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (ip->xDAC_itcmd_var0 == 0) itLb_Boo_3BC8(gobj, 12, 1.7999999523162842f, 10.0f, 20.0f);
    else follow_smash(gobj);
}
bool itLb_Boo_3270(HSD_GObj* gobj) { return false; }
bool itLb_Boo_3278(HSD_GObj* gobj) { return itLb_Boo_3058(gobj); }
void itLb_Boo_327C(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (ip->xDAC_itcmd_var0 == 0) itLb_Boo_3BC8(gobj, 12, 1.7999999523162842f, 10.0f, 20.0f);
    else follow_smash(gobj);
}
bool itLb_Boo_3368(HSD_GObj* gobj) { return false; }
bool itLb_Boo_3370(HSD_GObj* gobj) { return false; }
void itLb_Boo_3378(HSD_GObj* gobj) {}
bool itLb_Boo_3380(HSD_GObj* gobj) { return false; }
bool itLb_Boo_3388(HSD_GObj* gobj) { return false; }
void itLb_Boo_3390(HSD_GObj* gobj) {}
bool itLb_Boo_3398(HSD_GObj* gobj) { return false; }
bool itLb_Boo_33A0(HSD_GObj* gobj) { return false; }
void itLb_Boo_33A8(HSD_GObj* gobj) {}
bool itLb_Boo_33B0(HSD_GObj* gobj) { return false; }
bool itLb_Boo_33B8(HSD_GObj* gobj) { return false; }
void itLb_Boo_33C0(HSD_GObj* gobj) {}
bool itLb_Boo_33C8(HSD_GObj* gobj) { return false; }

bool itLb_Boo_33D0(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (animation_prefix(gobj, ip)) return false;
    Item_80268E5C(gobj, 9, 2);
    return false;
}
void itLb_Boo_34BC(HSD_GObj* gobj)
{
    itLb_Boo_3BC8(gobj, 12, 1.7999999523162842f, 10.0f, 20.0f);
}
bool itLb_Boo_34FC(HSD_GObj* gobj) { return false; }
