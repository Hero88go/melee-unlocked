/* ACE PlLc2.dat, native callbacks. The state and attribute layouts are Ness's. */
#ifndef MU_ACE_LUCAS2_H
#define MU_ACE_LUCAS2_H

#include <Runtime/platform.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftNess/types.h>
#include <melee/ft/kinds/ftNess/forward.h>
#include <melee/it/types.h>
#include <melee/it/kinds/types.h>
#include <sysdolphin/baselib/forward.h>
#include "../../akaneia/mu_ak_fighter.h"
#include <stddef.h>
#include <string.h>

typedef struct ftLc2_FighterVars {
    HSD_GObj* yoyo_gobj;
    Vec3 yoyo_hitbox_pos;
    float x223C;
    HSD_GObj* pkflash_gobj;
    HSD_GObj* pkthunder_gobj;
    HSD_GObj* bat_gobj;
    u32 pkthunder_gfx;
    float energy;
} ftLc2_FighterVars;

_Static_assert(offsetof(ftLc2_FighterVars, energy) ==
               offsetof(struct ftNess_FighterVars, pkthunder_gfx) + sizeof(u32),
               "PlLc2 extra word follows Ness fighter variables");
_Static_assert(sizeof(ftLc2_FighterVars) <= sizeof(((Fighter*) 0)->u),
               "PlLc2 fighter variables fit");

static inline ftLc2_FighterVars* ftLc2_Vars(Fighter* fp)
{
    return (ftLc2_FighterVars*) &fp->u;
}

static inline float ftLc2_FloatBits(u32 bits)
{
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static inline Item_GObj** ftLc2_KirbyFlashSlot(Fighter* fp)
{
    return (Item_GObj**) &fp->u.kb.x44;
}

void ftLc2_Init_OnDamage(HSD_GObj*);
void ftLc2_DamageScale(HSD_GObj*);
void ftLc2_ColorScale(HSD_GObj*);
Item_GObj* ftLc2_Thunder_Spawn(HSD_GObj*, Vec3*, float);
Item_GObj* ftLc2_Fire_Spawn(HSD_GObj*, Vec3*, ItemKind, float);
extern ItemLogicTable ftLc2_ArticleLogic[9];

#include "lucas2_specialn.h"
#include "lucas2_specials.h"
#include "lucas2_specialhi.h"
#include "lucas2_speciallw.h"

#endif
