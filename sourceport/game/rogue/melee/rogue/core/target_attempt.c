#include "target_attempt.h"
#include "event_metrics.h"
#include <string.h>

void RogueTarget_Reset(RogueTargetAttempt* a) { if(a) memset(a,0,sizeof(*a)); }

int RogueTarget_Begin(RogueTargetAttempt* a, const RogueRun* run, unsigned forced)
{
    const RogueEventDesc* e;
    const RogueEventOpportunity* op;
    if(!a || !run || a->active || a->finalized || forced>1 || !run->run_id ||
        run->phase!=ROGUE_EVENT || run->current.kind!=ROGUE_NODE_EVENT ||
        run->floor>=15 || !run->current.node_id || (run->current.node_id-1)/2!=run->floor ||
        run->receipts[run->floor].committed || run->carried_percent>999) return 0;
    e=&run->current.payload.event;op=&run->event_opportunities[run->floor/5];
    if(e->template_id!=ROGUE_EVENT_TARGETS || e->base_character!=run->character ||
        !RogueTarget_Stage(run->character) || e->native_stage!=RogueTarget_Stage(run->character) ||
        e->timer_frames!=2700 || e->native_kit_assist>1 || !op->present ||
        op->floor!=run->floor || memcmp(e,&op->event,sizeof(*e))) return 0;
    RogueTarget_Reset(a);
    a->run_id=run->run_id;a->node_id=run->current.node_id;
    a->incoming_percent=run->carried_percent;a->base_character=run->character;
    a->stage=e->native_stage;a->assist=e->native_kit_assist;a->forced=forced;
    a->active=1;return 1;
}

int RogueTarget_Observe(RogueTargetAttempt* a, unsigned remaining, unsigned total)
{
    if(!a || !a->active || a->error || a->finalized) return 0;
    if(total!=10 || remaining>total || (!a->observed && remaining!=10) ||
        (a->observed && remaining>a->remaining)) { a->error=1;return 0; }
    a->observed=1;a->remaining=remaining;return 1;
}

static int finalize(RogueTargetAttempt* a, unsigned outcome,
    unsigned remaining, unsigned total, unsigned frames, unsigned goal, unsigned reason)
{
    RogueNodeResult result;
    if(!a || !a->active || a->error || !a->observed || frames>2700 || goal>1) return 0;
    memset(&result,0,sizeof(result));
    result.run_id=a->run_id;result.node_id=a->node_id;result.submatch_count=1;
    result.kind=ROGUE_RESULT_SKILL;result.measured=1;result.debug_forced=a->forced;
    result.carried_percent=a->incoming_percent;
    result.event_end=reason;
    result.outcome=reason==ROGUE_EVENT_END_QUIT ? ROGUE_RESULT_QUIT :
        outcome==6 ? ROGUE_RESULT_WIN : ROGUE_RESULT_FAILURE;
    result.targets.present=1;result.targets.base_character=a->base_character;
    result.targets.native_stage=a->stage;result.targets.total=total;
    result.targets.remaining=remaining;result.targets.remaining_frames=frames;
    result.targets.native_goal=goal;result.targets.native_outcome=outcome;
    result.targets.native_kit_assist=a->assist;
    if(reason!=ROGUE_EVENT_END_QUIT &&
        !RogueEvent_TargetPoints(total,remaining,frames,outcome==6,&result.objective_points)) {
        a->error=1;return 0;
    }
    if(!RogueTarget_ResultValid(&result)) { a->error=1;return 0; }
    if(a->finalized) {
        if(!memcmp(&a->result,&result,sizeof(result))) return 1;
        a->error=1;return 0;
    }
    if(!RogueTarget_Observe(a,remaining,total)) return 0;
    a->result=result;a->finalized=1;a->end_reason=reason;return 1;
}

int RogueTarget_Finalize(RogueTargetAttempt* a, unsigned outcome,
    unsigned remaining, unsigned total, unsigned frames, unsigned goal)
{
    if(outcome!=1 && outcome!=4 && outcome!=6) {
        RogueTarget_Interrupt(a);return 0;
    }
    return finalize(a,outcome,remaining,total,frames,goal,outcome==6 ? ROGUE_EVENT_END_COMPLETE :
        outcome==1 ? ROGUE_EVENT_END_TIMEOUT : ROGUE_EVENT_END_FALL);
}

void RogueTarget_Interrupt(RogueTargetAttempt* a)
{
    if(!a || !a->run_id) return;
    a->error=1;a->end_reason=ROGUE_EVENT_END_INTERRUPTED;
}

int RogueTarget_End(RogueTargetAttempt* a, unsigned reason, unsigned remaining,
    unsigned total, unsigned frames, unsigned goal)
{
    if(!a || !a->active || (a->finalized && !a->error) ||
        (reason!=ROGUE_EVENT_END_SKIP && reason!=ROGUE_EVENT_END_QUIT)) return 0;
    if(a->error) {
        if(reason!=ROGUE_EVENT_END_QUIT) return 0;
        /* Abandonment records the user's action, not unverified stage data. */
        memset(&a->result,0,sizeof(a->result));
        a->result.run_id=a->run_id;a->result.node_id=a->node_id;
        a->result.kind=ROGUE_RESULT_SKILL;a->result.submatch_count=1;
        a->result.measured=1;a->result.debug_forced=a->forced;
        a->result.carried_percent=a->incoming_percent;
        a->result.event_end=reason;a->result.outcome=ROGUE_RESULT_QUIT;
        a->finalized=1;a->error=0;a->end_reason=reason;return 1;
    }
    return finalize(a,7,remaining,total,frames,goal,reason);
}

unsigned RogueTarget_Settle(RogueTargetAttempt* a, RogueRun* run)
{
    unsigned status;
    if(!a || !run || !a->finalized || a->error || a->run_id!=run->run_id) return ROGUE_SETTLEMENT_INVALID;
    status=RogueRun_Resolve(run,&a->result);
    if(status==ROGUE_SETTLEMENT_COMMITTED || status==ROGUE_SETTLEMENT_ALREADY_COMMITTED) a->active=0;
    return status;
}
