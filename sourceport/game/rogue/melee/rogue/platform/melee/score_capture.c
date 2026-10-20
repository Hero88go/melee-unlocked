#include "score_capture.h"
#include "../../core/play_context.h"
#include "../../director/rogue_director.h"
#include "debug_launch.h"
#include <melee/gm/types.h>
#include <melee/gm/gm_unsplit.h>
#include <melee/gm/gm_16F1.h>
#include <melee/gm/gmregclear.h>
#include <melee/gm/gmmain_lib.h>
#include <dolphin/os.h>
#include <string.h>

/* Capture/checkpoint values only. No fighter/archive/GObj pointers survive. */
static RogueRun checkpoint;
static RogueNodeResult captured;
static unsigned entered, collecting, finalized, failure, spawned;
static int expected_carry;
static unsigned stock_bonus_before[8], suppressed_record_writes;
#if ROGUE_QA_RECOVERY
static unsigned recovery_injected;
static RogueRun recovery_before;
#endif

static void stock_bonus_snapshot(unsigned values[8])
{
    unsigned i;
    memset(values,0,8*sizeof(values[0]));
    for(i=0;i<256;++i) if(gmMainLib_8015DADC(i)) values[i/32]|=1U<<(i%32);
}

int RogueScore_Enter(void)
{
    RogueRun* run=RogueDirector_Run();
    checkpoint=*run;
    memset(&captured,0,sizeof(captured));
    entered=RogueRuntime_IsActive() && run->phase==ROGUE_FIGHT;
    collecting=finalized=failure=spawned=0;
    suppressed_record_writes=0;
    if(entered) stock_bonus_snapshot(stock_bonus_before);
    captured.run_id=run->run_id; captured.node_id=run->current.node_id;
    captured.kind=run->current.kind; captured.submatch_count=1;
    captured.native_format=ROGUE_NATIVE_CUMULATIVE;
    expected_carry=run->score_totals.native_raw<0 ? 0 :
        run->score_totals.native_raw>999999999 ? 999999999 : (int)run->score_totals.native_raw;
#if ROGUE_QA_SETTLEMENT
    /* Controlled renderer-boundary input, never a fabricated bonus row.
     * QA mode marks the entire attempt non-spendable. Other nodes use the
     * ordinary native carry and existing match tests remain unchanged. */
    if(captured.node_id==1) expected_carry=999999900;
#endif
    captured.native_input=expected_carry;
#if ROGUE_QA_MODE || ROGUE_QA_RECOVERY || (ROGUE_DEBUG && ROGUE_LAUNCH_CONFIGURED)
    captured.debug_forced=1;
#endif
    return expected_carry;
}
void RogueScore_ActorCreated(unsigned player, unsigned subfighter)
{
    if(entered && !subfighter && player>=1 && player<=3 &&
        RoguePlay_FighterRole(RogueDirector_Run(),player)==ROGUE_ACTOR_COMBAT_OPPONENT)
        spawned|=1U<<(player-1);
}
int RogueScore_SuppressRecords(void)
{
    int active=RogueRuntime_IsActive();
    if(active && entered) ++suppressed_record_writes;
    return active;
}
void RogueScore_BeginNative(void)
{
    if(RogueRuntime_IsActive() && entered && !finalized && !failure) collecting=1;
}
static void row(unsigned id, unsigned occurrences, int value, unsigned source, unsigned exclusion)
{
    RogueNativeRow* r;
    unsigned i;
    if(captured.row_count>=ROGUE_NATIVE_ROWS || !occurrences) { failure=1; return; }
    for(i=0;i<captured.row_count;++i) if(captured.rows[i].native_id==id) { failure=2; return; }
    r=&captured.rows[captured.row_count++];
    r->native_id=id; r->occurrences=occurrences; r->points=value;
    r->source=source; r->exclusion=exclusion;
}
int RogueScore_CaptureBonus(unsigned id, int value)
{
    if(RogueRuntime_IsActive() && collecting && !failure) {
        unsigned count=1;
        if(id<0xD7 && gmDecisionGetType(id)==1) count=fn_8016F160(0,id);
        /* E6-F8 are read from the campaign/event flag bytes aliased onto
         * player_standings[6], not this fight's per-player bonus counters. */
        if(id>=0xE6 && id<=0xF8) row(id,count,value,ROGUE_SOURCE_CAMPAIGN,ROGUE_SCORE_CAMPAIGN);
        else if(id<=0xFF) row(id,count,value,ROGUE_SOURCE_COMBAT_BONUS,ROGUE_SCORE_KEEP);
        else row(id,count,value,ROGUE_SOURCE_UNKNOWN,ROGUE_SCORE_UNCLASSIFIED);
    }
    return value; /* The original native sum/evaluation is unchanged. */
}
void RogueScore_EndNative(int carry, int extra, int bonus, int time, int damage, int special, int display)
{
    unsigned i;
    RoguePoints sum=0;
    if(!RogueRuntime_IsActive() || !entered || finalized) return;
    collecting=0;
    if(carry!=expected_carry) failure=3;
    for(i=0;i<captured.row_count;++i)
        if(!RoguePoints_Add(sum,captured.rows[i].points,&sum)) failure=4;
    if((unsigned)sum!=(unsigned)bonus) failure=8;
    sum=0;
    if(extra) row(0x10000,1,extra,ROGUE_SOURCE_UNKNOWN,ROGUE_SCORE_UNCLASSIFIED);
    if(time) row(0x10001,1,time,ROGUE_SOURCE_TIME,ROGUE_SCORE_KEEP);
    if(damage) row(0x10002,1,damage,ROGUE_SOURCE_STYLE,ROGUE_SCORE_KEEP);
    if(special) row(0x10003,1,special,ROGUE_SOURCE_UNKNOWN,ROGUE_SCORE_UNCLASSIFIED);
    for(i=0;i<captured.row_count;++i)
        if(!RoguePoints_Add(sum,captured.rows[i].points,&sum)) failure=4;
    captured.native_stage_delta=sum;
    if(!RoguePoints_Add(carry,sum,&captured.native_output)) failure=4;
    captured.native_display_output=display;
    captured.display_capped=captured.native_output!=display;
    captured.measured=!failure; finalized=1;
}
void RogueScore_ExitWithoutClear(void* value)
{
    MatchEnd* end=value;
    int bonus;
    RoguePoints total;
    int display;
    if(!RogueRuntime_IsActive() || !entered || finalized || failure || !end) return;
    /* Called by native VS exit after gm_80166378/fn_8016C4F4, while player
     * bonus counters still exist. Quit/retry skip C4F4, so finalize those
     * counters here. Never read MatchPlayerData.score: in stock mode it
     * encodes ranking/death time (0xFF000001 + frames/60), not points.
     * Evaluate the same bonus mask as Stage Clear exactly once for this
     * receipt. Defeat has no Stage Clear time/damage/clear award. */
    if(end->outcome==OUTCOME_NO_CONTEST || end->outcome==OUTCOME_RETRY)
        fn_8016F344(end);
    RogueScore_BeginNative();
    bonus=fn_8016FFD4(end,fn_8017F008(),0);
    total=(RoguePoints)expected_carry+bonus;
    display=total<0 ? 0 : total>999999999 ? 999999999 : (int)total;
    RogueScore_EndNative(expected_carry,0,bonus,0,0,0,display);
}
unsigned RogueScore_Finish(int won, const void* value)
{
    const MatchEnd* end=value;
    RogueRun* run=RogueDirector_Run();
    unsigned i,j,status;
    char raw[24],score[24];
    if(!entered || !end || captured.run_id!=run->run_id || captured.node_id!=run->current.node_id) failure=5;
    if(!finalized) failure=6;
    if(failure) { OSReport("[rogue] settlement_error=%u node=%u\n",failure,captured.node_id); return 0; }
    captured.outcome=end->outcome==OUTCOME_NO_CONTEST ? ROGUE_RESULT_QUIT : won ? ROGUE_RESULT_WIN : ROGUE_RESULT_FAILURE;
    captured.carried_percent=end->player_standings[0].percent;
    captured.provenance_verified=1;
    captured.donor_count=0; memset(captured.donors,0,sizeof(captured.donors));
    for(i=1;i<=run->current.payload.combat.enemy_count;++i) {
        unsigned donor=run->current.payload.combat.fighters[i-1];
        if(!(spawned & (1U<<(i-1))) || end->player_standings[i].stocks>0 ||
            end->player_standings[i].pkind==Gm_PKind_NA) continue;
        for(j=0;j<captured.donor_count;++j) if(captured.donors[j]==donor) break;
        if(j==captured.donor_count && j<3) captured.donors[captured.donor_count++]=donor;
    }
#if ROGUE_QA_RECOVERY
    /* One deliberate inaccessible-result fault per emulator session. The
     * real engine still produces the result; only acceptance is rejected.
     * All attempts in this opt-in fixture remain non-spendable. */
    if(!recovery_injected) {
        recovery_injected=1;
        recovery_before=*run;
        captured.measured=0;
        status=RogueRun_Resolve(run,&captured);
        OSReport("[rogue] recovery_fault node=%u status=%u unchanged=%u\n",
            captured.node_id,status,!memcmp(run,&recovery_before,sizeof(*run)));
        failure=7;
        OSReport("[rogue] settlement_error=7 node=%u\n",captured.node_id);
        return 0;
    }
#endif
    status=RogueRun_Resolve(run,&captured);
    if(!status) { failure=7; OSReport("[rogue] settlement_error=7 node=%u\n",captured.node_id); return 0; }
    if(status==ROGUE_SETTLEMENT_ALREADY_COMMITTED) return status;
    {
        unsigned after[8];
        stock_bonus_snapshot(after);
        OSReport("[rogue] stock_bonus_records node=%u unchanged=%u suppressed=%u\n",
            captured.node_id,!memcmp(after,stock_bonus_before,sizeof(after)),suppressed_record_writes);
    }
#if ROGUE_DEBUG || ROGUE_QA_MODE
    for(i=0;i<captured.row_count;++i) {
        RoguePoints_Format(captured.rows[i].points,raw);
        OSReport("[rogue] score_row node=%u id=%u count=%u points=%s source=%u excluded=%u\n",
            captured.node_id,captured.rows[i].native_id,captured.rows[i].occurrences,raw,
            captured.rows[i].source,captured.rows[i].exclusion);
    }
    {
        RogueScoreReceipt* receipt=&run->receipts[(captured.node_id-1)/2];
        char input[24],output[24],delta[24],clear[24],show[24],display[24];
        char eligible[24],basis[24],boosted[24],converted[24],adjusted[24];
        RoguePoints_Format(captured.native_input,input);
        RoguePoints_Format(captured.native_output,output);
        RoguePoints_Format(receipt->run_score_delta,delta);
        RoguePoints_Format(receipt->rogue_clear_points,clear);
        RoguePoints_Format(receipt->showmanship_bonus,show);
        RoguePoints_Format(captured.native_display_output,display);
        OSReport("[rogue] score_receipt node=%u input=%s output=%s delta=%s clear=%s show=%s payout=%u forced=%u outcome=%u receipts=%u display=%s capped=%u\n",
            captured.node_id,input,output,delta,clear,show,receipt->total_gold,
            captured.debug_forced,captured.outcome,run->receipt_count,display,captured.display_capped);
        RoguePoints_Format(receipt->eligible_native_points,eligible);
        RoguePoints_Format(receipt->performance_basis,basis);
        RoguePoints_Format(receipt->boosted_performance,boosted);
        RoguePoints_Format(receipt->uncapped_performance_gold,converted);
        RoguePoints_Format(receipt->adjusted_performance_gold,adjusted);
        OSReport("[rogue] score_gold node=%u base=%u performance=%u effect=%u total=%u cap=%u show_rank=%u bounty_rank=%u patron_rank=%u eligible=%s basis=%s boosted=%s converted=%s adjusted=%s\n",
            captured.node_id,receipt->base_gold,receipt->performance_gold,receipt->effect_gold,
            receipt->total_gold,receipt->performance_cap,receipt->modifiers.showmanship_rank,
            receipt->modifiers.bounty_rank,receipt->modifiers.patron_rank,
            eligible,basis,boosted,converted,adjusted);
    }
#endif
    RoguePoints_Format(captured.native_stage_delta,raw); RoguePoints_Format(run->score,score);
    OSReport("[rogue] settled node=%u status=%u raw=%s score=%s gold=%u rows=%u donors=%u spawned=%x\n",
        captured.node_id,status,raw,score,run->gold,captured.row_count,captured.donor_count,spawned);
#if ROGUE_DEBUG
    for(i=0;i<captured.donor_count;++i)
        OSReport("[rogue] defeated_donor node=%u index=%u character=%u\n",captured.node_id,i,captured.donors[i]);
#endif
    return status;
}
int RogueScore_HasError(void) { return failure!=0; }
unsigned RogueScore_TestRepeat(int won, const void* value)
{
#if ROGUE_QA_SETTLEMENT
    static RogueRun before;
    unsigned status,unchanged;
    before=*RogueDirector_Run();
    status=RogueScore_Finish(won,value);
    unchanged=!memcmp(&before,RogueDirector_Run(),sizeof(before));
    OSReport("[rogue] settlement_replay node=%u status=%u unchanged=%u\n",captured.node_id,status,unchanged);
    return status==ROGUE_SETTLEMENT_ALREADY_COMMITTED && unchanged;
#else
    (void)won; (void)value;
    return 0;
#endif
}
void RogueScore_Retry(void)
{
    if(!failure) return;
    *RogueDirector_Run()=checkpoint;
    failure=0;
#if ROGUE_QA_RECOVERY
    OSReport("[rogue] recovery_retry node=%u restored=%u gold=%u receipts=%u\n",
        checkpoint.current.node_id,!memcmp(RogueDirector_Run(),&checkpoint,sizeof(checkpoint)),
        checkpoint.gold,checkpoint.receipt_count);
#endif
}
void RogueScore_Abandon(void)
{
    if(!failure) return;
    RogueDirector_Run()->phase=ROGUE_DEAD; RogueDirector_Run()->death_reason=2;
    failure=0;
#if ROGUE_QA_RECOVERY
    OSReport("[rogue] recovery_abandon node=%u phase=%u gold=%u receipts=%u\n",
        captured.node_id,RogueDirector_Run()->phase,RogueDirector_Run()->gold,
        RogueDirector_Run()->receipt_count);
#endif
}
