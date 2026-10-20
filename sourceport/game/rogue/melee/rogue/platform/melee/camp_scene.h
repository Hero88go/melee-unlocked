#ifndef ROGUE_CAMP_SCENE_H
#define ROGUE_CAMP_SCENE_H
#include <melee/mn/forward.h>
int RogueCampScene_Active(void);
void RogueCampScene_Prepare(StartMeleeData* match, unsigned port);
void RogueCampScene_Frame(void);
void RogueCampScene_End(void);
#endif
