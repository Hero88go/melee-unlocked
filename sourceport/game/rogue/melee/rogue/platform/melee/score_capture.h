#ifndef ROGUE_NATIVE_SCORE_CAPTURE_H
#define ROGUE_NATIVE_SCORE_CAPTURE_H
int RogueScore_Enter(void);
void RogueScore_ActorCreated(unsigned player, unsigned subfighter);
void RogueScore_BeginNative(void);
int RogueScore_CaptureBonus(unsigned id, int points);
void RogueScore_EndNative(int carry, int extra, int bonus, int time, int damage, int special, int display);
void RogueScore_ExitWithoutClear(void* match_end);
unsigned RogueScore_Finish(int won, const void* match_end);
unsigned RogueScore_TestRepeat(int won, const void* match_end);
int RogueScore_SuppressRecords(void);
int RogueScore_HasError(void);
void RogueScore_Retry(void);
void RogueScore_Abandon(void);
#endif
