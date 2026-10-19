/* Sector Z (Akaneia 1.0.1 and ACE 2.0.0, the same file: internal 95, external 312): what the stage
 * code (sectorz.c) and the laser item (sectorz_laser.c, m-ex item kind 252) share. */
#ifndef MEXGR_SECTORZ_H
#define MEXGR_SECTORZ_H

#include "../common/mu_gr_common.h"

#include <melee/it/forward.h>
#include <melee/it/kinds/types.h>

/* The laser's item variables (ip->xDD4_itemVar), console ip+DD4 to ip+DEC. */
typedef struct MexgrSzLaserVars {
    /* +0 ip+DD4 */ s16 started;  /* set on the first animation frame */
    /* +2 ip+DD6 */ s16 no_coll;  /* the second shot of a pair (ShipSpawnBullet's argument) */
    /* +4 ip+DD8 */ Vec3 cur;     /* the tip, joint 1 of the model, this frame */
    /* +10 ip+DE4 */ Vec3 prev;   /* the tip the frame before */
} MexgrSzLaserVars;

/* itFunction 0 of GrOSz.dat (the laser, item kind 252 on both discs). */
extern ItemLogicTable mexgr_sz_items[1];

#endif
