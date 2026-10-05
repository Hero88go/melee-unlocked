/* Akaneia's Tails, native: the functions shared between the Tails files.
 * Names follow the console debug symbols in PlTs.dat's ftFunction (ftTs_ prefix added). */
#ifndef MU_AK_TAILS_H
#define MU_AK_TAILS_H

#include <Runtime/platform.h>

#include "ftTs_types.h"

#include <melee/ft/forward.h>
#include <melee/it/forward.h>
#include <melee/it/kinds/types.h>
#include <sysdolphin/baselib/forward.h>

#include "../common/mu_ak_kirby.h"

/* tails.c: the fighter's own callbacks (m-ex ftFunction exports) */
void ftTs_OnLoad(Fighter_GObj* gobj);
void ftTs_OnRespawn(Fighter_GObj* gobj);           /* m-ex "ondeath" slot */
void ftTs_OnDestroy(Fighter_GObj* gobj);           /* m-ex "onunknown" slot */
void ftTs_OnItemPickup(Fighter_GObj* gobj, bool catch_item);
void ftTs_OnItemInvisible(Fighter_GObj* gobj);
void ftTs_OnItemVisible(Fighter_GObj* gobj);
void ftTs_OnItemRelease(Fighter_GObj* gobj, bool drop_item);
void ftTs_OnItemCatch(Fighter_GObj* gobj, bool catch_item);
void ftTs_OnUnknownItemRelated(Fighter_GObj* gobj, bool drop_item);
void ftTs_EyeTextureDamaged(Fighter_GObj* gobj);   /* m-ex "onhit" slot */
void ftTs_EyeTextureNormal(Fighter_GObj* gobj);
void ftTs_OnFrame(Fighter_GObj* gobj);
void ftTs_OnActionStateChange(Fighter_GObj* gobj);
void ftTs_ResetAttributes(Fighter_GObj* gobj);     /* m-ex "onrespawn" slot */
void ftTs_OnModelRender(Fighter_GObj* gobj);
void ftTs_EnterDoubleJump(Fighter_GObj* gobj);
void ftTs_OnLanding(Fighter_GObj* gobj);
extern const MotionState ftTs_MotionStateTable[ftTs_MS_SelfCount];

/* ftTs_specialn.c */
void ftTs_SpecialN_Enter(Fighter_GObj* gobj);
void ftTs_SpecialAirN_Enter(Fighter_GObj* gobj);
void ftTs_SpecialN_Anim(Fighter_GObj* gobj);
void ftTs_SpecialN_IASA(Fighter_GObj* gobj);
void ftTs_SpecialN_Phys(Fighter_GObj* gobj);
void ftTs_SpecialN_Coll(Fighter_GObj* gobj);
void ftTs_SpecialAirN_Anim(Fighter_GObj* gobj);
void ftTs_SpecialAirN_IASA(Fighter_GObj* gobj);
void ftTs_SpecialAirN_Phys(Fighter_GObj* gobj);
void ftTs_SpecialAirN_Coll(Fighter_GObj* gobj);

/* ftTs_specials.c */
void ftTs_SpecialS_EnterAirOrGround(Fighter_GObj* gobj);
void ftTs_SpecialS_Start_Anim(Fighter_GObj* gobj);
void ftTs_SpecialS_Start_IASA(Fighter_GObj* gobj);
void ftTs_SpecialS_Start_Phys(Fighter_GObj* gobj);
void ftTs_SpecialS_Start_Coll(Fighter_GObj* gobj);
void ftTs_SpecialS_End_Anim(Fighter_GObj* gobj);
void ftTs_SpecialS_End_IASA(Fighter_GObj* gobj);
void ftTs_SpecialS_End_Phys(Fighter_GObj* gobj);
void ftTs_SpecialS_End_Coll(Fighter_GObj* gobj);
void ftTs_SpecialS_Loop_Anim(Fighter_GObj* gobj);
void ftTs_SpecialS_Loop_IASA(Fighter_GObj* gobj);
void ftTs_SpecialS_Loop_Phys(Fighter_GObj* gobj);
void ftTs_SpecialS_Loop_Coll(Fighter_GObj* gobj);
void ftTs_SpecialAirS_Start_Anim(Fighter_GObj* gobj);
void ftTs_SpecialAirS_Start_IASA(Fighter_GObj* gobj);
void ftTs_SpecialAirS_Start_Phys(Fighter_GObj* gobj);
void ftTs_SpecialAirS_Start_Coll(Fighter_GObj* gobj);
void ftTs_SpecialAirS_End_Anim(Fighter_GObj* gobj);
void ftTs_SpecialAirS_End_IASA(Fighter_GObj* gobj);
void ftTs_SpecialAirS_End_Phys(Fighter_GObj* gobj);
void ftTs_SpecialAirS_End_Coll(Fighter_GObj* gobj);
void ftTs_SpecialAirS_Loop_Anim(Fighter_GObj* gobj);
void ftTs_SpecialAirS_Loop_IASA(Fighter_GObj* gobj);
void ftTs_SpecialAirS_Loop_Phys(Fighter_GObj* gobj);
void ftTs_SpecialAirS_Loop_Coll(Fighter_GObj* gobj);

/* ftTs_specialhi.c */
void ftTs_SpecialHi_EnterAirOrGround(Fighter_GObj* gobj);
bool ftTs_Fighter_IASACheck_UpSpecial(Fighter_GObj* gobj);
void ftTs_SpecialHi_Start_Anim(Fighter_GObj* gobj);
void ftTs_SpecialHi_Start_IASA(Fighter_GObj* gobj);
void ftTs_SpecialHi_Start_Phys(Fighter_GObj* gobj);
void ftTs_SpecialHi_Start_Coll(Fighter_GObj* gobj);
void ftTs_SpecialAirHi_Start_Anim(Fighter_GObj* gobj);
void ftTs_SpecialAirHi_Start_IASA(Fighter_GObj* gobj);
void ftTs_SpecialAirHi_Start_Phys(Fighter_GObj* gobj);
void ftTs_SpecialAirHi_Start_Coll(Fighter_GObj* gobj);
void ftTs_SpecialHi_Loop_Anim(Fighter_GObj* gobj);
void ftTs_SpecialHi_Loop_IASA(Fighter_GObj* gobj);
void ftTs_SpecialHi_Loop_Phys(Fighter_GObj* gobj);
void ftTs_SpecialHi_Loop_Coll(Fighter_GObj* gobj);
void ftTs_SpecialHi_Exhaust_Anim(Fighter_GObj* gobj);
void ftTs_SpecialHi_Exhaust_IASA(Fighter_GObj* gobj);
void ftTs_SpecialHi_Exhaust_Phys(Fighter_GObj* gobj);
void ftTs_SpecialHi_Exhaust_Coll(Fighter_GObj* gobj);
void ftTs_SpecialHi_Cancel_Anim(Fighter_GObj* gobj);
void ftTs_SpecialHi_Cancel_IASA(Fighter_GObj* gobj);
void ftTs_SpecialHi_Cancel_Phys(Fighter_GObj* gobj);
void ftTs_SpecialHi_Cancel_Coll(Fighter_GObj* gobj);

/* ftTs_speciallw.c */
void ftTs_SpecialLw_EnterAirOrGround(Fighter_GObj* gobj);
void ftTs_SpecialLwStart_Anim(Fighter_GObj* gobj);
void ftTs_SpecialLwStart_IASA(Fighter_GObj* gobj);
void ftTs_SpecialLwStart_Phys(Fighter_GObj* gobj);
void ftTs_SpecialLwStart_Coll(Fighter_GObj* gobj);
void ftTs_SpecialLwEnd_Anim(Fighter_GObj* gobj);
void ftTs_SpecialLwEnd_IASA(Fighter_GObj* gobj);
void ftTs_SpecialLwEnd_Phys(Fighter_GObj* gobj);
void ftTs_SpecialLwEnd_Coll(Fighter_GObj* gobj);
void ftTs_SpecialAirLwStart_Anim(Fighter_GObj* gobj);
void ftTs_SpecialAirLwStart_IASA(Fighter_GObj* gobj);
void ftTs_SpecialAirLwStart_Phys(Fighter_GObj* gobj);
void ftTs_SpecialAirLwStart_Coll(Fighter_GObj* gobj);
void ftTs_SpecialAirLwEnd_Anim(Fighter_GObj* gobj);
void ftTs_SpecialAirLwEnd_IASA(Fighter_GObj* gobj);
void ftTs_SpecialAirLwEnd_Phys(Fighter_GObj* gobj);
void ftTs_SpecialAirLwEnd_Coll(Fighter_GObj* gobj);
void ftTs_SpecialLwCharge_Anim(Fighter_GObj* gobj);
void ftTs_SpecialLwCharge_IASA(Fighter_GObj* gobj);
void ftTs_SpecialLwCharge_Phys(Fighter_GObj* gobj);
void ftTs_SpecialLwCharge_Coll(Fighter_GObj* gobj);
void ftTs_SpecialLwRun_Anim(Fighter_GObj* gobj);
void ftTs_SpecialLwRun_IASA(Fighter_GObj* gobj);
void ftTs_SpecialLwRun_Phys(Fighter_GObj* gobj);
void ftTs_SpecialLwRun_Coll(Fighter_GObj* gobj);
void ftTs_SpecialLwRunTurn_Anim(Fighter_GObj* gobj);
void ftTs_SpecialLwRunTurn_IASA(Fighter_GObj* gobj);
void ftTs_SpecialLwRunTurn_Phys(Fighter_GObj* gobj);
void ftTs_SpecialLwRunTurn_Coll(Fighter_GObj* gobj);
void ftTs_SpecialLwRunJump_Anim(Fighter_GObj* gobj);
void ftTs_SpecialLwRunJump_IASA(Fighter_GObj* gobj);
void ftTs_SpecialLwRunJump_Phys(Fighter_GObj* gobj);
void ftTs_SpecialLwRunJump_Coll(Fighter_GObj* gobj);
void ftTs_SpecialLwDive_Anim(Fighter_GObj* gobj);
void ftTs_SpecialLwDive_IASA(Fighter_GObj* gobj);
void ftTs_SpecialLwDive_Phys(Fighter_GObj* gobj);
void ftTs_SpecialLwDive_Coll(Fighter_GObj* gobj);
void ftTs_SpecialLwRunBrake_Anim(Fighter_GObj* gobj);
void ftTs_SpecialLwRunBrake_IASA(Fighter_GObj* gobj);
void ftTs_SpecialLwRunBrake_Phys(Fighter_GObj* gobj);
void ftTs_SpecialLwRunBrake_Coll(Fighter_GObj* gobj);
void ftTs_SpecialLwStopWall_Anim(Fighter_GObj* gobj);
void ftTs_SpecialLwStopWall_IASA(Fighter_GObj* gobj);
void ftTs_SpecialLwStopWall_Phys(Fighter_GObj* gobj);
void ftTs_SpecialLwStopWall_Coll(Fighter_GObj* gobj);

/* ftTs_visual.c: tail ball, spin trail, effects, mouth, result-screen voice */
void ftTs_ProcessTail(Fighter_GObj* gobj);
void ftTs_ProcessTrail(Fighter_GObj* gobj);
void ftTs_ProcessMouth(Fighter_GObj* gobj);
void ftTs_GXCallback(HSD_GObj* gobj, intptr_t pass);
void ftTs_CheckWinAudio(Fighter_GObj* gobj);
int ftTs_OnSpawnParticle(HSD_Particle* pp);
void ftTs_SpawnBallEffect(Fighter_GObj* gobj, int effect_id, int part);
void* ftTs_SpawnHelicopterEffect(Fighter_GObj* gobj, int effect_id, int part);
void ftTs_InitDashTailAnim(Fighter_GObj* gobj, float frame);
const ftTails_Colors* ftTs_GetColors(Fighter* fp);
extern const ftTails_Colors ftTs_MetalColor;

/* ftTs_cpu.c */
extern const ftTails_CpuData ftTs_CpuData;
void ftTs_MexCPU_InitCustomData(Fighter_GObj* gobj, const ftTails_CpuData* data);

/* itTs_shot.c: the SpecialN article, and the same article again for Kirby's copy of the move */
void ftTs_SpecialN_SpawnProjectile(Fighter_GObj* gobj);
void ftTs_SpecialN_SpawnProjectileWith(Fighter_GObj* gobj, int article, HSD_GObj** slot);
extern ItemStateTable itTs_Shot_StateTable[3];
extern ItemLogicTable itTs_Articles[2];

/* tails_kirby.c: the ability Kirby copies from Tails (the kbFunction of PlKbCpTs.dat) */
extern const MuAkKirbyCopy ftKbTs_Copy;

#endif
