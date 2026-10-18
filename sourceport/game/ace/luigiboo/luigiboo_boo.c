/* PlLb.dat article 1. Offsets are offsets into its itFunction block. */
#include "luigiboo.h"
#include "luigiboo_boo.h"

#include <dolphin/os.h>
#include <melee/ft/inlines.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/tobj.h>

#include <math.h>
#include <stddef.h>

/* +3598. The console walks state ids downward while writing the byte array
 * upward. This reversal is the mod's behavior, including its unusual masks. */
static u8 itLb_BooStateMask[367];
static void itLb_Boo_3598(void)
{
    int i;
    for (i = 0; i < 367; ++i) {
        unsigned state = 367 - i;
        itLb_BooStateMask[i] =
            367u - state <= 13u || 292u - state <= 16u ||
            189u - state <= 40u || 101u - state <= 4u ||
            144u - state <= 5u || 128u - state <= 3u;
    }
}

bool itLb_BooOwnerMotionBlocked(int motion)
{
    /* Own states run through 366. A malformed native state has no console
     * byte-table entry; treat it as blocked instead of reading past data. */
    return motion < 0 || motion >= 367 || itLb_BooStateMask[motion] != 0;
}

/* +3624 is installed directly as the GObj user-data destructor. */
static void itLb_Boo_3624(void* data)
{
    Item* ip = data;
    itLb_BooVars* v = itLb_BooVarsOf(ip);
    void (*remove)(void*) = v->native_remove;
    HSD_Free(v->scratch);
    remove(ip);
}

/* +01B0. */
static void itLb_Boo_OnSpawn(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ip->xCC_item_attr->x10_fall_speed = 0.0f;
    itLb_BooVarsOf(ip)->flags = 0;
    ip->x40_vel.x = ip->x40_vel.y = ip->x40_vel.z = 0.0f;
    itLb_Boo_3598();
    Item_80268E5C(gobj, 0, 2);
    itLb_BooVarsOf(ip)->scratch = HSD_MemAlloc(0x50);
    itLb_BooVarsOf(ip)->native_remove = gobj->user_data_remove_func;
    gobj->user_data_remove_func = itLb_Boo_3624;
}

/* +0234 is exactly blr. */
static void itLb_Boo_OnDestroy(HSD_GObj* gobj) { (void) gobj; }

/* +0238, export slot 13: switch off all four hit capsules. */
static bool itLb_Boo_OnShield(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    int i;
    for (i = 0; i < 4; ++i) {
        ip->x5D4_hitboxes[i].hit.state = 0;
    }
    return false;
}

/* +3850, also fighter +2238. */
void itLb_Boo_3850(HSD_GObj* gobj, int joint_index)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* joint = it_80272CC0(gobj, joint_index);
    lb_8000B1CC(joint, NULL, &ip->pos);
    joint->translate.x = joint->translate.y = joint->translate.z = 0.0f;
}

/* +41E8, also fighter +20EC: preserve all four quaternion storage words,
 * including the otherwise unused w, across the item state change. */
void itLb_Boo_41E8(HSD_GObj* gobj, int state, int flags, int before, int after)
{
    HSD_JObj* joint = it_80272CC0(gobj, before);
    Quaternion rotation = joint->rotate;
    Item_80268E5C(gobj, state, flags);
    it_80272CC0(gobj, after)->rotate = rotation;
}

void itLb_BooAlpha(HSD_JObj* joint, float alpha)
{
    HSD_DObj* a = HSD_JObjGetDObj(joint);
    HSD_DObj* b = a != NULL ? a->next : NULL;
    HSD_DObj* c = b != NULL ? b->next : NULL;
    /* The original checks all three DObjs before touching any material. */
    if (a != NULL && b != NULL && c != NULL) {
        a->mobj->mat->alpha = alpha;
        b->mobj->mat->alpha = alpha;
        c->mobj->mat->alpha = alpha;
    }
}

/* +38C0. */
void itLb_Boo_38C0(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itLb_BooVars* v = itLb_BooVarsOf(ip);
    Fighter* owner = GET_FIGHTER(v->owner);
    HSD_JObj* joint;
    itLb_Boo_3850(gobj, 1);
    Item_80268E5C(gobj, 12, 2);
    joint = it_80272CC0(gobj, 0);
    ip->facing_dir = -ip->facing_dir;
    joint->rotate.x = joint->rotate.z = joint->rotate.w = 0.0f;
    joint->rotate.y = (float) ((double) owner->facing_dir * -0x1.921fb54442d18p+0);
    ip->x40_vel.x = ip->x40_vel.y = ip->x40_vel.z = 0.0f;
    v->flags &= ~1u;
    ip->xDAC_itcmd_var0 = 0;
    ip->xDB0_itcmd_var1 = 1;
}

/* +3990. The final item-joint lookup is a tail call and is returned. */
HSD_JObj* itLb_Boo_3990(HSD_GObj* gobj)
{
    HSD_JObj* joint;
    itLb_Boo_41E8(gobj, 4, 2, 0, 0);
    joint = it_80272CC0(gobj, 0);
    itLb_BooAlpha(joint, 0.5f);
    return it_80272CC0(gobj, 0);
}

static void itLb_Boo_AttackEnter(HSD_GObj* gobj, int state)
{
    Item* ip = GET_ITEM(gobj);
    itLb_BooVars* v = itLb_BooVarsOf(ip);
    Fighter* owner = GET_FIGHTER(v->owner);
    HSD_JObj* joint;
    itLb_Boo_3850(gobj, 1);
    Item_80268E5C(gobj, state, 2);
    joint = it_80272CC0(gobj, 0);
    joint->rotate.x = joint->rotate.z = joint->rotate.w = 0.0f;
    joint->rotate.y = (float) ((double) owner->facing_dir * 0x1.921fb54442d18p+0);
    v->flags &= ~1u;
    ip->xDAC_itcmd_var0 = ip->xDB0_itcmd_var1 = 1;
}

/* +3A60 and +3B14 differ only in state 10 versus 11. */
void itLb_Boo_3A60(HSD_GObj* gobj) { itLb_Boo_AttackEnter(gobj, 10); }
void itLb_Boo_3B14(HSD_GObj* gobj) { itLb_Boo_AttackEnter(gobj, 11); }

/* +4158: first AObj frame anywhere in this JObj/DObj/MObj/TObj branch,
 * or -1 when the whole searched subtree has no animation object. */
float itLb_Boo_4158(HSD_JObj* joint)
{
    for (; joint != NULL; joint = joint->child) {
        HSD_DObj* dobj;
        if (joint->aobj != NULL) {
            return joint->aobj->curr_frame;
        }
        for (dobj = joint->u.dobj; dobj != NULL; dobj = dobj->next) {
            HSD_TObj* texture;
            if (dobj->aobj != NULL) {
                return dobj->aobj->curr_frame;
            }
            texture = dobj->mobj->tobj;
            if (dobj->mobj->aobj != NULL) {
                return dobj->mobj->aobj->curr_frame;
            }
            for (; texture != NULL; texture = texture->next) {
                if (texture->aobj != NULL) {
                    return texture->aobj->curr_frame;
                }
            }
        }
    }
    return -1.0f;
}

/* The console's half-angle wrapping helper is kept instead of fmodf. */
static float itLb_Boo_WrapAngle(float angle)
{
    float half = angle * 0.5f;
    if (cosf(half) == 0.0f) {
        return 0x1.921fb6p+1f;
    }
    return 2.0f * atanf(sinf(half) / cosf(half));
}

/* +3BC8. */
void itLb_Boo_3BC8(HSD_GObj* gobj, int flags, float speed, float inner_radius,
                   float outer_radius)
{
    Item* ip = GET_ITEM(gobj);
    itLb_BooVars* v = itLb_BooVarsOf(ip);
    Fighter* fp = GET_FIGHTER(v->owner);
    float target_x = fp->cur_pos.x;
    float target_y = fp->cur_pos.y + 15.0f;
    float dx;
    float dy;
    float distance;
    HSD_JObj* joint;
    float angle;
    float abs_angle;
    float turn;
    double turn_limit;

    target_x = fp->facing_dir < 0.0f ? target_x - 7.0f : target_x + 7.0f;
    dx = ip->pos.x - target_x;
    dy = ip->pos.y - target_y;
    if (dx < 0.0f) dx = -dx;
    if (dy < 0.0f) dy = -dy;
    distance = sqrtf((float) __builtin_fma((double) dy, (double) dy,
                                          (double) dx * (double) dx));

    if (inner_radius <= distance) {
        float vx = (target_x - ip->pos.x) * 0.1f;
        float vy = (target_y - ip->pos.y) * 0.1f;
        ip->x40_vel.x = ip->x40_vel.x + vx / distance;
        ip->x40_vel.y = ip->x40_vel.y + vy / distance;
        if (ftLb_VectorLength(&ip->x40_vel) > speed) {
            ip->x40_vel.y = ((target_y - ip->pos.y) * speed) / distance;
            ip->x40_vel.x = ((target_x - ip->pos.x) * speed) / distance;
            ip->x40_vel.z = 0.0f;
        }
    } else if (inner_radius > distance && distance != 0.0f) {
        float ratio = distance / inner_radius;
        float vy = ((target_y - ip->pos.y) * speed) / distance;
        float vx = ((target_x - ip->pos.x) * speed) / distance;
        ip->x40_vel.x = vx * ratio;
        ip->x40_vel.y = vy * ratio;
        ip->x40_vel.z = 0.0f;
        if (!(flags & 8)) ip->facing_dir = fp->facing_dir;
        if (flags & 1) v->flags |= 1u;
        if (flags & 2) v->flags &= ~4u;
    } else {
        ip->x40_vel.x = ip->x40_vel.y = ip->x40_vel.z = 0.0f;
    }
    if (ip->pos.z != 0.0f) ip->x40_vel.z = -ip->pos.z;
    if (inner_radius + outer_radius < distance && !(flags & 8)) {
        ip->facing_dir = ip->x40_vel.x > 0.0f ? 1.0f : -1.0f;
    }
    joint = it_80272CC0(gobj, 0);
    if (flags & 4) return;
    if (fabsf(joint->rotate.y) > 0x1.921fb54442d18p+1) {
        joint->rotate.y = itLb_Boo_WrapAngle(joint->rotate.y);
    }
    angle = (float) __builtin_fma((double) ip->facing_dir,
                                 (double) 0x1.921fcp-1f,
                                 -(double) joint->rotate.y);
    angle = itLb_Boo_WrapAngle(angle);
    abs_angle = angle < 0.0f ? -angle : angle;
    if ((double) abs_angle > 15.0 * 0x1.1df4722d4405fp-6) {
        double step = angle > 0.0f ? 0x1.1df4722d4405fp-6 : -0x1.1df4722d4405fp-6;
        v->turn_velocity = (float) ((double) v->turn_velocity + step);
    } else {
        float scale = abs_angle / 15.0f;
        v->turn_velocity = scale * v->turn_velocity;
    }
    turn = v->turn_velocity;
    turn_limit = 10.0 * 0x1.1df4722d4405fp-6;
    if ((double) fabsf(turn) > turn_limit) {
        turn = (float) (((double) turn * turn_limit) / (double) fabsf(turn));
        v->turn_velocity = turn;
    }
    joint->rotate.x = (float) ((double) joint->rotate.x * 0.9);
    joint->rotate.y = joint->rotate.y + turn;
    joint->rotate.z = (float) ((double) joint->rotate.z * 0.9);
}

/* +429C. */
void itLb_Boo_429C(HSD_GObj* gobj, float limit, float speed, float acceleration)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = GET_FIGHTER(itLb_BooVarsOf(ip)->owner);
    Vec3 direction;
    Vec3 target = { fp->cur_pos.x, fp->cur_pos.y + 15.0f, 0.0f };
    Vec3 displacement;
    Vec3 projection;
    Vec3 perpendicular;
    Vec3 correction;
    float dot;
    float perpendicular_length;
    float projection_length;
    float correction_length;
    float ratio;
    HSD_JObj* joint;

    PSVECNormalize(&ip->x40_vel, &direction);
    target.x = fp->facing_dir < 0.0f ? target.x + 7.0f : target.x - 7.0f;
    displacement.x = target.x - ip->pos.x;
    displacement.y = target.y - ip->pos.y;
    displacement.z = 0.0f;
    dot = PSVECDotProduct(&displacement, &direction);
    projection.x = direction.x * dot;
    projection.y = direction.y * dot;
    projection.z = direction.z * dot;
    PSVECSubtract(&projection, &displacement, &perpendicular);
    perpendicular_length = ftLb_VectorLength(&perpendicular);
    projection_length = ftLb_VectorLength(&projection);
    ratio = perpendicular_length / projection_length;
    correction.x = ip->x40_vel.x * ratio;
    correction.y = ip->x40_vel.y * ratio;
    correction.z = ip->x40_vel.z * ratio;
    correction_length = ftLb_VectorLength(&correction);
    ratio = ftLb_VectorLength(&perpendicular) / ftLb_VectorLength(&displacement);
    limit = (1.0f - ratio) * limit;
    if (correction_length >= limit || __builtin_isnan(correction_length) || __builtin_isnan(limit)) {
        Vec3 clamped;
        PSVECNormalize(&perpendicular, &clamped);
        clamped.x *= limit;
        clamped.y *= limit;
        clamped.z *= limit;
        PSVECSubtract(&ip->x40_vel, &clamped, &ip->x40_vel);
    } else {
        PSVECSubtract(&ip->x40_vel, &correction, &ip->x40_vel);
    }
    joint = it_80272CC0(gobj, 0);
    if (ip->x40_vel.x != 0.0f) {
        float offset = ip->facing_dir > 0.0f ? 0.0f : 0x1.921fb6p+1f;
        joint->rotate.z = atan2f(ip->x40_vel.y, ip->x40_vel.x) + offset;
    } else {
        joint->rotate.z = ip->x40_vel.y > 0.0f ? 0x1.921fb6p+0f : -0x1.921fb6p+0f;
    }
    PSVECNormalize(&ip->x40_vel, &direction);
    ip->x40_vel.x = direction.x * speed;
    ip->x40_vel.y = direction.y * speed;
    ip->x40_vel.z = direction.z * speed;
    PSVECNormalize(&displacement, &direction);
    ip->x40_vel.x = (float) __builtin_fma((double) direction.x, (double) acceleration,
                                        (double) ip->x40_vel.x);
    ip->x40_vel.y = (float) __builtin_fma((double) direction.y, (double) acceleration,
                                        (double) ip->x40_vel.y);
}

/* +45B0 duplicates the fighter's +22A8 helper. The fighter owns its literal
 * translation, including the misfire callbacks installed on the owner. */
void itLb_Boo_45B0(HSD_GObj* gobj)
{
    ftLb_BooDetach(gobj);
}

/* +0258 and +1330 share the same button tree. The latter also handles the
 * end of state 6's animation. Redundant snap calls are kept at their sites. */
static bool itLb_Boo_Input(HSD_GObj* gobj, bool state6)
{
    Item* ip = GET_ITEM(gobj);
    itLb_BooVars* v = itLb_BooVarsOf(ip);
    Fighter* fp = GET_FIGHTER(v->owner);
    bool ready = (v->flags & 1) != 0;
    float x = fp->input.lstick[0].x;
    float y = fp->input.lstick[0].y;
    float abs_x = x < 0.0f ? -x : x;
    float abs_y = y < 0.0f ? -y : y;
    if (itLb_BooOwnerMotionBlocked(fp->motion_id)) {
        if (!state6) itLb_Boo_3850(gobj, 1);
        itLb_Boo_3990(gobj);
        return false;
    }
    if (fp->input.pressed_buttons & 0x200) {
        if ((double) abs_x <= 0.2875 && (double) y < -0.2875 && ready) {
            if (!state6) itLb_Boo_3850(gobj, 1);
            itLb_Boo_38C0(gobj);
            return false;
        }
        if ((double) abs_x > 0.2875 && ready) {
            if (!state6) itLb_Boo_3850(gobj, 1);
            itLb_Boo_3B14(gobj);
            return false;
        }
        if ((double) abs_y < 0.2875 && (double) abs_x < 0.2875 && ready) {
            itLb_Boo_3850(gobj, 1);
            itLb_Boo_3A60(gobj);
            return false;
        }
    }
    if (state6 && !it_80272C6C(gobj)) {
        itLb_Boo_41E8(gobj, 0, 2, 0, 0);
    }
    return false;
}

bool itLb_Boo_0258(HSD_GObj* gobj) { return itLb_Boo_Input(gobj, false); }
bool itLb_Boo_1330(HSD_GObj* gobj) { return itLb_Boo_Input(gobj, true); }

void itLb_Boo_03D8(HSD_GObj* gobj)
{
    if (itLb_BooVarsOf(GET_ITEM(gobj))->flags & 1) {
        itLb_Boo_3BC8(gobj, 1, 2.0f, 10.0f, 20.0f);
    } else {
        itLb_Boo_3BC8(gobj, 1, 1.0f, 10.0f, 0.0f);
    }
}

bool itLb_Boo_0464(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = GET_FIGHTER(itLb_BooVarsOf(ip)->owner);
    Vec3 target;
    if (it_80272C6C(gobj)) return false;
    Item_80268E5C(gobj, 2, 2);
    ip->facing_dir = fp->cur_pos.x - ip->pos.x > 0.0f ? 1.0f : -1.0f;
    target.x = fp->facing_dir < 0.0f ? fp->cur_pos.x + 7.0f : fp->cur_pos.x - 7.0f;
    target.x = target.x - ip->pos.x;
    target.y = (fp->cur_pos.y + 15.0f) - ip->pos.y;
    target.z = 0.0f;
    PSVECNormalize(&target, &ip->x40_vel);
    ip->x40_vel.z = 0.0f;
    it_80272CC0(gobj, 0)->rotate.y =
        (float) (((double) ip->facing_dir * 0x1.921fb54442d18p+1) * 0.5);
    return false;
}

static void itLb_Boo_Fade(HSD_JObj* joint, bool fade_in)
{
    HSD_DObj* a = HSD_JObjGetDObj(joint);
    HSD_DObj* b = a != NULL ? a->next : NULL;
    HSD_DObj* c = b != NULL ? b->next : NULL;
    float bound = fade_in ? 1.0f : 0.5f;
    if (a == NULL || b == NULL || c == NULL) return;
    if (fade_in ? c->mobj->mat->alpha < bound : c->mobj->mat->alpha > bound) {
        double step = fade_in ? 0.07 : -0.07;
        c->mobj->mat->alpha = (float) ((double) c->mobj->mat->alpha + step);
        a->mobj->mat->alpha = (float) ((double) a->mobj->mat->alpha + step);
        b->mobj->mat->alpha = (float) ((double) b->mobj->mat->alpha + step);
        if (fade_in ? c->mobj->mat->alpha > bound : c->mobj->mat->alpha < bound) {
            c->mobj->mat->alpha = a->mobj->mat->alpha = b->mobj->mat->alpha = bound;
        }
    }
}

static float itLb_Boo_TurnVelocity(itLb_BooVars* v, float angle)
{
    float abs_angle = angle < 0.0f ? -angle : angle;
    float velocity;
    double maximum = 10.0 * 0x1.1df4722d4405fp-6;
    if ((double) abs_angle > 15.0 * 0x1.1df4722d4405fp-6) {
        double step = angle > 0.0f ? 0x1.1df4722d4405fp-6 : -0x1.1df4722d4405fp-6;
        v->turn_velocity = (float) ((double) v->turn_velocity + step);
    } else {
        float scale = abs_angle / 15.0f;
        v->turn_velocity = scale * v->turn_velocity;
    }
    velocity = v->turn_velocity;
    if ((double) fabsf(velocity) > maximum) {
        velocity = (float) (((double) velocity * maximum) / (double) fabsf(velocity));
        v->turn_velocity = velocity;
    }
    return velocity;
}

/* +05C4. */
void itLb_Boo_05C4(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itLb_BooVars* v = itLb_BooVarsOf(ip);
    Fighter* fp = GET_FIGHTER(v->owner);
    Vec3 delta;
    Vec3 normalized;
    HSD_JObj* joint;
    float frame;
    float end_frame;
    float ratio;
    float phase;
    float factor;
    float direction;
    float diff;
    delta.x = fp->facing_dir < 0.0f ? fp->cur_pos.x + 7.0f : fp->cur_pos.x - 7.0f;
    delta.x = delta.x - ip->pos.x;
    delta.y = (fp->cur_pos.y + 15.0f) - ip->pos.y;
    delta.z = 0.0f;
    ip->facing_dir = delta.x > 0.0f ? 1.0f : -1.0f;
    joint = it_80272CC0(gobj, 0);
    if ((double) fabsf(joint->rotate.y) > 0x1.921fb54442d18p+1) {
        joint->rotate.y = itLb_Boo_WrapAngle(joint->rotate.y);
    }
    diff = (float) __builtin_fma((double) ip->facing_dir, 0x1.921fb54442d18p+0,
                                 -(double) joint->rotate.y);
    diff = itLb_Boo_WrapAngle(diff);
    joint->rotate.y = joint->rotate.y + itLb_Boo_TurnVelocity(v, diff);
    frame = itLb_Boo_4158(joint);
    end_frame = lbGetJObjEndFrame(joint);
    if (delta.x != 0.0f) {
        direction = delta.x > 0.0f ? atan2f(delta.y, delta.x) : -atan2f(delta.y, -delta.x);
        ratio = frame / end_frame;
        phase = (float) (((double) ratio - 0.5) * 0x1.921fb54442d18p+1);
        factor = (float) __builtin_fma((double) sinf(phase), 0.5, 0.5);
        joint->rotate.z = direction * factor;
    } else {
        ratio = frame / end_frame;
        joint->rotate.z = delta.y > 0.0f ? 0x1.921fb6p+0f : -0x1.921fb6p+0f;
    }
    itLb_Boo_Fade(it_80272CC0(gobj, 0), false);
    PSVECNormalize(&delta, &normalized);
    phase = (float) (((double) ratio - 0.5) * 0x1.921fb54442d18p+1);
    factor = (float) __builtin_fma((double) sinf(phase), 0.5, 0.5);
    ip->x40_vel.x = normalized.x * factor;
    factor = (float) __builtin_fma((double) sinf(phase), 0.5, 0.5);
    ip->x40_vel.y = normalized.y * factor;
}

bool itLb_Boo_0B80(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itLb_BooVars* v = itLb_BooVarsOf(ip);
    Fighter* fp = GET_FIGHTER(v->owner);
    float target_x;
    float dx;
    float dy;
    float distance;
    if (!it_80272C6C(gobj)) {
        itLb_Boo_41E8(gobj, 2, 2, 0, 0);
    }
    target_x = fp->facing_dir < 0.0f ? fp->cur_pos.x + 7.0f : fp->cur_pos.x - 7.0f;
    dx = ip->pos.x - target_x;
    dy = ip->pos.y - (fp->cur_pos.y + 15.0f);
    if (dx < 0.0f) dx = -dx;
    if (dy < 0.0f) dy = -dy;
    distance = sqrtf((float) __builtin_fma((double) dy, (double) dy,
                                          (double) dx * (double) dx));
    if (distance < 15.0f) {
        HSD_JObj* joint;
        itLb_Boo_3850(gobj, 1);
        itLb_Boo_41E8(gobj, 3, 2, 0, 0);
        joint = it_80272CC0(gobj, 0);
        joint->rotate.x = itLb_Boo_WrapAngle(joint->rotate.x);
        joint->rotate.y = itLb_Boo_WrapAngle(joint->rotate.y);
        joint->rotate.z = itLb_Boo_WrapAngle(joint->rotate.z);
        ip->x40_vel.x = ip->pos.x - v->previous_pos.x;
        ip->x40_vel.y = ip->pos.y - v->previous_pos.y;
        ip->x40_vel.z = ip->pos.z - v->previous_pos.z;
        v->turn_velocity = 0.0f;
    } else {
        v->previous_pos = ip->pos;
    }
    return false;
}

void itLb_Boo_0E40(HSD_GObj* gobj) { itLb_Boo_429C(gobj, 0.2f, 1.0f, 0.2f); }
bool itLb_Boo_0E80(HSD_GObj* gobj)
{
    if (!it_80272C6C(gobj)) {
        itLb_Boo_41E8(gobj, 0, 2, 0, 0);
        itLb_BooAlpha(it_80272CC0(gobj, 0), 1.0f);
    }
    return false;
}
void itLb_Boo_0F54(HSD_GObj* gobj)
{
    itLb_Boo_3BC8(gobj, 1, 1.5f, 10.0f, 20.0f);
    itLb_Boo_Fade(it_80272CC0(gobj, 0), true);
}

bool itLb_Boo_1094(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = GET_FIGHTER(itLb_BooVarsOf(ip)->owner);
    if (!itLb_BooOwnerMotionBlocked(fp->motion_id) && !(itLb_BooVarsOf(ip)->flags & 4)) {
        itLb_Boo_41E8(gobj, 6, 2, 0, 0);
        itLb_BooAlpha(it_80272CC0(gobj, 0), 1.0f);
    }
    if (!it_80272C6C(gobj)) itLb_Boo_41E8(gobj, 5, 2, 0, 0);
    return false;
}
void itLb_Boo_11BC(HSD_GObj* gobj) { itLb_Boo_3BC8(gobj, 3, 1.0f, 10.0f, 20.0f); }
bool itLb_Boo_1204(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = GET_FIGHTER(itLb_BooVarsOf(ip)->owner);
    if (!itLb_BooOwnerMotionBlocked(fp->motion_id) && !(itLb_BooVarsOf(ip)->flags & 4)) {
        itLb_Boo_41E8(gobj, 6, 2, 0, 0);
        itLb_BooAlpha(it_80272CC0(gobj, 0), 1.0f);
    }
    return false;
}
void itLb_Boo_12E8(HSD_GObj* gobj) { itLb_Boo_3BC8(gobj, 3, 1.0f, 10.0f, 20.0f); }
void itLb_Boo_14B0(HSD_GObj* gobj) { itLb_Boo_3BC8(gobj, 3, 1.0f, 10.0f, 20.0f); }

/* Each collision callback is exactly li r3,0; blr. */
#define ITLB_FALSE_CALLBACK(offset) \
    bool itLb_Boo_##offset(HSD_GObj* gobj) { (void) gobj; return false; }
ITLB_FALSE_CALLBACK(045C)
ITLB_FALSE_CALLBACK(0B78)
ITLB_FALSE_CALLBACK(0E78)
ITLB_FALSE_CALLBACK(108C)
ITLB_FALSE_CALLBACK(11FC)
ITLB_FALSE_CALLBACK(1328)
ITLB_FALSE_CALLBACK(14F0)
#undef ITLB_FALSE_CALLBACK

static ItemStateTable itLb_BooStates[27] = {
#define ITLB_STATE(id, anim, phys, coll) { id, itLb_Boo_##anim, itLb_Boo_##phys, itLb_Boo_##coll }
    ITLB_STATE(0,0258,03D8,045C), ITLB_STATE(1,0464,05C4,0B78),
    ITLB_STATE(2,0B80,0E40,0E78), ITLB_STATE(3,0E80,0F54,108C),
    ITLB_STATE(4,1094,11BC,11FC), ITLB_STATE(5,1204,12E8,1328),
    ITLB_STATE(6,1330,14B0,14F0), ITLB_STATE(7,14F8,15D4,1A18),
    ITLB_STATE(8,1B08,1B10,1B14), ITLB_STATE(9,1B1C,1C40,1C68),
    ITLB_STATE(10,1C70,1D94,1E10), ITLB_STATE(11,1E18,1F60,2020),
    ITLB_STATE(12,2028,2184,21EC), ITLB_STATE(13,21F4,21FC,2204),
    ITLB_STATE(14,220C,2380,2610), ITLB_STATE(15,2618,26D8,295C),
    ITLB_STATE(16,2964,2A74,2B78), ITLB_STATE(17,2B80,2C84,2DD0),
    ITLB_STATE(18,2DD8,2DE0,2DE8), ITLB_STATE(19,2DF0,2F28,3050),
    ITLB_STATE(20,3058,3184,3270), ITLB_STATE(21,3278,327C,3368),
    ITLB_STATE(22,3370,3378,3380), ITLB_STATE(23,3388,3390,3398),
    ITLB_STATE(24,33A0,33A8,33B0), ITLB_STATE(25,33B8,33C0,33C8),
    ITLB_STATE(26,33D0,34BC,34FC),
#undef ITLB_STATE
};

const ItemLogicTable itLb_BooLogic = {
    .states = itLb_BooStates,
    .spawned = itLb_Boo_OnSpawn,
    .destroyed = itLb_Boo_OnDestroy,
    .hit_shield = itLb_Boo_OnShield,
};
