/* Akaneia's Lucas: Rope Snake. The zair / tether (AirCatch, AirCatchHit) is a small verlet rope
 * simulation driving the snake article's joints: masses built along the snake's bones, springs
 * between them, stage collision per mass, and a ledge search at the tip. Also the grab's wall
 * rebound (the snake hits a wall in front of him) and the debug-style line renderer for the rope.
 * Source: the disc's "src/fighters/lucas/aircatch.c" (code+0x3480..0x7DC8). */
#include "lucas.h"

#include <math.h>
#include <string.h>

#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcliffcommon.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/kinds/ftCommon/ftCo_AirCatch.h>
#include <melee/ft/kinds/ftCommon/ftCo_CliffJump.h>
#include <melee/ft/kinds/ftCommon/ftCo_FallSpecial.h>
#include <melee/ft/kinds/ftCommon/ftCo_Landing.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/it/it_2725.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>

/* ---------------------------------------------------------------------------------------------
 * Rope simulation.
 * ------------------------------------------------------------------------------------------- */

/* code+0x7D1C "Vec3_Distance" */
static float ftLc_Rope_Distance(const Vec3* a, const Vec3* b)
{
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float dz = a->z - b->z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* code+0x7A7C "Vec3_DistanceSquared" */
static float ftLc_Rope_DistanceSq(const Vec3* a, const Vec3* b)
{
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float dz = a->z - b->z;
    return dx * dx + dy * dy + dz * dz;
}

/* code+0x6F40 "Mass_Update": verlet step. Only x is damped; unpinned masses fall by their own
 * gravity after the step. */
static void ftLc_Mass_Update(LucasMass* m)
{
    float vx = (m->pos.x - m->prev_pos.x) * m->damping;
    float vy = m->pos.y - m->prev_pos.y;
    float vz = m->pos.z - m->prev_pos.z;
    m->prev_pos = m->pos;
    m->pos.x += vx;
    m->pos.y += vy;
    m->pos.z += vz;
    if (!m->pinned) {
        m->pos.y -= m->gravity;
    }
}

/* code+0x7904 "Spring_Update": a rope segment only pulls (it acts once stretched past max_len
 * when one is set), moving both ends half the correction each. */
static void ftLc_Spring_Update(LucasSpring* s, float stiffness)
{
    Vec3 a = s->a->pos;
    Vec3 b = s->b->pos;
    float dist = ftLc_Rope_Distance(&b, &a);
    float cur;
    float k;

    if (s->rest_len == 0.0f) {
        return;
    }
    cur = dist * stiffness;
    if (cur == 0.0f) {
        return;
    }
    if (s->max_len != 0.0f && s->max_len * stiffness >= cur) {
        return;
    }
    k = ((s->rest_len * stiffness - cur) / cur) * 0.5f;
    {
        float dx = (b.x - a.x) * k;
        float dy = (b.y - a.y) * k;
        float dz = (b.z - a.z) * k;
        s->a->pos.x = a.x - dx;
        s->a->pos.y = a.y - dy;
        s->a->pos.z = a.z - dz;
        s->b->pos.x = s->b->pos.x + dx;
        s->b->pos.y = s->b->pos.y + dy;
        s->b->pos.z = s->b->pos.z + dz;
    }
}

/* code+0x6FB8 "Manager_UpdateConstrains" */
static void ftLc_Rope_Constrain(LucasRope* rope)
{
    int i;
    for (i = 0; i < rope->spring_count; i++) {
        ftLc_Spring_Update(&rope->springs[i], rope->stiffness);
    }
    for (i = 0; i < rope->mass_count; i++) {
        LucasMass* m = &rope->masses[i];
        if (m->pinned) {
            m->pos = m->pin_pos;
        }
        if (rope->mass_cb != NULL) {
            rope->mass_cb(rope, m);
        }
    }
}

/* code+0x6940 "Manager_Update" */
static void ftLc_Rope_Update(LucasRope* rope)
{
    int i;
    for (i = 0; i < rope->mass_count; i++) {
        ftLc_Mass_Update(&rope->masses[i]);
    }
    for (i = 0; i < rope->iterations; i++) {
        ftLc_Rope_Constrain(rope);
    }
}

/* code+0x7CB0 "Manager_CreateMass" */
static LucasMass* ftLc_Rope_AddMass(LucasRope* rope, const Vec3* pos, float gravity)
{
    LucasMass* m = &rope->masses[rope->mass_count];
    m->gravity = gravity;
    m->damping = 0.99f;
    m->pos = *pos;
    m->prev_pos = *pos;
    m->pin_pos = *pos;
    m->pinned = 0;
    m->x1 = 0;
    rope->mass_count++;
    return m;
}

/* code+0x7C40 "Spring_CalculateLength" */
static void ftLc_Spring_SetRestLength(LucasSpring* s)
{
    Vec3 a = s->a->pos;
    Vec3 b = s->b->pos;
    s->rest_len = ftLc_Rope_Distance(&a, &b);
}

/* code+0x7580 "Physics_SetJOBJRope": masses along the joints first..last of `root`, `segments`
 * per joint (lerped toward the next joint), chained by springs. The last joint gets one mass. */
static void ftLc_Rope_Build(LucasRope* rope, HSD_JObj* root, int first, int last, int segments)
{
    LucasMass* prev = NULL;
    int joint;

    if (root == NULL || first > last) {
        return;
    }
    for (joint = first; joint <= last; joint++) {
        int seg;
        for (seg = 0; seg < segments; seg++) {
            HSD_JObj* j0;
            HSD_JObj* j1;
            Vec3 p0, p1, pos;
            LucasMass* m;

            if (rope->mass_count > LUCAS_ROPE_MAX_MASSES) {
                /* The console reports "too many physics masses" and halts (__assert). */
                OSReport("found: %d max: %d\n", first - last, LUCAS_ROPE_MAX_MASSES);
                return;
            }
            lb_80011E24(root, &j0, joint, -1);
            lb_80011E24(root, &j1, joint + 1, -1);
            lb_8000B1CC(j0, NULL, &p0);
            lb_8000B1CC(j1, NULL, &p1);
            if (segments <= 1) {
                pos = p0;
            } else {
                float t = (float) ((double) seg / ((double) segments - 1.0));
                pos.x = (p1.x - p0.x) * t + p0.x;
                pos.y = (p1.y - p0.y) * t + p0.y;
                pos.z = (p1.z - p0.z) * t + p0.z;
            }
            m = ftLc_Rope_AddMass(rope, &pos, 0.05f);
            m->joint = seg == 0 ? j0 : NULL;
            if (prev != NULL) {
                LucasSpring* s;
                if (rope->spring_count > LUCAS_ROPE_MAX_SPRINGS) {
                    OSReport("found: %d max: %d\n", first - last, LUCAS_ROPE_MAX_SPRINGS);
                    return;
                }
                s = &rope->springs[rope->spring_count++];
                s->a = prev;
                s->b = m;
                ftLc_Spring_SetRestLength(s);
            }
            if (joint == last) {
                return;
            }
            prev = m;
        }
    }
}

/* code+0x5434 "RetractSnake" */
static void ftLc_Rope_Retract(LucasRope* rope, float min_len, float step)
{
    int i;
    for (i = 0; i < rope->spring_count; i++) {
        if (rope->springs[i].rest_len > min_len) {
            rope->springs[i].rest_len -= step;
        }
    }
}

/* code+0x5680 "MassWallCollisionCallback": stop a mass at stage lines crossed since last step. */
static void ftLc_Rope_MassCollide(LucasRope* rope, LucasMass* m)
{
    Vec3 hit;
    int line_id;
    CollLine* line;

    if (m->pinned) {
        return;
    }
    if (!mpCheckAllRemap(&hit, &line_id, NULL, NULL, -1, -1, m->prev_pos.x,
                         m->prev_pos.y + 2.0f, m->pos.x, m->pos.y))
    {
        return;
    }
    line = &mpGetGroundCollLine()[line_id];
    /* console CollLine+5 bit 0 and MapLine+0xE bit 0 */
    if (!(line->flags & 0x10000) || (line->x0->lo_flags & 0x100)) {
        return;
    }
    if ((line->flags & 1) && m->prev_pos.y > m->pos.y) {
        m->pos.y = hit.y; /* floor, falling */
    }
    if ((line->flags & 2) && m->prev_pos.y < m->pos.y) {
        m->pos.y = hit.y; /* ceiling, rising */
    }
    if (line->flags & 0xC) {
        m->pos.x = hit.x; /* wall */
    }
}

/* code+0x660C "Lucas_UpdateSnakePhysicsModel": pose the snake's joints from the masses. */
static void ftLc_Rope_PoseSnake(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);
    LucasRope* rope = vars->rope;
    Item_GObj* snake;
    HSD_JObj* head;
    HSD_JObj* first;
    HSD_JObj* first_next;
    HSD_JObj* tip;
    float xscale;
    int i;

    if ((u32) (fp->motion_id - ftLc_MS_AirCatch) > 1) {
        fp->accessory4_cb = NULL;
        return;
    }
    xscale = fp->facing_dir * ftLc_MV(fp)->aircatch.rope_scale * fp->x34_scale.y;
    snake = vars->held_item_gobj;

    head = it_80272CC0(snake, 11);
    head->rotate.x = 0.0f;
    head->rotate.y = 0.0f;
    if ((fp->cmd_vars[0] != 0 && fp->motion_id == ftLc_MS_AirCatch) ||
        fp->motion_id == ftLc_MS_AirCatchHit)
    {
        head->rotate.z = -1.5f;
    } else {
        head->rotate.z = 0.0f;
    }

    first = it_80272CC0(snake, attrs->xF4_ROPE_FIRST_JOINT);
    first_next = it_80272CC0(snake, attrs->xF4_ROPE_FIRST_JOINT + 1);
    tip = it_80272CC0(snake, attrs->xF8_ROPE_TIP_JOINT);

    /* The disc walks one past the last mass (<=); that slot's joint is NULL. */
    for (i = 0; i <= rope->mass_count; i++) {
        LucasMass* m = &rope->masses[i];
        HSD_JObj* j = m->joint;
        float x, y, angle = 0.0f;
        int k;

        if (j == NULL) {
            continue;
        }
        j->flags |= 0x03800000; /* the joint's matrix is set here, not from its SRT */
        x = m->pos.x;
        y = m->pos.y;
        for (k = 0; k < rope->spring_count; k++) {
            LucasSpring* s = &rope->springs[k];
            if (s->b == m) {
                angle = atan2f(y - s->a->pos.y, x - s->a->pos.x);
            }
            if (s->a == m) {
                angle = atan2f(s->b->pos.y - y, s->b->pos.x - x);
            }
        }
        if (j == tip) {
            if ((fp->motion_id == ftLc_MS_AirCatch && fp->cmd_vars[0] != 0) ||
                fp->motion_id == ftLc_MS_AirCatchHit)
            {
                angle = fp->facing_dir == 1.0f ? 0.0f : 3.1415927f;
            }
        }
        MTXRotRad(j->mtx, 'z', angle);
        j->mtx[0][3] = x;
        j->mtx[1][3] = y;
        j->mtx[2][3] = 0.0f;
        j->mtx[0][1] *= xscale;
        j->mtx[1][1] *= xscale;
        j->mtx[2][1] *= xscale;
        if (j == first || j == first_next) {
            j->mtx[2][3] = m->pos.z;
        }
        HSD_JObjSetMtxDirtySub(j);
    }
}

/* code+0x6B48 "Lucas_EnableSnakeRagdoll": build the rope from the snake's joints and fling it. */
static void ftLc_Rope_Launch(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    LucasRope* rope = ftLc_Vars(fp)->rope;
    int i;

    rope->mass_count = 0;
    rope->spring_count = 0;
    ftLc_Rope_Build(rope, it_80272CC0(ftLc_Vars(fp)->held_item_gobj, 0),
                    attrs->xF4_ROPE_FIRST_JOINT, attrs->xF8_ROPE_TIP_JOINT,
                    attrs->x104_ROPE_SEGMENTS);
    for (i = 1; i < rope->mass_count; i++) {
        rope->masses[i].prev_pos.x -= attrs->x10C_ROPE_LAUNCH * fp->facing_dir + fp->self_vel.x;
    }
    if (rope->mass_count > 0) {
        rope->masses[rope->mass_count - 1].gravity = 0.0f;
    } else {
        /* With no mass the console writes masses[-1].gravity, which is the iteration count. */
        rope->iterations = 0;
    }
    for (i = 1; i < rope->spring_count; i++) {
        rope->springs[i].rest_len += attrs->x10C_ROPE_LAUNCH * 0.5f;
    }
}

/* ---------------------------------------------------------------------------------------------
 * Tether.
 * ------------------------------------------------------------------------------------------- */

/* code+0x7094 "Ledge_Find": the nearest grabbable floor edge above `pos` within a w x h box,
 * on the side he faces. */
static bool ftLc_FindLedge(Vec3* pos, int* dir_out, int* line_out, float facing, float w, float h)
{
    CollLine* lines = mpGetGroundCollLine();
    CollJoint* joint;
    float best = 100000.0f;
    bool found = false;
    Vec3 p = { 0.0f, 0.0f, 0.0f };

    for (joint = mu_ak_mp_joint_list(); joint != NULL; joint = joint->next) {
        int group;
        for (group = 0; group < 2; group++) {
            struct MapLineRange range =
                joint->inner->ranges[group == 0 ? MapLineGroup_Floor : MapLineGroup_Dynamic];
            int start = (u16) range.start;
            int end = start + (u16) range.count;
            int idx;
            for (idx = start; idx < end; idx++) {
                float d2;
                if (!(lines[idx].x0->lo_flags & 0x200)) { /* grabbable edge */
                    continue;
                }
                if (facing == 1.0f) {
                    mpFloorGetLeft(idx, &p);
                } else if (facing == -1.0f) {
                    mpFloorGetRight(idx, &p);
                }
                if (pos->y >= p.y) {
                    continue;
                }
                if (p.x <= pos->x - w * 0.5f || p.x >= pos->x + w * 0.5f) {
                    continue;
                }
                if (p.y <= pos->y - h * 0.5f || p.y >= pos->y + h * 0.5f) {
                    continue;
                }
                d2 = ftLc_Rope_DistanceSq(&p, pos);
                if (d2 >= best) {
                    continue;
                }
                *dir_out = (int) facing;
                *line_out = idx;
                best = d2;
                found = true;
            }
        }
    }
    return found;
}

/* code+0x54C4 "Lucas_UpdateTetherPosition": hang Lucas so his hand sits on the rope's second
 * mass. */
static void ftLc_Tether_PlaceFighter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    LucasRope* rope = ftLc_Vars(fp)->rope;
    Vec3 hand;
    HSD_JObj* root;

    lb_8000B1CC(fp->parts[ftLc_Attrs(fp)->xF0_TETHER_HAND_PART].joint, NULL, &hand);
    hand.x -= fp->cur_pos.x;
    hand.y -= fp->cur_pos.y;
    hand.z -= fp->cur_pos.z;
    fp->cur_pos.x = rope->masses[1].pos.x - hand.x;
    fp->cur_pos.y = rope->masses[1].pos.y - hand.y;
    fp->cur_pos.z = rope->masses[1].pos.z - hand.z;
    root = fp->parts[0].joint;
    root->translate = fp->cur_pos;
    HSD_JObjSetMtxDirtySub(root);
}

/* code+0x7AB8 "Accessory_AirCatchHit_UpdateSnakePhysics" */
static void ftLc_AirCatchHit_Accessory(HSD_GObj* gobj)
{
    LucasRope* rope = ftLc_Vars(GET_FIGHTER(gobj))->rope;
    ftLc_Rope_PoseSnake(gobj);
    if (rope->mass_count > 0) {
        ftLc_Rope_Update(rope);
    }
}

/* code+0x7350 "Enter_AirCatchHit": the tip caught `ledge` on `line_id`. */
static void ftLc_AirCatchHit_Enter(HSD_GObj* gobj, int line_id, Vec3* ledge)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    LucasRope* rope = vars->rope;
    int i;

    efSync_Spawn(0x41C, gobj);
    ft_80088478(fp, 4, 0x7F, 0x40);
    Fighter_ChangeMotionState(gobj, ftLc_MS_AirCatchHit, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    mv->aircatch.line_id = line_id;
    mv->aircatch.reeling = 0;
    mv->aircatch.timer = attrs->x11C_HANG_FRAMES;
    mv->aircatch.rope_scale = 1.0f;
    fp->accessory4_cb = ftLc_AirCatchHit_Accessory;
    fp->death2_cb = ftLc_Snake_DestroyCB;
    fp->take_dmg_cb = ftLc_Snake_DestroyCB;
    Item_80268E5C(vars->held_item_gobj, 4, ITEM_ANIM_UPDATE);

    for (i = 0; i < rope->mass_count; i++) {
        LucasMass* m = &rope->masses[i];
        /* Nothing may hang past the ledge. */
        if (fp->facing_dir > 0.0f ? m->pos.x > ledge->x
                                  : (fp->facing_dir < 0.0f && m->pos.x < ledge->x))
        {
            m->pos.x = ledge->x;
        }
        m->damping = attrs->x100_ROPE_HANG_DAMPING;
        m->pinned = 0;
        m->prev_pos = m->pos;
    }
    /* Carry his momentum into the rope. */
    rope->masses[1].prev_pos.x = rope->masses[1].pos.x - fp->self_vel.x;
    rope->masses[1].prev_pos.y = rope->masses[1].pos.y - fp->self_vel.y;
    rope->masses[1].prev_pos.z = rope->masses[1].pos.z - fp->self_vel.z;
    fp->self_vel.x = 0.0f;
    fp->self_vel.y = 0.0f;
    fp->self_vel.z = 0.0f;
    rope->masses[rope->mass_count - 1].pinned = 1;
    rope->masses[rope->mass_count - 1].pin_pos = *ledge;
    ftLc_Tether_PlaceFighter(gobj);
}

/* code+0x69C4 "Lucas_CheckEnterTether": did the rope segment from..to hit the stage? If so,
 * latch onto the nearest ledge around `to`, or onto the hit point itself. */
static bool ftLc_Tether_Check(HSD_GObj* gobj, Vec3* from, Vec3* to, float facing, float size)
{
    Vec3 hit;
    int line_id;
    int dir;
    int ledge_line;
    u32 checks = (facing > 0.0f ? 0x14 : 0x18) | 2;

    if (!mpCheckMultiple(from->x, from->y, to->x, to->y, &hit, &line_id, NULL, NULL, checks, -1,
                         -1))
    {
        return false;
    }
    if (ftLc_FindLedge(to, &dir, &ledge_line, facing, size, size)) {
        Vec3 ledge = { 0.0f, 0.0f, 0.0f };
        if (dir == 1) {
            mpFloorGetLeft(ledge_line, &ledge);
        } else if (dir == -1) {
            mpFloorGetRight(ledge_line, &ledge);
        }
        ftLc_AirCatchHit_Enter(gobj, ledge_line, &ledge);
        return true;
    }
    hit.x = (float) (hit.x - facing * 1.5);
    ftLc_AirCatchHit_Enter(gobj, line_id, &hit);
    return true;
}

/* code+0x5784 "Accessory_AirCatch_UpdateSnakePhysics": the throw. On the command script's go
 * (cmd_vars[1]) the rope is built and flung; afterwards it is simulated with the second mass
 * pinned to his hand, and once cmd_vars[0] is set the tip looks for something to catch. */
static void ftLc_AirCatch_Accessory(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    LucasRope* rope = ftLc_Vars(fp)->rope;
    Vec3 hand;

    if (fp->cmd_vars[2] != 0) {
        fp->cmd_vars[2] = 0;
    }

    if (rope->mass_count <= 0) {
        float x1;
        Vec3 point;
        LucasMass* last;
        if (fp->cmd_vars[1] == 0) {
            return;
        }
        fp->cmd_vars[1] = 0;
        lb_8000B1CC(fp->parts[ftLc_Part_Hip].joint, NULL, &hand);
        x1 = (float) attrs->x130_WALL_CHECK_DIST * fp->facing_dir * fp->x34_scale.y + hand.x;
        if (mpCheckAllRemap(NULL, NULL, NULL, NULL, -1, -1, hand.x, hand.y, x1, hand.y)) {
            /* A wall right in front of him: no throw. */
            ftCo_80096900(gobj, 1, 0, true, 1.0f, (float) attrs->x138_MISS_LANDING_LAG);
            return;
        }
        ftLc_Rope_Launch(gobj);
        if (!fp->allow_sdi) { /* console fp+0x2218 bit 0x2000: frozen in hitlag */
            ftLc_Rope_Update(rope);
        }
        ftLc_Rope_PoseSnake(gobj);
        last = &rope->masses[rope->mass_count - 1];
        point.x = fp->cur_pos.x;
        point.y = last->pos.y;
        point.z = fp->cur_pos.z;
        ftLc_Tether_Check(gobj, &point, &last->pos, fp->facing_dir,
                          attrs->x128_LEDGE_SEARCH_SIZE);
        return;
    }

    lb_8000B1CC(fp->parts[ftLc_Part_Hand].joint, NULL, &hand);
    rope->masses[1].pinned = 1;
    rope->masses[1].pos = hand;
    rope->masses[1].pin_pos = hand;
    if (!fp->allow_sdi) {
        ftLc_Rope_Update(rope);
    }
    ftLc_Rope_PoseSnake(gobj);
    if (fp->cmd_vars[0] == 0 || fp->allow_sdi) {
        return;
    }
    {
        LucasMass* last = &rope->masses[rope->mass_count - 1];
        Vec3 tip = last->pos;
        tip.x += fp->facing_dir * attrs->x12C_TIP_OFFSET_X;
        if (ftLc_Tether_Check(gobj, &last->prev_pos, &tip, fp->facing_dir,
                              attrs->x128_LEDGE_SEARCH_SIZE))
        {
            ftAnim_8006E7B8(fp, 0);
            ftLc_Rope_PoseSnake(gobj);
        }
    }
}

/* m-ex entertether (with the tether enabled), code+0x3B78 "AirCatch_Enter" */
void ftLc_AirCatch_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    LucasRope* rope = ftLc_Vars(fp)->rope;
    ftLucas_MotionVars* mv = ftLc_MV(fp);

    /* Out of an air dodge the miss comes with landing lag. */
    mv->aircatch.landing_lag =
        fp->motion_id == ftCo_MS_EscapeAir ? (s32) attrs->x134_AIRCATCH_LANDING_LAG : 0;
    Fighter_ChangeMotionState(gobj, ftLc_MS_AirCatch, 0, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    ftLc_CommonSpawnSnake(gobj, 3);
    fp->cmd_vars[0] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[3] = 0;
    fp->x1A6A = 0x1FF;
    mv->aircatch.rope_scale = 1.0f;
    rope->mass_count = 0;
    rope->spring_count = 0;
    rope->stiffness = 1.0f;
    rope->iterations = attrs->x108_ROPE_ITERATIONS;
    rope->mass_cb = ftLc_Rope_MassCollide;
    fp->accessory4_cb = ftLc_AirCatch_Accessory;
}

/* code+0x3480 */
void ftLc_AirCatch_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);

    if (fp->cmd_vars[3] != 0) {
        /* Reel the missed rope back in, then hide the snake. */
        if (!(vars->rope->springs[0].rest_len <= 0.0f)) {
            ftLc_Rope_Retract(vars->rope, 0.0f, 0.5f);
        } else {
            GET_ITEM(vars->held_item_gobj)->xDAA_flag.b7 = false;
        }
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCo_80096900(gobj, 1, 0, true, 1.0f, (float) attrs->x138_MISS_LANDING_LAG);
    }
}

/* code+0x3564 */
void ftLc_AirCatch_IASA(HSD_GObj* gobj) {}

/* code+0x3568 */
void ftLc_AirCatch_Phys(HSD_GObj* gobj)
{
    ft_80084DB0(gobj);
}

/* code+0x3588 */
void ftLc_AirCatch_Coll(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ft_80081D0C(gobj)) {
        s32 lag = ftLc_MV(fp)->aircatch.landing_lag;
        if (lag > 0) {
            ftCo_LandingFallSpecial_Enter(gobj, false, (float) lag);
        } else {
            ft_80082B1C(gobj);
        }
        ftLc_Snake_DestroyCB(gobj);
    }
}

/* code+0x3618: ride moving platforms; drop off if the ledge line goes away. */
void ftLc_AirCatchHit_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    LucasRope* rope = ftLc_Vars(fp)->rope;

    if (mpLib_80054ED8(mv->aircatch.line_id)) {
        Vec3* anchor = &rope->masses[rope->mass_count - 1].pin_pos;
        Vec3 speed;
        mpGetSpeed(mv->aircatch.line_id, anchor, &speed);
        PSVECAdd(anchor, &speed, anchor);
    } else {
        ftCo_80096900(gobj, 1, 0, true, 1.0f, (float) attrs->x138_MISS_LANDING_LAG);
        ftLc_Snake_DestroyCB(gobj);
    }
    ftLc_Tether_PlaceFighter(gobj);
    if (mv->aircatch.timer > 0) {
        mv->aircatch.timer--;
    }
}

/* code+0x3714: hang until A or the hang timer, then reel in; close enough (or out of time)
 * climbs onto the ledge or jumps up. */
void ftLc_AirCatchHit_IASA(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucasAttributes* attrs = ftLc_Attrs(fp);
    ftLucas_MotionVars* mv = ftLc_MV(fp);
    LucasRope* rope = ftLc_Vars(fp)->rope;

    if (mv->aircatch.reeling == 0) {
        if ((fp->input.pressed_buttons & HSD_PAD_A) || mv->aircatch.timer == 0) {
            mv->aircatch.reeling = 1;
            mv->aircatch.timer = attrs->x120_REEL_FRAMES;
        }
        return;
    }
    if (mv->aircatch.reeling != 1) {
        return;
    }
    {
        int i;
        float dist;
        for (i = 0; i < rope->spring_count; i++) {
            if (rope->springs[i].rest_len > attrs->x114_ROPE_REEL_MIN) {
                rope->springs[i].rest_len -= attrs->x110_ROPE_REEL_STEP;
            }
        }
        {
            LucasMass* first = &rope->masses[0];
            LucasMass* last = &rope->masses[rope->mass_count - 1];
            float dx = first->pos.x - last->pos.x;
            float dy = first->pos.y - last->pos.y;
            float dz = first->pos.z - last->pos.z;
            dist = sqrtf((float) ((double) dx * dx + (double) dy * dy + (double) dz * dz));
        }
        if (!(attrs->x118_ROPE_CLIMB_DIST > dist) && mv->aircatch.timer != 0) {
            return;
        }
    }
    if (ftCo_800C3A14(gobj) && ft_80082E3C(gobj) == NULL) {
        ftCliffCommon_80081370(gobj);
        ftLc_Snake_DestroyCB(gobj);
        return;
    }
    fp->self_vel.x = 0.0f;
    ftCo_8009B390(gobj, attrs->x124_CLIMB_FORCE);
    ftLc_Snake_DestroyCB(gobj);
}

/* code+0x38BC */
void ftLc_AirCatchHit_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->self_vel.x = 0.0f;
    fp->self_vel.y = 0.0f;
    fp->self_vel.z = 0.0f;
}

/* code+0x38D8 */
void ftLc_AirCatchHit_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftCo_LandingFallSpecial_Enter(gobj, false, 10.0f);
        ftLc_Snake_DestroyCB(gobj);
    }
}

/* ---------------------------------------------------------------------------------------------
 * The rope drawn as lines (code+0x4090 "Physics_Render"), shown with the fighter's
 * x21FC_flag.b6 debug display bit.
 * ------------------------------------------------------------------------------------------- */
void ftLc_Rope_Render(HSD_GObj* gobj)
{
    LucasRope* rope = ftLc_Vars(GET_FIGHTER(gobj))->rope;
    int i;

    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
    GXSetZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
    GXSetZCompLoc(GX_FALSE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE,
                  GX_AF_NONE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXLoadPosMtxImm(HSD_CObjGetCurrent()->view_mtx, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXBegin(GX_LINES, GX_VTXFMT0, (u16) (rope->spring_count * 2));
    for (i = 0; i < rope->spring_count; i++) {
        LucasSpring* s = &rope->springs[i];
        GXPosition3f32(s->a->pos.x, s->a->pos.y, s->a->pos.z);
        GXColor4u8(0x50, 0x50, 0x50, 0xFF);
        GXPosition3f32(s->b->pos.x, s->b->pos.y, s->b->pos.z);
        GXColor4u8(0xFF, 0xFF, 0xFF, 0xFF);
    }
    GXEnd();
    HSD_StateInvalidate(-1);
    HSD_StateInitTev();
    HSD_ClearVtxDesc();
}

/* ---------------------------------------------------------------------------------------------
 * Grab: the snake bumping a wall in front of him bounces him off it.
 * ------------------------------------------------------------------------------------------- */

/* ftCo_8009EE30 (static in ftCo_StopWall.c), which the disc calls by address: enter StopWall
 * against the wall he is touching. */
static void ftLc_EnterStopWall(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    CollData* coll = &fp->coll_data;
    Vec3 vec;
    if (coll->env_flags & Collide_RightWallMask) {
        vec.x = coll->ecb.left.x;
        vec.y = coll->ecb.left.y;
    } else {
        vec.x = coll->ecb.right.x;
        vec.y = coll->ecb.right.y;
    }
    vec.z = 0.0f;
    ftKb_SpecialN_800F1F1C(gobj, &vec);
    Fighter_ChangeMotionState(gobj, ftCo_MS_StopWall, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    fp->cur_pos.x = -(fp->x68C_transNPos.z * -fp->facing_dir - (fp->cur_pos.x + vec.x));
    ft_800843FC(gobj);
    ftCommon_8007E2FC(gobj);
}

/* code+0x5BA4 "Catch_CheckEnterRebound" */
static void ftLc_Catch_CheckRebound(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 p;
    int line_id;
    lb_8000B1CC(fp->parts[ftLc_Part_Hip].joint, NULL, &p);
    if (mpCheckAllRemap(NULL, &line_id, NULL, NULL, -1, -1, p.x, p.y,
                        fp->facing_dir * 8.0f + p.x, p.y))
    {
        if (mpGetGroundCollLine()[line_id].flags & 0xC) {
            ftLc_EnterStopWall(gobj);
        }
    }
}

/* code+0x3FF8 "Lucas_CatchAccessory4": set by OnCatch. */
void ftLc_Catch_Accessory(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->motion_id == ftCo_MS_Catch && fp->cur_anim_frame == 11.0f) {
        ftLc_Catch_CheckRebound(gobj);
    }
    if (fp->motion_id == ftCo_MS_CatchDash && fp->cur_anim_frame == 14.0f) {
        ftLc_Catch_CheckRebound(gobj);
    }
}
