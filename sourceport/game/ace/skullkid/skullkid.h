/* PlSd.dat, internal 40, external 39, native 47. */
#ifndef MU_ACE_SKULLKID_H
#define MU_ACE_SKULLKID_H
#include <Runtime/platform.h>
#include <melee/ft/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/kinds/ftNess/types.h>
#include <sysdolphin/baselib/forward.h>
#include "../../akaneia/mu_ak_fighter.h"

/* Keep the borrowed Ness fields in their native layout. Own item references
 * are native pointers; names below identify their console offsets. */
typedef struct ftSd_FighterVars {
    struct ftNess_FighterVars ness;
    u8 reserved[0x98 - sizeof(struct ftNess_FighterVars)];
    /* 22C4 */ int float_used;
    /* 22C8 */ int c8;
    /* 22CC */ int landing_timer;
    /* 22D0 */ int d0;
    /* 22D4 */ Item_GObj* model1;
    /* 22D8 */ Item_GObj* model2;
    /* 22DC */ int recharge;
    /* 22E0 */ int e0;
    /* 22E4 */ int e4;
    /* 22E8 */ int e8;
    /* 22EC */ int ec;
    /* 22F0 */ int f0;
    /* 22F4 */ int ready;
    /* 22F8 */ Item_GObj* spell;
} ftSd_FighterVars;
_Static_assert(sizeof(ftSd_FighterVars) <= sizeof(((Fighter*)0)->u), "Skull Kid vars fit");
static inline ftSd_FighterVars* ftSd_Vars(Fighter* fp)
{
    return (ftSd_FighterVars*)&fp->u;
}
static inline void ftSd_Change(HSD_GObj* gobj, int state, MotionFlags flags,
                              float frame, float speed, float blend)
{
    Fighter_ChangeMotionState(gobj, state, flags, frame, speed, blend, NULL);
}
void ftSd_SpecialN_Enter(HSD_GObj*);
void ftSd_SpecialAirN_Enter(HSD_GObj*);
void ftSd_SpecialS_Enter(HSD_GObj*);
void ftSd_SpecialAirS_Enter(HSD_GObj*);
void ftSd_SpecialHi_Enter(HSD_GObj*);
void ftSd_SpecialAirHi_Enter(HSD_GObj*);
void ftSd_SpecialLw_Enter(HSD_GObj*);
void ftSd_SpecialAirLw_Enter(HSD_GObj*);
void ftSd_SpecialN_Spawn(HSD_GObj*);
void ftSd_OnFrame(HSD_GObj*);
void ftSd_OnStateChange(HSD_GObj*);
void ftSd_EnterFloat(HSD_GObj*);
void ftSd_OnLanding(HSD_GObj*);
void ftSd_Trails_OnLoad(HSD_GObj*);
void ftSd_Hi_GroundLaunch(HSD_GObj*);
void ftSd_Hi_AirLaunch(HSD_GObj*);
void ftSd_Hi_GroundEnd(HSD_GObj*);
void ftSd_Hi_AirEnd(HSD_GObj*);
void ftSd_HideHeld(Fighter*);
void ftSd_RestoreHeld(Fighter*);
Item_GObj* itSd_Spell_Spawn(HSD_GObj*, Vec3*, ItemKind, float);
Item_GObj* itSd_Model_Spawn(HSD_GObj*, Vec3*, ItemKind, int, int, float);
#include "skullkid_callbacks.inc"
#endif
