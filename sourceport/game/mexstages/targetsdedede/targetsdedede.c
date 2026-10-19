/* Targets!Dedede (Akaneia 1.0.1 and ACE 2.0.0, the same file: internal 93, external 310), the
 * m-ex grFunction block of GrTDe.dat (197 instructions with symbols) and its one itFunction block
 * (1352 instructions, stage item 0: the walking enemy). Disassembly: `py -3.12 grdis.py ak
 * GrTDe.dat` (out_dis_GrTDe.txt) and `... GrTDe.dat it 0` (out_dis_GrTDe_it0.txt) in
 * run-source/rel09-stages.
 *
 * Exports 1, 3, 5, 6 and 8; the other slots keep the MxDt row's words. Three map gobjs (points,
 * background, floor, animation only); the retail targets (Ground_801C4210, by the raw address the
 * block calls) and an enemy at each of the stage points 0xFC to 0x103 the file has. The touch-line
 * callback hands out the dynamics of the joint 1 lines from the stage's yakumono parameters, as the
 * retail target stages do.
 *
 * The enemy is MexTK's walker, built on Mr. Saturn's item code (several of its helpers are retail
 * functions the block calls by raw address: it_8027B730, it_80274484, it_80276CB8, it_8026E8C4,
 * Item_80268DD4, lb_8000BA0C). It waits, turns to look around, walks for a random time, looks
 * again, turns at walls and ledges, falls, can be picked up and thrown, and is knocked back when
 * hit. */
#include "../peachcastle64/wp1_common.h"

#include <dolphin/mtx.h>
#include "../../akaneia/mu_ak_fighter.h"
#include <melee/gr/granime.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/it/it_3F14.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <melee/it/itzako.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>

/* ---- the stage ---- */

/* The stage's yakumono parameters: the dynamics of the joint 1 lines by line kind. */
typedef struct MexgrTdYakumono {
    DISC_PTR(DynamicsDesc) floor;
    DISC_PTR(DynamicsDesc) ceiling;
    DISC_PTR(DynamicsDesc) right_wall;
    DISC_PTR(DynamicsDesc) left_wall;
} DISC_STRUCT MexgrTdYakumono;

/* Points_OnCreation (+154), Background_OnCreation (+190), Floor_OnCreation (+1E8) */
static void mexgr_td_anim_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

/* Background_OnFrame (+1C0) */
static void mexgr_td_background_frame(Ground_GObj* gobj)
{
    (void) gobj;
    lb_800115F4();
}

/* The OnUnk words (lone blrs, never called). */
static bool mexgr_td_unk(Ground_GObj* gobj)
{
    (void) gobj;
    return false;
}

/* map_gobjs (+0) */
static StageCallbacks mexgr_td_map[3] = {
    { mexgr_td_anim_create, mexgr_td_unk, mu_gr_gobj_nop, mu_gr_gobj_nop, 0 },
    { mexgr_td_anim_create, mexgr_td_unk, mexgr_td_background_frame, mu_gr_gobj_nop, 0 },
    { mexgr_td_anim_create, mexgr_td_unk, mu_gr_gobj_nop, mu_gr_gobj_nop, 0xC0000000 },
};

/* Spawn_Enemy (+298): stage item 0 standing at `pos`, facing right, through Item_8026862C. */
static void mexgr_td_spawn_enemy(Vec3* pos)
{
    SpawnItem spawn;
    spawn.x0_parent_gobj = NULL;
    spawn.x4_parent_gobj2 = NULL;
    spawn.kind = mu_gr_item_kind(0);
    spawn.hold_kind = 0;
    spawn.x10 = 0;
    spawn.pos = *pos;
    spawn.prev_pos = *pos;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0F;
    spawn.facing_dir = 1.0F;
    spawn.x3C_damage = 0;
    spawn.x3E = 0;
    spawn.x40 = 0;
    spawn.x44_flag.b0 = 1;
    spawn.x48_ground_or_air = 0;
    mu_ak_item_create(&spawn);
}

/* SpawnEnemies (+228): an enemy at each stage point 0xFC .. 0x103 the file has. */
static void mexgr_td_spawn_enemies(void)
{
    int id;
    for (id = 0xFC; id != 0x104; id++) {
        Vec3 pos;
        if (Ground_801C2D24(id, &pos)) {
            Vec3 at = pos;
            mexgr_td_spawn_enemy(&at);
        }
    }
}

/* OnInit (+3C) */
static void mexgr_td_init(void)
{
    mexgr_wp1_init(3);
    Ground_801C4210();
    mexgr_td_spawn_enemies();
}

/* OnTouchLine (+94) */
static DynamicsDesc* mexgr_td_touch_line(int line)
{
    MexgrTdYakumono* param;
    int joint;
    if (line == -1) {
        return NULL;
    }
    joint = mpJointFromLine(line);
    param = Ground_GetYakumonoParam();
    if (joint != 1) {
        return NULL;
    }
    switch (mpLineGetKind(line)) {
    case 1: return DP(param->floor);
    case 2: return DP(param->ceiling);
    case 4: return DP(param->right_wall);
    case 8: return DP(param->left_wall);
    default: return NULL;
    }
}

/* ---- stage item 0: the walker (itFunction block 0) ---- */

/* Its attributes (article special attributes). */
typedef struct MexitTdAttrs {
    /* +0 */ f32 anim_rate;     /* carried and thrown */
    /* +4 */ s32 spawn_wait;
    /* +8 */ f32 walk_speed;
    /* +C */ s32 look_frames;   /* a look turns pi in this many frames */
    /* +10 */ s32 knock_x;
    /* +14 */ s32 knock_y;
    /* +18 */ s32 x18;
    /* +1C */ s32 x1C;
    /* +20 */ s32 walk_min;
    /* +24 */ s32 walk_max;
} DISC_STRUCT MexitTdAttrs;

/* Its variables (ip+DD4 on). */
typedef struct MexitTdVars {
    /* +0 ip+DD4 */ u8 x0_pad[0x20];
    /* +20 ip+DF4 */ int spawn_wait;
    /* +24 ip+DF8 */ int look;         /* 1 looking, 2 done, 0 turning */
    /* +28 ip+DFC */ float look_angle;
    /* +2C ip+E00 */ float throw_scale;
    /* +30 ip+E04 */ Vec3 last_pos;
    /* +3C ip+E10 */ u8 x3C_pad[0x8];
    /* +44 ip+E18 */ Vec3 floor_normal;
    /* +50 ip+E24 */ int walk_frames;
} MexitTdVars;
_Static_assert(sizeof(MexitTdVars) <= sizeof(((Item*) 0)->xDD4_itemVar), "walker vars fit");

static MexitTdVars* mexit_td_vars(Item* ip) { return (MexitTdVars*) &ip->xDD4_itemVar; }
static MexitTdAttrs* mexit_td_attrs(Item* ip) { return DP(ip->xC4_article_data->x4_specialAttributes); }

static void mexit_td_walk_enter(Item_GObj* gobj);
static void mexit_td_fall_enter(Item_GObj* gobj);
static void mexit_td_turn_enter(Item_GObj* gobj);
static void mexit_td_stun_enter(Item_GObj* gobj);
static void mexit_td_spawn_enter(Item_GObj* gobj);
static void mexit_td_spawn_air_enter(Item_GObj* gobj);

/* Item_SetAnimRate (+110C) */
static void mexit_td_anim_rate(Item_GObj* gobj, float rate)
{
    GET_ITEM(gobj)->x5D0_animFrameSpeed = rate;
    lb_8000BA0C(GET_JOBJ(gobj), rate);
}

/* Item_SetRotationBlend (+1148): face the way the item faces, turned by `angle` (facing * pi/2
 * in double plus facing * angle in float). */
static void mexit_td_face(Item_GObj* gobj, float angle)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    float facing = ip->facing_dir;
    jobj->rotate.x = 0.0F;
    jobj->rotate.y = (float) ((double) facing * 0x1.921fb54442d18p+0 + (double) (facing * angle));
    jobj->rotate.z = 0.0F;
    HSD_JObjSetMtxDirtySub(jobj);
}

static void mexit_td_track(Item* ip)
{
    mexit_td_vars(ip)->last_pos = ip->pos;
}

/* Walking, carried and hit set the same two flags: x17 = 1, x19 = 1. */
static void mexit_td_upright(Item* ip)
{
    ip->xDC8_word.flags.x17 = 1;
    ip->xDC8_word.flags.x19 = 1;
}

/* Spawn_Enter (+F88): standing still, waiting. */
static void mexit_td_spawn_enter(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itResetVelocity(ip);
    it_802762B0(ip);
    Item_80268E5C(gobj, 7, 3);
    mexit_td_anim_rate(gobj, 1.0F);
    ip->owner = NULL;
}

/* SpawnAir_Enter (+15D8) */
static void mexit_td_spawn_air_enter(Item_GObj* gobj)
{
    Item_80268E5C(gobj, 8, 3);
    mexit_td_anim_rate(gobj, 1.0F);
    GET_ITEM(gobj)->owner = NULL;
}

/* Pickup_Enter (+1000) */
static void mexit_td_pickup_enter(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitTdAttrs* attr = mexit_td_attrs(ip);
    if (ip->msid != 4) {
        Item_80268E5C(gobj, 4, ITEM_ANIM_UPDATE);
        mexit_td_anim_rate(gobj, attr->anim_rate);
        mexit_td_upright(ip);
        mexit_td_face(gobj, 0.0F);
    } else {
        Item_80268E5C(gobj, 4, ITEM_ANIM_UPDATE);
    }
}

/* Dropped_Enter (+1098) */
static void mexit_td_dropped_enter(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitTdAttrs* attr = mexit_td_attrs(ip);
    ip->xDC8_word.flags.x15 = 1;
    mexit_td_upright(ip);
    Item_80268E5C(gobj, 5, 6);
    mexit_td_anim_rate(gobj, attr->anim_rate);
    mexit_td_face(gobj, 0.0F);
}

/* zz_0282b14_ (+11A4), a copy of itDosei_Logic7_EnteredAir: motion 6 upright, 10 otherwise. */
static void mexit_td_entered_air(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Item_80268E5C(gobj, ip->xDC8_word.flags.x19 ? 6 : 10, 3);
    mexit_td_anim_rate(gobj, 1.0F);
    ip->owner = NULL;
}

/* Walk_Enter (+1208): a random walk time in [walk_min, walk_max), walking speed ahead. */
static void mexit_td_walk_enter(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitTdAttrs* attr = mexit_td_attrs(ip);
    int lo;
    ip->xD5C = 0;
    mexit_td_upright(ip);
    ip->owner = NULL;
    lo = attr->walk_min;
    mexit_td_vars(ip)->walk_frames = lo + HSD_Randi(attr->walk_max - lo);
    it_802762B0(ip);
    Item_80268E5C(gobj, 1, 0xB);
    mexit_td_anim_rate(gobj, 1.0F);
    mexit_td_face(gobj, 0.0F);
    ip->x40_vel.x = ip->facing_dir * attr->walk_speed;
    ip->x40_vel.z = 0.0F;
    ip->x40_vel.y = 0.0F;
}

/* Fall_Enter (+12E4) */
static void mexit_td_fall_enter(Item_GObj* gobj)
{
    Item_80268E5C(gobj, 3, 3);
    mexit_td_anim_rate(gobj, 1.0F);
    GET_ITEM(gobj)->owner = NULL;
}

/* Look_Enter (+1340): state 12, standing. */
static void mexit_td_look_enter(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itResetVelocity(ip);
    Item_80268E5C(gobj, 12, 3);
    mexit_td_anim_rate(gobj, 1.0F);
    ip->owner = NULL;
}

/* Turn_Enter (+13B0): back to the last tracked position, still. */
static void mexit_td_turn_enter(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitTdVars* v = mexit_td_vars(ip);
    v->look = 0;
    ip->pos = v->last_pos;
    v->look_angle = 0.0F;
    ip->x40_vel.x = 0.0F;
    it_802762B0(ip);
    Item_80268E5C(gobj, 2, 3);
    mexit_td_anim_rate(gobj, 1.0F);
    ip->owner = NULL;
}

/* Stun_Enter (+1460) */
static void mexit_td_stun_enter(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    itResetVelocity(ip);
    Item_80268E5C(gobj, 0, 3);
    mexit_td_anim_rate(gobj, 1.0F);
    ip->owner = NULL;
}

/* Enter_WalkOrTurn (+14D0), as itDosei_80281734: walk on when the floor does not slope against
 * the facing, else turn. */
static void mexit_td_walk_or_turn(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    float nx = mexit_td_vars(ip)->floor_normal.x;
    if (ip->facing_dir == 1.0F) {
        if (nx >= 0.0F) {
            mexit_td_walk_enter(gobj);
        } else {
            mexit_td_turn_enter(gobj);
        }
    } else if (nx > 0.0F || nx != nx) {
        mexit_td_turn_enter(gobj);
    } else {
        mexit_td_walk_enter(gobj);
    }
}

/* Idle_Enter (+1540): look, starting a quarter turn (pi/2) away. */
static void mexit_td_idle_enter(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitTdVars* v = mexit_td_vars(ip);
    v->look = 1;
    v->look_angle = 1.5707964F;
    ip->x40_vel.x = 0.0F;
    mexit_td_upright(ip);
    ip->owner = NULL;
    Item_80268E5C(gobj, 9, 3);
    mexit_td_anim_rate(gobj, 1.0F);
    mexit_td_face(gobj, -v->look_angle);
}

/* Stun_Anim (+4E8), states 0 and 12 */
static bool mexit_td_stun_anim(Item_GObj* gobj)
{
    mexit_td_track(GET_ITEM(gobj));
    mexit_td_face(gobj, 0.0F);
    if (!it_80272C6C(gobj)) {
        mexit_td_walk_enter(gobj);
    }
    return false;
}

static void mexit_td_phys_nop(Item_GObj* gobj) { (void) gobj; }

/* Stun_Coll (+55C), Spawn_Coll (+B88), Turn_Coll (+890), Idle_Coll (+D88) share this shape. */
static bool mexit_td_stun_coll(Item_GObj* gobj)
{
    it_8026D62C(gobj, mexit_td_fall_enter);
    it_80276CB8(gobj);
    return false;
}

/* Walk_Anim (+5A8): the walk animation runs faster downhill (0.5 * floor normal x * facing + 1,
 * in double); after the walk time, look around. */
static bool mexit_td_walk_anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitTdAttrs* attr = mexit_td_attrs(ip);
    MexitTdVars* v = mexit_td_vars(ip);
    mexit_td_track(ip);
    mexit_td_anim_rate(gobj, (float) ((double) ip->x378_itemColl.floor.normal.x * 0.5 *
                                          (double) ip->facing_dir + 1.0));
    if (!it_80272C6C(gobj)) {
        Item_80268E5C(gobj, 1, ITEM_ANIM_UPDATE);
    }
    if (v->walk_frames > 0) {
        v->walk_frames--;
    } else {
        int lo = attr->walk_min;
        v->walk_frames = lo + HSD_Randi(attr->walk_max - lo);
        mexit_td_look_enter(gobj);
    }
    return false;
}

/* Walk_Phys (+698): walking speed by the animation rate, ahead; turn when xDC0 says so. */
static void mexit_td_walk_phys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    float speed = mexit_td_attrs(ip)->walk_speed * ip->x5D0_animFrameSpeed;
    if (speed < 0.0F) {
        speed = -speed;
    }
    ip->x40_vel.x = ip->facing_dir * speed;
    if (ip->xDC0 & 3) {
        mexit_td_turn_enter(gobj);
    }
}

/* Walk_Coll (+700): off the floor it falls; at a ledge it turns; on a slope of pi/4 or more it
 * slides (entered_air), else it walks on. */
static bool mexit_td_walk_coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    float nx;
    if (!it_8026D8A4(gobj, mexit_td_turn_enter)) {
        mexit_td_fall_enter(gobj);
        return false;
    }
    if (it_80276308(gobj) != 0) {
        mexit_td_turn_enter(gobj);
        return false;
    }
    nx = ip->x378_itemColl.floor.normal.x;
    if (nx < 0.0F) {
        nx = -nx;
    }
    if ((double) nx >= 0x1.921fb54442d18p-1) {
        ip->xD5C = 1;
        ip->xDC8_word.flags.x1F = 1;
        mexit_td_entered_air(gobj);
    } else {
        ip->xD5C = 0;
        ip->xDC8_word.flags.x1F = 0;
        it_80276CB8(gobj);
    }
    return false;
}

/* Turn_Anim (+7E8): at the end, walk the other way. */
static bool mexit_td_turn_anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    mexit_td_track(ip);
    if (!it_80272C6C(gobj)) {
        Item_80268E5C(gobj, 1, ITEM_ANIM_UPDATE);
        ip->facing_dir = -ip->facing_dir;
        jobj->rotate.y = (float) ((double) ip->facing_dir * 0x1.921fb54442d18p+0);
        HSD_JObjSetMtxDirtySub(jobj);
    }
    return false;
}

/* Turn_Coll (+890) and Idle_Coll (+D88): done looking means walking; Idle keeps the frame. */
static bool mexit_td_turn_coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    it_8026D62C(gobj, mexit_td_fall_enter);
    if (mexit_td_vars(ip)->look == 2) {
        mexit_td_walk_enter(gobj);
    }
    it_80276CB8(gobj);
    return false;
}

static bool mexit_td_idle_coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    it_8026D62C(gobj, mexit_td_fall_enter);
    if (mexit_td_vars(ip)->look == 2) {
        float frame = ip->x5CC_currentAnimFrame;
        mexit_td_walk_enter(gobj);
        Item_80268DD4(gobj, frame);
    }
    it_80276CB8(gobj);
    return false;
}

/* Fall_Anim (+900) */
static bool mexit_td_fall_anim(Item_GObj* gobj)
{
    mexit_td_track(GET_ITEM(gobj));
    if (!it_80272C6C(gobj)) {
        Item_80268E5C(gobj, 3, ITEM_ANIM_UPDATE);
    }
    return false;
}

/* Fall_Phys (+968), SpawnAir_Phys (+BF8), Damaged_Phys (+F10) */
static void mexit_td_fall_phys(Item_GObj* gobj)
{
    ItemAttr* attrs = GET_ITEM(gobj)->xCC_item_attr;
    it_80272860(gobj, attrs->x10_fall_speed, attrs->x14_fall_speed_max);
}

/* Fall_Coll (+998) */
static bool mexit_td_fall_coll(Item_GObj* gobj)
{
    it_8026E15C(gobj, mexit_td_walk_enter);
    return false;
}

/* Pickup_Anim (+9C4) */
static bool mexit_td_pickup_anim(Item_GObj* gobj)
{
    if (!it_80272C6C(gobj)) {
        mexit_td_pickup_enter(gobj);
    }
    return false;
}

/* Pickup_Coll (+A0C), Dropped_Anim (+A14) */
static bool mexit_td_false(Item_GObj* gobj)
{
    (void) gobj;
    return false;
}

/* Dropped_Phys (+A1C) */
static void mexit_td_dropped_phys(Item_GObj* gobj)
{
    Item_ApplyFallingPhysics(gobj);
}

/* Dropped_Coll (+A78), Damaged_Coll (+F40) */
static bool mexit_td_land_stun(Item_GObj* gobj)
{
    it_8026E15C(gobj, mexit_td_stun_enter);
    return false;
}

/* State6_Anim (+AA4), SpawnAir_Anim (+BD4) */
static bool mexit_td_track_anim(Item_GObj* gobj)
{
    mexit_td_track(GET_ITEM(gobj));
    return false;
}

/* State6_Coll (+ACC): the slide; on landing walk or turn by the floor's slope. */
static bool mexit_td_slide_coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    mexit_td_vars(ip)->floor_normal = ip->x378_itemColl.floor.normal;
    it_8026E8C4(gobj, mexit_td_walk_or_turn, mexit_td_fall_enter);
    return false;
}

/* Spawn_Anim (+B28): wait, then look around. */
static bool mexit_td_spawn_anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitTdVars* v = mexit_td_vars(ip);
    mexit_td_track(ip);
    if (v->spawn_wait > 0) {
        v->spawn_wait--;
        return false;
    }
    mexit_td_idle_enter(gobj);
    return false;
}

/* Spawn_Coll (+B88) */
static bool mexit_td_spawn_coll(Item_GObj* gobj)
{
    it_8026D62C(gobj, mexit_td_spawn_air_enter);
    it_80276CB8(gobj);
    return false;
}

/* SpawnAir_Coll (+C28) */
static bool mexit_td_spawn_air_coll(Item_GObj* gobj)
{
    it_8026E15C(gobj, mexit_td_spawn_enter);
    return false;
}

/* Idle_Anim (+C54): the look turns by pi / look_frames a frame (in double); while looking the
 * model follows it; at 0 the look is done and the walk speed is set. */
static bool mexit_td_idle_anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitTdAttrs* attr = mexit_td_attrs(ip);
    MexitTdVars* v = mexit_td_vars(ip);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    mexit_td_track(ip);
    v->look_angle = (float) ((double) v->look_angle - 0x1.921fb54442d18p+1 / (double) attr->look_frames);
    if (v->look == 1) {
        float facing = ip->facing_dir;
        jobj->rotate.y = (float) ((double) (-v->look_angle * facing) + (double) facing * 0x1.921fb54442d18p+0);
        HSD_JObjSetMtxDirtySub(jobj);
        if (!(v->look_angle > 0.0F) && v->look_angle == v->look_angle) {
            v->look = 2;
            ip->x40_vel.x = ip->facing_dir * attr->walk_speed;
        }
    }
    if (!it_80272C6C(gobj)) {
        Item_80268E5C(gobj, 9, ITEM_ANIM_UPDATE);
    }
    return false;
}

/* State10_Anim (+E1C) */
static bool mexit_td_state10_anim(Item_GObj* gobj)
{
    mexit_td_track(GET_ITEM(gobj));
    mexit_td_anim_rate(gobj, 1.0F);
    return false;
}

/* State10_Coll (+E68) */
static bool mexit_td_state10_coll(Item_GObj* gobj)
{
    it_8026E8C4(gobj, mexit_td_spawn_enter, mexit_td_spawn_air_enter);
    return false;
}

/* Damaged_Anim (+EA8) */
static bool mexit_td_damaged_anim(Item_GObj* gobj)
{
    mexit_td_track(GET_ITEM(gobj));
    if (!it_80272C6C(gobj)) {
        Item_80268E5C(gobj, 11, ITEM_ANIM_UPDATE);
    }
    return false;
}

/* item_state_table (+0) */
static ItemStateTable mexit_td_states[13] = {
    { 5, mexit_td_stun_anim, mexit_td_phys_nop, mexit_td_stun_coll },
    { 0, mexit_td_walk_anim, mexit_td_walk_phys, mexit_td_walk_coll },
    { 2, mexit_td_turn_anim, mexit_td_phys_nop, mexit_td_turn_coll },
    { -1, mexit_td_fall_anim, mexit_td_fall_phys, mexit_td_fall_coll },
    { 3, mexit_td_pickup_anim, mexit_td_phys_nop, mexit_td_false },
    { 1, mexit_td_false, mexit_td_dropped_phys, mexit_td_land_stun },
    { -1, mexit_td_track_anim, mexit_td_phys_nop, mexit_td_slide_coll },
    { 4, mexit_td_spawn_anim, mexit_td_phys_nop, mexit_td_spawn_coll },
    { 4, mexit_td_track_anim, mexit_td_fall_phys, mexit_td_spawn_air_coll },
    { 0, mexit_td_idle_anim, mexit_td_phys_nop, mexit_td_idle_coll },
    { -1, mexit_td_state10_anim, mexit_td_phys_nop, mexit_td_state10_coll },
    { 3, mexit_td_damaged_anim, mexit_td_fall_phys, mexit_td_land_stun },
    { 4, mexit_td_stun_anim, mexit_td_phys_nop, mexit_td_stun_coll },
};

/* OnSpawn (+D0), export 1: still, the spawn wait from the attributes, throw scale 1, facing
 * either way, then it_8027B730 (by raw address) and the spawn state, unrotated. */
static void mexit_td_spawned(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitTdAttrs* attr = mexit_td_attrs(ip);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    MexitTdVars* v = mexit_td_vars(ip);
    itResetVelocity(ip);
    v->spawn_wait = attr->spawn_wait;
    v->throw_scale = 1.0F;
    ip->xDC8_word.flags.x15 = 1;
    ip->facing_dir = HSD_Randi(2) != 0 ? -1.0F : 1.0F;
    ip->xDD0_flag.b4 = 1;
    it_8027B730(gobj);
    ip->xDC8_word.flags.x1C = 1;
    mexit_td_spawn_enter(gobj);
    jobj->rotate.x = 0.0F;
    jobj->rotate.y = 0.0F;
    jobj->rotate.z = 0.0F;
    HSD_JObjSetMtxDirtySub(jobj);
}

/* OnPickup (+1C4), OnDrop (+1E4) */
static void mexit_td_picked_up(Item_GObj* gobj) { mexit_td_pickup_enter(gobj); }
static void mexit_td_dropped(Item_GObj* gobj) { mexit_td_dropped_enter(gobj); }

/* OnThrow (+204): the thrown state, then the joint scale (it_80274484 by raw address). */
static void mexit_td_thrown(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitTdAttrs* attr = mexit_td_attrs(ip);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    if (ip->msid != 5) {
        ip->xDC8_word.flags.x15 = 1;
        Item_80268E5C(gobj, 5, 6);
        mexit_td_anim_rate(gobj, attr->anim_rate);
    } else {
        Item_80268E5C(gobj, 5, 6);
    }
    it_80274484(gobj, jobj, mexit_td_vars(ip)->throw_scale);
}

/* OnGiveDamage (+2B0): a thrown walker bounces off its victim. */
static bool mexit_td_dmg_dealt(Item_GObj* gobj)
{
    if (GET_ITEM(gobj)->msid != 5) {
        return false;
    }
    itColl_BounceOffVictim(gobj);
    it_802725D4(gobj);
    return false;
}

/* OnTakeDamage (+300): knocked back (knock_x ahead, |knock_y| up), 60 frames of life off. */
static bool mexit_td_dmg_received(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitTdAttrs* attr = mexit_td_attrs(ip);
    float up;
    Item_80268E5C(gobj, 11, 3);
    it_802762BC(ip);
    mexit_td_anim_rate(gobj, 1.0F);
    ip->x40_vel.x = (float) attr->knock_x * ip->facing_dir;
    up = (float) attr->knock_y;
    if (up < 0.0F) {
        up = -up;
    }
    ip->x40_vel.y = up;
    ip->x40_vel.z = 0.0F;
    mexit_td_upright(ip);
    mexit_td_face(gobj, 0.0F);
    ip->owner = NULL;
    ip->xD44_lifeTimer -= 60.0F;
    return it_80273130(gobj);
}

/* OnReflect (+43C) */
static bool mexit_td_reflected(Item_GObj* gobj) { return it_80273030(gobj); }

/* OnClank (+45C), OnHitShieldDetermineDestroy (+4A4) */
static bool mexit_td_bounce_victim(Item_GObj* gobj)
{
    itColl_BounceOffVictim(gobj);
    return false;
}

/* OnHitShieldBounce (+480): "li r3, 0" after the bounce. */
static bool mexit_td_shield_bounced(Item_GObj* gobj)
{
    itColl_BounceOffShield(gobj);
    return false;
}

/* OnUnk3 (+4C8), export 14 */
static void mexit_td_evt_unk(Item_GObj* gobj, HSD_GObj* ref)
{
    it_8026B894(gobj, ref);
}

/* Exports 0, 1, 3 .. 10, 12 .. 14. */
static const ItemLogicTable mexit_td_items[1] = {
    {
        .states = mexit_td_states,
        .spawned = mexit_td_spawned,
        .picked_up = mexit_td_picked_up,
        .dropped = mexit_td_dropped,
        .thrown = mexit_td_thrown,
        .dmg_dealt = mexit_td_dmg_dealt,
        .dmg_received = mexit_td_dmg_received,
        .entered_air = mexit_td_entered_air,
        .reflected = mexit_td_reflected,
        .clanked = mexit_td_bounce_victim,
        .shield_bounced = mexit_td_shield_bounced,
        .hit_shield = mexit_td_bounce_victim,
        .evt_unk = mexit_td_evt_unk,
    },
};

const MuGrStage mu_gr_targets_dedede = {
    .name = "Targets!Dedede",
    .file = "/GrTDe.dat",
    .map_gobjs = mexgr_td_map,
    .on_init = mexgr_td_init,
    /* The MxDt row leaves on_demo_init 0 (the file does not export it): the console would
     * call address 0 in the attract demo, which never shows a target stage. Natively a
     * no-op, so the layer has a word to install. */
    .on_demo_init = mu_gr_demo_nop,
    /* The row leaves callback4 and on_check_shadow_render 0 as well. Nothing in the game calls
     * callback4; the shadow check runs only for map joints with the shadow flags. Natively
     * the common no-op and always-render callbacks fill them. */
    .callback4 = mu_gr_false,
    .on_check_shadow_render = mu_gr_shadow_always,
    .on_load = mu_gr_event_nop,
    .on_start = mu_gr_event_nop,
    .on_touch_line = mexgr_td_touch_line,
    .items = mexit_td_items,
    .item_count = 1,
    .flags = MU_GR_READY,
};
