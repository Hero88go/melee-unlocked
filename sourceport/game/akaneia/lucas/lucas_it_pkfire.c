/* Akaneia's Lucas: the PK Fire bolt (article 1). State 0 flies (no physics callback: it keeps
 * its spawn velocity); hitting someone turns it into a lingering spark (state 1) that drifts
 * slowly. Source: PlLc.dat itFunction item 1. */
#include "lucas.h"

#include <string.h>

#include <melee/db/db.h>
#include <melee/ef/efsync.h>
#include <melee/ft/ftlib.h>
#include <melee/it/it_2725.h>
#include <melee/it/itmaplib.h>
#include <melee/mp/mpcoll.h>
#include <sysdolphin/baselib/jobj.h>

typedef struct ftLc_PKFireAttrs {
    /* +00 */ float x0_LIFE;
} DISC_STRUCT ftLc_PKFireAttrs;

/* ip+0xDD4 */
typedef struct ftLc_PKFireVars {
    /* +DD4 */ int xDD4;
    /* +DD8 */ HSD_GObj* owner;
} ftLc_PKFireVars;

static inline ftLc_PKFireVars* ftLc_PKFire_Vars(Item* ip)
{
    return (ftLc_PKFireVars*) &ip->xDD4_itemVar;
}

static inline ftLc_PKFireAttrs* ftLc_PKFire_Attrs(Item* ip)
{
    return DP(ip->xC4_article_data->x4_specialAttributes);
}

/* +0xE0 / +0x170 */
static bool ftLc_PKFire_Anim(Item_GObj* gobj)
{
    return it_80273130(gobj);
}

/* +0xE4 */
static bool ftLc_PKFire_Coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    bool touched;
    s32 map;

    it_80276214(gobj);
    ip->pos = ip->x378_itemColl.cur_pos;
    touched = mpColl_800471F8(&ip->x378_itemColl);
    map = it_80276308(gobj);
    if (touched) {
        ip->xC30 = ip->x378_itemColl.floor.index;
    }
    return ((touched | map) & 0xD) != 0;
}

ItemStateTable ftLc_PKFire_States[2] = {
    { 0, ftLc_PKFire_Anim, NULL, ftLc_PKFire_Coll },
    { 1, ftLc_PKFire_Anim, NULL, ftLc_PKFire_Coll },
};

/* +0x174 "Enter_FireHit": become the spark. */
static void ftLc_PKFire_EnterHit(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Item_80268E5C(gobj, 1, ITEM_ANIM_UPDATE);
    it_80275158(gobj, 20.0f);
    ftLc_PKFire_Vars(ip)->xDD4 = 0;
    ip->xDAC_itcmd_var0 = 0;
    ip->xDB0_itcmd_var1 = 0;
    HSD_JObjSetFlagsAll(GET_JOBJ(gobj), JOBJ_HIDDEN);
    ip->x40_vel.x = ip->x40_vel.x < 0.0f ? -0.16f : 0.16f;
    ip->x40_vel.y = 0.0f;
    ip->x40_vel.z = 0.0f;
}

/* itFunction ondestroy, +0x20: fizzle effect if it dies in flight. */
void ftLc_PKFire_OnDestroy(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (ip->msid == 0) {
        efSync_Spawn(0x421, gobj, &ip->pos);
    }
}

/* itFunction ongivedamage, +0x44 */
bool ftLc_PKFire_OnGiveDamage(Item_GObj* gobj)
{
    if (GET_ITEM(gobj)->msid == 0) {
        ftLc_PKFire_EnterHit(gobj);
    }
    return false;
}

/* itFunction onreflect, +0x80 */
bool ftLc_PKFire_OnReflect(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ip->xD44_lifeTimer = ftLc_PKFire_Attrs(ip)->x0_LIFE;
    return it_80273030(gobj);
}

/* itFunction onhitshieldbounce, +0x98 */
bool ftLc_PKFire_OnHitShieldBounce(Item_GObj* gobj)
{
    HSD_JObj* jobj = GET_JOBJ(gobj);
    jobj->rotate.z = -jobj->rotate.z;
    HSD_JObjSetMtxDirtySub(jobj);
    return itColl_BounceOffShield(gobj);
}

/* itFunction onhitshielddeterminedestroy, +0xD8 */
bool ftLc_PKFire_OnHitShieldDestroy(Item_GObj* gobj)
{
    return true;
}

/* ftFunction code+0x64E4 "Spawn_PKFire" */
Item_GObj* ftLc_PKFire_Spawn(HSD_GObj* owner, Vec3* pos, Vec3* vel, float facing)
{
    SpawnItem spawn;
    Item_GObj* gobj;

    memset(&spawn, 0, sizeof(spawn));
    spawn.x0_parent_gobj = owner;
    spawn.x4_parent_gobj2 = owner;
    spawn.kind = mu_ak_article_kind(owner, ftLc_Art_PKFire);
    ftLib_80086990(owner, &spawn.pos);
    spawn.prev_pos.x = pos->x;
    spawn.prev_pos.y = pos->y;
    spawn.prev_pos.z = 0.0f;
    spawn.vel = *vel;
    spawn.facing_dir = facing;
    spawn.x3C_damage = 0;
    spawn.x44_flag.b0 = true;
    spawn.x40 = 0;

    gobj = Item_80268B18(&spawn);
    if (gobj != NULL) {
        Item* ip = GET_ITEM(gobj);
        HSD_JObj* jobj;
        db_80225DD8(gobj, owner);
        Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
        it_80275158(gobj, ftLc_PKFire_Attrs(ip)->x0_LIFE);
        ip->owner = owner;
        ftLc_PKFire_Vars(ip)->owner = owner;
        /* Mirror the model for the facing. */
        jobj = GET_JOBJ(gobj);
        jobj->scale.x = spawn.facing_dir;
        HSD_JObjSetMtxDirtySub(jobj);
    }
    return gobj;
}
