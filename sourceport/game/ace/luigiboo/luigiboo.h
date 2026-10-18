/* PlLb.dat fighter data, translated from verified16/listing.txt. */
#ifndef MU_ACE_LUIGIBOO_H
#define MU_ACE_LUIGIBOO_H

#include "luigiboo_math.h"
#include <melee/ft/types.h>
#include <sysdolphin/baselib/forward.h>

typedef struct ftLb_State {
    u32 unused;                /* allocated block +0, no readers */
    HSD_GObj* boo;              /* +4 */
    HSD_Archive* archive;       /* +8 */
} ftLb_State;

typedef struct ftLb_FighterVars {
    u8 retail[0x22F8 - 0x222C];
    ftLb_State* state;          /* fp+22F8 */
} ftLb_FighterVars;

typedef struct ftLb_MotionVars {
    float speed;               /* fp+2340, shared with retail Luigi misfire */
    u8 retail[0x14 - 4];
    s32 hitlag;                /* fp+2354 */
    float stick_x, stick_y;     /* fp+2358/+235C */
    float facing;              /* fp+2360 */
    ftLb_Path path;            /* fp+2364, 0x34 bytes */
    float path_time;           /* fp+2398 */
} ftLb_MotionVars;

static inline ftLb_State* ftLb_StateOf(Fighter* fp)
{
    return ((ftLb_FighterVars*) &fp->u)->state;
}
static inline ftLb_MotionVars* ftLb_MV(Fighter* fp)
{
    return (ftLb_MotionVars*) &fp->mv;
}

/* Fighter block helpers called by Boo's own code. */
void ftLb_BooDetach(HSD_GObj* gobj);                  /* +22A8 */
void ftLb_Misfire_OnActionStateChange(HSD_GObj* gobj); /* +2588 */
void ftLb_Misfire_OnTakeDamage(HSD_GObj* gobj);       /* +2630 */

#endif
