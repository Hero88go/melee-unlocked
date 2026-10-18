/* ACE's Metal Sonic, down special: Fox's Reflector rows (360 to 369) with his own entries, his
 * own animation callbacks (each one Fox's, then the effect hook cleared) and his own ground
 * physics. None of the ten rows has an interrupt callback (Fox's turn and release checks are
 * gone).
 *
 * The ground entry is Fox's without the start effect hook; the air entry calls Fox's and then
 * clears the hook. Each animation callback calls Fox's (by its address, through a register) and
 * then sets accessory4_cb to NULL, so the effect hooks Fox's animation callbacks arm
 * (ftFx_SpecialLw_CreateLoopGFX) never run. Fox's collision and physics callbacks that his rows
 * keep can still arm one (NOTES.md, retail effect ids).
 *
 * The ground physics (+4CB4, rows 360, 361, 362, 364) has a mod bug: it calls
 * ftCommon_CalcGroundAccel_Deaccel with the GObj in place of the Fighter. On the console that
 * reads the word at GObj+EC and writes one at GObj+E4, past the 0x38-byte GObj, in whatever the
 * GObj pool holds next; the fighter's own ground acceleration is not written. Natively the call
 * is not made (a native HSD_GObj is another size and that write would corrupt native memory); the
 * fighter is left as on the console. Logged once. */
#include "metalsonic.h"

#include <dolphin/os.h>

#include <melee/ft/fighter.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftFox/ftfoxspeciallw.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>

/* +0818 (slot 10, speciallw): state 360 from frame 0, ftAnim_8006EBA4, then Fox's reflector words
 * (ftFox_SpecialLw_SetVars without its last line): releaseLag = attrs +98 truncated (fctiwz),
 * isRelease = 0, gravityDelay = attrs +A4, cmd_vars[1] = 4. Fox's start effect hook is not set. */
void ftNm_SpecialLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftFox_DatAttrs* da;

    Fighter_ChangeMotionState(gobj, ftNm_MS_SpecialLwStart, Ft_MF_None, 0.0f, 1.0f, 0.0f, NULL);
    ftAnim_8006EBA4(gobj);
    da = fp->dat_attrs;
    fp->mv.fx.SpecialLw.isRelease = 0;
    fp->mv.fx.SpecialLw.gravityDelay = da->xA4_FOX_REFLECTOR_GRAVITY_DELAY;
    fp->cmd_vars[1] = 4;
    fp->mv.fx.SpecialLw.releaseLag = (s32) da->x98_FOX_REFLECTOR_RELEASE_LAG;
}

/* +08A4 (slot 11, specialairlw): Fox's ftFx_SpecialAirLw_Enter (0x800E85EC, through a register),
 * then the start effect hook it set is cleared. */
void ftNm_SpecialAirLw_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftFx_SpecialAirLw_Enter(gobj);
    fp->accessory4_cb = NULL;
}

/* +4C70, +4CEC, +4D30, +4D74, +4DBC, +4E00, +4E44, +4E88, +4ECC, +4F10: ten callbacks of the same
 * 17 instructions, each with another Fox address (the "retail" column of tables.txt). The
 * Fighter is read again after the call (Fox's may have changed the state, not the Fighter). */
#define FTNM_LW_ANIM(name, fox)                                                                    \
    void name(HSD_GObj* gobj)                                                                      \
    {                                                                                              \
        fox(gobj);                                                                                 \
        GET_FIGHTER(gobj)->accessory4_cb = NULL;                                                   \
    }

/* +4C70: 0x800E8694 */ FTNM_LW_ANIM(ftNm_SpecialLwStart_Anim, ftFx_SpecialLwStart_Anim)
/* +4CEC: 0x800E8A24 */ FTNM_LW_ANIM(ftNm_SpecialLwLoop_Anim, ftFx_SpecialLwLoop_Anim)
/* +4D30: 0x800E965C */ FTNM_LW_ANIM(ftNm_SpecialLwHit_Anim, ftFx_SpecialLwHit_Anim)
/* +4D74: 0x800E9B40 */ FTNM_LW_ANIM(ftNm_SpecialLwEnd_Anim, ftFx_SpecialLwEnd_Anim)
/* +4DBC: 0x800E8FDC */ FTNM_LW_ANIM(ftNm_SpecialLwTurn_Anim, ftFx_SpecialLwTurn_Anim)
/* +4E00: 0x800E8720 */ FTNM_LW_ANIM(ftNm_SpecialAirLwStart_Anim, ftFx_SpecialAirLwStart_Anim)
/* +4E44: 0x800E8A9C */ FTNM_LW_ANIM(ftNm_SpecialAirLwLoop_Anim, ftFx_SpecialAirLwLoop_Anim)
/* +4E88: 0x800E97B4 */ FTNM_LW_ANIM(ftNm_SpecialAirLwHit_Anim, ftFx_SpecialAirLwHit_Anim)
/* +4ECC: 0x800E9B84 */ FTNM_LW_ANIM(ftNm_SpecialAirLwEnd_Anim, ftFx_SpecialAirLwEnd_Anim)
/* +4F10: 0x800E90EC */ FTNM_LW_ANIM(ftNm_SpecialAirLwTurn_Anim, ftFx_SpecialAirLwTurn_Anim)

/* +4CB4: the ground rows' physics: ftColl_8007AEF8 (the reflector's position update flag
 * cleared), then the console's ftCommon_CalcGroundAccel_Deaccel((Fighter*) gobj, 0.02), not made
 * natively (see the top of this file). No ground friction, no ground to self movement. */
void ftNm_SpecialLw_Phys(HSD_GObj* gobj)
{
    ftColl_8007AEF8(gobj);
    {
        static int logged;
        if (!logged) {
            logged = 1;
            OSReport("[ak] Metal Sonic: the down special's ground physics (+4CB4) skips the "
                     "file's friction call on the GObj (mod bug, not reproduced)\n");
        }
    }
}

/* +4DB8: 363's physics: ground friction (ft_80084F3C), a tail call. */
void ftNm_SpecialLwEnd_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
}
