/* ACE 2.0.0 PlRc.dat. Offsets refer to the file's ftFunction block.
 * Evidence: run-source/rel09-ace-native/raichu/verified16/.
 * The descriptor is added only after every load path and callback is ported. */
#ifndef MU_ACE_RAICHU_H
#define MU_ACE_RAICHU_H

#include <Runtime/platform.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftPikachu/types.h>
#include <sysdolphin/baselib/forward.h>
#include "../../akaneia/mu_ak_fighter.h"
#include <stddef.h>

typedef struct ftRc_FighterVars {
    u8 reserved[0xC0];
    /* fp+22EC */ s32 can_cancel_hi;
    /* fp+22F0 */ s32 unused;
    /* fp+22F4 */ s32 charge;
} ftRc_FighterVars;

_Static_assert(offsetof(ftRc_FighterVars, charge) == 0xC8, "PlRc charge offset");
_Static_assert(sizeof(ftRc_FighterVars) <= sizeof(((Fighter*) 0)->u), "Raichu variables fit");

static inline ftRc_FighterVars* ftRc_Vars(Fighter* fp)
{
    return (ftRc_FighterVars*) &fp->u;
}
static inline ftPikachuAttributes* ftRc_Attrs(Fighter* fp)
{
    return (ftPikachuAttributes*) fp->dat_attrs;
}

/* Entry points exported by PlRc.dat. */
void ftRc_SpecialN_Enter(HSD_GObj*);
void ftRc_SpecialAirN_Enter(HSD_GObj*);
void ftRc_SpecialS_Enter(HSD_GObj*);
void ftRc_SpecialAirS_Enter(HSD_GObj*);
void ftRc_SpecialHi_Enter(HSD_GObj*);
void ftRc_SpecialAirHi_Enter(HSD_GObj*);

/* Own callbacks, in table order. */
void ftRc_SpecialN_Anim(HSD_GObj*);
void ftRc_SpecialN_Phys(HSD_GObj*);
void ftRc_SpecialN_Coll(HSD_GObj*);
void ftRc_SpecialAirN_Phys(HSD_GObj*);
void ftRc_SpecialAirN_Coll(HSD_GObj*);
void ftRc_SpecialSStart_Anim(HSD_GObj*);
void ftRc_Empty(HSD_GObj*);
void ftRc_SpecialSStart_Phys(HSD_GObj*);
void ftRc_SpecialSStart_Coll(HSD_GObj*);
void ftRc_SpecialSHold_Anim(HSD_GObj*);
void ftRc_SpecialSHold_IASA(HSD_GObj*);
void ftRc_SpecialSHold_Phys(HSD_GObj*);
void ftRc_SpecialSHold_Coll(HSD_GObj*);
void ftRc_SpecialSEnd_Coll(HSD_GObj*);
void ftRc_SpecialAirSStart_Anim(HSD_GObj*);
void ftRc_SpecialAirSStart_Phys(HSD_GObj*);
void ftRc_SpecialAirSStart_Coll(HSD_GObj*);
void ftRc_SpecialAirSHold_Anim(HSD_GObj*);
void ftRc_SpecialAirSHold_Coll(HSD_GObj*);
void ftRc_SpecialAirSEnd_Anim(HSD_GObj*);
void ftRc_SpecialAirSEnd_Coll(HSD_GObj*);
void ftRc_SpecialAirS0_Anim(HSD_GObj*);
void ftRc_SpecialAirS0_Phys(HSD_GObj*);
void ftRc_SpecialAirS0_Coll(HSD_GObj*);
void ftRc_SpecialAirHiEnd_IASA(HSD_GObj*);
void ftRc_SpecialAirHiEnd_Coll(HSD_GObj*);
void ftRc_SpecialHiCancel_Anim(HSD_GObj*);
void ftRc_SpecialHiCancel_IASA(HSD_GObj*);
void ftRc_SpecialHiCancel_Phys(HSD_GObj*);

/* Own article 1 spawner, +21FC. */
Item_GObj* itRc_Jolt_Spawn(HSD_GObj*, Vec3*, ItemKind, GroundOrAir, float);
bool itRc_Jolt_CheckSurfacesFrom(Item_GObj*, bool);

#endif
