#include "race_attempt.h"
#include "event_metrics.h"
#include <string.h>

int RogueRace_RemainingFrames(unsigned limit,unsigned seconds,unsigned subframe,unsigned* frames)
{
    unsigned value;
    /* Native countdown uses an increasing fractional field, initially 59. */
    if(!frames || !limit || limit>3240 || seconds>limit/60 || subframe>59) return 0;
    value=seconds*60+59-subframe;
    if(value>limit) return 0;
    *frames=value;return 1;
}

void RogueRace_Reset(RogueRaceAttempt* a) { if(a) memset(a,0,sizeof(*a)); }
int RogueRace_Begin(RogueRaceAttempt* a,const RogueRun* run,unsigned forced)
{
    const RogueEventDesc* e;
    const RogueEventOpportunity* op;
    if(!a || !run || a->active || a->finalized || forced>1 || !run->run_id ||
        run->phase!=ROGUE_EVENT || run->current.kind!=ROGUE_NODE_EVENT || run->floor>=15 ||
        !run->current.node_id || (run->current.node_id-1)/2!=run->floor ||
        run->receipts[run->floor].committed || run->carried_percent>999) return 0;
    e=&run->current.payload.event;op=&run->event_opportunities[run->floor/5];
    if(e->template_id!=ROGUE_EVENT_RACE || e->base_character!=run->character ||
        e->native_stage!=82 || !RogueRace_LimitFrames(run->character) ||
        e->timer_frames!=RogueRace_LimitFrames(run->character) || e->native_kit_assist>1 ||
        !op->present || op->floor!=run->floor || memcmp(e,&op->event,sizeof(*e))) return 0;
    RogueRace_Reset(a);a->run_id=run->run_id;a->node_id=run->current.node_id;
    a->incoming_percent=run->carried_percent;a->base_character=run->character;
    a->limit_frames=e->timer_frames;a->assist=e->native_kit_assist;a->forced=forced;
    a->active=1;return 1;
}
int RogueRace_Checkpoint(RogueRaceAttempt* a,unsigned index)
{
    if(!a || !a->active || a->finalized || a->error) return 0;
    return RogueEvent_RaceCheckpoint(&a->checkpoints,index);
}
void RogueRace_Interrupt(RogueRaceAttempt* a) { if(a && a->run_id) a->error=1; }
static int finish(RogueRaceAttempt* a,unsigned outcome,unsigned goal,unsigned frames,unsigned reason)
{
    RogueNodeResult result;
    RogueRaceScore score;
    unsigned complete=reason==ROGUE_EVENT_END_COMPLETE;
    if(!a || !a->active || a->error || frames>a->limit_frames) return 0;
    if(!RogueEvent_RacePoints(a->checkpoints,complete ? goal : 0,frames,a->limit_frames,complete,&score)) return 0;
    memset(&result,0,sizeof(result));
    result.run_id=a->run_id;result.node_id=a->node_id;result.submatch_count=1;
    result.kind=ROGUE_RESULT_SKILL;result.measured=1;result.debug_forced=a->forced;
    result.carried_percent=a->incoming_percent;result.event_end=reason;
    result.outcome=reason==ROGUE_EVENT_END_QUIT ? ROGUE_RESULT_QUIT : complete ? ROGUE_RESULT_WIN : ROGUE_RESULT_FAILURE;
    if(reason==ROGUE_EVENT_END_QUIT) memset(&score,0,sizeof(score));
    result.objective_points=score.total;
    result.race.present=1;result.race.base_character=a->base_character;result.race.native_stage=82;
    result.race.native_kit_assist=a->assist;result.race.limit_frames=a->limit_frames;
    result.race.remaining_frames=frames;result.race.checkpoints=a->checkpoints;
    result.race.native_goal=goal;result.race.native_outcome=outcome;
    result.race.native_goal_score=score.native_goal_score;result.race.checkpoint_points=score.checkpoints;
    result.race.goal_points=score.goal;result.race.time_points=score.time;
    if(!RogueRace_ResultValid(&result)) return 0;
    if(a->finalized) {
        if(!memcmp(&a->result,&result,sizeof(result))) return 1;
        a->error=1;return 0;
    }
    a->result=result;a->finalized=1;return 1;
}
int RogueRace_Finalize(RogueRaceAttempt* a,unsigned outcome,unsigned goal,unsigned frames)
{
    if(outcome!=1 && outcome!=4 && outcome!=6) { RogueRace_Interrupt(a);return 0; }
    return finish(a,outcome,goal,frames,outcome==6 ? ROGUE_EVENT_END_COMPLETE :
        outcome==1 ? ROGUE_EVENT_END_TIMEOUT : ROGUE_EVENT_END_FALL);
}
int RogueRace_End(RogueRaceAttempt* a,unsigned reason,unsigned frames)
{
    if(!a || !a->active || (a->finalized && !a->error) ||
        (reason!=ROGUE_EVENT_END_SKIP && reason!=ROGUE_EVENT_END_QUIT)) return 0;
    if(a->error) {
        if(reason!=ROGUE_EVENT_END_QUIT) return 0;
        /* An explicit abandonment is valid; unverified engine metrics are not. */
        memset(&a->result,0,sizeof(a->result));a->result.run_id=a->run_id;a->result.node_id=a->node_id;
        a->result.kind=ROGUE_RESULT_SKILL;a->result.submatch_count=1;a->result.measured=1;
        a->result.debug_forced=a->forced;a->result.carried_percent=a->incoming_percent;
        a->result.event_end=reason;a->result.outcome=ROGUE_RESULT_QUIT;
        a->finalized=1;a->error=0;return 1;
    }
    return finish(a,7,0,frames,reason);
}
unsigned RogueRace_Settle(RogueRaceAttempt* a,RogueRun* run)
{
    unsigned status;
    if(!a || !run || !a->finalized || a->error || a->run_id!=run->run_id) return ROGUE_SETTLEMENT_INVALID;
    status=RogueRun_Resolve(run,&a->result);
    if(status==ROGUE_SETTLEMENT_COMMITTED || status==ROGUE_SETTLEMENT_ALREADY_COMMITTED) a->active=0;
    return status;
}
