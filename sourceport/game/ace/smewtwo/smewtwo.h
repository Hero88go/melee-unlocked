/* PlSm.dat, native 46, m-ex internal 39, external 38. */
#ifndef MU_ACE_SMEWTWO_H
#define MU_ACE_SMEWTWO_H
#include <Runtime/platform.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftMewtwo/types.h>
#include "../../akaneia/mu_ak_fighter.h"

typedef struct ftSm_Vars {
    struct ftMewtwo_FighterVars mewtwo;
    u8 reserved[0x80-sizeof(struct ftMewtwo_FighterVars)];
    /* 22AC */ int cancel;
    /* 22B0 */ int jump_input;
    /* 22B4 */ int landing;
    /* 22B8 */ Fighter* targets[4];
    /* 22C8 */ int level;
    /* 22CC */ int capped;
    /* 22D0 */ int power;
    /* 22D4 */ int air_used;
    /* 22D8 */ int cooldown;
    /* 22DC */ u8 hitbox_upgraded[4];
    /* 22E0 */ int transformed;
    /* 22E4 */ float energy;
    /* 22E8 */ int e8;
    /* 22EC */ int jump_used;
    /* 22F0 */ int jump_timer;
    /* 22F4 */ int f4;
    /* 22F8 */ HSD_GObj* hud;
} ftSm_Vars;
_Static_assert(sizeof(ftSm_Vars)<=sizeof(((Fighter*)0)->u),"S. Mewtwo variables fit");
static inline ftSm_Vars* ftSm_GetVars(Fighter* fp) { return (ftSm_Vars*)&fp->u; }
static inline void ftSm_Change(HSD_GObj* g,int state,MotionFlags flags,float frame,float speed,float blend)
{ Fighter_ChangeMotionState(g,state,flags,frame,speed,blend,NULL); }
void ftSm_SpecialN_Enter(HSD_GObj*);
void ftSm_SpecialAirN_Enter(HSD_GObj*);
void ftSm_SpecialS_Enter(HSD_GObj*);
void ftSm_SpecialAirS_Enter(HSD_GObj*);
void ftSm_SpecialHi_Enter(HSD_GObj*);
void ftSm_SpecialAirHi_Enter(HSD_GObj*);
void ftSm_SpecialLw_Enter(HSD_GObj*);
void ftSm_SpecialAirLw_Enter(HSD_GObj*);
void ftSm_Transform(HSD_GObj*);
void ftSm_ResetTransform(HSD_GObj*);
void ftSm_Trails_OnLoad(HSD_GObj*);
HSD_GObj* ftSm_Hud_Create(HSD_GObj*);
void ftSm_Projectiles_Spawn(HSD_GObj*);
void ftSm_SpecialN_Aura(HSD_GObj*);
void ftSm_CB_10A0(HSD_GObj*);
void ftSm_CB_1118(HSD_GObj*);
void ftSm_CB_2634(HSD_GObj*);
void ftSm_CB_2638(HSD_GObj*);
void ftSm_CB_2704(HSD_GObj*);
void ftSm_CB_1B80(HSD_GObj*);
void ftSm_CB_1CC8(HSD_GObj*);
bool ftSm_CB_1CDC(HSD_GObj*);
void ftSm_HitboxUpgrade(HSD_GObj*);
void ftSm_TargetsCheck(HSD_GObj*);
void ftSm_CB_3328(HSD_GObj*);
void ftSm_CB_3420(HSD_GObj*);
void ftSm_UpdateLevel(Fighter*);
void ftSm_OnFrame(HSD_GObj*);
void ftSm_OnStateChange(HSD_GObj*);
void ftSm_OnRespawn(HSD_GObj*);
void ftSm_OnLanding(HSD_GObj*);
void ftSm_DoubleJump(HSD_GObj*);
#include "smewtwo_callbacks.inc"
#endif
