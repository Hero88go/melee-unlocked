#ifndef ROGUE_NATIVE_RACE_H
#define ROGUE_NATIVE_RACE_H
#include <melee/mn/forward.h>
int RogueNativeRace_Prepare(StartMeleeData*, unsigned port);
int RogueNativeRace_Finish(void);
void RogueNativeRace_Frame(void);
void RogueNativeRace_Reset(void);
void RogueNativeRace_UiEnd(void);
int RogueNativeRace_HasError(void);
void RogueNativeRace_Abandon(void);
#endif
