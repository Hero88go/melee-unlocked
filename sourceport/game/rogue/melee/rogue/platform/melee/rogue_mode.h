#ifndef ROGUE_MODE_H
#define ROGUE_MODE_H
#include <melee/gm/types.h>
#include <sysdolphin/baselib/forward.h>
extern GameModeState RogueMode_States[];
void RogueMode_Load(void);
void RogueMode_Unload(void);
void RogueMode_Enter(void* data);
void RogueMode_Exit(void* data);
void RogueMode_Frame(void);
void RogueMode_NativeRouteFrame(void);
void RogueMode_NativeOverlayEnter(void);
void RogueMode_NativeOverlayEnd(void);
bool RogueMode_CharacterUnlocked(u8 kind);
void RogueMode_MatchFrame(void);
void RogueMode_CssHeading(struct HSD_JObj* heading);
#endif
