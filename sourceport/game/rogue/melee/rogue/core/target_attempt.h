#ifndef ROGUE_TARGET_ATTEMPT_H
#define ROGUE_TARGET_ATTEMPT_H
#include "run.h"

typedef struct RogueTargetAttempt {
    RogueSeed run_id;
    unsigned node_id, incoming_percent, base_character, stage, assist, forced;
    unsigned active, observed, remaining, finalized, error, end_reason;
    RogueNodeResult result;
} RogueTargetAttempt;

/* Sheik's raw prototype course has three targets, not the stock Zelda course.
 * Exclude it until the native shared-form policy is verified. */
unsigned RogueTarget_Stage(unsigned base_character);
int RogueTarget_ResultValid(const RogueNodeResult*);
void RogueTarget_Reset(RogueTargetAttempt*);
int RogueTarget_Begin(RogueTargetAttempt*, const RogueRun*, unsigned forced);
int RogueTarget_Observe(RogueTargetAttempt*, unsigned remaining, unsigned total);
int RogueTarget_Finalize(RogueTargetAttempt*, unsigned native_outcome,
    unsigned remaining, unsigned total, unsigned frames, unsigned native_goal);
unsigned RogueTarget_Settle(RogueTargetAttempt*, RogueRun*);
void RogueTarget_Interrupt(RogueTargetAttempt*);
int RogueTarget_End(RogueTargetAttempt*, unsigned reason, unsigned remaining,
    unsigned total, unsigned frames, unsigned native_goal);
#endif
