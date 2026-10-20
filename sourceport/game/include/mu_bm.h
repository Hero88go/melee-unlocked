/* Borrowed moves: rogueMelee and Melee Maker edit the same retail fighter code with one-to-one engines
 * (Rogue_AbilityVars ~ Bam_DonorVars, ...). The decomp's shared edits (melee-native.patch, #if MU_ROGUE)
 * call neutral names: MU_BM_* asks the engine of the loaded mod disc, MU_BMR_* only rogueMelee's and
 * MU_BMM_* only Melee Maker's (lines one engine left retail). Each engine answers natively while idle,
 * and only one mod disc plays at a time. */
#ifndef MU_BM_H
#define MU_BM_H
#include <melee/rogue/platform/melee/specials/special_engine.h>
#if MU_MAKER
#include <engine/engine.h>
int mu_bm_maker(void);   /* Melee Maker's disc is the loaded mod (mu_native.c) */
#define MU_BM_PICK(m, r, ...) (mu_bm_maker() ? m(__VA_ARGS__) : r(__VA_ARGS__))
#define MU_BMM_Vars(...) Bam_DonorVars(__VA_ARGS__)
#define MU_BMM_BoneJoint(...) Bam_DonorBoneJoint(__VA_ARGS__)
#define MU_BMM_DonorKind(...) Bam_DonorKind(__VA_ARGS__)
#define MU_BMM_InBorrowedMove(...) Bam_InBorrowedMove(__VA_ARGS__)
#define MU_BMM_ClimberPartner(...) Bam_ClimberPartner(__VA_ARGS__)
#define MU_BMM_DonorData(...) Bam_DonorData(__VA_ARGS__)
#define MU_BMM_TrySpecial(...) Bam_TrySpecial(__VA_ARGS__)
#define MU_BMM_MotionState(...) Bam_DonorMotionState(__VA_ARGS__)
#define MU_BMM_BorrowEnd(...) Bam_BorrowEnd(__VA_ARGS__)
#define MU_BMM_FighterDestroyed(...) Bam_BorrowFighterDestroyed(__VA_ARGS__)
#define MU_BMM_BorrowedTransform(...) Bam_BorrowedTransform(__VA_ARGS__)
#define MU_BMM_Transformed(...) Bam_BorrowTransformed(__VA_ARGS__)
#define MU_BMM_AerialLandingLag(...) Bam_AerialLandingLag(__VA_ARGS__)
#define MU_BMM_PartIndex(...) Bam_PartJoint(__VA_ARGS__)
#else
#define mu_bm_maker() 0
#define MU_BM_PICK(m, r, ...) r(__VA_ARGS__)
#endif
#define MU_BMR_Vars(...) Rogue_AbilityVars(__VA_ARGS__)
#define MU_BMR_BoneJoint(...) Rogue_AbilityMapBone(__VA_ARGS__)
#define MU_BMR_DonorKind(...) Rogue_AbilitySourceKind(__VA_ARGS__)
#define MU_BMR_InBorrowedMove(...) Rogue_IsAbilityState(__VA_ARGS__)
#define MU_BMR_ClimberPartner(...) Rogue_AbilityClimberPartner(__VA_ARGS__)
#define MU_BMR_DonorData(...) Rogue_AbilityData(__VA_ARGS__)
#define MU_BMR_TrySpecial(...) Rogue_TrySpecial(__VA_ARGS__)
#define MU_BMR_MotionState(...) Rogue_AbilityMotionState(__VA_ARGS__)
#define MU_BMR_BorrowEnd(...) Rogue_AbilityCleanup(__VA_ARGS__)
#define MU_BMR_FighterDestroyed(...) Rogue_AbilityFighterDestroyed(__VA_ARGS__)
#define MU_BMR_BorrowedTransform(...) Rogue_BorrowedTransform(__VA_ARGS__)
#define MU_BMR_Transformed(...) Rogue_AbilityTransformed(__VA_ARGS__)
#define MU_BMR_AerialLandingLag(...) Rogue_AerialLandingLag(__VA_ARGS__)
#define MU_BMR_PartIndex(...) Rogue_AbilityPartIndex(__VA_ARGS__)
#define MU_BM_Vars(...) MU_BM_PICK(MU_BMM_Vars, MU_BMR_Vars, __VA_ARGS__)
#define MU_BM_BoneJoint(...) MU_BM_PICK(MU_BMM_BoneJoint, MU_BMR_BoneJoint, __VA_ARGS__)
#define MU_BM_DonorKind(...) MU_BM_PICK(MU_BMM_DonorKind, MU_BMR_DonorKind, __VA_ARGS__)
#define MU_BM_InBorrowedMove(...) MU_BM_PICK(MU_BMM_InBorrowedMove, MU_BMR_InBorrowedMove, __VA_ARGS__)
#define MU_BM_ClimberPartner(...) MU_BM_PICK(MU_BMM_ClimberPartner, MU_BMR_ClimberPartner, __VA_ARGS__)
#define MU_BM_DonorData(...) MU_BM_PICK(MU_BMM_DonorData, MU_BMR_DonorData, __VA_ARGS__)
#define MU_BM_MotionState(...) MU_BM_PICK(MU_BMM_MotionState, MU_BMR_MotionState, __VA_ARGS__)
#define MU_BM_BorrowEnd(...) MU_BM_PICK(MU_BMM_BorrowEnd, MU_BMR_BorrowEnd, __VA_ARGS__)
#define MU_BM_FighterDestroyed(...) MU_BM_PICK(MU_BMM_FighterDestroyed, MU_BMR_FighterDestroyed, __VA_ARGS__)
#define MU_BM_BorrowedTransform(...) MU_BM_PICK(MU_BMM_BorrowedTransform, MU_BMR_BorrowedTransform, __VA_ARGS__)
#define MU_BM_Transformed(...) MU_BM_PICK(MU_BMM_Transformed, MU_BMR_Transformed, __VA_ARGS__)
#define MU_BM_AerialLandingLag(...) MU_BM_PICK(MU_BMM_AerialLandingLag, MU_BMR_AerialLandingLag, __VA_ARGS__)
#define MU_BM_PartIndex(...) MU_BM_PICK(MU_BMM_PartIndex, MU_BMR_PartIndex, __VA_ARGS__)
#endif
