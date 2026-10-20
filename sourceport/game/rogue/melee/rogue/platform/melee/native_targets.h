#ifndef ROGUE_NATIVE_TARGETS_H
#define ROGUE_NATIVE_TARGETS_H
#include <melee/mn/forward.h>
int RogueTargets_Prepare(StartMeleeData*, unsigned port);
int RogueTargets_Finish(void);
void RogueTargets_Frame(void);
void RogueTargets_Reset(void);
void RogueTargets_UiEnd(void);
int RogueTargets_HasError(void);
void RogueTargets_Abandon(void);
void RogueTargets_TimeoutBoundaryProbe(void);
#endif
