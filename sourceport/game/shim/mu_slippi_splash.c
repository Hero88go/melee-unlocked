/* Slippi online menus: VS splash, in-game name tags and the online results screen, native.
 * Covers GALE01r2.ini codes InitVsSplash (4913) + CheckAltStageName (5658) + FG_CreateSubtext
 * (5844, plain mode), PlayOpponentCharAnnouncer (5033), HideAllJObjs (5055), HideLetterJObjs
 * (5076), SkipStageNumberShow (5093), InitInGame (4039), ParseNumbersBetter (4230),
 * ControlAllPanels (4231), GrayPanelsForRemotePlayers (4245). Every entry point is inert unless
 * the menus flag is on and the matching online scene is current. Console struct offsets from the
 * asm are translated to decomp field names; MSRB game info stays console layout (raw bytes). */
#include <string.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>
#include <melee/if/ifall.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/pl/player.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/sislib_font.h>
#include <mu_native.h>

/* MSRB game info (console StartMeleeData): rules 0x60 bytes, then 0x24 per player. */
#define GI_IS_TEAMS 0x8
#define GI_STAGE 0xE
#define GI_PLAYER(i) (0x60 + (i) * 0x24)
#define GP_CKIND 0x0
#define GP_SLOT_TYPE 0x1
#define GP_TEAM 0x9

static const MuMatchState* load_ms(void)
{
    mu_slippi_load_match_state(NULL);
    return mu_slippi_match_state();
}

static int gi_u8(const MuMatchState* ms, int off)
{
    return ms->game_info[off];
}

/* ---------------------------------------------------------------- VS splash (8:4) */

int mu_slippi_splash_active(void)
{
    return mu_slippi_menus_enabled() && mu_slippi_on_online_splash();
}

/* FG_CreateSubtext, plain mode: init, size, colour, then the formatted contents. */
static int create_subtext(HSD_Text* t, const GXColor* color, float scale, float x, float y,
                          const char* fmt, const char* arg)
{
    int idx = HSD_SisLib_803A6B98(t, x, y, fmt, arg);
    HSD_SisLib_803A7548(t, idx, scale, scale);
    HSD_SisLib_803A74F0(t, idx, (GXColor*) color);
    HSD_SisLib_803A70A0(t, idx, (char*) fmt, arg);
    return idx;
}

/* Stage-name SIS ids by stage-select id (mnstagesw.c mnStageSw_stageIcons, static there;
 * the asm reads it at mnStageSw_803ED488 + 0x5C). */
static const u8 stage_name_ids[0x1D] = {
    0x08, 0x09, 0x11, 0x0A, 0x0C, 0x06, 0x0B, 0x07, 0x0E, 0x0D,
    0x1D, 0x17, 0x0F, 0x10, 0x12, 0x13, 0x14, 0x15, 0x1A, 0x1B,
    0x1C, 0x16, 0x18, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24,
};

static const GXColor splash_colors[5] = {
    { 0xE5, 0x4C, 0x4C, 0xFF }, /* red */
    { 0x4B, 0x4C, 0xE5, 0xFF }, /* blue */
    { 0xFF, 0xCB, 0x00, 0xFF }, /* yellow */
    { 0x00, 0xB2, 0x00, 0xFF }, /* green */
    { 0xFF, 0xFF, 0xFF, 0xFF }, /* white */
};
static const char* const team_labels[3] = { "RT", "BT", "GT" };
static const char* const port_labels[4] = { "P1", "P2", "P3", "P4" };

static void splash_player(HSD_Text* t, const MuMatchState* ms, int p, float x, float y)
{
    const float size = 0.5f;
    if (gi_u8(ms, GI_IS_TEAMS) == 0) {
        create_subtext(t, &splash_colors[p & 3], size, x, y, "%s", port_labels[p & 3]);
    } else {
        int team = gi_u8(ms, GI_PLAYER(p) + GP_TEAM);
        int col = team >= 2 ? team + 1 : team; /* team 2 is green: skip yellow */
        if (col > 4) {
            col = 4;
        }
        create_subtext(t, &splash_colors[col], size, x, y, "%s",
                       team < 3 ? team_labels[team] : "");
    }
    create_subtext(t, &splash_colors[4], size, x + 36.0f, y, "%s", ms->names[p]);
}

/* InitVsSplash: gm_Scene_IntroEasy_OnEnter, right after fn_80186634. */
void mu_slippi_splash_text(void)
{
    const MuMatchState* ms;
    HSD_Text* t;
    int local, local_team, left = 0, right = 0, p, stage, sel;

    if (!mu_slippi_splash_active()) {
        return;
    }

    HSD_SisLib_803A62A0(0, "SdSlChr.usd", "SIS_SelCharData");
    ms = load_ms();

    t = HSD_SisLib_803A6754(0, 0);
    t->default_kerning = 1;
    t->default_alignment = 0;
    t->pos_z = 0.0f;
    t->font_size.x = 1.0f;
    t->font_size.y = 1.0f;

    local = ms->local_index & 3;
    local_team = gi_u8(ms, GI_PLAYER(local) + GP_TEAM);
    for (p = 3; p >= 0; p--) {
        int go_left, count;
        float base, fc;
        if (gi_u8(ms, GI_PLAYER(p) + GP_SLOT_TYPE) >= 3) {
            continue;
        }
        if (p == local) {
            go_left = 1;
        } else if (gi_u8(ms, GI_IS_TEAMS) == 0) {
            go_left = 0;
        } else {
            go_left = gi_u8(ms, GI_PLAYER(p) + GP_TEAM) == local_team;
        }
        if (go_left) {
            count = left++;
            base = 60.0f;
        } else {
            count = right++;
            base = 420.0f;
        }
        fc = (float) count;
        splash_player(t, ms, p, base + fc * -22.0f, 80.0f + fc * -22.0f);
    }

    /* Stage name */
    t = HSD_SisLib_803A6754(0, 0);
    t->pos_x = 238.0f;
    t->pos_y = 440.0f;
    t->box_size_x = 160.0f;
    t->box_size_y = 300.0f;
    t->font_size.x = 1.0f;
    t->font_size.y = 1.0f;
    t->default_alignment = 1;
    t->default_kerning = 1;
    t->pos_z = 0.0f;
    stage = (ms->game_info[GI_STAGE] << 8) | ms->game_info[GI_STAGE + 1];
    if (stage == 3 && ms->alt_stage_mode != 0) { /* CheckAltStageName */
        HSD_SisLib_803A6368(t, 89);              /* Frozen Pokemon Stadium */
        return;
    }
    for (sel = 0; sel < 0x1D; sel++) {
        if ((int) gm_801641CC((u8) sel) == stage) {
            break;
        }
    }
    if (sel >= 0x1D) {
        sel = 0;
    }
    HSD_SisLib_803A6368(t, stage_name_ids[sel]);
}

/* PlayOpponentCharAnnouncer: the announcer names the remote player's character. */
unsigned int mu_slippi_splash_announce_char(unsigned int vanilla)
{
    const MuMatchState* ms;
    if (!mu_slippi_splash_active()) {
        return vanilla;
    }
    ms = load_ms();
    return (unsigned int) gi_u8(ms, GI_PLAYER(ms->remote_index & 3) + GP_CKIND);
}

/* HideLetterJObjs: after HSD_JObjAnimAll in fn_80184AB8, strip the anims of children 9-13. */
void mu_slippi_splash_hide_letters(HSD_JObj* jobj)
{
    int i;
    if (!mu_slippi_splash_active()) {
        return;
    }
    for (i = 9; i < 14; i++) {
        HSD_JObj* child = NULL;
        lb_80011E24(jobj, &child, i, -1);
        if (child != NULL) {
            HSD_JObjRemoveAnimAll(child);
        }
    }
}

/* SkipStageNumberShow (global in Slippi, gated to the online splash here). */
int mu_slippi_splash_skip_stage_number(void)
{
    return mu_slippi_splash_active();
}

/* HideAllJObjs: fn_8018504C hides the 27 stage-preview joints and returns early. */
int mu_slippi_splash_hide_stage(HSD_JObj* jobj)
{
    int i;
    if (!mu_slippi_splash_active() || jobj == NULL) {
        return 0;
    }
    for (i = 0; i < 27; i++) {
        HSD_JObj* child = NULL;
        lb_80011E24(jobj, &child, i, -1);
        if (child != NULL) {
            child->flags |= JOBJ_HIDDEN;
        }
    }
    return 1;
}

/* ---------------------------------------------------------------- in game (8:2) */

static int hud_canvas = -1;
static HSD_Text* hud_text;

HSD_Text* mu_slippi_hud_text(void)
{
    return hud_text;
}

int mu_slippi_hud_canvas(void)
{
    return hud_canvas;
}

static void hud_cobj_cb(HSD_GObj* gobj, intptr_t pass)
{
    /* The asm also skips drawing while the name-tag pause flag (ifnametag.c un_804D6D6C, static)
     * is set; online matches do not pause, so it is not mirrored. */
    HSD_GObj_803910D8(gobj, pass);
}

/* Tag plate width from the encoded name, as the asm counts it (20 - glyph width per glyph). */
static int tag_width(HSD_Text* t)
{
    s32 size = 0;
    int width = 0;
    const u8* widths_ext;
    u8* cur = fn_803A6FEC(t->sis_buffer, 0, &size);
    if (cur == NULL) {
        return 60;
    }
    cur += 15; /* subtext header */
    widths_ext = DP(HSD_SisLib_804D1124[t->font_idx][1]);
    for (;;) {
        u8 op = cur[0];
        if (op == 0x20) {
            width += 20 - HSD_SisLib_8040CB00[cur[1]].left;
            cur += 2;
        } else if (op == 0x40) {
            width += 20 - (widths_ext != NULL ? widths_ext[cur[1] * 2] : 0);
            cur += 2;
        } else if (op == 0x0F || op == 0x00) { /* 0x00: guard, the asm stops on 0x0F only */
            break;
        } else {
            cur += 1;
        }
    }
    if (width < 60) {
        width = 60;
    }
    if (width > 144) {
        width = 144;
    }
    return width;
}

/* InitInGame: end of ifStatus_802F665C. */
void mu_slippi_ingame_hud_init(void)
{
    SceneDesc* sd = NULL;
    HSD_CObj* cobj;
    HSD_GObj* cam;
    HSD_Text* t;
    const MuMatchState* ms;
    DISC_PTR(DynamicModelDesc)* tag_models = NULL;
    HSD_Joint* tag_joint = NULL;
    int i;

    if (!(mu_slippi_menus_enabled() && mu_slippi_in_online_game())) {
        return;
    }

    lbArchive_LoadSections(*ifAll_GetArchive(), (void**) &sd, "ScInfDmg_scene_data", NULL);
    if (sd == NULL) {
        return;
    }
    cobj = HSD_CObjLoadDesc(DP(DP(sd->cameras)[0].desc));
    cam = GObj_Create(19, 20, 0);
    HSD_GObjObject_80390A70(cam, HSD_GObj_CameraKind, cobj);
    GObj_SetupGXLinkMax(cam, hud_cobj_cb, 8);
    cam->gxlink_prios = 1ULL << 12;
    hud_canvas = HSD_SisLib_803A611C(2, cam, 9, 13, 0, 12, 80, 8);

    /* Text struct for the disconnect/desync messages (online core). */
    t = HSD_SisLib_803A6754(2, hud_canvas);
    t->default_kerning = 1;
    t->default_alignment = 1;
    t->pos_z = 0.0f;
    t->font_size.x = 0.1f;
    t->font_size.y = 0.1f;
    hud_text = t;

    ms = load_ms();

    t = HSD_SisLib_803A6754(2, hud_canvas);
    t->default_kerning = 1;
    t->default_alignment = 2;
    t->pos_z = 0.0f;
    t->font_size.x = 0.1f;
    t->font_size.y = 0.1f;
    t->active_color.a = 0x80;
    HSD_SisLib_803A6B98(t, 270.0f, 207.0f, "Delay: %df", (int) ms->delay_frames);
    HSD_SisLib_803A7548(t, 0, 0.33f, 0.33f);

    lbArchive_LoadSections(*ifAll_GetArchive(), (void**) &tag_models, "ScInfPnm_scene_models",
                           NULL);
    if (tag_models != NULL) {
        tag_joint = DP(DP(tag_models[0])->joint);
    }

    for (i = 0; i < 6; i++) {
        float hud_x;
        HSD_GObj* bg_gobj;
        HSD_JObj* bg;
        HSD_JObj* child = NULL;
        if (Player_GetPlayerSlotType(i) == Gm_PKind_NA) {
            continue;
        }
        hud_x = ifAll_GetPlayerHUDPosition(i)->x;

        t = HSD_SisLib_803A6754(2, hud_canvas);
        t->default_fitting = 1;
        t->default_alignment = 1;
        t->x4C = 1;
        t->default_kerning = 1;
        t->font_size.x = 0.06f;
        t->font_size.y = 0.06f;
        t->pos_x = hud_x + 0.8f;
        t->pos_y = 20.64f;
        t->pos_z = 0.0f;
        t->box_size_x = 150.0f;
        t->box_size_y = 150.0f;
        HSD_SisLib_803A6B98(t, 0.0f, 0.0f, "%s", i < 4 ? ms->names[i] : "");
        HSD_SisLib_803A7548(t, 0, 0.54f, 0.54f);

        if (tag_joint == NULL) {
            continue;
        }
        bg_gobj = GObj_Create(14, 15, 0);
        bg = HSD_JObjLoadJoint(tag_joint);
        HSD_GObjObject_80390A70(bg_gobj, HSD_GObj_JObjKind, bg);
        GObj_SetupGXLink(bg_gobj, HSD_GObj_JObjCallback, 12, 0);
        HSD_JObjSetTranslateX(bg, hud_x + 0.775f);
        HSD_JObjSetTranslateY(bg, -24.06f);
        HSD_JObjSetScaleY(bg, 0.62f);
        lb_80011E24(bg, &child, 1, -1);
        if (child != NULL && child->u.dobj != NULL) {
            HSD_DObj* d = child->u.dobj;
            HSD_JObjSetTranslateZ(child, 0.0f);
            d->flags |= DOBJ_HIDDEN;
            if (d->next != NULL) {
                d = d->next;
                d->flags |= DOBJ_HIDDEN;
                if (d->next != NULL && d->next->mobj != NULL && d->next->mobj->mat != NULL) {
                    HSD_Material* mat = d->next->mobj->mat;
                    GXColor black = { 0, 0, 0, 255 };
                    mat->alpha = 0.33f;
                    mat->diffuse = black;
                }
            }
        }
        HSD_JObjSetScaleX(bg, (float) tag_width(t) * 0.0146f);
    }
}

/* ParseNumbersBetter (global in Slippi): no kerning-start bytes before digits, only inside the
 * online major here (decision D3). */
int mu_slippi_digits_no_kerning(void)
{
    return mu_slippi_menus_enabled() && mu_slippi_in_online_mode();
}

/* ---------------------------------------------------------------- results (8:3, Party) */

static int results_active(void)
{
    return mu_slippi_menus_enabled() && mu_slippi_on_online_results();
}

/* ControlAllPanels: every port reads the 1P port's pad. */
void mu_slippi_results_control_all_panels(void)
{
    int src, i;
    if (!results_active()) {
        return;
    }
    src = gmMainLib_804D3EE0->vs.unk_530.x6 & 3;
    for (i = 0; i < 4; i++) {
        if (i != src) {
            HSD_PadCopyStatus[i] = HSD_PadCopyStatus[src];
        }
    }
}

/* GrayPanelsForRemotePlayers: remote panels take the CPU (grey) colour. */
u8 mu_slippi_results_slot_type(int port, u8 slot_type)
{
    if (!results_active()) {
        return slot_type;
    }
    if (port == mu_slippi_state()->game_local_index) {
        return slot_type;
    }
    return 1;
}
