/* Akaneia's Lucas: the PK Freeze projectile (article 0, and article 10 for Kirby's copy of the
 * move, see ftLc_PKFreeze_HeldSlot). A copy of Ness's PK Flash item
 * (itnesspkflash.c) reworked by the m-ex authors: state 0 charges and drifts, steered by Lucas's
 * stick while he still holds it; state 1 is the burst (at 2x scale), state 2 the burst on
 * contact with the stage. Source: PlLc.dat itFunction item 0 (offsets below are into its code). */
#include "lucas.h"

#include <string.h>

#include <melee/db/db.h>
#include <melee/ef/efsync.h>
#include <melee/ft/ftlib.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/it/kinds/itnesspkflash.h>
#include <sysdolphin/baselib/jobj.h>

/* The article's special attributes (disc data). */
typedef struct ftLc_PKFreezeAttrs {
    /* +00 */ float x0_LIFE;
    /* +04 */ float x4_MAX_CHARGE;
    /* +08 */ float x8_unused;
    /* +0C */ float xC_unused;
    /* +10 */ float x10_LAUNCH_ANGLE;   /* degrees */
    /* +14 */ float x14_LAUNCH_SPEED;
    /* +18 */ float x18_STEER_ACCEL;
    /* +1C */ float x1C_GRAVITY;
    /* +20 */ float x20_MAX_SPEED_X;
    /* +24 */ float x24_TERMINAL_VEL;
    /* +28 */ float x28_BURST_LIFE;
} DISC_STRUCT ftLc_PKFreezeAttrs;

/* ip+0xDD4 */
typedef struct ftLc_PKFreezeVars {
    /* +DD4 */ int xDD4;
    /* +DD8 */ float charge;
    /* +DDC */ int released;          /* never set by the disc code */
    /* +DE0 */ HSD_GObj* owner;       /* Lucas, while he holds the move */
    /* +DE4 */ FighterKind owner_kind;
    /* +DE8 */ int burst_frames;
} ftLc_PKFreezeVars;

static inline ftLc_PKFreezeVars* ftLc_PKFreeze_Vars(Item* ip)
{
    _Static_assert(sizeof(ftLc_PKFreezeVars) <= sizeof(ip->xDD4_itemVar), "PK Freeze vars");
    return (ftLc_PKFreezeVars*) &ip->xDD4_itemVar;
}

static inline ftLc_PKFreezeAttrs* ftLc_PKFreeze_Attrs(Item* ip)
{
    return DP(ip->xC4_article_data->x4_specialAttributes);
}

/* Where the fighter that holds the move keeps this freeze. Kirby's copy of the move uses article 10,
 * whose code (itFunction of PlKbCpLc.dat) is this file's code again with one change: the three
 * routines that look at the holder (State0_AnimCB +0xF4, State0_PhysCB +0x80, OnDestroy +0x5C of
 * that block) read fp+0x2270 where Lucas's own read fp+0x2240. Every other word is the same after
 * relocation, and its OnDestroy lacks only the null test of the holder's Fighter. Article 10 is
 * only ever spawned by a Kirby and article 0 only by Lucas, so the holder's kind picks the word. */
static inline Item_GObj** ftLc_PKFreeze_HeldSlot(Fighter* fp)
{
    return fp->kind == Ft_Kind_Kirby ? ftKbLc_PKFreeze(fp) : &ftLc_Vars(fp)->pkfreeze_gobj;
}

/* Is the owner still Lucas (or Kirby) holding this freeze in his PK Freeze state? */
static Fighter* ftLc_PKFreeze_Holder(Item* ip)
{
    ftLc_PKFreezeVars* vars = ftLc_PKFreeze_Vars(ip);
    Fighter* fp;
    if (vars->owner == NULL || vars->owner != ip->owner) {
        return NULL;
    }
    fp = GET_FIGHTER(vars->owner);
    if (fp == NULL || *ftLc_PKFreeze_HeldSlot(fp) == NULL) {
        return NULL;
    }
    return fp;
}

static void ftLc_PKFreeze_Burst(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Item_80268E5C(gobj, 1, ITEM_ANIM_UPDATE);
    ftLc_PKFreeze_Vars(ip)->burst_frames = 0;
    ip->xD44_lifeTimer = 60.0f;
}

/* +0xC0 */
static bool ftLc_PKFreeze_State0_Anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc_PKFreezeVars* vars = ftLc_PKFreeze_Vars(ip);
    ftLc_PKFreezeAttrs* attrs = ftLc_PKFreeze_Attrs(ip);

    if (vars->released == 0) {
        float charge = vars->charge + 1.0f;
        vars->charge = charge > attrs->x4_MAX_CHARGE ? attrs->x4_MAX_CHARGE : charge;
        if (vars->owner != NULL && vars->owner == ip->owner) {
            Fighter* fp = GET_FIGHTER(vars->owner);
            if (fp == NULL || *ftLc_PKFreeze_HeldSlot(fp) == NULL) {
                /* Lucas let go: burst. */
                ftLc_PKFreeze_Burst(gobj);
                return false;
            }
        }
    }
    if (it_80273130(gobj) == true) {
        ftLc_PKFreeze_Burst(gobj);
    }
    if (!it_80272C6C(gobj)) {
        Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    }
    return false;
}

/* +0x214: steer with Lucas's stick while he holds it, then fall. */
static void ftLc_PKFreeze_State0_Phys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc_PKFreezeVars* vars = ftLc_PKFreeze_Vars(ip);
    ftLc_PKFreezeAttrs* attrs = ftLc_PKFreeze_Attrs(ip);
    float vy;

    if (vars->released == 0) {
        Fighter* fp = ftLc_PKFreeze_Holder(ip);
        if (fp != NULL) {
            float stick = fp->input.lstick[0].x;
            float abs_stick = stick < 0.0f ? -stick : stick;
            if (abs_stick > 0.2) {
                float vx = attrs->x18_STEER_ACCEL * stick + ip->x40_vel.x;
                float max = attrs->x20_MAX_SPEED_X;
                ip->x40_vel.x = vx;
                if (vx < 0.0f) {
                    if (!(max >= -vx)) {
                        ip->x40_vel.x = -max;
                    }
                } else if (vx > max) {
                    ip->x40_vel.x = max;
                }
            }
        }
    }
    vy = ip->x40_vel.y - attrs->x1C_GRAVITY;
    if (vy < -attrs->x24_TERMINAL_VEL) {
        vy = -attrs->x24_TERMINAL_VEL;
    }
    if (attrs->x24_TERMINAL_VEL < vy) {
        vy = attrs->x24_TERMINAL_VEL;
    }
    ip->x40_vel.y = vy;
    ip->x40_vel.z = 0.0f;
}

/* +0x310: touching the stage bursts it (PK Flash's check, it_802AA810). */
static bool ftLc_PKFreeze_State0_Coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (it_802AA810(gobj, &ip->x378_itemColl)) {
        efSync_Spawn(10, ip->owner, &ip->pos);
        Item_80268E5C(gobj, 2, ITEM_ANIM_UPDATE);
        ip->xD44_lifeTimer = ftLc_PKFreeze_Attrs(ip)->x28_BURST_LIFE;
        Item_8026B074(ip);
        Item_8026AE84(ip, 0x86, 0x7F, 0x40);
    }
    return false;
}

static void ftLc_PKFreeze_SetScale(Item_GObj* gobj, float s)
{
    HSD_JObj* jobj = GET_JOBJ(gobj);
    jobj->scale.x = s;
    jobj->scale.y = s;
    jobj->scale.z = s;
    HSD_JObjSetMtxDirtySub(jobj);
}

/* +0x3E8 */
static bool ftLc_PKFreeze_State1_Anim(Item_GObj* gobj)
{
    ftLc_PKFreeze_Vars(GET_ITEM(gobj))->burst_frames++;
    ftLc_PKFreeze_SetScale(gobj, 2.0f);
    return it_80273130(gobj);
}

/* +0x440 (also state 2) */
static void ftLc_PKFreeze_State1_Phys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ip->x40_vel.x = 0.0f;
    ip->x40_vel.y = 0.0f;
    ip->x40_vel.z = 0.0f;
}

/* +0x45C (also state 2) */
static bool ftLc_PKFreeze_State1_Coll(Item_GObj* gobj)
{
    return false;
}

/* +0x464 */
static bool ftLc_PKFreeze_State2_Anim(Item_GObj* gobj)
{
    ftLc_PKFreeze_SetScale(gobj, 2.0f);
    return it_80273130(gobj);
}

ItemStateTable ftLc_PKFreeze_States[3] = {
    { 0, ftLc_PKFreeze_State0_Anim, ftLc_PKFreeze_State0_Phys, ftLc_PKFreeze_State0_Coll },
    { 1, ftLc_PKFreeze_State1_Anim, ftLc_PKFreeze_State1_Phys, ftLc_PKFreeze_State1_Coll },
    { 2, ftLc_PKFreeze_State2_Anim, ftLc_PKFreeze_State1_Phys, ftLc_PKFreeze_State1_Coll },
};

/* itFunction ondestroy, +0x30: tell Lucas it is gone. */
void ftLc_PKFreeze_OnDestroy(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc_PKFreezeVars* vars = ftLc_PKFreeze_Vars(ip);

    it_802725D4(gobj);
    if (vars->owner != NULL) {
        if (vars->owner == ip->owner) {
            Fighter* fp = GET_FIGHTER(vars->owner);
            if (fp != NULL) {
                Item_GObj** held = ftLc_PKFreeze_HeldSlot(fp);
                if (*held != NULL) {
                    *held = NULL;
                }
                fp->death2_cb = NULL;
                fp->take_dmg_cb = NULL;
            }
        }
        vars->owner = NULL;
    }
    ip->xDC8_word.flags.x13 = false;
}

/* itFunction onreflect, +0xB8 */
bool ftLc_PKFreeze_OnReflect(Item_GObj* gobj)
{
    return false;
}

/* ftFunction code+0x6D00 "Init_PKFreeze": start charging and launch at the attribute angle. */
static void ftLc_PKFreeze_Init(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc_PKFreezeVars* vars = ftLc_PKFreeze_Vars(ip);
    ftLc_PKFreezeAttrs* attrs = ftLc_PKFreeze_Attrs(ip);
    float angle;

    it_8026B3A8(gobj);
    ip->xDC8_word.flags.x13 = false;
    it_80272940(gobj);
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    it_80275158(gobj, attrs->x0_LIFE);
    vars->charge = 0.0f;
    vars->released = 0;
    angle = (float) (attrs->x10_LAUNCH_ANGLE * 0.0174533 * ip->facing_dir + 1.5707963267948966);
    ip->x40_vel.x = -attrs->x14_LAUNCH_SPEED * cosf(angle);
    ip->x40_vel.y = attrs->x14_LAUNCH_SPEED * sinf(angle);
    ip->x40_vel.z = 0.0f;
    db_80225DD8(gobj, ip->owner);
}

/* ftFunction code+0x5EB0 "ItemSpawn_PKFreeze" */
Item_GObj* ftLc_PKFreeze_Spawn(HSD_GObj* owner, Vec3* pos, ItemKind kind, float facing)
{
    SpawnItem spawn;
    Item_GObj* gobj;

    memset(&spawn, 0, sizeof(spawn));
    spawn.x0_parent_gobj = owner;
    spawn.x4_parent_gobj2 = owner;
    spawn.kind = kind;
    ftLib_80086990(owner, &spawn.pos);
    spawn.prev_pos.x = pos->x;
    spawn.prev_pos.y = pos->y;
    spawn.prev_pos.z = 0.0f;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0f;
    spawn.facing_dir = facing;
    spawn.x3C_damage = 0;
    spawn.x44_flag.b0 = true;
    spawn.x40 = 0;

    gobj = Item_80268B18(&spawn);
    if (gobj != NULL) {
        Item* ip = GET_ITEM(gobj);
        ftLc_PKFreezeVars* vars = ftLc_PKFreeze_Vars(ip);
        ip->xDAC_itcmd_var0 = 0;
        ip->xDB0_itcmd_var1 = 0;
        ip->xDB4_itcmd_var2 = 0;
        ip->xDB8_itcmd_var3 = 0;
        it_80275158(gobj, ftLc_PKFreeze_Attrs(ip)->x0_LIFE);
        vars->charge = 0.0f;
        vars->released = 0;
        vars->owner = owner;
        vars->owner_kind = GET_FIGHTER(owner)->kind;
        ftLc_PKFreeze_Init(gobj);
    }
    return gobj;
}
