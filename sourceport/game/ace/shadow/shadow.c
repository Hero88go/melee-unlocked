/* ACE's Shadow: load, the events the file exports, the effects, the debug drawing of the homing
 * attack and the state table (m-ex "ftFunction" of PlSh.dat).
 *
 * PlSh.dat exports 25 ftFunction slots: 0 onload, 1 ondeath, 2 onunknown, 3 move_logic, 4 to 11
 * the special entries, 13 to 18 the item events, 21 and 22 the eye textures, 23 onframe, 24
 * onactionstatechange, 25 onrespawn, 26 onmodelrender, 32 enterdoublejump. MxDt sets one
 * default only (slot 3, Sandbag's table, which the export replaces): every other field below is
 * NULL because the slot is empty on the disc too (run-source/rel09-ace-native/shadow/mxdt.txt).
 *
 * The file carries no m-ex CPU helper (no MexCPU code, no spoof word: nothing in the block
 * installs a proc or writes fp->kind), so a CPU Shadow plays the game's generic CPU code. */
#include "shadow.h"

#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/os.h>

#include <math.h>
#include <string.h>

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdrawcommon.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/types.h>
#include <melee/it/it_26B1.h>
#include <melee/it/item.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_013B.h>
#include <melee/lb/types.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/object.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>

_Static_assert(sizeof(ftSh_FighterVars) <= sizeof(((Fighter*) 0)->u),
               "his fighter variables fit the fighter union");
_Static_assert(sizeof(ftSh_MotionVars) <= sizeof(((Fighter*) 0)->mv),
               "his state variables fit the state union");

/* ---- small callbacks the special states install ------------------------------------------- */

/* +3B84: `b efLib_DestroyAll`. Its own address is what the specials store in take_dmg_cb and
 * death2_cb, so it is a function here too. */
void ftSh_DestroyAll(HSD_GObj* gobj)
{
    efLib_DestroyAll(gobj);
}

/* +48AC: the side special's take_dmg_cb and death2_cb: the stored charge is lost when hit. */
void ftSh_SpecialS_OnHit(HSD_GObj* gobj)
{
    ftSh_FV(GET_FIGHTER(gobj))->specials_charge = 0;
    efLib_DestroyAll(gobj);
}

/* +596C: `blr`, the spin dash's deal_dmg_cb (the hit does nothing). */
void ftSh_Nothing(HSD_GObj* gobj) {}

/* ---- load, death, destroy, attributes ------------------------------------------------------ */

static void ftSh_GXLink(HSD_GObj* gobj, intptr_t pass);

/* +0000 (slot 0, onload). In the file's order:
 *   the attributes: 0x154 bytes of ftData->ext_attr copied into the game's buffer fp+2D8
 *     (dat_attrs_backup, 0x424 bytes) and fp+2D4 (dat_attrs) pointed at it; the bytes stay
 *     big-endian and ftSh_DatAttrs reads them as such;
 *   article 0 of ftData->x48_items handed to the item code (MEX_IndexFighterItem, 0x803D7058);
 *   his own GX link (+0D54, the homing attack's debug circles after the normal draw);
 *   four fighter variables cleared (222C, 2234, 2230, 2248);
 *   "PlySonicColor" of the costume's file (MEX_GetData(8)[kind] runtime [costume] +0x14, the
 *     archive) into fp+2234 when the costume has an archive.
 *
 * One native difference, not in the file: the wall jump bit from MxDt's byte for internal 56 (1),
 * as the Toad, Metal Mario and Sonic BM ports do (on the console m-ex code reads the byte). */
static void ftSh_Init_OnLoad(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_FighterVars* fv = ftSh_FV(fp);
    ftSh_DiscSlot* items;
    HSD_Archive* archive;

    memcpy(fp->dat_attrs_backup, DP(fp->ft_data->ext_attr), sizeof(ftSh_DatAttrs));
    fp->dat_attrs = fp->dat_attrs_backup;

    items = (ftSh_DiscSlot*) DISC_GET(void, fp->ft_data->x48_items);
    mu_ak_register_article(fp->kind, DP(items[ftSh_Article_Spring]), ftSh_Article_Spring);

    HSD_GObjGXLink_8039084C(gobj);
    GObj_SetupGXLink(gobj, ftSh_GXLink, 5, 0);

    fv->spring_gobj = NULL;
    fv->color = NULL;
    fv->specials_charge = 0;
    fv->run_shoes_spawned = 0;

    archive = mu_ak_costume_archive(fp->kind, fp->costume_id);
    if (archive != NULL) {
        fv->color = (ftSh_ColorData*) HSD_ArchiveGetPublicAddress(archive, "PlySonicColor");
    }

    if (mu_mex_fighter_walljump(mu_ak_mex_internal(fp->kind)) > 0) {
        fp->can_walljump = true;
    }

    {
        /* Bring-up evidence (once). */
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[ak] Shadow load: article %d, color %s, wall jump %d\n",
                     mu_ak_item_kind(fp->kind, ftSh_Article_Spring),
                     fv->color != NULL ? "yes" : "NO", (int) fp->can_walljump);
        }
    }
}

/* +00D8 (slot 1, ondeath): the stored spin dash charge dropped and the four model part
 * overrides reset (parts 0 to 2 to 0, part 3 to -1). */
static void ftSh_Init_OnDeath(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftSh_FV(fp)->specials_charge = 0;
    ftParts_80074A4C(gobj, 0, 0);
    ftParts_80074A4C(gobj, 1, 0);
    ftParts_80074A4C(gobj, 2, 0);
    ftParts_80074A4C(gobj, 3, -1);
}

/* +0144 (slot 2, onunknown = OnDestroy): the item in fp+222C destroyed and forgotten. Nothing in
 * PlSh.dat ever writes that word (the spring is never spawned by his code), so it stays NULL. */
static void ftSh_Init_OnDestroy(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_FighterVars* fv = ftSh_FV(fp);

    if (fv->spring_gobj != NULL) {
        Item_8026A8EC(fv->spring_gobj);
        fv->spring_gobj = NULL;
    }
}

/* +0CB0 (slot 25, onrespawn = the attribute reload): the 0x154 bytes copied again into fp+2D8. */
static void ftSh_Init_OnRespawn(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    memcpy(fp->dat_attrs_backup, DP(fp->ft_data->ext_attr), sizeof(ftSh_DatAttrs));
}

/* ---- items held in the hand ------------------------------------------------------------------
 * The bodies are the decomp's inlines (ft/inlines.h) with both hand flags 1, read instruction by
 * instruction: +087C (the hold kind call at 0x8026B320 is itGetHoldKind) = Fighter_OnItemPickup,
 * +0A30 and +0A9C = Fighter_OnItemDrop, +0980 = Fighter_OnItemInvisible. */

static void ftSh_Init_OnItemPickup(HSD_GObj* gobj, bool catch_item)
{
    Fighter_OnItemPickup(gobj, catch_item, true, true);
}

/* +0980 (slot 14) */
static void ftSh_Init_OnItemInvisible(HSD_GObj* gobj)
{
    Fighter_OnItemInvisible(gobj, true);
}

/* +09D8 (slot 15): the same as +0980, ftAnim_80070CC4 (the hide), as the Akaneia Sonic's file
 * has it. Mod bug kept: making the item visible hides it. */
static void ftSh_Init_OnItemVisible(HSD_GObj* gobj)
{
    Fighter_OnItemInvisible(gobj, true);
}

/* +0A30 (slot 16) */
static void ftSh_Init_OnItemDrop(HSD_GObj* gobj, bool drop_item)
{
    Fighter_OnItemDrop(gobj, drop_item, true, true);
}

/* +0A98 (slot 17): a branch to +087C. */
static void ftSh_Init_OnItemCatch(HSD_GObj* gobj, bool catch_item)
{
    ftSh_Init_OnItemPickup(gobj, catch_item);
}

/* +0A9C (slot 18): the same 26 words as +0A30. */
static void ftSh_Init_OnUnknownItemRelated(HSD_GObj* gobj, bool drop_item)
{
    Fighter_OnItemDrop(gobj, drop_item, true, true);
}

/* ---- eyes ----------------------------------------------------------------------------------- */

/* +0B04 (slot 21): the hurt eyes, texture objects 0 then 1 at frame 3 (+3AC4). */
static void ftSh_Init_EyeTextureDamaged(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 3.0F);
    ftAnim_800704F0(gobj, 1, 3.0F);
}

/* +0B54 (slot 22): back to frame 0. */
static void ftSh_Init_EyeTextureNormal(HSD_GObj* gobj)
{
    ftAnim_800704F0(gobj, 0, 0.0F);
    ftAnim_800704F0(gobj, 1, 0.0F);
}

/* ---- every frame ---------------------------------------------------------------------------- */

/* +3B88: the run effect's callback. Its texture animations are posed on the fighter's command
 * frame (HSD_ForeachAnim, JOBJ type, TOBJ mask, HSD_AObjReqAnim, argument kind 1 with the frame)
 * and animated once, then HSD_AObjSetRate through the same walk, and the root scaled by the
 * fighter's x34_scale.y.
 *
 * The console's second walk passes argument kind 6 (AOBJ_ARG_AOV) with two zero words to
 * HSD_AObjSetRate, which takes a float. Kind 6 sets no float register, so the rate is f1 as
 * HSD_JObjAnimAll left it: JObjUpdateFunc's |value| for the last track it ran, scale Z of the
 * effect's last animated joint (EfShData.dat effect 6: the root's second child's second child),
 * 16383/16384 or 1.0 by its loop frame. That joint's scale.z after the update is that value. */
static float ftSh_RunEffectRate(HSD_JObj* root)
{
    HSD_JObj* last = root->child != NULL ? root->child->next : NULL;
    last = last != NULL ? last->child : NULL;
    last = last != NULL ? last->next : NULL;
    return last != NULL ? last->scale.z : 1.0F;   /* the tree effect 6 always has */
}

static void ftSh_RunEffectCallback(EF_Effect* effect)
{
    HSD_GObj* owner = (HSD_GObj*) effect->user_data;
    Fighter* fp = GET_FIGHTER(owner);
    HSD_JObj* jobj = effect->gobj->hsd_obj;

    HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjReqAnim, AOBJ_ARG_AF,
                    fp->x3E4_fighterCmdScript.frame_count);
    HSD_JObjAnimAll(jobj);
    HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjSetRate, AOBJ_ARG_AF,
                    ftSh_RunEffectRate(jobj));
    jobj->scale.x = fp->x34_scale.y;
    jobj->scale.y = fp->x34_scale.y;
    jobj->scale.z = fp->x34_scale.y;
    HSD_JObjSetMtxDirtySub(jobj);
}

/* +0BA4 (slot 23, onframe): the dash attack gets the trail (accessory4_cb +3C2C when it has
 * none); in Run the run effect 5006 is spawned once on TransN (its callback +3B88, x21EC
 * efLib_DestroyAll). Any other state re-arms it. Unlike the Akaneia Sonic's, nothing paints the
 * effect. */
static void ftSh_Init_OnFrame(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_FighterVars* fv = ftSh_FV(fp);
    EF_Effect* effect;

    if (fp->motion_id == ftCo_MS_AttackDash) {
        if (fp->accessory4_cb == NULL) {
            fp->accessory4_cb = ftSh_GFXTrail;
        }
        fv->run_shoes_spawned = 0;
        return;
    }
    if (fp->motion_id != ftCo_MS_Run) {
        fv->run_shoes_spawned = 0;
        return;
    }
    if (fv->run_shoes_spawned != 0) {
        return;
    }
    effect = efSync_Spawn(ftSh_Ef_RunShoes, gobj, fp->parts[FtPart_TransN].joint, NULL);
    if (effect == NULL) {
        return;
    }
    effect->user_data = gobj;
    effect->update = ftSh_RunEffectCallback;
    ftSh_RunEffectCallback(effect);
    fv->run_shoes_spawned = 1;
    fp->x21EC = efLib_DestroyAll;
}

/* +0C7C (slot 24, onactionstatechange): a stored full spin dash charge keeps its color overlay
 * (article list entry 1 of ftData+0x48, as the Akaneia Sonic's). */
static void ftSh_Init_OnActionStateChange(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftSh_FV(fp)->specials_charge < ftSh_DA(fp)->specials_max_charge) {
        return;
    }
    {
        ftSh_DiscSlot* items = (ftSh_DiscSlot*) DISC_GET(void, fp->ft_data->x48_items);
        lb_800144C8(&fp->x488, (struct Fighter_804D653C_t*) DP(items[1]), 0, 0);
    }
}

/* +0CC8 (slot 26, onmodelrender, called with the model matrix): the mouth is one of the model
 * groups 2 and 3, shown on the side that faces the camera; group 0 on model 1 hides both. Only
 * the gobj is read. */
static void ftSh_Init_OnModelRender(HSD_GObj* gobj, int arg1, Mtx mtx)
{
    Fighter* fp = GET_FIGHTER(gobj);

    (void) arg1;
    (void) mtx;
    if (fp->x5F4_arr[0].idx == 1) {
        fp->x5F4_arr[2].idx = -1;
        fp->x5F4_arr[3].idx = -1;
        return;
    }
    if (fp->facing_dir == 1.0F) {
        if ((u8) fp->x5F4_arr[2].idx != 0xFF) {
            fp->x5F4_arr[3].idx = fp->x5F4_arr[2].idx;
        } else if ((u8) fp->x5F4_arr[3].idx == 0xFF) {
            fp->x5F4_arr[3].idx = fp->x5F4_arr[2].prev;
        }
        fp->x5F4_arr[2].idx = -1;
        return;
    }
    if ((u8) fp->x5F4_arr[2].idx == 0xFF) {
        fp->x5F4_arr[2].idx = fp->x5F4_arr[2].prev;
    }
    fp->x5F4_arr[3].idx = -1;
}

/* +0D50 (slot 32, enterdoublejump): `b ftCo_JumpAerial_Enter_Basic`. */
static void ftSh_Init_EnterDoubleJump(HSD_GObj* gobj)
{
    ftCo_JumpAerial_Enter_Basic(gobj);
}

/* ---- effects -------------------------------------------------------------------------------- */

/* +3AE4 and +58C8 (30 words each, the same instructions): the warp's accessory4_cb. Once per
 * move (x2219_b0) it asks for WaistN's world position into a stack Vec3 that is then dropped (an
 * effect spawn was taken out of the source, the call stays), marks the move, puts the hitlag
 * effect pauses in, and removes itself. */
void ftSh_GFXWarp(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x2219_b0) {
        Vec3 unused;
        lb_8000B1CC(fp->parts[FtPart_WaistN].joint, NULL, &unused);
        fp->x2219_b0 = true;
    }
    fp->pre_hitlag_cb = efLib_PauseAll;
    fp->post_hitlag_cb = efLib_ResumeAll;
    fp->accessory4_cb = NULL;
}

/* +3F48: the spin ball (5000 on TransN), once per move; TransN's scale set to the model scale
 * without marking the matrix (as the Akaneia Sonic's); the hitlag pauses; removes itself. */
void ftSh_GFXSpin(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x2219_b0) {
        HSD_JObj* joint = fp->parts[FtPart_TransN].joint;

        efSync_Spawn(ftSh_Ef_Spin, gobj, joint);
        joint->scale.x = fp->co_attrs.model_scaling;
        joint->scale.y = fp->co_attrs.model_scaling;
        joint->scale.z = fp->co_attrs.model_scaling;
        fp->pre_hitlag_cb = efLib_PauseAll;
        fp->post_hitlag_cb = efLib_ResumeAll;
        fp->x2219_b0 = true;
    }
    fp->accessory4_cb = NULL;
}

/* +55AC: XRotN's world position into fp+2238 and the angle of this frame's step into fp+2244
 * (atan2f(dy, dx) of cur_pos against prev_pos). */
void ftSh_UpdateTrailPosAndRot(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_FighterVars* fv = ftSh_FV(fp);

    lb_8000B1CC(fp->parts[FtPart_XRotN].joint, NULL, &fv->trail_pos);
    fv->trail_angle = atan2f(fp->cur_pos.y - fp->prev_pos.y, fp->cur_pos.x - fp->prev_pos.x);
}

/* +5608: the trail of this frame, while cmd_vars[3] is 1. */
void ftSh_GFXTrailLoop(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_FighterVars* fv = ftSh_FV(fp);
    GXColor tint;

    if (fv->color != NULL) {
        tint = fv->color->trail_diffuse;
        ftSh_SpawnTrailEffectAt(gobj, fp->cmd_vars[3] == 1, &fv->trail_pos, &fv->trail_angle,
                                &tint);
    } else {
        ftSh_SpawnTrailEffectAt(gobj, fp->cmd_vars[3] == 1, &fv->trail_pos, &fv->trail_angle,
                                NULL);
    }
}

/* +3C2C: start a trail (installed as accessory4_cb), then the per-frame loop. Outside the up
 * smash (motion 0x3F) cmd_vars[3] is set here. */
void ftSh_GFXTrail(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftSh_UpdateTrailPosAndRot(gobj);
    fp->accessory4_cb = ftSh_GFXTrailLoop;
    if (fp->motion_id != ftCo_MS_AttackHi4) {
        fp->cmd_vars[3] = 1;
    }
}

/* +5C90: the homing dash's accessory4_cb: +3F48, the trail start (+55AC and the same three
 * stores as +3C2C, inlined), then the trail angle from the velocity. */
void ftSh_GFXSpinAndTrailVelocityDirection(HSD_GObj* gobj)
{
    Fighter* fp;

    ftSh_GFXSpin(gobj);
    ftSh_GFXTrail(gobj);
    fp = GET_FIGHTER(gobj);
    ftSh_FV(fp)->trail_angle = atan2f(fp->self_vel.y, fp->self_vel.x);
}

/* +5DD4: the dive's accessory4_cb: +3F48's body inlined, then the trail start (both the same
 * stores in the same order). */
void ftSh_GFXSpinAndTrail(HSD_GObj* gobj)
{
    ftSh_GFXSpin(gobj);
    ftSh_GFXTrail(gobj);
}

/* +5A64: lay a trail segment (effect 5001) from last frame's XRotN position to this frame's.
 * The angle of the step is brought into [0, 2 pi) in double steps rounded to single. A segment is
 * laid when asked and when the point moved; unlike the Akaneia Sonic's there is no test of the
 * forward speed or of the step length. The segment is left in the world (parent_gobj NULL), the
 * root's scale moved onto both ends; with a color, the root's first material gets it. The
 * position and angle are kept either way. Used for the fighter (+5A64, color from the costume)
 * and for Kirby's hat (hat +175C, its own fields and a fixed color, shadow_kirby.c). */
void ftSh_SpawnTrailEffectAt(HSD_GObj* gobj, bool spawn, Vec3* trail_pos, float* trail_angle,
                             const GXColor* color)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos;
    float angle;

    lb_8000B1CC(fp->parts[FtPart_XRotN].joint, NULL, &pos);

    angle = atan2f(pos.y - trail_pos->y, pos.x - trail_pos->x);
    if (angle < 0.0F) {
        do {
            angle = (float) (angle + FTSH_TAU);
        } while (angle < 0.0F);
    }
    while (angle >= FTSH_TAU) {
        angle = (float) (angle - FTSH_TAU);
    }

    if ((trail_pos->x != pos.x || trail_pos->y != pos.y) && spawn) {
        Vec3 zero = { 0.0F, 0.0F, 0.0F };
        EF_Effect* effect = efSync_Spawn(ftSh_Ef_Trail, gobj, &zero, NULL);

        if (effect != NULL) {
            HSD_JObj* root = effect->gobj->hsd_obj;
            HSD_JObj* head = root->child;
            HSD_JObj* tail = head->next;
            Vec3 scale = root->scale;

            effect->parent_gobj = NULL;
            root->scale.x = 1.0F;
            root->scale.y = 1.0F;
            root->scale.z = 1.0F;

            head->scale = scale;
            head->rotate.z = angle;
            head->translate = pos;

            tail->scale = scale;
            tail->translate = *trail_pos;
            tail->rotate.z = *trail_angle;

            if (color != NULL) {
                root->u.dobj->mobj->mat->diffuse = *color;
            }
        }
    }

    *trail_angle = angle;
    *trail_pos = pos;
}

/* ---- debug drawing (fighter debug display only) --------------------------------------------- */

/* +5624: the GX state of both circles (the same calls and arguments as the Akaneia Sonic's). */
static void ftSh_GXLink_Init(void)
{
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
    GXSetZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
    GXSetZCompLoc(GX_FALSE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE,
                  GX_AF_NONE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXLoadPosMtxImm(HSD_CObjGetViewingMtxPtrDirect(HSD_CObjGetCurrent()), GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
}

static void ftSh_GXLink_End(void)
{
    HSD_StateInvalidate(HSD_STATE_ALL);
    HSD_StateInitTev();
    HSD_ClearVtxDesc();
}

/* +3C78: a circle of radius 5 (+3AE0) around the homing target, 9 points, light red. The angle
 * is (i * 2 pi) * 0.125 in double, rounded to single; each coordinate one fmadds. */
MU_NO_CONTRACT void ftSh_GXLink_DrawTarget(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftSh_SpecialNVars* mv = &ftSh_MV(fp)->specialn;
    int i;

    ftSh_GXLink_Init();
    GXBegin(GX_LINESTRIP, GX_VTXFMT0, 9);
    for (i = 0; i != 9; i++) {
        float angle = (float) ((double) (float) i * FTSH_TAU * (double) 0.125F);
        float c = cosf(angle);
        float s = sinf(angle);
        GXPosition3f32(MU_FMADDS(c, 5.0F, mv->target.x), MU_FMADDS(s, 5.0F, mv->target.y), 0.0F);
        GXColor4u8(0xFF, 0xB4, 0xB4, 0xFF);
    }
    ftSh_GXLink_End();
}

/* +3DD8: the search range (truncated to an integer, attribute +30) around him, 33 points,
 * white, at his depth. */
MU_NO_CONTRACT void ftSh_GXLink_DrawRadius(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float radius = (float) (s32) ftSh_DA(fp)->specialn_search_range;
    int i;

    ftSh_GXLink_Init();
    GXBegin(GX_LINESTRIP, GX_VTXFMT0, 33);
    for (i = 0; i != 33; i++) {
        float angle = (float) ((double) (float) i * FTSH_TAU * (double) 0.03125F);
        float c = cosf(angle);
        float s = sinf(angle);
        GXPosition3f32(MU_FMADDS(c, radius, fp->cur_pos.x), MU_FMADDS(s, radius, fp->cur_pos.y),
                       fp->cur_pos.z);
        GXColor4u8(0xFF, 0xFF, 0xFF, 0xFF);
    }
    ftSh_GXLink_End();
}

/* +0D54: the normal fighter draw, then (pass 2, fighter debug display on: x21FC bit 0x02) the
 * search radius in the charge (343) and the target in the dash (345). */
static void ftSh_GXLink(HSD_GObj* gobj, intptr_t pass)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftDrawCommon_80080E18(gobj, pass);
    if (pass != 2 || !fp->x21FC_flag.b6) {
        return;
    }
    if (fp->motion_id == ftSh_MS_SpecialNCharge) {
        ftSh_GXLink_DrawRadius(gobj);
    }
    if (fp->motion_id == ftSh_MS_SpecialNAttack) {
        ftSh_GXLink_DrawTarget(gobj);
    }
}

/* ---- the state table ------------------------------------------------------------------------ */

#define FTSH_ROW(anim, flags, move, a, i, p, c)                                                  \
    { (anim), (flags), { (u32) (move) << 24 }, (a), (i), (p), (c), ftCamera_UpdateCameraBox }

/* +0184 (slot 3, move_logic): 36 rows of 0x20 bytes, every word the file's (tables.txt). Rows
 * that share callbacks share them on the disc too. The retail callbacks are named by the address
 * in the row; the empty ones (`blr`) are ftSh_SpecialN_Empty, one function for all. */
const MotionState ftSh_Init_MotionStateTable[FTSH_MS_COUNT] = {
    /* 341 */ FTSH_ROW(295, 0x00340111, 0x12, ftSh_SpecialNStart_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialN_Empty, ftSh_SpecialNStart_Coll),
    /* 342 */ FTSH_ROW(296, 0x00340111, 0x12, ftSh_SpecialNStart_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialN_Empty, ftSh_SpecialNStart_Coll),
    /* 343 */ FTSH_ROW(297, 0x00340111, 0x12, ftSh_SpecialNCharge_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialN_Empty, ftSh_SpecialNCharge_Coll),
    /* 344 */ FTSH_ROW(298, 0x00340111, 0x12, ftSh_SpecialNAttackMiss_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialN_Empty, ftSh_SpecialNDash_Coll),
    /* 345: coll +11CC is a branch to +0FB8 */
    /* 345 */ FTSH_ROW(298, 0x00340111, 0x12, ftSh_SpecialNAttack_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialN_Empty, ftSh_SpecialNDash_Coll),
    /* 346 */ FTSH_ROW(299, 0x00340111, 0x12, ftSh_SpecialNCancel_Anim, ftSh_SpecialNCancel_IASA,
                       ftSh_SpecialNCancel_Phys, ftSh_SpecialNCancel_Coll),
    /* 347 */ FTSH_ROW(302, 0x00340111, 0x12, ftSh_SpecialNLanding_Anim,
                       ftSh_SpecialNLanding_IASA, ftSh_SpecialNLanding_Phys,
                       ftSh_SpecialNLanding_Coll),
    /* 348 */ FTSH_ROW(301, 0x00340111, 0x12, ftSh_SpecialNRebound_Anim,
                       ftSh_SpecialNRebound_IASA, ftSh_SpecialNRebound_Phys,
                       ftSh_SpecialNRebound_Coll),
    /* 349 */ FTSH_ROW(300, 0x00340111, 0x12, ftSh_SpecialNRebound_Anim,
                       ftSh_SpecialNRebound_IASA, ftSh_SpecialNRebound_Phys,
                       ftSh_SpecialNRebound_Coll),
    /* 350: phys ft_80084F3C (0x80084F3C) */
    /* 350 */ FTSH_ROW(303, 0x00340013, 0x14, ftSh_SpecialHiStart_Anim, ftSh_SpecialN_Empty,
                       ft_80084F3C, ftSh_SpecialHiStart_Coll),
    /* 351: phys ftCommon_SetSelfMovementFromGroundedMovement (0x8007CB74) */
    /* 351 */ FTSH_ROW(321, 0x00340013, 0x14, ftSh_SpecialHiWarp_Anim, ftSh_SpecialN_Empty,
                       ftCommon_SetSelfMovementFromGroundedMovement, ftSh_SpecialHiWarp_Coll),
    /* 352: phys ft_80084F3C */
    /* 352 */ FTSH_ROW(322, 0x00340013, 0x14, ftSh_SpecialHiEnd_Anim, ftSh_SpecialN_Empty,
                       ft_80084F3C, ftSh_SpecialHiEnd_Coll),
    /* 353 */ FTSH_ROW(303, 0x00340413, 0x14, ftSh_SpecialAirHiStart_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialAirHiStart_Phys, ftSh_SpecialAirHiStart_Coll),
    /* 354: no physics (+1AF4 is `blr`): the warp's speed is set once at its entry */
    /* 354 */ FTSH_ROW(321, 0x00340413, 0x14, ftSh_SpecialAirHiWarp_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialN_Empty, ftSh_SpecialAirHiWarp_Coll),
    /* 355 */ FTSH_ROW(323, 0x00340413, 0x14, ftSh_SpecialAirHiEnd_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialAirHiEnd_Phys, ftSh_SpecialAirHiEnd_Coll),
    /* 356: phys ft_80084F3C */
    /* 356 */ FTSH_ROW(304, 0x00340112, 0x13, ftSh_SpecialSStart_Anim, ftSh_SpecialN_Empty,
                       ft_80084F3C, ftSh_SpecialSStart_Coll),
    /* 357: phys ft_80084F3C */
    /* 357 */ FTSH_ROW(306, 0x00340112, 0x13, ftSh_SpecialSEnd_Anim, ftSh_SpecialN_Empty,
                       ft_80084F3C, ftSh_SpecialSEnd_Coll),
    /* 358: anim +2110 is +1F20's 42 words again; phys ft_80084EEC (0x80084EEC) */
    /* 358 */ FTSH_ROW(307, 0x00340112, 0x13, ftSh_SpecialSStart_Anim, ftSh_SpecialN_Empty,
                       ft_80084EEC, ftSh_SpecialAirSStart_Coll),
    /* 359 */ FTSH_ROW(309, 0x00340112, 0x13, ftSh_SpecialAirSEnd_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialAirSEnd_Phys, ftSh_SpecialAirSEnd_Coll),
    /* 360 */ FTSH_ROW(310, 0x00340112, 0x13, ftSh_SpecialSHold_Anim, ftSh_SpecialSHold_IASA,
                       ftSh_SpecialSHold_Phys, ftSh_SpecialSHold_Coll),
    /* 361 */ FTSH_ROW(311, 0x00340112, 0x13, ftSh_SpecialS_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialS_Phys, ftSh_SpecialS_Coll),
    /* 362 */ FTSH_ROW(313, 0x00340112, 0x13, ftSh_SpecialAirS_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialAirS_Phys, ftSh_SpecialAirS_Coll),
    /* 363: row 20's callbacks */
    /* 363 */ FTSH_ROW(312, 0x00340112, 0x13, ftSh_SpecialS_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialS_Phys, ftSh_SpecialS_Coll),
    /* 364: row 21's callbacks */
    /* 364 */ FTSH_ROW(314, 0x00340112, 0x13, ftSh_SpecialAirS_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialAirS_Phys, ftSh_SpecialAirS_Coll),
    /* 365: phys ft_80084F3C */
    /* 365 */ FTSH_ROW(304, 0x00340114, 0x15, ftSh_SpecialLwStart_Anim, ftSh_SpecialN_Empty,
                       ft_80084F3C, ftSh_SpecialLwStart_Coll),
    /* 366: phys ft_80084F3C */
    /* 366 */ FTSH_ROW(306, 0x00340114, 0x15, ftSh_SpecialLwEnd_Anim, ftSh_SpecialN_Empty,
                       ft_80084F3C, ftSh_SpecialLwEnd_Coll),
    /* 367: anim +2B08 (the same 19 words as +2964); phys ft_80084EEC */
    /* 367 */ FTSH_ROW(307, 0x00340114, 0x15, ftSh_SpecialAirLwStart_Anim, ftSh_SpecialN_Empty,
                       ft_80084EEC, ftSh_SpecialAirLwStart_Coll),
    /* 368: phys ft_80084EEC */
    /* 368 */ FTSH_ROW(309, 0x00340114, 0x15, ftSh_SpecialAirLwEnd_Anim, ftSh_SpecialN_Empty,
                       ft_80084EEC, ftSh_SpecialAirLwEnd_Coll),
    /* 369 */ FTSH_ROW(305, 0x00340114, 0x15, ftSh_SpecialLwCharge_Anim,
                       ftSh_SpecialLwCharge_IASA, ftSh_SpecialLwCharge_Phys,
                       ftSh_SpecialLwCharge_Coll),
    /* 370 */ FTSH_ROW(315, 0x00340114, 0x15, ftSh_SpecialLwRun_Anim, ftSh_SpecialLwRun_IASA,
                       ftSh_SpecialLwRun_Phys, ftSh_SpecialLwRun_Coll),
    /* 371: anim +3438 is `blr` */
    /* 371 */ FTSH_ROW(316, 0x00340114, 0x15, ftSh_SpecialN_Empty, ftSh_SpecialLwRunTurn_IASA,
                       ftSh_SpecialLwRunTurn_Phys, ftSh_SpecialLwRunTurn_Coll),
    /* 372: anim +3550 is `blr` */
    /* 372 */ FTSH_ROW(317, 0x00340114, 0x15, ftSh_SpecialN_Empty, ftSh_SpecialLwRunJump_IASA,
                       ftSh_SpecialLwRunJump_Phys, ftSh_SpecialLwRunJump_Coll),
    /* 373 */ FTSH_ROW(318, 0x00340114, 0x15, ftSh_SpecialLwDive_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialLwDive_Phys, ftSh_SpecialLwDive_Coll),
    /* 374: animation 14 (the common RunBrake's) */
    /* 374 */ FTSH_ROW(14, 0x00340114, 0x15, ftSh_SpecialLwRunBrake_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialLwRunBrake_Phys, ftSh_SpecialLwRunBrake_Coll),
    /* 375 */ FTSH_ROW(319, 0x00340114, 0x15, ftSh_SpecialLwStopWall_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialN_Empty, ftSh_SpecialLwStopWall_Coll),
    /* 376: row 34's callbacks */
    /* 376 */ FTSH_ROW(320, 0x00340114, 0x15, ftSh_SpecialLwStopWall_Anim, ftSh_SpecialN_Empty,
                       ftSh_SpecialN_Empty, ftSh_SpecialLwStopWall_Coll),
};

/* The 25 slots PlSh.dat exports. Every other field is NULL, as the disc's MxDt leaves it (slot 3's
 * default is replaced by the export). Written and never run, so not MU_AK_READY. */
const MuAkFighter mu_ak_shadow = {
    .name = "Shadow",
    .file = "PlSh.dat",

    .onload = ftSh_Init_OnLoad,
    .ondeath = ftSh_Init_OnDeath,
    .onunknown = ftSh_Init_OnDestroy,
    .specialn = ftSh_SpecialN_Enter,
    .specialairn = ftSh_SpecialN_Enter,
    .specials = ftSh_SpecialS_Enter,
    .specialairs = ftSh_SpecialS_Enter,
    .specialhi = ftSh_SpecialHi_Enter,
    .specialairhi = ftSh_SpecialAirHi_Enter,
    .speciallw = ftSh_SpecialLw_Enter,
    .specialairlw = ftSh_SpecialLw_Enter,
    /* The item events take (gobj, bool) like the decomp's tables (INTEGRATION.md). */
    .onitempickup = (MuAkEvent) ftSh_Init_OnItemPickup,
    .onmakeiteminvisible = ftSh_Init_OnItemInvisible,
    .onmakeitemvisible = ftSh_Init_OnItemVisible,
    .onitemdrop = (MuAkEvent) ftSh_Init_OnItemDrop,
    .onitemcatch = (MuAkEvent) ftSh_Init_OnItemCatch,
    .onunknownitemrelated = (MuAkEvent) ftSh_Init_OnUnknownItemRelated,
    .onhit = ftSh_Init_EyeTextureDamaged,
    .onunknowneyetexturerelated = ftSh_Init_EyeTextureNormal,
    .onframe = ftSh_Init_OnFrame,
    .onactionstatechange = ftSh_Init_OnActionStateChange,
    .onrespawn = ftSh_Init_OnRespawn,
    /* Slot 26 is called (gobj, int, Mtx) (ftdrawcommon.c). */
    .onmodelrender = (MuAkEvent) ftSh_Init_OnModelRender,
    .enterdoublejump = ftSh_Init_EnterDoubleJump,

    .move_logic = ftSh_Init_MotionStateTable,
    .move_logic_count = FTSH_MS_COUNT,

    .flags = 0,
    .articles = &itSh_Spring_LogicTable,
    .article_count = ftSh_Article_Count,

    .kirby = &ftKbSh_Copy,
};
