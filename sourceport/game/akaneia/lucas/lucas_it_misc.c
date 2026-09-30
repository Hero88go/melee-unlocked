/* Akaneia's Lucas: the stick (article 8: forward smash, win pose) and the Rope Snake (article 9:
 * grab, dash grab, taunt, tether, intro), their spawners on the fighter side, and the article
 * table the integration layer registers. Source: PlLc.dat itFunction items 8 and 9, and
 * ftFunction code+0x3C84..0x3FF8 / 0x5474. */
#include "lucas.h"

#include <string.h>

#include <melee/ft/ft_0BF0.h>
#include <melee/it/it_2725.h>
#include <sysdolphin/baselib/jobj.h>

/* Both articles keep the fighter that spawned them at ip+0xDD4. */
static inline HSD_GObj** ftLc_Article_Owner(Item* ip)
{
    return (HSD_GObj**) &ip->xDD4_itemVar;
}

/* ---------------------------------------------------------------------------------------------
 * Fighter-side spawners.
 * ------------------------------------------------------------------------------------------- */

static Item_GObj* ftLc_SpawnHeld(HSD_GObj* gobj, Vec3* pos, int part, float facing, int article,
                                 bool with_life, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    SpawnItem spawn;
    Item_GObj* item_gobj;

    memset(&spawn, 0, sizeof(spawn));
    spawn.x0_parent_gobj = gobj;
    spawn.x4_parent_gobj2 = gobj;
    spawn.kind = mu_ak_article_kind(gobj, article);
    spawn.pos.x = pos->x;
    spawn.pos.y = pos->y;
    spawn.pos.z = 0.0f;
    spawn.prev_pos = spawn.pos;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0f;
    spawn.facing_dir = facing;
    spawn.x3C_damage = 0;
    spawn.x44_flag.b0 = false;
    spawn.x40 = 0;

    item_gobj = Item_80268B18(&spawn);
    if (item_gobj != NULL) {
        Item* ip = GET_ITEM(item_gobj);
        HSD_JObj* jobj;
        float scale;
        ip->xDAC_itcmd_var0 = 0;
        ip->xDB0_itcmd_var1 = 0;
        ip->xDB4_itcmd_var2 = 0;
        ip->xDB8_itcmd_var3 = 0;
        ip->xDCC_flag.b0 = false;
        *ftLc_Article_Owner(ip) = gobj;
        if (with_life) {
            it_80275158(item_gobj, 1200.0f);
        }
        Item_8026AB54(item_gobj, gobj, part);
        if (state != -1) {
            Item_80268E5C(item_gobj, state, ITEM_ANIM_UPDATE);
        }
        /* Scale with the fighter. */
        jobj = GET_JOBJ(item_gobj);
        scale = ip->xCC_item_attr->x60_scale * fp->x34_scale.y;
        jobj->scale.x = scale;
        jobj->scale.y = scale;
        jobj->scale.z = scale;
        HSD_JObjSetMtxDirtySub(jobj);
        ip->scl = fp->x34_scale.y;
    }
    return item_gobj;
}

/* code+0x3C84 "SpawnItem_Stick" */
Item_GObj* ftLc_SpawnStick(HSD_GObj* gobj, Vec3* pos, int part, float facing)
{
    return ftLc_SpawnHeld(gobj, pos, part, facing, ftLc_Art_Stick, false, -1);
}

/* code+0x3E00 "SpawnItem_Snake": `state` -1 keeps the article's first state. */
Item_GObj* ftLc_SpawnSnake(HSD_GObj* gobj, Vec3* pos, int part, int state, float facing)
{
    return ftLc_SpawnHeld(gobj, pos, part, facing, ftLc_Art_Snake, true, state);
}

/* code+0x5474 "Snake_DestroyCB": put the snake away (not while hanging from it). */
void ftLc_Snake_DestroyCB(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);
    if (vars->held_item_gobj == NULL || fp->motion_id == ftLc_MS_AirCatchHit) {
        return;
    }
    ftLc_RemoveAndDestroyItem(vars->held_item_gobj);
    vars->held_item_gobj = NULL;
}

/* code+0x3F78 "Common_SpawnSnake": in his hand, with its head standing in for one of his bones
 * (parts[139]) so grab and tether positions follow it. */
void ftLc_CommonSpawnSnake(HSD_GObj* gobj, int state)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftLucas_FighterVars* vars = ftLc_Vars(fp);
    Item_GObj* snake = ftLc_SpawnSnake(gobj, &fp->cur_pos, ftLc_Part_Hand, state, fp->facing_dir);

    /* The console calls it_80272CC0 on the result even when the spawn failed. */
    if (snake != NULL) {
        fp->parts[ftLc_Part_SnakeHead].joint = it_80272CC0(snake, 10);
    }
    vars->held_item_gobj = snake;
    if (snake != NULL) {
        fp->x21EC = ftLc_Snake_DestroyCB;
        fp->death2_cb = ftLc_Snake_DestroyCB;
        fp->take_dmg_cb = ftLc_Snake_DestroyCB;
    }
}

/* ---------------------------------------------------------------------------------------------
 * Stick (item 8).
 * ------------------------------------------------------------------------------------------- */

/* +0x48: lives while Lucas is in forward smash (or on the results screen). */
static bool ftLc_Stick_Anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_GObj** owner = ftLc_Article_Owner(ip);
    if (*owner != NULL) {
        Fighter* ofp = GET_FIGHTER(*owner);
        if (ofp->x18 == 14) {
            return false;
        }
        if (ofp->motion_id == ftLc_MS_AttackS4) {
            if (ftLc_Vars(ofp)->held_item_gobj != NULL) {
                return false;
            }
        }
        if (*owner == ip->owner) {
            ftLc_Vars(ofp)->held_item_gobj = NULL;
        }
    }
    *owner = NULL;
    ip->owner = NULL;
    return true;
}

/* +0xB4 */
static bool ftLc_Stick_Coll(Item_GObj* gobj)
{
    return false;
}

ItemStateTable ftLc_Stick_States[1] = {
    { 0, ftLc_Stick_Anim, NULL, ftLc_Stick_Coll },
};

/* itFunction onpickup, +0x10 */
void ftLc_Stick_OnPickup(Item_GObj* gobj)
{
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    Item_802694CC(gobj);
}

/* ---------------------------------------------------------------------------------------------
 * Rope Snake (item 9).
 * ------------------------------------------------------------------------------------------- */

/* +0x2E8 "Common_AnimCB": keep the snake while its owner is in `msid`, else detach and
 * destroy it. */
static bool ftLc_Snake_KeepWhile(Item_GObj* gobj, FtMotionId msid)
{
    Item* ip = GET_ITEM(gobj);
    HSD_GObj** owner = ftLc_Article_Owner(ip);
    if (*owner == NULL) {
        ip->owner = NULL;
        return true;
    }
    {
        Fighter* ofp = GET_FIGHTER(*owner);
        if (ofp->motion_id == msid) {
            return false;
        }
        if (*owner == ip->owner) {
            ftLc_Vars(ofp)->held_item_gobj = NULL;
        }
    }
    *owner = NULL;
    ip->owner = NULL;
    return true;
}

/* +0x84 / +0x100: the grab and dash grab. Once the owner leaves `grab`, the snake detaches and
 * the check against the pull state then always ends it. */
static bool ftLc_Snake_GrabShared(Item_GObj* gobj, FtMotionId grab, FtMotionId pull)
{
    Item* ip = GET_ITEM(gobj);
    HSD_GObj** owner = ftLc_Article_Owner(ip);
    if (*owner != NULL) {
        Fighter* ofp = GET_FIGHTER(*owner);
        if (ofp->motion_id == grab) {
            return false;
        }
        if (*owner == ip->owner) {
            ftLc_Vars(ofp)->held_item_gobj = NULL;
        }
        *owner = NULL;
    }
    ip->owner = NULL;
    return ftLc_Snake_KeepWhile(gobj, pull);
}

static bool ftLc_Snake_Catch_Anim(Item_GObj* gobj)
{
    return ftLc_Snake_GrabShared(gobj, ftCo_MS_Catch, ftCo_MS_Catch + 1);
}

static bool ftLc_Snake_CatchDash_Anim(Item_GObj* gobj)
{
    return ftLc_Snake_GrabShared(gobj, ftCo_MS_CatchDash, ftCo_MS_CatchDash + 1);
}

/* +0x17C: the throw; when its animation ends it switches to the "caught" pose. */
static bool ftLc_Snake_AirCatch_Anim(Item_GObj* gobj)
{
    if (it_80272C6C(gobj)) {
        return ftLc_Snake_KeepWhile(gobj, ftLc_MS_AirCatch);
    }
    Item_80268E5C(gobj, 4, ITEM_ANIM_UPDATE);
    return false;
}

/* +0x1DC */
static bool ftLc_Snake_AirCatchHit_Anim(Item_GObj* gobj)
{
    return ftLc_Snake_KeepWhile(gobj, ftLc_MS_AirCatchHit);
}

/* +0x234: entrance; follow Lucas's scale. */
static bool ftLc_Snake_Intro_Anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_GObj* owner;
    if (ip == NULL) {
        return false;
    }
    owner = *ftLc_Article_Owner(ip);
    if (owner == NULL || !ftCo_800BF228(owner)) {
        return false;
    }
    {
        HSD_JObj* jobj = GET_JOBJ(gobj);
        float s = GET_JOBJ(owner)->scale.y;
        jobj->scale.x = s;
        jobj->scale.y = s;
        jobj->scale.z = s;
        HSD_JObjSetMtxDirtySub(jobj);
    }
    return false;
}

ItemStateTable ftLc_Snake_States[8] = {
    { 0, ftLc_Snake_Catch_Anim, NULL, NULL },       /* grab */
    { 1, ftLc_Snake_CatchDash_Anim, NULL, NULL },   /* dash grab */
    { 2, NULL, NULL, NULL },                        /* taunt */
    { 3, ftLc_Snake_AirCatch_Anim, NULL, NULL },    /* tether throw */
    { 4, ftLc_Snake_AirCatchHit_Anim, NULL, NULL }, /* tether caught */
    { 5, NULL, NULL, NULL },
    { 6, ftLc_Snake_Intro_Anim, NULL, NULL },       /* entrance */
    { 7, ftLc_Snake_Intro_Anim, NULL, NULL },
};

/* itFunction onpickup, +0x80 */
void ftLc_Snake_OnPickup(Item_GObj* gobj)
{
    Item_802694CC(gobj);
}

/* ---------------------------------------------------------------------------------------------
 * The article table (itFunction order = the decomp's ItemLogicTable order).
 * ------------------------------------------------------------------------------------------- */
ItemLogicTable ftLc_ArticleLogic[ftLc_Art_Count] = {
    [ftLc_Art_PKFreeze] = {
        .states = ftLc_PKFreeze_States,
        .destroyed = ftLc_PKFreeze_OnDestroy,
        .reflected = ftLc_PKFreeze_OnReflect,
    },
    [ftLc_Art_PKFire] = {
        .states = ftLc_PKFire_States,
        .destroyed = ftLc_PKFire_OnDestroy,
        .dmg_dealt = ftLc_PKFire_OnGiveDamage,
        .reflected = ftLc_PKFire_OnReflect,
        .shield_bounced = ftLc_PKFire_OnHitShieldBounce,
        .hit_shield = ftLc_PKFire_OnHitShieldDestroy,
    },
    [ftLc_Art_Unused2] = { 0 },
    [ftLc_Art_PKThunder] = {
        .states = ftLc_PKThunder_States,
        .destroyed = ftLc_PKThunder_OnDestroy,
        .dmg_dealt = ftLc_PKThunder_OnGiveDamage,
        .reflected = ftLc_PKThunder_OnReflect,
        .shield_bounced = ftLc_PKThunder_OnHitShieldBounce,
        .hit_shield = ftLc_PKThunder_OnHitShieldDestroy,
    },
    [ftLc_Art_PKThunderTail1] = { .states = ftLc_PKThunderTail_States },
    [ftLc_Art_PKThunderTail2] = { .states = ftLc_PKThunderTail_States },
    [ftLc_Art_PKThunderTail3] = { .states = ftLc_PKThunderTail_States },
    [ftLc_Art_PKThunderTail4] = { .states = ftLc_PKThunderTail_States },
    [ftLc_Art_Stick] = {
        .states = ftLc_Stick_States,
        .picked_up = ftLc_Stick_OnPickup,
    },
    [ftLc_Art_Snake] = {
        .states = ftLc_Snake_States,
        .picked_up = ftLc_Snake_OnPickup,
    },
};
