/* Saturn Valley's coffee break: the down taunt that starts it (+0F90 .. +1388), the coffee map
 * gobj 9 with its scrolling text (+3AA0 .. +4188), the screen tint (+04A8 .. +0948, +39A4 ..
 * +3AA0). Offsets refer to GrSv.dat grFunction. */
#include "saturnvalley.h"

#include <math_native.h>
#include <string.h>

#include <dolphin/gx.h>
#include <melee/cm/camera.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftaction.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftdynamics.h>
#include <melee/ft/kinds/ftFox/ftfoxappeals.h>
#include <melee/ft/types.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdisplay.h>
#include <melee/gr/ground.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/sislib_font.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tobj.h>

/* Map gobj 9's Ground user data (console +E0 on). */
typedef struct SvCoffeeVars {
    int state;          /* console +E0 */
    int canvas;         /* +E4 */
    HSD_Text* text;     /* +E8 */
    float text_end;     /* +EC: the text's last y */
    int xF0;            /* +F0: not used */
    int song;           /* +F4: the song to restore */
    HSD_TObj* scroll;   /* +F8: the third texture of joint 0, the indirect scroll */
    HSD_GObj* overlay;  /* +FC */
} SvCoffeeVars;
SV_FITS(SvCoffeeVars);

/* The screen tint's user data (+39B4: HSD_MemAlloc(0x20), byte for byte this layout). */
typedef struct SvOverlay {
    u8 destroy;         /* +0 */
    u8 alpha_max;       /* +1 */
    u8 x2;              /* +2: written 0, not read */
    u8 cur[4];          /* +3: the color drawn */
    u8 a[4];            /* +7 */
    u8 b[4];            /* +B */
    u8 pad;             /* +F */
    float fade_step;    /* +10 */
    float fade;         /* +14 */
    float color_step;   /* +18 */
    float color_t;      /* +1C */
} SvOverlay;
_Static_assert(sizeof(SvOverlay) == 0x20, "SvOverlay is the console's 0x20 bytes");

/* lbl_804D38E8, the background music fade factor of lb/lbaudio_ax.c (static there, reached through
 * mu_lbAudioAx_bgm_fade). The block fades it to 0 around the coffee song and sets it back to 1. */
float* mu_lbAudioAx_bgm_fade(void);
static float* sv_bgm_fade(void) { return mu_lbAudioAx_bgm_fade(); }

/* ---- the taunt (a fighter state the stage defines) ---- */

static void sv_taunt_anim(HSD_GObj* g);
static void sv_taunt_input(HSD_GObj* g) { (void) g; }
/* +44A8 taunt: anim_id 0, flags 0x72, move word 0x01000000; the coll callback is the data word
 * at +0004, a jump to 800E5C5C (ftFx_AppealS_Coll). */
static MotionState sv_taunt = {
    0, 0x72, { 0x01000000 },
    sv_taunt_anim, sv_taunt_input, ft_80084F3C, ftFx_AppealS_Coll, ftCamera_UpdateCameraBox,
};

/* +0F90 Fighter_EnterStageState: state 0xEF without its animation, then the stage's own row of
 * the "ftcmd" table (motion 10000, anim_id 0, the row's flags, script and figatree), and the
 * stage's callbacks. The console stores the row's figatree word only when it is not positive
 * (any relocated pointer is above 0x80000000 there); every row of the file is a relocated
 * pointer or 0, so the pointer is used as it is. */
static void sv_enter_stage_state(HSD_GObj* g, const MotionState* state, Fighter_WaitAnimData* row,
                                 float anim_start, float anim_speed, float anim_blend)
{
    Fighter* fp = HSD_GObjGetUserData(g);
    Fighter_ChangeMotionState(g, 0xEF, Ft_MF_SkipAnim, 0.0f, 1.0f, 0.0f, NULL);
    fp->x594_s32 = row->x10_animCurrFlags;
    fp->motion_id = 10000;
    fp->anim_id = 0;
    fp->x5A4 = (uintptr_t) -1;
    fp->x3E4_fighterCmdScript.u = DP(row->xC);
    fp->x3E4_fighterCmdScript.timer = 0.0f;
    ftCo_8009E7B4(fp, NULL);
    fp->x590 = (FigaTree*) (mu_uptr) (u32) row->x4;
    ftAnim_8006EBE8(g, anim_start, anim_speed, anim_blend);
    ftAnim_8006E9B4(g);
    if (anim_start == 0.0f) {
        ftCo_800C0408(g);
        ftAction_80073240(g);
    } else {
        ftAction_8007349C(g);
    }
    fp->anim_cb = state->anim_cb;
    fp->phys_cb = state->phys_cb;
    fp->input_cb = state->input_cb;
    fp->coll_cb = state->coll_cb;
    fp->cam_cb = state->cam_cb;
}
/* +0054 Fighter_Coffee_AnimCB: the script's first variable starts the coffee break once (sound
 * 10). Console detail kept: when the coffee gobj already exists it returns before the end test. */
static void sv_taunt_anim(HSD_GObj* g)
{
    Fighter* fp = HSD_GObjGetUserData(g);
    if (fp->cmd_vars[0] != 0) {
        fp->cmd_vars[0] = 0;
        if (Ground_GetMapGObj(9) != NULL) return;
        if (mu_grTSeak_80223908(9) == NULL) __assert("coffee.c", 0x6A, "could not create coffee");
        mu_gr_mex_sfx(10);
    }
    if (ftAnim_IsFramesRemaining(g)) return;
    ft_8008A2BC(g);
}
/* +1134 Fighter_EnterCoffeeTaunt: no export reaches it (+1178 inlines it). */
void sv_taunt_enter(HSD_GObj* g, int index, SvCoffeeGate* gate)
{
    Fighter* fp = HSD_GObjGetUserData(g);
    fp->cmd_vars[0] = fp->cmd_vars[1] = fp->cmd_vars[2] = fp->cmd_vars[3] = 0;
    sv_enter_stage_state(g, &sv_taunt, &gate->ftcmd[index], 0.0f, 1.0f, 0.0f);
}
/* +10EC Fighter_Coffee_OnTakeDamage: no export or table reaches it. */
void sv_taunt_take_damage(HSD_GObj* g)
{
    Fighter* fp = HSD_GObjGetUserData(g);
    HSD_GObj* coffee = Ground_GetMapGObj(9);
    if (coffee != NULL) Ground_801C4A08(coffee);
    fp->take_dmg_cb = NULL;
}
/* +1178 Coffee_CheckToStart: a fighter in Wait whose down taunt was pressed last frame and is
 * not held, matched by kind or by its m-ex name, takes the row for its facing direction. */
static void sv_coffee_try(HSD_GObj* g, int slot, SvCoffeeGate* gate)
{
    Fighter* fp = HSD_GObjGetUserData(g);
    SvTauntSlot* rows;
    SvTaunt* row;
    int index;
    if (fp->motion_id != 14 || (fp->input.held_buttons[0] & HSD_PAD_DPADDOWN) || fp->x682 != 1) {
        return;
    }
    rows = DP(sv_param->x00);
    for (row = DP(rows[0]); row != NULL; row = DP(*++rows)) {
        const char* name;
        if (row->kind == fp->kind) break;
        name = DP(row->name);
        if (name != NULL) {
            const char* fighter = mu_gr_mex_fighter_name(Player_GetPlayerCharacter(slot));
            if (fighter != NULL && strcmp(name, fighter) == 0) break;
        }
    }
    if (row == NULL) return;
    gate->started = 1;
    index = !(fp->facing_dir <= 0.0f) ? row->right : row->left;
    sv_taunt_enter(g, index, gate);
}
/* +12FC Coffee_Check: once per match; every fighter is tried in the same frame. */
void sv_coffee_check(SvCoffeeGate* gate)
{
    if (gate->started || gate->ftcmd == NULL) return;
    for (int slot = 0; slot < 6; ++slot) {
        HSD_GObj* fighter;
        if (Player_GetPlayerSlotType(slot) == 3) continue;
        fighter = Player_GetEntity(slot);
        if (fighter != NULL) sv_coffee_try(fighter, slot, gate);
    }
}

/* ---- the screen tint ---- */

/* +04A8 Overlay_Process: fade in (out once destroy is set, freed when its alpha is already 0),
 * and the A to B color walk; reaching B swaps the two and starts again from 0. */
static void sv_overlay_proc(HSD_GObj* g)
{
    SvOverlay* o = HSD_GObjGetUserData(g);
    float t, inv;
    if (o->destroy == 0) {
        o->fade = sv_approach(o->fade, 1.0f, o->fade_step);
    } else {
        o->fade = sv_approach(o->fade, 0.0f, o->fade_step);
        if (o->cur[3] == 0) {
            HSD_GObjFree(g);
            return;
        }
    }
    o->cur[3] = (u8) (int) ((float) o->alpha_max * o->fade);
    t = sv_approach(o->color_t, 1.0f, o->color_step);
    o->color_t = t;
    if (t == 1.0f) {
        u8 old_a[4];
        memcpy(old_a, o->a, 4);
        memcpy(o->a, o->b, 4);
        memcpy(o->b, old_a, 4);
        t = 0.0f;
        o->color_t = 0.0f;
        inv = 1.0f;
    } else {
        inv = 1.0f - t;
    }
    for (int k = 0; k < 3; ++k) {
        float b = (float) o->b[k];
        float a = (float) o->a[k] * inv;
        o->cur[k] = (u8) (int) mu_fmadds(b, t, a);
    }
}
/* +0768 GXScreenOverlay: one blended quad, x and y -50 to 50 under the current view matrix. */
static void sv_overlay_draw(HSD_GObj* g, intptr_t pass)
{
    SvOverlay* o = HSD_GObjGetUserData(g);
    u8 r = o->cur[0], gr = o->cur[1], b = o->cur[2], a = o->cur[3];
    (void) pass;
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GXSetZCompLoc(GX_FALSE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE,
                  GX_AF_NONE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXLoadPosMtxImm(HSD_CObjGetCurrent()->view_mtx, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2f32(-50.0f, 50.0f);
    GXColor4u8(r, gr, b, a);
    GXPosition2f32(50.0f, 50.0f);
    GXColor4u8(r, gr, b, a);
    GXPosition2f32(50.0f, -50.0f);
    GXColor4u8(r, gr, b, a);
    GXPosition2f32(-50.0f, -50.0f);
    GXColor4u8(r, gr, b, a);
    GXEnd();
    HSD_StateInvalidate(HSD_STATE_ALL);
}
/* +39B4 CreateScreenOverlay. */
static HSD_GObj* sv_overlay_create(void)
{
    HSD_GObj* g = GObj_Create(3, 5, 0);
    SvOverlay* o;
    GObj_SetupGXLink(g, sv_overlay_draw, 8, 0);
    o = HSD_MemAlloc(sizeof(SvOverlay));
    GObj_InitUserData(g, 4, HSD_Free, o);
    o->destroy = 0;
    o->x2 = 0;
    memcpy(o->cur, sv_param->x4C, 4);
    memcpy(o->a, sv_param->x4C, 4);
    memcpy(o->b, sv_param->x50, 4);
    o->color_step = sv_param->x54;
    o->cur[3] = 0;
    o->color_t = 0.0f;
    o->fade = 0.0f;
    o->alpha_max = sv_param->x4C[3];
    o->fade_step = 0x1.111112p-6f; /* +439C: 1/60 as a float */
    HSD_GObj_SetupProc(g, sv_overlay_proc, 1);
    return g;
}
/* +39A4 Overlay_Destroy: no export reaches it (+3AA0 inlines it). */
void sv_overlay_destroy(HSD_GObj* g) { ((SvOverlay*) HSD_GObjGetUserData(g))->destroy = 1; }

/* ---- the coffee gobj (map gobj 9) ---- */

/* +3DA0 Text_MeasureText: width of the widest line and 32 per line break, times the text scale.
 * Console details kept: every character counts the margins of glyph 0 (HSD_SisLib_8040CB00[0]),
 * only codes up to 0x3FFF count, and the first line adds no height. */
static void sv_text_measure(HSD_Text* text, float* width, float* height)
{
    const u8* p = text->sis_buffer;
    float line = 0.0f, widest = 0.0f, lines = 0.0f, scale;
    if (p[0] != 0) {
        u8 b = p[0];
        do {
            if (b & 0x60) {
                u16 code = (u16) ((p[0] << 8) | p[1]);
                p += 1;
                if (code <= 0x3FFF) {
                    line += (float) (HSD_SisLib_8040CB00[0].right + HSD_SisLib_8040CB00[0].left);
                    if (line > widest) widest = line;
                }
                b = *++p;
                continue;
            }
            switch (b) {
            case 0x03:
                lines += 32.0f;
                line = 0.0f;
                b = p[1]; p += 1;
                break;
            case 0x05:
                b = p[3]; p += 3;
                break;
            case 0x06: case 0x07: case 0x0A: case 0x0E:
                b = p[5]; p += 5;
                break;
            case 0x0C:
                b = p[4]; p += 4;
                break;
            case 0x1A:
                line += 16.0f;
                if (line > widest) widest = line;
                b = p[1]; p += 1;
                break;
            default:
                b = p[1]; p += 1;
                break;
            }
        } while (b != 0);
    }
    scale = text->x34.y * text->font_size.y;
    *height = scale * lines;
    *width = scale * widest;
}
/* +0948 Coffee_GXCallback: indirect texture stages 1 and 4 scroll by the third texture's
 * translate.y and scale.y while the model draws, then every indirect setting is cleared. */
static void sv_coffee_draw(HSD_GObj* g, intptr_t pass)
{
    SvCoffeeVars* v = SV_VARS(SvCoffeeVars, g);
    f32 mtx[2][3] = { { 0.0f, 0.0f, 0.0f }, { v->scroll->translate.y, 0.0f, v->scroll->scale.y } };
    GXSetIndTexMtx(GX_ITM_0, mtx, 1);
    GXSetNumIndStages(2);
    GXSetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD0, GX_TEXMAP2);
    GXSetIndTexCoordScale(GX_INDTEXSTAGE0, GX_ITS_1, GX_ITS_1);
    GXSetTevIndirect(GX_TEVSTAGE1, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_ST, GX_ITM_0, GX_ITW_OFF,
                     GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);
    GXSetIndTexOrder(GX_INDTEXSTAGE1, GX_TEXCOORD1, GX_TEXMAP2);
    GXSetIndTexCoordScale(GX_INDTEXSTAGE1, GX_ITS_1, GX_ITS_1);
    GXSetTevIndirect(GX_TEVSTAGE4, GX_INDTEXSTAGE1, GX_ITF_8, GX_ITB_ST, GX_ITM_0, GX_ITW_OFF,
                     GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);
    grDisplay_801C5DB0(g, pass);
    GXSetNumIndStages(0);
    GXSetTevIndirect(GX_TEVSTAGE0, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_NONE, GX_ITM_OFF, GX_ITW_OFF,
                     GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);
    GXSetTevIndirect(GX_TEVSTAGE1, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_NONE, GX_ITM_OFF, GX_ITW_OFF,
                     GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);
    GXSetTevIndirect(GX_TEVSTAGE4, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_NONE, GX_ITM_OFF, GX_ITW_OFF,
                     GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF);
    GXSetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL);
    GXSetIndTexOrder(GX_INDTEXSTAGE1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL);
}
/* +3FA4 Coffee_OnCreation: a SIS message (2 + HSD_Randi(count)) on a canvas of the match camera,
 * starting at joint 2's height and scrolling to joint 3's; the runner vanishes. */
void sv_coffee_create(HSD_GObj* g)
{
    SvCoffeeVars* v = SV_VARS(SvCoffeeVars, g);
    HSD_JObj *from, *to;
    HSD_Text* text;
    HSD_GObj* runner;
    Vec3 top, bottom;
    float size, width, height;
    g->render_cb = sv_coffee_draw;
    from = Ground_801C3FA4(g, 2);
    to = Ground_801C3FA4(g, 3);
    lb_8000B1CC(from, NULL, &top);
    lb_8000B1CC(to, NULL, &bottom);
    v->canvas = HSD_SisLib_803A611C(1, Camera_80030A50(), 9, 13, 0, 7, 0, 7);
    size = sv_param->x68 * 0.03125f;
    text = HSD_SisLib_803A5ACC(1, v->canvas, 0.0f, -top.y, top.z, 1000.0f, 96.0f);
    text->x4C = 1;
    text->default_alignment = 1;
    text->font_size.x = sv_param->x60;
    text->x34.x = size;
    text->x34.y = size;
    HSD_SisLib_803A6368(text, HSD_Randi(sv_param->x6C) + 2);
    v->text = text;
    sv_text_measure(text, &width, &height);
    text->pos_x = sv_param->x60 * -500.0f;
    v->text_end = -height - bottom.y;
    v->song = Ground_801C5A94();
    v->scroll = Ground_801C3FA4(g, 0)->u.dobj->mobj->tobj->next->next;
    grAnime_801C8138(g, ((Ground*) HSD_GObjGetUserData(g))->map_id, 0);
    v->state = 0;
    runner = Ground_GetMapGObj(6);
    if (runner != NULL) sv_runner_state(runner, 6);
}
/* +3AA0 Coffee_OnFrame: 0 music fades out until the cup animation ends, then the coffee song
 * and the tint; 1 the next animation; 2 the text scrolls, then the text, canvas and tint go;
 * 3 the music fades again, then the stage song, the runner back to state 0, and the gobj ends. */
void sv_coffee_frame(HSD_GObj* g)
{
    SvCoffeeVars* v = SV_VARS(SvCoffeeVars, g);
    int map_id = ((Ground*) HSD_GObjGetUserData(g))->map_id;
    float* fade = sv_bgm_fade();
    switch (v->state) {
    case 0:
        *fade = sv_approach(*fade, 0.0f, sv_param->x5C);
        if (grAnime_801C83D0(g, 0, 2)) {
            *fade = 1.0f;
            lbAudioAx_80023F28(sv_param->x58);
            v->overlay = sv_overlay_create();
            v->state = 1;
            grAnime_801C8138(g, map_id, 1);
        }
        break;
    case 1:
        if (grAnime_801C83D0(g, 0, 2)) {
            v->state = 2;
            grAnime_801C8138(g, map_id, 2);
        }
        break;
    case 2: {
        float y = sv_approach(v->text->pos_y, v->text_end, sv_param->x64);
        v->text->pos_y = y;
        if (v->text_end == y) {
            HSD_SisLib_803A5CC4(v->text);
            HSD_SisLib_803A5DA0(v->canvas);
            if (v->overlay != NULL) sv_overlay_destroy(v->overlay);
            v->overlay = NULL;
            v->state = 3;
            grAnime_801C8138(g, map_id, 3);
        }
        break;
    }
    case 3:
        *fade = sv_approach(*fade, 0.0f, sv_param->x5C);
        if (grAnime_801C83D0(g, 0, 2)) {
            *fade = 1.0f;
            lbAudioAx_80023F28(v->song);
            sv_runner_state(Ground_GetMapGObj(6), 0);
            Ground_801C4A08(g);
        }
        break;
    }
}
