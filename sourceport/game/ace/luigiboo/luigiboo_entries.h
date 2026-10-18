/* Independent PlLb.dat fighter entries and callbacks, called by its table. */
#ifndef MU_ACE_LUIGIBOO_ENTRIES_H
#define MU_ACE_LUIGIBOO_ENTRIES_H

#include <melee/ft/forward.h>

void ftLb_SpecialN_Enter(Fighter_GObj* gobj);       /* +052C */
void ftLb_SpecialAirN_Enter(Fighter_GObj* gobj);    /* +0530 */
void ftLb_SpecialS_Enter(Fighter_GObj* gobj);       /* +056C */
void ftLb_SpecialAirS_Enter(Fighter_GObj* gobj);    /* +0590 */
void ftLb_SpecialLw_Enter(Fighter_GObj* gobj);      /* +06AC */
void ftLb_SpecialAirLw_Enter(Fighter_GObj* gobj);   /* +06B0 */
void ftLb_OnFrame(Fighter_GObj* gobj);             /* +06B4 */
void ftLb_SpecialS_Anim(Fighter_GObj* gobj);       /* +0A1C */
void ftLb_SpecialAirHi_IASA(Fighter_GObj* gobj);   /* +0F3C */
void ftLb_SpecialAirHi_Phys(Fighter_GObj* gobj);   /* +0F5C */
void ftLb_Recovery_Anim(Fighter_GObj* gobj);       /* +10A4 */
void ftLb_Recovery_IASA(Fighter_GObj* gobj);       /* +1118 */

#endif
