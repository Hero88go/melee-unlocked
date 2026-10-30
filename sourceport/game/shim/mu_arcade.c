/* The recognised 2D-style mod build's own code, native (run-source/arcade-0980/STUDY.md).
 *
 * Its disc is an m-ex build: its costumes, stages, menus and m-ex tables load as files (mu_mex.c).
 * Its code list carries three codes of its own and its main.dol one data byte. The hitbox code is
 * the Arcade look's display switches (the host turns them on for the profile); the rest is here,
 * while its disc is the active profile (MU_TE2_ARCADE_PACK), offline and in replays recorded with it. */
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <melee/lb/lbcollision.h>

int mu_option2_offline(unsigned int bit);
unsigned int mu_display_options(void);
unsigned int mu_grid_color(void);
int mu_online_active(void);
int mu_online_pending(void);
int mu_widescreen(void);
float mu_picture_widen(void);

int mu_arcade_pack(void)
{
    return mu_option2_offline(MU_TE2_ARCADE_PACK);
}

/* The camera code below, for the 2D-style build's disc or for the Arcade look's "2D camera" on any
 * disc. The setting is display only: never online, where the host also leaves the bit off. */
int mu_arcade_camera(void)
{
    if (mu_arcade_pack())
        return 1;
    return (mu_display_options() & MU_DISPLAY_ARCADE_CAMERA) != 0 && !mu_online_active() && !mu_online_pending();
}

/* "Camera (Widescreen)", C2 at fn_800301D0+0x2C, after Camera_8002A4AC sets the match camera up for
 * the frame: the camera becomes orthographic, its box scaled by the eye's distance from the point it
 * looks at; the eye moves to z 1000 straight in front of that point (no tilt or pan); and the stage's
 * camera tracking values are replaced. The code's paired-single products are single precision, in
 * this order. The code comes in two forms that differ only in the box's half width: 0.07 of the
 * viewport for the widescreen code (Slippi's 16:9 code on), 0.05 for 4:3. True widescreen widens only
 * perspective cameras in the renderer, so this flat box is widened here by the same factor. */
void mu_arcade_pack_camera(HSD_GObj* gobj)
{
    HSD_CObj* cobj = gobj->hsd_obj;
    HSD_WObj* eye = cobj->eyepos;
    HSD_WObj* interest = cobj->interest;
    const f32 angle = stage_info.cam_info.cam_angle_left;
    const f32 distance = (eye->pos.z - interest->pos.z) + 10.0F;
    const f32 height = cobj->viewport.ymax, width = cobj->viewport.xmax;   /* CObj +0x18 and +0x10 */

    /* The flat projection itself only for the build's own disc. For the Arcade look's setting the
     * renderer flattens the picture (gx_shader.cpp build_projection), whose depth keeps the HUD
     * timer in view; the game's orthographic box put it out of range. */
    if (mu_arcade_pack()) {
        const f32 half_width = mu_widescreen() ? 0.07F : 0.05F * mu_picture_widen();
        cobj->projection_type = PROJ_ORTHO;
        cobj->projection_param.ortho.top = ((0.05F * height) / angle) * distance;
        cobj->projection_param.ortho.bottom = ((-0.064F * height) / angle) * distance;
        cobj->projection_param.ortho.left = ((-half_width * width) / angle) * distance;
        cobj->projection_param.ortho.right = ((half_width * width) / angle) * distance;
        eye->pos.z = 1000.0F;   /* far back: the box above is sized by the old distance */
    }
    eye->pos.x = interest->pos.x;
    eye->pos.y = interest->pos.y;
    stage_info.cam_info.cam_track_ratio = 1.25F;
    stage_info.cam_info.cam_fixed_zoom = 0.5F;
    stage_info.cam_info.cam_track_smooth = 0.66F;
    stage_info.cam_info.cam_zoom_rate = 125.0F;
}

/* Arcade look background "Grid": lines every 5 units on a plane 30 units behind the play plane (in
 * front of nothing the stage needs: the host has hidden the scenery further back), over the stage's
 * camera bounds and a margin. Each line is a thin rectangle drawn with the match camera (the game's own
 * debug rectangle, lbColl_80009DD4), before the stage, so the stage and the fighters cover it. */
void mu_arcade_grid(void)
{
    const StageBlastZone* b = &stage_info.cam_info.cam_bounds;
    const unsigned int rgb = mu_grid_color();
    GXColor color;
    Vec3 v0, v1;
    f32 x, y, left, right, top, bottom;
    if (!(mu_display_options() & MU_DISPLAY_GRID)) {
        return;
    }
    color.r = (u8) (rgb >> 16); color.g = (u8) (rgb >> 8); color.b = (u8) rgb; color.a = 0xFF;
    left = (f32) (5 * (int) ((b->left - 120.0F) / 5.0F));
    right = b->right + 120.0F;
    bottom = (f32) (5 * (int) ((b->bottom - 120.0F) / 5.0F));
    top = b->top + 120.0F;
    if (!(right > left) || !(top > bottom) || right - left > 2000.0F || top - bottom > 2000.0F) {
        return;
    }
    v0.z = v1.z = -30.0F;
    for (x = left; x <= right; x += 5.0F) {
        v0.x = x - 0.12F; v1.x = x + 0.12F; v0.y = bottom; v1.y = top;
        lbColl_80009DD4(&v0, &v1, &color);
    }
    for (y = bottom; y <= top; y += 5.0F) {
        v0.y = y - 0.12F; v1.y = y + 0.12F; v0.x = left; v1.x = right;
        lbColl_80009DD4(&v0, &v1, &color);
    }
}
