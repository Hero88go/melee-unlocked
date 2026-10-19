/* The "Akaneia family" helpers of WP4 (run-source/rel09-stages WORKPACKAGES.md, PLAN.md section 2).
 *
 * Green Hill Zone (GrGh.dat) and Luigi's Mansion (GrLm.dat) come from one author's template, which
 * Village, Delfino Plaza, Fichina, Dolphin Park, Gamecube and Boxing Ring share too: the yakumono
 * param fetch with its assert, the empty exports, the map gobj slots that are a lone blr or
 * "li r3, 0; blr", Points_OnCreation, Background_OnCreation and the stage hurtbox item
 * (grMaterial_801C8CFC, _801C8DE0 and the word Item_UpdateHurtbox, 801C8E08). This is WP4's copy,
 * included by greenhill/ and luigismansion/ only. Every symbol carries the prefix mexgr_wp4_ so it
 * cannot collide with the copies of WP3, WP5 and WP6; the integration owner can merge the copies
 * into mexstages/common/ later (same bodies, offsets given at each one). */
#ifndef MEXGR_WP4_AKFAMILY_LIB_H
#define MEXGR_WP4_AKFAMILY_LIB_H

#include "../common/mu_gr_common.h"
#include "../common/mu_gr_mex.h"

#include <melee/gr/forward.h>
#include <melee/it/forward.h>
#include <sysdolphin/baselib/forward.h>

/* OnInit+0 .. +18 and the assert at its end: stage_info.yakumono_param, and when the file has none,
 * __assert(file, line, "yakumono not found") as the block does (the console stops there). */
void* mexgr_wp4_yakumono(const char* file, int line);

/* Map gobj slot 1 (OnUnk, the decomp's callback1, never called): a lone blr, so it returns its
 * argument, which is never zero. */
bool mexgr_wp4_unk(Ground_GObj* gobj);

/* Map gobj slot 3 (OnDeletion, the decomp's callback3, run on destroy): "li r3, 0; blr". */
void mexgr_wp4_deletion(Ground_GObj* gobj);

/* Points_OnCreation (GrGh +1C4, GrLm +19C) and the other "animation set 0" creators. */
void mexgr_wp4_anim0_create(Ground_GObj* gobj);

/* Background_OnCreation (GrGh +200, GrLm +1D8): animation set 0, then x10 bit 2 off (the
 * halfword at +10 with rlwinm 0, 0x13, 0x11). */
void mexgr_wp4_background_create(Ground_GObj* gobj);

/* The stage hurtbox item: grMaterial_801C8CFC(0, 0, gp, jobj, NULL, on_hurt, NULL), its capsule
 * (a, b, scale) through grMaterial_801C8DE0, then grMaterial_801C8E08 (the block calls that one
 * through its data word Item_UpdateHurtbox = 801C8E08). Returns the item gobj, unchecked as on the
 * console. */
Item_GObj* mexgr_wp4_hurtbox(Ground* gp, HSD_JObj* jobj,
                             void (*on_hurt)(Item_GObj*, Ground*, Vec3*, HSD_GObj*, f32),
                             float ax, float ay, float az, float bx, float by, float bz,
                             float scale);

/* The block's own private variables live in Ground.u (console offsets from +C4, cleared by
 * Ground_GetStageGObj in the mod view). A stage's struct is checked against the union's size. */
#define MEXGR_WP4_VARS(type, gp) ((type*) (void*) &(gp)->u)
#define MEXGR_WP4_VARS_FIT(type) _Static_assert(sizeof(type) <= sizeof(((Ground*) 0)->u), #type " fits Ground.u")

#endif
