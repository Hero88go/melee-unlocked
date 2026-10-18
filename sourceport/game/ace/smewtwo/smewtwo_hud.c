/* PlSm.dat +0AE4, +23B8..+2634, +351C..+35A8. */
#include "smewtwo.h"
#include <melee/ft/inlines.h>
#include <melee/pl/player.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmtoulib.h>
#include <melee/if/ifall.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbspdisplay.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>

typedef struct ftSm_Hud { HSD_GObj* fighter; int player; } ftSm_Hud;
static float ftSm_HudFrame(HSD_JObj* joint)
{
    for(;joint!=NULL;joint=joint->child) {
        if(joint->aobj) return joint->aobj->curr_frame;
        for(HSD_DObj* d=joint->u.dobj;d!=NULL;d=d->next) {
            if(d->aobj) return d->aobj->curr_frame;
            HSD_MObj* m=d->mobj;
            if(m->aobj) return m->aobj->curr_frame;
            for(HSD_TObj* t=m->tobj;t!=NULL;t=t->next)
                if(t->aobj) return t->aobj->curr_frame;
        }
    }
    return -1;
}
static void ftSm_HudProc(HSD_GObj* gobj)
{
    HSD_JObj* joint=gobj->hsd_obj;
    HSD_JObj* meter=NULL;
    ftSm_Hud* data=gobj->user_data;
    lb_80011E24(joint,&meter,1,-1);
    if(data->fighter==NULL || Player_GetStocks(data->player)<=0 || gm_GetDbPauseFlag(1)==2) {
        HSD_JObjSetFlagsAll(joint,JOBJ_HIDDEN);
        return;
    }
    Fighter* fp=GET_FIGHTER(data->fighter);
    ftSm_Vars* v=ftSm_GetVars(fp);
    joint->translate=*ifAll_GetPlayerHUDPosition(fp->player_idx);
    if(v->transformed) {
        int power=v->power;
        if(power>10) HSD_JObjReqAnimAll(meter,100);
        else if(power<0) HSD_JObjReqAnimAll(meter,0);
        else {
            float target=(float)(power*10);
            float frame=ftSm_HudFrame(meter);
            if(frame<target) HSD_JObjReqAnimAll(meter,frame+1);
            frame=ftSm_HudFrame(meter);
            if(frame>target) HSD_JObjReqAnimAll(meter,frame-1);
        }
    } else if(v->energy>60) HSD_JObjReqAnimAll(meter,100);
    else {
        float target=(v->energy/6.0f)*10.0f;
        float frame=ftSm_HudFrame(meter);
        if(frame<target) HSD_JObjReqAnimAll(meter,frame+1);
        frame=ftSm_HudFrame(meter);
        if(frame>target) HSD_JObjReqAnimAll(meter,frame-1);
    }
    HSD_JObjAnimAll(meter);
    HSD_JObjClearFlagsAll(joint,JOBJ_HIDDEN);
    HSD_JObjSetMtxDirtySub(joint);
}
HSD_GObj* ftSm_Hud_Create(HSD_GObj* fighter)
{
    typedef DISC_PTR(DynamicModelDesc) ModelSlot;
    HSD_Archive* archive=lbArchive_LoadArchive("PlSmHUD.dat");
    ModelSlot* models=HSD_ArchiveGetPublicAddress(archive,"MgMtr_scene_models");
    HSD_GObj* gobj=fn_8019035C(false,DP(models[0]),0,0,11,true,ftSm_HudProc,0);
    ftSm_Hud* data=HSD_MemAlloc(sizeof(*data));
    GObj_InitUserData(gobj,4,HSD_Free,data);
    data->fighter=fighter;
    data->player=GET_FIGHTER(fighter)->player_idx;
    return gobj;
}
