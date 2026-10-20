#ifndef ROGUE_RACE_ATTEMPT_H
#define ROGUE_RACE_ATTEMPT_H
#include "run.h"
typedef struct RogueRaceAttempt {
    RogueSeed run_id;
    unsigned node_id,incoming_percent,base_character,limit_frames,assist,forced;
    unsigned active,finalized,error,checkpoints;
    RogueNodeResult result;
} RogueRaceAttempt;
void RogueRace_Reset(RogueRaceAttempt*);
int RogueRace_RemainingFrames(unsigned limit,unsigned seconds,unsigned subframe,unsigned* frames);
int RogueRace_Begin(RogueRaceAttempt*,const RogueRun*,unsigned forced);
int RogueRace_Checkpoint(RogueRaceAttempt*,unsigned index);
int RogueRace_Finalize(RogueRaceAttempt*,unsigned native_outcome,unsigned goal,unsigned remaining_frames);
int RogueRace_End(RogueRaceAttempt*,unsigned reason,unsigned remaining_frames);
void RogueRace_Interrupt(RogueRaceAttempt*);
unsigned RogueRace_Settle(RogueRaceAttempt*,RogueRun*);
#endif
