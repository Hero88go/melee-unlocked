/* GrGc.dat, internal 89, external 306. Native translation of grFunction,
 * exports 1 and 3 through 9, offsets refer to disassembly.txt.
 * Separate multiply and add instructions retain their individual roundings. */
#include "gc_lib.h"
#include <dolphin/card.h>
#include <dolphin/os.h>
#include <melee/ef/efsync.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ft_0D31.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/if/if_2F6E.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbtime.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/axdriver.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/tobj.h>
#include <math.h>
#include <string.h>

typedef struct GcWeight { u16 id, weight; } DISC_STRUCT GcWeight;
typedef struct GcParam {
    s32 x00[4];
    Vec3_BE text_pos, text_scale;
    DISC_PTR(HSD_Joint) cube_joint;
    DISC_PTR(HSD_AnimJoint) cube_enter;
    DISC_PTR(HSD_MatAnimJoint) cube_enter_mat;
    DISC_PTR(HSD_AnimJoint) cube_exit;
    DISC_PTR(HSD_MatAnimJoint) cube_exit_mat;
    DISC_PTR(HSD_Joint) score_joint;
    s32 view_time, save_life, save_gap;
    f32 save_rate;
    s32 clock_life;
    f32 clock_scale, clock_marker_scale;
    s32 pong_enter;
    s32 pong_win_score;
    f32 bounce_mul, bounce_factor, ball_speed, ball_accel, ball_max, ball_noise;
    f32 paddle_accel, paddle_fast_accel, paddle_friction, paddle_fast_friction;
    f32 paddle_max, paddle_fast_max;
    Vec3_BE winner_offset;
    s32 catch_window;
    f32 fighter_scale, bios_close_chance;
    s32 cube_gap_min, cube_gap_max;
    f32 line_rate_min, line_rate_max;
    s32 floor_count;
    DISC_PTR(GcWeight) floor_weights;
} DISC_STRUCT GcParam;
_Static_assert(__builtin_offsetof(GcParam,cube_joint) == 0x28, "parameter model slots");
_Static_assert(__builtin_offsetof(GcParam,view_time) == 0x40, "parameter event timings");
_Static_assert(__builtin_offsetof(GcParam,pong_win_score) == 0x60, "parameter game settings");
_Static_assert(__builtin_offsetof(GcParam,winner_offset) == 0x94, "parameter winner position");
_Static_assert(__builtin_offsetof(GcParam,floor_weights) == 0xC0, "parameter floor playlist");
typedef struct GcGlyph {
    u8 letter, advance, spacing, x3;
    DISC_PTR(u8) bitmap;
} DISC_STRUCT GcGlyph;
typedef DISC_PTR(GcGlyph) GcGlyphSlot;
typedef struct GcSaveIcon {
    char comment[64];
    u8 pingpong, count;
    u16 speeds;
    HSD_ImageDesc* images[8];
    HSD_Tlut* tlut;
} GcSaveIcon;
typedef struct GcBackground { int timer, last; HSD_GObj* event; } GcBackground;
typedef struct GcSave {
    u8 state; s8 frame; u8 delay; s8 direction;
    GcSaveIcon* icon;
    Vec3 pos, scale;
} GcSave;
typedef struct GcText { int life, leave; HSD_GObjEvent callback; } GcText;
typedef struct GcSaveEvent {
    int count, gap;
    HSD_GObj* saves[21];
    int life, leave;
} GcSaveEvent;
typedef struct GcClock {
    HSD_JObj* blocks;
    HSD_JObj* anchors[16];
    int life;
    u8 leaving;
} GcClock;
typedef struct GcBios {
    int timer, state;
    Vec3 pos;
    HSD_JObj* root;
    HSD_JObj* base;
    HSD_JObj* menu;
    HSD_JObj* disc;
    HSD_JObj* clock;
    HSD_JObj* memory;
    HSD_JObj* options;
    HSD_JObj* controller;
} GcBios;
typedef struct GcBall {
    HSD_JObj* joint;
    f32 x, y, radius, speed, vx, vy;
} GcBall;
typedef struct GcPaddle {
    HSD_GObj* fighter;
    HSD_JObj* joint;
    HSD_JObj* score_joint;
    f32 x, y, size;
    u32 buttons;
    f32 velocity;
    u8 score, winner;
    int cpu_timer;
    f32 cpu_offset, cpu_error;
} GcPaddle;
typedef struct GcTV {
    GcBall ball;
    GcPaddle left, right;
    f32 min_x, max_x, top, bottom;
    int state, timer;
} GcTV;
typedef struct GcView { int state, timer; HSD_GObj* save; } GcView;
typedef struct GcCubes { int state, timer; } GcCubes;
typedef struct GcPaddleMotion { GcPaddle* paddle; int warped; } GcPaddleMotion;
GC_FIT(GcBackground); GC_FIT(GcSave); GC_FIT(GcText); GC_FIT(GcSaveEvent);
GC_FIT(GcClock); GC_FIT(GcBios); GC_FIT(GcTV); GC_FIT(GcView);
_Static_assert(sizeof(GcPaddleMotion) <= sizeof(((Fighter*)0)->mv), "paddle state fits");
#define BG(g) GC_VARS(GcBackground, HSD_GObjGetUserData(g))
#define SAVE(g) GC_VARS(GcSave, HSD_GObjGetUserData(g))
#define TEXT(g) GC_VARS(GcText, HSD_GObjGetUserData(g))
#define SE(g) GC_VARS(GcSaveEvent, HSD_GObjGetUserData(g))
#define CLOCK(g) GC_VARS(GcClock, HSD_GObjGetUserData(g))
#define BIOS(g) GC_VARS(GcBios, HSD_GObjGetUserData(g))
#define TV(g) GC_VARS(GcTV, HSD_GObjGetUserData(g))
#define VIEW(g) GC_VARS(GcView, HSD_GObjGetUserData(g))
#define CUBES(g) GC_VARS(GcCubes, HSD_GObjGetUserData(g))
#define PM(fp) ((GcPaddleMotion*) (void*) &(fp)->mv)
static GcParam* gc_param;
static GcGlyphSlot* gc_font;
static GcSaveIcon gc_icons[168];
static u8 gc_queue[128], gc_save_count, gc_save_index;
static int gc_volume = 127;

static HSD_JObj* gc_joint(HSD_GObj* g, int n) { return Ground_801C3FA4(g, n); }
static void gc_anim(HSD_GObj* g, int n)
{
    Ground* gp = HSD_GObjGetUserData(g);
    grAnime_801C8138(g, gp->map_id, n);
}
static void gc_rate(HSD_JObj* j, double rate)
{
    /* Jev antenna_rate review in ../fichina applies to this same callback ABI
     * choice: use the explicitly computed rate (confidence 1.0), preserving
     * random draws. Original arg type 6 depends on volatile console f1. */
    HSD_ForeachAnim(j, JOBJ_TYPE, 0xFFFF, HSD_AObjSetRate, AOBJ_ARG_AF, rate);
}
static Vec3 gc_vec(Vec3_BE v) { Vec3 r = {v.x,v.y,v.z}; return r; }
static HSD_DObj* gc_dobj(HSD_JObj* j, int n)
{
    HSD_DObj* d = j->u.dobj;
    while (n-- > 0) {
        if (d->next == NULL) __assert("gamecube.c", 0x1D8, "dobj not found");
        d = d->next;
    }
    return d;
}
/* +2E08: MEX_GetData(16) stage bank, preserving the raw sound parameters. */
static void gc_sound(int id, int volume, int pan)
{
    int bank = mu_gr_mex_stage_bank(Stage_8022519C(Stage_80225194()));
    HSD_AudioSFXStartParam(bank * 10000 + id, volume, pan, 0, 6);
}
static void gc_shuffle(void)
{
    int i;
    for (i = gc_save_count; i > 1; --i) {
        int n = HSD_Randi(i);
        u8 t = gc_queue[i-1]; gc_queue[i-1] = gc_queue[n]; gc_queue[n] = t;
    }
}
/* +7C5C: sector-aligned card read. The source does not close its fast-open file. */
static void gc_card_read(int index, void* dest, int length, int offset)
{
    CARDFileInfo file;
    if (CARDFastOpen(0, index, &file) == 0) {
        int displacement = offset & 511;
        int amount = length;
        void* buffer;
        if (displacement) {
            offset -= displacement;
            amount += displacement;
            if (amount & 511) amount = (amount / 512) * 512 + 512;
        }
        buffer = mu_gr_mex_calloc(amount);
        CARDRead(&file, buffer, amount, offset);
        memcpy(dest, (u8*)buffer + displacement, length);
        HSD_Free(buffer);
    }
}
/* +1BBC: select at most 21 save files, load their comments and animated icons. */
static void gc_load_saves(void)
{
    void* work = HSD_MemAlloc(0xA000);
    void* comments = HSD_MemAlloc(512);
    CARDStat stat;
    int total = 0, i;
    gc_save_count = gc_save_index = 0;
    /* The native CARDMountAsync service completes the mount before returning.
     * No callback is needed for the source's synchronous wait here. */
    if (CARDMountAsync(0, work, NULL, NULL) >= 0) {
        for (i = 0; i < 128; ++i) {
            if (CARDGetStatus(0, i, &stat) >= 0) gc_queue[total++] = i;
        }
        gc_save_count = total;
        gc_shuffle();
        gc_save_count = 0;
        for (i = 0; i < total && i < 21; ++i) {
            int file = gc_queue[i], k;
            GcSaveIcon* icon;
            if (CARDGetStatus(0, file, &stat) < 0) continue;
            k = gc_save_count++;
            gc_queue[k] = k;
            icon = &gc_icons[k];
            icon->pingpong = (stat.bannerFormat >> 2) & 1;
            icon->speeds = stat.iconSpeed;
            icon->count = 0;
            for (k = 0; k < 8; ++k) {
                int format = (stat.iconFormat >> (k * 2)) & 3;
                int bytes;
                HSD_ImageDesc* image;
                void* data;
                if (format == 0) break;
                image = HSD_ImageDescAlloc();
                icon->images[k] = image;
                bytes = 0;
                if (format == 1) { image->format = GX_TF_C8; bytes = 0x400; }
                if (format == 2) { image->format = GX_TF_RGB5A3; bytes = 0x800; }
                image->width = image->height = 32;
                data = HSD_MemAlloc(bytes);
                DISC_SET(image->image_ptr, data);
                gc_card_read(file, data, bytes, stat.offsetIcon[k]);
                ++icon->count;
            }
            if (stat.offsetIconTlut != 0xFFFFFFFF) {
                icon->tlut = HSD_TlutAlloc();
                icon->tlut->n_entries = 256;
                icon->tlut->fmt = GX_TL_RGB5A3;
                icon->tlut->lut = HSD_MemAlloc(512);
                gc_card_read(file, icon->tlut->lut, 512, stat.offsetIconTlut);
            }
            if (stat.commentAddr != 0xFFFFFFFF) {
                gc_card_read(file, comments, 512, stat.commentAddr);
                memcpy(icon->comment, comments, 64);
            }
        }
        gc_shuffle();
        CARDUnmount(0);
    }
    HSD_Free(work); HSD_Free(comments);
}

static HSD_GObj* gc_text_create(const char*, int, Vec3*, Vec3*, f32, int, HSD_GObjEvent);
static void gc_text_destroy(HSD_GObj*);
static void gc_save_state(HSD_GObj*, int);
static void gc_bios_state(HSD_GObj*, int);
static void gc_clock_update(HSD_GObj*);
static void gc_fighter_accessory(HSD_GObj*);
static void gc_tv_process(HSD_GObj*);
static void gc_ball_init(HSD_GObj*);
static void gc_score(HSD_JObj*, int);
static void gc_score_hide(HSD_JObj*);

/* +2014: one animated cube for every set cell of each 6 by 7 glyph bitmap. */
static MU_NO_CONTRACT HSD_GObj* gc_text_create(const char* text, int animation,
    Vec3* pos, Vec3* scale, f32 max_width, int life, HSD_GObjEvent callback)
{
    HSD_GObj* g = mu_grTSeak_80223908(4);
    HSD_JObj* parent;
    HSD_JObj* j;
    int advance = 0, c;
    TEXT(g)->life = life;
    TEXT(g)->callback = callback;
    grAnime_801C8138(g, 4, animation);
    parent = gc_joint(g, 1);
    for (c = 0; text[c]; ++c) {
        int ch = (u8)text[c], n;
        GcGlyph* glyph = NULL;
        if ((unsigned)(ch - 'A') <= 25) ch += 'a' - 'A';
        if (gc_font != NULL) {
            for (n = 0; DP(gc_font[n]) != NULL; ++n) {
                GcGlyph* p = DP(gc_font[n]);
                if (p->letter == ch) { glyph = p; break; }
            }
        }
        if (glyph != NULL) {
            int row, col;
            u8* bitmap = DP(glyph->bitmap);
            for (row = 0; row < 7; ++row) {
                for (col = 0; col < 6; ++col) {
                    if (!bitmap[row * 8 + col]) continue;
                    j = HSD_JObjLoadJoint(DP(gc_param->cube_joint));
                    j->translate.x = (f32)(advance * 10 + col * 10);
                    j->translate.y = (f32)(-row * 10);
                    HSD_JObjAddAnimAll(j, DP(gc_param->cube_enter), DP(gc_param->cube_enter_mat), NULL);
                    HSD_JObjReqAnimAll(j, 0);
                    HSD_JObjAnimAll(j);
                    gc_rate(j, (double)HSD_Randf() * 0.5 + 6.0);
                    HSD_JObjAddChild(parent, j);
                }
            }
            advance += glyph->advance + glyph->spacing;
        }
    }
    for (j = parent->child; j; j = j->next) {
        j->translate.x -= (f32)((advance - 1) * 5);
        HSD_JObjSetMtxDirtySub(j);
    }
    j = gc_joint(g, 0);
    j->translate = *pos;
    HSD_JObjSetMtxDirtySub(j);
    parent->scale = *scale;
    if (max_width != -1) {
        f32 ratio = (scale->x * max_width) / (f32)((double)advance * 10.0 * (double)scale->x);
        /* Jev question: repair ordinary text fitting? Answer: keep negative-only
         * test (+23F0), normal positive strings never shrink. jev-cache/14b4... */
        if (ratio < 0) parent->scale.x = ratio;
    }
    HSD_JObjSetMtxDirtySub(parent);
    return g;
}
static MU_NO_CONTRACT void gc_text_destroy(HSD_GObj* g)
{
    HSD_JObj* j;
    TEXT(g)->leave = 60;
    for (j = gc_joint(g, 1)->child; j; j = j->next) {
        HSD_JObjAddAnimAll(j, DP(gc_param->cube_exit), DP(gc_param->cube_exit_mat), NULL);
        HSD_JObjReqAnimAll(j, 0); HSD_JObjAnimAll(j);
        gc_rate(j, (double)HSD_Randf() * 0.5 + 0.5);
    }
}
static void gc_text_create_map(HSD_GObj* g)
{
    mexgr_gc_anim0_create(g); TEXT(g)->life = TEXT(g)->leave = -1;
}
static bool gc_text_callback(HSD_GObj* g)
{
    if (TEXT(g)->callback) TEXT(g)->callback(g);
    return g != NULL;
}
static void gc_text_frame(HSD_GObj* g)
{
    GcText* s = TEXT(g);
    if (s->leave != -1) {
        if (s->leave <= 0) { Ground_801C4A08(g); return; }
        --s->leave;
    }
    if (s->life >= 0 && --s->life == 0) gc_text_destroy(g);
}
static void gc_save_scale(HSD_GObj* g, Vec3* scale)
{
    HSD_JObj* root = gc_joint(g, 0);
    SAVE(g)->scale = *scale; root->scale = *scale; HSD_JObjSetMtxDirtySub(root);
}
static void gc_save_create(HSD_GObj* g)
{
    Vec3 scale = {1,1,1};
    SAVE(g)->frame = SAVE(g)->delay = 0; SAVE(g)->direction = 1;
    gc_save_scale(g, &scale);
}
static void gc_save_state(HSD_GObj* g, int state)
{
    GcSave* s = SAVE(g);
    HSD_JObj* j;
    gc_anim(g, state); s->state = state;
    j = gc_joint(g, 0); j->translate = s->pos; j->scale = s->scale;
    HSD_JObjSetMtxDirtySub(j);
}
static HSD_GObj* gc_spawn_save(void)
{
    HSD_GObj* g = mu_grTSeak_80223908(3);
    if (gc_save_count > gc_save_index) {
        /* Jev question: single or double queue lookup at +28E4,+28E8?
         * Answer: retain both. This changes displayed icon order. */
        SAVE(g)->icon = &gc_icons[gc_queue[gc_queue[gc_save_index++]]];
    } else {
        SAVE(g)->icon = NULL;
        HSD_DObjSetFlags(gc_dobj(gc_joint(g, 1), 2), 1);
    }
    gc_save_state(g, 0);
    return g;
}
static void gc_save_frame(HSD_GObj* g)
{
    GcSave* s = SAVE(g);
    GcSaveIcon* icon;
    HSD_TObj* tex;
    int speed;
    if (s->state == 0) lb_8000B09C(gc_joint(g, 1));
    if (s->state == 2 && !lb_8000B09C(gc_joint(g, 1))) {
        Ground_801C4A08(g); return;
    }
    icon = s->icon;
    if (!icon || !icon->count) return;
    if (s->delay) { --s->delay; return; }
    tex = gc_dobj(gc_joint(g, 1), 2)->mobj->tobj;
    tex->imagedesc = icon->images[(int)s->frame]; tex->tlut = icon->tlut;
    speed = (icon->speeds >> (s->frame * 2)) & 3;
    s->delay = speed == 0 ? 255 : speed == 1 ? 4 : speed == 2 ? 8 : 16;
    if (s->direction > 0) {
        ++s->frame;
        if (s->frame >= icon->count) {
            if (icon->pingpong) { s->frame = icon->count - 2; s->direction = -1; }
            else s->frame = 0;
        }
    } else if (s->direction < 0) {
        --s->frame;
        if (s->frame < 0) {
            if (icon->pingpong) { s->frame = 1; s->direction = 1; }
            else s->frame = icon->count - 1;
        }
    }
    if (s->frame >= icon->count) s->frame = 0;
    else if (s->frame < 0) s->frame = icon->count - 1;
}
static void gc_save_delete(HSD_GObj* g)
{
    HSD_TObj* tex = gc_dobj(gc_joint(g, 1), 2)->mobj->tobj;
    tex->imagedesc = NULL; tex->tlut = NULL;
}

/* +AD4,+B90: 21 animated save cubes, linked to the event's anchor bones. */
static void gc_save_event_create(HSD_GObj* g)
{
    GcSaveEvent* s = SE(g);
    gc_anim(g, 0);
    s->life = gc_param->save_life; s->leave = -1;
    s->gap = gc_param->save_gap; s->count = 0;
    gc_save_index = 0; gc_shuffle();
    gc_rate(gc_joint(g, 0), gc_param->save_rate);
}
static void gc_save_event_frame(HSD_GObj* g)
{
    static const int bones[21] = {3,5,7,9,11,13,15,18,20,22,24,26,28,30,33,35,37,39,41,43,45};
    GcSaveEvent* s = SE(g);
    int i;
    if (s->count <= 20) {
        if (s->gap > 0) --s->gap;
        else {
            HSD_GObj* save = gc_spawn_save();
            HSD_JObj* root = gc_joint(save, 0);
            HSD_JObj* anchor = gc_joint(g, bones[s->count]);
            Vec3 scale;
            anchor->parent->rotate.x = (f32)(0.15 - (double)(s->count / 7) * 0.15);
            anchor->translate.z = -1000;
            anchor->scale.x = anchor->scale.y = anchor->scale.z = 3;
            lb_8000C2F8(root, anchor);
            scale = anchor->scale; gc_save_scale(save, &scale);
            s->saves[s->count++] = save;
            s->gap = gc_param->save_gap;
        }
    }
    if (s->life >= 0) {
        if (--s->life == 0) {
            for (i = 0; i < s->count; ++i) if (s->saves[i]) gc_save_state(s->saves[i], 2);
            s->leave = 100;
            --s->leave;
            return;
        }
    }
    if (s->leave != -1) {
        if (s->leave > 0) --s->leave;
        else Ground_801C4A08(g);
    }
}
/* +2A48: the clock draws copies of smc_joint at animated anchor positions. */
static void gc_shared_blocks(HSD_GObj* g, HSD_JObj** dest, int count)
{
    HSD_Joint* desc = HSD_ArchiveGetPublicAddress(grDatFiles_801C6330(0)->unk0, "smc_joint");
    int i;
    *dest = HSD_JObjAlloc();
    HSD_JObjAddChild(gc_joint(g, 0), *dest);
    for (i = 0; i < count; ++i) HSD_JObjAddChild(*dest, HSD_JObjLoadJoint(desc));
}
static MU_NO_CONTRACT void gc_block_position(HSD_JObj* root, int n, HSD_JObj* anchor, f32 scale)
{
    HSD_JObj* block = NULL;
    lb_80011E24(root,&block,n+1,-1);
    if (block != NULL) {
        lb_8000B1CC(anchor, NULL, &block->translate);
        block->scale = anchor->scale;
        block->scale.x *= scale; block->scale.y *= scale; block->scale.z *= scale;
        HSD_JObjSetMtxDirtySub(block);
    }
}
static void gc_clock_create(HSD_GObj* g)
{
    static const int bones[16] = {1,2,3,4,5,6,8,13,7,9,10,11,12,14,15,16};
    GcClock* s = CLOCK(g);
    int i;
    gc_anim(g, 0); gc_shared_blocks(g, &s->blocks, 13);
    for (i = 0; i < 16; ++i) s->anchors[i] = gc_joint(g, bones[i]);
    gc_clock_update(g); s->life = gc_param->clock_life;
}
static MU_NO_CONTRACT void gc_clock_update(HSD_GObj* g)
{
    GcClock* s = CLOCK(g);
    datetime time;
    double r;
    int i;
    for (i = 0; i < 5; ++i) gc_block_position(s->blocks, i, s->anchors[i], gc_param->clock_scale);
    gm_801692E8(lbTime_GetTimeInSeconds(), &time);
    r = (double)time.second / 60.0 * 3.141592653589793;
    s->anchors[5]->rotate.z = -(f32)(r + r); HSD_JObjSetMtxDirtySub(s->anchors[5]);
    r = (double)time.minute / 60.0 * 3.141592653589793;
    s->anchors[6]->rotate.z = -(f32)(r + r); HSD_JObjSetMtxDirtySub(s->anchors[6]);
    r = (double)(time.hour % 12) / 12.0 * 3.141592653589793;
    s->anchors[7]->rotate.z = -(f32)(r + r); HSD_JObjSetMtxDirtySub(s->anchors[7]);
    for (i = 5; i < 13; ++i) gc_block_position(s->blocks, i, s->anchors[i+3], gc_param->clock_marker_scale);
}
static void gc_clock_frame(HSD_GObj* g)
{
    GcClock* s = CLOCK(g);
    gc_clock_update(g);
    if (s->life > 0) --s->life;
    else if (!s->leaving) { s->leaving = 1; gc_anim(g, 1); }
    if (s->leaving && !lb_8000B09C(gc_joint(g, 0))) Ground_801C4A08(g);
}
static void gc_bios_anim(HSD_GObj* g, HSD_JObj* j, int animation)
{
    Ground* gp = HSD_GObjGetUserData(g);
    struct UnkStageDat_x8_t* row = &DP(grDatFiles_801C6330(gp->map_id)->unk4->unk8)[gp->map_id];
    HSD_AnimJointSlot* slots = DP(row->unk4);
    HSD_JObjRemoveAnimByFlags(j, 0x1FF);
    HSD_JObjAddAnim(j, DP(slots[animation]), NULL, NULL);
    HSD_JObjReqAnim(j, 0); HSD_JObjAnim(j);
}
static void gc_bios_state(HSD_GObj* g, int state)
{
    static const s8 anim[9] = {-1,4,8,3,7,6,10,5,9};
    static const u16 timer[9] = {240,240,30,240,30,240,30,240,30};
    GcBios* s = BIOS(g);
    if (anim[state] >= 0) gc_bios_anim(g, s->menu, anim[state]);
    HSD_JObjSetFlags(s->controller, 16); HSD_JObjSetFlags(s->memory, 16);
    HSD_JObjSetFlagsAll(s->clock, 16); HSD_JObjSetFlags(s->disc, 16);
    HSD_JObjSetFlags(s->options, 16);
    s->timer = timer[state]; s->state = state;
}
static void gc_bios_create(HSD_GObj* g)
{
    GcBios* s = BIOS(g);
    HSD_JObj* j;
    s->pos.x = s->pos.y = 0; s->pos.z = -500;
    s->root = gc_joint(g,1); s->base = gc_joint(g,2); s->menu = gc_joint(g,3);
    s->disc = gc_joint(g,9); s->clock = gc_joint(g,12); s->memory = gc_joint(g,8);
    s->options = gc_joint(g,11); s->controller = gc_joint(g,10);
    j = gc_joint(g,0); j->translate = s->pos; HSD_JObjSetupMatrixSub(j);
    gc_bios_state(g,0); gc_sound(1,127,64);
    gc_bios_anim(g,s->root,0); gc_bios_anim(g,s->base,1);
}
static void gc_bios_close(HSD_GObj* g)
{
    GcBios* s = BIOS(g);
    gc_bios_anim(g,s->root,2); s->state = -1; s->timer = 60;
}
static void gc_bios_frame(HSD_GObj* g)
{
    GcBios* s = BIOS(g);
    if (s->timer == 0) {
        if (s->state == -1) { Ground_801C4A08(g); return; }
        if ((unsigned)s->state <= 8) {
            if ((1 << s->state) & 0x154) gc_bios_state(g,0);
            else if ((1 << s->state) & 0xAA) {
                if (HSD_Randf() < gc_param->bios_close_chance) {
                    gc_bios_close(g); gc_sound(0,127,64);
                } else { gc_bios_state(g,s->state+1); gc_sound(2,127,64); }
            } else if (s->state == 0) {
                static const int options[4] = {3,1,5,7};
                gc_bios_state(g, options[HSD_Randi(4)]); gc_sound(3,127,64);
            }
        }
    }
    if (s->timer > 0) --s->timer;
}
static void gc_score(HSD_JObj* j, int value)
{
    HSD_DObj* d;
    int i = 0;
    for (d = j->u.dobj; d; d = d->next,++i) {
        if (i == value) HSD_DObjClearFlags(d,1); else HSD_DObjSetFlags(d,1);
    }
}
static void gc_score_hide(HSD_JObj* j)
{
    HSD_DObj* d;
    for (d = j->u.dobj; d; d = d->next) HSD_DObjSetFlags(d,1);
}
static void gc_warp(HSD_GObj* g, f32 size)
{
    Fighter* fp = HSD_GObjGetUserData(g);
    f32 scale = size * (fp->x34_scale.y * fp->co_attrs.model_scaling);
    efSync_Spawn(0x43F, NULL, &fp->cur_pos, &scale, -1);
}
static void gc_fighter_return(HSD_GObj* g)
{
    Fighter* fp = HSD_GObjGetUserData(g);
    gc_warp(g,gc_param->fighter_scale); Fighter_UpdateModelScale(g);
    fp->cur_pos.x = fp->cur_pos.z = 0; fp->cur_pos.y = 100;
    ftCo_Fall_Enter(g); ftColl_8007B7A4(g,30); gc_warp(g,1);
}
static void gc_fighter_control(HSD_GObj* g, GcPaddle* paddle, f32 facing)
{
    Fighter* fp = HSD_GObjGetUserData(g);
    gc_warp(g,1); fp->facing_dir = facing;
    ftCommon_8007E2FC(g); ftCo_Fall_Enter(g); ftColl_8007B0C0(g,2);
    fp->anim_cb = fp->input_cb = fp->phys_cb = fp->coll_cb = NULL;
    PM(fp)->paddle = paddle; PM(fp)->warped = 0;
    paddle->fighter = g; fp->accessory4_cb = gc_fighter_accessory;
    gc_volume = 127;
}
static MU_NO_CONTRACT void gc_fighter_accessory(HSD_GObj* g)
{
    Fighter* fp = HSD_GObjGetUserData(g);
    HSD_JObj* j = g->hsd_obj;
    GcPaddleMotion* motion = PM(fp);
    f32 scale = gc_param->fighter_scale * fp->co_attrs.model_scaling;
    Vec3 offset;
    j->scale.x = j->scale.y = j->scale.z = scale;
    offset.x = fp->facing_dir * -40;
    offset.y = -25 - ((((fp->coll_data.desired_ecb.top.y - fp->coll_data.desired_ecb.bottom.y) * .5f) * 3) * fp->co_attrs.model_scaling);
    offset.z = 0;
    lb_8000B1CC(motion->paddle->joint, &offset, &fp->cur_pos);
    j->translate = fp->cur_pos;
    if (!motion->warped) { motion->warped = 1; gc_warp(g,gc_param->fighter_scale); }
    HSD_JObjSetMtxDirtySub(j);
}
static MU_NO_CONTRACT void gc_paddle_cpu(GcPaddle* p, GcBall* ball)
{
    f32 delta = ball->x - p->x;
    f32 target, current, distance;
    p->buttons = 0;
    if ((delta > 0 && ball->vx > 0) || (delta <= 0 && ball->vx <= 0)) return;
    if (p->cpu_timer == 0) {
        p->cpu_offset = HSD_Randf() * 50 - 25;
        p->cpu_error = HSD_Randf() * 50;
        p->cpu_timer = HSD_Randi(120) + 10;
    } else --p->cpu_timer;
    target = ball->y + p->cpu_offset; current = p->y - p->cpu_error;
    distance = current - target;
    if (distance < 0) distance = -distance;
    if (distance < 10) return;
    if (target > current) p->buttons |= 0x10000;
    else if (target < current) p->buttons |= 0x20000;
}
static MU_NO_CONTRACT void gc_paddle_input(GcPaddle* p, GcBall* ball, f32 top, f32 bottom)
{
    f32 limit;
    bool fast;
    if (p->fighter) {
        Fighter* fp = HSD_GObjGetUserData(p->fighter);
        p->buttons = fp->input.held_buttons[0];
    } else gc_paddle_cpu(p,ball);
    fast = (p->buttons & 0x200) != 0;
    if (p->buttons & 0x20000) p->velocity -= fast ? gc_param->paddle_fast_accel : gc_param->paddle_accel;
    else if (p->buttons & 0x10000) p->velocity += fast ? gc_param->paddle_fast_accel : gc_param->paddle_accel;
    else if (p->velocity > 0) {
        p->velocity -= fast ? gc_param->paddle_fast_friction : gc_param->paddle_friction;
        if (p->velocity < 0) p->velocity = 0;
    } else if (p->velocity < 0) {
        p->velocity += fast ? gc_param->paddle_fast_friction : gc_param->paddle_friction;
        if (p->velocity > 0) p->velocity = 0;
    }
    limit = fast ? gc_param->paddle_fast_max : gc_param->paddle_max;
    if (p->velocity > limit) p->velocity = limit;
    if (p->velocity < -limit) p->velocity = -limit;
    p->y += p->velocity; if (p->y > top) p->y = top;
    if (p->y - p->size < bottom) p->y = p->size + bottom;
}
static void gc_increment(GcPaddle* p)
{
    u8 score = p->score + 1;
    if (score > 9) score = 9;
    p->score = score; gc_score(p->score_joint,p->score); gc_sound(12,gc_volume,64);
}
static MU_NO_CONTRACT void gc_ball_bounce(GcBall* ball, GcPaddle* paddle)
{
    f32 half = paddle->size * .5f;
    f32 distance = (ball->y - (paddle->y - half)) / half;
    f32 vy, vx, norm, random;
    ball->vx = -ball->vx;
    ball->vy = (distance * gc_param->bounce_mul) * gc_param->bounce_factor;
    random = HSD_Randf();
    vy = (((random + random) - 1) * gc_param->ball_noise) + ball->vy;
    /* +8258 draws once, doubles that result before subtracting 1. */
    vx = ball->vx;
    norm = sqrtf(vx * vx + vy * vy);
    ball->vx = (vx / norm) * ball->speed;
    ball->vy = (vy / norm) * ball->speed;
    ball->speed += gc_param->ball_accel;
    if (ball->speed > gc_param->ball_max) ball->speed = gc_param->ball_max;
    gc_sound(10,gc_volume,64);
}
static MU_NO_CONTRACT void gc_ball_init(HSD_GObj* g)
{
    GcBall* ball = &TV(g)->ball;
    f32 angle = (f32)((double)(HSD_Randi(90) - 45) * 0.017453292519943295);
    int direction = HSD_Randi(2) == 1 ? 1 : -1;
    ball->speed = gc_param->ball_speed;
    ball->vx = ((f32)direction * ball->speed) * cosf(angle);
    ball->vy = ball->speed * sinf(angle);
    ball->x = ball->y = 0;
}
static MU_NO_CONTRACT void gc_tv_create(HSD_GObj* g)
{
    GcTV* s = TV(g);
    HSD_JObj* left_score = gc_joint(g,11);
    HSD_JObj* right_score = gc_joint(g,12);
    Vec3 pos, scale = {.5,.5,.5};
    s->top = gc_joint(g,7)->translate.y; s->bottom = gc_joint(g,8)->translate.y;
    s->min_x = gc_joint(g,9)->translate.x; s->max_x = gc_joint(g,10)->translate.x;
    s->ball.x = s->ball.y = 0; s->ball.radius = 5;
    s->ball.joint = gc_joint(g,2); s->ball.speed = s->ball.vx = s->ball.vy = 0;
    s->left.joint = gc_joint(g,3); s->left.size = 50;
    s->left.x = s->left.joint->translate.x; s->left.y = s->left.joint->translate.y;
    s->left.buttons = 0; s->left.score = 0;
    s->left.score_joint = HSD_JObjLoadJoint(DP(gc_param->score_joint));
    HSD_JObjAddChild(left_score,s->left.score_joint); gc_score(s->left.score_joint,0);
    s->right.joint = gc_joint(g,5); s->right.size = 50;
    s->right.x = s->right.joint->translate.x; s->right.y = s->right.joint->translate.y;
    s->right.buttons = 0; s->right.score = 0;
    s->right.score_joint = HSD_JObjLoadJoint(DP(gc_param->score_joint));
    HSD_JObjAddChild(right_score,s->right.score_joint); gc_score(s->right.score_joint,0);
    s->state = 2; s->timer = gc_param->pong_enter;
    lb_8000B1CC(gc_joint(g,1),NULL,&pos);
    gc_text_create("Ready!",0,&pos,&scale,-1,180,NULL);
    gc_volume = 64;
}
static void gc_tv_frame(HSD_GObj* g)
{
    GcTV* s = TV(g);
    int i;
    if (s->state == 0) {
        if (s->timer == 0) {
            gc_ball_init(g); gc_score_hide(s->left.score_joint); gc_score_hide(s->right.score_joint);
        }
        --s->timer;
        gc_tv_process(g);
        if (s->state == 1) return;
    } else if (s->state == 1) {
        if (s->timer != 0) { --s->timer; return; }
        if (s->right.fighter) {
            if (s->right.winner & 0x80) gc_fighter_return(s->right.fighter);
            else ftCo_800D40B8(s->right.fighter);
        }
        if (s->left.fighter) {
            if (s->left.winner & 0x80) gc_fighter_return(s->left.fighter);
            else ftCo_800D40B8(s->left.fighter);
        }
        fn_801650E8(); Ground_801C4A08(g);
        return;
    } else if (s->state == 2) {
        if (s->timer == 0) { s->state = 0; s->timer = 60; }
        else {
            for (i = 0; i < 6; ++i) {
                HSD_GObj* f;
                Fighter* fp;
                if (Player_GetPlayerSlotType(i) == 3 || !(f = Player_GetEntity(i))) continue;
                fp = HSD_GObjGetUserData(f);
                if (fp->motion_id != 14 || (fp->input.held_buttons[0] & 4) || fp->x682 > gc_param->catch_window) continue;
                if (!s->left.fighter) gc_fighter_control(f,&s->left,1);
                else if (!s->right.fighter) gc_fighter_control(f,&s->right,-1);
            }
            --s->timer;
            if (s->state == 1) return;
        }
    }
    if (s->left.fighter || s->right.fighter) gm_80165290(1);
}
static MU_NO_CONTRACT void gc_tv_process(HSD_GObj* g)
{
    GcTV* s = TV(g);
    GcBall* b = &s->ball;
    GcPaddle* l = &s->left;
    GcPaddle* r = &s->right;
    b->x += b->vx; b->y += b->vy;
    gc_paddle_input(l,b,s->top,s->bottom); gc_paddle_input(r,b,s->top,s->bottom);
    if (b->radius + b->y > s->top) {
        b->y = s->top - b->radius; b->vy = -b->vy; gc_sound(11,gc_volume,64);
    }
    if (b->y - b->radius < s->bottom) {
        b->y = s->bottom + b->radius; b->vy = -b->vy; gc_sound(11,gc_volume,64);
    }
    if (b->x - b->radius < l->x && !(l->x > (b->x - b->radius) - b->vx) &&
        b->y - b->radius < l->y && !(b->radius + b->y < l->y - l->size)) {
        b->x = l->x + b->radius; gc_ball_bounce(b,l);
    }
    if (b->radius + b->x > r->x && !(r->x < (b->radius + b->x) - b->vx) &&
        b->y - b->radius < r->y && !(b->radius + b->y < r->y - r->size)) {
        b->x = r->x - b->radius; gc_ball_bounce(b,r);
    }
    if (b->x < s->min_x) {
        /* +3360 scores right, then redundantly refreshes the left score. */
        gc_increment(r); gc_score(l->score_joint,l->score);
        b->x = b->y = b->speed = b->vx = b->vy = 0; s->timer = 120;
    }
    if (b->x > s->max_x) {
        gc_score(r->score_joint,r->score); gc_increment(l);
        b->x = b->y = b->speed = b->vx = b->vy = 0; s->timer = 120;
    }
    b->joint->translate.x = b->x; b->joint->translate.y = b->y;
    l->joint->translate.x = l->x; l->joint->translate.y = l->y;
    r->joint->translate.x = r->x; r->joint->translate.y = r->y;
    HSD_JObjSetMtxDirtySub(g->hsd_obj);
    if (l->score >= gc_param->pong_win_score || r->score >= gc_param->pong_win_score) {
        HSD_GObj* winner;
        const char* name;
        Vec3 offset = gc_vec(gc_param->winner_offset), pos, namepos, scale = {.5,.5,.5};
        s->state = 1; s->timer = 180; HSD_JObjSetFlags(b->joint,16);
        l->winner = (l->winner & 0x7F) | (l->score >= gc_param->pong_win_score ? 0x80 : 0);
        r->winner = (r->winner & 0x7F) | (r->score >= gc_param->pong_win_score ? 0x80 : 0);
        lb_8000B1CC(gc_joint(g,1),&offset,&pos); namepos = pos; namepos.y -= 50;
        if (l->score >= gc_param->pong_win_score) { winner = l->fighter; name = "Left"; }
        else { winner = r->fighter; name = "Right"; }
        if (winner) {
            Fighter* fp = HSD_GObjGetUserData(winner);
            name = mu_gr_mex_fighter_name(Player_GetPlayerCharacter(fp->player_idx));
        }
        gc_text_create("Winner!",1,&pos,&scale,-1,180,NULL);
        gc_text_create(name,1,&namepos,&scale,-1,180,NULL);
    }
}

/* +16EC,+8534,+8620,+8684: single save cube viewer. */
static void gc_view_create(HSD_GObj* g)
{
    GcView* s = VIEW(g);
    HSD_JObj* anchor;
    Vec3 scale;
    gc_anim(g,0); gc_save_index = 0; gc_shuffle();
    s->state = 0; s->timer = 60; s->save = gc_spawn_save();
    anchor = gc_joint(g,1); lb_8000C2F8(gc_joint(s->save,0),anchor);
    scale = anchor->scale; gc_save_scale(s->save,&scale);
}
static void gc_view_frame(HSD_GObj* g)
{
    GcView* s = VIEW(g);
    if (s->state == 0) {
        if (s->timer >= 0 && --s->timer == 0) {
            Vec3 pos = {0,0,0}, scale = {1,1,1};
            gc_save_state(s->save,1); s->timer = gc_param->view_time; s->state = 1;
            /* +85A0 reads Ground.gobj as a string, not the save comment.
             * Jev question: native pointer bytes or bounded console header?
             * Answer: reconstruct the console header. Its classifier's first
             * byte is zero, so the original produces an empty string here. */
            gc_text_create("",0,&pos,&scale,-1,gc_param->view_time,NULL);
        }
    } else if (s->state == 1) {
        if (s->timer >= 0 && --s->timer == 0) {
            gc_save_state(s->save,2); s->timer = 120; s->state = 2;
        }
    } else if (s->state == 2 && s->timer > 0 && --s->timer == 0) Ground_801C4A08(g);
}
/* +17E4 .. +19BC: background group with periodic lines of cubes. */
static void gc_cubes_create(HSD_GObj* g)
{
    GcCubes* s = CUBES(g);
    s->state = 0;
    s->timer = gc_param->cube_gap_min + HSD_Randi(gc_param->cube_gap_max - gc_param->cube_gap_min);
    gc_anim(g,0);
}
static void gc_cubes_frame(HSD_GObj* g)
{
    GcCubes* s = CUBES(g);
    if (s->state == 0) {
        if (grAnime_801C83D0(g,2,7)) { s->state = 1; gc_anim(g,1); }
    } else if (s->state == 1) {
        if (grAnime_801C83D0(g,2,7)) { s->state = 2; gc_anim(g,2); }
        else if (s->timer > 0) --s->timer;
        else {
            s->timer = gc_param->cube_gap_min + HSD_Randi(gc_param->cube_gap_max - gc_param->cube_gap_min);
            mu_grTSeak_80223908(11);
        }
    } else if (s->state == 2 && grAnime_801C83D0(g,2,7)) Ground_801C4A08(g);
}
static MU_NO_CONTRACT void gc_line_create(HSD_GObj* g)
{
    f32 rate;
    gc_anim(g,HSD_Randi(4));
    rate = (gc_param->line_rate_max - gc_param->line_rate_min) * HSD_Randf();
    rate += gc_param->line_rate_min;
    gc_rate(g->hsd_obj,rate);
}
static void gc_line_frame(HSD_GObj* g)
{
    if (grAnime_801C83D0(g,2,7)) Ground_801C4A08(g);
}
static bool gc_force_game(void)
{
    int i;
    bool forced = true;
    for (i = 0; i < 6; ++i) {
        HSD_GObj* f;
        Fighter* fp;
        if (Player_GetPlayerSlotType(i) != 0) continue;
        f = Player_GetEntity(i);
        if (!f) { forced = false; continue; }
        fp = HSD_GObjGetUserData(f);
        if (fp->input.held_buttons[0] & 4) { forced = false; continue; }
        forced = false;
        /* The source stops immediately at the first existing human without
         * this button, rather than considering the other human slots. */
        break;
    }
    return forced;
}
static int gc_decide_event(int last)
{
    static const int ids[5] = {5,10,7,6,8};
    static const int weights[5] = {45,20,15,15,5};
    int sum = 0, i, pick, seen = 0;
    if (gc_force_game()) return 8;
    for (i = 0; i < 5; ++i) if (ids[i] != last) sum += weights[i];
    pick = HSD_Randi(sum);
    for (i = 0; i < 5; ++i) {
        if (ids[i] == last) continue;
        seen += weights[i]; if (pick < seen) return ids[i];
    }
    return 8;
}
static void gc_event_end(HSD_GObj* g)
{
    GcBackground* s = BG(Ground_GetMapGObj(1));
    (void)g;
    s->event = NULL; s->timer = 240;
}
static void gc_background_create(HSD_GObj* g)
{
    BG(g)->timer = 240; BG(g)->last = -1; gc_anim(g,0);
}
static void gc_background_frame(HSD_GObj* g)
{
    GcBackground* s = BG(g);
    int i, event = 8;
    if (!s->event) {
        if (s->timer > 0) --s->timer;
        else {
            for (i = 0; i < 6; ++i) {
                HSD_GObj* f;
                Fighter* fp;
                if (Player_GetPlayerSlotType(i) == 3 || !(f = Player_GetEntity(i))) continue;
                fp = HSD_GObjGetUserData(f);
                if (fp->motion_id == 14 && (fp->input.held_buttons[0] & 4)) continue;
                event = gc_decide_event(s->last); break;
            }
            s->event = mu_grTSeak_80223908(event); s->last = event;
            ((Ground*) HSD_GObjGetUserData(s->event))->x1C_callback = gc_event_end;
        }
    }
    lb_800115F4();
}
static void gc_floor_create(HSD_GObj* g)
{
    Ground* gp = HSD_GObjGetUserData(g);
    int animation = 0, i, sum = 0, pick, seen = 0;
    u32 buttons = HSD_PadMasterStatus[0].button;
    gp->x10_flags.b5 = 1;
    if (buttons & 8) animation = 0;
    else if (buttons & 2) animation = 1;
    else if (buttons & 4) animation = 2;
    else if (buttons & 1) animation = 3;
    else {
        GcWeight* weights = DP(gc_param->floor_weights);
        for (i = 0; i < gc_param->floor_count; ++i) sum += weights[i].weight;
        pick = HSD_Randi(sum);
        for (i = 0; i < gc_param->floor_count; ++i) {
            seen += weights[i].weight;
            if (pick < seen) { animation = weights[i].id; break; }
        }
    }
    grAnime_801C8138(g,gp->map_id,animation);
}
static void gc_hud_hide(HSD_GObj* g)
{
    int i;
    for (i = 0; i < 8; ++i) {
        if (ifStatus_803F9628[i].x0) HSD_JObjSetFlagsAll(ifStatus_803F9628[i].x0->hsd_obj,16);
    }
    HSD_GObjFree(g);
}
static void gc_load(void)
{
    HSD_GObj* g = GObj_Create(14,14,0);
    HSD_GObj_SetupProc(g,gc_hud_hide,0);
    /* +033C sets 0x00100000 in the first StartMeleeRules word. */
    gm_GetStartMeleeRules()->x1_3 = 1;
}
static void gc_bios_callback(HSD_GObj* g) { (void)g; }
static void gc_go(void)
{
    Vec3 pos = gc_vec(gc_param->text_pos), scale = gc_vec(gc_param->text_scale);
    lbAudioAx_800237A8(510039,127,64);
    gc_text_create("GO!",0,&pos,&scale,-1,30,gc_bios_callback);
}
static void gc_init(void)
{
    HSD_Archive* archive = grDatFiles_801C6330(0)->unk0;
    Vec3 pos, scale;
    gc_param = mexgr_gc_yakumono("gamecube.c",0x294);
    memset(gc_icons,0,sizeof gc_icons); memset(gc_queue,0,sizeof gc_queue);
    gc_volume = 127;
    gc_font = HSD_ArchiveGetPublicAddress(archive,"bitfont");
    if (!gc_font) __assert("gamecube.c",0x299,"bitfont not found");
    DISC_SET(gc_param->cube_joint,HSD_ArchiveGetPublicAddress(archive,"cube_joint"));
    DISC_SET(gc_param->cube_enter,HSD_ArchiveGetPublicAddress(archive,"cube_enter_animjoint"));
    DISC_SET(gc_param->cube_enter_mat,HSD_ArchiveGetPublicAddress(archive,"cube_enter_matanim_joint"));
    DISC_SET(gc_param->cube_exit,HSD_ArchiveGetPublicAddress(archive,"cube_exit_animjoint"));
    DISC_SET(gc_param->cube_exit_mat,HSD_ArchiveGetPublicAddress(archive,"cube_exit_matanim_joint"));
    DISC_SET(gc_param->score_joint,HSD_ArchiveGetPublicAddress(archive,"colortv_score_joint"));
    mu_grTSeak_80223908(0); mu_grTSeak_80223908(1); mu_grTSeak_80223908(2);
    gc_load_saves();
    pos = gc_vec(gc_param->text_pos); scale = gc_vec(gc_param->text_scale);
    gc_text_create("Ready!",0,&pos,&scale,-1,30,NULL);
    Ground_801C39C0(); Ground_801C3BB4();
}
static bool gc_shadow(Vec3* pos, int arg, HSD_JObj* jobj)
{
    Vec3 joint_pos;
    (void)arg;
    lb_8000B1CC(jobj,NULL,&joint_pos);
    return pos->y > joint_pos.y;
}
static StageCallbacks gc_maps[] = {
    {mexgr_gc_anim0_create,mexgr_gc_unk,mu_gr_gobj_nop,mexgr_gc_deletion,0},
    {gc_background_create,mexgr_gc_unk,gc_background_frame,mexgr_gc_deletion,0x80000000},
    {gc_floor_create,mexgr_gc_unk,mu_gr_gobj_nop,mexgr_gc_deletion,0},
    {gc_save_create,mexgr_gc_false,gc_save_frame,gc_save_delete,0},
    {gc_text_create_map,gc_text_callback,gc_text_frame,mexgr_gc_deletion,0},
    {gc_save_event_create,mexgr_gc_false,gc_save_event_frame,mu_gr_gobj_nop,0},
    {gc_clock_create,mexgr_gc_false,gc_clock_frame,mu_gr_gobj_nop,0},
    {gc_bios_create,mexgr_gc_false,gc_bios_frame,mu_gr_gobj_nop,0},
    {gc_tv_create,mexgr_gc_false,gc_tv_frame,mu_gr_gobj_nop,0},
    {gc_view_create,mexgr_gc_false,gc_view_frame,mu_gr_gobj_nop,0},
    {gc_cubes_create,mexgr_gc_false,gc_cubes_frame,mu_gr_gobj_nop,0},
    {gc_line_create,mexgr_gc_false,gc_line_frame,mu_gr_gobj_nop,0},
};
const MuGrStage mu_gr_gamecube = {
    .name = "Gamecube", .file = "/GrGc.dat", .map_gobjs = gc_maps,
    .on_init = gc_init, .on_demo_init = mu_gr_demo_nop, .on_load = gc_load,
    .on_start = gc_go, .callback4 = mu_gr_false, .on_touch_line = mu_gr_no_touch_line,
    .on_check_shadow_render = gc_shadow, .flags = MU_GR_READY,
};
