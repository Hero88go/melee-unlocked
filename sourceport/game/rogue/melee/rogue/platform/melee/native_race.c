#include "native_race.h"
#include "debug_launch.h"
#include "melee_fighter.h"
#include "ui_text.h"
#include <sysdolphin/baselib/controller.h>
#include "../../core/race_attempt.h"
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

static RogueRaceAttempt attempt;
static unsigned failure,last_second;
/* Ordered vertical course-section planes, anchored to native world-space
 * exit locators rather than archive-local translations. These are progress
 * checkpoints, not substitute finish predicates. Native goals remain final. */
static const unsigned checkpoint_goals[5]={0x9C,0xA0,0xA2,0xA6,0xAB};
static float checkpoint_x[5],course_height;
static unsigned checkpoints_ready;
#if ROGUE_QA_NATIVE_RACE_ENTRY && (ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE)
static unsigned control_frames;
#endif

static RogueText pause_text,pause_back,record_label;
static unsigned ui_generation,input_port,paused,pause_release,quit_confirm;

static unsigned saved_specials[4],saved_aerials[5],saved_stacks[ROGUE_UPGRADES];

static int kit_same(const RogueRun* run)
{
    return !memcmp(saved_specials,run->specials,sizeof(saved_specials)) &&
        !memcmp(saved_aerials,run->aerials,sizeof(saved_aerials)) &&
        !memcmp(saved_stacks,run->stacks,sizeof(saved_stacks));
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

void RogueNativeRace_UiEnd(void)
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
    RogueText_Line(&pause_text,65,135,"ROGUE RACE TO THE FINISH");
    RogueText_Style(&pause_text,.8f,0xF6CD36);
    RogueText_Line(&pause_text,65,185,quit_confirm ? "Quit run / No event payout or draft." : "A: END EVENT    X: QUIT RUN");
    RogueText_Style(&pause_text,.65f,0xFFFFFF);
    RogueText_Line(&pause_text,65,225,quit_confirm ? "A: QUIT RUN    B: BACK" : "Keeps course checkpoint points / No clear bonus.");
    RogueText_Style(&pause_text,.6f,0xFFFFFF);
    RogueText_Line(&pause_text,65,253,"Bonus challenge: no combat bounty/heal procs");
    RogueText_Style(&pause_text,.5f,0xD1D5DF);
    RogueText_Line(&pause_text,65,280,"START: RESUME    No retries / Rogue records only");
    RogueText_Style(&pause_text,.55f,0xF6CD36);
    RogueText_End(&pause_text);
}

static int remaining(unsigned* frames)
{
    return RogueRace_RemainingFrames(attempt.limit_frames,gm_8016AEEC(),gm_8016AEFC(),frames);
}
static void end(unsigned reason)
{
    unsigned frames;
    if(!remaining(&frames) || !RogueRace_End(&attempt,reason,frames)) return;
    OSReport("[rogue] race_end reason=%u frames=%u points=%d\n",reason,frames,(int)attempt.result.objective_points);
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
            OSReport("[rogue] race_pause seconds=%u sub=%u\n",gm_8016AEEC(),gm_8016AEFC());
        }
    }
    if(!now) return 0;
    if(pause_release) {
        if(!HSD_PadMasterStatus[input_port].button) pause_release=0;
        return 1;
    }
    buttons=HSD_PadMasterStatus[input_port].trigger;

    if(buttons&HSD_PAD_B) { quit_confirm=0;pause_release=1;pause_draw();return 1; }
    if(buttons&HSD_PAD_A) { end(quit_confirm ? ROGUE_EVENT_END_QUIT : ROGUE_EVENT_END_SKIP);return 1; }
    if(buttons&HSD_PAD_X) { quit_confirm=1;pause_release=1;pause_draw(); }
    return 1;
}

static void start(void)
{
    unsigned frames,i;
    Vec3 position;
    /* Classic fn_8017C7EC reads campaign globals. Resolve the same native
     * per-base timer from the immutable Rogue descriptor instead. */
    gmVs_GetSceneState()->timer_seconds=attempt.limit_frames/60;
    if(!remaining(&frames) || frames!=attempt.limit_frames) failure=1;
    checkpoints_ready=1;course_height=800.0f*Ground_801C0498();
    for(i=0;i<5;++i) {
        if(!Ground_801C2D24(checkpoint_goals[i],&position) ||
            !(position.x==position.x) || (i && position.x<=checkpoint_x[i-1])) {
            checkpoints_ready=0;failure=1;break;
        }
        checkpoint_x[i]=position.x;
        OSReport("[rogue] race_checkpoint_plane index=%u locator=%u x=%f y=%f\n",
            i,checkpoint_goals[i],position.x,position.y);
    }
    pause_init();
    OSReport("[rogue] race_start base=%u limit=%u valid=%u seconds=%u sub=%u\n",
        attempt.base_character,attempt.limit_frames,!failure,gm_8016AEEC(),gm_8016AEFC());
}

static void capture(u8 outcome)
{
    int goal;
    unsigned frames,accepted;
    if(!RogueRuntime_IsActive() || !attempt.active) return;
    if(attempt.finalized && (attempt.result.event_end==ROGUE_EVENT_END_SKIP ||
        attempt.result.event_end==ROGUE_EVENT_END_QUIT)) {
        if(outcome!=OUTCOME_NO_CONTEST) failure=1;
        return;
    }
    /* Ground_801C3D44 latches the actual exit into x6D0. The accessor
     * remains valid here; no stage pointer survives this callback. */
    goal=Ground_801C1DD4();
    if(!remaining(&frames)) { failure=1;RogueRace_Interrupt(&attempt);return; }
    accepted=RogueRace_Finalize(&attempt,outcome,goal<0 ? 0 : (unsigned)goal,frames);
    if(!accepted) failure=1;
    OSReport("[rogue] race_capture accepted=%u outcome=%u goal=%d frames=%u checkpoints=%u points=%d\n",
        accepted,outcome,goal,frames,attempt.checkpoints,(int)attempt.result.objective_points);
}

int RogueNativeRace_Prepare(StartMeleeData* match,unsigned port)
{
    RogueRun* run=RogueDirector_Run();
    unsigned forced=0;
#if ROGUE_QA_MODE || (ROGUE_DEBUG && ROGUE_LAUNCH_CONFIGURED)
    forced=1;
#endif
    if(!RogueRuntime_IsActive() || port>=4 || !RogueRace_Begin(&attempt,run,forced)) return 0;
    failure=0;checkpoints_ready=0;last_second=~0U;input_port=port;paused=pause_release=quit_confirm=0;
    memcpy(saved_specials,run->specials,sizeof(saved_specials));
    memcpy(saved_aerials,run->aerials,sizeof(saved_aerials));
    memcpy(saved_stacks,run->stacks,sizeof(saved_stacks));
    OSReport("[rogue] race_kit_prepare assist=%u special0=%u aerial0=%u effective_special0=%u effective_aerial0=%u speed_rank=%u\n",
        attempt.assist,run->specials[0],run->aerials[0],RoguePlay_Special(run,0),RoguePlay_Aerial(run,0),run->stacks[2]);
#if ROGUE_QA_NATIVE_RACE_ENTRY && (ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE)
    control_frames=0;
#endif
#if ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE
    RogueEventRecords_Read(persistent_records);
#endif
    RogueFighter_ResetMatch();
    gm_SetupRulesDefaults(&match->rules);
    /* Native gm_8017CE34, bonus kind 2. Preserve the native Race stage
     * callbacks/camera/hazards while replacing campaign continuation. */
    match->rules.match_kind=1;match->rules.is_teams=0;
    match->rules.timer_enabled=1;match->rules.time_limit=attempt.limit_frames/60;
    match->rules.timer_counts_up=0;match->rules.timer_shows_hours=0;
    match->rules.x1_1=1;match->rules.x4_2=1;match->rules.x4_3=1;
    match->rules.x2_5=0;match->rules.x5_1=1;match->rules.x4_4=0;
    match->rules.x3_1=0;match->rules.x3_2=0;match->rules.x3_3=0;
    match->rules.x3_4=0;match->rules.x3_6=1;match->rules.x3_7=0;
    match->rules.sd_penalty=-1;match->rules.x18=0;match->rules.x20=0;
    match->rules.x9=3;match->rules.item_freq=-1;match->rules.stkind=82;
    match->rules.on_pause_override=gm_80165290;
    match->rules.on_match_start=start;match->rules.on_match_end=capture;
    gm_SetupAllPlayerDefaults(match->players);
    gm_SetupHumanPlayer(&match->players[0],run->character,run->costume,1,port);
    lbDvd_GetPreloadCacheScene()->game_cache.stkind=82;
    lbDvd_80018254();
    lbAudioAx_80026F2C(24);
    lbAudioAx_8002702C(4,lbAudioAx_80026E84(run->character));
    lbAudioAx_8002702C(8,lbAudioAx_80026EBC(82));
    lbAudioAx_80027168();gm_LoadRumbleEnabled(match);
    OSReport("[rogue] race_prepare node=%u base=%u limit=%u incoming=%u assist=%u forced=%u port=%u\n",
        attempt.node_id,run->character,attempt.limit_frames,attempt.incoming_percent,attempt.assist,forced,port);
    return 1;
}

void RogueNativeRace_Frame(void)
{
    unsigned seconds;
    Fighter_GObj* object;
    Fighter* player;
    if(!RogueRuntime_IsActive() || !RogueRuntime_Get()->match_active ||
        !attempt.active || attempt.finalized || gm_GetDbPauseFlag(2)) return;
    if(pause_frame()) return;
    object=Player_GetEntity(0);player=object ? object->user_data : NULL;
#if ROGUE_QA_NATIVE_RACE_ENTRY && (ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE)
    if(player && ++control_frames%4==0)
        OSReport("[rogue] race_control frame=%u x=%d y=%d air=%u motion=%u\n",
            control_frames,(int)player->cur_pos.x,(int)player->cur_pos.y,player->ground_or_air,player->motion_id);
#endif
    if(checkpoints_ready && !failure && player && Player_GetStocks(0)>0 &&
        player->cur_pos.y>=0 && player->cur_pos.y<=course_height) {
        while(attempt.checkpoints<5 && player->cur_pos.x>=checkpoint_x[attempt.checkpoints]) {
            unsigned index=attempt.checkpoints;
            if(!RogueRace_Checkpoint(&attempt,index)) { failure=1;break; }
            OSReport("[rogue] race_checkpoint index=%u locator=%u x=%f y=%f count=%u\n",
                index,checkpoint_goals[index],player->cur_pos.x,player->cur_pos.y,attempt.checkpoints);
        }
    }
    seconds=gm_8016AEEC();
    if(seconds!=last_second) {
        Fighter_GObj* object=Player_GetEntity(0);
        Fighter* player=object ? object->user_data : NULL;
        if(player) OSReport("[rogue] race_observe seconds=%u sub=%u x=%d y=%d goal=%d stocks=%d\n",
            seconds,gm_8016AEFC(),(int)player->cur_pos.x,(int)player->cur_pos.y,
            Ground_801C1DD4(),Player_GetStocks(0));
        last_second=seconds;
    }
    /* Ordinary route availability excludes Race until native validation. */
}

int RogueNativeRace_Finish(void)
{
    RogueRun* run=RogueDirector_Run();
    unsigned status;
#if ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE
    RogueEventRecords_Check(persistent_records,"race");
#endif
    if(failure || !RogueRuntime_IsActive() || RogueRuntime_Get()->match_resources || !kit_same(run)) {
        RogueRace_Interrupt(&attempt);failure=1;return 0;
    }
    status=RogueRace_Settle(&attempt,run);
    if(status!=ROGUE_SETTLEMENT_COMMITTED && status!=ROGUE_SETTLEMENT_ALREADY_COMMITTED) {
        RogueRace_Interrupt(&attempt);failure=1;return 0;
    }
    OSReport("[rogue] race_settle status=%u goal=%u checkpoints=%u points=%d incoming=%u carried=%u phase=%u forced=%u\n",
        status,attempt.result.race.native_goal,attempt.checkpoints,(int)attempt.result.objective_points,
        attempt.incoming_percent,run->carried_percent,run->phase,attempt.forced);
    OSReport("[rogue] race_kit_restored assist=%u saved_same=%u effective_special0=%u effective_aerial0=%u speed_rank=%u receipt_assist=%u\n",
        attempt.assist,kit_same(run),RoguePlay_Special(run,0),RoguePlay_Aerial(run,0),run->stacks[2],attempt.result.race.native_kit_assist);
    return 1;
}
void RogueNativeRace_Reset(void) { RogueRace_Reset(&attempt);failure=0; }
int RogueNativeRace_HasError(void) { return failure!=0; }
void RogueNativeRace_Abandon(void)
{
    if(failure && RogueRace_End(&attempt,ROGUE_EVENT_END_QUIT,0) &&
        RogueRace_Settle(&attempt,RogueDirector_Run())==ROGUE_SETTLEMENT_COMMITTED) failure=0;
}
