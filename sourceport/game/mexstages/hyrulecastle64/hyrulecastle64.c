/* Hyrule Castle 64 (Akaneia 1.0.1 and ACE 2.0.0, the same file: internal 72, external 289), the
 * m-ex grFunction block of GrOHy.dat (237 instructions with symbols) and its one itFunction block
 * (793 instructions, stage item 0 = the tornado). Disassembly: `py -3.12 grdis.py ak GrOHy.dat`
 * (out_dis_GrOHy.txt) and `... GrOHy.dat it 0` (out_dis_GrOHy_it0.txt) in run-source/rel09-stages.
 *
 * Every export. Three map gobjs: the points (animation only), the background (collision and
 * animation), the floor, whose frame spawns a tornado between joints 2 and 3 every 1200 to 2399
 * frames while none is out. The tornado spawns, waits 90 frames, drifts the other way, and catches
 * every fighter inside its 10 x 27 box into a spin of its own (a motion id the game has no state
 * for, 0x154, with the block's anim and phys callbacks); after 61 frames the spin ends in a hit of
 * 10 percent straight up. After 1200 frames, with nobody spinning, it plays its exit animation and
 * goes. */
#include "../peachcastle64/wp1_common.h"

#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <melee/cm/camera.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/kinds/ftCommon/ftCo_Damage.h>
#include <melee/ft/kinds/ftCommon/ftCo_DamageFall.h>
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/itdraw.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/item.h>
#include <melee/it/itmaplib.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <math.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/state.h>

/* The floor's state in its Ground union: console gp+0xE0 and gp+0xE4 (union +0x1C, +0x20). */
typedef struct MexgrHyFloor {
    u8 x0_pad[0x1C];
    /* +1C gp+E0 */ Item_GObj* tornado;   /* the tornado out, NULL when none */
    /* +20 gp+E4 */ int timer;            /* frames to the next tornado */
} MexgrHyFloor;
_Static_assert(sizeof(MexgrHyFloor) <= sizeof(((Ground*) 0)->u), "floor state fits the Ground union");

/* The tornado's variables (ip+DD4 on, the item variable union). */
typedef struct MexgrHyTornado {
    /* +0 ip+DD4 */ Ground_GObj* floor;   /* the map gobj that spawned it */
    /* +4 ip+DD8 */ int life;             /* 1200, down by one a frame */
    /* +8 ip+DDC */ int wait;             /* 90 frames of waiting before it moves */
} MexgrHyTornado;
_Static_assert(sizeof(MexgrHyTornado) <= sizeof(((Item*) 0)->xDD4_itemVar), "tornado vars fit");

/* A caught fighter's motion variables (fp+2340, fp+2344). */
typedef struct MexgrHySpin {
    /* +0 fp+2340 */ Item_GObj* tornado;
    /* +4 fp+2344 */ int frame;
} MexgrHySpin;
_Static_assert(sizeof(MexgrHySpin) <= sizeof(((Fighter*) 0)->mv), "spin vars fit");

#define MEXGR_HY_SPIN_MOTION 0x154

static MexgrHyFloor* mexgr_hy_floor(Ground* gp) { return (MexgrHyFloor*) &gp->u; }
static MexgrHyTornado* mexgr_hy_vars(Item* ip) { return (MexgrHyTornado*) &ip->xDD4_itemVar; }
static MexgrHySpin* mexgr_hy_spin(Fighter* fp) { return (MexgrHySpin*) &fp->mv; }

/* ---- the stage ---- */

/* Points_OnCreation (+9C) */
static void mexgr_hy_points_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

/* Background_OnCreation (+D8). The block passes the gobj where Ground_InitMapColl takes the joint
 * (as Metal Cavern's); natively the joint is passed. */
static void mexgr_hy_background_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

/* Background_OnFrame (+128) */
static void mexgr_hy_background_frame(Ground_GObj* gobj)
{
    Ground_UpdateMapColl(gobj);
    lb_800115F4();
}

/* Floor_OnCreation (+154) */
static void mexgr_hy_floor_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrHyFloor* fl = mexgr_hy_floor(gp);
    grAnime_801C8138(gobj, gp->map_id, 0);
    fl->timer = HSD_Randi(1200) + 1200;
    fl->tornado = NULL;
}

/* Floor_SpawnTornado (+344): an air spawn (hold kind 8) at `pos`, still, facing `facing`; the
 * tornado keeps the floor gobj. */
static Item_GObj* mexgr_hy_spawn_tornado(Ground_GObj* gobj, ItemKind kind, Vec3* pos, float facing)
{
    SpawnItem spawn;
    Item_GObj* tornado;
    spawn.x0_parent_gobj = NULL;
    spawn.x4_parent_gobj2 = NULL;
    spawn.kind = kind;
    spawn.hold_kind = 8;
    spawn.x10 = 0;
    spawn.pos = *pos;
    spawn.prev_pos = *pos;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0F;
    spawn.facing_dir = facing;
    spawn.x3C_damage = 0;
    spawn.x3E = 0;
    spawn.x40 = 0;
    spawn.x44_flag.b0 = 1;
    spawn.x48_ground_or_air = 0;
    tornado = Item_80268B5C(&spawn);
    if (tornado != NULL) {
        mexgr_hy_vars(GET_ITEM(tornado))->floor = gobj;
    }
    return tornado;
}

/* A random point between two values, as the block does it: the lower one plus a random part of
 * the distance (fmuls, then fadds). */
static float mexgr_hy_between(float a, float b)
{
    float lo = a <= b ? a : b;
    float span = b - a;
    if (span < 0.0F) {
        span = -span;
    }
    return HSD_Randf() * span + lo;
}

/* Floor_OnFrame (+1A4): with no tornado out, the timer runs down; at 0 a tornado spawns at a
 * random point of the box between joints 2 and 3, facing either way, and the timer restarts. */
static void mexgr_hy_floor_frame(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrHyFloor* fl = mexgr_hy_floor(gp);
    Vec3 a, b, pos;
    float facing;
    if (fl->tornado != NULL) {
        return;
    }
    if (--fl->timer != 0) {
        return;
    }
    lb_8000B1CC(Ground_801C3FA4(gobj, 2), NULL, &a);
    lb_8000B1CC(Ground_801C3FA4(gobj, 3), NULL, &b);
    pos.x = mexgr_hy_between(a.x, b.x);
    pos.y = mexgr_hy_between(a.y, b.y);
    pos.z = 0.0F;
    facing = HSD_Randi(2) == 0 ? -1.0F : 1.0F;
    fl->tornado = mexgr_hy_spawn_tornado(gobj, mu_gr_item_kind(0), &pos, facing);
    fl->timer = HSD_Randi(1200) + 1200;
}

/* OnUnk and the lone blrs; OnDeletion "li r3, 0". */
static bool mexgr_hy_false(Ground_GObj* gobj)
{
    (void) gobj;
    return false;
}

/* map_gobjs (+0): OnCreation, OnUnk, OnFrame, OnDeletion, flags. */
static StageCallbacks mexgr_hy_map[3] = {
    { mexgr_hy_points_create, mexgr_hy_false, mu_gr_gobj_nop, mu_gr_gobj_nop, 0 },
    { mexgr_hy_background_create, mexgr_hy_false, mexgr_hy_background_frame, mu_gr_gobj_nop, 0xC0000000 },
    { mexgr_hy_floor_create, mexgr_hy_false, mexgr_hy_floor_frame, mu_gr_gobj_nop, 0 },
};

/* onInit (+3C) */
static void mexgr_hy_init(void)
{
    mexgr_wp1_init(3);
}

/* OnCheckShadowRender (+94): "li r3, 1". */
static bool mexgr_hy_shadow(Vec3* pos, int arg, HSD_JObj* jobj)
{
    (void) pos;
    (void) arg;
    (void) jobj;
    return true;
}

/* ---- stage item 0: the tornado (itFunction block 0) ---- */

static void mexit_hy_main_think(Item_GObj* gobj);

/* stateSpawn (+330): 1200 frames of life, the spawn animation, sound 0 of the stage's bank. */
static void mexit_hy_state_spawn(Item_GObj* gobj)
{
    mexgr_hy_vars(GET_ITEM(gobj))->life = 1200;
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    mu_gr_mex_sfx(0);
}

/* stateWait (+6CC): 90 frames still. */
static void mexit_hy_state_wait(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    mexgr_hy_vars(ip)->wait = 90;
    ip->x40_vel.x = 0.0F;
    Item_80268E5C(gobj, 2, ITEM_ANIM_UPDATE);
    ip->on_accessory = mexit_hy_main_think;
}

/* stateMove (+744) */
static void mexit_hy_state_move(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Item_80268E5C(gobj, 1, ITEM_ANIM_UPDATE);
    ip->on_accessory = mexit_hy_main_think;
}

/* stateExit (+A74) */
static void mexit_hy_state_exit(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ip->x40_vel.x = 0.0F;
    Item_80268E5C(gobj, 3, ITEM_ANIM_UPDATE);
}

/* The current animation is done when the first animation object of the item's model has reached
 * its end frame (curr >= end, ordered; the console reads a NULL object at 4 and 0xC). */
static bool mexit_hy_anim_done(Item_GObj* gobj)
{
    HSD_AObj* aobj = mexgr_wp1_first_aobj_all(GET_JOBJ(gobj));
    return aobj->curr_frame >= aobj->end_frame;
}

/* spawnAnim (+C4) */
static bool mexit_hy_spawn_anim(Item_GObj* gobj)
{
    if (mexit_hy_anim_done(gobj)) {
        mexit_hy_state_wait(gobj);
    }
    return false;
}

/* spawnPhys, moveAnim, waitPhys, exitPhys, exitColl: "li r3, 0". */
static bool mexit_hy_false(Item_GObj* gobj)
{
    (void) gobj;
    return false;
}

static void mexit_hy_phys_nop(Item_GObj* gobj) { (void) gobj; }

/* .text.null (+720) */
static void mexit_hy_null(Item_GObj* gobj) { (void) gobj; }

/* spawnColl (+124) and waitColl (+278) */
static bool mexit_hy_coll_air(Item_GObj* gobj)
{
    it_8026D8A4(gobj, mexit_hy_null);
    return false;
}

/* movePhys (+158): speed up by 0.05 a frame toward the facing, up to 0.5 (the add in double). */
static void mexit_hy_move_phys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    float facing = ip->facing_dir;
    float vel = (float) ((double) ip->x40_vel.x + (double) facing * 0x1.999999999999ap-5);
    float speed = vel < 0.0F ? -vel : vel;
    if (speed > 0.5F) {
        ip->x40_vel.x = facing * 0.5F;
    } else {
        ip->x40_vel.x = vel;
    }
}

/* moveColl (+1C4): the air collision lands into waiting; so does running into a ledge. */
static bool mexit_hy_move_coll(Item_GObj* gobj)
{
    Vec3 edge;
    it_8026D8A4(gobj, mexit_hy_state_wait);
    if (it_80276348(gobj, &edge) != 0) {
        mexit_hy_state_wait(gobj);
    }
    return false;
}

/* waitAnim (+21C): after the wait, turn around and move. */
static bool mexit_hy_wait_anim(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (--mexgr_hy_vars(ip)->wait == 0) {
        ip->facing_dir = -ip->facing_dir;
        mexit_hy_state_move(gobj);
    }
    return false;
}

/* exitAnim (+2A4): the item goes when its exit animation ends. */
static bool mexit_hy_exit_anim(Item_GObj* gobj)
{
    if (mexit_hy_anim_done(gobj)) {
        Item_8026A8EC(gobj);
    }
    return false;
}

/* SpinTornado_Anim (+AAC): after 60 frames of spinning, 10 percent and a hit straight up whose
 * knockback is gravity * percent + 150 (the block reads the gravity attribute). */
static void mexit_hy_spin_anim(Fighter_GObj* fgobj)
{
    Fighter* fp = GET_FIGHTER(fgobj);
    MexgrHySpin* sv = mexgr_hy_spin(fp);
    Item* ip = GET_ITEM(sv->tornado);
    Fighter* hit;
    ftAnim_IsFramesRemaining(fgobj);
    if (++sv->frame <= 60) {
        return;
    }
    fp->cur_pos.x = ip->pos.x;
    fp->cur_pos.z = 0.0F;
    Fighter_TakeDamage_8006CC7C(fp, 10.0F);
    hit = GET_FIGHTER(fgobj);
    hit->dmg.kb_applied = fp->co_attrs.gravity * fp->dmg.x1830_percent + 150.0F;
    hit->dmg.x1848_kb_angle = 90;
    hit->dmg.facing_dir_1 = hit->facing_dir;
    hit->dmg.x184c_damaged_hurtbox = 0;
    hit->dmg.x183C_applied = 0;
    hit->dmg.x1860_element = 0;
    hit->dmg.x1854_collpos = hit->cur_pos;
    ftCo_8008DCE0(fgobj, -1, 0.0F);
}

/* SpinTornado_Phys (+BA0): the top joint circles the tornado, rising, turning one degree a frame;
 * the fighter's position follows that joint. Doubles and roundings as the block does them. */
static void mexit_hy_spin_phys(Fighter_GObj* fgobj)
{
    Fighter* fp = GET_FIGHTER(fgobj);
    MexgrHySpin* sv = mexgr_hy_spin(fp);
    Item* ip = GET_ITEM(sv->tornado);
    HSD_JObj* top = fp->parts[ftParts_GetBoneIndex(fp, 0)].joint;
    float t = (float) sv->frame;
    double radius = (double) t * 0x1.999999999999ap-3;
    float angle = t * 0.5F;
    float rot;
    top->translate.x = (float) ((double) cosf(angle) * radius + (double) ip->pos.x);
    top->translate.z = (float) (-radius * (double) sinf(angle));
    top->translate.y = (float) ((double) (ip->pos.y + 6.0F) + (double) t * 0x1.3333333333333p-2);
    rot = ftPartGetRotZ(fp, 0);
    ftPartSetRotY(fp, 0, (float) ((double) rot - (double) t * 0x1.1df4722d4405fp-6));
    lb_8000B1CC(top, NULL, &fp->cur_pos);
}

/* SpinTornado (+9AC): the fighter is caught: placed 6 above the tornado, put into the spin. */
static void mexit_hy_catch(Fighter_GObj* fgobj, Item_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(fgobj);
    Item* ip;
    ftCommon_8007DB58(fgobj);
    mexgr_hy_spin(fp)->tornado = gobj;
    ip = GET_ITEM(gobj);
    fp->cur_pos.x = ip->pos.x;
    fp->cur_pos.y = ip->pos.y + 6.0F;
    fp->cur_pos.z = ip->pos.z;
    ftCommon_8007E2FC(fgobj);
    ftCo_80090780(fgobj);
    mexgr_hy_spin(fp)->frame = 0;
    fp->motion_id = MEXGR_HY_SPIN_MOTION;
    fp->anim_cb = mexit_hy_spin_anim;
    fp->input_cb = NULL;
    fp->phys_cb = mexit_hy_spin_phys;
    fp->coll_cb = NULL;
    fp->hitlag_cb = NULL;
    fp->dmg.armor1 = 1000.0F;
    mu_gr_mex_sfx(2);
}

/* TornadoMainThink (+784), the on_accessory callback of the wait and move states: dust (effect
 * 0x423) every 15 frames of life; every fighter inside the box x - 5 .. x + 5, y .. y + 27 that is
 * not spinning, not intangible (ftColl_8007B868), with x1A6A clear and not asleep is caught. At the
 * end of its life the tornado exits once no fighter spins. */
static void mexit_hy_main_think(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexgrHyTornado* tv = mexgr_hy_vars(ip);
    HSD_GObj* fgobj;
    float left, right, top, bottom;
    if (tv->life % 15 == 0) {
        float zero = 0.0F;
        efSync_Spawn(0x423, gobj, GET_JOBJ(gobj), &zero);
    }
    left = ip->pos.x - 5.0F;
    right = ip->pos.x + 5.0F;
    top = ip->pos.y + 27.0F;
    bottom = ip->pos.y + 0.0F;
    for (fgobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; fgobj != NULL; fgobj = fgobj->next) {
        Fighter* fp = GET_FIGHTER(fgobj);
        float x = fp->cur_pos.x;
        float y = fp->cur_pos.y;
        if (!(x >= left) || !(x <= right) || !(y <= top) || !(y >= bottom)) {
            continue;
        }
        if (fp->motion_id == MEXGR_HY_SPIN_MOTION) {
            continue;
        }
        if (ftColl_8007B868(fgobj) != 0) {
            continue;
        }
        if (fp->x1A6A != 0) {
            continue;
        }
        if (fp->is_sleeping) {
            continue;
        }
        mexit_hy_catch(fgobj, gobj);
    }
    if (--tv->life > 0) {
        return;
    }
    for (fgobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; fgobj != NULL; fgobj = fgobj->next) {
        if (GET_FIGHTER(fgobj)->motion_id == MEXGR_HY_SPIN_MOTION) {
            return;
        }
    }
    mexit_hy_state_exit(gobj);
}

/* GXLink_Tornado (+36C): the item's model, then, with the camera's debug display on (x399_b3,
 * Camera_80030A78) and in pass 2, the catch box as a translucent red strip. */
static void mexit_hy_draw(HSD_GObj* gobj, intptr_t pass)
{
    Item* ip;
    float x0, x1, y0, y1, z;
    it_8026EECC(gobj, pass);
    if (!Camera_80030A78() || pass != 2) {
        return;
    }
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GXSetZCompLoc(GX_FALSE);
    GXSetNumTexGens(0);
    GXSetTevClampMode(0, 0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXLoadPosMtxImm(HSD_CObjGetViewingMtxPtrDirect(HSD_CObjGetCurrent()), GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    ip = GET_ITEM(gobj);
    x0 = ip->pos.x - 5.0F;
    x1 = ip->pos.x + 5.0F;
    y1 = ip->pos.y + 27.0F;
    y0 = ip->pos.y + 0.0F;
    z = 0.0F;
    GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, 5);
    GXPosition3f32(x1, y1, z);
    GXColor4u8(0xFF, 0x80, 0x80, 0x78);
    GXPosition3f32(x0, y1, z);
    GXColor4u8(0xFF, 0x80, 0x80, 0x78);
    GXPosition3f32(x1, y0, z);
    GXColor4u8(0xFF, 0x80, 0x80, 0x78);
    GXPosition3f32(x0, y1, z);
    GXColor4u8(0xFF, 0x80, 0x80, 0x78);
    GXPosition3f32(x0, y0, z);
    GXColor4u8(0xFF, 0x80, 0x80, 0x78);
    HSD_StateInvalidate(-1);
}

/* onSpawn (+40), export 1: scale 1, the spawn state, then its own draw function in link 3. The
 * "li r3, 0" after it goes into a void slot. */
static void mexit_hy_spawned(Item_GObj* gobj)
{
    GET_ITEM(gobj)->scl = 1.0F;
    mexit_hy_state_spawn(gobj);
    HSD_GObjGXLink_8039084C(gobj);
    GObj_SetupGXLink(gobj, mexit_hy_draw, 3, 0);
}

/* OnDestroy (+A0), export 2: the floor may spawn the next one. */
static void mexit_hy_destroyed(Item_GObj* gobj)
{
    MexgrHyTornado* tv = mexgr_hy_vars(GET_ITEM(gobj));
    if (tv->floor == NULL) {
        return;
    }
    mexgr_hy_floor(HSD_GObjGetUserData(tv->floor))->tornado = NULL;
    tv->floor = NULL;
}

/* item_state_table (+0) */
static ItemStateTable mexit_hy_states[4] = {
    { 0, mexit_hy_spawn_anim, mexit_hy_phys_nop, mexit_hy_coll_air },
    { 1, mexit_hy_false, mexit_hy_move_phys, mexit_hy_move_coll },
    { 2, mexit_hy_wait_anim, mexit_hy_phys_nop, mexit_hy_coll_air },
    { 4, mexit_hy_exit_anim, mexit_hy_phys_nop, mexit_hy_false },
};

/* Exports 0, 1, 2. */
static const ItemLogicTable mexit_hy_items[1] = {
    { .states = mexit_hy_states, .spawned = mexit_hy_spawned, .destroyed = mexit_hy_destroyed },
};

const MuGrStage mu_gr_hyrule_castle_64 = {
    .name = "Hyrule Castle 64",
    .file = "/GrOHy.dat",
    .map_gobjs = mexgr_hy_map,
    .on_init = mexgr_hy_init,
    .on_demo_init = mu_gr_demo_nop,
    .on_load = mu_gr_event_nop,
    .on_start = mu_gr_event_nop,
    .callback4 = mu_gr_false,
    .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = mexgr_hy_shadow,
    .items = mexit_hy_items,
    .item_count = 1,
    .flags = MU_GR_READY,
};
