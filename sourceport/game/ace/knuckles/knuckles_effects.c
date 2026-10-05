/* Knuckles native port working copy; PlKx.dat comparison review in progress. */
/* Akaneia's Sonic: effect helpers (spin ball, speed trail, running shoe colors).
 * Hand-written from PlSn.dat's ftFunction code; see NOTES.md. */
#include "knuckles.h"

#include <math.h>

#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/object.h>
#include <sysdolphin/baselib/tobj.h>

#define ftKx_TAU 6.283185307179586

/* The hit/hitlag callbacks every Sonic special installs: effects die when he is hit and freeze
 * with him in hitlag. (take_dmg_cb and death2_cb share one callback.) */
void ftKx_SetEffectCallbacks(Fighter* fp, HSD_GObjEvent on_hit)
{
    fp->take_dmg_cb = on_hit;
    fp->death2_cb = on_hit;
    fp->take_dmg_2_cb = efLib_DestroyAll;
    fp->pre_hitlag_cb = efLib_PauseAll;
    fp->post_hitlag_cb = efLib_ResumeAll;
}

/* Sonic_UpdateTrailPosAndRot: remember where bone 2 is now and which way he is moving. */
void ftKx_UpdateTrailPosAndRot(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftKnuckles_FighterVars* fv = ftKx_FV(fp);

    lb_8000B1CC(fp->parts[FtPart_XRotN].joint, NULL, &fv->trail_pos);
    fv->trail_angle =
        atan2f(fp->cur_pos.y - fp->prev_pos.y, fp->cur_pos.x - fp->prev_pos.x);
}

/* Sonic_GFXTrail: start a trail. Installed as accessory4_cb; hands over to the per-frame
 * loop. Outside the up smash the trail only spawns while cmd_vars[3] says so (set here). */
void ftKx_GFXTrail(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKx_UpdateTrailPosAndRot(gobj);
    fp->accessory4_cb = ftKx_GFXTrailLoop;
    if (fp->motion_id != ftCo_MS_AttackHi4) {
        fp->cmd_vars[3] = 1;
    }
}

/* Sonic_GFXTrailLoop */
void ftKx_GFXTrailLoop(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKx_SpawnTrailEffect(gobj, fp->cmd_vars[3] == 1);
}

/* SpawnTrailEffect: lay a trail segment from last frame's bone 2 position to this frame's.
 * A segment spawns only when asked, while moving forward, and when the step is short enough
 * (distance squared <= 400) to not smear across a teleport.
 *
 * @p trail_pos and @p trail_angle are where last frame's position and angle are kept, and
 * @p tint the color the segment's material gets (NULL: as modeled). Sonic keeps the two in his
 * fighter variables and tints from his costume (ftKx_SpawnTrailEffect below); Kirby's copy keeps
 * them in Kirby's and tints with a fixed color (sonic_kirby.c). The routine is the same. */
void ftKx_SpawnTrailEffectAt(HSD_GObj* gobj, bool spawn, Vec3* trail_pos, float* trail_angle,
                             const GXColor* tint)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos;
    float angle;

    lb_8000B1CC(fp->parts[FtPart_XRotN].joint, NULL, &pos);

    angle = atan2f(pos.y - trail_pos->y, pos.x - trail_pos->x);
    if (angle < 0.0F) {
        do {
            angle = (float) (angle + ftKx_TAU);
        } while (angle < 0.0F);
    }
    while (angle >= ftKx_TAU) {
        angle = (float) (angle - ftKx_TAU);
    }

    if (trail_pos->x != pos.x || trail_pos->y != pos.y) {
        float forward = fp->self_vel.x * fp->facing_dir;
        float dist2 = ftKx_Vec3_DistSquared(trail_pos, &pos);

        if (spawn && !(forward < 0.0F) && dist2 <= 400.0F) {
            Vec3 zero = { 0.0F, 0.0F, 0.0F };
            EF_Effect* effect = efSync_Spawn(ftKx_Ef_Trail, gobj, &zero, NULL);

            if (effect != NULL) {
                HSD_JObj* root;
                HSD_JObj* head;
                HSD_JObj* tail;
                Vec3 scale;

                /* Leave it in the world rather than following the fighter. */
                effect->parent_gobj = NULL;

                /* The root carries the fighter's scale; move it onto the two ends and put the
                 * head at this frame's position, the tail at last frame's. */
                root = effect->gobj->hsd_obj;
                scale = root->scale;
                root->scale.x = 1.0F;
                root->scale.y = 1.0F;
                root->scale.z = 1.0F;

                head = root->child;
                head->scale = scale;
                head->translate = pos;
                head->rotate.z = angle;

                tail = head->next;
                tail->scale = scale;
                tail->translate = *trail_pos;
                tail->rotate.z = *trail_angle;

                if (tint != NULL) {
                    HSD_DObj* dobj = root->u.dobj;
                    dobj->mobj->mat->diffuse = *tint;
                }
            }
        }
    }

    *trail_pos = pos;
    *trail_angle = angle;
}

/* SpawnTrailEffect, Sonic's own. */
void ftKx_SpawnTrailEffect(HSD_GObj* gobj, bool spawn)
{
    ftKnuckles_FighterVars* fv = ftKx_FV(GET_FIGHTER(gobj));

    if (fv->color != NULL) {
        GXColor tint = fv->color->trail_diffuse;

        ftKx_SpawnTrailEffectAt(gobj, spawn, &fv->trail_pos, &fv->trail_angle, &tint);
    } else {
        ftKx_SpawnTrailEffectAt(gobj, spawn, &fv->trail_pos, &fv->trail_angle, NULL);
    }
}

/* Sonic_GFXSpin: the spin ball, once per move (x2219_b0 marks it spawned). Installed as
 * accessory4_cb and removes itself. */
void ftKx_GFXSpin(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x2219_b0) {
        HSD_JObj* joint = fp->parts[FtPart_TransN].joint;

        efSync_Spawn(ftKx_Ef_Spin, gobj, joint);
        /* The m-ex code writes the fighter's own bone scale (not the effect's) directly,
         * without marking the matrix dirty. */
        joint->scale.x = fp->co_attrs.model_scaling;
        joint->scale.y = fp->co_attrs.model_scaling;
        joint->scale.z = fp->co_attrs.model_scaling;
        fp->x2219_b0 = true;
        fp->pre_hitlag_cb = efLib_PauseAll;
        fp->post_hitlag_cb = efLib_ResumeAll;
    }
    fp->accessory4_cb = NULL;
}

/* Sonic_GFXSpinAndTrail */
void ftKx_GFXSpinAndTrail(HSD_GObj* gobj)
{
    ftKx_GFXSpin(gobj);
    ftKx_GFXTrail(gobj);
}

/* Sonic_GFXSpinAndTrailVelocityDirection: as above, with the trail angle taken from velocity. */
void ftKx_GFXSpinAndTrailVelocityDirection(HSD_GObj* gobj)
{
    Fighter* fp;

    ftKx_GFXSpin(gobj);
    ftKx_GFXTrail(gobj);
    fp = GET_FIGHTER(gobj);
    ftKx_FV(fp)->trail_angle = atan2f(fp->self_vel.y, fp->self_vel.x);
}

/* RunEffectCallback: the running shoe blur follows the fighter's animation frame and scale.
 * Its texture animation is posed on the fighter's frame and then frozen (MexTK's
 * JOBJ_PauseOnFrame over texture anims). */
void ftKx_RunEffectCallback(EF_Effect* effect)
{
    HSD_JObj* jobj = effect->gobj->hsd_obj;
    Fighter* fp = GET_FIGHTER((HSD_GObj*) effect->user_data);

    HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjReqAnim, AOBJ_ARG_AF,
                    fp->x3E4_fighterCmdScript.frame_count);
    HSD_JObjAnimAll(jobj);
    /* m-ex passes arg kind 6 with two zero words here; the effect is a rate of zero. */
    HSD_ForeachAnim(jobj, JOBJ_TYPE, TOBJ_MASK, HSD_AObjSetRate, AOBJ_ARG_AF, 0.0F);

    jobj->scale.x = fp->x34_scale.y;
    jobj->scale.y = fp->x34_scale.y;
    jobj->scale.z = fp->x34_scale.y;
    HSD_JObjSetMtxDirtySub(jobj);
}

/* Color_Shoes: set the TEV konst color of the first texture on the jobj's dobj_index-th dobj
 * (MexTK's JOBJ_GetDObjChild walk, with its "dobj not found!" assert). */
void ftKx_ColorShoes(HSD_JObj* jobj, int dobj_index, ftKnuckles_ColorData* color)
{
    HSD_DObj* dobj = jobj->u.dobj;
    int i;

    for (i = dobj_index; i > 0; i--) {
        HSD_ASSERTMSG(__LINE__, dobj->next != NULL, "dobj not found!");
        dobj = dobj->next;
    }

    /* The m-ex code reads the color unconditionally (Akaneia costumes always carry it); a
     * missing PlySonicColor (no costume-archive hook) leaves the shoes as modeled. */
    if (color == NULL) {
        return;
    }
    if (dobj != NULL && dobj->mobj != NULL && dobj->mobj->tobj != NULL &&
        dobj->mobj->tobj->tev != NULL)
    {
        dobj->mobj->tobj->tev->konst = color->shoe_konst;
    }
}
