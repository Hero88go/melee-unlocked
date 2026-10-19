/* GrSb exports 1, 3 and 6. Five map gobjs, two catch-and-launch barrels.
 * +7A8 squares the single-precision differences in double precision before sqrtf.
 * +35C tests the timer before decrement, so the catch wait includes its zero frame. */
#include "../common/mu_gr_common.h"
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdevice.h>
#include <melee/ft/kinds/ftCommon/ftCo_Damage.h>
#include <melee/ft/kinds/ftCommon/ftCo_Lift.h>
#include <melee/ft/kinds/ftCommon/ftCo_Throw.h>
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdisplay.h>
#include <melee/gr/grmaterial.h>
#include <melee/gr/ground.h>
#include <melee/gr/grzakogenerator.h>
#include <melee/it/it_26B1.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/lb/lbcollision.h>
#include <melee/mp/mpcoll.h>
#include <sysdolphin/baselib/gobj.h>
#include <dolphin/os.h>
#include <math.h>

typedef struct BarrelVars { int state; Vec3 pos; int timer; Fighter_GObj* fighter; } BarrelVars;
typedef struct BarrelFighter { Ground_GObj* barrel; } BarrelFighter;
extern Camera game_camera;
/* +5B8/+3FC pass the gobj where Ground_801C5440 expects Ground*. Its x20[0] aliases
 * the high word of gobj gxlink_prios on console. Preserve that otherwise-unused word by name. */
static void barrel_sound(Ground_GObj* gobj, unsigned int sound)
{
    Ground sound_state = { 0 };
    sound_state.x20[0] = (int) (gobj->gxlink_prios >> 32);
    Ground_801C5440(&sound_state, 0, sound);
    gobj->gxlink_prios = ((u64) (u32) sound_state.x20[0] << 32) | (u32) gobj->gxlink_prios;
}
static BarrelVars* barrel_vars(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    return (BarrelVars*) &gp->u;
}
static Ground_GObj* fighter_barrel(Fighter* fp) { return ((BarrelFighter*) &fp->mv)->barrel; }
static bool barrel_false(Ground_GObj* gobj) { (void) gobj; return false; }
static void barrel_change(Ground_GObj* gobj, int state)
{
    static const char* names[3] = { "BARREL_WAIT", "BARREL_CATCH", "BARREL_SHOOT" };
    Ground* gp = HSD_GObjGetUserData(gobj);
    barrel_vars(gobj)->state = state;
    grAnime_801C8138(gobj, gp->map_id, state);
    OSReport("barrel state changing to %s", names[state]);
}
static void barrel_death(Fighter_GObj* fighter)
{
    Fighter* fp = HSD_GObjGetUserData(fighter);
    barrel_change(fighter_barrel(fp), 0);
}
static void barrel_accessory(Fighter_GObj* fighter)
{
    Fighter* fp = HSD_GObjGetUserData(fighter);
    fp->cur_pos = barrel_vars(fighter_barrel(fp))->pos;
}
static void barrel_input(Fighter_GObj* fighter) { (void) fighter; }
static void barrel_enter(Fighter_GObj* fighter, Ground_GObj* barrel)
{
    Fighter* fp = HSD_GObjGetUserData(fighter);
    CmSubject* cam;
    ftCommon_8007DB58(fighter); ftCo_8009750C(fighter); ftCo_800DD168(fighter);
    ftCommon_8007D5D4(fp);
    Fighter_ChangeMotionState(fighter, 0x125, 0, 0.0F, 1.0F, 0.0F, NULL);
    ftAnim_8006EBA4(fighter);
    fp->death2_cb = barrel_death; fp->accessory1_cb = barrel_accessory; fp->input_cb = barrel_input;
    ftCommon_8007E2FC(fighter);
    fp->x1988 = 2; fp->invisible = 1; fp->x1A6A = 0x1FF;
    cam = fp->x890_cameraBox;
    cam->target_ext.h.x = -20; cam->target_ext.h.y = 20;
    cam->target_ext.v.x = 20; cam->target_ext.v.y = -20; cam->target_ext.v.z = 0;
    fp->x209A = 1;
    ((BarrelFighter*) &fp->mv)->barrel = barrel;
}
static lbColl_80008D30_arg1 barrel_hit = { 1, 0, 361, 0, 0, 180, 0, 0, 0 };
static void barrel_shoot(Fighter_GObj* fighter, Vec3* pos, float angle)
{
    Fighter* fp = HSD_GObjGetUserData(fighter);
    HitCapsule hit;
    fp->cur_pos = *pos;
    mpColl_80043680(&fp->coll_data, &fp->cur_pos);
    ftCamera_80076064(fp); ftColl_800788D4(fighter);
    if (angle < 0) angle += 360.0F;
    if (fp->facing_dir > 0) angle = 180.0F - angle;
    lbColl_80008D30(&hit, &barrel_hit);
    /* +6B0 leaves argument 3 unspecified. Fixed knockback makes it unread. Jev selected 0;
     * jev_questions.json and jev_answer.json record that native adaptation (confidence 0.97). */
    fp->dmg.kb_applied = ftColl_80079EA8(fp, &hit, 0);
    fp->dmg.x1848_kb_angle = (int) angle; fp->dmg.facing_dir_1 = fp->facing_dir;
    fp->dmg.x184c_damaged_hurtbox = 0; fp->dmg.x1854_collpos = fp->cur_pos;
    fp->dmg.x1860_element = barrel_hit.element;
    Fighter_TakeDamage_8006CC7C(fp, (float) barrel_hit.damage);
    ftCo_8008DCE0(fighter, 0x5B, 0.0F);
    fp->mv.co.damage.x1A = 0;
}
MU_NO_CONTRACT static bool barrel_grab(Ground_GObj* gobj, Fighter_GObj* fighter)
{
    BarrelVars* v = barrel_vars(gobj);
    Fighter* fp;
    Vec3 pos;
    float dx, dy;
    double distance;
    if (v->state != 0) return false;
    fp = HSD_GObjGetUserData(fighter);
    it_8026BB68(fighter, &pos);
    dx = v->pos.x - pos.x; dy = v->pos.y - pos.y;
    distance = (double) dx * (double) dx;
    distance += (double) dy * (double) dy;
    if (sqrtf((float) distance) < 12.0F) {
        barrel_change(gobj, 1); v->timer = 30; v->fighter = fighter;
        /* The script is NULL; the unspecified overlay priority is never used by that script. */
        grMaterial_801C9604(gobj, NULL, false);
        efSync_Spawn(0x405, gobj, &fp->cur_pos);
        mu_grLast_8021B2D8(gobj);
        barrel_sound(gobj, 0x129);
        barrel_enter(fighter, gobj);
    }
    return false;
}
static void barrel_frame(Ground_GObj* gobj)
{
    BarrelVars* v = barrel_vars(gobj);
    lb_8000B1CC(Ground_801C3FA4(gobj, 1), NULL, &v->pos);
    if (v->state == 1 && grAnime_801C83D0(gobj, false, 7)) {
        int old = v->timer--;
        if (old == 0) { barrel_change(gobj, 2); v->timer = 18; grMaterial_801C95C4(gobj); }
    } else if (v->state == 2) {
        if (v->timer > 0 && --v->timer == 0) {
            barrel_shoot(v->fighter, &v->pos, 270.0F);
            barrel_sound(gobj, 0x12A);
        }
        if (grAnime_801C83D0(gobj, false, 7)) barrel_change(gobj, 0);
    }
}
static void barrel_draw(Ground_GObj* gobj, intptr_t pass)
{
    static GXColor diffuse = { 255, 255, 255, 50 }, ambient = { 128, 0, 0, 50 };
    grDisplay_801C5DB0(gobj, pass);
    if (game_camera.x399_b4 && pass == 2) {
        Vec3 pos = barrel_vars(gobj)->pos;
        lbColl_80008FC8(pos, pos, &diffuse, &ambient, 12.0F);
    }
}
static void barrel_create(Ground_GObj* gobj)
{
    barrel_change(gobj, 0);
    grMaterial_801C94D8(GET_JOBJ(gobj));
    lb_8000B1CC(Ground_801C3FA4(gobj, 1), NULL, &barrel_vars(gobj)->pos);
    gobj->render_cb = barrel_draw;
    ftCo_800C0764(gobj, 2, barrel_grab);
}
static void sb_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
}
static void sb_frame(Ground_GObj* gobj) { Ground_UpdateMapColl(gobj); }
static void sb_floor_frame(Ground_GObj* gobj) { Ground_UpdateMapColl(gobj); lb_800115F4(); }
#define SB_ROW(create, frame) { create, barrel_false, frame, mu_gr_gobj_nop, 0 }
static StageCallbacks sb_map[5] = {
    SB_ROW(sb_create, sb_frame), SB_ROW(sb_create, sb_frame), SB_ROW(sb_create, sb_floor_frame),
    SB_ROW(barrel_create, barrel_frame), SB_ROW(barrel_create, barrel_frame),
};
static void sb_init(void)
{
    int i;
    for (i = 0; i < 5; i++) mu_grTSeak_80223908(i);
    Ground_801C39C0(); Ground_801C3BB4();
}
static void sb_go(void) { grZakoGenerator_801CAE04(NULL); }
const MuGrStage mu_gr_smashketball = {
    .name = "Smashketball", .file = "GrSb.dat", .map_gobjs = sb_map,
    .on_init = sb_init, .on_start = sb_go, .flags = MU_GR_READY,
};
