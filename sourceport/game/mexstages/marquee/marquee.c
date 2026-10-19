/* GrWWLL2 exports only 1, 3 and 5. Three plain map gobjs, then a fourth on load renders
 * the fighters' names, or the distinct team names. Unexported callbacks retain the retail row.
 * Map callbacks +B0/+128/+1A0 pass a gobj to Ground_InitMapColl on console. These maps have
 * no collision-joint bindings, so passing their actual joint has the same effect. */
#include "../common/mu_gr_common.h"
#include "../common/mu_gr_mex.h"
#include <melee/ft/types.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/ground.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_00F9.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <dolphin/os.h>
#include <string.h>

static const u8 letter_width[] = {
    0x21,0x2b,0x3d,0x3d,0x55,0x4b,0,0x32,0x32,0x26,0x38,0x1e,0x24,0x1f,0x1e,0x4b,
    0x2b,0x3f,0x3f,0x53,0x3f,0x46,0x46,0x46,0x46,0x1f,0x1f,0x35,0x32,0x35,0x38,0x64,
    0x4c,0x4c,0x4c,0x51,0x3d,0x3d,0x4b,0x4b,0x26,0x35,0x53,0x46,0x64,0x44,0x58,0x51,
    0x58,0x51,0x3d,0x47,0x4b,0x42,0x64,0x4b,0x4b,0x42,0x2d,0x1f,0x24,0x32,0x41,0x1a,
    0x44,0x4b,0x38,0x44,0x44,0x3d,0x44,0x44,0x23,0x1f,0x4b,0x24,0x64,0x44,0x44,0x44,
    0x36,0x3d,0x33,0x3f,0x46,0x3d,0x5b,0x44,0x30,0x36,0,0,
};
static bool marquee_false(Ground_GObj* gobj) { (void) gobj; return false; }
static void marquee_create(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    Ground_InitMapColl(GET_JOBJ(gobj), gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
}
static void marquee_frame(Ground_GObj* gobj) { Ground_UpdateMapColl(gobj); }
static void marquee_floor(Ground_GObj* gobj) { Ground_UpdateMapColl(gobj); lb_800115F4(); }
static int marquee_width(u8 ch) { return ch == ' ' ? 65 : letter_width[ch - 33]; }
MU_NO_CONTRACT static void marquee_letters(Ground_GObj* gobj)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    HSD_JObj* root = GET_JOBJ(gobj);
    HSD_JObj* anchor = Ground_801C3FA4(gobj, 0);
    HSD_JObj* previous;
    DynamicModelDesc* model;
    char names[144] = { 0 };
    int i, used = 0, entries = 0, width = 0, cursor = 0;
    lb_8000B1CC(Ground_801C3FA4(Ground_GetMapGObj(2), 2), NULL, &anchor->translate);
    model = HSD_ArchiveGetPublicAddress(grDatFiles_801C6330(gp->map_id)->unk0, "MarqueeLetters_jobjdesc");
    if (gm_8016B168()) {
        u8 teams[6] = { 0 };
        static const char* team_names[6] = {
            "Red Team", "Blue Team", "Green Team", "Training Dummy", "Team 5", "Team 6"
        };
        for (i = 0; i < 6; i++) {
            if (Player_GetPlayerSlotType(i) != 3) {
                Fighter* fp = HSD_GObjGetUserData(Player_GetEntity(i));
                teams[fp->team] = 1;
            }
        }
        for (i = 0; i < 6; i++) if (teams[i]) {
            /* +3A4 deliberately checks the team index, not how many names came before. */
            if (i > 0) { strcpy(names + used, " & "); used += 3; }
            strcpy(names + used, team_names[i]); used += strlen(team_names[i]);
        }
    } else {
        for (i = 0; i < 6; i++) if (Player_GetPlayerSlotType(i) != 3) {
            /* codes.gct 801609A8 replaces the retail name table with MxDt's names. */
            const char* name = mu_gr_mex_fighter_name(Player_GetPlayerCharacter(i));
            if (strlen(name) > 20) __assert("marquee.c", 0x127, "fighter name over 20 characters\n");
            if (entries > 0) { strcpy(names + used, " & "); used += 3; }
            strcpy(names + used, name); used += strlen(name); entries++;
        }
    }
    names[used] = 0;
    mu_grLast_8021B2D8(gobj);
    OSReport("%s", names);
    for (i = 0; i < used; i++) width += marquee_width(names[i]);
    previous = Ground_801C3FA4(gobj, 1);
    for (i = 0; i < used; i++) {
        HSD_JObj* glyph;
        float x, half;
        int w;
        if (names[i] == ' ') { cursor += 65; continue; }
        glyph = HSD_JObjLoadJoint(DP(model->joint));
        previous->next = glyph;
        glyph->parent = previous->parent;
        gm_8016895C(glyph, model, 0);
        HSD_JObjReqAnim(glyph, (float) ((u8) names[i] - 33));
        HSD_JObjAnim(glyph);
        w = marquee_width(names[i]);
        x = (float) cursor / 100.0F; x *= 6.0F;
        half = (float) width / 100.0F; half *= 6.0F; half *= 0.5F;
        x -= half;
        half = (float) (w / 2) / 100.0F; half *= 6.0F;
        glyph->translate.x = x + half;
        cursor += w;
        previous = glyph;
    }
    if (width > 2600) anchor->scale.x *= 2600.0F / (float) width;
    HSD_ForeachAnim(root, 6, 0xFFFF, HSD_AObjStopAnim, AOBJ_ARG_AOV, NULL, NULL);
    HSD_JObjSetMtxDirtySub(root);
}
#define MARQUEE_ROW(create, frame) { create, marquee_false, frame, mu_gr_gobj_nop, 0 }
static StageCallbacks marquee_map[4] = {
    MARQUEE_ROW(marquee_create, marquee_frame), MARQUEE_ROW(marquee_create, marquee_frame),
    MARQUEE_ROW(marquee_create, marquee_floor), MARQUEE_ROW(marquee_letters, mu_gr_gobj_nop),
};
static void marquee_init(void)
{
    mu_grTSeak_80223908(0); mu_grTSeak_80223908(1); mu_grTSeak_80223908(2);
    Ground_801C39C0(); Ground_801C3BB4();
}
static void marquee_load(void) { mu_grTSeak_80223908(3); }
const MuGrStage mu_gr_midnight_marquee = {
    .name = "Midnight Marquee", .file = "GrWWLL2.dat", .map_gobjs = marquee_map,
    .on_init = marquee_init, .on_load = marquee_load, .flags = MU_GR_READY,
};
