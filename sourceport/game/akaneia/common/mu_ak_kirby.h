/* Kirby's copy abilities for the fighters Akaneia adds: the shared layer.
 *
 * Akaneia ships one hat file per added fighter (PlKbCpWf.dat and so on, named in MxDt "kirby"),
 * each with three public objects next to the hat data itself:
 *
 *   ftDataKirbyCopy<Name>   the hat: the retail KirbyHatStruct (model, visibility table) followed
 *                           by whatever the ability needs (article data, parameters)
 *   ftcmd                   the ability's animations and their scripts, 0x18 bytes each
 *   kbFunction              the ability's PowerPC (m-ex "kbFunction" exports), not used here
 *   itFunction              the PowerPC of the ability's articles, not used here
 *
 * The PowerPC is rewritten in C per fighter (akaneia/<fighter>/<fighter>_kirby.c) and described by
 * one MuAkKirbyCopy, which the fighter's MuAkFighter points at. This file is what that C calls in
 * place of the m-ex runtime routines, and what the decomp's Kirby code calls at the places the
 * m-ex engine patches hook (LAYER.md lists every site). */
#ifndef MU_AK_KIRBY_H
#define MU_AK_KIRBY_H

#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/lb/forward.h>
#include <melee/lb/types.h>
#include <sysdolphin/baselib/forward.h>

#include "mu_disc.h"

/* One entry of a hat file's "ftcmd" table. The layout is the game's own animation table entry
 * (Fighter_WaitAnimData); the difference is +4, which in a fighter's file is an offset into its
 * animation file and here points at the animation itself, inside the hat file. */
typedef struct MuAkKirbyCmd {
    /*  +0 */ DISC_PTR(const char) name;   /* "PlyKirby5K_Share_ACTION_WfSpecialN_figatree" */
    /*  +4 */ DISC_PTR(FigaTree) anim;
    /*  +8 */ u32 anim_size;               /* 0 in every Akaneia hat file */
    /*  +C */ DISC_PTR(CmdUnion) script;
    /* +10 */ s32 flags;                   /* goes to fp->x594_s32; 0 in every Akaneia hat file */
    /* +14 */ u32 x14;
} DISC_STRUCT MuAkKirbyCmd;

/* The m-ex "kbFunction" exports of one hat file, in export order (MexTK kbFunction.txt, plus the
 * per-frame slot 7 the engine reads). A NULL entry is an empty m-ex slot. */
typedef struct MuAkKirbyCopy {
    /* 0 */ void (*on_swallow)(HSD_GObj* gobj);       /* the ability was gained: put the hat on */
    /* 1 */ void (*on_lose)(HSD_GObj* gobj);          /* the ability is lost: take the hat off */
    /* 2 */ void (*special_n)(HSD_GObj* gobj);
    /* 3 */ void (*special_air_n)(HSD_GObj* gobj);
    /* 4 */ void (*on_hurt)(HSD_GObj* gobj);          /* Kirby's death callback, see LAYER.md */
    /* 5 */ void (*init_items)(FighterKind kind, KirbyHatStruct* hat);
    /* 6: the ability's action states. Entry n is KirbyStateChange state n; anim_id is an index
     * into the hat file's ftcmd table. The flags and the move id word are not read by m-ex. */
    const MotionState* move_logic;
    int move_logic_count;
    /* 7 */ void (*on_frame)(HSD_GObj* gobj);         /* once per frame while Kirby wears the hat */
} MuAkKirbyCopy;

/* ---- for the fighters' Kirby code ---- */

/* m-ex KirbyStateChange (console 803D7080): Kirby enters state `state` of the ability he holds.
 * `flags` is accepted for the callers' sake and ignored, as m-ex ignores it (its code overwrites
 * the argument). */
void mu_ak_kirby_state_change(Fighter_GObj* gobj, int state, MotionFlags flags, float anim_start,
                              float anim_speed, float anim_blend);
/* m-ex MEX_GetKirbyCpData (console 803D7084): the loaded hat data of a fighter kind. */
KirbyHatStruct* mu_ak_kirby_hat(FighterKind kind);
/* The loaded ftcmd table of a fighter kind, NULL when the scene has none. */
MuAkKirbyCmd* mu_ak_kirby_cmd(FighterKind kind);
/* What every Akaneia hat's swallow export does first: the retail hat attach (ftkirby.c
 * ftKb_SpecialN_800EFA40, Mario's) without the x2225_b2 write. */
void mu_ak_kirby_attach_hat(Fighter_GObj* gobj);

/* The functions the decomp's hook sites call (ftkirby.c, ftCo_ThrownKirby.c) and the two the
 * effect and sound code call (mu_ak_kirby_has_copy, mu_ak_kirby_reset, ..., mu_ak_kirby_copy_kind)
 * are declared in include/mu_native.h, which every game source gets, and nowhere else. */

#endif
