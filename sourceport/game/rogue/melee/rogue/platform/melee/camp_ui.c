#include "camp_ui.h"
#include "camp_scene.h"
#include "ui_text.h"
#include "../../director/rogue_director.h"
#include "../../core/camp_vendor.h"
#include "../../core/offers.h"
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/pl/player.h>
#include <melee/gr/ground.h>
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbvector.h>
#include <melee/lb/lbbgflash.h>
#include <melee/if/ifall.h>
#include <melee/if/iftime.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/mn/inlines.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <string.h>
#include <math.h>
#include <dolphin/os.h>

enum { GENERAL=1, AERIAL, HEAL, EXIT, SLOT_CHOICE };
static const char* names[4]={"GENERAL STORE","AERIAL SERVICE","REST HEAL","EXIT"};
static const char* slots[5]={"Neutral Air","Forward Air","Back Air","Up Air","Down Air"};
static const char* fighters[26]={"Captain Falcon","Donkey Kong","Fox","Mr Game and Watch","Kirby","Bowser","Link","Luigi","Mario","Marth","Mewtwo","Ness","Peach","Pikachu","Ice Climbers","Jigglypuff","Samus","Yoshi","Zelda","Sheik","Falco","Young Link","Dr Mario","Roy","Pichu","Ganondorf"};
static RogueText text,back,markers[4];
static unsigned generation, modal, card, slot, release_gate, frozen, departing;
static unsigned saved_flags[2],saved_count[2];
static Vec3 anchors[4],opened_position,saved_position[2];
static unsigned saved_motion[2];
static RogueAcquireChoice choice;
static const char* feedback;

/* Read-only transaction evidence over value-owned run data, including all
 * inventories, sold flags, prices, equipment, history and RNG streams. */
static unsigned run_fingerprint(const RogueRun* run)
{
    const unsigned char* bytes=(const unsigned char*)run;
    unsigned i,hash=2166136261U;
    for(i=0;i<sizeof(*run);++i) hash=(hash^bytes[i])*16777619U;
    return hash;
}
static int enabled(unsigned index)
{
    const RogueRun* run=RogueDirector_Run();
    return index==0 ? run->phase==ROGUE_SHOP : index==2 ? run->phase==ROGUE_REST : 1;
}
static void freeze(void)
{
    unsigned actor;
    if(frozen) return;
    for(actor=0;actor<2;++actor) {
        HSD_GObj* object=Player_GetEntityAtIndex(0,actor);
        HSD_GObjProc* proc;
        unsigned n=0,bits=0;
        if(!object) { saved_count[actor]=0; continue; }
        for(proc=object->proc;proc;proc=proc->child) {
            if(n>=32) OSPanic(__FILE__,__LINE__,"camp process snapshot overflow");
            bits|=(unsigned)proc->flags_1<<n++;
            proc->flags_1=1;
        }
        saved_flags[actor]=bits; saved_count[actor]=n;
        saved_position[actor]=((Fighter*)object->user_data)->cur_pos;
        saved_motion[actor]=((Fighter*)object->user_data)->motion_id;
    }
    frozen=1;
}
static unsigned validate_frozen(void)
{
    unsigned actor,actors=0;
    for(actor=0;actor<2;++actor) if(saved_count[actor]) {
        HSD_GObj* object=Player_GetEntityAtIndex(0,actor);
        Fighter* fp=object ? object->user_data : NULL;
        if(!fp || fp->cur_pos.x!=saved_position[actor].x ||
           fp->cur_pos.y!=saved_position[actor].y || fp->cur_pos.z!=saved_position[actor].z ||
           fp->motion_id!=saved_motion[actor])
            OSPanic(__FILE__,__LINE__,"camp modal changed a frozen actor");
        actors++;
    }
    return actors;
}
static void resume(void)
{
    unsigned actor,actors;
    if(!frozen) return;
    actors=validate_frozen();
    for(actor=0;actor<2;++actor) {
        HSD_GObj* object=Player_GetEntityAtIndex(0,actor);
        HSD_GObjProc* proc;
        unsigned n=0;
        if(!object && saved_count[actor]) OSPanic(__FILE__,__LINE__,"camp actor disappeared during modal");
        if(!object) continue;
        for(proc=object->proc;proc;proc=proc->child) {
            if(n>=saved_count[actor]) OSPanic(__FILE__,__LINE__,"camp process owner changed");
            proc->flags_1=(saved_flags[actor]>>n++)&1;
        }
        if(n!=saved_count[actor]) OSPanic(__FILE__,__LINE__,"camp process count changed");
        n=0;
        for(proc=object->proc;proc;proc=proc->child) {
            if(proc->flags_1!=((saved_flags[actor]>>n++)&1))
                OSPanic(__FILE__,__LINE__,"camp process flags not restored");
        }
        Fighter_ResetInputData_80068854(object);
    }
    frozen=0;
    OSReport("[rogue] camp_resume actors=%u positions=1 flags=1\n",actors);
}
static void init(void)
{
    unsigned i;
    HSD_GObj* ground=Ground_GetMapGObj(0);
    int found=0;
    if(!ground || !ifAll_GetHUDGObj()) return;
    for(i=0;i<3;++i) {
        HSD_JObj* joint=Ground_801C3FA4(ground,7+i);
        if(!joint) OSPanic(__FILE__,__LINE__,"missing native service anchor");
        lb_8000B1CC(joint,NULL,&anchors[i]);
    }
    for(i=0x99;i<0xB3;++i) if(Ground_801C2D24(i,&anchors[3])) { found=1; break; }
    if(!found) OSPanic(__FILE__,__LINE__,"missing native camp portal");
    generation=RogueRuntime_Get()->scene_generation;release_gate=1;
    HSD_SisLib_803A62A0(4,"SdMenu.usd","SIS_MenuData");
    HSD_SisLib_803A611C(4,NULL,9,0x14,0,0xF,0,0x13);
    RogueText_CreateSized(&back,4,0,128);
    RogueText_Begin(&back);RogueText_End(&back);
    back.native->font_size.x=back.native->font_size.y=1;
    back.native->pos_x=35;back.native->pos_y=95;
    back.native->box_size_x=570;back.native->box_size_y=335;
    back.native->bg_color.r=back.native->bg_color.g=back.native->bg_color.b=8;
    back.native->bg_color.a=235;
    RogueText_CreateSized(&text,4,0,8192);
    for(i=0;i<4;++i) {
        RogueText_CreateSized(&markers[i],4,0,256);
        OSReport("[rogue] camp_anchor service=%u x=%d y=%d z=%d\n",i+1,(int)anchors[i].x,(int)anchors[i].y,(int)anchors[i].z);
    }
    RogueRuntime_ResourceAcquire(generation);
    ifTime_HideTimers();
}
static void line(float x,float y,float scale,unsigned color,const char* value)
{
    RogueText_Line(&text,x,y,"%s",value);RogueText_Style(&text,scale,color);
}
static void draw(unsigned near)
{
    RogueRun* run=RogueDirector_Run();
    unsigned i;
    back.native->hidden=!modal;
    RogueText_Begin(&text);
    for(i=0;i<4;++i) {
        Vec3 screen,world=anchors[i];
        markers[i].native->hidden=modal || !enabled(i);
        if(modal || !enabled(i)) continue;
        world.y+=8;
        lbVector_WorldToScreen(Camera_80030A50()->hsd_obj,&world,&screen,0);
        RogueText_Begin(&markers[i]);
        RogueText_Line(&markers[i],screen.x-35,screen.y,"%s",names[i]);
        RogueText_Style(&markers[i],.55f,near==i+1 ? 0xF6CD36 : 0xFFFFFF);
        RogueText_End(&markers[i]);
    }
    RogueText_Line(&text,35,35,"%s   GOLD %u",run->camp.kind==ROGUE_NODE_SHOP ? "SHOP" : "REST",run->gold);
    RogueText_Style(&text,.8f,0xF6CD36);
    if(!modal) {
        if(near) RogueText_Line(&text,35,65,"A: %s",names[near-1]);
        else RogueText_Line(&text,35,65,"Walk to a service marker");
        RogueText_Style(&text,.7f,0xFFFFFF);
        if(feedback) line(35,85,.6f,0xF6CD36,feedback);
    } else if(modal==GENERAL || modal==AERIAL) {
        const RogueOfferState* offers=modal==GENERAL ? &run->camp.general : RogueRun_AerialOffers(run,slot);
        line(55,110,.85f,0xF6CD36,names[modal-1]);
        if(modal==AERIAL) {
            RogueText_Line(&text,55,139,"LEFT / RIGHT: %s (%u/5)",slots[slot],slot+1);
            RogueText_Style(&text,.65f,0xFFFFFF);
        }
        for(i=0;i<3;++i) {
            unsigned id=offers->ids[i];
            unsigned price=modal==GENERAL ? run->camp.general_prices[i] : run->camp.aerial_prices[slot][i];
            RogueText_Line(&text,55,174+i*31,"%s %s   %u GOLD%s",card==i ? ">" : " ",RogueOffer_Name(id),price,offers->sold[i] ? "  SOLD" : "");
            RogueText_Style(&text,.62f,card==i ? 0xF6CD36 : 0xFFFFFF);
        }
        if(modal==AERIAL) {
            RogueOfferRef ref=RogueOffer_Decode(offers->ids[card]);
            const RogueAerialDef* move=RogueAerial_Find(ref.stable_id);
            const RogueAerialDef* old=RogueAerial_Find(run->aerials[slot]);
            RogueText_Line(&text,55,281,"Donor: %s / %s",move ? fighters[move->character] : "Unavailable",slots[slot]);
            RogueText_Style(&text,.62f,0xFFFFFF);
            RogueText_Line(&text,55,305,"%s: %s",offers->sold[card] ? "Equipped" : "Replaces",old ? fighters[old->character] : "Native aerial");
            RogueText_Style(&text,.62f,0xFFFFFF);
            line(55,329,.62f,0xBAC3E0,"Equipment activates next encounter");
        } else {
            RogueOfferRef ref=RogueOffer_Decode(offers->ids[card]);
            if(ref.kind==ROGUE_OFFER_SPECIAL) {
                const RogueSpecialDef* move=RogueSpecial_Find(ref.stable_id);
                const RogueSpecialDef* old=RogueSpecial_Find(run->specials[move->slot]);
                RogueText_Line(&text,55,281,"Donor: %s / %s",fighters[move->character],RogueOffer_Description(offers->ids[card]));
                RogueText_Style(&text,.6f,0xFFFFFF);
                RogueText_Line(&text,55,305,"%s: %s",offers->sold[card] ? "Equipped" : "Replaces",old ? old->name : "Native special");
                RogueText_Style(&text,.6f,0xFFFFFF);
                line(55,329,.6f,0xBAC3E0,"Equipment activates next encounter");
            } else RogueText_Wrap(&text,55,279,60,RogueOffer_Description(offers->ids[card]));
        }
        RogueText_Line(&text,55,362,"A: BUY   X: REROLL %u GOLD   B: CLOSE",modal==GENERAL ? RogueRun_RerollCost(run,1) : RogueRun_AerialRerollCost(run));
        RogueText_Style(&text,.6f,0xFFFFFF);
        if(feedback) line(55,396,.57f,0xF6CD36,feedback);
    } else if(modal==SLOT_CHOICE) {
        static const char* special_slots[4]={"Neutral special","Side special","Up special","Down special"};
        line(55,110,.8f,0xF6CD36,"CHOOSE SPECIAL SLOT");
        for(i=0;i<4;++i) {
            const RogueSpecialDef* old=RogueSpecial_Find(run->specials[i]);
            RogueText_Line(&text,55,160+i*40,"%s %s: %s",card==i ? ">" : " ",special_slots[i],old ? old->name : "Native");
            RogueText_Style(&text,.65f,card==i ? 0xF6CD36 : 0xFFFFFF);
        }
        RogueText_Line(&text,55,350,"%u GOLD   A: CONFIRM   B: CANCEL",choice.price);
        RogueText_Style(&text,.65f,0xFFFFFF);
    } else {
        line(55,115,.9f,0xF6CD36,names[modal-1]);
        if(modal==HEAL) {
            RogueText_Line(&text,55,180,"Recover %u damage, once this visit",20+10*run->stacks[13]);
            RogueText_Style(&text,.7f,0xFFFFFF);
            line(55,220,.7f,0xFFFFFF,run->camp.heal_used ? "Healing already used / B: CLOSE" : "A: HEAL   B: CANCEL");
        } else line(55,180,.7f,0xFFFFFF,"A: LEAVE ROOM   B: CANCEL");
        if(feedback) line(55,300,.65f,0xF6CD36,feedback);
    }
    RogueText_End(&text);
}
void RogueCampUi_Frame(unsigned port)
{
    RogueRun* run=RogueDirector_Run();
    HSD_GObj* object=Player_GetEntity(0);
    Fighter* fighter=object ? object->user_data : NULL;
    unsigned near=0,i,input;
    float distance=10000;
    if(!fighter) return;
    if(!generation) init();
    if(!generation) return;
    if(frozen) validate_frozen();
    if(departing) {
        if(++departing==60) { gm_SetNextGameModeStateId(3);gm_801A4B60(); }
        return;
    }
    input=(unsigned)mn_80229624(port);
    if(HSD_PadMasterStatus[port].trigger&HSD_PAD_B) input|=MenuInput_Back;
    if(!modal && fighter->ground_or_air==GA_Ground &&
       fighter->motion_id>=ftCo_MS_Wait && fighter->motion_id<=ftCo_MS_WalkFast) {
        for(i=0;i<4;++i) if(enabled(i)) {
            float dx=fabsf(fighter->cur_pos.x-anchors[i].x);
            if(dx<15 && dx<distance && fabsf(fighter->cur_pos.y-anchors[i].y)<45) { near=i+1;distance=dx; }
        }
    }
    if(release_gate) {
        if(!HSD_PadMasterStatus[port].button && fabsf(HSD_PadMasterStatus[port].nml_stickX)<.25f &&
           fabsf(HSD_PadMasterStatus[port].nml_stickY)<.25f) {
            release_gate=0;if(!modal) resume();
        }
        draw(near);return;
    }
    if(!modal) {
        if(near && (input&MenuInput_AButton)) {
            modal=near;card=0;feedback=NULL;release_gate=1;opened_position=fighter->cur_pos;freeze();
            OSReport("[rogue] camp_modal open=%u x=%d y=%d gold=%u history=%u resources=%u\n",modal,(int)opened_position.x,(int)opened_position.y,run->gold,run->history_count,RogueRuntime_Get()->match_resources);
        }
    } else if(input&MenuInput_Back) {
        if(modal==SLOT_CHOICE) {
            card=choice.card;RogueOffer_ChoiceCancel(&choice);modal=GENERAL;
            OSReport("[rogue] camp_choice_cancel card=%u gold=%u history=%u state=%08x\n",card,run->gold,run->history_count,run_fingerprint(run));
        }
        else {
            OSReport("[rogue] camp_modal close=%u same_position=%u gold=%u history=%u resources=%u\n",modal,
                fighter->cur_pos.x==opened_position.x && fighter->cur_pos.y==opened_position.y,
                run->gold,run->history_count,RogueRuntime_Get()->match_resources);
            modal=0;
        }
        release_gate=1;feedback=NULL;
    } else if(modal==GENERAL || modal==AERIAL || modal==SLOT_CHOICE) {
        if(input&MenuInput_Up) card=(card+(modal==SLOT_CHOICE ? 3 : 2))%(modal==SLOT_CHOICE ? 4 : 3);
        else if(input&MenuInput_Down) card=(card+1)%(modal==SLOT_CHOICE ? 4 : 3);
        else if(modal==AERIAL && (input&(MenuInput_Left|MenuInput_Right))) {
            slot=(slot+((input&MenuInput_Left) ? 4 : 1))%5;card=0;feedback=NULL;
            OSReport("[rogue] camp_filter slot=%u gold=%u history=%u reroll_price=%u resources=%u\n",slot,run->gold,run->history_count,RogueRun_AerialRerollCost(run),RogueRuntime_Get()->match_resources);
        } else if(input&(MenuInput_AButton|MenuInput_XButton)) {
            unsigned before=run->gold,h=run->history_count,state_before=run_fingerprint(run);
            int accepted=0,reroll=(input&MenuInput_XButton)!=0;
            if(modal==SLOT_CHOICE) {
                if(!reroll) {
                    unsigned parent_card=choice.card,selected_slot=card;
                    accepted=RogueOffer_ChoiceCommit(run,&choice,selected_slot);
                    if(accepted) {modal=GENERAL;card=parent_card;}
                    OSReport("[rogue] camp_choice_commit accepted=%u card=%u slot=%u gold=%u history=%u selected=%u\n",accepted,parent_card,selected_slot,run->gold,run->history_count,run->upgrade_choices[0]);
                }
            }
            else if(modal==GENERAL && !reroll && RogueOffer_ChoiceBegin(run,card,&choice)) {
                OSReport("[rogue] camp_choice_open card=%u gold=%u history=%u state=%08x\n",card,run->gold,run->history_count,run_fingerprint(run));
                modal=SLOT_CHOICE;card=0;release_gate=1;draw(near);return;
            }
            else if(modal==GENERAL) accepted=reroll ? RogueRun_Reroll(run,1) : RogueRun_Buy(run,card);
            else accepted=reroll ? RogueRun_RerollAerial(run,slot) : RogueRun_BuyAerial(run,slot,card);
            feedback=accepted ? (reroll ? "Offers refreshed" : "Purchased for next encounter") : "Unavailable: check gold or sold status";
            release_gate=1;
            OSReport("[rogue] camp_transaction service=%u slot=%u card=%u reroll=%u accepted=%u before=%u after=%u history_before=%u history_after=%u resources=%u\n",modal,slot,card,reroll,accepted,before,run->gold,h,run->history_count,RogueRuntime_Get()->match_resources);
            OSReport("[rogue] camp_transaction_state before=%08x after=%08x\n",state_before,run_fingerprint(run));
            OSReport("[rogue] camp_equipment aerials=%u,%u,%u,%u,%u resources=%u\n",run->aerials[0],run->aerials[1],run->aerials[2],run->aerials[3],run->aerials[4],RogueRuntime_Get()->match_resources);
        }
    } else if(input&MenuInput_AButton) {
        release_gate=1;
        if(modal==HEAL) {
            unsigned before=run->carried_percent;
            int accepted=RogueRun_HealCamp(run);
            if(accepted) for(i=0;i<2;++i) {
                HSD_GObj* actor=Player_GetEntityAtIndex(0,i);
                if(actor) { Fighter* fp=actor->user_data;fp->dmg.x1830_percent=run->carried_percent;
                    fp->dmg.x18F0=0;Player_SetHPByIndex(0,i,run->carried_percent); }
            }
            feedback=accepted ? "Recovered damage" : "Healing already used";
            OSReport("[rogue] camp_heal accepted=%u before=%u after=%u history=%u\n",accepted,before,run->carried_percent,run->history_count);
        } else if(modal==EXIT && RogueRun_ExitCamp(run)) {
            departing=1;lbBgFlash_8002063C(0x78);
            OSReport("[rogue] camp_exit floor=%u phase=%u resolved=%u reward=%u resources=%u\n",run->floor,run->phase,run->nodes_resolved,run->reward_source.kind,RogueRuntime_Get()->match_resources);
        }
    }
    draw(near);
}
void RogueCampUi_End(void)
{
    unsigned i;
    if(generation) {
        RogueText_Destroy(&text);RogueText_Destroy(&back);
        for(i=0;i<4;++i) RogueText_Destroy(&markers[i]);
        HSD_SisLib_803A5F50(4);
        RogueRuntime_ResourceRelease(generation);
    }
    /* Scene teardown owns frozen native actors. No saved engine pointers. */
    generation=modal=card=slot=release_gate=frozen=departing=0;
    memset(saved_flags,0,sizeof(saved_flags));memset(saved_count,0,sizeof(saved_count));
    RogueOffer_ChoiceCancel(&choice);feedback=NULL;
}
