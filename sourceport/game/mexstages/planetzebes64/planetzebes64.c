/* Planet Zebes 64 (Akaneia 1.0.1 and ACE 2.0.0, the same file: internal 74, external 291), the
 * m-ex grFunction block of GrOPz.dat: 281 instructions with symbols, no stage items (disassembly:
 * run-source/rel09-stages/out_dis_GrOPz.txt, `py -3.12 grdis.py ak GrOPz.dat`).
 *
 * The shape the 64 stages share (Peach's Castle 64 GrOPc.dat, Saffron City 64 GrOSf.dat, Planet
 * Zebes 64, Mushroom Kingdom 64 GrOMk.dat; Hyrule Castle 64 exports everything and is not one of
 * them), read from all four blocks:
 *
 *   - Exports 1 (map table), 3 (on_init) and 5 (on_load, a lone blr) only. The other five slots
 *     keep the MxDt row's words, Dream Land 64's retail callbacks (descriptor fields left NULL).
 *   - on_init (17 instructions, the same in all four, 3 or 4 map gobjs): grTSeak_80223908(0 .. n-1)
 *     in order, then the camera range and blast zone (Ground_801C39C0, Ground_801C3BB4).
 *   - The map table names Sheik's target stage code (gr/grtseak.c) by raw retail address in most
 *     slots, words the block does not relocate: row 0 is grTSk_StageCallbacks[0] whole
 *     (stageGObj0_OnInit, 80223A1C, 80223A24, 80223A28, flags 0); row 1 is the block's own
 *     Background_Create with Sheik's row 1 rest (80223B0C, stageGObj1_GObjProc, 80223B34); the
 *     later rows take 80223A7C as callback1 and 80223AB8 as callback3, and row 2 has flags
 *     0xC0000000 (lights and fog of that map gobj) except on Peach's Castle 64 (0). Those Sheik
 *     functions are static in the decomp, so a native table cannot point at them: this file has
 *     one native copy of each body it uses (mexgr_pz_tseak_*), same code, same order of calls.
 *   - Background_Create (23 instructions, the same in all four): animation set 0, Ground x11 bits
 *     0 to 2 = 2 (the half word at +0x10 masked and or'ed with 0x40), material flags 0x28000000 on
 *     the map gobj's joint.
 *
 * Planet Zebes 64's own part is the lava (map gobj 3): it waits 600 to 899 frames, shakes the
 * camera 120 frames before it moves, then moves for 180 frames to a random height inside one of
 * three bands (never the band it is in), waits again for that band's time, and so on. Each frame
 * the joint follows the height and the stage's lava level (Ground_801C438C, the value the fighter
 * code and the camera read through Ground_801C4368) follows the joint's child. A fighter below that
 * level is hit by the block's own hit description, and one who enters it from above starts sound 0
 * of the stage's bank (m-ex 803D7078, mu_gr_mex_sfx). */
#include "../common/mu_gr_common.h"
#include "../common/mu_gr_mex.h"

#include <melee/cm/camera.h>
#include <melee/ft/ftdevice.h>
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/grmaterial.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/lb/forward.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>

/* The lava's state, in its map gobj's Ground union: console gp+0xE0 to gp+0xF4. The first 0x1C
 * bytes of the union are not used by the block; the padding keeps every field at its console
 * offset from the union. */
typedef struct MexgrPzLava {
    u8 x0_pad[0x1C];
    /* +1C gp+E0 */ float y;      /* the height the joint is moved to */
    /* +20 gp+E4 */ float target; /* the height it moves to */
    /* +24 gp+E8 */ float speed;  /* per frame while it moves */
    /* +28 gp+EC */ int state;    /* 0 waiting, 1 moving */
    /* +2C gp+F0 */ int timer;
    /* +30 gp+F4 */ int region;   /* the band of mexgr_pz_regions it is in, or moves to */
} MexgrPzLava;
_Static_assert(sizeof(MexgrPzLava) <= sizeof(((Ground*) 0)->u), "lava state fits the Ground union");

static MexgrPzLava* mexgr_pz_lava(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    return (MexgrPzLava*) &gp->u;
}

/* .rodata.lava_regions (+4BC): per band the weight of the random choice, the wait after reaching
 * it (frames, [min, max)) and the band itself (y, [min, max), truncated to whole units). */
static const struct {
    /* +0 */ int chance;
    /* +4 */ int wait_min;
    /* +8 */ int wait_max;
    /* +C */ float y_min;
    /* +10 */ float y_max;
} mexgr_pz_regions[3] = {
    { 40, 600, 900, -200.0F, -170.0F },
    { 40, 600, 900, -80.0F, -20.0F },
    { 20, 120, 420, 15.0F, 80.0F },
};

/* .data.lava_hazard (+4F8): the hit the fighter code applies to a fighter in the lava (the same
 * layout as the description retail Brinstar's acid gives, read through lbColl_80008D30). */
static lbColl_80008D30_arg1 mexgr_pz_lava_hit = {
    HitCapsule_Enabled, 14, 90, 75, 0, 110, 1, 1, 8,
};

/* Sheik's target stage callbacks the map table names by retail address (gr/grtseak.c, static
 * there): stageGObj0_OnInit 802239F0, stageGObj1_OnInit 80223ABC, stageGObj1_GObjProc 80223B14,
 * the "return false" callback1 words 80223A1C, 80223A7C, 80223B0C and the empty 80223A24,
 * 80223A28, 80223AB8, 80223B34. */
static void mexgr_pz_tseak_anim(Ground_GObj* gobj)       /* 802239F0 */
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

static void mexgr_pz_tseak_coll_anim(Ground_GObj* gobj)  /* 80223ABC */
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

static void mexgr_pz_tseak_coll(Ground_GObj* gobj)       /* 80223B14 */
{
    Ground_UpdateMapColl(gobj);
}

static bool mexgr_pz_tseak_false(Ground_GObj* gobj)      /* 80223A1C, 80223A7C, 80223B0C */
{
    (void) gobj;
    return false;
}

static void mexgr_pz_tseak_nop(Ground_GObj* gobj)        /* 80223A24, 80223A28, 80223AB8, 80223B34 */
{
    (void) gobj;
}

/* Background_Create (+98) */
static void mexgr_pz_background_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
    gp->x11_flags.b012 = 2;
    grMaterial_801C8858(GET_JOBJ(gobj), 0x28000000);
}

/* Lava_GetNextPos (+354): the next band, drawn over the other two by weight (the block has the
 * loop unrolled over the three constant weights: totals 60, 60, 80), then a whole height inside
 * it and the speed that reaches it in 180 frames. The band is stored before the second draw. */
static void mexgr_pz_lava_next(Ground_GObj* gobj)
{
    MexgrPzLava* lv = mexgr_pz_lava(gobj);
    const int cur = lv->region;
    int total = 0;
    int sum = 0;
    int next = 0;
    int draw;
    int lo;
    int hi;
    int i;

    for (i = 0; i < 3; i++) {
        if (i != cur) {
            total += mexgr_pz_regions[i].chance;
        }
    }
    draw = HSD_Randi(total);
    /* draw < total, so a band is always found (the block leaves its register unset otherwise) */
    for (i = 0; i < 3; i++) {
        if (i == cur) {
            continue;
        }
        sum += mexgr_pz_regions[i].chance;
        if (sum > draw) {
            next = i;
            break;
        }
    }
    lv->region = next;
    lo = (int) mexgr_pz_regions[next].y_min;
    hi = (int) mexgr_pz_regions[next].y_max;
    lv->target = (float) (HSD_Randi(hi - lo) + lo);
    lv->speed = (lv->target - lv->y) / 180.0F;
}

/* Lava_Check (+2C4), the fighter device callback ftCo_800C07F8 registers (type 3). Below the lava
 * level the fighter gets mexgr_pz_lava_hit; one whose previous height plus the level's change was
 * above the level just entered it: sound 0 of the stage's bank. */
static bool mexgr_pz_lava_check(Ground_GObj* gobj, Fighter_GObj* fobj, Vec3* arg)
{
    Fighter* fp = HSD_GObjGetUserData(fobj);
    float level;
    float change;
    (void) gobj;

    Ground_801C4368(&level, &change);
    if (fp->cur_pos.y < level) {
        *(void**) arg = &mexgr_pz_lava_hit;
        if (level < fp->prev_pos.y + change) {
            mu_gr_mex_sfx(0);
        }
        return true;
    }
    return false;
}

/* Lava_Create (+F4) */
static void mexgr_pz_lava_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    MexgrPzLava* lv = (MexgrPzLava*) &gp->u;

    grAnime_801C8138(gobj, gp->map_id, 0);
    ftCo_800C07F8(gobj, 3, mexgr_pz_lava_check);
    /* the joint's y is written without marking its matrix; Lava_Update marks it every frame */
    lv->y = -180.0F;
    GET_JOBJ(gobj)->translate.y = -180.0F;
    lv->target = 0.0F;
    lv->state = 0;
    lv->region = 0;
    lv->timer = HSD_Randi(300) + 600;
    mexgr_pz_lava_next(gobj);
}

/* Lava_Update (+190) */
static void mexgr_pz_lava_frame(Ground_GObj* gobj)
{
    MexgrPzLava* lv = mexgr_pz_lava(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    Vec3 pos;

    lb_800115F4();
    if (lv->state == 0) {
        lv->timer--;
        if (lv->timer <= 0) {
            lv->timer = 180;
            lv->state = 1;
        } else if (lv->timer == 120) {
            Camera_RequestQuake(QuakeKind_Loop, NULL);
        }
    } else if (lv->state == 1) {
        lv->timer--;
        if (lv->timer > 0) {
            lv->y = lv->y + lv->speed;
        } else {
            const int wait_min = mexgr_pz_regions[lv->region].wait_min;
            lv->timer = HSD_Randi(mexgr_pz_regions[lv->region].wait_max - wait_min) + wait_min;
            lv->state = 0;
            mexgr_pz_lava_next(gobj);
        }
    }
    jobj->translate.y = lv->y;
    HSD_JObjSetMtxDirtySub(jobj);
    lb_8000B1CC(jobj->child, NULL, &pos);
    Ground_801C438C(pos.y);
}

/* map_gobjs (+0): OnCreation, OnUnk, OnFrame, OnDeletion, flags */
static StageCallbacks mexgr_pz_map[4] = {
    { mexgr_pz_tseak_anim, mexgr_pz_tseak_false, mexgr_pz_tseak_nop, mexgr_pz_tseak_nop, 0 },
    { mexgr_pz_background_create, mexgr_pz_tseak_false, mexgr_pz_tseak_coll, mexgr_pz_tseak_nop, 0 },
    { mexgr_pz_tseak_coll_anim, mexgr_pz_tseak_false, mexgr_pz_tseak_coll, mexgr_pz_tseak_nop, 0xC0000000 },
    { mexgr_pz_lava_create, mexgr_pz_tseak_false, mexgr_pz_lava_frame, mexgr_pz_tseak_nop, 0 },
};

/* onInit (+50) */
static void mexgr_pz_init(void)
{
    mu_grTSeak_80223908(0);
    mu_grTSeak_80223908(1);
    mu_grTSeak_80223908(2);
    mu_grTSeak_80223908(3);
    Ground_801C39C0();
    Ground_801C3BB4();
}

/* on_demo_init, on_start, callback4, on_touch_line, on_check_shadow_render: NULL, so the MxDt row's
 * Dream Land 64 callbacks stay. */
const MuGrStage mu_gr_planet_zebes_64 = {
    .name = "Planet Zebes 64",
    .file = "/GrOPz.dat",
    .map_gobjs = mexgr_pz_map,
    .on_init = mexgr_pz_init,
    .on_load = mu_gr_event_nop,
    .flags = MU_GR_READY,
};
