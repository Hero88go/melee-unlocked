/* Charizard's items (PlLz.dat itFunction, three modules in ftData->x48_items order).
 *
 *   0 Fire: the Flamethrower flame. A copy of Bowser's flame item (itkoopaflame.c): same item
 *     variables, same special attributes, the collision callback is Bowser's own function. It
 *     differs in its effect ids (0x1775 + variant), in drawing the flame angle before the speed,
 *     and in its logic table (no reflect/clank/absorb/shield handlers).
 *   1 Rock: Rock Smash's held rock. One child of its model is shown per floor material; it
 *     destroys itself when its owner leaves the move.
 *   2 RockBurst: the pieces the rock breaks into, thrown at a random angle and spun randomly.
 *
 * The item state tables and logic tables below are what m-ex installs for these item kinds; the
 * integration layer registers them for the kinds mu_ak_article_kind hands out. */
#include "ftlizardon.h"

#include <math.h>

#include <melee/db/db.h>
#include <melee/ef/efsync.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/kinds/inlines.h>
#include <melee/it/kinds/itkoopaflame.h>
#include <melee/it/types.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>

/* ---- special attributes (article x4, disc data) ---- */

typedef struct itLzFire_Attrs {
    /* +00 */ float lifetime;        ///< 28
    /* +04 */ float hitbox_lifetime; ///< 20
    /* +08 */ float min_speed;       ///< 1.9
    /* +0C */ float max_speed;       ///< 2.2
    /* +10 */ float min_angle;       ///< 2.18 rad
    /* +14 */ float max_angle;       ///< 2.53 rad
} DISC_STRUCT itLzFire_Attrs;

typedef struct itLzRock_Attrs {
    /* +00 */ int variant_max;              ///< highest floor material with its own look (19)
    /* +04 */ DISC_PTR(be_s32) variant_child; ///< model child shown per floor material
} DISC_STRUCT itLzRock_Attrs;

typedef struct itLzRockBurst_Attrs {
    /* +00 */ int variant_max;
    /* +04 */ DISC_PTR(be_s32) variant_child;
    /* +08 */ int lifetime;   ///< 6
    /* +0C */ float speed;    ///< 4.0
    /* +10 */ float min_angle; ///< degrees (30)
    /* +14 */ float max_angle; ///< degrees (110)
} DISC_STRUCT itLzRockBurst_Attrs;

/* Rock item variables (ip->xDD4_itemVar). */
typedef struct itLzRock_ItemVars {
    /* ip+DD4 */ HSD_GObj* owner;
} itLzRock_ItemVars;

#define ITLZ_FIRE_GFX_BASE 0x1775      ///< flame effect, plus the variant 0..3
#define ITLZ_ROCKBURST_DESTROY_GFX 0x177B
#define ITLZ_ROCK_LIFETIME 1200.0F
/* The console's degree-to-radian constant (a double 0.0174533, not M_PI / 180). */
#define ITLZ_DEG_TO_RAD 0.0174533

static inline void* itLz_SpecialAttrs(Item* ip)
{
    return DP(ip->xC4_article_data->x4_specialAttributes);
}

static inline itLzRock_ItemVars* itLzRock_Vars(Item* ip)
{
    return (itLzRock_ItemVars*) &ip->xDD4_itemVar;
}

/* Which model child a floor material shows: the table entry, or entry 0 past the table. */
static int itLz_VariantChild(int variant_max, be_s32* table, int variant)
{
    int idx = variant > variant_max ? 0 : variant;
    return BEV(table[idx]);
}

/* The spawn both rock items share: at @p pos, not moving, from @p owner. */
static HSD_GObj* itLz_SpawnRockItem(HSD_GObj* owner, Vec3* pos, int kind, float facing_dir)
{
    SpawnItem spawn = { 0 };

    spawn.x0_parent_gobj = owner;
    spawn.x4_parent_gobj2 = owner;
    spawn.kind = kind;
    spawn.pos.x = pos->x;
    spawn.pos.y = pos->y;
    spawn.pos.z = 0.0F;
    spawn.prev_pos.x = pos->x;
    spawn.prev_pos.y = pos->y;
    spawn.prev_pos.z = 0.0F;
    spawn.facing_dir = facing_dir;
    spawn.x3C_damage = 0;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0F;
    spawn.x44_flag.b0 = 0;
    spawn.x40 = 0;
    return Item_80268B18(&spawn);
}

/* ========================================================================================= */
/* 0 Fire                                                                                     */
/* ========================================================================================= */

/* PlLz ClampRotation is Item_ClampAngle. PlLz Phys and the item's phys callback (identical). */
static void itLzFire_Phys(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itKoopaFlame_ItemVars* kf = &ip->xDD4_itemVar.koopaflame;

    kf->x0_pos = ip->pos;
    ip->x40_vel.x = kf->x28_speed * sinf(kf->x24_angle);
    ip->x40_vel.y = kf->x28_speed * cosf(kf->x24_angle);
    ip->x40_vel.z = 0.0F;
    kf->x18_vel = ip->x40_vel;
    lbVector_Normalize(&kf->x18_vel);
}

/* PlLz Flame_Init: Bowser's itKoopaFlame_Setup, minus its it_8026B3A8 call, and clearing a
 * different spawn flag: console bit 0x8000 of the word at ip+DC8, which the decomp's flag32 keeps
 * inside the 4-bit field xF (Bowser clears x13, bit 0x1000). */
static void itLzFire_Setup(HSD_GObj* gobj, HSD_GObj* owner)
{
    Item* ip = GET_ITEM(gobj);
    Vec3 pos;

    ip->xDC8_word.flags.xF &= ~4;
    it_80272940(gobj);
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    pos = ip->pos;
    Item_802694CC(gobj);
    itLzFire_Phys(gobj);
    ip->pos.x -= ip->x40_vel.x;
    ip->pos.y -= ip->x40_vel.y;
    it_8026D9A0(gobj);
    ip->pos = pos;
    db_80225DD8(gobj, owner);
}

/* PlLz SpawnItem_Fire. @p speed_scale and @p size_scale are the fighter's reserves over their
 * maxima (Bowser passes the integers and divides here). */
HSD_GObj* itLzFire_Spawn(HSD_GObj* owner, Vec3* pos, u32 hit_id, int gfx, int kind,
                         float facing_dir, float speed_scale, float size_scale)
{
    SpawnItem spawn = { 0 };
    HSD_GObj* gobj;

    spawn.x0_parent_gobj = owner;
    spawn.x4_parent_gobj2 = owner;
    spawn.kind = kind;
    ftLib_80086990(owner, &spawn.pos);
    spawn.prev_pos.x = pos->x;
    spawn.prev_pos.y = pos->y;
    spawn.prev_pos.z = 0.0F;
    spawn.facing_dir = facing_dir;
    spawn.x3C_damage = 0;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0F;
    spawn.x40 = hit_id;
    spawn.x44_flag.b0 = 1;
    gobj = Item_80268B18(&spawn);
    if (gobj != NULL) {
        Item* ip = GET_ITEM(gobj);
        itLzFire_Attrs* attrs = (itLzFire_Attrs*) itLz_SpecialAttrs(ip);
        itKoopaFlame_ItemVars* kf = &ip->xDD4_itemVar.koopaflame;
        float range;

        Item_ClearCmdVars(ip);
        it_80275158(gobj, attrs->lifetime);
        kf->x0_pos = *pos;
        kf->xC_direction.x = ip->facing_dir;
        kf->xC_direction.y = 0.0F;
        kf->xC_direction.z = 0.0F;
        kf->x30 = 0;
        kf->x34_base_scale = 0.0F;
        kf->x38_base_speed = speed_scale;
        kf->x3C_scale = size_scale;

        /* angle first, then speed (Bowser draws them the other way round) */
        range = attrs->max_angle - attrs->min_angle;
        kf->x24_angle = range * HSD_Randf() + attrs->min_angle;
        range = attrs->max_speed - attrs->min_speed;
        kf->x28_speed = (range * HSD_Randf() + attrs->min_speed) * kf->x38_base_speed;
        kf->x2C_lifetime = attrs->lifetime;
        if (ip->facing_dir != 1.0F) {
            kf->x24_angle = -kf->x24_angle;
        }
        Item_ClampAngle(&kf->x24_angle);

        kf->x40_frame_counter = 0;
        kf->x44_spawned = false;
        kf->x48_gfx = gfx;
        ip->xDCC_flag.b3 = 0;
        itLzFire_Setup(gobj, owner);
    }
    return gobj;
}

/* The flame's state animation: scale model and hitbox with the size reserve, drop the hitbox after
 * hitbox_lifetime frames, spawn the flame effect once. */
static bool itLzFire_Anim(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itLzFire_Attrs* attrs = (itLzFire_Attrs*) itLz_SpecialAttrs(ip);
    itKoopaFlame_ItemVars* kf = &ip->xDD4_itemVar.koopaflame;
    HSD_JObj* jobj = GET_JOBJ(gobj);
    float scale = kf->x3C_scale;

    if (ip->x5D4_hitboxes[0].hit.state != HitCapsule_Disabled) {
        if (kf->x34_base_scale == 0.0F) {
            kf->x34_base_scale = ip->x5D4_hitboxes[0].hit.scale;
        }
        ip->x5D4_hitboxes[0].hit.scale = scale * kf->x34_base_scale;
    }
    jobj->scale.x = scale;
    jobj->scale.y = scale;
    jobj->scale.z = scale;
    HSD_JObjSetMtxDirtySub(jobj);

    kf->x40_frame_counter += 1;
    if (kf->x40_frame_counter > attrs->hitbox_lifetime) {
        it_802725D4(gobj);
    }
    if (!kf->x44_spawned) {
        kf->x44_spawned = true;
        efSync_Spawn(ITLZ_FIRE_GFX_BASE + kf->x48_gfx, gobj, jobj);
    }
    return it_80273130(gobj);
}

static bool itLzFire_DmgDealt(HSD_GObj* gobj)
{
    return false;
}

static bool itLzFire_HitShield(HSD_GObj* gobj)
{
    return false;
}

static ItemStateTable itLzFire_States[] = {
    { 0, itLzFire_Anim, itLzFire_Phys, itKoopaFlame_UnkMotion0_Coll },
};

/* ========================================================================================= */
/* 1 Rock                                                                                     */
/* ========================================================================================= */

/* PlLz SpawnItem_Rock: the rock appears held at @p part and shows the look of @p variant. */
HSD_GObj* itLzRock_Spawn(HSD_GObj* owner, Vec3* pos, int part, int kind, float facing_dir,
                         int variant)
{
    Fighter* fp = GET_FIGHTER(owner);
    HSD_GObj* gobj = itLz_SpawnRockItem(owner, pos, kind, facing_dir);

    if (gobj != NULL) {
        Item* ip = GET_ITEM(gobj);
        itLzRock_Attrs* attrs = (itLzRock_Attrs*) itLz_SpecialAttrs(ip);
        HSD_JObj* jobj = GET_JOBJ(gobj);
        HSD_JObj* child;
        int shown;
        int i;

        ftLz_Vars(fp)->rock = gobj;
        shown = itLz_VariantChild(attrs->variant_max, DP(attrs->variant_child), variant);
        Item_ClearCmdVars(ip);
        ip->xDCC_flag.b0 = 0;
        itLzRock_Vars(ip)->owner = owner;
        ftLz_Vars(fp)->rock_variant = variant;
        ip->scl = fp->x34_scale.y;
        it_80275158(gobj, ITLZ_ROCK_LIFETIME);
        Item_8026AB54(gobj, owner, part);

        for (child = jobj->child->child, i = 0; child != NULL; child = child->next, i++) {
            if (i == shown) {
                HSD_JObjClearFlags(child, JOBJ_HIDDEN);
            } else {
                HSD_JObjSetFlags(child, JOBJ_HIDDEN);
            }
        }
    }
    return gobj;
}

/* The rock lives while its owner is in Rock Smash (states 351, 352). */
static bool itLzRock_Anim(HSD_GObj* gobj)
{
    HSD_GObj* owner = itLzRock_Vars(GET_ITEM(gobj))->owner;
    if (owner == NULL) {
        return true;
    }
    return (u32) (GET_FIGHTER(owner)->motion_id - ftLz_MS_SpecialLw) > 1;
}

static void itLzRock_Phys(HSD_GObj* gobj) {}

static bool itLzRock_Coll(HSD_GObj* gobj)
{
    return false;
}

static void itLzRock_Destroyed(HSD_GObj* gobj)
{
    HSD_GObj* owner = itLzRock_Vars(GET_ITEM(gobj))->owner;
    if (owner != NULL) {
        ftLz_Vars(GET_FIGHTER(owner))->rock = NULL;
    }
}

static void itLzRock_PickedUp(HSD_GObj* gobj)
{
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
}

static ItemStateTable itLzRock_States[] = {
    { 0, itLzRock_Anim, itLzRock_Phys, itLzRock_Coll },
};

/* ========================================================================================= */
/* 2 RockBurst                                                                                */
/* ========================================================================================= */

/* PlLz SpawnItem_RockBurst. @p part is passed but not used, as on the console. */
HSD_GObj* itLzRockBurst_Spawn(HSD_GObj* owner, Vec3* pos, int part, int kind, float facing_dir)
{
    Fighter* fp = GET_FIGHTER(owner);
    HSD_GObj* gobj = itLz_SpawnRockItem(owner, pos, kind, facing_dir);

    if (gobj != NULL) {
        Item* ip = GET_ITEM(gobj);
        itLzRockBurst_Attrs* attrs = (itLzRockBurst_Attrs*) itLz_SpecialAttrs(ip);
        HSD_JObj* jobj;
        HSD_JObj* child;
        float min_angle;
        float angle;
        float speed;
        int shown;
        int i;

        Item_ClearCmdVars(ip);
        it_80275158(gobj, (float) attrs->lifetime);

        jobj = GET_JOBJ(gobj);
        jobj->rotate.x = HSD_Randf() * 359.0F;
        jobj->rotate.y = HSD_Randf() * 359.0F;
        jobj->rotate.z = HSD_Randf() * 359.0F;
        HSD_JObjSetMtxDirtySub(jobj);

        min_angle = attrs->min_angle;
        angle = (attrs->max_angle - min_angle) * HSD_Randf() + min_angle;
        angle = (float) (angle * ITLZ_DEG_TO_RAD);
        speed = attrs->speed;
        ip->x40_vel.x = speed * sinf(angle) * ip->facing_dir;
        speed = attrs->speed;
        ip->x40_vel.y = speed * cosf(angle);
        ip->x40_vel.z = 0.0F;
        Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);

        /* The piece of the owner's floor material shows, one of its two halves hidden at random;
         * the other pieces are hidden. */
        shown = itLz_VariantChild(attrs->variant_max, DP(attrs->variant_child),
                                  ftLz_Vars(fp)->rock_variant);
        for (child = jobj->child, i = 0; child != NULL; child = child->next, i++) {
            if (i == shown) {
                HSD_JObj* half;
                int pick = HSD_Randi(2);
                if (pick != 0) {
                    lb_80011E24(child, &half, 2, -1);
                    HSD_JObjSetFlags(half, JOBJ_HIDDEN);
                    if (pick == 1) {
                        continue;
                    }
                }
                lb_80011E24(child, &half, 3, -1);
                HSD_JObjSetFlags(half, JOBJ_HIDDEN);
            } else {
                HSD_JObjSetFlagsAll(child, JOBJ_HIDDEN);
            }
        }
    }
    return gobj;
}

static bool itLzRockBurst_Anim(HSD_GObj* gobj)
{
    return it_80273130(gobj);
}

static void itLzRockBurst_Phys(HSD_GObj* gobj) {}

static bool itLzRockBurst_Coll(HSD_GObj* gobj)
{
    return false;
}

static void itLzRockBurst_Destroyed(HSD_GObj* gobj)
{
    efSync_Spawn(ITLZ_ROCKBURST_DESTROY_GFX, gobj, GET_JOBJ(gobj));
}

static bool itLzRockBurst_HitShield(HSD_GObj* gobj)
{
    return true;
}

static ItemStateTable itLzRockBurst_States[] = {
    { 0, itLzRockBurst_Anim, itLzRockBurst_Phys, itLzRockBurst_Coll },
};

/* ========================================================================================= */
/* Logic tables (m-ex itFunction order == ItemLogicTable)                                     */
/* ========================================================================================= */

ItemLogicTable mu_ak_charizard_item_logic[ftLz_Item_Count] = {
    [ftLz_Item_Fire] = {
        .states = itLzFire_States,
        .dmg_dealt = itLzFire_DmgDealt,
        .hit_shield = itLzFire_HitShield,
    },
    [ftLz_Item_Rock] = {
        .states = itLzRock_States,
        .destroyed = itLzRock_Destroyed,
        .picked_up = itLzRock_PickedUp,
    },
    [ftLz_Item_RockBurst] = {
        .states = itLzRockBurst_States,
        .destroyed = itLzRockBurst_Destroyed,
        .hit_shield = itLzRockBurst_HitShield,
    },
};
