/* Retail hook entry points (see project.toml [[hook]]).
 *
 * Called from: retail, through the hooks in project.toml.
 * State: none.
 */
#include <bam/bam.h>
#include <bam/retail.h>
#include <engine/internal.h>
#include <dolphin/os.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include "training.h"

void Bam_OnlineSceneEnter(void);
void Bam_OnlineSceneReady(void);
void Bam_OnlineSceneExit(void);
void Bam_ReplayBuildsSceneExit(void);

/* inject Fighter_800679B0+0x10: r4 = size HSD_ObjAllocInit will use for
 * Fighter structs (retail 0x23EC, Slippi's ExtendPlayerBlock makes 0x2600).
 * Returns the size including our extension slot. */
unsigned BAM_FighterAllocSize(unsigned size)
{
    return Bam_FighterAllocSize(size);
}

/* Natively, right after Fighter_Create allocates the struct: the block is
 * not cleared, and the game's own code reaches the engine (parts lookups)
 * before the fighter-created hook, so the extension slot starts empty. */
void BAM_OnFighterAllocated(Fighter* fp)
{
    Bam_FighterExtClear(fp);
}

/* inject Fighter_Create+0x4F0 (after ftLib_800867E8(gobj); r31 = gobj). */
void BAM_OnFighterCreated(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    BAM_LOG("fighter_create kind=%d player=%d sub=%d build=%d\n", (int) fp->kind,
            (int) fp->player_idx, (int) fp->is_sub_fighter, (int) Bam_IsBuildFighter(fp));
    BamFighter_Created(fp);
}

/* inject gm_801A4014 after gm_801A4B88(info): a new scene's heaps exist. */
void BAM_OnSceneEnter(void)
{
    Bam_OnSceneEnter(NULL);
    Bam_OnlineSceneEnter();
}

/* inject gm_801A4014 before the scene's state on_exit. */
void BAM_OnSceneExit(void)
{
    Bam_OnSceneExit();
    Bam_OnlineSceneExit();
    Bam_ReplayBuildsSceneExit();
    Bam_TrainingSceneExit();
}

/* inject gm_801A4014 after scene->on_enter(): the scene created its stage
 * and fighters; the first frame has not run. */
void BAM_OnSceneReady(void)
{
    Bam_OnlineSceneReady();
    if (bam_match) {
        Bam_DonorModelsLoad();
        Bam_LogHeapRoom("match ready");
        Bam_LogHeapTable();
    }
}

/* inject gm_801A4D34 after on_frame(): once per game frame (also during
 * Slippi rollback re-simulation, which is what we want). */
void BAM_OnFrame(void)
{
    unsigned i;
    Bam_TrainingFrame();
    if (!bam_match || Bam_TrainingFrozen()) return;
    for (i = 0; i < BAM_FIGHTERS; ++i)
        if (bam_match->fighters[i].fighter) {
            Bam_BorrowFighterFrame(bam_match->fighters[i].fighter);
            Bam_ParasolTrack(bam_match->fighters[i].fighter);
        }
}

/* ftCo_LandingAir_EnterWithLag, natively: after the donor's landing lag was
 * taken (MU_BM_AerialLandingLag in the game's source, before the L-cancel
 * halving). The training display's part of the original inject. */
void BAM_OnLandingAirLag(Fighter* fp, int msid)
{
    Bam_TrainingLanding(fp, msid);
}

/* inject at the entry of every native aerial-jump enter (multi-jump
 * ftCo_800D74A4 and the Ness/Yoshi/Peach/Mewtwo variants).
 *
 * Shine (and other borrowed moves) can be jump-cancelled. The borrower's
 * jump code then runs while the donor's attributes and motion table are
 * still installed: a multi-jumper (Jigglypuff, Kirby) asks for its own
 * kind-specific jump state 341+, which the borrowed move maps onto the
 * donor's state of the same number (Fox Blaster) and the fighter freezes.
 * End the borrowed move first so the jump is fully native. */
void BAM_BeforeNativeJump(Fighter_GObj* gobj)
{
    Bam_BorrowEnd(GET_FIGHTER(gobj));
}

/* Natively: ftAnim_ApplyPartAnim returns early during a borrowed move (the
 * game's source), and the quit combo is the Source Port's own (its pause
 * controls), so those overlay hooks are not here. */

/* lb_8000B1CC entry (world position of a joint). Borrowed moves have handed
 * it garbage joints (Dr. Mario's down air and Ganondorf's forward air on
 * Jigglypuff). A pointer outside the game's memory (MEM1 and what follows it,
 * up to 0x86000000 natively) is logged and replaced by NULL, which this
 * function treats as "no joint" instead of crashing. */
void* BAM_CheckJointPos(void* jobj)
{
    uintptr_t p = (uintptr_t) jobj;
    if (p == 0 || (p >= 0x80000000u && p < 0x86000000u && !(p & 3))) return jobj;
    BAM_NOTE("joint: bad joint %llx passed to lb_8000B1CC, ignored\n", (unsigned long long) p);
    return NULL;
}
