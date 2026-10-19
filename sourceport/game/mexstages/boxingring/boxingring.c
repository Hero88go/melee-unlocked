/* Boxing Ring (Akaneia 1.0.1 and ACE 2.0.0, the same file: internal 91, external 308), the m-ex
 * grFunction block of GrBx.dat: 3,031 instructions with symbols (disassembly:
 * `py -3.12 grdis.py ak GrBx.dat`, run-source/rel09-stages/out_dis_GrBx.txt). Sound bank 75.
 * Offsets below (+XXXX) are block offsets of that listing.
 *
 * Every export is its own (1, 3 to 9). Twelve map gobjs: 0 Points, 1 Sky, 2 Background (the crowd;
 * Kirby, King Dedede and Meta Knight appear in it when a player plays them), 3 Lights, 4 Floor (the
 * ring), 5 Rope (two, spring a fighter that touches them into a jump), 6 Screen (the big screen),
 * 7 Cannon (two confetti cannons), 8 Video (what the screen shows: the field camera picture, the
 * "Ready" / "GO" banner, then the players' titles from GrBxTt.csv), 9 VideoKO (a KO flash in the
 * player's colour, with the crowd cheering and the confetti), 10 VideoNME (a King Dedede extra),
 * 11 RopeFG (in the table, never created by the block).
 *
 * The block keeps its state in each map gobj's Ground at +E0 and up (console offsets). Natively
 * those are the named fields of the structs below, overlaid on Ground.u (+C4 on the console; the
 * block leaves +C4 to +DF unused), the console offset next to each field. Ground_GetStageGObj clears
 * Ground in the mod view as m-ex does (memset 0x204 on the console), so fields the block never sets
 * start at zero as there (the rope cooldowns, Video's KO flags and its VideoKO gobj).
 *
 * The block's own globals: .bss.gr_param (yakumono param), .bss.titles and .bss.titles_length.
 *
 * Floats: the block never fuses a multiply-add (each product is an fmuls of its own, the sums
 * fadds), so every function with float arithmetic is MU_NO_CONTRACT. */
#include "../common/mu_gr_common.h"
#include "../common/mu_gr_mex.h"

#include <dolphin/dvd.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <melee/cm/camera.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/ftCo_Damage.h>
#include <melee/ft/kinds/ftCommon/ftCo_Jump.h>
#include <melee/ft/kinds/ftCommon/ftCo_Throw.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftCommon/types.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/grdisplay.h>
#include <melee/gr/grlib.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/gr/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/lbfile.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/lb/lbvector.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/axdriver.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tobj.h>

#include <math.h>
#include <string.h>

/* ---- yakumono_param (disc data, big-endian): the fields the block reads ---- */
typedef struct BxParam {
    /* +00 */ f32 under_scale;        /* OnInit: Ground_801C438C(under_scale * Ground_801C0498()) */
    /* +04 */ f32 crowd_rate;         /* the crowd's animation rate */
    /* +08 */ f32 cheer_rate;         /* its rate while it cheers */
    /* +0C */ s32 cheer_frames;
    /* +10 */ s32 burst_delay;        /* KO: frames to the confetti burst on the floor */
    /* +14 */ s32 ko_delay;           /* KO: frames to the VideoKO animation */
    /* +18 */ s32 cannon_delay;       /* KO: frames to the cannons */
    /* +1C */ s32 cannon_reload;      /* cannon: frames from its animation to its effects */
    /* +20 */ s32 rope_joint;         /* the rope joint whose position is the spring box */
    /* +24 */ f32 rope_half_width;
    /* +28 */ f32 rope_height;
    /* +2C */ s32 spring_frame;       /* the knee bend frame the spring jumps at */
    /* +30 */ f32 spring_velocity;    /* the jump's vertical speed */
    /* +34 */ s32 confetti_joint;     /* floor joint whose children get effect 30002 at a KO */
    /* +38 */ s32 burst_joint;        /* floor joint whose children get effect 30005 */
    /* +3C */ s32 cannon_parent[2];   /* floor joints the two cannons hang on */
    /* +44 */ s32 rope_parent[2];     /* floor joints the two ropes hang on */
    /* +4C */ GXColor ffa_color[4];   /* a player's colour (by its team bits) out of team battle */
    /* +5C */ GXColor cpu_color;      /* a CPU's colour out of team battle, and "no player" */
    /* +60 */ u8 x60[4];              /* not read */
    /* +64 */ GXColor team_color[4];  /* a team's colour in team battle */
    /* +74 */ u8 x74[8];              /* not read */
    /* +7C */ f32 panel_y[4];         /* text height on a screen panel, by team bits */
    /* +8C */ u8 brighten;            /* added to each channel of the title colour */
    /* +8D */ u8 x8D[3];
    /* +90 */ Vec2_BE title_scale[4]; /* title text font size, by team bits */
    /* +B0 */ GXColor names_color;    /* the fighter name lines */
    /* +B4 */ Vec2_BE names_scale[4]; /* their font size, by team bits */
    /* +D4 */ s32 screen_frames;      /* frames each screen state 3 to 5 stays */
    /* +D8 */ s32 nme_frames;         /* VideoNME's life, and added to the next draw's wait */
    /* +DC */ s32 nme_period;         /* frames between VideoNME draws */
    /* +E0 */ f32 nme_chance;         /* VideoNME appears when this is >= HSD_Randf() */
} DISC_STRUCT BxParam;
_Static_assert(__builtin_offsetof(BxParam, ffa_color) == 0x4C, "BxParam colours");
_Static_assert(__builtin_offsetof(BxParam, panel_y) == 0x7C, "BxParam panel");
_Static_assert(__builtin_offsetof(BxParam, names_color) == 0xB0, "BxParam names");
_Static_assert(__builtin_offsetof(BxParam, nme_chance) == 0xE0, "BxParam nme");

/* ---- per map gobj state, overlaid on Ground.u ---- */

/* Map gobj 2, Background. */
typedef struct BxBackgroundVars {
    /* +E0 */ int cheer;               /* frames left of the cheer rate */
} BxBackgroundVars;

/* Map gobj 4, Floor. */
typedef struct BxFloorVars {
    /* +E0 */ HSD_GObj* rope[2];       /* +E0, +E4 */
    /* +E8 */ HSD_GObj* cannon[2];     /* +E8, +EC */
    /* +F0 */ int burst;               /* frames to the confetti burst, -1 none */
    /* +F4 */ int cannons;             /* frames to the cannons, -1 none */
} BxFloorVars;

/* Map gobj 5, Rope. */
typedef struct BxRopeVars {
    /* +E0 */ f32 left, right, bottom, top;   /* the spring box, every frame */
    /* +F0 */ HSD_JObj* joint;
    /* +F4 */ u8 cooldown[6];          /* per player slot, frames before it can spring again */
} BxRopeVars;

/* Map gobj 7, Cannon. */
typedef struct BxCannonVars {
    /* +E0 */ int timer;
} BxCannonVars;

/* Video's two panels (+FC and +118, 0x1C bytes each on the console). */
typedef struct BxPanel {
    /* +00 */ HSD_JObj* emblem;        /* the series emblem (IfAll's Eblm_matanim_joint) */
    /* +04 */ HSD_JObj* frame;         /* coloured with the team colour */
    /* +08 */ HSD_JObj* title_joint;
    /* +0C */ HSD_Text* title;         /* the title line ("Mr. Nintendo", "Red Team") */
    /* +10 */ HSD_JObj* names_joint;
    /* +14 */ HSD_Text* names;         /* up to three fighter names */
    /* +18 */ f32 y;
} BxPanel;

/* Video's team map (+154, 8 bytes each on the console: a bit byte, three player slots, the title
 * pointer). Byte 0 bits 0x60 count the members, bits 0x18 hold the team (or the slot, out of team
 * battle), both 2 bits wide: `count` and `team` keep only those 2 bits. */
typedef struct BxTeam {
    u8 count;                          /* byte 0 bits 0x60 */
    u8 team;                           /* byte 0 bits 0x18 */
    u8 slots[4];                       /* +1 .. +3, 0xFF none; [3] is a native spare, see below */
    const char* title;                 /* +4 */
} BxTeam;

/* Map gobj 8, Video. */
typedef struct BxVideoVars {
    /* +E0 */ HSD_GObj* camera;        /* the screen's own camera (its picture goes to Screen) */
    /* +E4 */ HSD_GObj* field_camera;  /* copies the game's picture into a texture */
    /* +E8 */ HSD_JObj* picture;       /* joint 12: shows the field camera texture */
    /* +EC */ HSD_GObj* ko;            /* VideoKO, created at the first KO */
    /* +F0 */ HSD_Text* banner;        /* "Ready", "Sudden Death", "GO" */
    /* +F4 */ int canvas;              /* HSD_SisLib_803A611C's result */
    /* +F8 */ HSD_MatAnimJoint* emblem_anim;
    /* +FC */ BxPanel panel[2];        /* +FC, +118 */
    /* +134 */ int timer;              /* frames left in this screen state, -1 none */
    /* +138 */ int state;              /* 0 Ready, 1 Sudden Death, 2 GO, 3 to 5 titles */
    /* +13C */ const char* names[6];   /* each slot's MxDt fighter name */
    /* +154 */ BxTeam teams[6];
    /* +184 */ u8 queue_count;         /* +185 is set to 0 and never read */
    /* +186 */ u8 queue[6];            /* teams with members, shuffled for the panels */
    /* +18C */ u8 ko_shown[6];         /* per slot: its KO flash already played */
    /* +194 */ int nme_timer;          /* -1: no King Dedede plays */
} BxVideoVars;

/* Map gobj 9, VideoKO. */
typedef struct BxKoVars {
    /* +E0 */ HSD_MObj* border;
    /* +E4 */ HSD_MObj* banner;
    /* +E8 */ int delay;               /* frames to its animation, -1 none */
} BxKoVars;

/* Map gobj 10, VideoNME. */
typedef struct BxNmeVars {
    /* +E0 */ int timer;
} BxNmeVars;

/* Map gobj 11, RopeFG. */
typedef struct BxRopeFgVars {
    /* +E0 */ Vec3 corner[4];          /* +E0, +EC, +F8, +104: joints 3 to 6, world */
    /* +110 */ Vec3 view[4];           /* the same in view space, each frame it draws */
    /* +140 */ f32 alpha;
} BxRopeFgVars;

#define BX_VARS(type, gobj) ((type*) (void*) &((Ground*) HSD_GObjGetUserData(gobj))->u)
#define BX_FITS(type) _Static_assert(sizeof(type) <= sizeof(((Ground*) 0)->u), #type " fits Ground.u")
BX_FITS(BxBackgroundVars);
BX_FITS(BxFloorVars);
BX_FITS(BxRopeVars);
BX_FITS(BxCannonVars);
BX_FITS(BxVideoVars);
BX_FITS(BxKoVars);
BX_FITS(BxNmeVars);
BX_FITS(BxRopeFgVars);

/* The field camera's user data (FieldCamera_Create: HSD_MemAlloc(8), the image desc and a flag
 * byte whose bit 0x80 lets FieldCamera_Render copy). */
typedef struct BxFieldCamera {
    HSD_ImageDesc* image;
    u8 copy;
} BxFieldCamera;

/* SpringSquat keeps the rope joint at fp+2384 (motion vars +44), a word like the decomp's own
 * motion var pointers (MV_PTR / MV_SETPTR). */
typedef struct BxSpringMv {
    u8 x0[0x44];
    /* fp+2384 */ u32 rope;
} BxSpringMv;
_Static_assert(sizeof(BxSpringMv) <= sizeof(((Fighter*) 0)->mv), "BxSpringMv fits the motion vars");

/* .bss.gr_param (+11B8), .bss.titles (+2480), .bss.titles_length (+247C). */
static BxParam* bx_param;
static char* bx_titles;
static size_t bx_titles_length;

/* The audience members (.data.__compound_literal.3, +2484): a fighter's MxDt name and the joint of
 * the Background gobj that shows it. */
static const struct {
    const char* name;
    int joint;
} bx_audience[] = {
    { "Kirby", 3 },
    { "King Dedede", 4 },
    { "Meta Knight", 5 },
    { NULL, 0 },
};

static void bx_video_set_state(HSD_GObj* gobj, int state);

/* ---- small shared routines ---- */

/* OnUnk (callback1, never called): a lone blr returns its argument, never zero. */
static bool bx_unk(Ground_GObj* gobj)
{
    return gobj != NULL;
}

/* OnDeletion (callback3): "li r3, 0; blr". */
static void bx_deletion(Ground_GObj* gobj)
{
    (void) gobj;
}

static void bx_anim(HSD_GObj* gobj, int set)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    grAnime_801C8138(gobj, gp->map_id, set);
}

/* .text.MEX_PlayStageSoundRaw (+1450): sound `id` of the stage's own bank (MEX_GetData(16)->stage
 * ->sound[Stage_8022519C(Stage_80225194()) * 3] * 10000 + id), volume 0xFF, pan 0x80, then 0, 6.
 * Ids: 0 VideoNME, 1 a rope spring, 2 a cannon, 3 the confetti burst, 4 the KO flash. */
static void bx_sound(int id)
{
    int bank = mu_gr_mex_stage_bank(Stage_8022519C(Stage_80225194()));
    HSD_AudioSFXStartParam(bank * 10000 + id, 0xFF, 0x80, 0, 6);
}

/* Fighter_GetMEXName (+2098): MEX_GetData(12)[Player_GetPlayerCharacter(slot)]. The console
 * table always has a name there; natively the helper is NULL for a kind without an external id. */
static const char* bx_fighter_name(int slot)
{
    return mu_gr_mex_fighter_name(Player_GetPlayerCharacter(slot));
}

/* MEX_GetFighterName (+1BCC): false for an empty slot (slot type 3), else the name. */
static bool bx_slot_name(int slot, const char** name)
{
    if (Player_GetPlayerSlotType(slot) == Gm_PKind_NA) {
        return false;
    }
    *name = bx_fighter_name(slot);
    return true;
}

/* strcmp of the block. A NULL name (native only, see bx_fighter_name) never matches. */
static bool bx_same(const char* a, const char* b)
{
    return a != NULL && b != NULL && strcmp(a, b) == 0;
}

/* ClearStageScale (+1844): scale 1 on the gobj's joint, then HSD_JObjSetMtxDirtySub. */
static void bx_clear_scale(HSD_GObj* gobj)
{
    HSD_JObj* jobj = GET_JOBJ(gobj);
    jobj->scale.x = 1.0f;
    jobj->scale.y = 1.0f;
    jobj->scale.z = 1.0f;
    HSD_JObjSetMtxDirtySub(jobj);
}

/* The rate of every animation of a joint tree (HSD_ForeachAnim(jobj, 6, 0xFFFF, HSD_AObjSetRate,
 * ...)). The block passes "6, 1" then the float; as in the Fichina and Gamecube ports the rate
 * reaches HSD_AObjSetRate as its float argument (AOBJ_ARG_AF). */
static void bx_rate(HSD_JObj* jobj, f32 rate)
{
    HSD_ForeachAnim(jobj, JOBJ_TYPE, 0xFFFF, HSD_AObjSetRate, AOBJ_ARG_AF, rate);
}

/* LoadTitleCSV (+11BC): GrBxTt.csv read whole into the heap ("title,name,name..." per line, CR LF),
 * each CR and comma then turned into a NUL up to the first NUL. Native safety: the buffer (the
 * size rounded up to 32, as the block reads it) is zeroed first, so the scan stops at the file's
 * end when the read stops there; the console reads the rounded size from the disc, whose padding
 * after a file is zero on these discs. */
static void bx_load_titles(void)
{
    s32 entry = DVDConvertPathToEntrynum("GrBxTt.csv");
    size_t size;
    size_t alloc;
    char* p;
    if (entry == -1) {
        __assert("boxing_screen.c", 0x364, "title file not found!\n");
    }
    size = lbFileGetSize("GrBxTt.csv");
    if ((s32) size == -1) {
        return;
    }
    bx_titles_length = size;
    alloc = (size + 0x1F) & ~(size_t) 0x1F;
    bx_titles = HSD_MemAlloc(alloc);
    memset(bx_titles, 0, alloc);
    lbFile_800161C4(entry, 0, (uintptr_t) bx_titles, alloc, 0x21, 1);
    for (p = bx_titles; *p != '\0'; p++) {
        if (*p == '\r' || *p == ',') {
            *p = '\0';
        }
    }
}

/* ---- exports 3 to 9 ---- */

/* OnInit (+F0). The param pointer is stored before the check, as the block does. */
static MU_NO_CONTRACT void bx_init(void)
{
    f32 scale;
    bx_param = Ground_GetYakumonoParam();
    if (bx_param == NULL) {
        __assert("boxing.c", 0x384, "yakumono node was not found in dat");
    }
    bx_load_titles();
    mu_grTSeak_80223908(0);
    mu_grTSeak_80223908(1);
    mu_grTSeak_80223908(2);
    mu_grTSeak_80223908(4);
    mu_grTSeak_80223908(6);
    mu_grTSeak_80223908(3);
    Ground_801C39C0();
    Ground_801C3BB4();
    scale = bx_param->under_scale;
    Ground_801C438C(scale * Ground_801C0498());
}

/* ---- 0 Points, 1 Sky, 3 Lights ---- */

/* Points_OnCreation (+1BC). */
static void bx_points_create(Ground_GObj* gobj)
{
    bx_anim(gobj, 0);
}

/* Sky_OnCreation (+1F8): animation set 0; x11 bits 0 to 2 = 2, x10 bit 2 off (the halfword at +10:
 * rlwinm 0,1B,17, rlwinm 0,13,11, ori 0x40). */
static void bx_sky_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    bx_anim(gobj, 0);
    gp->x11_flags.b012 = 2;
    gp->x10_flags.b2 = 0;
}

/* Lights_OnCreation (+384): x10 bit 2 off, no animation. */
static void bx_lights_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    gp->x10_flags.b2 = 0;
}

/* ---- 2 Background ---- */

/* Background_CheckSpawnAudience (+12EC): each audience member hidden, then shown again when any
 * player slot (not empty) plays that fighter. */
static void bx_background_audience(HSD_GObj* gobj)
{
    int i;
    int slot;
    for (i = 0; bx_audience[i].name != NULL; i++) {
        HSD_JObj* jobj = Ground_801C3FA4(gobj, bx_audience[i].joint);
        HSD_JObjSetFlags(jobj, JOBJ_HIDDEN);
        for (slot = 0; slot < 6; slot++) {
            if (Player_GetPlayerSlotType(slot) == Gm_PKind_NA) {
                continue;
            }
            if (bx_same(bx_audience[i].name, bx_fighter_name(slot))) {
                HSD_JObjClearFlags(jobj, JOBJ_HIDDEN);
            }
        }
    }
}

/* Background_OnCreation (+250): animation set 0, the audience, the crowd at its rate, and effect
 * 30009 at the origin. */
static void bx_background_create(Ground_GObj* gobj)
{
    BxBackgroundVars* v = BX_VARS(BxBackgroundVars, gobj);
    Vec3 pos;
    bx_anim(gobj, 0);
    bx_background_audience(gobj);
    v->cheer = 0;
    bx_rate(GET_JOBJ(gobj), bx_param->crowd_rate);
    pos.x = 0.0f;
    pos.y = 0.0f;
    pos.z = -600.0f;
    grLib_801C96F8(30009, 30, &pos);
}

/* Background_OnFrame (+304): the cheer runs out to the crowd rate; the wind every frame. */
static void bx_background_frame(Ground_GObj* gobj)
{
    BxBackgroundVars* v = BX_VARS(BxBackgroundVars, gobj);
    if (v->cheer > 0 && --v->cheer == 0) {
        bx_rate(GET_JOBJ(gobj), bx_param->crowd_rate);
    }
    lb_800115F4();
}

/* Background_SetCheer (+2838): the crowd at the cheer rate for `frames`, when it exists. */
static void bx_background_cheer(int frames)
{
    HSD_GObj* gobj = Ground_GetMapGObj(2);
    if (gobj != NULL) {
        BX_VARS(BxBackgroundVars, gobj)->cheer = frames;
        bx_rate(GET_JOBJ(gobj), bx_param->cheer_rate);
    }
}

/* ---- 4 Floor, 7 Cannon ---- */

/* Floor_CreateAttachment (+13F4): map gobj `id`, its joint 0 bound to the parent's joint `joint`
 * (the parent joint is read first, as the block does). */
static HSD_GObj* bx_attach(HSD_GObj* parent, int id, int joint)
{
    HSD_JObj* anchor = Ground_801C3FA4(parent, joint);
    HSD_GObj* gobj = mu_grTSeak_80223908(id);
    lb_8000C2F8(Ground_801C3FA4(gobj, 0), anchor);
    return gobj;
}

/* Floor_OnCreation (+3A8): the two ropes and the two cannons on the ring. No collision call here:
 * OnInit's Ground_801C39C0 / Ground_801C3BB4 set up the stage's collision. */
static void bx_floor_create(Ground_GObj* gobj)
{
    BxFloorVars* v = BX_VARS(BxFloorVars, gobj);
    v->rope[0] = bx_attach(gobj, 5, bx_param->rope_parent[0]);
    v->rope[1] = bx_attach(gobj, 5, bx_param->rope_parent[1]);
    v->cannon[0] = bx_attach(gobj, 7, bx_param->cannon_parent[0]);
    v->cannon[1] = bx_attach(gobj, 7, bx_param->cannon_parent[1]);
    v->burst = -1;
    v->cannons = -1;
}

/* Cannon_Fire (+14D8): its animation, then the reload timer. */
static void bx_cannon_fire(HSD_GObj* gobj)
{
    bx_anim(gobj, 0);
    BX_VARS(BxCannonVars, gobj)->timer = bx_param->cannon_reload;
}

/* Floor_OnFrame (+454): the confetti burst (sound 3, effect 30005 on each child of the burst joint)
 * and the cannons, each when its timer reaches 0; a timer at 0 still counts down to -1. */
static void bx_floor_frame(Ground_GObj* gobj)
{
    BxFloorVars* v = BX_VARS(BxFloorVars, gobj);
    if (v->burst >= 0) {
        if (v->burst == 0) {
            HSD_JObj* child;
            bx_sound(3);
            for (child = Ground_801C3FA4(gobj, bx_param->burst_joint)->child; child != NULL;
                 child = child->next)
            {
                grLib_801C97DC(30005, 30, child);
            }
        }
        v->burst--;
    }
    if (v->cannons >= 0) {
        if (v->cannons == 0) {
            bx_cannon_fire(v->cannon[0]);
            bx_cannon_fire(v->cannon[1]);
        }
        v->cannons--;
    }
}

/* Floor_FireConfetti (+28AC): the burst and cannon timers, effect 30002 on each child of the
 * confetti joint now. Map gobj 4 is not checked, as on the console. */
static void bx_floor_confetti(void)
{
    HSD_GObj* gobj = Ground_GetMapGObj(4);
    BxFloorVars* v = BX_VARS(BxFloorVars, gobj);
    HSD_JObj* child;
    v->burst = bx_param->burst_delay;
    v->cannons = bx_param->cannon_delay;
    for (child = Ground_801C3FA4(gobj, bx_param->confetti_joint)->child; child != NULL;
         child = child->next)
    {
        grLib_801C97DC(30002, 30, child);
    }
}

/* Cannon_OnCreation (+65C). */
static void bx_cannon_create(Ground_GObj* gobj)
{
    BX_VARS(BxCannonVars, gobj)->timer = 0;
}

/* Cannon_OnFrame (+670): when the reload runs out, effects 30000 and 30001 on joint 3, 30008 on
 * joint 4, sound 2. */
static void bx_cannon_frame(Ground_GObj* gobj)
{
    BxCannonVars* v = BX_VARS(BxCannonVars, gobj);
    HSD_JObj* mouth;
    if (v->timer <= 0 || --v->timer != 0) {
        return;
    }
    mouth = Ground_801C3FA4(gobj, 3);
    grLib_801C97DC(30000, 30, mouth);
    grLib_801C97DC(30001, 30, mouth);
    grLib_801C97DC(30008, 30, Ground_801C3FA4(gobj, 4));
    bx_sound(2);
}

/* ---- 5 Rope: the spring ---- */

/* SpringSquat_Interrupt (+2BE8): a lone blr. */
static void bx_spring_input(HSD_GObj* gobj)
{
    (void) gobj;
}

/* SpringSquat_Anim (+2B3C): the fighter stays on the rope joint; at the param frame, or when the
 * animation ends, a jump with the param's vertical speed. */
static MU_NO_CONTRACT void bx_spring_anim(HSD_GObj* gobj)
{
    Fighter* fp = HSD_GObjGetUserData(gobj);
    f32 frame;
    lb_8000B1CC(MV_PTR(HSD_JObj*, ((BxSpringMv*) (void*) &fp->mv)->rope), NULL, &fp->cur_pos);
    frame = (f32) bx_param->spring_frame;
    if (fp->cur_anim_frame >= frame || !ftAnim_IsFramesRemaining(gobj)) {
        ftCo_Jump_Enter(gobj);
        fp->self_vel.y = bx_param->spring_velocity;
    }
}

/* Fighter_BounceTrajectory (+2BEC): a fighter in hitstun is sent up instead: its knockback
 * direction bent halfway to straight up at the same horizontal-plane speed (z keeps the normalized
 * value), then ftCo_8008E5A4. (+0.0f is the block's: it turns -0 into +0.) */
static MU_NO_CONTRACT void bx_bounce(Fighter* fp)
{
    Vec3 v = fp->x8c_kb_vel;
    f32 speed = sqrtf(v.x * v.x + v.y * v.y);
    lbVector_Normalize(&v);
    v.x = (v.x + 0.0f) * 0.5f * speed;
    v.y = (v.y + 1.0f) * 0.5f * speed;
    fp->x8c_kb_vel = v;
    ftCo_8008E5A4(fp);
}

/* Fighter_EnterSpringJump (+2494): false while the fighter is held (victim_gobj); a fighter in
 * hitstun bounces (true); otherwise it lets go of whoever it holds and enters the knee bend (24)
 * with the spring callbacks (input: none, animation: SpringSquat_Anim, collision: NULL). */
static bool bx_spring_enter(HSD_GObj* gobj, HSD_JObj* rope)
{
    Fighter* fp = HSD_GObjGetUserData(gobj);
    HSD_GObj* held;
    if (fp->victim_gobj != NULL) {
        return false;
    }
    if (fp->x221C_b6) {
        bx_bounce(fp);
        return true;
    }
    MV_SETPTR(((BxSpringMv*) (void*) &fp->mv)->rope, rope);
    held = fp->x1A5C;
    if (held != NULL) {
        ftCommon_8007DB58(held);
        ftCo_800DCFD4(held);
        ftCo_800DC920(gobj, held);
    }
    ftCommon_8007DB58(gobj);
    fp->self_vel.x = 0.0f;
    fp->self_vel.y = 0.0f;
    fp->self_vel.z = 0.0f;
    Fighter_ChangeMotionState(gobj, ftCo_MS_KneeBend, 0, 0.0f, 1.0f, 0.0f, NULL);
    fp->anim_cb = bx_spring_anim;
    fp->input_cb = bx_spring_input;
    fp->coll_cb = NULL;
    return true;
}

/* Rope_ProcessFighterDetectBox (+16AC, a priority 7 proc): the box under the rope joint, then each
 * slot: its cooldown counts down; a fighter inside the box (bounds included) with no cooldown that
 * springs gets sound 1, the rope's animation, its position put on the rope joint, and 10 frames
 * of cooldown. */
static MU_NO_CONTRACT void bx_rope_detect(HSD_GObj* gobj)
{
    BxRopeVars* v = BX_VARS(BxRopeVars, gobj);
    Vec3 pos;
    int slot;
    lb_8000B1CC(v->joint, NULL, &pos);
    v->left = pos.x - bx_param->rope_half_width;
    v->right = pos.x + bx_param->rope_half_width;
    v->bottom = pos.y - bx_param->rope_height;
    v->top = pos.y;
    for (slot = 0; slot < 6; slot++) {
        HSD_GObj* fighter;
        Fighter* fp;
        if (v->cooldown[slot] != 0) {
            v->cooldown[slot]--;
        }
        fighter = Player_GetEntity(slot);
        if (fighter == NULL) {
            continue;
        }
        fp = HSD_GObjGetUserData(fighter);
        if (!(fp->cur_pos.x >= v->left) || !(fp->cur_pos.x <= v->right) ||
            !(fp->cur_pos.y >= v->bottom) || !(fp->cur_pos.y <= v->top))
        {
            continue;
        }
        if (v->cooldown[slot] != 0) {
            continue;
        }
        if (!bx_spring_enter(fighter, v->joint)) {
            continue;
        }
        bx_sound(1);
        bx_anim(gobj, 0);
        fp->cur_pos = pos;
        v->cooldown[slot] = 10;
    }
}

/* Rope_GXCallback (+151C): the stage display, then in pass 3 a yellow outline of the spring box.
 * HSD_GObj_80390ED0 renders passes 0 to 2 only (mask 7), so the outline is never drawn in a match;
 * kept as the block has it. GXEnd (a no-op on the console) ends the strip for the native GX. */
static void bx_rope_draw(HSD_GObj* gobj, intptr_t pass)
{
    BxRopeVars* v;
    grDisplay_801C5DB0(gobj, pass);
    if (pass != 3) {
        return;
    }
    v = BX_VARS(BxRopeVars, gobj);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXLoadPosMtxImm(HSD_CObjGetViewingMtxPtrDirect(HSD_CObjGetCurrent()), GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXBegin(GX_LINESTRIP, GX_VTXFMT0, 5);
    GXPosition3f32(v->left, v->bottom, 0.0f);
    GXColor4u8(0xFF, 0xFF, 0x00, 0xFF);
    GXPosition3f32(v->right, v->bottom, 0.0f);
    GXColor4u8(0xFF, 0xFF, 0x00, 0xFF);
    GXPosition3f32(v->right, v->top, 0.0f);
    GXColor4u8(0xFF, 0xFF, 0x00, 0xFF);
    GXPosition3f32(v->left, v->top, 0.0f);
    GXColor4u8(0xFF, 0xFF, 0x00, 0xFF);
    GXPosition3f32(v->left, v->bottom, 0.0f);
    GXColor4u8(0xFF, 0xFF, 0x00, 0xFF);
    GXEnd();
    HSD_StateInvalidate(-1);
}

/* Rope_OnCreation (+550): its draw callback, the rope joint, the detect proc. The gobj's
 * render_cb is replaced directly, as the block writes gobj+1C. */
static void bx_rope_create(Ground_GObj* gobj)
{
    BxRopeVars* v = BX_VARS(BxRopeVars, gobj);
    gobj->render_cb = bx_rope_draw;
    v->joint = Ground_801C3FA4(gobj, bx_param->rope_joint);
    HSD_GObj_SetupProc(gobj, bx_rope_detect, 7);
}

/* ---- 6 Screen, 8 Video: the big screen ---- */

/* FieldCamera_Render (+2598): while copying is on, the game picture (640 x 320 from y 80) into the
 * texture, then GXPixModeSync and GXInvalidateTexAll. */
static void bx_field_camera_render(HSD_GObj* gobj, intptr_t pass)
{
    BxFieldCamera* fc = HSD_GObjGetUserData(gobj);
    (void) pass;
    if (fc->copy & 0x80) {
        HSD_ImageDesc* image = fc->image;
        GXSetTexCopySrc(0, 80, 640, 320);
        GXSetTexCopyDst(image->width, image->height, image->format, GX_FALSE);
        GXCopyTex(DP(image->image_ptr), GX_FALSE);
        GXPixModeSync();
        GXInvalidateTexAll();
    }
}

/* FieldCamera_Free (+2614). */
static void bx_field_camera_free(void* data)
{
    BxFieldCamera* fc = data;
    HSD_Free(DP(fc->image->image_ptr));
    HSD_ImageDescFree(fc->image);
    HSD_Free(fc);
}

/* FieldCamera_Create (+18C4): a camera class gobj (17, 18) whose render (priority 3) copies the
 * picture into an RGB565 image of width x height; copying starts off. */
static HSD_GObj* bx_field_camera_create(u16 width, u16 height)
{
    HSD_GObj* gobj = GObj_Create(17, 18, 0);
    BxFieldCamera* fc;
    HSD_ImageDesc* image;
    GObj_SetupGXLinkMax(gobj, bx_field_camera_render, 3);
    fc = HSD_MemAlloc(sizeof(*fc));
    fc->image = HSD_ImageDescAlloc();
    image = fc->image;
    DISC_SET(image->image_ptr, HSD_MemAlloc(GXGetTexBufferSize(width, height, GX_TF_RGB565, 0, 0)));
    image->format = GX_TF_RGB565;
    image->width = width;
    image->height = height;
    image->maxLOD = 0.0f;
    image->minLOD = 0.0f;
    image->mipmap = 0;
    fc->copy &= 0x7F;
    GObj_InitUserData(gobj, 6, bx_field_camera_free, fc);
    return gobj;
}

/* VideoScreen_OnRender (+19DC): once Screen has given it a picture (its user data, the Screen
 * texture's image desc), the screen camera draws gx link 1 (cleared to black, no fog, camera kind 0
 * for grDisplay) and copies the result into that image every frame, as Pokemon Stadium's screen
 * (grStadium fn_801D2ED0). */
static void bx_video_camera_render(HSD_GObj* gobj, intptr_t pass)
{
    HSD_CObj* cobj;
    (void) pass;
    if (gobj->user_data == NULL) {
        return;
    }
    cobj = GET_COBJ(gobj);
    if (HSD_CObjSetCurrent(cobj)) {
        HSD_SetEraseColor(0, 0, 0, 1);
        HSD_CObjEraseScreen(cobj, 1, 0, 0);
        HSD_FogSet(NULL);
        Camera_800310A0(0);
        HSD_GObj_80390ED0(gobj, 7);
        HSD_CObjEndCurrent();
    }
    lb_800122C8(gobj->user_data, 0, 0, 1);
}

/* Video_CreateText (+1A98): a text of three empty lines (y -16, 16, 48) on the canvas, centred,
 * box width `width`, font size (sx, sy), placed later by the joint (TextJoint_UpdatePosition). */
static void bx_text_create(HSD_JObj** joint_out, HSD_Text** text_out, int canvas, int width,
                           HSD_JObj* joint, f32 sx, f32 sy)
{
    HSD_Text* text = HSD_SisLib_803A6754(1, canvas);
    HSD_SisLib_803A6B98(text, 0.0f, -16.0f, "");
    HSD_SisLib_803A6B98(text, 0.0f, 16.0f, "");
    HSD_SisLib_803A6B98(text, 0.0f, 48.0f, "");
    text->default_fitting = 1;
    text->box_size_x = (f32) width;
    text->default_alignment = 1;
    text->default_kerning = 1;
    text->pos_x = 0.0f;
    text->pos_y = 0.0f;
    text->pos_z = 0.0f;
    text->x4C = 1;
    text->font_size.x = sx;
    text->font_size.y = sy;
    *text_out = text;
    *joint_out = joint;
}

/* TextJoint_UpdatePosition (+20E4): each text at its joint's world position plus (0, y, 0), y
 * then negated (text y grows downwards). */
static void bx_panel_place(BxPanel* panel)
{
    Vec3 offset;
    offset.x = 0.0f;
    offset.y = panel->y;
    offset.z = 0.0f;
    lb_8000B1CC(panel->title_joint, &offset, (Vec3*) &panel->title->pos_x);
    panel->title->pos_y = -panel->title->pos_y;
    lb_8000B1CC(panel->names_joint, &offset, (Vec3*) &panel->names->pos_x);
    panel->names->pos_y = -panel->names->pos_y;
}

/* Team_GetColor (+27B0): in team battle the team's colour; otherwise the CPU colour for a CPU's
 * entry, else the colour of its team bits (the slot's low 2 bits). */
static const GXColor* bx_team_color(const BxTeam* team)
{
    if (gm_8016B168()) {
        return &bx_param->team_color[team->team];
    }
    if (Player_GetPlayerSlotType(team->slots[0]) == Gm_PKind_Cpu) {
        return &bx_param->cpu_color;
    }
    return &bx_param->ffa_color[team->team];
}

/* TeamMap_FindTitle (+1C24): the CSV line whose names are exactly this entry's fighters (as many
 * names as members, each member's MxDt name among them) gives the title; else "<colour> Team" for a
 * team of two or more and "New Challenger" for one. Fields are the NUL-cut pieces of a line; a
 * line's last field is its LF, so a line has (fields - 1) names; at most 6 fields are taken, the
 * rest of a longer line is read as the next line. Written as the block's control flow, including
 * its quirk: an empty field (a NUL where a field starts) that is not the counted end goes back to
 * the same byte without moving, which never ends on the console either. GrBxTt.csv (the same file
 * on both discs: 33 lines "title,name" CR LF, no empty field) never reaches it. */
static int bx_find_title(BxTeam* team)
{
    char* p = bx_titles;
    char* end = bx_titles + bx_titles_length;
    char* fields[6];
    int count = team->count;
    int n;
    if (!(p < end)) {
        goto not_found;
    }
line:                                   /* +88 */
    n = 0;
field:                                  /* +22C */
    if (*p == '\0') {                   /* +1B8 */
        if (n - 1 == count) {
            goto match;
        }
        goto at_end;                    /* +80, no advance */
    }
    fields[n] = p;
scan:                                   /* +23C */
    if (*p == '\n') {                   /* +5C */
        if (n - 1 == count) {
            goto match;
        }
        goto next_line;
    }
    if (p[1] == '\0') {                 /* +21C: this field ends */
        p += 2;
        if (++n == 6) {
            goto next_line;
        }
        goto field;
    }
    p++;
    goto scan;
match:                                  /* +1CC */
    {
        int hits = 0;
        int k;
        for (k = 0; k < count; k++) {
            const char* name;
            int i;
            if (!bx_slot_name(team->slots[k], &name)) {
                break;
            }
            for (i = 1; i <= count; i++) {
                if (bx_same(fields[i], name)) {
                    hits++;
                    break;
                }
            }
        }
        if (hits == count) {
            team->title = fields[0];
            return 1;
        }
    }
next_line:                              /* +70: past the LF */
    if (*p == '\n') {
        p++;
    }
at_end:                                 /* +80 */
    if (p < end) {
        goto line;
    }
not_found:                              /* +110 */
    if (team->count <= 1) {
        team->title = "New Challenger";
        return 0;
    }
    switch (team->team) {
    case 0: team->title = "Red Team"; break;
    case 2: team->title = "Green Team"; break;
    case 3: team->title = "Yellow Team"; break;
    default: team->title = "Blue Team"; break;
    }
    return 1;
}

/* Video_SetData (+2F08): one panel for one entry: the title in its colour brightened by the
 * param (each channel capped at 255), the member names (an empty line for a missing one), the font
 * sizes and height of its team bits, the emblem of its first member's fighter (IfAll's emblem
 * material animation stopped at that frame), the emblem and frame in the plain colour. */
static void bx_panel_set(HSD_GObj* gobj, const BxTeam* team, BxPanel* panel)
{
    BxVideoVars* v = BX_VARS(BxVideoVars, gobj);
    const GXColor* color = bx_team_color(team);
    GXColor bright = *color;
    int add = bx_param->brighten;
    int k;
    bright.r = bright.r + add > 0xFF ? 0xFF : bright.r + add;
    bright.g = bright.g + add > 0xFF ? 0xFF : bright.g + add;
    bright.b = bright.b + add > 0xFF ? 0xFF : bright.b + add;
    HSD_SisLib_803A70A0(panel->title, 0, (char*) team->title);
    HSD_SisLib_803A74F0(panel->title, 0, &bright);
    panel->title->font_size.x = bx_param->title_scale[team->team].x;
    panel->title->font_size.y = bx_param->title_scale[team->team].y;
    panel->names->font_size.x = bx_param->names_scale[team->team].x;
    panel->names->font_size.y = bx_param->names_scale[team->team].y;
    for (k = 0; k < 3; k++) {
        int slot = team->slots[k];
        if (slot == 0xFF) {
            HSD_SisLib_803A70A0(panel->names, k, NULL);
        } else {
            HSD_SisLib_803A70A0(panel->names, k, (char*) v->names[slot]);
            HSD_SisLib_803A74F0(panel->names, k, &bx_param->names_color);
        }
    }
    HSD_JObjAddAnimAll(panel->emblem, NULL, v->emblem_anim, NULL);
    HSD_AObjSetRate(panel->emblem->u.dobj->mobj->tobj->aobj, 0.0f);
    panel->emblem->u.dobj->mobj->mat->diffuse = *color;
    HSD_JObjReqAnimByFlags(panel->emblem, 0x10,
        (f32) (u32) mu_gr_mex_fighter_emblem(Player_GetPlayerCharacter(team->slots[0])));
    HSD_JObjAnim(panel->emblem);
    panel->frame->u.dobj->mobj->mat->diffuse = *color;
    panel->y = bx_param->panel_y[team->team];
}

/* Video_ShuffleDisplayQueue (+2EAC): from the last place down to 1, a swap with HSD_Randi(place)
 * (not place + 1: an entry never stays where it is). */
static void bx_shuffle(u8* queue, int n)
{
    int i;
    if (n <= 1) {
        return;
    }
    for (i = n - 1; i != 0; i--) {
        int j = HSD_Randi(i);
        u8 a = queue[i];
        u8 b = queue[j];
        queue[i] = b;
        queue[j] = a;
    }
}

/* Video_RandomizeTitles (+26D0): one entry on both panels, or two shuffled ones. */
static void bx_video_titles(HSD_GObj* gobj)
{
    BxVideoVars* v = BX_VARS(BxVideoVars, gobj);
    int n = v->queue_count;
    if (n == 0) {
        return;
    }
    if (n <= 1) {
        BxTeam* team = &v->teams[v->queue[0]];
        bx_panel_set(gobj, team, &v->panel[0]);
        bx_panel_set(gobj, team, &v->panel[1]);
        return;
    }
    bx_shuffle(v->queue, n);
    bx_panel_set(gobj, &v->teams[v->queue[0]], &v->panel[0]);
    bx_panel_set(gobj, &v->teams[v->queue[1]], &v->panel[1]);
}

/* Video_SetScreenState (+1E80, its cases at +2D00 .. +2E6C: the jump table points into the
 * identical copy .text.Video_SetScreenState). 0 "Ready", 1 "Sudden Death", 2 "GO" (60 frames):
 * the banner shown, animation set 0. 3, 4, 5: the banner hidden, animation sets 1, 2, 3, new
 * titles at 3 and 5 (before / after the animation as the block orders it), param frames each. A
 * state past 5 is only stored. */
static void bx_video_set_state(HSD_GObj* gobj, int state)
{
    BxVideoVars* v = BX_VARS(BxVideoVars, gobj);
    switch (state) {
    case 0:
        v->banner->hidden = 0;
        HSD_SisLib_803A70A0(v->banner, 0, "Ready");
        bx_anim(gobj, 0);
        v->timer = -1;
        break;
    case 1:
        v->banner->hidden = 0;
        HSD_SisLib_803A70A0(v->banner, 0, "Sudden Death");
        bx_anim(gobj, 0);
        v->timer = -1;
        break;
    case 2:
        v->banner->hidden = 0;
        HSD_SisLib_803A70A0(v->banner, 0, "GO");
        bx_anim(gobj, 0);
        v->timer = 60;
        break;
    case 3:
        v->banner->hidden = 1;
        bx_video_titles(gobj);
        bx_anim(gobj, 1);
        v->timer = bx_param->screen_frames;
        break;
    case 4:
        v->banner->hidden = 1;
        bx_anim(gobj, 2);
        v->timer = bx_param->screen_frames;
        break;
    case 5:
        v->banner->hidden = 1;
        bx_anim(gobj, 3);
        bx_video_titles(gobj);
        v->timer = bx_param->screen_frames;
        break;
    default:
        break;
    }
    v->state = state;
}

/* Video_DisplayGO (+2074): Ground_801C10B8's callback at GO. */
static void bx_video_go(HSD_GObj* gobj)
{
    bx_video_set_state(gobj, 2);
}

/* Video_OnCreation (+724): the screen: a field camera texture on joint 12, a camera of its own
 * (the map gobj's camera desc) drawing gx link 1 with a text canvas, two panels and the banner;
 * the team map from the player slots (team battle: by Player_GetTeam, asserting on a team past 5;
 * otherwise one entry per slot), a title for each, the queue of entries with members; "Sudden
 * Death" in the sudden death scene (3), else "Ready", GO at the match start; the VideoNME draws
 * when a slot plays King Dedede. */
static void bx_video_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    BxVideoVars* v = BX_VARS(BxVideoVars, gobj);
    HSD_TObj* tobj;
    UnkStageDat* dat;
    HSD_GObj* camera;
    u8 kind;
    HSD_Text* text;
    int i;
    bx_clear_scale(gobj);
    gp->x8_callback = NULL;
    gp->xC0 = 0.0f;
    v->emblem_anim = HSD_ArchiveGetPublicAddress(lbDvd_8001819C("IfAll"), "Eblm_matanim_joint");
    HSD_GObjGXLink_8039084C(gobj);
    GObj_SetupGXLink(gobj, grDisplay_801C5DB0, 1, 0);
    v->picture = Ground_801C3FA4(gobj, 12);
    tobj = v->picture->u.dobj->mobj->tobj;
    v->field_camera = bx_field_camera_create(640, 320);
    tobj->imagedesc = ((BxFieldCamera*) v->field_camera->user_data)->image;
    dat = grDatFiles_801C6330(0)->unk4;
    camera = GObj_Create(17, 19, 0);
    kind = HSD_GObj_CameraKind;
    HSD_GObjObject_80390A70(camera, kind, lb_80013B14(DP(DP(dat->unk8)[gp->map_id].x10)));
    GObj_SetupGXLinkMax(camera, bx_video_camera_render, 1);
    camera->gxlink_prios = 2;
    v->camera = camera;
    v->canvas = HSD_SisLib_803A611C(1, camera, 9, 13, 0, 1, 0, 1);
    bx_text_create(&v->panel[0].title_joint, &v->panel[0].title, v->canvas, 200,
                   Ground_801C3FA4(gobj, 3), 0.3f, 0.3f);
    bx_text_create(&v->panel[0].names_joint, &v->panel[0].names, v->canvas, 150,
                   Ground_801C3FA4(gobj, 4), 0.4f, 0.4f);
    v->panel[0].emblem = Ground_801C3FA4(gobj, 5);
    v->panel[0].frame = Ground_801C3FA4(gobj, 13);
    bx_text_create(&v->panel[1].title_joint, &v->panel[1].title, v->canvas, 200,
                   Ground_801C3FA4(gobj, 7), 0.3f, 0.3f);
    bx_text_create(&v->panel[1].names_joint, &v->panel[1].names, v->canvas, 150,
                   Ground_801C3FA4(gobj, 8), 0.4f, 0.4f);
    v->panel[1].emblem = Ground_801C3FA4(gobj, 9);
    v->panel[1].frame = Ground_801C3FA4(gobj, 14);

    text = HSD_SisLib_803A6754(1, v->canvas);
    HSD_SisLib_803A6B98(text, 0.0f, -16.0f, NULL);
    text->default_alignment = 1;
    text->default_kerning = 1;
    text->pos_x = 0.0f;
    text->pos_y = 0.0f;
    text->pos_z = 0.0f;
    text->x4C = 1;
    text->font_size.x = 0.9f;
    text->font_size.y = 0.9f;
    text->default_fitting = 1;
    text->box_size_x = 156.0f;
    v->banner = text;

    for (i = 0; i < 6; i++) {
        v->teams[i].slots[0] = 0xFF;
        v->teams[i].slots[1] = 0xFF;
        v->teams[i].slots[2] = 0xFF;
    }
    /* +368: a member goes in at slots[count]. The count is 2 bits on the console, a fourth
     * member would wrap it to 0 and write the title pointer's first byte; a VS match has at most
     * three on a team, so slots[3] is a native spare that is never written. */
    for (i = 0; i < 6; i++) {
        const char* name;
        BxTeam* team;
        int index;
        if (!bx_slot_name(i, &name)) {
            continue;
        }
        v->names[i] = name;
        if (gm_8016B168()) {
            index = Player_GetTeam(i);
            if ((u32) index > 5) {
                __assert("boxing_screen.c", 0x2F3, "unknown team");
            }
        } else {
            index = i;
        }
        team = &v->teams[index];
        team->slots[team->count] = i;
        team->count = (team->count + 1) & 3;
        team->team = index & 3;
    }
    for (i = 0; i < 6; i++) {
        bx_find_title(&v->teams[i]);
    }
    v->queue_count = 0;
    for (i = 0; i < 6; i++) {
        if (v->teams[i].count != 0) {
            v->queue[v->queue_count++] = i;
        }
    }
    if (gm_GetCurrentSceneIndex() == 3) {
        bx_video_set_state(gobj, 1);
    } else {
        bx_video_set_state(gobj, 0);
    }
    Ground_801C10B8(gobj, bx_video_go);
    v->nme_timer = -1;
    for (i = 0; i < 6; i++) {
        if (Player_GetPlayerSlotType(i) == Gm_PKind_NA) {
            continue;
        }
        if (bx_same(bx_fighter_name(i), "King Dedede")) {
            v->nme_timer = bx_param->nme_period;
        }
    }
}

/* Video_GetFighterTeamColor (+2164): the colour of the entry that has this fighter's slot, the
 * CPU colour when none has it. */
static const GXColor* bx_fighter_color(HSD_GObj* gobj, HSD_GObj* fighter)
{
    BxVideoVars* v = BX_VARS(BxVideoVars, gobj);
    int slot = ((Fighter*) HSD_GObjGetUserData(fighter))->player_idx;
    int i;
    for (i = 0; i < 6; i++) {
        if (v->teams[i].slots[0] == slot || v->teams[i].slots[1] == slot ||
            v->teams[i].slots[2] == slot)
        {
            return bx_team_color(&v->teams[i]);
        }
    }
    return &bx_param->cpu_color;
}

/* VideoKO_PlayAnimation (+21F0): the crowd cheers, the confetti, sound 0x144, the KO flash's
 * delay, its border and banner in the colour. */
static void bx_ko_play(HSD_GObj* gobj, HSD_GObj* fighter, const GXColor* color)
{
    BxKoVars* v = BX_VARS(BxKoVars, gobj);
    (void) fighter;
    bx_background_cheer(bx_param->cheer_frames);
    bx_floor_confetti();
    lbAudioAx_8002411C(0x144);
    v->delay = bx_param->ko_delay;
    v->border->mat->diffuse = *color;
    v->banner->mat->diffuse = *color;
}

/* Video_OnFrame (+C4C): the screen state timer (2 -> 3 -> 4 -> 5 -> 4 ...), the panels placed,
 * the field camera copying while joint 12 shows, a KO flash for each fighter whose x2071 state is
 * 13 (once per KO), and the VideoNME draws. */
static void bx_video_frame(Ground_GObj* gobj)
{
    BxVideoVars* v = BX_VARS(BxVideoVars, gobj);
    BxFieldCamera* fc;
    int slot;
    if (v->timer > 0) {
        v->timer--;
    } else if (v->timer != -1) {
        switch (v->state) {
        case 2: bx_video_set_state(gobj, 3); break;
        case 3: bx_video_set_state(gobj, 4); break;
        case 4: bx_video_set_state(gobj, 5); break;
        case 5: bx_video_set_state(gobj, 4); break;
        default: break;
        }
    }
    bx_panel_place(&v->panel[0]);
    bx_panel_place(&v->panel[1]);
    fc = HSD_GObjGetUserData(v->field_camera);
    fc->copy = (fc->copy & 0x7F) | ((v->picture->flags & JOBJ_HIDDEN) ? 0 : 0x80);
    for (slot = 0; slot < 6; slot++) {
        HSD_GObj* fighter = Player_GetEntity(slot);
        Fighter* fp;
        if (fighter == NULL) {
            continue;
        }
        fp = HSD_GObjGetUserData(fighter);
        if (fp->x2070.x2071_b0_3 != 13) {
            v->ko_shown[slot] = 0;
            continue;
        }
        if (v->ko_shown[slot] != 0) {
            continue;
        }
        if (v->ko == NULL) {
            v->ko = mu_grTSeak_80223908(9);
        }
        bx_ko_play(v->ko, fighter, bx_fighter_color(gobj, fighter));
        v->ko_shown[slot] = 1;
    }
    if (v->nme_timer == -1) {
        return;
    }
    if (Ground_GetMapGObj(10) != NULL) {
        return;
    }
    if (v->nme_timer > 0) {
        v->nme_timer--;
        return;
    }
    v->nme_timer = bx_param->nme_period;
    if (!(bx_param->nme_chance >= HSD_Randf())) {
        return;
    }
    mu_grTSeak_80223908(10);
    v->nme_timer += bx_param->nme_frames;
}

/* Screen_OnCreation (+5C8): animation set 0, x11 bits 0 to 2 = 1, x10 bit 2 off, then Video, whose
 * camera gets the Screen texture's image desc (joint 2) as its user data. Video is not checked, as
 * on the console. */
static void bx_screen_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    HSD_TObj* tobj;
    HSD_GObj* video;
    bx_anim(gobj, 0);
    tobj = Ground_801C3FA4(gobj, 2)->u.dobj->mobj->tobj;
    gp->x11_flags.b012 = 1;
    gp->x10_flags.b2 = 0;
    video = mu_grTSeak_80223908(8);
    BX_VARS(BxVideoVars, video)->camera->user_data = tobj->imagedesc;
}

/* ---- 9 VideoKO, 10 VideoNME ---- */

/* VideoKO_OnCreation (+E84): scale 1, x8 callback NULL, xC0 0, gx link 1, the materials of joints
 * 1 (border) and 2 (banner), no delay. */
static void bx_ko_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    BxKoVars* v = BX_VARS(BxKoVars, gobj);
    HSD_DObj* dobj;
    bx_clear_scale(gobj);
    gp->x8_callback = NULL;
    gp->xC0 = 0.0f;
    HSD_GObjGXLink_8039084C(gobj);
    GObj_SetupGXLink(gobj, grDisplay_801C5DB0, 1, 0);
    dobj = HSD_JObjGetDObj(Ground_801C3FA4(gobj, 1));
    if (dobj == NULL) {
        __assert("boxing_screen.h", 0x89, "could not find border object");
    }
    v->border = dobj->mobj;
    dobj = HSD_JObjGetDObj(Ground_801C3FA4(gobj, 2));
    if (dobj == NULL) {
        __assert("boxing_screen.h", 0x8F, "could not find banner object");
    }
    v->banner = dobj->mobj;
    v->delay = -1;
}

/* VideoKO_OnFrame (+F74): at 0 its animation, sound 4 and a quake (kind 2); a timer at 0 still
 * counts down to -1. */
static void bx_ko_frame(Ground_GObj* gobj)
{
    BxKoVars* v = BX_VARS(BxKoVars, gobj);
    if (v->delay < 0) {
        return;
    }
    if (v->delay == 0) {
        Vec3 origin;
        bx_anim(gobj, 0);
        bx_sound(4);
        origin.x = 0.0f;
        origin.y = 0.0f;
        origin.z = 0.0f;
        Camera_RequestQuake(2, &origin);
    }
    v->delay--;
}

/* VideoNME_OnCreation (+1000): scale 1, x8 callback NULL, xC0 0, gx link 1, animation set 0, its
 * life, sound 0. */
static void bx_nme_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    bx_clear_scale(gobj);
    gp->x8_callback = NULL;
    gp->xC0 = 0.0f;
    HSD_GObjGXLink_8039084C(gobj);
    GObj_SetupGXLink(gobj, grDisplay_801C5DB0, 1, 0);
    bx_anim(gobj, 0);
    BX_VARS(BxNmeVars, gobj)->timer = bx_param->nme_frames;
    bx_sound(0);
}

/* VideoNME_OnFrame (+1098): removed when its life is over. */
static void bx_nme_frame(Ground_GObj* gobj)
{
    BxNmeVars* v = BX_VARS(BxNmeVars, gobj);
    if (v->timer > 0) {
        v->timer--;
        return;
    }
    Ground_801C4A08(gobj);
}

/* ---- 11 RopeFG (in the table; the block never creates it) ---- */

/* Vec2Cross (+3204) and Cross (+3190): (a - b) x (c - b) in x, y. */
static MU_NO_CONTRACT f32 bx_cross(const Vec3* a, const Vec3* b, const Vec3* c)
{
    f32 d1x = a->x - b->x;
    f32 d1y = a->y - b->y;
    f32 d2x = c->x - b->x;
    f32 d2y = c->y - b->y;
    f32 p = d1x * d2y;
    f32 q = d1y * d2x;
    return p - q;
}

/* ContainsPoint (+2924): the point inside the view quad with all four cross products >= 0, or
 * (the other winding) the first three > 0 or NaN and the last <= 0, as the block's compares. */
static MU_NO_CONTRACT bool bx_contains(const BxRopeFgVars* v, const Vec3* point)
{
    f32 c1 = bx_cross(&v->view[1], &v->view[0], point);
    f32 c2 = bx_cross(&v->view[2], &v->view[1], point);
    f32 c3 = bx_cross(&v->view[3], &v->view[2], point);
    f32 c4 = bx_cross(&v->view[0], &v->view[3], point);
    bool nonneg = c1 >= 0.0f && c2 >= 0.0f && c3 >= 0.0f && c4 >= 0.0f;
    bool other = !(c1 <= 0.0f) && !(c2 <= 0.0f) && !(c3 <= 0.0f) && c4 <= 0.0f;
    return nonneg || other;
}

/* SetAlpha (+2A98): the alpha of every material of a joint tree (its children and next joints). */
static void bx_set_alpha(HSD_JObj* jobj, f32 alpha)
{
    HSD_DObj* dobj;
    for (dobj = jobj->u.dobj; dobj != NULL; dobj = dobj->next) {
        if (dobj->mobj != NULL) {
            dobj->mobj->mat->alpha = alpha;
        }
    }
    if (jobj->child != NULL) {
        bx_set_alpha(jobj->child, alpha);
    }
    if (jobj->next != NULL) {
        bx_set_alpha(jobj->next, alpha);
    }
}

/* RopeFG_AlphaThink (+22C4, its render callback, link 3): in pass 0 the four corners in view
 * space; while player slot 0's fighter is inside them the ropes fade by 0.1 a frame down to 0.4,
 * else back by 0.1 up to 1 (sums in double, as the block's fadd / fsub with a double 0.1); then the
 * stage display. The block reads slot 0's fighter without a check (on the console a missing one
 * reads low memory); natively a missing fighter only skips the fade. */
static MU_NO_CONTRACT void bx_ropefg_draw(HSD_GObj* gobj, intptr_t pass)
{
    if (pass == 0) {
        BxRopeFgVars* v = BX_VARS(BxRopeFgVars, gobj);
        HSD_GObj* fighter;
        Mtx view;
        Vec3 point;
        int i;
        HSD_CObjGetViewingMtx(GET_COBJ(Camera_80030A50()), view);
        for (i = 0; i < 4; i++) {
            PSMTXMultVec(view, &v->corner[i], &v->view[i]);
        }
        fighter = Player_GetEntity(0);
        if (fighter != NULL) {
            PSMTXMultVec(view, &((Fighter*) HSD_GObjGetUserData(fighter))->cur_pos, &point);
            if (bx_contains(v, &point)) {
                f32 alpha = v->alpha;
                if (!(alpha <= 0.4f)) {         /* ble: a NaN alpha goes on, as the block */
                    alpha = (f32) ((f64) alpha - 0.1);
                    if (!(alpha > 0.4f) && alpha == alpha) {
                        alpha = 0.4f;
                    }
                    v->alpha = alpha;
                    bx_set_alpha(GET_JOBJ(gobj), alpha);
                }
            } else {
                f32 alpha = v->alpha;
                if (alpha < 1.0f) {
                    alpha = (f32) ((f64) alpha + 0.1);
                    if (!(alpha < 1.0f) && alpha == alpha) {
                        alpha = 1.0f;
                    }
                    v->alpha = alpha;
                    bx_set_alpha(GET_JOBJ(gobj), alpha);
                }
            }
        }
    }
    grDisplay_801C5DB0(gobj, pass);
}

/* RopeFG_OnCreation (+10DC): the world positions of joints 3 to 6, alpha 1, its draw in gx link 3,
 * x11 bits 0 to 2 = 3. */
static void bx_ropefg_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    BxRopeFgVars* v = BX_VARS(BxRopeFgVars, gobj);
    int i;
    for (i = 0; i < 4; i++) {
        lb_8000B1CC(Ground_801C3FA4(gobj, 3 + i), NULL, &v->corner[i]);
    }
    v->alpha = 1.0f;
    HSD_GObjGXLink_8039084C(gobj);
    GObj_SetupGXLink(gobj, bx_ropefg_draw, 3, 0);
    gp->x11_flags.b012 = 3;
}

/* ---- export 1: map_gobjs (+0): OnCreation, OnUnk, OnFrame, OnDeletion, flags ---- */

#define BX_ROW(create, frame, flags) { create, bx_unk, frame, bx_deletion, flags }

static StageCallbacks bx_map[12] = {
    BX_ROW(bx_points_create, mu_gr_gobj_nop, 0),                   /* 0 Points */
    BX_ROW(bx_sky_create, mu_gr_gobj_nop, 0),                      /* 1 Sky */
    BX_ROW(bx_background_create, bx_background_frame, 0xC0000000), /* 2 Background */
    BX_ROW(bx_lights_create, mu_gr_gobj_nop, 0),                   /* 3 Lights */
    BX_ROW(bx_floor_create, bx_floor_frame, 0),                    /* 4 Floor */
    BX_ROW(bx_rope_create, mu_gr_gobj_nop, 0),                     /* 5 Rope */
    BX_ROW(bx_screen_create, mu_gr_gobj_nop, 0),                   /* 6 Screen */
    BX_ROW(bx_cannon_create, bx_cannon_frame, 0),                  /* 7 Cannon */
    BX_ROW(bx_video_create, bx_video_frame, 0),                    /* 8 Video */
    BX_ROW(bx_ko_create, bx_ko_frame, 0),                          /* 9 VideoKO */
    BX_ROW(bx_nme_create, bx_nme_frame, 0),                        /* 10 VideoNME */
    BX_ROW(bx_ropefg_create, mu_gr_gobj_nop, 0),                   /* 11 RopeFG */
};

/* Exports 4 to 9 (+198 .. +1B4): lone blr (demo init, on_load, OnGO), "li r3, 0" (OnUnused,
 * onTouchLine), "li r3, 1" (OnCheckShadowRender). */
const MuGrStage mu_gr_boxing_ring = {
    .name = "Boxing Ring",
    .file = "/GrBx.dat",
    .map_gobjs = bx_map,
    .on_init = bx_init,
    .on_demo_init = mu_gr_demo_nop,
    .on_load = mu_gr_event_nop,
    .on_start = mu_gr_event_nop,
    .callback4 = mu_gr_false,
    .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = mu_gr_shadow_always,
    .flags = MU_GR_READY,
};
