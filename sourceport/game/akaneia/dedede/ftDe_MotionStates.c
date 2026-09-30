/* King Dedede (Akaneia), native: move_logic, the fighter's own action states 341 to 395.
 *
 * Copied from PlDe.dat ftFunction "move_logic" (55 entries of 0x20 bytes). Animation ids and flag
 * words are the disc values; the third word is FtMoveId << 24 plus the x9 flag bits
 * (0x00400000 = x9_b1). The four aerial jumps use the common JumpAerial callbacks, like Kirby. */
#include "ftDe.h"

#include <melee/ft/ftcamera.h>
#include <melee/ft/kinds/ftCommon/ftCo_Attack100.h> /* ftCo_JumpAerialF1_* */

#define FTDE_STATE_PROTOS(name)                                                                   \
    void ftDe_##name##_Anim(HSD_GObj* gobj);                                                     \
    void ftDe_##name##_IASA(HSD_GObj* gobj);                                                     \
    void ftDe_##name##_Phys(HSD_GObj* gobj);                                                     \
    void ftDe_##name##_Coll(HSD_GObj* gobj);

FTDE_STATE_PROTOS(SpecialNStart)
FTDE_STATE_PROTOS(SpecialNLoop)
FTDE_STATE_PROTOS(SpecialNEnd)
FTDE_STATE_PROTOS(SpecialNGrab)
FTDE_STATE_PROTOS(SpecialNGrabItem)
FTDE_STATE_PROTOS(SpecialNEat)
FTDE_STATE_PROTOS(SpecialNEatWait)
FTDE_STATE_PROTOS(SpecialNEatTurn)
FTDE_STATE_PROTOS(SpecialNSpit)
FTDE_STATE_PROTOS(SpecialNSpitItem)
FTDE_STATE_PROTOS(SpecialNEatWalk)
FTDE_STATE_PROTOS(SpecialNEatJump1)
FTDE_STATE_PROTOS(SpecialNEatJump2)
FTDE_STATE_PROTOS(SpecialNEatLanding)
FTDE_STATE_PROTOS(SpecialAirNStart)
FTDE_STATE_PROTOS(SpecialAirNLoop)
FTDE_STATE_PROTOS(SpecialAirNEnd)
FTDE_STATE_PROTOS(SpecialAirNGrab)
FTDE_STATE_PROTOS(SpecialAirNGrabItem)
FTDE_STATE_PROTOS(SpecialAirNEat)
FTDE_STATE_PROTOS(SpecialAirNEatWait)
FTDE_STATE_PROTOS(SpecialAirNEatTurn)
FTDE_STATE_PROTOS(SpecialAirNSpit)
FTDE_STATE_PROTOS(SpecialAirNSpitItem)
FTDE_STATE_PROTOS(SpecialS)
FTDE_STATE_PROTOS(SpecialAirS)
FTDE_STATE_PROTOS(SpecialHiStart)
FTDE_STATE_PROTOS(SpecialHiJump)
FTDE_STATE_PROTOS(SpecialHiLoop)
FTDE_STATE_PROTOS(SpecialHiTurn)
FTDE_STATE_PROTOS(SpecialHiLanding)
FTDE_STATE_PROTOS(SpecialHiHit)
FTDE_STATE_PROTOS(SpecialLwStart)
FTDE_STATE_PROTOS(SpecialLw)
FTDE_STATE_PROTOS(SpecialLwHold)
FTDE_STATE_PROTOS(SpecialLwTurn)
FTDE_STATE_PROTOS(SpecialLwWalk)
FTDE_STATE_PROTOS(SpecialLwJumpSquat)
FTDE_STATE_PROTOS(SpecialLwJump)
FTDE_STATE_PROTOS(SpecialLwFall)
FTDE_STATE_PROTOS(SpecialLwLanding)
FTDE_STATE_PROTOS(SpecialAirLwStart)
FTDE_STATE_PROTOS(SpecialAirLw)

/* The common aerial jump callbacks, as in the Kirby table. */
#define FTDE_JUMP(anim)                                                                           \
    {                                                                                             \
        anim, 0x6A, FtMoveId_Default << 24, ftCo_JumpAerialF1_Anim, ftCo_JumpAerialF1_IASA,     \
            ftCo_JumpAerialF1_Phys, ftCo_JumpAerialF1_Coll, ftCamera_UpdateCameraBox,           \
    }

#define FTDE_STATE(anim, flags, word, name)                                                       \
    {                                                                                             \
        anim, flags, word, ftDe_##name##_Anim, ftDe_##name##_IASA, ftDe_##name##_Phys,          \
            ftDe_##name##_Coll, ftCamera_UpdateCameraBox,                                        \
    }

#define N (FtMoveId_SpecialN << 24)
#define S (FtMoveId_SpecialS << 24)
#define HI (FtMoveId_SpecialHi << 24)
#define LW (FtMoveId_SpecialLw << 24)
#define X9_B1 0x00400000

MotionState ftDe_MotionStateTable[FTDE_MS_COUNT] = {
    /* 341 */ FTDE_JUMP(295),
    /* 342 */ FTDE_JUMP(296),
    /* 343 */ FTDE_JUMP(297),
    /* 344 */ FTDE_JUMP(298),
    /* 345 */ FTDE_STATE(299, 0x00340111, N, SpecialNStart),
    /* 346 */ FTDE_STATE(300, 0x003C0011, N, SpecialNLoop),
    /* 347 */ FTDE_STATE(301, 0x00340011, N, SpecialNEnd),
    /* 348 */ FTDE_STATE(302, 0x00340011, N, SpecialNGrab),
    /* 349 */ FTDE_STATE(302, 0x00340011, N, SpecialNGrabItem),
    /* 350 */ FTDE_STATE(303, 0x00340011, N, SpecialNEat),
    /* 351 */ FTDE_STATE(304, 0x00340011, N | X9_B1, SpecialNEatWait),
    /* 352 */ FTDE_STATE(311, 0x00340011, N, SpecialNEatTurn),
    /* 353 */ FTDE_STATE(312, 0x00340011, N, SpecialNSpit),
    /* 354 */ FTDE_STATE(312, 0x00340011, N, SpecialNSpitItem),
    /* 355 */ FTDE_STATE(305, 0x00344011, N, SpecialNEatWalk),
    /* 356 */ FTDE_STATE(306, 0x00344011, N, SpecialNEatWalk),
    /* 357 */ FTDE_STATE(307, 0x00344011, N, SpecialNEatWalk),
    /* 358 */ FTDE_STATE(308, 0x00348011, N, SpecialNEatJump1),
    /* 359 */ FTDE_STATE(309, 0x00340011, N, SpecialNEatJump2),
    /* 360 */ FTDE_STATE(310, 0x00340011, N | X9_B1, SpecialNEatLanding),
    /* 361 */ FTDE_STATE(299, 0x00340411, N, SpecialAirNStart),
    /* 362 */ FTDE_STATE(300, 0x00340411, N, SpecialAirNLoop),
    /* 363 */ FTDE_STATE(301, 0x00340411, N, SpecialAirNEnd),
    /* 364 */ FTDE_STATE(302, 0x00340411, N, SpecialAirNGrab),
    /* 365 */ FTDE_STATE(302, 0x00340411, N, SpecialAirNGrabItem),
    /* 366 */ FTDE_STATE(303, 0x00340411, N, SpecialAirNEat),
    /* 367 */ FTDE_STATE(304, 0x00340411, N | X9_B1, SpecialAirNEatWait),
    /* 368 */ FTDE_STATE(311, 0x00340411, N, SpecialAirNEatTurn),
    /* 369 */ FTDE_STATE(312, 0x00340411, N, SpecialAirNSpit),
    /* 370 */ FTDE_STATE(312, 0x00340411, N, SpecialAirNSpitItem),
    /* 371 */ FTDE_STATE(316, 0x00340012, S, SpecialS),
    /* 372 */ FTDE_STATE(317, 0x00340412, S, SpecialAirS),
    /* 373 */ FTDE_STATE(318, 0x00340013, HI, SpecialHiStart),
    /* 374 */ FTDE_STATE(319, 0x00340013, HI, SpecialHiStart),
    /* 375 */ FTDE_STATE(320, 0x00340013, HI, SpecialHiJump),
    /* 376 */ FTDE_STATE(321, 0x00340413, HI, SpecialHiLoop),
    /* 377 */ FTDE_STATE(322, 0x00340413, HI, SpecialHiTurn),
    /* 378 */ FTDE_STATE(323, 0x00340413, HI, SpecialHiTurn),
    /* 379 */ FTDE_STATE(324, 0x00340013, HI, SpecialHiLanding),
    /* 380 */ FTDE_STATE(325, 0x00340013, HI, SpecialHiLanding),
    /* 381 */ FTDE_STATE(326, 0x00340413, HI, SpecialHiHit),
    /* 382 */ FTDE_STATE(328, 0x00340014, LW, SpecialLwStart),
    /* 383 */ FTDE_STATE(329, 0x00340014, LW, SpecialLw),
    /* 384 */ FTDE_STATE(330, 0x00340014, LW, SpecialLw),
    /* 385 */ FTDE_STATE(331, 0x00340014, LW, SpecialLwHold),
    /* 386 */ FTDE_STATE(332, 0x00340014, LW, SpecialLwHold),
    /* 387 */ FTDE_STATE(333, 0x00340014, LW, SpecialLwTurn),
    /* 388 */ FTDE_STATE(334, 0x00340014, LW, SpecialLwWalk),
    /* 389 */ FTDE_STATE(335, 0x00340014, LW, SpecialLwJumpSquat),
    /* 390 */ FTDE_STATE(336, 0x00340414, LW, SpecialLwJump),
    /* 391 */ FTDE_STATE(337, 0x00340414, LW, SpecialLwFall),
    /* 392 */ FTDE_STATE(338, 0x00340014, LW, SpecialLwLanding),
    /* 393 */ FTDE_STATE(339, 0x00340414, LW, SpecialAirLwStart),
    /* 394 */ FTDE_STATE(340, 0x00340414, LW, SpecialAirLw),
    /* 395 */ FTDE_STATE(341, 0x00340414, LW, SpecialAirLw),
};

_Static_assert(sizeof(ftDe_MotionStateTable) / sizeof(ftDe_MotionStateTable[0]) == 55, "move_logic has 55 states");
