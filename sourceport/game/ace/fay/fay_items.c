/* PlFy.dat: seven itFunction blocks and ftFunction +39B8/+3AE8/+3D1C/+3E74/+4244.
 * All item exports are native C. MxDt supplies no default callbacks for these articles. */
#include "fay.h"

#include <math.h>

#include <melee/db/db.h>
#include <melee/ef/eflib.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/inlines.h>
#include <melee/gr/ground.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_2725.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/iteffect.h>
#include <melee/it/itmaplib.h>
#include <melee/it/kinds/inlines.h>
#include <melee/lb/lbvector.h>
#include <melee/mp/mpcoll.h>
#include <melee/mp/types.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/controller.h>

static ItemStateTable itFy_Shot_States[];
static ItemStateTable itFy_Gun_States[];
static ItemStateTable itFy_Gun2_States[];
static ItemStateTable itFy_Shot2_States[];
static ItemStateTable itFy_Blade_States[];
static ItemStateTable itFy_Wave_States[];

/* The base spawn is populated in the same field order as the fighter's five routines. */
static HSD_GObj* itFy_Create(HSD_GObj* owner, Vec3* pos, Vec3* vel, ItemKind kind,
                            float facing, bool parent, bool collision, bool centered)
{
    SpawnItem spawn = { 0 };
    ftLib_80086990(owner, &spawn.pos);
    spawn.x0_parent_gobj = parent ? owner : NULL;
    spawn.x4_parent_gobj2 = parent ? owner : NULL;
    spawn.kind = kind;
    /* Blade and slash leave this console stack word unspecified. The public
     * Item_80268B18 prefunction overwrites it through Item_802674AC before allocation;
     * all seven added article kinds become hold kind 8. The other callers also store 8. */
    if (!centered && collision) {
        spawn.hold_kind = 8;
    }
    spawn.prev_pos = *pos;
    if (centered) {
        spawn.pos = *pos;
        spawn.pos.z = spawn.prev_pos.z = 0.0f;
    }
    if (vel != NULL) {
        spawn.vel = *vel;
    }
    spawn.facing_dir = facing;
    spawn.x44_flag.b0 = collision;
    return Item_80268B18(&spawn);
}

/* +39B8: both guns. Suppress their destroy effect and sound, attach to bone 0x43. */
void itFy_Held_Spawn(HSD_GObj* owner, Vec3* pos, ItemKind kind, s32 state, float facing)
{
    HSD_GObj* gobj = itFy_Create(owner, pos, NULL, kind, facing, true, true, false);
    Item* ip = GET_ITEM(gobj);
    itFy_IV(ip)->owner = owner;
    ip->xCC_item_attr->destroy_gfx = -1;
    ip->xCC_item_attr->destroy_sfx = -1;
    Item_80268E5C(gobj, state, 2);
    Item_8026AB54(gobj, owner, 0x43);
    db_80225DD8(gobj, owner);
    it_802750F8(gobj);
}

/* +3AE8: two shot modes. */
void itFy_Shot_Spawn(HSD_GObj* owner, Vec3* pos, Vec3* vel, ItemKind kind,
                     s32 lifetime, float facing)
{
    HSD_GObj* gobj = itFy_Create(owner, pos, vel, kind, facing, true, true, false);
    Item* ip = GET_ITEM(gobj);
    itFy_IV(ip)->owner = owner;
    ip->xCC_item_attr->destroy_gfx = -1;
    ip->xCC_item_attr->destroy_sfx = -1;
    it_80275158(gobj, (float) lifetime);
    Item_80268E5C(gobj, 0, 2);
    db_80225DD8(gobj, owner);
    it_802750F8(gobj);
}

/* +3D1C: no parent in the spawn, but a saved owner in both item variable slots. */
void itFy_Slash_Spawn(HSD_GObj* owner, Vec3* pos, Vec3* vel, s32 lifetime,
                      float facing, float angle)
{
    HSD_GObj* gobj = itFy_Create(owner, pos, vel,
                                mu_ak_item_kind(GET_FIGHTER(owner)->kind, ftFy_Article_Slash),
                                facing, false, false, false);
    Item* ip = GET_ITEM(gobj);
    itFy_IV(ip)->owner = owner;
    itFy_IV(ip)->owner2 = owner;
    itFy_IV(ip)->angle = angle;
    it_80275158(gobj, (float) lifetime);
    Item_80268E5C(gobj, 0, 2);
    db_80225DD8(gobj, owner);
    it_802750F8(gobj);
}

/* +3E74: the blade's initial and previous positions are the passed x/y with z zero. */
void itFy_Blade_Spawn(HSD_GObj* owner, Vec3* pos, s32 state, float facing)
{
    HSD_GObj* gobj = itFy_Create(owner, pos, NULL,
                                mu_ak_item_kind(GET_FIGHTER(owner)->kind, ftFy_Article_Blade),
                                facing, true, false, true);
    Item* ip = GET_ITEM(gobj);
    itFy_IV(ip)->owner = owner;
    ip->xCC_item_attr->destroy_gfx = 0;
    ip->xCC_item_attr->destroy_sfx = 0;
    Item_80268E5C(gobj, state, 0);
    Item_8026AB54(gobj, owner, 0x43);
    db_80225DD8(gobj, owner);
    it_802750F8(gobj);
}

/* +4244: unlike the shots, no parent in the spawn and no destroy-attribute writes. */
void itFy_Wave_Spawn(HSD_GObj* owner, Vec3* pos, s32 lifetime, float facing)
{
    Vec3 vel = { GET_FIGHTER(owner)->facing_dir * 2.0f, 0.0f, 0.0f };
    HSD_GObj* gobj = itFy_Create(owner, pos, &vel,
                                mu_ak_item_kind(GET_FIGHTER(owner)->kind, ftFy_Article_Wave),
                                facing, false, true, false);
    Item* ip = GET_ITEM(gobj);
    itFy_IV(ip)->owner = owner;
    ftFy_FV(GET_FIGHTER(owner))->s2_shot = gobj;
    it_80275158(gobj, (float) lifetime);
    Item_80268E5C(gobj, 0, 2);
    db_80225DD8(gobj, owner);
    it_802750F8(gobj);
}

static bool itFy_True(HSD_GObj* gobj) { return true; }
static bool itFy_Reflect(HSD_GObj* gobj)
{
    it_80273030(gobj);
    return false;
}

static void itFy_Shot_OnSpawn(HSD_GObj* gobj)
{
    GET_ITEM(gobj)->xBC_itemStateContainer = itFy_Shot_States;
}
static void itFy_Shot2_OnSpawn(HSD_GObj* gobj)
{
    GET_ITEM(gobj)->xBC_itemStateContainer = itFy_Shot2_States;
}
static void itFy_Gun_OnSpawn(HSD_GObj* gobj)
{
    GET_ITEM(gobj)->xBC_itemStateContainer = itFy_Gun_States;
}
static void itFy_Gun2_OnSpawn(HSD_GObj* gobj)
{
    GET_ITEM(gobj)->xBC_itemStateContainer = itFy_Gun2_States;
}
static void itFy_Blade_OnSpawn(HSD_GObj* gobj)
{
    GET_ITEM(gobj)->xBC_itemStateContainer = itFy_Blade_States;
}
static void itFy_Wave_OnSpawn(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ip->xBC_itemStateContainer = itFy_Wave_States;
    ip->xDBC_itcmd_var4_word = 0;
    itFy_IV(ip)->angle = 0.0f;
}

static bool itFy_CountLifetime(Item* ip)
{
    ip->xD44_lifeTimer -= 1.0f;
    return ip->xD44_lifeTimer <= 0.0f;
}

/* it0 +58: model scale is assigned every frame without dirtying its matrix. */
static bool itFy_Shot_Anim(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* joint = it_80272CC0(gobj, 0);
    bool done = itFy_CountLifetime(ip);
    joint->scale.z = 0.7f;
    joint->scale.x = 4.5f;
    joint->scale.y = 4.5f;
    return done;
}
static bool itFy_Shot2_Anim(HSD_GObj* gobj)
{
    return itFy_CountLifetime(GET_ITEM(gobj));
}

/* it0 +120 and it3 +D4: copy/reflection and moving-surface calls remain, but the file
 * discards both accumulated vectors. No velocity is assigned by these routines. */
static bool itFy_MapTouch(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    CollData* coll = &ip->x378_itemColl;
    Vec3 reflected = { 0 }, moving = { 0 }, step;
    bool hit = false;
#define FY_SURFACE(mask, member, get_speed) \
    if ((coll->env_flags & (mask)) && \
        MU_FMADDS(ip->x40_vel.x, coll->member.normal.x, \
             ip->x40_vel.y * coll->member.normal.y) < 0.0f) { \
        step = ip->x40_vel; \
        lbVector_Mirror(&step, &coll->member.normal); \
        lbVector_Add_xy(&reflected, &step); \
        if (get_speed(coll, &step)) { lbVector_Add_xy(&moving, &step); } \
        hit = true; \
    }
    FY_SURFACE(Collide_LeftWallMask, left_facing_wall, mpCollGetSpeedLeftWall)
    FY_SURFACE(Collide_RightWallMask, right_facing_wall, mpCollGetSpeedRightWall)
    FY_SURFACE(Collide_CeilingMask, ceiling, mpCollGetSpeedCeiling)
    FY_SURFACE(Collide_FloorMask, floor, mpCollGetSpeedFloor)
#undef FY_SURFACE
    return hit;
}

static bool itFy_Shot_Coll(HSD_GObj* gobj)
{
    it_8026D9A0(gobj);
    return itFy_MapTouch(gobj);
}

/* it1 +34/+98. The ground gun's 38-frame life differs from the air gun's 30. */
static bool itFy_Gun_Check(HSD_GObj* gobj, bool air)
{
    Fighter* fp = GET_FIGHTER(itFy_IV(GET_ITEM(gobj))->owner);
    if (fp->motion_id == (air ? ftFy_MS_SpecialAirN : ftFy_MS_SpecialN)) {
        return fp->cur_anim_frame >= (air ? 30.0f : 38.0f);
    }
    if (fp->motion_id == (air ? ftFy_MS_SpecialAirNSwap : ftFy_MS_SpecialNSwap)) {
        return fp->cur_anim_frame >= 6.0f;
    }
    return true;
}
static bool itFy_Gun_Anim(HSD_GObj* gobj) { return itFy_Gun_Check(gobj, false); }
static bool itFy_GunAir_Anim(HSD_GObj* gobj) { return itFy_Gun_Check(gobj, true); }

/* it2 +34: the initial test accepts states 342 and 344; either swap starts a 6-frame fade. */
static bool itFy_Gun2_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(itFy_IV(GET_ITEM(gobj))->owner);
    if (fp->motion_id == ftFy_MS_SpecialN2 || fp->motion_id == ftFy_MS_SpecialAirN2) {
        return fp->cur_anim_frame >= 55.0f;
    }
    if (fp->motion_id == ftFy_MS_SpecialNSwap || fp->motion_id == ftFy_MS_SpecialAirNSwap) {
        Item_80268E5C(gobj, 1, 0);
        it_80275158(gobj, 6.0f);
        Item_802694CC(gobj);
        return false;
    }
    return true;
}
static bool itFy_Gun2Fade_Anim(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ip->xD44_lifeTimer -= 1.0f;
    efLib_DestroyAll(gobj);
    return ip->xD44_lifeTimer <= 0.0f;
}

/* it4 +44: bit masks are those in the file, including both side-special sequences. */
static bool itFy_Blade_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(itFy_IV(GET_ITEM(gobj))->owner);
    unsigned idx = (unsigned) (fp->motion_id - ftFy_MS_SpecialS);
    if (idx <= 24) {
        u32 bit = 1u << idx;
        if (bit & 0x00D80003u) {
            return false;
        }
        if (bit & 0x01200000u) {
            return fp->cur_anim_frame > 15.0f;
        }
    }
    return true;
}
static bool itFy_BladeAerial_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(itFy_IV(GET_ITEM(gobj))->owner);
    if (fp->motion_id == ftCo_MS_AttackAirHi) {
        return fp->cur_anim_frame > 36.0f;
    }
    if (fp->motion_id == ftCo_MS_AttackAirLw) {
        return fp->cur_anim_frame > 34.0f;
    }
    return true;
}
static bool itFy_BladeBack_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(itFy_IV(GET_ITEM(gobj))->owner);
    return fp->motion_id != ftCo_MS_AttackAirB || fp->cur_anim_frame > 25.0f;
}

/* it5 +288 and it6 +36C share the two effect calls. */
static void itFy_Impact(HSD_GObj* gobj)
{
    Vec3 pos = { 0 }, rot = { 0 };
    Ground_801C53EC(0x73);
    it_80278800(gobj, 0x43, 0, &pos, &rot, 0, 0.0f);
    it_80278800(gobj, 0x513, 0, &pos, &rot, 0, 0.0f);
}

static void itFy_Slash_Fade(HSD_GObj* gobj)
{
    Item_80268E5C(gobj, 1, 2);
    it_80275158(gobj, 2.0f);
    Item_802694CC(gobj);
}
static bool itFy_Slash_Anim(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ip->xD44_lifeTimer -= 1.0f;
    if (ip->xD44_lifeTimer <= 2.0f) {
        itFy_Slash_Fade(gobj);
    }
    return false;
}
static void itFy_Slash_Phys(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    it_80272CC0(gobj, 0)->rotate.x = itFy_IV(ip)->angle;
}
static bool itFy_Slash_Coll(HSD_GObj* gobj)
{
    it_8026D9A0(gobj);
    /* it5 +31C..+43C has the same contact test, without the unused mirror calls. */
    Item* ip = GET_ITEM(gobj);
    CollData* c = &ip->x378_itemColl;
    bool hit = false;
#define FY_DOT(member) \
    MU_FMADDS(ip->x40_vel.x, c->member.normal.x, ip->x40_vel.y * c->member.normal.y)
    /* it5 +350/+38C tests LT; +3BC/+418 returns on GE, so unordered ceiling
     * and floor dots count as contact too. */
    hit = ((c->env_flags & Collide_LeftWallMask) && FY_DOT(left_facing_wall) < 0.0f) ||
          ((c->env_flags & Collide_RightWallMask) && FY_DOT(right_facing_wall) < 0.0f) ||
          ((c->env_flags & Collide_CeilingMask) && !(FY_DOT(ceiling) >= 0.0f)) ||
          ((c->env_flags & Collide_FloorMask) && !(FY_DOT(floor) >= 0.0f));
#undef FY_DOT
    if (hit) {
        itFy_Slash_Fade(gobj);
    }
    return false;
}
static bool itFy_Slash_Hit(HSD_GObj* gobj)
{
    itFy_Impact(gobj);
    return true;
}
static bool itFy_Slash_Shield(HSD_GObj* gobj)
{
    itFy_Impact(gobj);
    return false;
}
static bool itFy_SlashFade_Anim(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    if (ip->xDB0_itcmd_var1 != 1) {
        ip->xDB0_itcmd_var1 = 1;
        itFy_Impact(gobj);
    }
    return itFy_CountLifetime(ip);
}
static void itFy_SlashFade_Phys(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ip->x40_vel.x = ip->x40_vel.y = 0.0f;
}

static void itFy_Wave_EndOwner(HSD_GObj* gobj)
{
    HSD_GObj* owner = itFy_IV(GET_ITEM(gobj))->owner;
    Fighter* fp = GET_FIGHTER(owner);
    Fighter_ChangeMotionState(owner, fp->ground_or_air == GA_Air
                                       ? ftFy_MS_SpecialAirS2End : ftFy_MS_SpecialS2End,
                              FTFY_MF_SWAP, 0.0f, 1.0f, 0.0f, NULL);
}
static bool itFy_Wave_Hit(HSD_GObj* gobj)
{
    itFy_Impact(gobj);
    itFy_Wave_EndOwner(gobj);
    return true;
}
static void itFy_Wave_Fade(HSD_GObj* gobj)
{
    it_80275158(gobj, 4.0f);
    Item_80268E5C(gobj, 1, 2);
}

/* it6 +468: aim changes only outside the stick dead zone; the horizontal direction uses
 * the owner's live facing direction and y velocity has the file's negative sine sign. */
static void itFy_Wave_Steer(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = GET_FIGHTER(itFy_IV(ip)->owner);
    HSD_JObj* joint = it_80272CC0(gobj, 0);
    float angle = itFy_IV(ip)->angle;
    if (fp->input.lstick[0].y > 0.3) {
        angle = (float) ((double) angle - 0.008);
    } else if (fp->input.lstick[0].y < -0.3) {
        angle = (float) ((double) angle + 0.008);
    } else {
        joint->rotate.x = angle;
        return;
    }
    itFy_IV(ip)->angle = angle;
    ip->x40_vel.y = sinf(angle) * -2.0f;
    ip->x40_vel.x = (fp->facing_dir + fp->facing_dir) * cosf(angle);
    joint->rotate.x = angle;
}

static bool itFy_Wave_Anim(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* fp = GET_FIGHTER(itFy_IV(ip)->owner);
    (void) it_80272CC0(gobj, 0);
    itFy_Wave_Steer(gobj);
    ip->xD44_lifeTimer -= 1.0f;
    if ((unsigned) (fp->motion_id - ftFy_MS_SpecialS2) > 5) {
        itFy_Wave_Fade(gobj);
    }
    if ((fp->ground_or_air == GA_Ground || fp->ground_or_air == GA_Air) &&
        ip->xDAC_itcmd_var0 == 1 && !(fp->input.held_buttons[0] & HSD_PAD_B)) {
        itFy_Wave_Fade(gobj);
    }
    if (ip->xD44_lifeTimer <= 0.0f) {
        itFy_Wave_Fade(gobj);
        itFy_Wave_EndOwner(gobj);
    }
    return false;
}
static bool itFy_Wave_Coll(HSD_GObj* gobj)
{
    it_8026D9A0(gobj);
    if (itFy_MapTouch(gobj)) {
        itFy_Wave_EndOwner(gobj);
        itFy_Wave_Fade(gobj);
    }
    return false;
}
static bool itFy_WaveFade_Anim(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* joint = it_80272CC0(gobj, 0);
    bool done = itFy_CountLifetime(ip);
    ip->x40_vel.x = ip->x40_vel.y = ip->x40_vel.z = 0.0f;
    joint->scale.x = joint->scale.y = joint->scale.z = 0.0f;
    if (done) {
        ftFy_FV(GET_FIGHTER(itFy_IV(ip)->owner))->s2_shot = NULL;
    }
    return done;
}

static ItemStateTable itFy_Shot_States[] = { { 0, itFy_Shot_Anim, NULL, itFy_Shot_Coll } };
static ItemStateTable itFy_Gun_States[] = {
    { 0, itFy_Gun_Anim, NULL, NULL }, { 1, itFy_GunAir_Anim, NULL, NULL }
};
static ItemStateTable itFy_Gun2_States[] = {
    { 0, itFy_Gun2_Anim, NULL, NULL }, { 1, itFy_Gun2Fade_Anim, NULL, NULL }
};
static ItemStateTable itFy_Shot2_States[] = { { 0, itFy_Shot2_Anim, NULL, itFy_Shot_Coll } };
static ItemStateTable itFy_Blade_States[] = {
    { 0, itFy_Blade_Anim, NULL, NULL }, { 1, itFy_BladeAerial_Anim, NULL, NULL },
    { 2, itFy_BladeBack_Anim, NULL, NULL }
};
static ItemStateTable itFy_Slash_States[] = {
    { 0, itFy_Slash_Anim, itFy_Slash_Phys, itFy_Slash_Coll },
    { 1, itFy_SlashFade_Anim, itFy_SlashFade_Phys, NULL }
};
static ItemStateTable itFy_Wave_States[] = {
    { 0, itFy_Wave_Anim, NULL, itFy_Wave_Coll },
    { 1, itFy_WaveFade_Anim, NULL, itFy_Wave_Coll }
};

const ItemLogicTable itFy_Articles[ftFy_Article_Count] = {
    { .states = itFy_Shot_States, .spawned = itFy_Shot_OnSpawn,
      .dmg_dealt = itFy_True, .reflected = itFy_Reflect, .hit_shield = itFy_True },
    { .states = itFy_Gun_States, .spawned = itFy_Gun_OnSpawn },
    { .states = itFy_Gun2_States, .spawned = itFy_Gun2_OnSpawn },
    { .states = itFy_Shot2_States, .spawned = itFy_Shot2_OnSpawn,
      .dmg_dealt = itFy_True, .reflected = itFy_Reflect, .hit_shield = itFy_True },
    { .states = itFy_Blade_States, .spawned = itFy_Blade_OnSpawn },
    { .states = itFy_Slash_States, .dmg_dealt = itFy_Slash_Hit,
      .reflected = itFy_Reflect, .hit_shield = itFy_Slash_Shield },
    { .states = itFy_Wave_States, .spawned = itFy_Wave_OnSpawn,
      .dmg_dealt = itFy_Wave_Hit, .reflected = itFy_Reflect, .hit_shield = itFy_Wave_Hit },
};
