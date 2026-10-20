#include "home_run_attempt.h"
#include "event_metrics.h"
#include <string.h>

void RogueHomeRun_Reset(RogueHomeRunAttempt* a)
{ if(a) memset(a,0,sizeof(*a)); }

int RogueHomeRun_Begin(RogueHomeRunAttempt* a, const RogueRun* run, unsigned debug_forced)
{
    const RogueEventOpportunity* opportunity;
    if(!a || !run || a->active || a->finalized || !run->run_id ||
        run->phase!=ROGUE_EVENT || run->floor>=ROGUE_FLOORS ||
        run->current.kind!=ROGUE_NODE_EVENT || !run->current.node_id ||
        (run->current.node_id-1)/2!=run->floor || run->carried_percent>999 ||
        run->current.payload.event.template_id!=ROGUE_EVENT_HOME_RUN ||
        run->current.payload.event.native_stage!=84 ||
        run->current.payload.event.timer_frames!=600 ||
        run->current.payload.event.base_character!=run->character ||
        run->receipts[run->floor].committed || debug_forced>1) return 0;
    opportunity=&run->event_opportunities[run->floor/5];
    if(!opportunity->present || opportunity->floor!=run->floor ||
        memcmp(&opportunity->event,&run->current.payload.event,sizeof(opportunity->event))) return 0;
    RogueHomeRun_Reset(a);
    a->run_id=run->run_id; a->node_id=run->current.node_id;
    a->incoming_percent=run->carried_percent; a->debug_forced=debug_forced;
    a->active=1; return 1;
}

void RogueHomeRun_Observe(RogueHomeRunAttempt* a, RoguePoints raw, unsigned display,
    unsigned unit, unsigned ready)
{
    if(!a || !a->active || a->finalized || a->error) return;
    if(raw<0 || ready>1 || (unit!=ROGUE_DISTANCE_TENTHS_FOOT && unit!=ROGUE_DISTANCE_DECIMETERS)) {
        a->error=1; return;
    }
    a->raw=raw; a->display=display; a->unit=unit; a->ready=ready; a->observed=1;
}

int RogueHomeRun_Finalize(RogueHomeRunAttempt* a, unsigned mode, RoguePoints raw, unsigned timed_out)
{
    unsigned reason=mode==0 ? ROGUE_EVENT_END_COMPLETE :
        timed_out ? ROGUE_EVENT_END_TIMEOUT : ROGUE_EVENT_END_NO_DISTANCE;
    if(!a || !a->active || a->error || timed_out>1) return 0;
    if(a->finalized) {
        if(a->result.home_run.present && a->result.event_end==reason &&
            a->result.home_run.native_mode==mode && a->result.home_run.raw_centimeters==raw) return 1;
        a->error=1; return 0;
    }
    if(!a->observed || !a->ready || a->raw!=raw ||
        !RogueEvent_HomeRunResult(a->run_id,a->node_id,a->incoming_percent,mode,
            raw,a->display,a->unit,&a->result)) { a->error=1; return 0; }
    if(mode==2 && timed_out) a->result.event_end=ROGUE_EVENT_END_TIMEOUT;
    a->result.debug_forced=a->debug_forced;
    a->end_reason=a->result.event_end;
    a->finalized=1; return 1;
}

void RogueHomeRun_Interrupt(RogueHomeRunAttempt* a)
{
    if(!a || !a->run_id) return;
    a->error=1; a->end_reason=ROGUE_EVENT_END_INTERRUPTED;
}

int RogueHomeRun_End(RogueHomeRunAttempt* a, unsigned reason)
{
    if(!a || !a->active || (a->finalized && !a->error) ||
        (a->error && reason!=ROGUE_EVENT_END_QUIT) ||
        (reason!=ROGUE_EVENT_END_SKIP && reason!=ROGUE_EVENT_END_FALL &&
         reason!=ROGUE_EVENT_END_QUIT)) return 0;
    memset(&a->result,0,sizeof(a->result));
    a->result.run_id=a->run_id; a->result.node_id=a->node_id;
    a->result.kind=ROGUE_RESULT_SKILL; a->result.submatch_count=1;
    a->result.measured=1; a->result.debug_forced=a->debug_forced;
    a->result.carried_percent=a->incoming_percent;
    a->result.event_end=reason;
    a->result.outcome=reason==ROGUE_EVENT_END_QUIT ? ROGUE_RESULT_QUIT : ROGUE_RESULT_FAILURE;
    a->finalized=1; a->end_reason=reason; a->error=0;
    return 1;
}

unsigned RogueHomeRun_Settle(RogueHomeRunAttempt* a, RogueRun* run)
{
    unsigned status;
    if(!a || !run || a->error || !a->finalized || run->run_id!=a->run_id)
        return ROGUE_SETTLEMENT_INVALID;
    status=RogueRun_Resolve(run,&a->result);
    if(status==ROGUE_SETTLEMENT_COMMITTED || status==ROGUE_SETTLEMENT_ALREADY_COMMITTED) a->active=0;
    return status;
}
