/* Akaneia's Lucas: the PK Thunder head (article 3) and its ten tail segments (articles 4-7).
 * The head keeps a 24-entry history of its positions and angles; every other frame while Lucas
 * holds the move it spawns a tail segment that rides a slot of that history. Lucas steers the head
 * with the stick. The tails reuse Ness's trail animation and collision callbacks
 * (itNesspkthundertrail_UnkMotion0_Anim / _Coll, which the disc points at by address) and their
 * item vars layout. Source: PlLc.dat itFunction items 3 and 4-7. */
#include "lucas.h"

#include <math.h>
#include <string.h>

#include <melee/cm/camera.h>
#include <melee/db/db.h>
#include <melee/ef/eflib.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/ftparts.h>
#include <melee/gr/stage.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/it/kinds/itnesspkthunderball.h>
#include <melee/it/kinds/itnesspkthundertrail.h>
#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/jobj.h>

#define LC_PI 3.141592653589793
#define LC_TWO_PI 6.283185307179586

#define LC_THUNDER_HISTORY 24
#define LC_THUNDER_TAILS 10

typedef struct ftLc_PKThunderAttrs {
    /* +00 */ float x0_LIFE;
    /* +04 */ float x4_SPEED;
    /* +08 */ float x8_START_ANGLE;   /* degrees */
    /* +0C */ float xC_STICK_DEADZONE;
    /* +10 */ float x10_TURN_RATE;    /* degrees per frame */
} DISC_STRUCT ftLc_PKThunderAttrs;

/* ip+0xDD4 */
typedef struct ftLc_PKThunderVars {
    /* +DD4 */ Item_GObj* tails[LC_THUNDER_TAILS];
    /* +DFC */ Vec3 pos_history[LC_THUNDER_HISTORY];
    /* +F1C */ float angle_history[LC_THUNDER_HISTORY]; /* [0] is the current heading */
    /* +F7C */ float speed;
    /* +F80 */ float xF80;
    /* +F84 */ float xF84;
    /* +F88 */ int frame;
    /* +F8C */ int tail_count;
    /* +F90 */ int reflected;      /* the disc stores int 1 in a float slot */
    /* +F94 */ HSD_GObj* owner;    /* Lucas */
    /* +F98 */ float xF98;
} ftLc_PKThunderVars;

static inline ftLc_PKThunderVars* ftLc_Thunder_Vars(Item* ip)
{
    _Static_assert(sizeof(ftLc_PKThunderVars) <= sizeof(ip->xDD4_itemVar), "PK Thunder vars");
    return (ftLc_PKThunderVars*) &ip->xDD4_itemVar;
}

static inline ftLc_PKThunderAttrs* ftLc_Thunder_Attrs(Item* ip)
{
    return DP(ip->xC4_article_data->x4_specialAttributes);
}

/* The tails use Ness's trail layout: x0 = head, x4 = history slot, x8 = blink counter. */
static inline itNesspkthundertrail_ItemVars* ftLc_Tail_Vars(Item* ip)
{
    return &ip->xDD4_itemVar.nesspkthundertrail;
}

/* ftFunction code+0x6158 / tail +0x140 "ThunderHead_GetPosition": history slot `index` (every
 * second entry of the head's position history). */
void ftLc_ThunderHead_GetPosition(Item_GObj* head, Vec3* out, int index)
{
    Item* ip;
    if (head == NULL || (ip = GET_ITEM(head)) == NULL) {
        if (out != NULL) {
            out->x = out->y = out->z = 0.0f;
        }
        return;
    }
    if (out != NULL) {
        *out = ftLc_Thunder_Vars(ip)->pos_history[index * 2];
    }
}

static bool ftLc_Thunder_OwnerHolds(HSD_GObj* owner)
{
    FtMotionId msid = GET_FIGHTER(owner)->motion_id;
    return msid == ftLc_MS_SpecialHiHold || msid == ftLc_MS_SpecialAirHiHold;
}

/* +0xA24 "ThunderHead_UpdatePositions" */
static void ftLc_Thunder_PushPosition(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc_PKThunderVars* vars = ftLc_Thunder_Vars(ip);
    memmove(&vars->pos_history[1], &vars->pos_history[0],
            sizeof(Vec3) * (LC_THUNDER_HISTORY - 1));
    vars->pos_history[0] = ip->pos;
}

/* +0x798 "ThunderHead_RemoveFromLucas" */
static void ftLc_Thunder_Detach(HSD_GObj* owner)
{
    Fighter* fp = GET_FIGHTER(owner);
    ftLucas_FighterVars* fvars = ftLc_Vars(fp);
    if (fvars->pkthunder_gobj != NULL) {
        fvars->pkthunder_gobj = NULL;
    }
    if ((u32) (fp->motion_id - ftLc_MS_SpecialHiStart) <= 8) {
        efLib_DestroyAll(owner);
        fvars->pkthunder_gfx = 0;
    }
    fp->death2_cb = NULL;
    fp->take_dmg_cb = NULL;
    ftPartSetRotX(fp, 0, 0.0f);
}

/* Let go of one tail segment. */
static void ftLc_Thunder_ReleaseTail(Item_GObj* tail)
{
    Item* tip = GET_ITEM(tail);
    it_802725D4(tail);
    ftLc_Tail_Vars(tip)->x0 = NULL;
    tip->owner = NULL;
    tip->xDC8_word.flags.x14 = false;
}

/* +0x864 "ItemSpawn_ThunderTail" */
static Item_GObj* ftLc_Thunder_SpawnTail(HSD_GObj* owner, Item_GObj* head, Vec3* pos, int index,
                                         u32 ignore_id, float facing)
{
    /* .rodata at +0xA74: which tail article each segment uses. */
    static const int articles[LC_THUNDER_TAILS] = { 4, 4, 4, 4, 4, 5, 5, 5, 6, 7 };
    SpawnItem spawn;
    Item_GObj* gobj;

    if ((u32) index > 9 || owner == NULL) {
        return NULL;
    }
    memset(&spawn, 0, sizeof(spawn));
    spawn.x0_parent_gobj = owner;
    spawn.x4_parent_gobj2 = head;
    spawn.kind = mu_ak_article_kind(owner, articles[index]);
    spawn.pos.x = pos->x;
    spawn.pos.y = pos->y;
    spawn.pos.z = 0.0f;
    spawn.prev_pos = spawn.pos;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0f;
    spawn.facing_dir = facing;
    spawn.x3C_damage = 0;
    spawn.x44_flag.b0 = true;
    spawn.x40 = ignore_id;

    gobj = Item_80268B18(&spawn);
    if (gobj != NULL) {
        Item* ip = GET_ITEM(gobj);
        itNesspkthundertrail_ItemVars* tvars = ftLc_Tail_Vars(ip);
        ip->xDAC_itcmd_var0 = 0;
        ip->xDB0_itcmd_var1 = 0;
        ip->xDB4_itcmd_var2 = 0;
        ip->xDB8_itcmd_var3 = 0;
        tvars->x8 = 0;
        tvars->x0 = head;
        tvars->x4 = index;
        it_8026B3A8(gobj);
        ip->xDC8_word.flags.x14 = false;
        it_80272940(gobj);
        ip->x40_vel.x = 0.0f;
        ip->x40_vel.y = 0.0f;
        ip->x40_vel.z = 0.0f;
        ip->xDCC_flag.b4567 &= 7;
        db_80225DD8(gobj, owner);
        Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    }
    return gobj;
}

/* Wrap into [lo, 2pi] the way the disc does after a turn (double steps). */
static float ftLc_Thunder_Wrap(float a, double lo)
{
    while (a < lo) {
        a = (float) (a + LC_TWO_PI);
    }
    while (a > LC_TWO_PI) {
        a = (float) (a - LC_TWO_PI);
    }
    return a;
}

static void ftLc_Thunder_SetVelFromAngle(Item* ip)
{
    ftLc_PKThunderVars* vars = ftLc_Thunder_Vars(ip);
    ip->x40_vel.x = vars->speed * cosf(vars->angle_history[0]);
    ip->x40_vel.y = vars->speed * sinf(vars->angle_history[0]);
    ip->x40_vel.z = 0.0f;
}

/* ---------------------------------------------------------------------------------------------
 * Head states.
 * ------------------------------------------------------------------------------------------- */

/* +0x35C: grow the tail, and end when Lucas stops holding the move. */
static bool ftLc_Thunder_Anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc_PKThunderVars* vars = ftLc_Thunder_Vars(ip);
    int frame = vars->frame;

    if (!(frame & 1) && vars->tail_count <= 9) {
        int n = vars->tail_count;
        if (ip->owner == vars->owner) {
            vars->tails[n] = ftLc_Thunder_SpawnTail(ip->owner, gobj, &ip->pos, n,
                                                    ip->xAC4_ignoreItemID, ip->facing_dir);
        } else {
            vars->tails[n] = NULL;
        }
        vars->tail_count = n + 1;
    }
    vars->frame = frame + 1;

    if (!vars->reflected && vars->owner != NULL) {
        if (vars->owner != ip->owner) {
            ftLc_PKThunder_OnDestroy(gobj);
            return true;
        }
        if (ip->owner == NULL || !ftLc_Thunder_OwnerHolds(ip->owner)) {
            return true;
        }
    }
    return it_80273130(gobj);
}

/* +0x4A8: shift the angle history, steer toward the stick while held, move along the heading. */
static void ftLc_Thunder_Phys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc_PKThunderVars* vars = ftLc_Thunder_Vars(ip);
    ftLc_PKThunderAttrs* attrs = ftLc_Thunder_Attrs(ip);
    Fighter* fp;
    Vec3 stick;
    float ax, ay;

    memmove(&vars->angle_history[1], &vars->angle_history[0],
            sizeof(float) * (LC_THUNDER_HISTORY - 1));

    if (vars->reflected || vars->owner == NULL || vars->owner != ip->owner ||
        !ftLc_Thunder_OwnerHolds(vars->owner))
    {
        ftLc_Thunder_PushPosition(gobj);
        return;
    }
    fp = GET_FIGHTER(vars->owner);
    stick.x = fp->input.lstick[0].x;
    stick.y = fp->input.lstick[0].y;
    stick.z = 0.0f;
    ax = stick.x < 0.0f ? -stick.x : stick.x;
    ay = stick.y < 0.0f ? -stick.y : stick.y;
    if ((attrs->xC_STICK_DEADZONE < ax || LC_ISNAN(ax)) ||
        (attrs->xC_STICK_DEADZONE < ay || LC_ISNAN(ay)))
    {
        Vec3 cross;
        float between = lbVector_Angle(&ip->x40_vel, &stick);
        float angle = vars->angle_history[0];
        lbVector_CrossprodNormalized(&ip->x40_vel, &stick, &cross);
        if (between < 0.7853982 || LC_ISNAN(between)) {
            /* Close to the stick: ease in proportionally. */
            if (cross.z != 0.0f) {
                float step = between / (45.0f / attrs->x10_TURN_RATE);
                angle = cross.z < 0.0f ? angle - step : angle + step;
            }
        } else if (cross.z != 0.0f) {
            double step = attrs->x10_TURN_RATE * 0.017453292;
            angle = (float) (cross.z < 0.0f ? angle - step : angle + step);
        }
        vars->angle_history[0] = ftLc_Thunder_Wrap(angle, -LC_TWO_PI);
        ftLc_Thunder_SetVelFromAngle(ip);
    }
    ftLc_Thunder_PushPosition(gobj);
}

/* +0x720: the stage kills it (PK Thunder ball's own check, it_802AB4B8). */
static bool ftLc_Thunder_Coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (it_802AB4B8(gobj, &ip->x378_itemColl)) {
        it_802725D4(gobj);
        return true;
    }
    return false;
}

ItemStateTable ftLc_PKThunder_States[1] = {
    { 0, ftLc_Thunder_Anim, ftLc_Thunder_Phys, ftLc_Thunder_Coll },
};

/* itFunction ondestroy, +0x10 */
void ftLc_PKThunder_OnDestroy(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc_PKThunderVars* vars = ftLc_Thunder_Vars(ip);
    int i;

    it_802725D4(gobj);
    if (vars->owner != NULL) {
        if (!vars->reflected && vars->owner == ip->owner) {
            ftLc_Thunder_Detach(vars->owner);
        }
        vars->owner = NULL;
    }
    for (i = 0; i < LC_THUNDER_TAILS; i++) {
        Item_GObj* tail = vars->tails[i];
        if (tail != NULL && ftLc_Tail_Vars(GET_ITEM(tail))->x0 != NULL) {
            ftLc_Thunder_ReleaseTail(tail);
        }
    }
    ip->xDC8_word.flags.x14 = false;
}

/* itFunction ongivedamage, +0xEC */
bool ftLc_PKThunder_OnGiveDamage(Item_GObj* gobj)
{
    return false;
}

/* itFunction onreflect, +0xF4: turn around, drop the tail, and belong to no one. */
bool ftLc_PKThunder_OnReflect(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc_PKThunderVars* vars = ftLc_Thunder_Vars(ip);
    int i;

    if (vars->owner != NULL) {
        if (ip->owner == vars->owner) {
            ftLucas_FighterVars* fvars = ftLc_Vars(GET_FIGHTER(ip->owner));
            if (fvars->pkthunder_gobj != NULL && fvars->pkthunder_gobj == gobj) {
                fvars->pkthunder_gobj = NULL;
            }
        }
        vars->owner = NULL;
    }
    vars->reflected = 1;
    {
        /* Turn around. The disc wraps the result with an upper bound of pi/2 (its
         * .rodata.cst8+0x10) rather than 2pi, so it can come out negative; kept. */
        float a = (float) (vars->angle_history[0] + LC_PI);
        while (a < 0.0f) {
            a = (float) (a + LC_TWO_PI);
        }
        while (a > 1.5707963267948966) {
            a = (float) (a - LC_TWO_PI);
        }
        vars->angle_history[0] = a;
    }
    for (i = 0; i < LC_THUNDER_TAILS; i++) {
        if (vars->tails[i] != NULL) {
            ftLc_Thunder_ReleaseTail(vars->tails[i]);
            vars->tails[i] = NULL;
        }
    }
    vars->frame = 0;
    vars->tail_count = 0;
    ftLc_Thunder_SetVelFromAngle(ip);
    return false;
}

/* itFunction onhitshieldbounce, +0x29C */
bool ftLc_PKThunder_OnHitShieldBounce(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc_PKThunderVars* vars = ftLc_Thunder_Vars(ip);
    float a;
    lbVector_Mirror(&ip->x40_vel, &ip->xC58);
    ip->x40_vel.z = 0.0f;
    a = atan2f(ip->x40_vel.y, ip->x40_vel.x);
    vars->angle_history[0] = a;
    while (a < 0.0f) {
        a += 6.2831855f;
    }
    while (a > 6.2831855f) {
        a -= 6.2831855f;
    }
    vars->angle_history[0] = a;
    return false;
}

/* itFunction onhitshielddeterminedestroy, +0x334 */
bool ftLc_PKThunder_OnHitShieldDestroy(Item_GObj* gobj)
{
    ftLc_PKThunder_OnDestroy(gobj);
    return true;
}

/* ftFunction code+0x6E08 "ThunderHead_EnterState" */
static void ftLc_Thunder_Init(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftLc_PKThunderVars* vars = ftLc_Thunder_Vars(ip);
    ftLc_PKThunderAttrs* attrs = ftLc_Thunder_Attrs(ip);
    int i;

    it_8026B3A8(gobj);
    ip->xDCC_flag.b4567 &= 7;
    ip->xDC8_word.flags.x13 = false;
    it_80272940(gobj);
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    it_80275158(gobj, attrs->x0_LIFE);
    /* 23 of the 24 slots (the disc's loop count). */
    for (i = 0; i < LC_THUNDER_HISTORY - 1; i++) {
        vars->angle_history[i] = (float) (attrs->x8_START_ANGLE * 0.0174533);
    }
    vars->speed = attrs->x4_SPEED;
    ip->x40_vel.x = 0.0f;
    ip->x40_vel.y = attrs->x4_SPEED;
    ip->x40_vel.z = 0.0f;
    db_80225DD8(gobj, vars->owner);
}

/* ftFunction code+0x5FC4 "SpawnItem_PKThunder" */
Item_GObj* ftLc_PKThunder_Spawn(HSD_GObj* owner, Vec3* pos, float facing)
{
    SpawnItem spawn;
    Item_GObj* gobj;

    memset(&spawn, 0, sizeof(spawn));
    spawn.x0_parent_gobj = owner;
    spawn.x4_parent_gobj2 = owner;
    spawn.kind = mu_ak_article_kind(owner, ftLc_Art_PKThunder);
    ftLib_80086990(owner, &spawn.pos);
    spawn.prev_pos.x = pos->x;
    spawn.prev_pos.y = pos->y;
    spawn.prev_pos.z = 0.0f;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0f;
    spawn.facing_dir = facing;
    spawn.x3C_damage = 0;
    spawn.x44_flag.b0 = true;
    spawn.x40 = Item_8026AE60();

    gobj = Item_80268B18(&spawn);
    if (gobj != NULL) {
        Item* ip = GET_ITEM(gobj);
        ftLc_PKThunderVars* vars = ftLc_Thunder_Vars(ip);
        int i;
        ip->xDAC_itcmd_var0 = 0;
        ip->xDB0_itcmd_var1 = 0;
        ip->xDB4_itcmd_var2 = 0;
        ip->xDB8_itcmd_var3 = 0;
        for (i = 0; i < LC_THUNDER_TAILS; i++) {
            vars->tails[i] = NULL;
        }
        for (i = 0; i < LC_THUNDER_HISTORY; i++) {
            vars->pos_history[i] = *pos;
        }
        vars->xF84 = 0.0f;
        vars->frame = 0;
        vars->tail_count = 0;
        vars->reflected = 0;
        vars->owner = owner;
        vars->xF98 = 0.0f;
        ftLc_Thunder_Init(gobj);
        /* On stage 0x54 the head does not get a camera box. */
        if (Stage_80225194() == 0x54 && ip->xDCD_flag.b01 != 0 && ip->x520_cameraBox != NULL) {
            Camera_800290D4(ip->x520_cameraBox);
            ip->x520_cameraBox = NULL;
            ip->xDCD_flag.b01 = 0;
        }
    }
    return gobj;
}

/* ---------------------------------------------------------------------------------------------
 * Tail segments (items 4-7, identical code).
 * ------------------------------------------------------------------------------------------- */

/* +0x10: follow the head's history and point along it, stretched with the head's speed. */
static void ftLc_ThunderTail_Phys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itNesspkthundertrail_ItemVars* tvars = ftLc_Tail_Vars(ip);
    Item_GObj* head = tvars->x0;
    HSD_JObj* jobj;
    Item* hip;
    float prev_x = 0.0f, prev_y = 0.0f;
    float a;

    if (head == NULL) {
        return;
    }
    jobj = GET_JOBJ(gobj);
    ftLc_ThunderHead_GetPosition(head, &ip->pos, tvars->x4);
    hip = GET_ITEM(head);
    jobj->scale.z = hip != NULL ? (ftLc_Thunder_Vars(hip)->speed * 2.0f) * 0.25f : 0.0f;
    HSD_JObjSetMtxDirtySub(jobj);

    if (tvars->x0 != NULL && GET_ITEM(tvars->x0) != NULL) {
        Vec3* prev = &ftLc_Thunder_Vars(GET_ITEM(tvars->x0))->pos_history[(tvars->x4 + 1) * 2];
        prev_x = prev->x;
        prev_y = prev->y;
    }
    a = atan2f(ip->pos.y - prev_y, ip->pos.x - prev_x);
    if (ip->facing_dir != 1.0f) {
        a = (float) (a + LC_PI);
    }
    jobj->rotate.x = -ip->facing_dir * a;
    HSD_JObjSetMtxDirtySub(jobj);
}

ItemStateTable ftLc_PKThunderTail_States[1] = {
    { 0, itNesspkthundertrail_UnkMotion0_Anim, ftLc_ThunderTail_Phys,
      itNesspkthundertrail_UnkMotion0_Coll },
};
