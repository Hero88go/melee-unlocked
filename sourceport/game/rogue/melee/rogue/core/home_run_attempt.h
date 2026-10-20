#ifndef ROGUE_HOME_RUN_ATTEMPT_H
#define ROGUE_HOME_RUN_ATTEMPT_H
#include "run.h"

/* Values only: survives match teardown without retaining native objects. */
typedef struct RogueHomeRunAttempt {
    RogueSeed run_id;
    unsigned node_id, incoming_percent, active, observed, finalized, error;
    unsigned display, unit, ready, debug_forced, end_reason;
    RoguePoints raw;
    RogueNodeResult result;
} RogueHomeRunAttempt;

int RogueHomeRun_Begin(RogueHomeRunAttempt*, const RogueRun*, unsigned debug_forced);
void RogueHomeRun_Observe(RogueHomeRunAttempt*, RoguePoints raw, unsigned display,
    unsigned unit, unsigned ready);
int RogueHomeRun_Finalize(RogueHomeRunAttempt*, unsigned mode, RoguePoints raw, unsigned timed_out);
int RogueHomeRun_End(RogueHomeRunAttempt*, unsigned reason);
void RogueHomeRun_Interrupt(RogueHomeRunAttempt*);
unsigned RogueHomeRun_Settle(RogueHomeRunAttempt*, RogueRun*);
void RogueHomeRun_Reset(RogueHomeRunAttempt*);
#endif
