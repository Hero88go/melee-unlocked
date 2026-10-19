#ifndef MU_SPROUTTOWER_H
#define MU_SPROUTTOWER_H
#include "../common/mu_gr_common.h"
#include "../common/mu_gr_mex.h"
#include <melee/gr/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/aobj.h>

/* The disc's yakumono block. Names include original byte positions so each
 * access can be checked against the stage block without a native offset. */
typedef struct SpParams {
    s32 x000; f32 x004, x008; s32 x00C;
    s32 x010, x014, x018, x01C, x020, x024;
    f32 x028; s32 x02C, x030, x034; f32 x038;
    s32 x03C, x040, x044, x048; f32 x04C, x050, x054;
    f32 x058, x05C, x060; s32 x064, x068;
    f32 x06C, x070, x074, x078, x07C, x080, x084, x088, x08C, x090;
    u16 x094, x096, x098, x09A, x09C, x09E, x0A0, x0A2, x0A4, x0A6;
    s32 x0A8, x0AC, x0B0, x0B4;
    f32 x0B8, x0BC, x0C0, x0C4, x0C8, x0CC, x0D0, x0D4, x0D8;
    s32 x0DC, x0E0, x0E4, x0E8; f32 x0EC, x0F0, x0F4, x0F8, x0FC;
    f32 x100, x104, x108; s32 x10C, x110, x114, x118;
    f32 x11C, x120, x124;
} DISC_STRUCT SpParams;

extern SpParams* sp_param;
extern float sp_time;
extern int sp_player_count;
extern bool sp_is_gc;

/* Helpers used by both the time/wildlife and shadow halves of the stage. */
bool sp_night(void);
float sp_step(float value, float target, float step);
void sp_alpha(HSD_JObj* joint, float alpha);
void sp_diffuse(HSD_JObj* joint, unsigned int diffuse);
void sp_shadow_install(void);
void sp_shadow_create(Ground_GObj* gobj);
void sp_shadow_frame(Ground_GObj* gobj);
bool sp_shadow_check(Vec3* pos, int arg, HSD_JObj* joint);

#define SP_VARS(T, g) ((T*) (void*) &((Ground*) HSD_GObjGetUserData(g))->u)
#define SP_FITS(T) _Static_assert(sizeof(T) <= sizeof(((Ground*) 0)->u), #T " fits Ground.u")
#endif
