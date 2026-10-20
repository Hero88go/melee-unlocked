#include "native_intro.h"
#include "native_destination.h"
#include "rogue_mode.h"
#include "debug_launch.h"
#include "ui_text.h"
#include "../../core/native_route_view.h"
#include "../../core/play_context.h"
#include "../../encounters/encounter_registry.h"
#include "../../director/rogue_director.h"
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmscene.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/pl/player.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <melee/lb/lbspdisplay.h>
#include <dolphin/os.h>
#include <string.h>

typedef char IntroLayoutCheck[sizeof(RogueNativeIntroData)==0x20 ? 1 : -1];
RogueNativeIntroData rogue_native_intro;
static unsigned held,frames,entry_hash;
#if ROGUE_DEBUG
static unsigned route_idle_frames,route_idle_hash;
#endif
/* Native SIS scene teardown owns these text allocations. End drops handles. */
static RogueText labels;
static unsigned labels_ready;
static unsigned focus_entry, focus_side, focus_phase;
static unsigned side_entry[2],side_end[2];
static unsigned kit_entry[2];
static unsigned kit_status_entry[2];
static unsigned name_focus_stamp[6];
static unsigned backdrop_focus_stamp;
static unsigned departure_ticks,departure_side;
static RogueNativeRouteView route_view;
static unsigned run_hash(void)
{
    const unsigned char* bytes=(const unsigned char*)RogueDirector_Run();
    unsigned i,hash=2166136261U;
    for(i=0;i<sizeof(RogueRun);++i) hash=(hash^bytes[i])*16777619U;
    return hash;
}
int RogueNativeIntro_Prepare(unsigned port)
{
    const RogueRun* run=RogueDirector_Run();
    struct GameCache* cache;
    const unsigned* roster[2]={NULL,NULL};
    unsigned roster_count[2]={0,0};
    unsigned i,count=0;
    u64 audio=0;
    if(port>=4 || RogueRuntime_Get()->match_active ||
       (run->phase!=ROGUE_REWARD && run->phase!=ROGUE_ROUTE)) return 0;
    for(i=0;i<2;++i) {
        const RogueNodeDesc* node=&run->preview[i];
        const RogueEncounter* combat=node->kind<=ROGUE_BOSS ? RogueNode_Combat(node) : NULL;
        unsigned j;
        if(combat) {
            if(!combat->enemy_count || combat->enemy_count>3 ||
               !combat->recipe || combat->recipe>ROGUE_RECIPES || !combat->stage || combat->stage>6) return 0;
            for(j=0;j<combat->enemy_count;++j) if(combat->fighters[j]>=26) return 0;
            roster[i]=combat->fighters;roster_count[i]=combat->enemy_count;
        } else if(node->kind==ROGUE_NODE_EVENT) {
            if(node->payload.event.template_id<1 || node->payload.event.template_id>7) return 0;
            if(node->payload.event.template_id==ROGUE_EVENT_GAUNTLET) {
                const RogueEventDesc* event=&node->payload.event;
                if(!event->dangerous) return 0;
                for(j=0;j<3;++j) {
                    unsigned stage=event->combat_stages[j];
                    if(event->opponents[j]>=26 ||
                       (stage!=31 && stage!=32 && stage!=28 && stage!=8 && stage!=2 && stage!=3)) return 0;
                }
                roster[i]=event->opponents;roster_count[i]=3;
                OSReport("[rogue] native_gauntlet_roster side=%u fighters=%u,%u,%u stages=%u,%u,%u sequential=1\n",
                    i,roster[i][0],roster[i][1],roster[i][2],event->combat_stages[0],event->combat_stages[1],event->combat_stages[2]);
            }
        } else if(node->kind!=ROGUE_NODE_SHOP && node->kind!=ROGUE_NODE_REST) return 0;
    }
    if(!RogueNativeRoute_Build(run,&route_view)) return 0;
    memset(&rogue_native_intro,0,sizeof(rogue_native_intro));
    rogue_native_intro.port=(unsigned char)port;
    rogue_native_intro.nametag=0x78; /* Native sentinel: display fighter name. */
    rogue_native_intro.map_stop=(unsigned char)route_view.native_stop;
    rogue_native_intro.left_count=(unsigned char)roster_count[0];
    rogue_native_intro.right_count=(unsigned char)roster_count[1];
    memset(rogue_native_intro.left,0x21,3);
    memset(rogue_native_intro.right,0x21,3);
    cache=&lbDvd_GetPreloadCacheScene()->game_cache;
    lbDvd_80018C6C();
    for(i=0;i<rogue_native_intro.left_count;++i) {
        rogue_native_intro.left[i]=(unsigned char)roster[0][i];
        cache->entries[count].char_id=roster[0][i];
        cache->entries[count++].color=0;
        audio|=lbAudioAx_80026E84(roster[0][i]);
    }
    lbDvd_80018254(); lbDvd_80018C2C(0xC7); lbDvd_80017700(4);
    for(i=0;i<rogue_native_intro.right_count;++i) {
        rogue_native_intro.right[i]=(unsigned char)roster[1][i];
        cache->entries[count].char_id=roster[1][i];
        cache->entries[count++].color=0;
        audio|=lbAudioAx_80026E84(roster[1][i]);
    }
    /* One additional costume may be needed for the committed player demo.
     * Load before control, never from the confirm-button path. */
    cache->entries[count].char_id=run->character;
    cache->entries[count].color=run->costume;
    audio|=lbAudioAx_80026E84(run->character);
    cache->stkind=31;
    lbDvd_80018254();
    lbAudioAx_80026F2C(0x1C); lbAudioAx_8002702C(0xC,audio); lbAudioAx_80027168();
    frames=0; entry_hash=run_hash(); held=1;
#if ROGUE_DEBUG
    route_idle_frames=0; route_idle_hash=0;
#endif
    memset(name_focus_stamp,0,sizeof(name_focus_stamp));
    backdrop_focus_stamp=~0U;
    departure_ticks=0;
    OSReport("[rogue] native_map_page floor=%u act=%u stop=%u\n",run->floor,route_view.act,route_view.native_stop);
    OSReport("[rogue] native_intro_prepare left=%u right=%u map_stop=%u hash=%x\n",
        rogue_native_intro.left_count,rogue_native_intro.right_count,route_view.native_stop,entry_hash);
    return 1;
}
int RogueNativeIntro_Held(void)
{
    return held && RogueRuntime_IsActive();
}
unsigned RogueNativeIntro_PlayerCharacter(void)
{ return RogueDirector_Run()->character; }
void RogueNativeIntro_Commit(unsigned side)
{
    if(!RogueNativeIntro_Held() || side>1 || departure_ticks ||
       RogueDirector_Run()->phase!=ROGUE_FIGHT)
        OSPanic(__FILE__,__LINE__,"invalid native departure");
    departure_side=side;
    departure_ticks=1;
}
void RogueNativeIntro_Frame(void)
{
    unsigned i,actors=0,demos=0,hold_frames=1800;
#if ROGUE_DEBUG && ROGUE_QA_MAP_SWEEP
    unsigned floor=RogueDirector_Run()->floor;
    if(floor!=4 && floor!=5 && floor!=9 && floor!=10 && floor!=14) hold_frames=120;
#endif
    if(!RogueNativeIntro_Held() || RogueRuntime_Get()->scene!=GS_INTRO_EASY) return;
    if(departure_ticks) {
        if(departure_ticks==1) {
            const RogueRun* run=RogueDirector_Run();
            unsigned rejected=1-departure_side;
#if ROGUE_DEBUG
            Fighter_GObj* preserved[3][2];
            unsigned first=departure_side ? rogue_native_intro.left_count : 0;
            unsigned count=departure_side ? rogue_native_intro.right_count : rogue_native_intro.left_count;
            unsigned j,sub,primaries=0,partners=0;
            for(j=0;j<count;++j) for(sub=0;sub<2;++sub)
                preserved[j][sub]=Player_GetEntityAtIndex(first+j,sub);
#endif
            RogueNativeDestination_Hide(rejected);
            RogueNativeIntro_ReplaceSide(rejected,run->character,run->costume);
#if ROGUE_DEBUG
            /* Frame-local identity proof: never retain demo pointers beyond
             * replacement or across a scene transition. */
            for(j=0;j<count;++j) for(sub=0;sub<2;++sub)
                if(preserved[j][sub]!=Player_GetEntityAtIndex(first+j,sub))
                    OSPanic(__FILE__,__LINE__,"selected native demo changed");
            for(j=0;j<6;++j) {
                if(Player_GetEntity(j)) ++primaries;
                if(Player_GetEntityAtIndex(j,1)) ++partners;
            }
            if(primaries!=count+1)
                OSPanic(__FILE__,__LINE__,"rejected native demos survived");
            OSReport("[rogue] native_group_replace selected=%u primaries=%u partners=%u preserved=1\n",
                count,primaries,partners);
#endif
            if(rejected) rogue_native_intro.right_count=1;
            else rogue_native_intro.left_count=1;
            for(i=side_entry[rejected];i<side_end[rejected];++i)
                HSD_SisLib_803A70A0(labels.native,i,"%s","");
            HSD_SisLib_803A70A0(labels.native,focus_entry,"%s",departure_side==0 ? "SELECTED ENCOUNTER" : "PLAYER");
            HSD_SisLib_803A70A0(labels.native,focus_entry+1,"%s",departure_side==1 ? "SELECTED ENCOUNTER" : "PLAYER");
            OSReport("[rogue] native_player_insert selected_side=%u player_side=%u character=%u costume=%u resources=%u\n",
                departure_side,rejected,run->character,run->costume,RogueRuntime_Get()->match_resources);
        }
        if(++departure_ticks>=90) {
            gm_SetNextGameModeStateId(2);
            held=0;
            RogueNativeIntro_ReleaseTimeline();
            OSReport("[rogue] native_departure_release side=%u\n",departure_side);
        }
        return;
    }
    ++frames;
#if ROGUE_DEBUG
    /* The reward claim legitimately changes the run. Start a separate
     * value-only baseline when destination selection first becomes active. */
    if(RogueDirector_Run()->phase==ROGUE_ROUTE) {
        if(!route_idle_frames) route_idle_hash=run_hash();
        ++route_idle_frames;
        if(route_idle_frames==1 || route_idle_frames==1801) {
            unsigned route_actors=0,route_demos=0;
            for(i=0;i<6;++i) if(Player_GetEntity(i)) {
                ++route_actors;
                if(Player_GetPlayerSlotType(i)==Gm_PKind_Demo) ++route_demos;
            }
            OSReport("[rogue] native_route_idle frames=%u actors=%u demos=%u unchanged=%u resources=%u\n",
                route_idle_frames,route_actors,route_demos,
                run_hash()==route_idle_hash,RogueRuntime_Get()->match_resources);
        }
    } else route_idle_frames=0;
#endif
    if(frames==120 || frames==1800) {
        for(i=0;i<6;++i) {
            Fighter_GObj* entity=Player_GetEntity(i);
            if(entity) { actors++; if(Player_GetPlayerSlotType(i)==Gm_PKind_Demo) demos++; }
        }
        OSReport("[rogue] native_intro_idle frames=%u actors=%u demos=%u unchanged=%u resources=%u\n",
            frames,actors,demos,run_hash()==entry_hash,RogueRuntime_Get()->match_resources);
#if ROGUE_DEBUG
        if(labels_ready && labels.native) {
            HSD_Text* text=labels.native;
            const unsigned char* data=(const unsigned char*)text->sis_buffer;
            unsigned length=(unsigned char*)text->alloc_data->next-data;
            unsigned hash=2166136261U;
            for(i=0;i<length;++i) hash=(hash^data[i])*16777619U;
            OSReport("[rogue] native_text_state frame=%u bytes=%u hash=%08x stack=%u capacity=%u skip=%u hidden=%u depth=%u scale=%f,%f fit=%f color=%02x%02x%02x%02x\n",
                frames,length,hash,text->x6C,text->x6E,text->x98,text->hidden,text->x4C,
                text->x80.x,text->x80.y,text->x88,text->active_color.r,text->active_color.g,
                text->active_color.b,text->active_color.a);
        }
#endif
    }
#if !ROGUE_QA_NATIVE_INTRO || ROGUE_QA_NATIVE_DEPARTURE
    if(frames>=120) RogueMode_NativeRouteFrame();
    return;
#endif
    if(frames>=hold_frames && (HSD_PadMasterStatus[rogue_native_intro.port].trigger&HSD_PAD_START)) {
#if ROGUE_DEBUG && ROGUE_QA_MAP_SWEEP
        if(floor<14) {
            RogueRun* run=RogueDirector_Run();
            unsigned side=run->preview[0].kind==ROGUE_NODE_EVENT ? 1 : 0;
            if(run->phase==ROGUE_REWARD && !RogueRun_ChooseUpgrade(run,0))
                OSPanic(__FILE__,__LINE__,"map sweep reward failed");
            if(!RogueRun_ChooseRoute(run,side)) OSPanic(__FILE__,__LINE__,"map sweep route failed");
            if(run->phase==ROGUE_FIGHT) {
                if(!RogueRun_MatchEnd(run,1,0)) OSPanic(__FILE__,__LINE__,"map sweep result failed");
            } else if(!RogueRun_LeaveService(run)) OSPanic(__FILE__,__LINE__,"map sweep service failed");
            OSReport("[rogue] native_map_advance from=%u to=%u synthetic=1\n",floor,run->floor);
            gm_SetNextGameModeStateId(3); gm_801A4B60();
            return;
        }
#endif
        gm_ChangeGameModeAfterCurrentScene(GM_MENU); gm_801A4B60();
    }
}
void RogueNativeIntro_End(void)
{
    RogueMode_NativeOverlayEnd();
    held=labels_ready=departure_ticks=0;
    memset(&labels,0,sizeof(labels));
}

void RogueNativeIntro_Standby(void* root)
{
    HSD_JObj* label=NULL;
    HSD_DObj* dobj;
    HSD_ImageDesc* image;
    unsigned i;
    if(!RogueNativeIntro_Held()) return;
    /* IrAls.dat / ScItrAllstar_scene_data model 0, joint 60.
     * Decoded texture and archive hashes: wp5-loading-label-source.json. */
    lb_80011E24(root,&label,60,-1);
    dobj=HSD_JObjGetDObj(label);
    image=dobj && dobj->mobj && dobj->mobj->tobj ? dobj->mobj->tobj->imagedesc : NULL;
    if(!image || image->width!=225 || image->height!=20 || image->format!=GX_TF_IA4)
        OSPanic(__FILE__,__LINE__,"native loading-label layout mismatch");
    if(departure_ticks>=60) HSD_JObjClearFlagsAll(label,JOBJ_HIDDEN);
    else HSD_JObjSetFlagsAll(label,JOBJ_HIDDEN);
    /* Decoded retail S/T/A/G/E textures: wp5-stage-caption-source.json.
     * Native act/floor text replaces the original stage caption for Rogue. */
    for(i=9;i<=13;++i) {
        lb_80011E24(root,&label,i,-1);
        HSD_JObjSetFlagsAll(label,JOBJ_HIDDEN);
    }
    lb_80011E24(root,&label,18,-1); HSD_JObjSetFlagsAll(label,JOBJ_HIDDEN);
    lb_80011E24(root,&label,19,-1); HSD_JObjSetFlagsAll(label,JOBJ_HIDDEN);
    /* Native fn_8018325C animates these two ally markers; option groups
     * are future opponents, so neither marker has truthful meaning here. */
    lb_80011E24(root,&label,0x3A,-1);
    HSD_JObjSetFlagsAll(label,JOBJ_HIDDEN);
    lb_80011E24(root,&label,0x3B,-1);
    HSD_JObjSetFlagsAll(label,JOBJ_HIDDEN);
}

float RogueNativeIntro_GroupY(unsigned count, unsigned index, float native_y)
{
    /* The retail three-member row stages allies above the player. Future
     * encounter members are peers; lower the two rear demos into the same
     * native viewport. These offsets are Rogue presentation tuning only. */
    static const float group_y[3]={1.0f,3.0f,5.0f};
    if(RogueNativeIntro_Held() && count==3 && index<3) return group_y[index];
    return native_y;
}

void RogueNativeIntro_Labels(void)
{
    const RogueRun* run=RogueDirector_Run();
    unsigned side;
    static const char* kinds[7]={"FIGHT","ELITE","BOSS","SHOP","REST","EVENT","CHOICE"};
    static const float map_x[5]={45,135,278,420,550};
    static const char* stages[6]={"Battlefield","Final Destination","Dream Land",
        "Yoshi's Story","Fountain of Dreams","Pokemon Stadium"};
    static const char* events[8]={"","HOME-RUN CONTEST","BREAK THE TARGETS",
        "RACE TO THE FINISH","MUSHROOM KINGDOM DASH","BRINSTAR ESCAPE",
        "ALL-STAR GAUNTLET","SANDBAG COMBO LAB"};
    static const char* objectives[8]={"","Launch Sandbag for distance",
        "Break every target","Reach the finish",
        "Reach the stage exit","Escape before time expires",
        "Win three consecutive rounds","Damage, variety and combos"};
    if(!RogueNativeIntro_Held() || labels_ready) return;
    HSD_SisLib_803A62A0(4,"SdMenu.usd","SIS_MenuData");
    /* Native names use GX link 14 / camera priority 18. A second camera on
     * that link would render both owners twice. Give Rogue labels a distinct
     * link and render them after the native presentation. */
    HSD_SisLib_803A611C(4,NULL,9,0x14,0,0xF,0,0x13);
    RogueText_CreateSized(&labels,4,0,2048);
    RogueText_Begin(&labels);
    RogueText_Line(&labels,25,6,"ACT %u / 3    FLOOR %u / 15",route_view.act,route_view.floor+1);
    RogueText_Style(&labels,0.6f,0xFFFFFF);
    for(side=0;side<5;++side) {
        const RogueNativeRouteSlot* slot=&route_view.slots[side];
        if(slot->kind==ROGUE_ROUTE_CHOICE)
            RogueText_Line(&labels,map_x[side],101,"%u %s/%s",slot->floor+1,
                kinds[slot->alternatives[0]],kinds[slot->alternatives[1]]);
        else RogueText_Line(&labels,map_x[side],101,"%u %s",slot->floor+1,kinds[slot->kind]);
        RogueText_Style(&labels,0.38f,slot->state==ROGUE_ROUTE_CURRENT ? 0xF6CD36 :
            slot->state==ROGUE_ROUTE_CLEARED ? 0x90D8AA : slot->state==ROGUE_ROUTE_FAILED ? 0xFF8585 : 0xD1D5DF);
    }
    for(side=0;side<2;++side) {
        const RogueNodeDesc* node=&run->preview[side];
        kit_entry[side]=~0U;
        const RogueEncounter* combat=node->kind<=ROGUE_BOSS ? RogueNode_Combat(node) : NULL;
        float x=side ? 340.0f : 25.0f;
        side_entry[side]=labels.used;
        if(combat) {
            /* Native fighter names end above this compact footer. Values are
             * the generated encounter, including mutations/counter-builds,
             * rather than the unmodified recipe defaults. */
            RogueText_Line(&labels,x,410,"%s / %s",stages[combat->stage-1],
                rogue_recipes[combat->recipe-1].name);
            RogueText_Style(&labels,0.44f,0xFFFFFF);
#if ROGUE_DEBUG
            OSReport("[rogue] native_intro_stage side=%u stage=%u label=%s entry=%u\n",
                side,combat->stage,stages[combat->stage-1],labels.used-1);
#endif
            RogueText_Line(&labels,x,422,"%s",rogue_recipes[combat->recipe-1].rule);
            RogueText_Style(&labels,0.42f,0xF6CD36);
            RogueText_Line(&labels,x,434,"Power %u%%   Defense %u%%",combat->damage,combat->defense);
            RogueText_Style(&labels,0.44f,0xFFFFFF);
            RogueText_Line(&labels,x,446,"Speed %u%%   Size %u%%",combat->speed,combat->scale);
            RogueText_Style(&labels,0.44f,0xFFFFFF);
            if(combat->enemy_count==3)
                RogueText_Line(&labels,x,458,"Stocks %u / %u / %u   Start %u%%",
                    combat->stocks[0],combat->stocks[1],combat->stocks[2],combat->starting_percent);
            else if(combat->enemy_count==2)
                RogueText_Line(&labels,x,458,"Stocks %u / %u   Start %u%%",
                    combat->stocks[0],combat->stocks[1],combat->starting_percent);
            else RogueText_Line(&labels,x,458,"Stocks %u   Start %u%%",
                combat->stocks[0],combat->starting_percent);
            RogueText_Style(&labels,0.44f,0xFFFFFF);
#if ROGUE_DEBUG
            OSReport("[rogue] native_combat_preview side=%u recipe=%u actors=%u power=%u defense=%u speed=%u scale=%u percent=%u stocks=%u,%u,%u\n",
                side,combat->recipe,combat->enemy_count,combat->damage,combat->defense,
                combat->speed,combat->scale,combat->starting_percent,
                combat->stocks[0],combat->stocks[1],combat->stocks[2]);
#endif
        } else if(node->kind==ROGUE_NODE_EVENT && node->payload.event.template_id==ROGUE_EVENT_GAUNTLET) {
            const RogueEventDesc* event=&node->payload.event;
            const char* stage_names[3];
            unsigned round;
            for(round=0;round<3;++round) {
                unsigned stage=event->combat_stages[round];
                stage_names[round]=stage==31 ? "BF" : stage==32 ? "FD" : stage==28 ? "DL" :
                    stage==8 ? "YS" : stage==2 ? "FoD" : "PS";
            }
            RogueText_Line(&labels,x,410,"ALL-STAR MINI-GAUNTLET");
            RogueText_Style(&labels,0.55f,0xF6CD36);
            RogueText_Line(&labels,x,422,"3 rounds, one opponent at a time");
            RogueText_Style(&labels,0.44f,0xFFFFFF);
            RogueText_Line(&labels,x,434,"DANGER: defeat ends the run");
            RogueText_Style(&labels,0.44f,0xFF8585);
            RogueText_Line(&labels,x,446,"Stocks and damage carry across rounds");
            RogueText_Style(&labels,0.42f,0xD1D5DF);
            RogueText_Line(&labels,x,458,"Round stages: %s / %s / %s",stage_names[0],stage_names[1],stage_names[2]);
            RogueText_Style(&labels,0.44f,0xFFFFFF);
            OSReport("[rogue] native_event_preview side=%u template=6 timer=%u dangerous=%u assist=%u objective=Win three consecutive rounds\n",
                side,event->timer_frames,event->dangerous,event->native_kit_assist);
        } else {
            const char* title=node->kind==ROGUE_NODE_SHOP ? "SHOP" :
                node->kind==ROGUE_NODE_REST ? "REST" : events[node->payload.event.template_id];
            /* Leave the native center VS insignia clear of event details. */
            if(side && node->kind==ROGUE_NODE_EVENT) x=365.0f;
            RogueText_Line(&labels,x,node->kind==ROGUE_NODE_EVENT ? 330 : 345,"%s",title);
            RogueText_Style(&labels,node->kind==ROGUE_NODE_EVENT ? 0.65f : 0.85f,0xF6CD36);
            if(node->kind==ROGUE_NODE_SHOP) {
                RogueText_Line(&labels,x,380,"General store and aerial service");
                RogueText_Style(&labels,0.6f,0xFFFFFF);
            } else if(node->kind==ROGUE_NODE_REST) {
                RogueText_Line(&labels,x,380,"Healing and aerial service");
                RogueText_Style(&labels,0.6f,0xFFFFFF);
            }
            else {
                const RogueEventDesc* event=&node->payload.event;
                RogueText_Line(&labels,x,355,"%s",objectives[event->template_id]);
                RogueText_Style(&labels,0.55f,0xFFFFFF);
                RogueText_Line(&labels,x,377,"%s",event->dangerous ?
                    "DANGER: defeat ends the run" : "Safe attempt: failure keeps the run");
                RogueText_Style(&labels,0.5f,event->dangerous ? 0xFF8585 : 0x90D8AA);
                if(event->timer_frames) {
                    RogueText_Line(&labels,x,397,"Time limit: %u seconds",(event->timer_frames+59)/60);
                    RogueText_Style(&labels,0.55f,0xFFFFFF);
                }
                RogueText_Line(&labels,x,417,"%s",event->dangerous ?
                    "Rogue records / stocks and damage carry" : "Rogue records / incoming damage restored");
                RogueText_Style(&labels,0.47f,0xD1D5DF);
                if(RogueEvent_HasNativeKit(event->template_id)) {
                    kit_entry[side]=labels.used;
                    RogueText_Line(&labels,x,437,"%s",event->native_kit_assist ?
                        "NATIVE MOVES - X: USE EQUIPPED" : "EQUIPPED MOVES - X: USE NATIVE");
                    RogueText_Style(&labels,0.45f,0xFFFFFF);
                }
                kit_status_entry[side]=labels.used;
                RogueText_Line(&labels,x,455,"%s",RoguePlay_EventKitLabel(run,event));
                RogueText_Style(&labels,0.43f,0xD1D5DF);
                OSReport("[rogue] native_event_preview side=%u template=%u timer=%u dangerous=%u assist=%u objective=%s\n",
                    side,event->template_id,event->timer_frames,event->dangerous,
                    event->native_kit_assist,objectives[event->template_id]);
            }
        }
        side_end[side]=labels.used;
        OSReport("[rogue] native_intro_side side=%u kind=%u actors=%u\n",
            side,node->kind,side ? rogue_native_intro.right_count : rogue_native_intro.left_count);
    }
    focus_entry=labels.used;
    RogueText_Line(&labels,25,30,"LEFT DESTINATION");
    RogueText_Style(&labels,0.48f,0xF6CD36);
    RogueText_Line(&labels,340,30,"RIGHT DESTINATION");
    RogueText_Style(&labels,0.48f,0xFFFFFF);
    RogueText_End(&labels);
    labels_ready=1;
    focus_side=2; focus_phase=~0U;
    RogueNativeIntro_Focus(0);
#if !ROGUE_QA_MODE || ROGUE_QA_NATIVE_DEPARTURE || ROGUE_QA_NATIVE_HRC || ROGUE_QA_NATIVE_TARGETS || ROGUE_QA_NATIVE_RACE
    RogueMode_NativeOverlayEnter();
#endif
}
void RogueNativeIntro_KitChanged(unsigned side)
{
    if(!labels_ready || side>1 || kit_entry[side]==~0U) return;
    HSD_SisLib_803A70A0(labels.native,kit_entry[side],"%s",
        RogueDirector_Run()->preview[side].payload.event.native_kit_assist ?
        "NATIVE MOVES - X: USE EQUIPPED" : "EQUIPPED MOVES - X: USE NATIVE");
    HSD_SisLib_803A70A0(labels.native,kit_status_entry[side],"%s",
        RoguePlay_EventKitLabel(RogueDirector_Run(),
            &RogueDirector_Run()->preview[side].payload.event));
#if ROGUE_DEBUG
    route_idle_hash=run_hash();route_idle_frames=0;
#endif
}
void RogueNativeIntro_Focus(unsigned side)
{
    GXColor selected={246,205,54,255}, ordinary={255,255,255,255};
    GXColor subdued={150,158,175,255};
    unsigned s,i;
    unsigned phase=RogueDirector_Run()->phase;
    if(!labels_ready || side>1 || (side==focus_side && phase==focus_phase)) return;
    focus_side=side; focus_phase=phase;
    RogueNativeDestination_Focus(side,phase);
    for(s=0;s<2;++s) for(i=side_entry[s];i<side_end[s];++i)
        HSD_SisLib_803A74F0(labels.native,i,phase!=ROGUE_ROUTE ? &ordinary : s==side ? &selected : &subdued);
    HSD_SisLib_803A74F0(labels.native,focus_entry,side==0 ? &selected : &ordinary);
    HSD_SisLib_803A74F0(labels.native,focus_entry+1,side==1 ? &selected : &ordinary);
    HSD_SisLib_803A70A0(labels.native,focus_entry,"%s",side==0 && phase==ROGUE_ROUTE ? "LEFT - A: CONFIRM" : "LEFT DESTINATION");
    HSD_SisLib_803A70A0(labels.native,focus_entry+1,"%s",side==1 && phase==ROGUE_ROUTE ? "RIGHT - A: CONFIRM" : "RIGHT DESTINATION");
}

void RogueNativeIntro_ModelFocus(HSD_GObj* object)
{
    unsigned i;
    Vec3 scale;
    HSD_JObj* model;
    if(!RogueNativeIntro_Held() || !labels_ready ||
       RogueDirector_Run()->phase!=ROGUE_ROUTE) return;
    for(i=0;i<6;++i) if(Player_GetEntity(i)==object) {
        if((i<rogue_native_intro.left_count ? 0U : 1U)==focus_side) return;
        model=object->hsd_obj;
        /* Native layout recomputes this scale each frame before this hook.
         * Linked demo entities inherit it afterward in the native function. */
        HSD_JObjGetScale(model,&scale);
        scale.x*=0.90f; scale.y*=0.90f; scale.z*=0.90f;
        HSD_JObjSetScale(model,&scale);
        return;
    }
}

void RogueNativeIntro_NameFocus(void* text,unsigned index)
{
    unsigned stamp,phase;
    GXColor color={255,255,255,255};
    /* Native frames 5/6 replace the name strings and their scale commands.
     * Apply Rogue layout only after that initialization, at input readiness. */
    if(!RogueNativeIntro_Held() || !labels_ready || frames<120 || !text || index>=6) return;
    phase=RogueDirector_Run()->phase;
    stamp=1+phase*3+focus_side;
    if(name_focus_stamp[index]==stamp) return;
    name_focus_stamp[index]=stamp;
    {
        unsigned side=index/3, member=index%3;
        unsigned count=side ? rogue_native_intro.right_count : rogue_native_intro.left_count;
        /* Keep the native name objects/font and staggered X composition.
         * Retail duo/trio rows reach y=431; reserve y=410 onward for the
         * encounter footer by compacting only Rogue's multi-member rows. */
        if(count==2 || count==3) {
            float x=(side ? 340.0f : 0.0f)+(count==2 ? 115.0f+50.0f*member : 110.0f+30.0f*member);
            float y=count==2 ? 363.0f+22.0f*member : 354.0f+18.0f*member;
            float scale=count==2 ? .7f : .55f;
            const RogueNodeDesc* node=&RogueDirector_Run()->preview[side];
            if(node->kind==ROGUE_NODE_EVENT && node->payload.event.template_id==ROGUE_EVENT_GAUNTLET) {
                y=340.0f+18.0f*member;scale=.45f;
            }
            HSD_SisLib_803A746C(text,0,x,y);
            HSD_SisLib_803A7548(text,0,scale,scale);
        }
    }
    if(phase==ROGUE_ROUTE) {
        if((index<3 ? 0U : 1U)==focus_side) {
            color.r=246; color.g=205; color.b=54;
        } else { color.r=150; color.g=158; color.b=175; }
    }
    HSD_SisLib_803A74F0(text,0,&color);
}

void RogueNativeIntro_BackdropDraw(HSD_GObj* object,intptr_t pass)
{
    HSD_JObj* joint;
    HSD_JObj* layers[2];
    u32 layer_flags[2];
    Mtx position;
    MtxPtr view;
    HSD_MObj* materials[2];
    GXColor original[2];
    GXColor ambient[2];
    float alpha[2];
    unsigned i,side;
    u32 left,top,width,height,split;
    if(!RogueNativeIntro_Held() || !labels_ready ||
       RogueDirector_Run()->phase!=ROGUE_ROUTE ||
       !(HSD_GObj_80390EB8(pass) & HSD_TRSP_XLU)) {
        HSD_GObj_JObjCallback(object,pass);
        return;
    }
    /* IrAls joints 2/3 are shared curved background layers, not sides.
     * Transparent root rendering is deferred by HSD's Z list: render only
     * these background layers immediately, then queue the rest once. */
    for(i=0;i<2;++i) {
        lb_80011E24(object->hsd_obj,&joint,i+2,-1);
        layers[i]=joint; layer_flags[i]=joint->flags;
        materials[i]=HSD_JObjGetDObj(joint)->mobj;
        original[i]=materials[i]->mat->diffuse;
        ambient[i]=materials[i]->mat->ambient;
        alpha[i]=materials[i]->mat->alpha;
    }
    GXGetScissor(&left,&top,&width,&height);
    view=HSD_CObjGetViewingMtxPtrDirect(HSD_CObjGetCurrent());
    split=width/2;
#if ROGUE_DEBUG
    if(backdrop_focus_stamp!=focus_side) {
        backdrop_focus_stamp=focus_side;
        OSReport("[rogue] native_focus_draw side=%u pass=%d scissor=%lu,%lu,%lu,%lu alpha=%f,%f\n",
            focus_side,pass,left,top,width,height,alpha[0],alpha[1]);
    }
#endif
    for(side=0;side<2;++side) {
        unsigned strength=side==focus_side ? 255 : 92;
        for(i=0;i<2;++i) {
            HSD_MObjSetDiffuseColor(materials[i],
                original[i].r*strength/255,original[i].g*strength/255,original[i].b*strength/255);
            materials[i]->mat->ambient.r=ambient[i].r*strength/255;
            materials[i]->mat->ambient.g=ambient[i].g*strength/255;
            materials[i]->mat->ambient.b=ambient[i].b*strength/255;
            HSD_MObjSetAlpha(materials[i],alpha[i]*strength/255.0f);
        }
        GXSetScissor(left+(side ? split : 0),top,side ? width-split : split,height);
        /* The farther layer (joint 3) precedes the nearer layer (joint 2),
         * matching native back-to-front transparency order. */
        for(i=2;i>0;--i) {
            joint=layers[i-1];
            if(!(joint->flags & JOBJ_HIDDEN)) {
                HSD_JObjSetupMatrix(joint);
                HSD_JOBJ_METHOD(joint)->make_pmtx(joint,view,position);
                HSD_JObjDispSub(joint,view,position,HSD_TRSP_XLU,0);
            }
        }
    }
    for(i=0;i<2;++i) {
        materials[i]->mat->diffuse=original[i];
        materials[i]->mat->ambient=ambient[i];
        HSD_MObjSetAlpha(materials[i],alpha[i]);
    }
    GXSetScissor(left,top,width,height);
    for(i=0;i<2;++i) layers[i]->flags |= JOBJ_HIDDEN;
    HSD_GObj_JObjCallback(object,pass);
    for(i=0;i<2;++i) layers[i]->flags=layer_flags[i];
}

static void map_tint(HSD_JObj* node,unsigned rgb)
{
    HSD_DObj* dobj;
    HSD_JObj* child;
    for(dobj=HSD_JObjGetDObj(node);dobj;dobj=dobj->next)
        if(dobj->mobj) HSD_MObjSetDiffuseColor(dobj->mobj,rgb>>16,rgb>>8,rgb);
    for(child=node->child;child;child=child->next) map_tint(child,rgb);
}
void RogueNativeIntro_Map(void* bases,void* icons)
{
    /* Relative preorder indices in IrRdMap subtrees 1 and 14. Native slots
     * 1/3/6/9/11 are spread across the original artwork; connectors and the
     * native moving marker stay authored. No scene pointers are retained. */
    static const unsigned base_nodes[5]={11,2,5,7,9};
    static const unsigned icon_nodes[5]={12,7,2,10,11};
    unsigned i,slot,keep;
    HSD_JObj* node;
    if(!RogueNativeIntro_Held()) return;
    for(i=1;i<=11;++i) {
        keep=0;
        for(slot=0;slot<5;++slot) if(base_nodes[slot]==i) keep=1;
        if(!keep) { lb_80011E24(bases,&node,i,-1); HSD_JObjSetFlagsAll(node,JOBJ_HIDDEN); }
    }
    for(i=2;i<=12;++i) {
        keep=0;
        for(slot=0;slot<5;++slot) if(icon_nodes[slot]==i) keep=1;
        if(!keep) { lb_80011E24(icons,&node,i,-1); HSD_JObjSetFlagsAll(node,JOBJ_HIDDEN); }
    }
    for(slot=0;slot<5;++slot) {
        unsigned state=route_view.slots[slot].state;
        unsigned rgb=state==ROGUE_ROUTE_CURRENT ? 0xFFD050 :
            state==ROGUE_ROUTE_CLEARED ? 0x70D890 : state==ROGUE_ROUTE_FAILED ? 0xEF6060 : 0x8C929E;
        lb_80011E24(bases,&node,base_nodes[slot],-1); map_tint(node,rgb);
        lb_80011E24(icons,&node,icon_nodes[slot],-1); map_tint(node,rgb);
    }
}
