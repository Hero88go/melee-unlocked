/* PlLc2.dat article blocks 0, 1, 2, 3 and 8 and the two ftFunction spawners. */
#include "lucas2.h"
#include <math.h>

#include <melee/cm/camera.h>
#include <melee/db/db.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/ft/kinds/ftNess/ftnessspecialn.h>
#include <melee/gr/stage.h>
#include <melee/it/inlines.h>
#include <melee/it/item.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/it/itcoll.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <melee/it/kinds/inlines.h>
#include <melee/it/kinds/types.h>
#include <melee/it/kinds/itnesspkflash.h>
#include <melee/it/kinds/itnesspkflashexplode.h>
#include <melee/it/kinds/itnesspkthunderball.h>
#include <sysdolphin/baselib/jobj.h>

/* The mod uses integer charge and saved hitbox scale, unlike retail Ness. */
typedef struct ftLc2_FlashVars {
    s32 unused;
    s32 charge;
    s32 reflected;
    HSD_GObj* owner;
    s32 unused2;
} ftLc2_FlashVars;

typedef struct ftLc2_ExplodeVars {
    s32 charge;
    s32 saved_scale;
    HSD_GObj* owner;
} ftLc2_ExplodeVars;

static inline ftLc2_FlashVars* flash_vars(Item* ip)
{
    return (ftLc2_FlashVars*) &ip->xDD4_itemVar;
}

static inline ftLc2_ExplodeVars* explode_vars(Item* ip)
{
    return (ftLc2_ExplodeVars*) &ip->xDD4_itemVar;
}

static bool item_false(Item_GObj* gobj) { (void) gobj; return false; }
static bool item_true(Item_GObj* gobj) { (void) gobj; return true; }
static void item_noop(Item_GObj* gobj) { (void) gobj; }

/* Article 0 +E4/+1A0 and article 1 +68. */
static bool fire_anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ip->xD44_lifeTimer -= 1.0f;
    return ip->xD44_lifeTimer <= 0.0f;
}

/* Article 0 +48: only the first hit enters the lingering state. */
static bool fire_damage(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (flash_vars(ip)->unused == 0) {
        flash_vars(ip)->unused = 1;
        ip->xD44_lifeTimer = 2.0f;
        Item_80268E5C(gobj, 1, ITEM_ANIM_UPDATE);
    }
    return false;
}

/* +118 passes the Item itself as the output vector. Reproduce its three
 * console header-word writes using named native fields. */
static bool fire_coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Vec3 normal;
    int wall;
    it_8026D9A0(gobj);
    wall = it_80276348(gobj, &normal);
    if (wall != 0) {
        u32 x, y;
        memcpy(&x, &normal.x, sizeof x);
        memcpy(&y, &normal.y, sizeof y);
        ip->x0 = (void*) (uintptr_t) x;
        ip->entity = (HSD_GObj*) (uintptr_t) y;
        memcpy(&ip->x8, &normal.z, sizeof ip->x8);
        ip->xD44_lifeTimer = 2.0f;
        Item_80268E5C(gobj, 1, ITEM_ANIM_UPDATE);
    }
    return false;
}

static bool fire_reflect(Item_GObj* gobj)
{
    it_80273030(gobj);
    return false;
}

static ItemStateTable fire_states[2] = {
    { 0, fire_anim, NULL, fire_coll },
    { 1, fire_anim, NULL, NULL },
};
static ItemStateTable unused_states[1] = {
    { 0, fire_anim, NULL, NULL },
};

/* ftFunction +4DC8. The shot ignores trajectory attributes and moves at 2.5. */
Item_GObj* ftLc2_Fire_Spawn(HSD_GObj* owner, Vec3* pos, ItemKind kind, float dir)
{
    SpawnItem spawn = { 0 };
    Item_GObj* gobj;
    Item* ip;
    spawn.x0_parent_gobj = spawn.x4_parent_gobj2 = owner;
    spawn.kind = kind;
    ftLib_80086990(owner, &spawn.pos);
    spawn.prev_pos = (Vec3) { pos->x, pos->y, 0.0f };
    spawn.vel.x = dir * 2.5f;
    spawn.facing_dir = dir;
    spawn.x44_flag.b0 = true;
    gobj = Item_80268B18(&spawn);
    if (gobj == NULL) return NULL;
    ip = GET_ITEM(gobj);
    ip->x40_vel = (Vec3) { ip->facing_dir * 2.5f, 0.0f, 0.0f };
    it_80275158(gobj, 20.0f);
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    memset(&ip->xDD4_itemVar, 0, sizeof(s32));
    it_8026B3A8(gobj);
    db_80225DD8(gobj, owner);
    it_802750F8(gobj);
    return gobj;
}

/* ftFunction +458C: retail PK Thunder initialization with article index 3. */
Item_GObj* ftLc2_Thunder_Spawn(HSD_GObj* owner, Vec3* pos, float dir)
{
    SpawnItem spawn = { 0 };
    Item_GObj* gobj;
    spawn.kind = mu_ak_article_kind(owner, 3);
    Item_InitSpawnPositionFromParent(&spawn, owner, pos);
    spawn.facing_dir = dir;
    spawn.x0_parent_gobj = spawn.x4_parent_gobj2 = owner;
    spawn.x44_flag.b0 = true;
    spawn.x40 = Item_8026AE60();
    gobj = Item_80268B18(&spawn);
    if (gobj != NULL) {
        Item* ip = GET_ITEM(gobj);
        itPKThunderAttributes* attr = DP(ip->xC4_article_data->x4_specialAttributes);
        Item_ClearCmdVars(ip);
        it_80275158(gobj, attr->x0_PKTHUNDER_LIFETIME);
        ip->xDD4_itemVar.pkthunder.xEF4 = NULL;
        for (int i = 0; i < 16; i++) {
            ip->xDD4_itemVar.pkthunder.positions[i] = (Vec3) { pos->x, pos->y, 0.0f };
            ip->xDD4_itemVar.pkthunder.angles[i] =
                attr->x8_PKTHUNDER_SPAWN_ANGLE * 0.01745329238474369f;
        }
        for (int i = 0; i < 6; i++) ip->xDD4_itemVar.pkthunder.xDD4[i] = NULL;
        ip->xDD4_itemVar.pkthunder.xEFC = 0;
        ip->xDD4_itemVar.pkthunder.xEF8 = 0;
        ip->xDD4_itemVar.pkthunder.xF00 = 0;
        ip->xDD4_itemVar.pkthunder.xF04 = owner;
        ip->xDD4_itemVar.pkthunder.xF08 = NULL;
        it_802ABA4C(gobj);
        ip->xDCC_flag.b3 = false;
        if (Stage_80225194() == 0x54) Item_80267454(gobj);
    }
    return gobj;
}

/* Article 2 +30: the owner-kind check uses Kirby's fighter kind, not item kind. */
static void flash_destroy(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc2_FlashVars* vars = flash_vars(ip);
    it_802725D4(gobj);
    if (vars->owner != NULL) {
        if (ip->owner == vars->owner) {
            Fighter* fp = GET_FIGHTER(vars->owner);
            if (fp->kind == Ft_Kind_Kirby) {
                *ftLc2_KirbyFlashSlot(fp) = NULL;
            } else {
                ftNs_SpecialN_SetNULL(vars->owner);
            }
        }
        vars->owner = NULL;
    }
    ip->xDC8_word.flags.x13 = 0;
}

/* The retail fixed call handles fighters; Kirby's custom hat uses the same
 * stored object slot and its own 400-based state numbers. */
static bool flash_held(HSD_GObj* owner)
{
    Fighter* fp = GET_FIGHTER(owner);
    if (fp->kind != Ft_Kind_Kirby) return ftNs_SpecialN_CheckSpecialNHold(owner);
    return *ftLc2_KirbyFlashSlot(fp) != NULL &&
        (fp->motion_id == 401 || fp->motion_id == 405);
}

static void flash_scale(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itFlashAttributes* a = DP(ip->xC4_article_data->x4_specialAttributes);
    float ratio = (a->xC_FLASH_GRAPHIC_SIZE_GROWTH_MUL -
                   a->x8_FLASH_GRAPHIC_SIZE_INIT_MUL) / a->x4_FLASH_HITBOX_SIZE_MUL;
    float s = __builtin_fmaf((float) flash_vars(ip)->charge, ratio, a->x8_FLASH_GRAPHIC_SIZE_INIT_MUL);
    HSD_JObj* joint = GET_JOBJ(gobj);
    joint->scale = (Vec3) { s, s, s };
    HSD_JObjSetMtxDirtySub(joint);
}

static void flash_delay(Item_GObj* gobj, int state)
{
    Item* ip = GET_ITEM(gobj);
    itFlashAttributes* a = DP(ip->xC4_article_data->x4_specialAttributes);
    Item_80268E5C(gobj, state, ITEM_ANIM_UPDATE);
    ip->xD44_lifeTimer = a->x28_FLASH_EXPLOSION_DELAY;
    ip->xDAC_itcmd_var0 = 0;
}

/* Article 2 +EC, +370 and +4E0. Charge arithmetic deliberately truncates. */
static bool flash_anim0(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc2_FlashVars* v = flash_vars(ip);
    itFlashAttributes* a = DP(ip->xC4_article_data->x4_specialAttributes);
    if (!it_80272C6C(gobj)) Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    if (v->reflected == 0) {
        v->charge = (s32) ((float) v->charge + 1.0f);
        if ((float) v->charge > a->x4_FLASH_HITBOX_SIZE_MUL)
            v->charge = (s32) a->x4_FLASH_HITBOX_SIZE_MUL;
        if (v->owner != NULL && ip->owner == v->owner && !flash_held(v->owner))
            flash_delay(gobj, 1);
    }
    if (it_80273130(gobj) == true) flash_delay(gobj, 1);
    flash_scale(gobj);
    return false;
}

static void flash_phys0(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc2_FlashVars* v = flash_vars(ip);
    itFlashAttributes* a = DP(ip->xC4_article_data->x4_specialAttributes);
    if (v->reflected == 0 && v->owner != NULL && ip->owner == v->owner && flash_held(v->owner)) {
        float x, y;
        ftLib_800865D8(v->owner, &x, &y);
        if (fabsf(x) > 0.2f) {
            ip->x40_vel.x = __builtin_fmaf(x, a->x18_FLASH_CONTROL, ip->x40_vel.x);
            if (fabsf(ip->x40_vel.x) > a->x20_FLASH_UNK2)
                ip->x40_vel.x = ip->x40_vel.x > 0 ? a->x20_FLASH_UNK2 : -a->x20_FLASH_UNK2;
        }
    }
    ip->x40_vel.y -= a->x1C_FLASH_GRAVITY;
    if (ip->x40_vel.y < -a->x24_FLASH_UNK3) ip->x40_vel.y = -a->x24_FLASH_UNK3;
    ip->x40_vel.z = 0.0f;
}

static bool flash_coll0(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (it_802AA810(gobj, &ip->x378_itemColl)) {
        flash_delay(gobj, 2);
        Item_8026B074(ip);
        Item_8026AE84(ip, 0x86, 127, 64);
    }
    flash_scale(gobj);
    return false;
}

static bool explode_anim(Item_GObj*);
static void explode_init(Item_GObj*);

/* Article 2 +99C. */
static Item_GObj* explode_spawn(HSD_GObj* owner, Item_GObj* flash, Vec3* pos,
                              ItemKind kind, float facing, float charge)
{
    SpawnItem spawn = { 0 };
    Item_GObj* gobj;
    spawn.kind = kind;
    spawn.pos = spawn.prev_pos = (Vec3) { pos->x, pos->y, 0.0f };
    spawn.facing_dir = facing;
    spawn.x0_parent_gobj = owner;
    spawn.x4_parent_gobj2 = flash;
    spawn.x44_flag.b0 = true;
    gobj = Item_80268B18(&spawn);
    if (gobj != NULL) {
        Item* ip = GET_ITEM(gobj);
        Item_ClearCmdVars(ip);
        explode_vars(ip)->charge = (s32) charge;
        explode_vars(ip)->owner = owner;
        explode_init(gobj);
    }
    return gobj;
}

static bool flash_anim1(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc2_FlashVars* v = flash_vars(ip);
    itFlashAttributes* a = DP(ip->xC4_article_data->x4_specialAttributes);
    ip->xDAC_itcmd_var0++;
    if (v->reflected == 0 && v->owner != NULL && ip->owner == v->owner &&
        (u32) ip->xDAC_itcmd_var0 >= (u32) ((s32) a->x28_FLASH_EXPLOSION_DELAY - 2)) {
        explode_spawn(ip->owner, gobj, &ip->pos,
                      mu_ak_article_kind(ip->owner, 8), ip->facing_dir, (float) v->charge);
        return true;
    }
    flash_scale(gobj);
    return it_80273130(gobj);
}

static bool flash_anim2(Item_GObj* gobj)
{
    flash_scale(gobj);
    return it_80273130(gobj);
}

static void item_stop(Item_GObj* gobj)
{
    GET_ITEM(gobj)->x40_vel = (Vec3) { 0.0f, 0.0f, 0.0f };
}

static void explode_phys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ip->x40_vel.y = ip->x40_vel.z = 0.0f;
}

static void explode_destroy(Item_GObj* gobj)
{
    if (gobj != NULL && GET_ITEM(gobj) != NULL) {
        Item* ip = GET_ITEM(gobj);
        it_802725D4(gobj);
        ip->owner = NULL;
    }
}

static bool thunder_coll(Item_GObj* gobj)
{
    return it_803F6BC8[0].collided(gobj);
}

/* Article 2 +C7C and article 8 +7C. */
static bool explode_anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc2_ExplodeVars* v = explode_vars(ip);
    itFlashExplAttributes* a = DP(ip->xC4_article_data->x4_specialAttributes);
    float ratio = (a->x8_FLASH_EXPL_GRAPHIC_SIZE_GROWTH - a->x4_FLASH_EXPL_GRAPHIC_SIZE_INIT) /
        a->x0_FLASH_EXPL_HITBOX_SIZE_MUL;
    float scale = __builtin_fmaf((float) v->charge, ratio, a->x4_FLASH_EXPL_GRAPHIC_SIZE_INIT);
    HSD_JObj* joint = GET_JOBJ(gobj);
    joint->scale = (Vec3) { scale, scale, scale };
    HSD_JObjSetMtxDirtySub(joint);
    if (ip->xDB4_itcmd_var2 == 0 && ip->x5D4_hitboxes[0].hit.state != HitCapsule_Disabled) {
        u32 damage = (u32) (s32) __builtin_fmaf(a->x10_FLASH_EXPL_DAMAGE_MUL, (float) v->charge,
                                     a->xC_FLASH_EXPL_BASE_DAMAGE);
        it_80272460(&ip->x5D4_hitboxes[0].hit, damage, gobj);
        ip->xDB4_itcmd_var2 = 1;
    }
    if (v->saved_scale == 0) v->saved_scale = (s32) ip->x5D4_hitboxes[0].hit.scale;
    ip->x5D4_hitboxes[0].hit.scale = (float) v->saved_scale * scale;
    return !it_80272C6C(gobj);
}

/* Article 2 +A84. */
static void explode_init(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itFlashExplAttributes* a = DP(ip->xC4_article_data->x4_specialAttributes);
    float ratio;
    Item_ClearFlagsAndEnterState(gobj, ip, 0);
    it_80275158(gobj, 1024.0f);
    explode_vars(ip)->saved_scale = 0;
    Item_802694CC(gobj);
    explode_anim(gobj);
    ratio = (float) explode_vars(ip)->charge / a->x0_FLASH_EXPL_HITBOX_SIZE_MUL;
    if (ratio > 0.84999996f) {
        Camera_RequestQuake(QuakeKind_Medium, &ip->pos);
        it_80273598(gobj, 14, 20);
    } else if (ratio > 0.65f) {
        Camera_RequestQuake(QuakeKind_Small, &ip->pos);
        it_80273598(gobj, 13, 15);
    } else if (ratio > 0.45f) {
        it_80273598(gobj, 12, 10);
    } else {
        it_80273598(gobj, 11, 5);
    }
    db_80225DD8(gobj, ip->owner);
}

static ItemStateTable flash_states[3] = {
    { 0, flash_anim0, flash_phys0, flash_coll0 },
    { 1, flash_anim1, item_stop, item_false },
    { 2, flash_anim2, item_stop, item_false },
};
static ItemStateTable explode_states[1] = {
    { 0, explode_anim, explode_phys, item_false },
};
/* Article 3 is an explicit retail wrapper table. Its damage callback returns zero. */
static ItemStateTable thunder_states[1] = {
    { 0, itNesspkthunderball_UnkMotion0_Anim,
      itNesspkthunderball_UnkMotion0_Phys, thunder_coll },
};

ItemLogicTable ftLc2_ArticleLogic[9] = {
    [0] = {
        .states = fire_states, .spawned = item_noop, .destroyed = item_noop,
        .picked_up = item_noop, .dropped = item_noop, .thrown = item_noop,
        .dmg_dealt = fire_damage, .dmg_received = item_true, .entered_air = item_noop,
        .reflected = fire_reflect, .shield_bounced = item_false, .hit_shield = item_true,
    },
    [1] = {
        .states = unused_states, .spawned = item_noop, .destroyed = item_noop,
        .picked_up = item_noop, .dropped = item_noop, .thrown = item_noop,
        .dmg_dealt = item_false, .dmg_received = item_false, .entered_air = item_noop,
        .reflected = item_false, .shield_bounced = item_false, .hit_shield = item_false,
    },
    [2] = {
        .states = flash_states, .destroyed = flash_destroy,
        .reflected = itNesspkflash_Logic102_Reflected,
        .absorbed = item_true, .evt_unk = itNesspkflash_Logic102_EvtUnk,
    },
    [3] = {
        .states = thunder_states, .destroyed = it_802AB90C,
        .dmg_dealt = item_false, .reflected = it_802AC098,
        .shield_bounced = it_802AC35C, .hit_shield = it_802AC3F8,
    },
    [8] = {
        .states = explode_states, .destroyed = explode_destroy,
        .absorbed = item_true, .shield_bounced = item_true, .hit_shield = item_true,
        .evt_unk = itNessPKFlashExplode_Logic103_EvtUnk,
    },
};
