#ifndef ROGUE_QA_NATIVE_HRC_H
#define ROGUE_QA_NATIVE_HRC_H
#include <melee/mn/forward.h>
void RogueHrcProbe_Prepare(StartMeleeData* match, unsigned port);
void RogueHrcProbe_Finish(void);
void RogueHrcProbe_Reset(void);
void RogueHrcProbe_Observe(int raw, int display, unsigned us, unsigned ready,
    unsigned mode);
void RogueHrcProbe_Finalized(unsigned mode, int raw);
#endif
