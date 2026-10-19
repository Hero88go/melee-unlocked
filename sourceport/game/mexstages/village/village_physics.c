/* Balloon rope, GrAc +2BA4..2E54 and +40FC..4304, +4B7C..4D60, +52E4.
 * The block has no fused multiply-adds. */
#include "village.h"
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbspdisplay.h>
#include <sysdolphin/baselib/debug.h>
#include <math.h>

MU_NO_CONTRACT static f32 ac_distance(Vec3* a, Vec3* b)
{
    float x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;
    float xx = x * x, yy = y * y, zz = z * z;
    return sqrtf((xx + yy) + zz);
}

/* +2BA4: console capacities are five masses and four springs. The counters are checked before
 * each insertion. The only caller requests joints one through four. */
void ac_rope_create(AcPhysics* p, HSD_JObj* root, int first, int last)
{
    AcMass* previous = NULL;
    int index;
    if (root == NULL) { return; }
    for (index = first; index <= last; index++) {
        HSD_JObj* joint = NULL;
        Vec3 pos;
        AcMass* mass;
        if (p->mass_count > 5) {
            __assert("village.c", 0x274, "too many physics masses");
        }
        lb_80011E24(root, &joint, index, -1);
        if (joint == NULL) { return; }
        lb_8000B1CC(joint, NULL, &pos);
        mass = &p->mass[p->mass_count++];
        mass->gravity = 0.01f;
        mass->damping = 0.99f;
        mass->pos = mass->previous = mass->anchor = pos;
        mass->fixed = 0;
        mass->joint = joint;
        if (previous != NULL) {
            AcSpring* spring;
            if (p->spring_count > 4) {
                __assert("village.c", 0x28D, "too many physics springs");
            }
            spring = &p->spring[p->spring_count++];
            spring->a = previous;
            spring->b = mass;
            spring->length = ac_distance(&previous->pos, &mass->pos);
        }
        previous = mass;
    }
}

/* +41D4: only the x component uses damping. The y and z velocity are undamped. */
MU_NO_CONTRACT static void ac_mass_update(AcMass* m)
{
    float x = m->pos.x, y = m->pos.y, z = m->pos.z;
    float vx = (x - m->previous.x) * m->damping;
    float vy = y - m->previous.y, vz = z - m->previous.z;
    m->previous = m->pos;
    m->pos.x = vx + x;
    m->pos.y = vy + y;
    m->pos.z = vz + z;
    if (!m->fixed) { m->pos.y -= m->gravity; }
}

MU_NO_CONTRACT static void ac_spring_update(AcSpring* s, float stiffness)
{
    float distance = ac_distance(&s->b->pos, &s->a->pos);
    float scaled, factor, x, y, z;
    if (s->length == 0.0f) { return; }
    scaled = distance * stiffness;
    if (scaled == 0.0f) { return; }
    if (s->limit != 0.0f && s->limit * stiffness >= scaled) { return; }
    factor = ((s->length * stiffness - scaled) / scaled) * 0.5f;
    x = (s->b->pos.x - s->a->pos.x) * factor;
    y = (s->b->pos.y - s->a->pos.y) * factor;
    z = (s->b->pos.z - s->a->pos.z) * factor;
    s->a->pos.x -= x;
    s->a->pos.y -= y;
    s->a->pos.z -= z;
    s->b->pos.x = x + s->b->pos.x;
    s->b->pos.y = y + s->b->pos.y;
    s->b->pos.z = z + s->b->pos.z;
}

/* +2DD0, +424C: integrate every mass, then solve constraints and restore pinned anchors for each
 * iteration. The joint matrices are updated separately by Balloon_OnFrame. */
void ac_physics_update(AcPhysics* p)
{
    int i, j;
    for (i = 0; i < p->mass_count; i++) { ac_mass_update(&p->mass[i]); }
    for (j = 0; j < p->iterations; j++) {
        for (i = 0; i < p->spring_count; i++) {
            ac_spring_update(&p->spring[i], p->stiffness);
        }
        for (i = 0; i < p->mass_count; i++) {
            if (p->mass[i].fixed) { p->mass[i].pos = p->mass[i].anchor; }
        }
    }
}
