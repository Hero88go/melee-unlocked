#ifndef MU_ACE_LUIGIBOO_BOO_H
#define MU_ACE_LUIGIBOO_BOO_H

#include "luigiboo_math.h"
#include <melee/ft/types.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/forward.h>

/* Item+DD4 on the console. Native pointers widen; access these fields by
 * name and never by a console byte displacement. */
typedef struct itLb_BooVars {
    HSD_GObj* owner;       /* +DD4 */
    u8 unused[0x14];      /* +DD8..+DEB, no readers or writers */
    Vec3 previous_pos;    /* +DEC */
    float turn_velocity;  /* +DF8 */
    u32 flags;            /* +DFC: 1=available, 4=hidden/return flag */
    void* scratch;        /* +E00: 0x50-byte allocation */
    /* Native allocator adaptation. Item is pool-backed; its original remove
     * callback must release it rather than console HSD_Free(item). */
    void (*native_remove)(void* data);
} itLb_BooVars;

_Static_assert(sizeof(itLb_BooVars) <= sizeof(((Item*) 0)->xDD4_itemVar),
               "native Boo variables fit the item's own-variable union");

static inline itLb_BooVars* itLb_BooVarsOf(Item* ip)
{
    return (itLb_BooVars*) &ip->xDD4_itemVar;
}

/* Exported article 1 logic table, article 0 has no itFunction. */
extern const ItemLogicTable itLb_BooLogic;

/* Common helpers, article offsets. */
void itLb_Boo_3850(HSD_GObj* gobj, int joint_index);
void itLb_Boo_38C0(HSD_GObj* gobj);
HSD_JObj* itLb_Boo_3990(HSD_GObj* gobj);
void itLb_Boo_3A60(HSD_GObj* gobj);
void itLb_Boo_3B14(HSD_GObj* gobj);
void itLb_Boo_3BC8(HSD_GObj* gobj, int flags, float speed, float inner_radius,
                   float outer_radius);
float itLb_Boo_4158(HSD_JObj* joint);
void itLb_Boo_41E8(HSD_GObj* gobj, int state, int flags, int before, int after);
void itLb_Boo_429C(HSD_GObj* gobj, float limit, float speed, float acceleration);
void itLb_Boo_45B0(HSD_GObj* gobj);
void itLb_BooAlpha(HSD_JObj* joint, float alpha);
bool itLb_BooOwnerMotionBlocked(int motion);

/* State table callback starts. Anim/coll return bool; phys returns void. */
#define ITLB_STATE_DECL(anim, phys, coll) \
    bool itLb_Boo_##anim(HSD_GObj* gobj); \
    void itLb_Boo_##phys(HSD_GObj* gobj); \
    bool itLb_Boo_##coll(HSD_GObj* gobj)
ITLB_STATE_DECL(0258, 03D8, 045C);
ITLB_STATE_DECL(0464, 05C4, 0B78);
ITLB_STATE_DECL(0B80, 0E40, 0E78);
ITLB_STATE_DECL(0E80, 0F54, 108C);
ITLB_STATE_DECL(1094, 11BC, 11FC);
ITLB_STATE_DECL(1204, 12E8, 1328);
ITLB_STATE_DECL(1330, 14B0, 14F0);
ITLB_STATE_DECL(14F8, 15D4, 1A18);
ITLB_STATE_DECL(1B08, 1B10, 1B14);
ITLB_STATE_DECL(1B1C, 1C40, 1C68);
ITLB_STATE_DECL(1C70, 1D94, 1E10);
ITLB_STATE_DECL(1E18, 1F60, 2020);
ITLB_STATE_DECL(2028, 2184, 21EC);
ITLB_STATE_DECL(21F4, 21FC, 2204);
ITLB_STATE_DECL(220C, 2380, 2610);
ITLB_STATE_DECL(2618, 26D8, 295C);
ITLB_STATE_DECL(2964, 2A74, 2B78);
ITLB_STATE_DECL(2B80, 2C84, 2DD0);
ITLB_STATE_DECL(2DD8, 2DE0, 2DE8);
ITLB_STATE_DECL(2DF0, 2F28, 3050);
ITLB_STATE_DECL(3058, 3184, 3270);
ITLB_STATE_DECL(3278, 327C, 3368);
ITLB_STATE_DECL(3370, 3378, 3380);
ITLB_STATE_DECL(3388, 3390, 3398);
ITLB_STATE_DECL(33A0, 33A8, 33B0);
ITLB_STATE_DECL(33B8, 33C0, 33C8);
ITLB_STATE_DECL(33D0, 34BC, 34FC);
#undef ITLB_STATE_DECL

#endif
