#ifndef ROGUE_DIRECTOR_H
#define ROGUE_DIRECTOR_H
#include "../core/run.h"
#include "rogue_runtime.h"
RogueRun* RogueDirector_Run(void);
typedef RogueSeed (*RogueAttemptIdSource)(void);
void RogueDirector_SetIdSource(RogueAttemptIdSource source);
void RogueDirector_Start(RogueSeed seed, unsigned character);
void RogueDirector_End(void);
#endif
