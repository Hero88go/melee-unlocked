#include "native_hrc.h"
#include "debug_launch.h"
#include "melee_fighter.h"
#include "ui_text.h"
#include <sysdolphin/baselib/controller.h>
#include "../../director/rogue_director.h"
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gm_1B03.h>
#include <melee/gm/gmregclear.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/mn/types.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <melee/lb/lbaudio_ax.h>
#include <dolphin/os.h>

#include "../../core/home_run_attempt.h"
#include "../../core/event_metrics.h"
#include <string.h>

static RogueHomeRunAttempt attempt;
static RogueText pause_text, pause_back, record_label;
static unsigned ui_generation, input_port, paused, pause_release, quit_confirm, failure;

static void start(void)
{
    gm_80181998();
    ui_generation=RogueRuntime_Get()->scene_generation;
    HSD_SisLib_803A62A0(4,"SdMenu.usd","SIS_MenuData");
    HSD_SisLib_803A611C(4,NULL,9,0x14,0,0xF,0,0x13);
    RogueText_CreateSized(&pause_back,4,0,128);
    RogueText_Begin(&pause_back); RogueText_End(&pause_back);
    pause_back.native->font_size.x=pause_back.native->font_size.y=1;
    pause_back.native->pos_x=45;pause_back.native->pos_y=110;
    pause_back.native->box_size_x=550;pause_back.native->box_size_y=225;
    pause_back.native->bg_color.r=pause_back.native->bg_color.g=pause_back.native->bg_color.b=8;
    pause_back.native->bg_color.a=235;
    RogueText_CreateSized(&pause_text,4,0,2048);
    pause_text.native->hidden=pause_back.native->hidden=1;
    RogueText_CreateSized(&record_label,4,0,384);
    RogueText_Begin(&record_label);
    RogueText_Line(&record_label,187,22,"ROGUE RECORDS ONLY");
    RogueText_Style(&record_label,.65f,0x101624);
    RogueText_Line(&record_label,185,20,"ROGUE RECORDS ONLY");
    RogueText_Style(&record_label,.65f,0xF6CD36);
    RogueText_End(&record_label);
    RogueRuntime_ResourceAcquire(ui_generation);
}

void RogueHrc_UiEnd(void)
{
    if(ui_generation) {
        RogueText_Destroy(&pause_text); RogueText_Destroy(&pause_back);
        RogueText_Destroy(&record_label);
        HSD_SisLib_803A5F50(4);
        RogueRuntime_ResourceRelease(ui_generation);
    }
    ui_generation=paused=pause_release=quit_confirm=0;
}

static void pause_draw(void)
{
    RogueText_Begin(&pause_text);
    RogueText_Line(&pause_text,65,135,"ROGUE HOME-RUN CONTEST");
    RogueText_Style(&pause_text,.8f,0xF6CD36);
    RogueText_Line(&pause_text,65,185,quit_confirm ? "Quit run / No event payout or draft." : "A: END EVENT    X: QUIT RUN");
    RogueText_Style(&pause_text,.65f,0xFFFFFF);
    RogueText_Line(&pause_text,65,225,quit_confirm ? "A: QUIT RUN    B: BACK" : "End Event uses this floor / Distance scores zero.");
    RogueText_Style(&pause_text,.6f,0xFFFFFF);
    RogueText_Line(&pause_text,65,253,"Bonus challenge: no combat bounty/heal procs");
    RogueText_Style(&pause_text,.5f,0xD1D5DF);
    RogueText_Line(&pause_text,65,280,"START: RESUME    No retries / Rogue records only");
    RogueText_Style(&pause_text,.55f,0xF6CD36);
    RogueText_End(&pause_text);
}

static void end(unsigned reason)
{
    if(!RogueHomeRun_End(&attempt,reason)) return;
    if(paused) OSReport("[rogue] hrc_pause_end seconds=%u sub=%u retry=%u quit_chord=%u\n",
        gm_8016AEEC(),gm_8016AEFC(),gmVs_GetSceneController()->start.x3_2,
        gmVs_GetSceneController()->start.x3_4);
    OSReport("[rogue] hrc_end reason=%u node=%u incoming=%u\n",reason,attempt.node_id,attempt.incoming_percent);
    /* The stock no-contest teardown is reused, never its major-mode exit. */
    fn_8016CF4C(0,OUTCOME_NO_CONTEST);
}

void RogueHrc_Frame(void)
{
    unsigned now,buttons;
    if(!RogueRuntime_IsActive() || !attempt.active || attempt.finalized || !ui_generation) return;
    now=gm_GetDbPauseFlag(1)!=0;
    if(now!=paused) {
        paused=now;pause_release=1;quit_confirm=0;
        pause_text.native->hidden=pause_back.native->hidden=!now;
        if(now) { pause_draw(); OSReport("[rogue] hrc_pause seconds=%u sub=%u\n",gm_8016AEEC(),gm_8016AEFC()); }
    }
    if(!now) {
        Fighter_GObj* object=Player_GetEntity(0);
        Fighter* player=object ? object->user_data : NULL;
        /* Native HRC has a lower field: leaving the raised platform need not
         * consume a stock. Only landing there during preparation is a fall;
         * airborne recovery and the native post-launch measurement stay live.
         * The native stage uses y < 4 for its field-contact region. */
        if(player && (Player_GetStocks(0)<=0 ||
            (gm_8016AEEC()>0 && player->ground_or_air==0 && player->cur_pos.y<4.0f))) {
            OSReport("[rogue] hrc_fall y=%d grounded=%u stocks=%d seconds=%u\n",
                (int)player->cur_pos.y,player->ground_or_air==0,
                Player_GetStocks(0),gm_8016AEEC());
            end(ROGUE_EVENT_END_FALL);
        }
        return;
    }
    if(pause_release) {
        if(!HSD_PadMasterStatus[input_port].button) pause_release=0;
        return;
    }
    buttons=HSD_PadMasterStatus[input_port].trigger;
#if ROGUE_DEBUG && ROGUE_QA_NATIVE_HRC
    /* Test an unverified native termination, never a fabricated distance. */
    if(buttons&HSD_PAD_Y) {
        RogueHomeRun_Interrupt(&attempt);failure=1;
        OSReport("[rogue] hrc_injected_interruption node=%u\n",attempt.node_id);
        fn_8016CF4C(0,OUTCOME_TERMINATED);return;
    }
#endif
    if(buttons&HSD_PAD_A) { end(quit_confirm ? ROGUE_EVENT_END_QUIT : ROGUE_EVENT_END_SKIP); return; }
    if(buttons&HSD_PAD_X) { quit_confirm=1;pause_release=1;pause_draw(); }
    if(buttons&HSD_PAD_B) { quit_confirm=0;pause_release=1;pause_draw(); }
}

int RogueHrc_HasError(void) { return failure!=0; }
void RogueHrc_Abandon(void)
{
    if(!failure) return;
    if(RogueHomeRun_End(&attempt,ROGUE_EVENT_END_QUIT) &&
        RogueHomeRun_Settle(&attempt,RogueDirector_Run())==ROGUE_SETTLEMENT_COMMITTED) {
        failure=0;
        OSReport("[rogue] hrc_abandon reason=%u phase=%u\n",ROGUE_EVENT_END_QUIT,RogueDirector_Run()->phase);
        OSReport("[rogue] hrc_abandon_receipt points=%d gold=%u draft_generated=%u measured_distance=%u carried=%u\n",
            (int)attempt.result.objective_points,
            RogueDirector_Run()->receipts[(attempt.node_id-1)/2].total_gold,
            RogueDirector_Run()->reward_source.generated,attempt.result.home_run.present,
            RogueDirector_Run()->carried_percent);
    }
}


void RogueHrc_Reset(void) { RogueHomeRun_Reset(&attempt); failure=0; }

int RogueHrc_Prepare(StartMeleeData* match, unsigned port)
{
    RogueRun* run=RogueDirector_Run();
    unsigned i, forced=0;
#if ROGUE_QA_MODE || (ROGUE_DEBUG && ROGUE_LAUNCH_CONFIGURED)
    forced=1;
#endif
    if(!RogueRuntime_IsActive() || port>=4 || !RogueHomeRun_Begin(&attempt,run,forced)) return 0;
    input_port=port; paused=pause_release=quit_confirm=0;
    RogueFighter_ResetMatch();
    gm_SetupRulesDefaults(&match->rules);
    /* Pinned gm_801B9A3C setup, without stock major-mode globals or exit.
     * No combat descriptor, attrition or consumable is accessed here. */
    match->rules.stkind=84;
    match->rules.match_kind=1;
    match->rules.timer_enabled=1;
    match->rules.x1_0=1;
    match->rules.x4_2=0;
    match->rules.x4_4=0;
    match->rules.is_teams=0;
    match->rules.item_freq=-1;
    match->rules.time_limit=10;
    match->rules.game_speed=1.0f;
    match->rules.x30=1.0f;
    match->rules.x5_0=1;
    match->rules.x1_3=1;
    match->rules.on_match_start=start;
    match->rules.x3_3=1;
    match->rules.x3_2=0; /* No native Z retry. */
    match->rules.x3_4=0; /* No stock no-contest chord. */
    match->rules.x3_6=1; /* Our explicit event pause choices replace stock help. */
    match->rules.x7=9;
    for(i=0;i<GM_MAX_PLAYERS;++i) {
        gm_SetupPlayerDefaults(&match->players[i]);
        match->players[i].slot=i;
        match->players[i].xC_b1=0;
        match->players[i].xD_b3=1;
    }
    gm_SetupHumanPlayer(&match->players[0],run->character,run->costume,1,port);
    match->players[0].xD_b2=1;
    match->players[1].ckind=ChKind_Sandbag;
    match->players[1].cpu_kind=0xF;
    match->players[1].defense_ratio=1.0f;
    match->players[1].slot_type=Gm_PKind_Cpu;
    match->players[1].stocks=1;
    match->players[1].team=1;
    lbAudioAx_80026F2C(24);
    lbAudioAx_8002702C(4,lbAudioAx_80026E84(run->character) |
        lbAudioAx_80026E84(ChKind_Sandbag));
    lbAudioAx_8002702C(8,lbAudioAx_80026EBC(84));
    lbAudioAx_80027168();
    gm_LoadRumbleEnabled(match);
    gm_80181A00(run->character,match->players[0].nametag);
    gm_80180B18(); /* Read native records into the native local display table. */
    OSReport("[rogue] hrc_enter node=%u incoming=%u forced=%u\n",run->current.node_id,run->carried_percent,forced);
    return 1;
}

void RogueHrc_Observe(int raw, int display, unsigned us, unsigned ready)
{
    if(!RogueRuntime_IsActive()) return;
    RogueHomeRun_Observe(&attempt,raw,(unsigned)display,
        us ? ROGUE_DISTANCE_TENTHS_FOOT : ROGUE_DISTANCE_DECIMETERS,ready);
}
void RogueHrc_Finalized(unsigned mode, int raw)
{
    if(!RogueRuntime_IsActive() || !attempt.active ||
        (attempt.finalized && !attempt.result.home_run.present)) return;
    OSReport("[rogue] hrc_capture accepted=%u mode=%u raw=%d\n",
        RogueHomeRun_Finalize(&attempt,mode,raw,gm_8016AEEC()==0),mode,raw);
}
int RogueHrc_Finish(void)
{
    RogueRun* run=RogueDirector_Run();
    unsigned status, floor=(attempt.node_id-1)/2;
    if(!RogueRuntime_IsActive() || RogueRuntime_Get()->match_resources) {
        RogueHomeRun_Interrupt(&attempt);failure=1;return 0;
    }
    status=RogueHomeRun_Settle(&attempt,run);
    if(status!=ROGUE_SETTLEMENT_COMMITTED && status!=ROGUE_SETTLEMENT_ALREADY_COMMITTED) {
        RogueHomeRun_Interrupt(&attempt); failure=1;
        OSReport("[rogue] hrc_unresolved reason=%u node=%u paid=0\n",attempt.end_reason,attempt.node_id);
        return 0;
    }
    OSReport("[rogue] hrc_settle status=%u node=%u raw=%d mm=%d points=%d gold=%u incoming=%u carried=%u phase=%u draft=%u forced=%u\n",
        status,attempt.node_id,(int)attempt.result.home_run.raw_centimeters,
        (int)attempt.result.home_run.distance_mm,(int)attempt.result.objective_points,
        run->receipts[floor].total_gold,attempt.incoming_percent,run->carried_percent,
        run->phase,run->reward_source.kind,attempt.debug_forced);
    OSReport("[rogue] hrc_outcome reason=%u outcome=%u receipts=%u claimed=%u draft_generated=%u\n",
        attempt.result.event_end,attempt.result.outcome,run->receipt_count,run->reward.claimed,run->reward_source.generated);
    return 1;
}
