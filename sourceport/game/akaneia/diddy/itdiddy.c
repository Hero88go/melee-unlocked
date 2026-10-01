/* Akaneia's Diddy Kong: his three articles, from PlDd.dat's "itFunction" root.
 *
 *   0 Popgun : held by Special N and the victory pose. Three looks (item states 0 idle,
 *              1 overheating, 2 exploding), no behavior of its own.
 *   1 Peanut : the popgun's projectile. Falls, ticks its lifetime, pops (effect 6002) when it
 *              hits, is hit, clanks, hits a shield or lands.
 *   2 Banana : Special Lw's peel. Lies on the ground (Wait) and trips the first grounded,
 *              trippable opponent standing on it, then flies up and vanishes on its next
 *              landing. Pickable and throwable like an item; destroyed after its third bounce.
 *
 * ftDd_ItemLogic is the m-ex item table in the decomp's ItemLogicTable layout, one row per
 * article, in the order of ftData x48 (see mu_ak_register_article). */
#include "ftdiddy.h"

#include <math.h>

#include <dolphin/gx.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_DamageFall.h>
#include <melee/ft/kinds/ftCommon/ftCo_DownBound.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/ground.h>
#include <melee/it/it_2725.h>
#include <melee/it/itdraw.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/state.h>

/* The peanut's pop, an effect from EfDdData.dat (m-ex effect id). */
#define itDd_GFX_NutHit 6002

/* Item states of each article. */
enum { itDdGun_MS_Idle, itDdGun_MS_Danger, itDdGun_MS_Blow };
enum { itDdNut_MS_Fly };
enum {
    itDdBanana_MS_SpawnThrown, /* tossed by Diddy */
    itDdBanana_MS_Wait,        /* lying on the ground, armed */
    itDdBanana_MS_Fall,
    itDdBanana_MS_Held,
    itDdBanana_MS_Thrown,      /* thrown by whoever picked it up */
    itDdBanana_MS_FlyUp,       /* just tripped someone */
};

/* ------------------------------------------------------------------------------------------ */
/* Popgun                                                                                     */
/* ------------------------------------------------------------------------------------------ */

/* Null_Func: the popgun's callbacks all return 0. */
static bool itDdGun_Null(Item_GObj* gobj)
{
    return false;
}

static void itDdGun_NullPhys(Item_GObj* gobj) {}

static ItemStateTable itDdGun_States[] = {
    { itDdGun_MS_Idle, itDdGun_Null, itDdGun_NullPhys, itDdGun_Null },
    { itDdGun_MS_Danger, itDdGun_Null, itDdGun_NullPhys, itDdGun_Null },
    { itDdGun_MS_Blow, itDdGun_Null, itDdGun_NullPhys, itDdGun_Null },
};

/* ------------------------------------------------------------------------------------------ */
/* Peanut                                                                                     */
/* ------------------------------------------------------------------------------------------ */

/* Pop at the peanut's root joint; every contact destroys it. */
static bool itDdNut_Pop(Item_GObj* gobj)
{
    Vec3 pos;

    lb_8000B1CC(it_80272CC0(gobj, 0), NULL, &pos);
    efSync_Spawn(itDd_GFX_NutHit, gobj, &pos);
    return true;
}

static void itDdNut_OnSpawn(Item_GObj* gobj)
{
    Item_80268E5C(gobj, itDdNut_MS_Fly, ITEM_ANIM_UPDATE);
}

static bool itDdNut_OnReflect(Item_GObj* gobj)
{
    it_80273030(gobj);
    return false;
}

static bool itDdNut_Anim(Item_GObj* gobj)
{
    return it_80273130(gobj);
}

static void itDdNut_Phys(Item_GObj* gobj)
{
    Item_ApplyFallingPhysics(gobj);
}

static bool itDdNut_Coll(Item_GObj* gobj)
{
    if (it_8026DAA8(gobj)) {
        return itDdNut_Pop(gobj);
    }
    return false;
}

static ItemStateTable itDdNut_States[] = {
    { itDdNut_MS_Fly, itDdNut_Anim, itDdNut_Phys, itDdNut_Coll },
};

/* ------------------------------------------------------------------------------------------ */
/* Banana: state changes                                                                      */
/* ------------------------------------------------------------------------------------------ */

static void itDdBanana_Trip_Check(HSD_GObj* gobj);
static void itDdBanana_GX(HSD_GObj* gobj, intptr_t pass);

/* SpawnThrown_Enter (called by Diddy's toss): no longer counted as held for the camera. */
void itDdBanana_SpawnThrown_Enter(Item_GObj* gobj)
{
    GET_ITEM(gobj)->xDCD_flag.b2 = false;
    Item_80268E5C(gobj, itDdBanana_MS_SpawnThrown, ITEM_ANIM_UPDATE);
}

/* Wait_Enter / the landing: stop and lie down. */
static void itDdBanana_Wait_Enter(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    ip->x40_vel.x = 0.0F;
    ip->x40_vel.y = 0.0F;
    it_8026DD5C(gobj);
    Item_80268E5C(gobj, itDdBanana_MS_Wait, ITEM_ANIM_UPDATE);
}

/* Fall_Enter */
static void itDdBanana_Fall_Enter(Item_GObj* gobj)
{
    it_802762BC(GET_ITEM(gobj));
    Item_80268E5C(gobj, itDdBanana_MS_Fall, ITEM_ANIM_UPDATE);
}

/* FlyUp_Enter: tripped someone; the next landing destroys the peel. */
static void itDdBanana_FlyUp_Enter(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    it_802762BC(ip);
    Item_80268E5C(gobj, itDdBanana_MS_FlyUp, ITEM_ANIM_UPDATE);
    ftDd_BananaVarsOf(ip)->flew_up = 1;
}

/* SpawnThrown_OnLand */
static void itDdBanana_SpawnThrown_OnLand(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    if (ftDd_BananaVarsOf(ip)->flew_up == 1) {
        ftDd_BananaVarsOf(ip)->destroy = 1;
        return;
    }
    itDdBanana_Wait_Enter(gobj);
}

/* Thrown_OnLand: bounces, and the third landing (or one after tripping) destroys it. */
static void itDdBanana_Thrown_OnLand(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftDd_BananaVars* vars = ftDd_BananaVarsOf(ip);

    vars->bounces++;
    if (vars->bounces > 2 || vars->flew_up == 1) {
        vars->destroy = 1;
        return;
    }
    itDdBanana_Wait_Enter(gobj);
}

/* ------------------------------------------------------------------------------------------ */
/* Banana: tripping                                                                           */
/* ------------------------------------------------------------------------------------------ */

/* Fighter_CanTrip: the fighter has a MissFoot animation. */
static bool itDdBanana_CanTrip(HSD_GObj* fighter)
{
    Fighter* fp = GET_FIGHTER(fighter);

    return ftData_80085FD4(fp, fp->x1C_actionStateList[ftCo_MS_MissFoot].anim_id)->x8 != 0;
}

/* Trip_CorrectModel (accessory callback): lift the model so its lowest foot/hand joint (the six
 * bones of ftData x44) is not below the fighter's position while tripping. */
static void itDdBanana_Trip_CorrectModel(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* trans = fp->parts[ftParts_GetBoneIndex(fp, FtPart_TransN)].joint;
    be_u16* bones = (be_u16*) DP(fp->ft_data->x44);
    float lowest = 50000.0F;
    int i;

    trans->translate.y = 0.0F;
    for (i = 0; i < 6; i++) {
        Vec3 pos;

        lb_8000B1CC(fp->parts[BEV(bones[i])].joint, NULL, &pos);
        if (pos.y < lowest) {
            lowest = pos.y;
        }
    }
    if (fp->cur_pos.y > lowest) {
        trans->translate.y = fp->x34_scale.y + (fp->cur_pos.y - lowest);
    }
    HSD_JObjSetMtxDirtySub(GET_JOBJ(gobj));
}

/* Trip_Anim: after 14 frames of the MissFoot animation, bounce down face up. */
static void itDdBanana_Trip_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftDd_MV(fp)->trip.frame++;
    if (!(fp->cur_anim_frame >= 14.0F)) {
        return;
    }
    /* The console calls ftCo_8009794C, the decomp's static DownBound entry; ftCo_80097D88 is its
     * public wrapper (for a sandbag it would take the sandbag path instead). */
    ftCo_80097D88(gobj);
    Fighter_ChangeMotionState(gobj, ftCo_MS_DownBoundU,
                              Ft_MF_SkipNametagVis | Ft_MF_KeepColAnimPartHitStatus, 0.0F, 1.0F,
                              0.0F, NULL);
}

static void itDdBanana_Trip_IASA(HSD_GObj* gobj) {}

static void itDdBanana_Trip_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}

static void itDdBanana_Trip_Coll(HSD_GObj* gobj)
{
    if (!ft_80082708(gobj)) {
        ftCo_80090780(gobj);
    }
}

/* Trip_Enter: the fighter slips. Built on DamageLw1 with the MissFoot animation from its 3rd
 * frame and the peel's own callbacks. */
static void itDdBanana_Trip_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    Fighter_ChangeMotionState(gobj, ftCo_MS_DamageLw1, Ft_MF_None, 0.0F, 1.0F, 0.0F, NULL);
    Ground_801C53EC(0x10A);
    ft_800881D8(fp, ((FtSFX*) DP(fp->ft_data->x4C_sfx))->x18, 0x7F, 0x40);
    ftData_80085CD8(fp, fp, fp->x1C_actionStateList[ftCo_MS_MissFoot].anim_id);
    ftAnim_8006EBE8(gobj, 3.0F, 1.0F, 0.0F);
    ftAnim_8006E9B4(gobj);
    fp->self_vel.x = fp->self_vel.x * 0.4;
    fp->gr_vel = fp->gr_vel * 0.4;
    fp->self_vel.y = 0.0F;
    fp->xF0_ground_kb_vel = 0.0F;
    fp->anim_cb = itDdBanana_Trip_Anim;
    fp->input_cb = itDdBanana_Trip_IASA;
    fp->phys_cb = itDdBanana_Trip_Phys;
    fp->coll_cb = itDdBanana_Trip_Coll;
    fp->accessory1_cb = itDdBanana_Trip_CorrectModel;
    ftDd_MV(fp)->trip.frame = 0;
    itDdBanana_Trip_CorrectModel(gobj);
}

/* Trip_Grabbed: nudge the peel up and along, then trip the fighter. */
static void itDdBanana_Trip_Grabbed(HSD_GObj* fighter, Item_GObj* banana)
{
    Fighter* fp = GET_FIGHTER(fighter);
    Item* ip = GET_ITEM(banana);

    ip->x40_vel.y = 1.7F;
    ip->x40_vel.x = fp->facing_dir * 0.5F;
    itDdBanana_Trip_Enter(fighter);
}

/* The fighters a peel can affect (Trip_Check and the debug display share the first tests). */
static bool itDdBanana_IsOpponent(Fighter* owner, Fighter* fp, bool teams, bool friendly_fire)
{
    if (fp->player_idx == owner->player_idx) {
        return false;
    }
    if (teams && !friendly_fire && fp->team == owner->team) {
        return false;
    }
    return true;
}

/* Trip_Check (item proc, priority 13): while lying armed, trip the first opponent standing on
 * the peel (within 5.5 plus half the fighter's trip width across, 1.0 up or down). */
static void itDdBanana_Trip_Check(HSD_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    Fighter* owner;
    bool teams;
    bool friendly_fire;
    HSD_GObj* cur;

    if (ip->xDC8_word.flags.xA || ip->msid != itDdBanana_MS_Wait) {
        return;
    }
    /* The console reads the owner before any test and would fault without one. */
    if (ip->owner == NULL) {
        return;
    }
    owner = GET_FIGHTER(ip->owner);
    teams = gm_8016B168();
    friendly_fire = gm_8016B0D4();

    for (cur = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; cur != NULL; cur = cur->next) {
        Fighter* fp = GET_FIGHTER(cur);
        float x, y, half;
        float base_x, base_y;

        if (!itDdBanana_IsOpponent(owner, fp, teams, friendly_fire)) {
            continue;
        }
        if (ftColl_8007B868(cur) || fp->ground_or_air != GA_Ground || fp->x1A6A != 0 ||
            fp->x1A5C != NULL || fp->victim_gobj != NULL || fp->is_sleeping || fp->x2219_b1 ||
            fp->x222A_b0 || fp->x221D_b6 || fp->stamina_dead || !itDdBanana_CanTrip(cur))
        {
            continue;
        }
        base_x = ip->pos.x + ip->x378_itemColl.desired_ecb.bottom.x;
        half = fp->x2C4.y * 0.5F;
        x = fp->cur_pos.x + fp->x2C4.x;
        if (!(x < base_x + 5.5F + half) || !(x > base_x - 5.5F - half)) {
            continue;
        }
        base_y = ip->pos.y + ip->x378_itemColl.desired_ecb.bottom.y;
        y = fp->cur_pos.y;
        if (!(y < base_y + 1.0F) || !(y > base_y - 1.0F)) {
            continue;
        }
        ftCommon_8007DB58(cur);
        itDdBanana_Trip_Grabbed(cur, gobj);
        itDdBanana_FlyUp_Enter(gobj);
        return;
    }
}

/* Banana_GX: the peel's model, plus (develop mode hitbox display, xDAA b6) its trip area and
 * each candidate's trip width on the translucent pass. */
static void itDdBanana_GX(HSD_GObj* gobj, intptr_t pass)
{
    Item* ip;
    Fighter* owner;
    bool teams;
    bool friendly_fire;
    HSD_GObj* cur;
    float left, right, top, bottom;

    it_8026EECC(gobj, pass);
    ip = GET_ITEM(gobj);
    if (ip->msid != itDdBanana_MS_Wait || !ip->xDAA_flag.b6 || pass != 2) {
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
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXLoadPosMtxImm(HSD_CObjGetViewingMtxPtrDirect(HSD_CObjGetCurrent()), GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);

    right = ip->pos.x + ip->x378_itemColl.desired_ecb.bottom.x;
    top = ip->pos.y + ip->x378_itemColl.desired_ecb.bottom.y;
    left = right - 5.5F;
    bottom = top - 1.0F;
    top = top + 1.0F;
    right = right + 5.5F;

    /* Five vertices, the second corner twice, as on the console. */
    GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, 5);
    GXPosition3f32(right, top, 0.0F);
    GXColor4u8(0xFF, 0x80, 0x80, 0x96);
    GXPosition3f32(left, top, 0.0F);
    GXColor4u8(0xFF, 0x80, 0x80, 0x96);
    GXPosition3f32(right, bottom, 0.0F);
    GXColor4u8(0xFF, 0x80, 0x80, 0x96);
    GXPosition3f32(left, top, 0.0F);
    GXColor4u8(0xFF, 0x80, 0x80, 0x96);
    GXPosition3f32(left, bottom, 0.0F);
    GXColor4u8(0xFF, 0x80, 0x80, 0x96);

    if (ip->owner == NULL) {
        HSD_StateInvalidate(-1);
        return;
    }
    owner = GET_FIGHTER(ip->owner);
    teams = gm_8016B168();
    friendly_fire = gm_8016B0D4();
    for (cur = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; cur != NULL; cur = cur->next) {
        Fighter* fp = GET_FIGHTER(cur);
        float mid, x0, x1;

        if (!itDdBanana_IsOpponent(owner, fp, teams, friendly_fire)) {
            continue;
        }
        if (ftColl_8007B868(cur) || fp->ground_or_air != GA_Ground || fp->x1A6A != 0 ||
            fp->is_sleeping)
        {
            continue;
        }
        HSD_StateInitDirect(0, 2);
        GXLoadPosMtxImm(HSD_CObjGetViewingMtxPtrDirect(HSD_CObjGetCurrent()), GX_PNMTX0);
        GXSetLineWidth(12, GX_TO_ONE);
        mid = fp->cur_pos.x + fp->x2C4.x;
        x1 = fp->x2C4.y * 0.5F + mid;
        x0 = -fp->x2C4.y * 0.5F + mid;
        GXBegin(GX_LINESTRIP, GX_VTXFMT0, 2);
        GXPosition3f32(x0, fp->cur_pos.y, 0.0F);
        GXColor4u8(0xFF, 0xFF, 0x80, 0x96);
        GXPosition3f32(x1, fp->cur_pos.y, 0.0F);
        GXColor4u8(0xFF, 0xFF, 0x80, 0x96);
    }
    HSD_StateInvalidate(-1);
}

/* ------------------------------------------------------------------------------------------ */
/* Banana: states and logic                                                                   */
/* ------------------------------------------------------------------------------------------ */

static bool itDdBanana_TickAnim(Item_GObj* gobj)
{
    return it_80273130(gobj);
}

static void itDdBanana_FallPhys(Item_GObj* gobj)
{
    GET_ITEM(gobj)->xD54_throwNum = 0;
    Item_ApplyFallingPhysics(gobj);
}

static bool itDdBanana_SpawnThrown_Coll(Item_GObj* gobj)
{
    it_8026E414(gobj, itDdBanana_SpawnThrown_OnLand);
    return ftDd_BananaVarsOf(GET_ITEM(gobj))->destroy;
}

static void itDdBanana_Wait_Phys(Item_GObj* gobj) {}

/* Lie along the floor's slope (the root joint's x rotation; the console leaves the matrix to
 * the item's own update). */
static bool itDdBanana_Wait_Coll(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    HSD_JObj* jobj;

    it_8026D62C(gobj, itDdBanana_Fall_Enter);
    jobj = it_80272CC0(gobj, 0);
    jobj->rotate.x = -atan2f(ip->x378_itemColl.floor.normal.x, ip->x378_itemColl.floor.normal.y);
    return false;
}

static bool itDdBanana_Fall_Coll(Item_GObj* gobj)
{
    it_8026E414(gobj, itDdBanana_Wait_Enter);
    return false;
}

static bool itDdBanana_Held_Anim(Item_GObj* gobj)
{
    return false;
}

static void itDdBanana_Held_Phys(Item_GObj* gobj) {}

static bool itDdBanana_Held_Coll(Item_GObj* gobj)
{
    return false;
}

static bool itDdBanana_Thrown_Coll(Item_GObj* gobj)
{
    it_8026E414(gobj, itDdBanana_Thrown_OnLand);
    return ftDd_BananaVarsOf(GET_ITEM(gobj))->destroy;
}

static void itDdBanana_FlyUp_Phys(Item_GObj* gobj)
{
    Item_ApplyFallingPhysics(gobj);
}

static ItemStateTable itDdBanana_States[] = {
    { 0, itDdBanana_TickAnim, itDdBanana_FallPhys, itDdBanana_SpawnThrown_Coll },
    { 1, itDdBanana_TickAnim, itDdBanana_Wait_Phys, itDdBanana_Wait_Coll },
    { 0, itDdBanana_TickAnim, itDdBanana_FallPhys, itDdBanana_Fall_Coll },
    { 2, itDdBanana_Held_Anim, itDdBanana_Held_Phys, itDdBanana_Held_Coll },
    { 0, itDdBanana_TickAnim, itDdBanana_FallPhys, itDdBanana_Thrown_Coll },
    { 3, itDdBanana_TickAnim, itDdBanana_FlyUp_Phys, itDdBanana_Thrown_Coll },
};

/* OnSpawn: 420 frames of life, falling, trip detection and the debug-aware draw. */
static void itDdBanana_OnSpawn(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);
    ftDd_BananaVars* vars = ftDd_BananaVarsOf(ip);

    vars->bounces = 0;
    vars->flew_up = 0;
    vars->destroy = 0;
    it_80275158(gobj, 420.0F);
    it_802762BC(ip);
    Item_80268E5C(gobj, itDdBanana_MS_Fall, ITEM_ANIM_UPDATE);
    ip->xDCD_flag.b2 = true;
    HSD_GObj_SetupProc(gobj, itDdBanana_Trip_Check, 13);
    HSD_GObjGXLink_8039084C(gobj);
    GObj_SetupGXLink(gobj, itDdBanana_GX, 6, 0);
}

/* OnDestroy: free the thrower's one-peel slot. */
static void itDdBanana_OnDestroy(Item_GObj* gobj)
{
    Item* ip = GET_ITEM(gobj);

    ftDd_FV(GET_FIGHTER(ftDd_BananaVarsOf(ip)->thrower))->banana = NULL;
}

static void itDdBanana_OnPickup(Item_GObj* gobj)
{
    Item_80268E5C(gobj, itDdBanana_MS_Held, ITEM_ANIM_UPDATE);
}

static void itDdBanana_OnDrop(Item_GObj* gobj)
{
    it_802762BC(GET_ITEM(gobj));
    Item_80268E5C(gobj, itDdBanana_MS_Fall, ITEM_ANIM_UPDATE);
}

static void itDdBanana_OnThrow(Item_GObj* gobj)
{
    Item_80268E5C(gobj, itDdBanana_MS_Thrown, ITEM_ANIM_UPDATE);
}

/* A peel lying on the ground hits nobody; in flight it bounces off its victim. */
static bool itDdBanana_OnGiveDamage(Item_GObj* gobj)
{
    if (GET_ITEM(gobj)->msid == itDdBanana_MS_Wait) {
        return false;
    }
    itColl_BounceOffVictim(gobj);
    return false;
}

static bool itDdBanana_OnTakeDamage(Item_GObj* gobj)
{
    return true;
}

static bool itDdBanana_OnReflect(Item_GObj* gobj)
{
    return it_80273030(gobj);
}

static bool itDdBanana_OnHitShieldBounce(Item_GObj* gobj)
{
    itColl_BounceOffShield(gobj);
    return false;
}

static bool itDdBanana_OnHitShield(Item_GObj* gobj)
{
    return true;
}

/* ------------------------------------------------------------------------------------------ */
/* The table                                                                                  */
/* ------------------------------------------------------------------------------------------ */

ItemLogicTable ftDd_ItemLogic[3] = {
    {
        /* 0 Popgun */
        itDdGun_States,
    },
    {
        /* 1 Peanut */
        itDdNut_States,
        itDdNut_OnSpawn, /* spawned */
        NULL,            /* destroyed */
        NULL,            /* picked_up */
        NULL,            /* dropped */
        NULL,            /* thrown */
        itDdNut_Pop,     /* dmg_dealt */
        itDdNut_Pop,     /* dmg_received */
        NULL,            /* entered_air */
        itDdNut_OnReflect,
        itDdNut_Pop,     /* clanked */
        NULL,            /* absorbed */
        NULL,            /* shield_bounced */
        itDdNut_Pop,     /* hit_shield */
        NULL,
    },
    {
        /* 2 Banana */
        itDdBanana_States,
        itDdBanana_OnSpawn,
        itDdBanana_OnDestroy,
        itDdBanana_OnPickup,
        itDdBanana_OnDrop,
        itDdBanana_OnThrow,
        itDdBanana_OnGiveDamage,
        itDdBanana_OnTakeDamage,
        NULL, /* entered_air */
        itDdBanana_OnReflect,
        NULL, /* clanked */
        NULL, /* absorbed */
        itDdBanana_OnHitShieldBounce,
        itDdBanana_OnHitShield,
        NULL,
    },
};
