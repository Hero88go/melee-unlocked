/* GrFc.dat +C3C..F88, +13AC..1604 and +16D0..1BF0. */
#include "fichina_codec.h"
#include "../common/mu_gr_mex.h"
#include <melee/cm/camera.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/kinds/ftFox/forward.h>
#include <melee/ft/types.h>
#include <melee/gm/gmscene.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/grdisplay.h>
#include <melee/gr/ground.h>
#include <melee/if/ifall.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/synth.h>
#include <sysdolphin/baselib/tobj.h>
#include <string.h>

/* Public functions declared by the interface module. */
extern void un_802FF570(void);
extern void un_802FF620(void);
typedef struct FcCodec { int state, timer; const FcDialogue* dialogue; HSD_Text* text; } FcCodec;
#define CODEC(gobj) ((FcCodec*) (void*) &((Ground*) HSD_GObjGetUserData(gobj))->u)
static const FcDialogue fc_fox[] = {
    {7,0,2,2}, {4,0,3,3}, {10,0,4,4}, {6,1,5,5}, {5,2,6,6}, {4,0,7,7}, {4,0,8,8}, {-1,0,0,0}
};
static const FcDialogue fc_falco[] = {
    {5,2,9,9}, {3,0,10,10}, {7,0,11,11}, {6,1,12,12}, {9,0,13,13}, {6,1,14,14}, {10,0,15,15}, {-1,0,0,0}
};
static const FcDialogue fc_wolf[] = {
    {4,0,16,16}, {8,0,17,17}, {5,1,18,18}, {10,0,19,19}, {6,1,20,20},
    {5,2,21,21}, {3,0,22,22}, {5,2,23,23}, {3,0,24,24}, {-1,0,0,0}
};
static void fc_taunt_enter(HSD_GObj* gobj);
static void fc_taunt_damage(HSD_GObj* gobj)
{
    Fighter* fp = HSD_GObjGetUserData(gobj);
    HSD_GObj* codec;
    fc_codec_player()->index = -1;
    codec = Ground_GetMapGObj(9);
    if (codec != NULL) Ground_801C4A08(codec);
    fp->take_dmg_cb = NULL;
}
static void fc_taunt_anim(HSD_GObj* gobj)
{
    Fighter* fp = HSD_GObjGetUserData(gobj);
    if (fp->throw_flags_b3) {
        fp->throw_flags_b3 = 0;
        fc_codec_player()->index = 0;
        fp->take_dmg_cb = fc_taunt_damage;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        fp->mv.fx.AppealS.animCount++;
        if (fp->mv.fx.AppealS.animCount > 2) ft_8008A2BC(gobj);
        else fc_taunt_enter(gobj);
    }
}
static void fc_taunt_enter(HSD_GObj* gobj)
{
    static const int motions[2][3] = {
        { ftFx_MS_AppealSStartR, ftFx_MS_AppealSR, ftFx_MS_AppealSEndR },
        { ftFx_MS_AppealSStartL, ftFx_MS_AppealSL, ftFx_MS_AppealSEndL },
    };
    Fighter* fp = HSD_GObjGetUserData(gobj);
    Fighter_ChangeMotionState(gobj, motions[fp->mv.fx.AppealS.facingDir][fp->mv.fx.AppealS.animCount],
                              0, 0.0f, 1.0f, 0.0f, NULL);
    fp->anim_cb = fc_taunt_anim;
}
static void fc_taunt_start(HSD_GObj* gobj, int slot)
{
    Fighter* fp = HSD_GObjGetUserData(gobj);
    FcCodecPlayer* player;
    const FcDialogue* sequence;
    if (fp->motion_id != 14 || (fp->input.held_buttons[0] & 4) || fp->x682 != 1) return;
    player = fc_codec_player();
    if (fp->kind == 1) sequence = fc_fox;
    else if (fp->kind == 22) sequence = fc_falco;
    else {
        const char* name = mu_gr_mex_fighter_name(Player_GetPlayerCharacter(slot));
        if (name == NULL || strcmp("Wolf", name) != 0) return;
        sequence = fc_wolf;
    }
    player->sequence = sequence; player->used = 1;
    fp->mv.fx.AppealS.facingDir = fp->facing_dir != 1.0f;
    fp->mv.fx.AppealS.animCount = 0;
    fp->throw_flags = 0;
    fc_taunt_enter(gobj);
}
void fc_codec_create(Ground_GObj* gobj)
{
    HSD_JObj* joint = GET_JOBJ(gobj);
    joint->scale.x = joint->scale.y = joint->scale.z = 1.0f;
    HSD_JObjSetMtxDirtySub(joint);
    gobj->render_cb = fc_codec_draw;
}
void fc_codec_draw(HSD_GObj* gobj, intptr_t pass)
{
    FcCodec* cv = CODEC(gobj);
    bool hidden = gm_GetDbPauseFlag(1) || gm_GetDbPauseFlag(2) || Camera_8003010C();
    if (cv->text != NULL) cv->text->hidden = hidden;
    if (!hidden) grDisplay_801C5DB0(gobj, pass);
}
void fc_codec_check_spawn(FcCodecPlayer* player)
{
    if (player->index == -1) {
        int slot;
        if (player->used) return;
        for (slot = 0; slot < 6; slot++) {
            HSD_GObj* fighter;
            if (Player_GetPlayerSlotType(slot) == 3) continue;
            fighter = Player_GetEntity(slot);
            if (fighter != NULL) fc_taunt_start(fighter, slot);
        }
    } else {
        const FcDialogue* dialogue;
        HSD_GObj* codec;
        ifAll_802F3394(); HSD_SynthStreamSetVolume(0.6f); un_802FF570();
        if (Ground_GetMapGObj(9) != NULL) return;
        dialogue = &player->sequence[player->index];
        if (dialogue->portrait < 0) {
            player->index = -1;
            ifAll_802F33CC(); HSD_SynthStreamSetVolume(1.0f); un_802FF620();
            return;
        }
        codec = mu_grTSeak_80223908(9);
        if (codec == NULL) __assert("fichina.c", 0x160, "could not create codec");
        CODEC(codec)->state = 0; CODEC(codec)->timer = 10; CODEC(codec)->dialogue = dialogue;
        grAnime_801C8138(codec, ((Ground*) HSD_GObjGetUserData(codec))->map_id, 0);
        player->index++;
    }
}
void fc_codec_frame(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    FcCodec* cv = CODEC(gobj);
    if (cv->state == 0) {
        if (cv->timer == 0) Ground_801C53EC(178);
        cv->timer--;
        if (grAnime_801C83D0(gobj, 1, 2)) {
            UnkArchiveStruct* archive;
            HSD_JObj *portrait, *texture;
            HSD_MatAnimJointSlot* slots;
            HSD_MatAnimJoint* anim;
            cv->state = 1; cv->timer = 60;
            grAnime_801C8138(gobj, gp->map_id, 1);
            archive = grDatFiles_801C6330(gp->map_id);
            if (archive == NULL) __assert("fichina_codec.c", 0x1D9, "stage gobj not found");
            portrait = Ground_801C3FA4(gobj, 1);
            slots = DP(DP(archive->unk4->unk8)[gp->map_id].unk8);
            anim = DP(slots[cv->dialogue->portrait]);
            HSD_JObjAddAnim(portrait, NULL, DP(anim->child), NULL);
            HSD_JObjReqAnim(portrait, 0.0f); HSD_JObjAnim(portrait);
            texture = Ground_801C3FA4(gobj, 3);
            texture->u.dobj->mobj->tobj->aobj->framerate = 0.0f;
            HSD_JObjReqAnim(texture, (f32) cv->dialogue->texture);
            HSD_SisLib_803A611C(1, NULL, 9, 13, 0, 1, 0, 7);
            cv->text = HSD_SisLib_803A5ACC(1, 0, 140.0f, 380.0f, 0.0f, 288.0f, 96.0f);
            cv->text->text_color = (GXColor) {255,255,255,255};
            HSD_SisLib_803A6368(cv->text, cv->dialogue->text);
            Ground_801C5440(gp, 0, fc_bank() + cv->dialogue->sound);
        }
    } else if (cv->state == 1) {
        cv->timer--;
        if (cv->timer < 0 && !Ground_801C54DC(gp, 0)) {
            cv->state = 2;
            grAnime_801C8138(gobj, gp->map_id, 2);
            HSD_SisLib_803A5DA0(1);
        }
    } else if (cv->state == 2 && grAnime_801C83D0(gobj, 1, 2)) Ground_801C4A08(gobj);
}
