#ifndef ROGUE_PLAY_CONTEXT_H
#define ROGUE_PLAY_CONTEXT_H
#include "run.h"
enum RoguePlayKind { ROGUE_PLAY_NONE, ROGUE_PLAY_COMBAT, ROGUE_PLAY_SKILL,
                     ROGUE_PLAY_GAUNTLET, ROGUE_PLAY_CAMP };
enum RogueActorRole { ROGUE_ACTOR_NONE, ROGUE_ACTOR_PLAYER,
    ROGUE_ACTOR_COMBAT_OPPONENT, ROGUE_ACTOR_PRACTICE,
    ROGUE_ACTOR_STAGE, ROGUE_ACTOR_OBJECTIVE };
unsigned RoguePlay_Kind(const RogueRun* run);
unsigned RoguePlay_FighterRole(const RogueRun* run, unsigned logical_slot);
int RoguePlay_PlayerEffects(unsigned play, unsigned role);
int RoguePlay_BorrowedMoves(unsigned play, unsigned role, int native_kit_selected);
int RoguePlay_CombatProc(unsigned play, unsigned source_role, unsigned target_role);
int RoguePlay_NativeKit(const RogueRun* run);
/* Structural preflight only: EQUIPPED does not certify course completion. */
enum RogueEventKit { ROGUE_EVENT_KIT_INVALID, ROGUE_EVENT_KIT_NATIVE,
                     ROGUE_EVENT_KIT_EQUIPPED };
unsigned RoguePlay_EventKit(const RogueRun* run, const RogueEventDesc* event);
const char* RoguePlay_EventKitLabel(const RogueRun* run, const RogueEventDesc* event);
unsigned RoguePlay_Special(const RogueRun* run, unsigned slot);
unsigned RoguePlay_Aerial(const RogueRun* run, unsigned slot);
#endif
