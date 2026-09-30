/* Akaneia's Tails, native: everything drawn around the moves.
 *
 *  - ProcessTail (a per-frame gobj proc): when model group 0 shows the curled-up ball, the tail
 *    ball joint is placed by hand (user matrix) and rolled toward the floor slope on the ground or
 *    the vertical speed in the air.
 *  - ProcessTrail: spawns the tail effects the animation scripts ask for (cmd_vars[3]) and records
 *    the two-ribbon trail drawn behind the side special's spin; ftTs_GXCallback draws it.
 *  - ProcessMouth: moves the mouth expression between the left and right mouth model groups with
 *    the facing, hides it while balled up, and hides worn head items while balled up.
 *  - The ball / helicopter effects and the particles are recoloured from the costume's
 *    "PlyTailsColor" table (or a flat grey when metal).
 *  - On the result screen: the victory voice line, with an alternative line when a Sonic player
 *    lost to him. */
#include "ftTs.h"
#include "ftTs_hooks.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <melee/ef/efsync.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>
#include <melee/gm/gmresultplayer.static.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/particle.h>
#include <sysdolphin/baselib/psstructs.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tobj.h>

#define TS_PI 3.141592653589793
#define TS_TWO_PI 6.283185307179586
#define TS_HALF_PI 1.5707963267948966

/* code+0x3230 MetalColor: every colour a flat 0xBEBEBEFF. */
const ftTails_Colors ftTs_MetalColor = {
    { 0xBE, 0xBE, 0xBE, 0xFF }, { 0xBE, 0xBE, 0xBE, 0xFF }, { 0xBE, 0xBE, 0xBE, 0xFF },
    { 0xBE, 0xBE, 0xBE, 0xFF }, { 0xBE, 0xBE, 0xBE, 0xFF }, { 0xBE, 0xBE, 0xBE, 0xFF },
    { 0xBE, 0xBE, 0xBE, 0xFF },
};

/* The colours in use: metal overrides the costume. NULL when the costume has no table (the
 * console would read low memory there; the Source Port skips the recolour instead). */
const ftTails_Colors* ftTs_GetColors(Fighter* fp)
{
    if (fp->is_metal) {
        return &ftTs_MetalColor;
    }
    return ftTs_Vars(fp)->particle_hook.colors;
}

/* ------------------------------------------------------------------------------------------------
 * Tail ball
 * --------------------------------------------------------------------------------------------- */

/* code+0x54E4 slerp: ease angle `from` toward `to` by `t`, the short way round. */
static float ftTs_slerp(float from, float to, float t)
{
    float start = fmodf(from, 6.2831855f);
    float delta = fmodf(fmodf(to, 6.2831855f) - start, 6.2831855f);
    float step;

    if ((double) delta >= TS_PI) {
        delta = (float) ((double) delta - TS_TWO_PI);
    } else if ((double) delta < -TS_PI) {
        delta = (float) ((double) delta + TS_TWO_PI);
    }
    step = delta * t;
    return fmodf(step + start, 6.2831855f);
}

/* code+0x324C Tails_EnableTailBall */
static void ftTs_EnableTailBall(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_FighterVars* fv = ftTs_Vars(fp);
    HSD_JObj* jobj;
    Vec3 local;
    Vec3 pos;
    Vec3 scale;
    Vec3 rot;
    double facing_rot;
    Mtx srt;
    float target;

    if (fp->ground_or_air == GA_Ground) {
        /* Lie along the floor. */
        target = (float) (atan2f(fp->coll_data.floor.normal.y, fp->coll_data.floor.normal.x) -
                          TS_HALF_PI);
    } else {
        target = atan2f(fp->self_vel.y, 1.0f) * fp->facing_dir;
    }
    if (fv->ball_angle != target) {
        fv->ball_angle = ftTs_slerp(fv->ball_angle, target, 0.2f);
    }

    jobj = fp->parts[ftTs_Part_TailBase].joint;
    local.x = 0.0f;
    local.y = 4.8f;
    local.z = 0.0f;
    lb_8000B1CC(fp->parts[ftTs_Part_BallPivot].joint, &local, &pos);

    scale.x = scale.y = scale.z = fp->x34_scale.y;
    facing_rot = fp->facing_dir * TS_HALF_PI;
    rot.x = 0.0f;
    rot.y = (float) facing_rot;
    rot.z = (float) (fv->ball_angle + facing_rot);
    HSD_MtxSRT(srt, &scale, &rot, &pos, NULL);
    PSMTXConcat(srt, fv->ball_mtx, jobj->mtx);

    jobj->flags |= JOBJ_USER_DEF_MTX | JOBJ_MTX_INDEP_PARENT | JOBJ_MTX_INDEP_SRT;
    HSD_JObjSetMtxDirtySub(jobj);
}

/* code+0x33AC Tails_DisableTailBall */
static void ftTs_DisableTailBall(Fighter_GObj* gobj)
{
    HSD_JObj* jobj = GET_FIGHTER(gobj)->parts[ftTs_Part_TailBase].joint;
    u32 flags = jobj->flags;

    jobj->flags = flags & ~(JOBJ_USER_DEF_MTX | JOBJ_MTX_INDEP_PARENT | JOBJ_MTX_INDEP_SRT);
    if (flags & JOBJ_USER_DEF_MTX) {
        HSD_JObjSetMtxDirtySub(jobj);
    }
}

/* code+0xC64 ProcessTail (proc, priority 15) */
void ftTs_ProcessTail(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->motion_id == ftCo_MS_AttackHi4 || fp->motion_id == ftCo_MS_AttackAirN) {
        ftTs_DisableTailBall(gobj);
    } else if (fp->x5F4_arr[0].idx != 0) {
        ftTs_EnableTailBall(gobj);
    } else {
        ftTs_DisableTailBall(gobj);
    }
}

/* ------------------------------------------------------------------------------------------------
 * Effects
 * --------------------------------------------------------------------------------------------- */

/* code+0x55B4 Effect_Recolor: set the TEV konst / tev0 colours of every textured material in an
 * effect's joint tree. */
static void ftTs_Effect_Recolor(HSD_JObj* jobj, const GXColor* konst, const GXColor* tev0)
{
    HSD_DObj* dobj;

    if (jobj == NULL) {
        return;
    }
    for (dobj = HSD_JObjGetDObj(jobj); dobj != NULL; dobj = dobj->next) {
        HSD_TObj* tobj;

        for (tobj = dobj->mobj->tobj; tobj != NULL; tobj = tobj->next) {
            if (tobj->tev != NULL) {
                tobj->tev->konst = *konst;
                tobj->tev->tev0 = *tev0;
            }
        }
    }
    ftTs_Effect_Recolor(jobj->child, konst, tev0);
    ftTs_Effect_Recolor(jobj->next, konst, tev0);
}

static inline bool ftTs_EffectIsModel(EF_Effect* eff)
{
    return eff != NULL && eff->gobj->obj_kind == HSD_GObj_JObjKind;
}

/* code+0x33EC SpawnBallEffect */
void ftTs_SpawnBallEffect(Fighter_GObj* gobj, int effect_id, int part)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const ftTails_Colors* colors = ftTs_GetColors(fp);
    EF_Effect* eff = efSync_Spawn(effect_id, gobj, fp->parts[part].joint);

    if (ftTs_EffectIsModel(eff) && colors != NULL) {
        ftTs_Effect_Recolor(eff->gobj->hsd_obj, &colors->ball_konst, &colors->ball_tev0);
        fp->x2219_b0 = true;
    }
}

/* code+0x349C SpawnHelicopterEffect */
void* ftTs_SpawnHelicopterEffect(Fighter_GObj* gobj, int effect_id, int part)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const ftTails_Colors* colors = ftTs_GetColors(fp);
    EF_Effect* eff = efSync_Spawn(effect_id, gobj, fp->parts[part].joint);

    if (ftTs_EffectIsModel(eff) && colors != NULL) {
        ftTs_Effect_Recolor(eff->gobj->hsd_obj, &colors->trail_inner, &colors->trail_outer);
    }
    return eff;
}

/* code+0xBA8 OnSpawnParticle: the generator user hook (fv.particle_hook). Runs the particle's
 * command list once so its own colour commands are in, then puts the costume's colours on top. */
int ftTs_OnSpawnParticle(HSD_Particle* pp)
{
    ftTails_ParticleHook* hook =
        (ftTails_ParticleHook*) ((u8*) pp->gen->userfunc - offsetof(ftTails_ParticleHook, funcs));
    const ftTails_Colors* colors = ftTs_GetColors(hook->fp);

    hsd_8039930C(pp, NULL);
    if (colors == NULL) {
        return 0;
    }
    pp->primCol = colors->particle_prim;
    pp->envCol.r = colors->particle_env.r;
    pp->envCol.g = colors->particle_env.g;
    pp->envCol.b = colors->particle_env.b;
    pp->envCol.a = 0;
    pp->primColTarget = colors->particle_prim;
    pp->primColTarget.a = 0;
    return 0;
}

/* code+0x5F78 InitDashTailAnim: restart the part animation on part-anim slot 3 (the spinning
 * tails) at `frame`. */
void ftTs_InitDashTailAnim(Fighter_GObj* gobj, float frame)
{
    Fighter* fp = GET_FIGHTER(gobj);
    struct ftData_x1C* part_anim = DP(DP(fp->ft_data->x1C)[3]);
    HSD_JObj* jobj = fp->parts[part_anim->x0].joint;

    HSD_JObjAddAnimAll(jobj, DP(DP(part_anim->x8)[0]), NULL, NULL);
    HSD_JObjReqAnimAll(jobj, frame);
    HSD_JObjAnim(jobj);
}

/* ------------------------------------------------------------------------------------------------
 * Spin trail
 * --------------------------------------------------------------------------------------------- */

/* code+0x3544 IsTailTrailProcess: the side special's loop states. */
static bool ftTs_IsTailTrailProcess(Fighter_GObj* gobj)
{
    FtMotionId msid = GET_FIGHTER(gobj)->motion_id;

    return (msid >= ftTs_MS_SpecialSLoop0 && msid <= ftTs_MS_SpecialSLoop4) ||
           (msid >= ftTs_MS_SpecialAirSLoop0 && msid <= ftTs_MS_SpecialAirSLoop4);
}

/* code+0x3574 Vec3_Add */
static inline void ftTs_Vec3_Add(Vec3* a, const Vec3* b)
{
    a->x += b->x;
    a->y += b->y;
    a->z += b->z;
}

/* code+0xCC0 ProcessTrail (proc, priority 15) */
void ftTs_ProcessTrail(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftTails_FighterVars* fv = ftTs_Vars(fp);
    static const Vec3 tip_a_offset = { -0.9f, 0.0f, -2.5f };
    static const Vec3 tip_b_offset = { 0.9f, 0.0f, -2.5f };
    Vec3 offset;
    Vec3 delta;
    Vec3 tip_offset;
    double dist2;
    int count;
    int i;

    /* Tail effects requested by the animation scripts. */
    switch (fp->motion_id) {
    case ftCo_MS_AttackAirN:
    case ftCo_MS_AttackDash:
        if (fp->cmd_vars[3] != 0) {
            ftTs_SpawnBallEffect(gobj, ftTs_Ef_BallAir, ftTs_Part_AirBall);
            fp->cmd_vars[3] = 0;
        }
        break;
    case ftCo_MS_AttackHi4:
        if (fp->cmd_vars[3] != 0) {
            ftTs_SpawnBallEffect(gobj, ftTs_Ef_BallUpSmash, ftTs_Part_Hand);
            fp->cmd_vars[3] = 0;
        }
        break;
    case ftTs_MS_SpecialN:
    case ftTs_MS_SpecialAirN:
        if (fp->cmd_vars[3] != 0) {
            ftTs_SpawnHelicopterEffect(gobj, ftTs_Ef_HeliSpecialN, ftTs_Part_TailBase);
            fp->cmd_vars[3] = 0;
        }
        break;
    case ftCo_MS_Run:
    case ftTs_MS_SpecialHiLoop:
        if (!fp->x2219_b0) {
            ftTs_SpawnHelicopterEffect(gobj, ftTs_Ef_HeliFlight, ftTs_Part_TailBase);
            fp->x2219_b0 = true;
        }
        break;
    default:
        break;
    }

    if (!ftTs_IsTailTrailProcess(gobj)) {
        fv->trail_count = 0;
        return;
    }

    /* A jump of more than 10 units in one frame (a warp) drags the whole trail along. */
    offset.x = offset.y = offset.z = 0.0f;
    delta.x = fp->cur_pos.x - fv->trail_last_pos.x;
    delta.y = fp->cur_pos.y - fv->trail_last_pos.y;
    delta.z = fp->cur_pos.z - fv->trail_last_pos.z;
    dist2 = (double) delta.x * delta.x;
    dist2 = dist2 + (double) delta.y * delta.y;
    dist2 = dist2 + (double) delta.z * delta.z;
    if ((float) dist2 > 100.0f) {
        offset = delta;
    }
    fv->trail_last_pos = fp->cur_pos;

    /* Drop the oldest sample. */
    count = fv->trail_count;
    if (count > 1) {
        for (i = 0; i < count - 1; i++) {
            fv->trail_a[i] = fv->trail_a[i + 1];
            fv->trail_b[i] = fv->trail_b[i + 1];
            ftTs_Vec3_Add(&fv->trail_a[i].tip, &offset);
            ftTs_Vec3_Add(&fv->trail_a[i].base, &offset);
            ftTs_Vec3_Add(&fv->trail_b[i].tip, &offset);
            ftTs_Vec3_Add(&fv->trail_b[i].base, &offset);
        }
    }
    if (count < ftTs_TrailMax) {
        count++;
    }
    fv->trail_count = count;

    /* The newest sample: both ribbons start at the tail base, one tip per tail. */
    tip_offset = tip_a_offset;
    lb_8000B1CC(fp->parts[ftTs_Part_TailBase].joint, NULL, &fv->trail_a[count - 1].base);
    lb_8000B1CC(fp->parts[ftTs_Part_TailA].joint, &tip_offset, &fv->trail_a[count - 1].tip);
    tip_offset = tip_b_offset;
    lb_8000B1CC(fp->parts[ftTs_Part_TailBase].joint, NULL, &fv->trail_b[count - 1].base);
    lb_8000B1CC(fp->parts[ftTs_Part_TailB].joint, &tip_offset, &fv->trail_b[count - 1].tip);
}

/* code+0x35C0 Tails_DrawAfterImage: one ribbon as a triangle strip, fading from transparent (oldest)
 * to opaque (newest). Drawn in the xlu pass (2) only, and only in the normal display mode. */
static void ftTs_DrawAfterImage(Fighter_GObj* gobj, intptr_t pass, const ftTails_TrailPoint* pts,
                                int count)
{
    Fighter* fp;
    const ftTails_Colors* colors;
    float last;
    int i;

    if (pass != 2 || count <= 0) {
        return;
    }
    fp = GET_FIGHTER(gobj);
    if (!(fp->x21FC_flag.byte & 1)) {
        return;
    }

    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GXSetZCompLoc(GX_FALSE);
    GXSetNumTexGens(0);
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

    colors = ftTs_GetColors(fp);
    if (colors != NULL) {
        GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, (u16) (count * 2));
        last = (float) (count - 1);
        for (i = 0; i < count; i++) {
            u8 alpha = 0;

            if (count > 1) {
                alpha = (u8) (int) ((float) i / last * 255.0f);
            }
            GXPosition3f32(pts[i].tip.x, pts[i].tip.y, pts[i].tip.z);
            GXColor4u8(colors->trail_outer.r, colors->trail_outer.g, colors->trail_outer.b, alpha);
            GXPosition3f32(pts[i].base.x, pts[i].base.y, pts[i].base.z);
            GXColor4u8(colors->trail_inner.r, colors->trail_inner.g, colors->trail_inner.b, alpha);
        }
    }
    HSD_StateInvalidate(-1);
}

/* code+0x1158 Tails_GXCallback: render callback of fv.trail_gobj (its user_data is the fighter). */
void ftTs_GXCallback(HSD_GObj* gobj, intptr_t pass)
{
    Fighter_GObj* fighter_gobj = gobj->user_data;
    ftTails_FighterVars* fv;

    if (!ftTs_IsTailTrailProcess(fighter_gobj)) {
        return;
    }
    fv = ftTs_Vars(GET_FIGHTER(fighter_gobj));
    ftTs_DrawAfterImage(fighter_gobj, pass, fv->trail_a, fv->trail_count);
    ftTs_DrawAfterImage(fighter_gobj, pass, fv->trail_b, fv->trail_count);
}

/* ------------------------------------------------------------------------------------------------
 * Mouth
 * --------------------------------------------------------------------------------------------- */

/* code+0x1074 ProcessMouth (proc, priority 15). Model groups: 0 = ball, 2 = mouth facing right,
 * 3 = mouth facing left (-1 hides a group). */
void ftTs_ProcessMouth(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    bool balled = fp->x5F4_arr[0].idx != 0;
    u8 right;

    /* Worn head items (bunny hood and the like) hide while balled up. */
    if (fp->x197C != NULL) {
        ((Item*) fp->x197C->user_data)->xDAA_flag.b7 = !balled;
    }
    if (fp->x1980 != NULL) {
        ((Item*) fp->x1980->user_data)->xDAA_flag.b7 = !balled;
    }

    if (balled) {
        fp->x5F4_arr[3].idx = -1;
        fp->x5F4_arr[2].idx = -1;
        return;
    }
    right = (u8) fp->x5F4_arr[2].idx;
    if (right > 3) {
        fp->x5F4_arr[3].idx = -1;
        return;
    }
    if (fp->facing_dir == 1.0f) {
        u8 left = (u8) fp->x5F4_arr[3].idx;

        if ((u8) (left - 1) <= 0xFD) {
            fp->x5F4_arr[2].idx = left;
        }
        fp->x5F4_arr[3].idx = 0;
    } else {
        if (right != 0) {
            fp->x5F4_arr[3].idx = right;
        }
        fp->x5F4_arr[2].idx = 0;
    }
}

/* ------------------------------------------------------------------------------------------------
 * Result screen
 * --------------------------------------------------------------------------------------------- */

/* code+0x3FCC DidHeLose: did a player on character `name` finish below `slot`? */
static bool ftTs_DidHeLose(int slot, const char* name)
{
    MatchEnd* match_end = &lbl_8046E3AC.match_end;
    u8 mine = match_end->player_standings[slot].is_big_loser;
    int i;

    for (i = 0; i < 6; i++) {
        const char* their_name;

        if (i == slot || Player_GetPlayerSlotType(i) == Gm_PKind_NA) {
            continue;
        }
        if (!(mine < match_end->player_standings[i].is_big_loser)) {
            continue;
        }
        their_name = mu_ak_fighter_name(Player_GetPlayerCharacter(i));
        if (their_name != NULL && strcmp(name, their_name) == 0) {
            return true;
        }
    }
    return false;
}

/* code+0x122C Tails_CheckWinAudio (proc on the demo model, priority 9): the win poses' scripts
 * put a voice id in cmd_vars[0] (and an alternative in cmd_vars[1], used when he beat a Sonic). */
void ftTs_CheckWinAudio(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if ((u32) fp->motion_id > 5 || !((0x25 >> fp->motion_id) & 1) || fp->cmd_vars[0] == 0) {
        return;
    }
    if (ftTs_DidHeLose(fp->player_idx, "Sonic")) {
        ft_800881D8(fp, fp->cmd_vars[1], 0x7F, 0x40);
    } else {
        ft_800881D8(fp, fp->cmd_vars[0], 0x7F, 0x40);
    }
    fp->cmd_vars[0] = 0;
}
