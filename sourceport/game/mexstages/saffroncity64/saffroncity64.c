/* Saffron City 64 (Akaneia 1.0.1 and ACE 2.0.0, the same file: internal 73, external 290), the
 * m-ex grFunction block of GrOSf.dat (323 instructions with symbols, the code past +584 without)
 * and its eight itFunction blocks (stage items 0 to 7: 241, 241, 292, 306, 294, 0, 80 and 115
 * instructions; items 0 and 1 share one block, item 5 has none). Disassembly: `py -3.12 grdis.py
 * ak GrOSf.dat` and `... GrOSf.dat it <n>` in run-source/rel09-stages (out_dis_GrOSf*.txt).
 *
 * Exports 1, 3 and 5; the other slots keep the MxDt row's words (Dream Land 64's). Five map
 * gobjs: the scenery (Sheik's row 0 callbacks), the background, the stage, the building door and
 * the light behind it (created and destroyed by the door). The door waits 900 frames, opens,
 * waits 720 frames or until a fighter stands in front of it, then lets out one of five Pokemon
 * (stage items 0 to 4, never the same one twice in a row) with its cry, and closes once the
 * Pokemon is done. A Pokemon stands in the doorway playing its animation (three of them fire a
 * projectile, stage items 5 to 7, when the animation's script says so); after 30 damage it is
 * knocked away by the hit and the door may close.
 *
 * Calls of `__init_registers+18` (item block 0 +CC, item 7 +90) are MexTK's bp(): a blr, nothing
 * natively (wp1_common.h). The door's switch jumps through a table into a second copy of the
 * frame function at +584 that nothing else calls; the native switch is the same. */
#include "../peachcastle64/wp1_common.h"

#include "../../akaneia/mu_ak_fighter.h"

#include <math.h>
#include <string.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/lbspdisplay.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/psstructs.h>
#include <sysdolphin/baselib/random.h>

/* ---- the stage ---- */

/* The door's state in its Ground union (console gp+E0 .. gp+EC, union +1C .. +28). The Pokemon
 * keep a pointer to it (their ip+DF4) and set `done` through it, so the four words stay
 * together in this order. */
typedef struct MexgrSfDoor {
    int state;   /* +0 gp+E0: 0 wait, 1 open, 2 open and waiting, 3 Pokemon out, 4 close */
    int timer;   /* +4 gp+E4 */
    int done;    /* +8 gp+E8: the Pokemon is done */
    int last;    /* +C gp+EC: the Pokemon let out last, -1 none */
} MexgrSfDoor;

typedef struct MexgrSfDoorState {
    u8 x0_pad[0x1C];
    MexgrSfDoor door;
} MexgrSfDoorState;
_Static_assert(sizeof(MexgrSfDoorState) <= sizeof(((Ground*) 0)->u), "door state fits");

static MexgrSfDoor* mexgr_sf_door(Ground_GObj* gobj)
{
    return &((MexgrSfDoorState*) &((Ground*) HSD_GObjGetUserData(gobj))->u)->door;
}

/* A Pokemon's variables (ip+DD4 on). */
typedef struct MexitSfPoke {
    u8 x0_pad[0x20];
    /* +20 ip+DF4 */ MexgrSfDoor* door;
    /* +24 ip+DF8 */ int hp;           /* damage it takes before it is knocked away */
    /* +28 ip+DFC */ float fall;
    /* +2C ip+E00 */ float xE00;
    /* +30 ip+E04 */ float kb_x;
    /* +34 ip+E08 */ float kb_y;
} MexitSfPoke;
_Static_assert(sizeof(MexitSfPoke) <= sizeof(((Item*) 0)->xDD4_itemVar), "Pokemon vars fit");

static MexitSfPoke* mexit_sf_poke(Item* ip) { return (MexitSfPoke*) &ip->xDD4_itemVar; }

/* The cry of each Pokemon: a sound of the stage's bank, -1 none (+878). */
static const s8 mexgr_sf_cries[5] = { 2, -1, 1, 3, 0 };

/* Stage_Create (+108) */
static void mexgr_sf_stage_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

/* Stage_Update (+158) */
static void mexgr_sf_stage_update(Ground_GObj* gobj)
{
    Ground_UpdateMapColl(gobj);
    lb_800115F4();
}

/* Door_Wait (+298) */
static void mexgr_sf_door_wait(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrSfDoor* d = mexgr_sf_door(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
    d->state = 0;
    d->timer = 900;
}

/* Door_Open (+2DC) */
static void mexgr_sf_door_open(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 1);
    mexgr_sf_door(gobj)->state = 1;
}

/* +688: open, waiting up to 720 frames; the light comes on (map gobj 4). */
static void mexgr_sf_door_open_wait(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrSfDoor* d = mexgr_sf_door(gobj);
    grAnime_801C8138(gobj, gp->map_id, 2);
    d->state = 2;
    d->timer = 720;
    mu_grTSeak_80223908(4);
}

/* +6E4: a Pokemon comes out at stage point 0x20, facing left, with its cry. */
static void mexgr_sf_door_poke(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrSfDoor* d = mexgr_sf_door(gobj);
    SpawnItem spawn;
    Vec3 pos;
    Item_GObj* poke;
    int pick;
    grAnime_801C8138(gobj, gp->map_id, 3);
    d->state = 3;
    pick = HSD_Randi(5);
    while (pick == d->last) {
        pick = HSD_Randi(5);
    }
    d->last = pick;
    Ground_801C2D24(0x20, &pos);
    spawn.facing_dir = -1.0F;
    spawn.x0_parent_gobj = NULL;
    spawn.x4_parent_gobj2 = NULL;
    spawn.kind = mu_gr_item_kind(pick);
    spawn.x3C_damage = 0;
    spawn.pos = pos;
    spawn.prev_pos = pos;
    spawn.vel.x = spawn.vel.y = spawn.vel.z = 0.0F;
    spawn.hold_kind = 8;
    spawn.x10 = 0;
    spawn.x3E = 0;
    spawn.x40 = 0;
    spawn.x44_flag.b0 = 0;
    spawn.x48_ground_or_air = 0;
    poke = mu_ak_item_create(&spawn);
    /* The block stores into the new item without a NULL check. */
    mexit_sf_poke(GET_ITEM(poke))->door = d;
    d->done = 0;
    if (mexgr_sf_cries[pick] != -1) {
        mu_gr_mex_sfx(mexgr_sf_cries[pick]);
    }
}

/* +834 */
static void mexgr_sf_door_close(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 4);
    mexgr_sf_door(gobj)->state = 4;
}

/* The current animation of the door's model has reached its end frame (curr >= end). */
static bool mexgr_sf_anim_done(Ground_GObj* gobj)
{
    HSD_AObj* aobj = mexgr_wp1_first_aobj(GET_JOBJ(gobj));
    return aobj->curr_frame >= aobj->end_frame;
}

/* Door_WaitThink (+32C) */
static void mexgr_sf_door_wait_think(Ground_GObj* gobj)
{
    MexgrSfDoor* d = mexgr_sf_door(gobj);
    if (--d->timer > 0) {
        return;
    }
    mexgr_sf_door_open(gobj);
}

/* Door_OpenThink (+364) */
static void mexgr_sf_door_open_think(Ground_GObj* gobj)
{
    if (mexgr_sf_anim_done(gobj)) {
        mexgr_sf_door_open_wait(gobj);
    }
}

/* Door_OpenWaitThink (+3B8): a fighter in front of the door (x 15 .. 30, y -2 .. 20, times the
 * stage scale Ground_801C0498) lets the Pokemon out at once; else the timer does. */
static void mexgr_sf_door_open_wait_think(Ground_GObj* gobj)
{
    MexgrSfDoor* d = mexgr_sf_door(gobj);
    HSD_GObj* fgobj;
    for (fgobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; fgobj != NULL; fgobj = fgobj->next) {
        Fighter* fp = HSD_GObjGetUserData(fgobj);
        float s = Ground_801C0498();
        float x = fp->cur_pos.x;
        float y;
        if (!(x <= s * 30.0F) || !(x >= s * 15.0F)) {
            continue;
        }
        y = fp->cur_pos.y;
        if (!(y <= s * 20.0F) || !(y >= s * -2.0F)) {
            continue;
        }
        d->timer--;
        mexgr_sf_door_poke(gobj);
        return;
    }
    if (--d->timer > 0) {
        return;
    }
    mexgr_sf_door_poke(gobj);
}

/* Door_OpenPokeThink (+4F8) */
static void mexgr_sf_door_poke_think(Ground_GObj* gobj)
{
    if (mexgr_sf_door(gobj)->done == 1) {
        mexgr_sf_door_close(gobj);
    }
}

/* Door_CloseThink (+528): closed, the light goes and the door waits again. */
static void mexgr_sf_door_close_think(Ground_GObj* gobj)
{
    if (mexgr_sf_anim_done(gobj)) {
        Ground_801C4A08(Ground_GetMapGObj(4));
        mexgr_sf_door_wait(gobj);
    }
}

/* Door_Create (+17C) */
static void mexgr_sf_door_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    mexgr_sf_door(gobj)->last = -1;
    mexgr_sf_door_wait(gobj);
    Ground_801C10B8(gobj, mexgr_sf_door_open);
}

/* Door_Update (+1DC) */
static void mexgr_sf_door_update(Ground_GObj* gobj)
{
    switch ((unsigned int) mexgr_sf_door(gobj)->state) {
    case 0: mexgr_sf_door_wait_think(gobj); break;
    case 1: mexgr_sf_door_open_think(gobj); break;
    case 2: mexgr_sf_door_open_wait_think(gobj); break;
    case 3: mexgr_sf_door_poke_think(gobj); break;
    case 4: mexgr_sf_door_close_think(gobj); break;
    default: break;
    }
    Ground_UpdateMapColl(gobj);
}

/* Light_Create (+268) */
static void mexgr_sf_light_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

/* map_gobjs (+0): rows 0 and 1 take Sheik's retail words, rows 2 to 4 her OnUnk and OnDeletion. */
static StageCallbacks mexgr_sf_map[5] = {
    { mexgr_wp1_tseak_anim, mexgr_wp1_tseak_false, mexgr_wp1_tseak_nop, mexgr_wp1_tseak_nop, 0 },
    { mexgr_wp1_background_create, mexgr_wp1_tseak_false, mexgr_wp1_tseak_coll, mexgr_wp1_tseak_nop, 0 },
    { mexgr_sf_stage_create, mexgr_wp1_tseak_false, mexgr_sf_stage_update, mexgr_wp1_tseak_nop, 0xC0000000 },
    { mexgr_sf_door_create, mexgr_wp1_tseak_false, mexgr_sf_door_update, mexgr_wp1_tseak_nop, 0 },
    { mexgr_sf_light_create, mexgr_wp1_tseak_false, mu_gr_gobj_nop, mexgr_wp1_tseak_nop, 0 },
};

/* onInit (+64) */
static void mexgr_sf_init(void)
{
    mexgr_wp1_init(4);
}

/* ---- stage items 0 to 4: the Pokemon ---- */

static float mexit_sf_f32(u32 bits)
{
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

/* MainEnter */
static void mexit_sf_main_enter(Item_GObj* gobj)
{
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
}

static void mexit_sf_nop(Item_GObj* gobj) { (void) gobj; }

/* Item 3's touch callback (+504): the fighter it touches gets Fighter_8006CF5C(fp, 5). */
static void mexit_sf_touch(HSD_GObj* fighter, HSD_GObj* item)
{
    (void) item;
    Fighter_8006CF5C(HSD_GObjGetUserData(fighter), 5);
}

/* Item 3's MainEnter (+2FC): the same state, plus the touch callback (it_80274F28). */
static void mexit_sf_main_enter3(Item_GObj* gobj)
{
    Item_80268E5C(gobj, 0, ITEM_ANIM_UPDATE);
    it_80274F28(GET_ITEM(gobj), 1, mexit_sf_nop, mexit_sf_touch);
}

/* onSpawn: standing, 30 damage to go. "li r3, 0" after it goes into a void slot. */
static void mexit_sf_spawned(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    mexit_sf_main_enter(gobj);
    mexit_sf_poke(ip)->hp = 30;
    ip->xC9C = 0;
}

static void mexit_sf_spawned3(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    mexit_sf_main_enter3(gobj);
    mexit_sf_poke(ip)->hp = 30;
    ip->xC9C = 0;
}

/* FlyEnter: knocked away along the hit's angle (361: 45 degrees) at knockback * x100, upside
 * down (rotate.x = pi), its hurtbox off; the door is told. */
static void mexit_sf_fly_enter(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitSfPoke* v = mexit_sf_poke(ip);
    float kb = ip->xCC8_knockback;
    float scale = p_ftCommonData->x100;
    float angle = ip->xCAC_angle == 361 ? 45.0F : (float) ip->xCAC_angle;
    float c, s;
    angle = (float) ((double) angle * 0x1.1df4722d4405fp-6);
    c = cosf(angle);
    s = sinf(angle);
    v->kb_x = kb * c * scale;
    v->kb_y = kb * s * scale;
    v->fall = 0.0F;
    v->xE00 = 0.0F;
    GET_JOBJ(gobj)->rotate.x = 3.1415927F;
    ip->xACC_itemHurtbox[0].state = (HurtCapsuleState) 0;
    Item_80268E5C(gobj, 1, ITEM_ANIM_UPDATE);
    v->door->done = 1;
}

/* onTakeDamage, export 7: away once the damage taken reaches the threshold. */
static bool mexit_sf_dmg_received(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (ip->xC9C >= mexit_sf_poke(ip)->hp) {
        mexit_sf_fly_enter(gobj);
    }
    return false;
}

/* Fire a projectile (stage item `index`) from joint `joint` of the model: the block's MainAnim
 * when the animation script sets command variable 0 (xDAC). */
static void mexit_sf_fire(Item_GObj* gobj, int joint, int index, int hold_kind, Vec3 vel)
{
    Item* ip = GET_ITEM(gobj);
    SpawnItem spawn;
    HSD_JObj* at;
    Vec3 pos;
    ip->xDAC_itcmd_var0 = 0;
    lb_80011E24(GET_JOBJ(gobj), &at, joint, -1);
    lb_8000B1CC(at, NULL, &pos);
    spawn.facing_dir = ip->facing_dir;
    spawn.x0_parent_gobj = NULL;
    spawn.x4_parent_gobj2 = NULL;
    spawn.kind = mu_gr_item_kind(index);
    spawn.x3C_damage = 0;
    spawn.pos = pos;
    spawn.prev_pos = pos;
    spawn.vel = vel;
    spawn.hold_kind = hold_kind;
    spawn.x10 = 0;
    spawn.x3E = 0;
    spawn.x40 = 0;
    spawn.x44_flag.b0 = 0;
    spawn.x48_ground_or_air = 1;
    mu_ak_item_create(&spawn);
}

/* MainAnim, the end: the door is told and the item goes when the animation is over. */
static bool mexit_sf_main_end(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_AObj* aobj = mexgr_wp1_first_aobj(GET_JOBJ(gobj));
    if (aobj->curr_frame >= aobj->end_frame) {
        mexit_sf_poke(ip)->door->done = 1;
        return true;
    }
    return false;
}

/* Items 0 and 1 (block 0) */
static bool mexit_sf_main_anim(Item_GObj* gobj)
{
    return mexit_sf_main_end(gobj);
}

/* Item 2 (block 2): fires stage item 7 from joint 3. */
static bool mexit_sf_main_anim2(Item_GObj* gobj)
{
    if (GET_ITEM(gobj)->xDAC_itcmd_var0 == 1) {
        Vec3 still = { 0.0F, 0.0F, 0.0F };
        mexit_sf_fire(gobj, 3, 7, 8, still);
    }
    return mexit_sf_main_end(gobj);
}

/* Item 3 (block 3): throws stage item 5 from joint 6, forward 0.7 (in double) and up 1. */
static bool mexit_sf_main_anim3(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (ip->xDAC_itcmd_var0 == 1) {
        Vec3 vel;
        vel.x = (float) ((double) ip->facing_dir * 0x1.6666666666666p-1);
        vel.y = 1.0F;
        vel.z = 0.0F;
        mexit_sf_fire(gobj, 6, 5, 0, vel);
    }
    return mexit_sf_main_end(gobj);
}

/* Item 4 (block 4): fires stage item 6 from joint 3, with sound 4 of the stage's bank. */
static bool mexit_sf_main_anim4(Item_GObj* gobj)
{
    if (GET_ITEM(gobj)->xDAC_itcmd_var0 == 1) {
        Vec3 still = { 0.0F, 0.0F, 0.0F };
        mexit_sf_fire(gobj, 3, 6, 8, still);
        mu_gr_mex_sfx(4);
    }
    return mexit_sf_main_end(gobj);
}

static bool mexit_sf_false(Item_GObj* gobj)
{
    (void) gobj;
    return false;
}

/* FlyPhys: the knockback decays by the fighters' knockback decay along its own direction; the
 * fall speeds up by 0.06 a frame to 4. */
static void mexit_sf_fly_phys(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    MexitSfPoke* v = mexit_sf_poke(ip);
    float keep = v->xE00;
    float vx = v->kb_x;
    float vy = v->kb_y;
    float decay = p_ftCommonData->x204_knockbackFrameDecay;
    float fall = v->fall - 0.06F;
    float dir;
    if (fall < -4.0F) {
        fall = -4.0F;
    }
    dir = atan2f(vy, vx);
    vx = vx - cosf(dir) * decay;
    vy = vy - sinf(dir) * decay;
    ip->x40_vel.x = vx;
    ip->x40_vel.y = fall + vy;
    v->kb_x = vx;
    v->kb_y = vy;
    v->fall = fall;
    v->xE00 = keep;
}

static ItemStateTable mexit_sf_states[2] = {
    { 0, mexit_sf_main_anim, mexit_sf_nop, mexit_sf_false },
    { 0, mexit_sf_false, mexit_sf_fly_phys, mexit_sf_false },
};
static ItemStateTable mexit_sf_states2[2] = {
    { 0, mexit_sf_main_anim2, mexit_sf_nop, mexit_sf_false },
    { 0, mexit_sf_false, mexit_sf_fly_phys, mexit_sf_false },
};
static ItemStateTable mexit_sf_states3[2] = {
    { 0, mexit_sf_main_anim3, mexit_sf_nop, mexit_sf_false },
    { 0, mexit_sf_false, mexit_sf_fly_phys, mexit_sf_false },
};
static ItemStateTable mexit_sf_states4[2] = {
    { 0, mexit_sf_main_anim4, mexit_sf_nop, mexit_sf_false },
    { 0, mexit_sf_false, mexit_sf_fly_phys, mexit_sf_false },
};

/* ---- stage items 6 and 7: the projectiles ---- */

/* MainAnim of item 6: it_80273130 (the life timer). Item 7's returns whether that is 1. */
static bool mexit_sf_shot_anim6(Item_GObj* gobj) { return it_80273130(gobj); }
static bool mexit_sf_shot_anim7(Item_GObj* gobj) { return it_80273130(gobj) == 1; }

/* MainPhys: the velocity decays by `decay` along its own direction. */
static void mexit_sf_shot_decay(Item* ip, float decay)
{
    float dir = atan2f(ip->x40_vel.y, ip->x40_vel.x);
    ip->x40_vel.x = ip->x40_vel.x - cosf(dir) * decay;
    ip->x40_vel.y = ip->x40_vel.y - sinf(dir) * decay;
}
static void mexit_sf_shot_phys6(Item_GObj* gobj) { mexit_sf_shot_decay(GET_ITEM(gobj), 0.0F); }
static void mexit_sf_shot_phys7(Item_GObj* gobj) { mexit_sf_shot_decay(GET_ITEM(gobj), 0.06F); }

/* Item 6 onSpawn: 30 frames of life, 3.5 ahead. */
static void mexit_sf_shot_spawned6(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    mexit_sf_main_enter(gobj);
    it_80275158(gobj, 30.0F);
    ip->x40_vel.x = ip->facing_dir * 3.5F;
    ip->x40_vel.y = 0.0F;
}

/* Item 7 onSpawn: 25 frames of life, 2.7 along an angle of 2.7 to 3.0 (times the facing), and a
 * sparkle (effect 0x44E + 0 .. 3) at twice its size. */
static void mexit_sf_shot_spawned7(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    float angle;
    float one = 1.0F;
    HSD_Generator* sparkle;
    mexit_sf_main_enter(gobj);
    it_80275158(gobj, 25.0F);
    angle = (HSD_Randf() * mexit_sf_f32(0x3E999998u) + 2.7F) * ip->facing_dir;
    ip->x40_vel.x = cosf(angle) * 2.7F;
    ip->x40_vel.y = sinf(angle) * 2.7F;
    sparkle = efSync_Spawn(0x44E + HSD_Randi(4), gobj, GET_JOBJ(gobj), &ip->facing_dir, &one);
    /* The block scales it without a NULL check. */
    sparkle->appsrt->scale.x = 2.0F;
    sparkle->appsrt->scale.y = 2.0F;
    sparkle->appsrt->scale.z = 2.0F;
}

static ItemStateTable mexit_sf_shot_states6[1] = {
    { 0, mexit_sf_shot_anim6, mexit_sf_shot_phys6, mexit_sf_false },
};
static ItemStateTable mexit_sf_shot_states7[1] = {
    { 0, mexit_sf_shot_anim7, mexit_sf_shot_phys7, mexit_sf_false },
};

/* The eight blocks in the row's item order. Exports: 0, 1, 2 (a lone "li r3, 0": a no-op), 7 for
 * items 0 to 4 (item 3 has no 2); 0, 1 for items 6 and 7; item 5 none. */
static const ItemLogicTable mexit_sf_items[8] = {
    { .states = mexit_sf_states, .spawned = mexit_sf_spawned, .destroyed = mexit_sf_nop,
      .dmg_received = mexit_sf_dmg_received },
    { .states = mexit_sf_states, .spawned = mexit_sf_spawned, .destroyed = mexit_sf_nop,
      .dmg_received = mexit_sf_dmg_received },
    { .states = mexit_sf_states2, .spawned = mexit_sf_spawned, .destroyed = mexit_sf_nop,
      .dmg_received = mexit_sf_dmg_received },
    { .states = mexit_sf_states3, .spawned = mexit_sf_spawned3,
      .dmg_received = mexit_sf_dmg_received },
    { .states = mexit_sf_states4, .spawned = mexit_sf_spawned, .destroyed = mexit_sf_nop,
      .dmg_received = mexit_sf_dmg_received },
    { 0 },
    { .states = mexit_sf_shot_states6, .spawned = mexit_sf_shot_spawned6 },
    { .states = mexit_sf_shot_states7, .spawned = mexit_sf_shot_spawned7 },
};

const MuGrStage mu_gr_saffron_city_64 = {
    .name = "Saffron City 64",
    .file = "/GrOSf.dat",
    .map_gobjs = mexgr_sf_map,
    .on_init = mexgr_sf_init,
    .on_load = mu_gr_event_nop,
    .items = mexit_sf_items,
    .item_count = 8,
    .flags = MU_GR_READY,
};
