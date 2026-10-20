#include "native_targets.h"
#include "debug_launch.h"
#include "melee_fighter.h"
#include "ui_text.h"
#include <sysdolphin/baselib/controller.h>
#include "../../core/target_attempt.h"
#include "../../core/event_metrics.h"
#include "../../core/play_context.h"
#include "../../director/rogue_director.h"
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1B03.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gr/ground.h>
#include <melee/mn/types.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/types.h>
#include <melee/pl/player.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/types.h>
#include <dolphin/os.h>
#include <string.h>
#if ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE
#include "qa/event_records.h"
static unsigned persistent_records[ROGUE_EVENT_RECORD_FIELDS];
#endif

static RogueTargetAttempt attempt;
static unsigned failure, last_second, last_remaining, last_fighter_kind;
#if ROGUE_QA_NATIVE_TARGET_ENTRY && (ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE)
static unsigned control_frames;
#endif
static RogueText pause_text,pause_back,record_label;
static unsigned ui_generation,input_port,paused,pause_release,quit_confirm;
static unsigned records[26][3];
static unsigned saved_specials[4],saved_aerials[5],saved_stacks[ROGUE_UPGRADES];

static int kit_same(const RogueRun* run)
{
    return !memcmp(saved_specials,run->specials,sizeof(saved_specials)) &&
        !memcmp(saved_aerials,run->aerials,sizeof(saved_aerials)) &&
        !memcmp(saved_stacks,run->stacks,sizeof(saved_stacks));
}

static void records_read(unsigned out[26][3])
{
    unsigned i;
    for(i=0;i<26;++i) {
        unsigned id=gm_CKindToSelKind(i);
        out[i][0]=*gmMainLib_8015D438(id);
        out[i][1]=(unsigned)*gmMainLib_8015D450(id);
        out[i][2]=gmMainLib_8015D48C(id);
    }
}

static void pause_init(void)
{
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

void RogueTargets_UiEnd(void)
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
    RogueText_Line(&pause_text,65,135,"ROGUE BREAK THE TARGETS");
    RogueText_Style(&pause_text,.8f,0xF6CD36);
    RogueText_Line(&pause_text,65,185,quit_confirm ? "Quit run / No event payout or draft." : "A: END EVENT    X: QUIT RUN");
    RogueText_Style(&pause_text,.65f,0xFFFFFF);
    RogueText_Line(&pause_text,65,225,quit_confirm ? "A: QUIT RUN    B: BACK" : "Keeps broken-target points / No clear bonus.");
    RogueText_Style(&pause_text,.6f,0xFFFFFF);
    RogueText_Line(&pause_text,65,253,"Bonus challenge: no combat bounty/heal procs");
    RogueText_Style(&pause_text,.5f,0xD1D5DF);
    RogueText_Line(&pause_text,65,280,"START: RESUME    No retries / Rogue records only");
    RogueText_Style(&pause_text,.55f,0xF6CD36);
    RogueText_End(&pause_text);
}

static void end(unsigned reason)
{
    s32 remaining,total;
    unsigned frames;
    Ground_801C1DE4(&remaining,&total);
    if(!RogueTarget_RemainingFrames(gm_8016AEEC(),gm_8016AEFC(),&frames) ||
        !RogueTarget_End(&attempt,reason,remaining,total,frames,Ground_801C1D98()!=0)) return;
    OSReport("[rogue] target_pause_end seconds=%u sub=%u retry=%u quit_chord=%u\n",
        gm_8016AEEC(),gm_8016AEFC(),gmVs_GetSceneController()->start.x3_2,
        gmVs_GetSceneController()->start.x3_4);
    OSReport("[rogue] target_end reason=%u node=%u remaining=%d total=%d frames=%u points=%d\n",
        reason,attempt.node_id,remaining,total,frames,(int)attempt.result.objective_points);
    fn_8016CF4C(0,OUTCOME_NO_CONTEST);
}

static int pause_frame(void)
{
    unsigned now=gm_GetDbPauseFlag(1)!=0,buttons;
    if(!ui_generation) return now;
    if(now!=paused) {
        paused=now;pause_release=1;quit_confirm=0;
        pause_text.native->hidden=pause_back.native->hidden=!now;
        if(now) {
            pause_draw();
            OSReport("[rogue] target_pause seconds=%u sub=%u\n",gm_8016AEEC(),gm_8016AEFC());
        }
    }
    if(!now) return 0;
    if(pause_release) {
        if(!HSD_PadMasterStatus[input_port].button) pause_release=0;
        return 1;
    }
    buttons=HSD_PadMasterStatus[input_port].trigger;
#if ROGUE_DEBUG && ROGUE_QA_NATIVE_TARGETS
    /* Explicit QA fault injection: discard the result while exercising the
     * real native scene teardown and unresolved-result UI. Never a payout. */
    if(buttons&HSD_PAD_Y) {
        RogueTarget_Interrupt(&attempt);failure=1;
        OSReport("[rogue] target_injected_interruption node=%u\n",attempt.node_id);
        fn_8016CF4C(0,OUTCOME_TERMINATED);return 1;
    }
#endif
    if(buttons&HSD_PAD_B) { quit_confirm=0;pause_release=1;pause_draw();return 1; }
    if(buttons&HSD_PAD_A) { end(quit_confirm ? ROGUE_EVENT_END_QUIT : ROGUE_EVENT_END_SKIP);return 1; }
    if(buttons&HSD_PAD_X) { quit_confirm=1;pause_release=1;pause_draw(); }
    return 1;
}

static void start(void)
{
    s32 remaining,total;
    Ground_801C1DE4(&remaining,&total);
    if(!RogueTarget_Observe(&attempt,remaining,total)) failure=1;
    pause_init();
    OSReport("[rogue] target_start remaining=%d total=%d valid=%u seconds=%u sub=%u\n",
        remaining,total,!failure,gm_8016AEEC(),gm_8016AEFC());
}

void RogueTargets_TimeoutBoundaryProbe(void)
{
#if ROGUE_DEBUG && ROGUE_QA_NATIVE_TARGETS && ROGUE_QA_TARGET_TIMEOUT_BOUNDARY
    s32 remaining,total;
    VsSceneController* controller;
    if(!RogueRuntime_IsActive() || !attempt.active || attempt.finalized ||
        !attempt.observed || gm_GetDbPauseFlag(1) || gm_GetDbPauseFlag(2)) return;
    Ground_801C1DE4(&remaining,&total);
    /* The controller replay must physically destroy all targets. Only the
     * timer is injected, immediately before the native outcome arbitration. */
    if(remaining!=0 || total!=10 || !Ground_801C1D98()) return;
    controller=gmVs_GetSceneController();
    if(controller->state.timer_seconds==0 && controller->state.unk_2C==59) return;
    OSReport("[rogue] target_timeout_boundary remaining=0 total=10 goal=1 before_seconds=%u before_sub=%u injected_timer=1\n",
        controller->state.timer_seconds,controller->state.unk_2C);
    controller->state.timer_seconds=0;controller->state.unk_2C=59;
#endif
}

static void capture(u8 outcome)
{
    s32 remaining,total;
    unsigned frames,goal,accepted;
    if(!RogueRuntime_IsActive() || !attempt.active) return;
    if(attempt.finalized && (attempt.result.event_end==ROGUE_EVENT_END_SKIP ||
            attempt.result.event_end==ROGUE_EVENT_END_QUIT)) {
        if(outcome!=OUTCOME_NO_CONTEST) failure=1;
        OSReport("[rogue] target_exit_ack outcome=%u accepted=%u\n",outcome,!failure);
        return;
    }
    Ground_801C1DE4(&remaining,&total);
    goal=Ground_801C1D98()!=0;
    /* Capture the native countdown before cleanup, including its fractional
     * field. Native timeout wins before the goal check. */
    if(!RogueTarget_RemainingFrames(gm_8016AEEC(),gm_8016AEFC(),&frames)) {
        failure=1;return;
    }
    accepted=RogueTarget_Finalize(&attempt,outcome,remaining,total,frames,goal);
    if(!accepted) failure=1;
    OSReport("[rogue] target_capture accepted=%u outcome=%u remaining=%d total=%d frames=%u goal=%u reason=%u points=%d\n",
        accepted,outcome,remaining,total,frames,goal,attempt.result.event_end,
        (int)attempt.result.objective_points);
}

int RogueTargets_Prepare(StartMeleeData* match, unsigned port)
{
    RogueRun* run=RogueDirector_Run();
    unsigned forced=0,character=run->character;
#if ROGUE_QA_MODE || (ROGUE_DEBUG && ROGUE_LAUNCH_CONFIGURED)
    forced=1;
#endif
    if(!RogueRuntime_IsActive() || port>=4 ||
        !RogueTarget_Begin(&attempt,run,forced)) return 0;
    failure=0;last_second=last_remaining=last_fighter_kind=~0U;
#if ROGUE_QA_NATIVE_TARGET_ENTRY && (ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE)
    control_frames=0;
#endif
    input_port=port;paused=pause_release=quit_confirm=0;
    memcpy(saved_specials,run->specials,sizeof(saved_specials));
    memcpy(saved_aerials,run->aerials,sizeof(saved_aerials));
    memcpy(saved_stacks,run->stacks,sizeof(saved_stacks));
    OSReport("[rogue] target_kit_prepare assist=%u special0=%u aerial0=%u effective_special0=%u effective_aerial0=%u speed_rank=%u\n",
        attempt.assist,run->specials[0],run->aerials[0],RoguePlay_Special(run,0),RoguePlay_Aerial(run,0),run->stacks[2]);
    records_read(records);
#if ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE
    RogueEventRecords_Read(persistent_records);
#endif
    RogueFighter_ResetMatch();
    gm_SetupRulesDefaults(&match->rules);
    match->rules.match_kind=1;match->rules.is_teams=0;
    match->rules.timer_enabled=1;match->rules.time_limit=45;
    match->rules.timer_counts_up=0;match->rules.timer_shows_hours=0;
    match->rules.x4_2=1;match->rules.x2_5=0;match->rules.x5_1=1;
    match->rules.x4_3=1;match->rules.x4_4=0;
    match->rules.x3_1=0;match->rules.x3_3=0;match->rules.x3_2=0;
    match->rules.x3_4=0; /* No native retry or stock no-contest. */
    match->rules.x3_6=1; /* Explicit Rogue pause choices replace stock help. */
    match->rules.sd_penalty=-1;match->rules.x18=0;match->rules.x20=0;
    /* The stock target clock runs through pause. Rogue's timed attempt must not. */
    match->rules.x3_7=0;match->rules.x9=1;match->rules.item_freq=-1;
    match->rules.on_pause_override=gm_80165290;
    match->rules.on_match_start=start;match->rules.on_match_end=capture;
    gm_SetupAllPlayerDefaults(match->players);
    /* Native Target Test uses solo Popo for the Ice Climbers base slot. */
    if(character==14) character=32;
    gm_SetupHumanPlayer(&match->players[0],character,run->costume,1,port);
    match->players[0].xC_b1=0;
    /* Native CSS selects Zelda's course before gmvs's held-A form swap.
     * A persistent Sheik base therefore shares that course, not GrTSk's
     * unused three-target layout. Keep the actual player selection intact. */
    match->rules.stkind=gm_801647F8(character==19 ? 18 : character);
    if(match->rules.stkind!=attempt.stage) return 0;
    lbDvd_GetPreloadCacheScene()->game_cache.stkind=match->rules.stkind;
    lbDvd_80018254();
    lbAudioAx_80026F2C(24);
    lbAudioAx_8002702C(4,lbAudioAx_80026E84(character));
    lbAudioAx_8002702C(8,lbAudioAx_80026EBC(match->rules.stkind));
    lbAudioAx_80027168();gm_LoadRumbleEnabled(match);
    OSReport("[rogue] target_prepare node=%u base=%u player=%u stage=%u seconds=45 incoming=%u forced=%u port=%u\n",
        attempt.node_id,run->character,character,match->rules.stkind,attempt.incoming_percent,forced,port);
    return 1;
}

void RogueTargets_Frame(void)
{
    s32 remaining,total;
    unsigned seconds;
    if(!RogueRuntime_IsActive() || !RogueRuntime_Get()->match_active ||
        !attempt.active || !attempt.observed ||
        attempt.finalized || gm_GetDbPauseFlag(2)) return;
    if(pause_frame()) return;
    if(Player_GetEntity(0)) {
        unsigned kind=ftLib_GetKind(Player_GetEntity(0));
        if(kind!=last_fighter_kind) {
            OSReport("[rogue] target_player base=%u fighter=%u stage=%u\n",
                attempt.base_character,kind,attempt.stage);
            last_fighter_kind=kind;
        }
    }
    Ground_801C1DE4(&remaining,&total);
    seconds=gm_8016AEEC();
    if(!RogueTarget_Observe(&attempt,remaining,total)) failure=1;
#if ROGUE_QA_NATIVE_TARGET_ENTRY && (ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE)
    /* Read-only controller feedback; never change player/target state. */
    if(++control_frames%4==0) {
        Fighter_GObj* object=Player_GetEntity(0);
        Fighter* player=object ? object->user_data : NULL;
        if(player) OSReport("[rogue] target_control frame=%u x=%d y=%d air=%u motion=%u remaining=%d\n",
            control_frames,(int)player->cur_pos.x,(int)player->cur_pos.y,
            player->ground_or_air,player->motion_id,remaining);
    }
#endif
    if(seconds!=last_second || (unsigned)remaining!=last_remaining) {
#if ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE
        Fighter_GObj* object=Player_GetEntity(0);
        Fighter* player=object ? object->user_data : NULL;
        if(player) OSReport("[rogue] target_position seconds=%u remaining=%d x=%d y=%d motion=%u\n",
            seconds,remaining,(int)player->cur_pos.x,(int)player->cur_pos.y,player->motion_id);
#endif
        OSReport("[rogue] target_observe seconds=%u sub=%u remaining=%d total=%d goal=%u stocks=%d\n",
            seconds,gm_8016AEFC(),remaining,total,Ground_801C1D98()!=0,Player_GetStocks(0));
        last_second=seconds;last_remaining=remaining;
    }
}

int RogueTargets_Finish(void)
{
    unsigned after[26][3],i,j,changed=0,status;
    RogueRun* run=RogueDirector_Run();
    records_read(after);
    for(i=0;i<26;++i) for(j=0;j<3;++j) changed+=records[i][j]!=after[i][j];
    OSReport("[rogue] target_records checked=78 changed=%u\n",changed);
#if ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE
    RogueEventRecords_Check(persistent_records,"targets");
#endif
    if(changed || failure || !RogueRuntime_IsActive() || RogueRuntime_Get()->match_resources) {
        RogueTarget_Interrupt(&attempt);failure=1;
        OSReport("[rogue] target_unresolved reason=%u node=%u paid=0\n",attempt.end_reason,attempt.node_id);
        return 0;
    }
    if(attempt.assist && !kit_same(run)) { RogueTarget_Interrupt(&attempt);failure=1;return 0; }
    status=RogueTarget_Settle(&attempt,run);
    if(status!=ROGUE_SETTLEMENT_COMMITTED && status!=ROGUE_SETTLEMENT_ALREADY_COMMITTED) {
        RogueTarget_Interrupt(&attempt);failure=1;
        OSReport("[rogue] target_unresolved reason=%u node=%u paid=0\n",attempt.end_reason,attempt.node_id);
        return 0;
    }
    OSReport("[rogue] target_settle status=%u node=%u remaining=%u points=%d incoming=%u carried=%u phase=%u draft=%u forced=%u\n",
        status,attempt.node_id,attempt.result.targets.remaining,(int)attempt.result.objective_points,
        attempt.incoming_percent,run->carried_percent,run->phase,run->reward_source.kind,attempt.forced);
    OSReport("[rogue] target_kit_restored assist=%u saved_same=%u effective_special0=%u effective_aerial0=%u speed_rank=%u receipt_assist=%u\n",
        attempt.assist,kit_same(run),RoguePlay_Special(run,0),RoguePlay_Aerial(run,0),run->stacks[2],attempt.result.targets.native_kit_assist);
    OSReport("[rogue] target_outcome reason=%u outcome=%u receipts=%u draft_generated=%u gold=%u\n",
        attempt.result.event_end,attempt.result.outcome,run->receipt_count,run->reward_source.generated,
        run->receipts[(attempt.node_id-1)/2].total_gold);
    return 1;
}

void RogueTargets_Reset(void) { RogueTarget_Reset(&attempt);failure=0; }
int RogueTargets_HasError(void) { return failure!=0; }
void RogueTargets_Abandon(void)
{
    if(!failure) return;
    if(RogueTarget_End(&attempt,ROGUE_EVENT_END_QUIT,0,0,0,0) &&
        RogueTarget_Settle(&attempt,RogueDirector_Run())==ROGUE_SETTLEMENT_COMMITTED) {
        failure=0;
        OSReport("[rogue] target_abandon reason=%u phase=%u node=%u\n",
            ROGUE_EVENT_END_QUIT,RogueDirector_Run()->phase,attempt.node_id);
        OSReport("[rogue] target_abandon_receipt points=%d gold=%u draft_generated=%u measured_targets=%u carried=%u\n",
            (int)attempt.result.objective_points,RogueDirector_Run()->receipts[(attempt.node_id-1)/2].total_gold,
            RogueDirector_Run()->reward_source.generated,attempt.result.targets.present,
            RogueDirector_Run()->carried_percent);
    }
}
