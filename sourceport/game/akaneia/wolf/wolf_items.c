/* Akaneia's Wolf: his articles (m-ex "itFunction" of PlWf.dat).
 *
 * m-ex builds each article's logic table from two sources: the defaults MxDt.dat holds for the
 * item kind (MxDt item section, entry kind - 237), and the fighter file's itFunction exports, which
 * replace the slots they name. Both are merged here into one ItemLogicTable per article, in the
 * order of Wolf's MxDt item lookup (item kinds 253 and 254 on Akaneia):
 *
 *   article 0, the laser: states, spawned, dmg_dealt, dmg_received, reflected, clanked and
 *     hit_shield are Wolf's; absorbed, shield_bounced and evt_unk are MxDt's defaults, Fox's laser
 *     functions; the rest are empty.
 *   article 1, the blaster in Wolf's hand: states and picked_up are Wolf's; MxDt has no defaults.
 *
 * Wolf's lookup lists two more kinds (255, a copy of Fox's laser defaults, and 256, empty) that no
 * Wolf code creates; they are not served (see NOTES.md). */
#include "wolf.h"

#include <dolphin/os.h>

#include <melee/it/forward.h>

#include <math.h>

#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/kinds/itfoxlaser.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

#define PI 3.141592653589793
#define TWO_PI 6.283185307179586

/* Frames a laser lives, and how long one that hit a wall lingers. */
#define ITWF_LASER_LIFETIME 25.0f
#define ITWF_LASER_WALL_LIFETIME 1.0f

/* ---- article 0: the laser ---- */

/* [Shoot_Anim]: fly along the angle, turn the model to match, and expire with the lifetime. */
static bool itWf_Laser_Shoot_Anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    itWolfLaser_ItemVars* lv = itWfLaser_Vars(ip);
    float angle = lv->angle;
    float speed = lv->speed;
    float back_x;

    ip->x40_vel.x = cosf(angle) * speed;
    ip->x40_vel.y = sinf(angle) * speed;
    ip->x40_vel.z = 0.0f;

    if (ip->x40_vel.x > 0.0f) {
        ip->facing_dir = 1.0f;
        jobj->rotate.y = 1.5707964f;
        back_x = -ip->x40_vel.x;
    } else {
        ip->facing_dir = -1.0f;
        jobj->rotate.y = -1.5707964f;
        back_x = ip->x40_vel.x;
    }
    /* Pitch: the angle of the velocity measured from straight back, turned half a circle. */
    jobj->rotate.x = (float) (atan2f(ip->x40_vel.y, back_x) + PI);
    HSD_JObjSetMtxDirtySub(jobj);

    {
        /* Bring-up evidence (the first frames only): the shot's flight. On the console
         * recording it moves 2.3 a frame at y 5.36 and its timer counts down from 24. */
        static int logged;
        if (logged < 30) {
            logged++;
            OSReport("[ak] Wolf laser frame: pos (%.2f, %.2f, %.2f) vel (%.2f, %.2f) timer %.0f "
                     "hitbox0 state %d dmg %.0f at (%.2f, %.2f, %.2f)\n",
                     ip->pos.x, ip->pos.y, ip->pos.z, ip->x40_vel.x, ip->x40_vel.y, ip->xD44_lifeTimer,
                     (int) ip->x5D4_hitboxes[0].hit.state, ip->x5D4_hitboxes[0].hit.damage,
                     ip->x5D4_hitboxes[0].hit.x4C.x, ip->x5D4_hitboxes[0].hit.x4C.y,
                     ip->x5D4_hitboxes[0].hit.x4C.z);
        }
    }
    return it_80273130(gobj);
}

/* [Shoot_Phys]: remember where the laser was, for the wall test. */
static void itWf_Laser_Shoot_Phys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    itWfLaser_Vars(ip)->prev_pos = ip->pos;
}

/* [Shoot_Coll]: a line test from last frame's position to this one; a hit parks the laser on the
 * wall and lets it expire next frame. */
static bool itWf_Laser_Shoot_Coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itWolfLaser_ItemVars* lv = itWfLaser_Vars(ip);
    Vec3 hit;

    if (mpCheckAllRemap(&hit, NULL, NULL, NULL, -1, -1, lv->prev_pos.x, lv->prev_pos.y, ip->pos.x,
                        ip->pos.y))
    {
        it_80275158(gobj, ITWF_LASER_WALL_LIFETIME);
        ip->pos = hit;
    }
    return false;
}

/* [OnSpawn] */
static void itWf_Laser_OnSpawn(Item_GObj* gobj)
{
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    it_80275158(gobj, ITWF_LASER_LIFETIME);
}

/* [OnGiveDamage], [OnTakeDamage], [onClank], [onHitShieldDetermineDestroy]: the laser is used up. */
static bool itWf_Laser_Destroyed(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    OSReport("[ak] Wolf laser used up at (%.2f, %.2f), timer %.0f\n", ip->pos.x, ip->pos.y,
             ip->xD44_lifeTimer);
    return true;
}

/* [OnReflect]: turn around and keep flying. */
static bool itWf_Laser_OnReflect(Item_GObj* gobj)
{
    itWolfLaser_ItemVars* lv = itWfLaser_Vars(GET_ITEM(gobj));
    float angle = (float) (lv->angle + PI);

    while (angle > TWO_PI) {
        angle = (float) (angle - TWO_PI);
    }
    lv->angle = angle;
    return false;
}

static ItemStateTable itWf_Laser_States[] = {
    { 0, itWf_Laser_Shoot_Anim, itWf_Laser_Shoot_Phys, itWf_Laser_Shoot_Coll },
};

/* ---- article 1: the blaster ---- */

/* [Gun_Anim], [Gun_Coll]: nothing; the blaster only follows Wolf's hand. */
static bool itWf_Gun_Nothing(Item_GObj* gobj)
{
    return false;
}

/* [Gun_Phys] */
static void itWf_Gun_Phys(Item_GObj* gobj) {}

/* [OnPickup]: Item_8026AB54 calls it when Wolf takes the gun in hand. */
static void itWf_Gun_OnPickup(Item_GObj* gobj)
{
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
}

static ItemStateTable itWf_Gun_States[] = {
    { 0, itWf_Gun_Nothing, itWf_Gun_Phys, itWf_Gun_Nothing },
};

const ItemLogicTable itWf_Articles[2] = {
    {
        /* article 0 (item kind 253 on Akaneia): the laser */
        .states = itWf_Laser_States,
        .spawned = itWf_Laser_OnSpawn,
        .destroyed = NULL,
        .picked_up = NULL,
        .dropped = NULL,
        .thrown = NULL,
        .dmg_dealt = itWf_Laser_Destroyed,
        .dmg_received = itWf_Laser_Destroyed,
        .entered_air = NULL,
        .reflected = itWf_Laser_OnReflect,
        .clanked = itWf_Laser_Destroyed,
        .absorbed = itFoxLaser_Logic94_Absorbed,
        .shield_bounced = itFoxLaser_Logic94_ShieldBounced,
        .hit_shield = itWf_Laser_Destroyed,
        .evt_unk = itFoxLaser_Logic94_EvtUnk,
    },
    {
        /* article 1 (item kind 254 on Akaneia): the blaster */
        .states = itWf_Gun_States,
        .picked_up = itWf_Gun_OnPickup,
    },
};
