/* Shared code of the WP1 stages (run-source/rel09-stages WORKPACKAGES.md, package 1): Peach's
 * Castle 64, Hyrule Castle 64, Saffron City 64, Mushroom Kingdom 64, Targets!Dedede and
 * Targets!Tails. Only these stages include this header (mexstages/<stage>/ of package 1).
 *
 * What the blocks share, read from their disassemblies (`py -3.12 grdis.py ak <File>.dat`):
 *
 *   - The map tables of the four 64 stages with items (GrOPc, GrOSf, GrOMk; Planet Zebes 64 is
 *     package 6) name Sheik's target stage callbacks (gr/grtseak.c) by raw retail address, words the
 *     block does not relocate, so on the console those slots run the retail code. The functions are
 *     static in the decomp: below is one native copy of each body the tables name, the same calls in
 *     the same order.
 *   - Background_Create (23 instructions, the same in the four 64 stages).
 *   - on_init (17 instructions): grTSeak_80223908(0 .. n - 1), then the camera range and the blast
 *     zone (Ground_801C39C0, Ground_801C3BB4).
 *   - Two first animation object searches over a joint tree: Saffron City 64's doors use the one
 *     that looks at the joints only (GrOSf.dat +610), Hyrule Castle 64's tornado item the MexTK one
 *     that also looks into each joint's display, material and texture objects
 *     (.text.JOBJ_GetFirstAOBJ, GrOHy.dat item 0 +5FC).
 *
 * m-ex runtime calls in these blocks and what serves them natively:
 *   - 803D708C (the stage item kind of item `index`): mu_gr_item_kind (shim/mu_mex_stage.h).
 *   - 803D7078 (a sound of the stage's own bank): mu_gr_mex_sfx (common/mu_gr_mex.h).
 *   - 8026862C (Item_8026862C): mu_ak_item_create (akaneia/mu_ak_fighter.h).
 *   - `bl __init_registers+18` (Saffron City 64 items 0, 1, 7; Mushroom Kingdom 64 +4A0 and item 0
 *     +B8, +31C): 80005358 is the blr that ends __init_registers, the address MexTK links its
 *     debugger breakpoint bp() to. The call returns at once and changes no register the code reads
 *     after it: nothing to do natively, each site says so. */
#ifndef MEXGR_WP1_COMMON_H
#define MEXGR_WP1_COMMON_H

#include "../common/mu_gr_common.h"
#include "../common/mu_gr_mex.h"

#include <melee/gr/forward.h>
#include <sysdolphin/baselib/forward.h>

/* Sheik's target stage callbacks, by the retail address the map tables name. */
void mexgr_wp1_tseak_anim(Ground_GObj* gobj);   /* 802239F0 stageGObj0_OnInit: animation set 0 */
void mexgr_wp1_tseak_coll(Ground_GObj* gobj);   /* 80223B14 stageGObj1_GObjProc: collision sync */
bool mexgr_wp1_tseak_false(Ground_GObj* gobj);  /* 80223A1C, 80223A7C, 80223B0C: return false */
void mexgr_wp1_tseak_nop(Ground_GObj* gobj);    /* 80223A24, 80223A28, 80223AB8, 80223B34 */

/* Background_Create (GrOPc.dat +7C, GrOSf.dat +AC, GrOMk.dat +98): animation set 0, Ground x11
 * bits 0 to 2 = 2 (the half word at +0x10 masked with ~0xE0 and or'ed with 0x40), the material
 * flags 0x28000000 on the map gobj's joint. */
void mexgr_wp1_background_create(Ground_GObj* gobj);

/* on_init of the 64 stages: map gobjs 0 .. count - 1 through the m-ex map gobj helper, then the
 * camera range and the blast zone. */
void mexgr_wp1_init(int count);

/* GrOSf.dat +610: the first animation object of a joint tree, depth first (the joint, its
 * children, then its next siblings), NULL when none. */
HSD_AObj* mexgr_wp1_first_aobj(HSD_JObj* jobj);

/* GrOHy.dat item 0 +5FC (.text.JOBJ_GetFirstAOBJ): the same walk, and at each joint, before its
 * children, the animation objects of its display objects, of each display object's material and of
 * that material's textures. The material is not checked for NULL (a display object always has
 * one in these files; the console would read address 0x14 otherwise). */
HSD_AObj* mexgr_wp1_first_aobj_all(HSD_JObj* jobj);

#endif
