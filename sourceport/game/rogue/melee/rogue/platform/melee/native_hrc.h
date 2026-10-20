#ifndef ROGUE_NATIVE_HRC_H
#define ROGUE_NATIVE_HRC_H
#include <melee/mn/forward.h>
int RogueHrc_Prepare(StartMeleeData*, unsigned port);
void RogueHrc_Observe(int raw, int display, unsigned us, unsigned ready);
void RogueHrc_Finalized(unsigned mode, int raw);
int RogueHrc_Finish(void);
void RogueHrc_Reset(void);
void RogueHrc_Frame(void);
void RogueHrc_UiEnd(void);
int RogueHrc_HasError(void);
void RogueHrc_Abandon(void);
#endif
