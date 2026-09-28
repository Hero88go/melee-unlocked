/* Training Mode CE, native build: the "Additional Codes".
 *
 * On the console these are the PowerPC patches under ASM/Additional Codes (ledger:
 * run-source/tmce-port/ledger_T4.md, one row per file). Natively each one is the C below, called
 * from the decomp statement its address lands on (mu_tmce_codes.h lists the sites). Every call
 * site checks mu_tmce_active() first, or the hook checks it and returns the vanilla value, so with
 * TM-CE off (always online, and in replays recorded without it) nothing here runs.
 *
 * Several Additional Codes are also Slippi General Codes, which the port already runs natively
 * whenever TM-CE can be active (TM-CE requires a non-vanilla base, see mu_tmce_enabled); those are
 * not repeated here, the ledger names the existing site.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include <mu_native.h>
#include <mu_tmce_codes.h>

#include <string.h>

#include <dolphin/gx/GXGeometry.h>
#include <dolphin/gx/GXStruct.h>
#include <dolphin/gx/GXVert.h>
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/db/db.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/inlines.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/kinds/ftLink/types.h>
#include <melee/ft/kinds/ftPopo/types.h>
#include <melee/ft/types.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/gr/stage.h>
#include <melee/if/textdraw.h>
#include <melee/if/textlib.h>
#include <melee/if/types.h>
#include <melee/it/itCharItems.h>
#include <melee/it/inlines.h>
#include <melee/it/types.h>
#include <melee/lb/lbanim.h>
#include <melee/lb/lbcollision.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>
#include <melee/mn/mnstagesel.h>
#include <melee/mn/types.h>
#include <melee/mp/mpcoll.h>
#include <melee/mp/mplib.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/rumble.h>

extern void* mu_tmce_ref_stc_matchcam;   /* camera.c keeps game_camera file-private */
#define game_camera (*(Camera*) mu_tmce_ref_stc_matchcam)
extern struct gmm_x0 gmMainLib_8045A6C0;
extern u8 mnVibration_804D4FF0[4];
extern void* mu_tmce_event_vars; /* tmce/native/tmce_runtime.c: the event menu's EventVars */

/* ======================================================================================== */
/* Hitbox Visualizer                                                                       */
/* ======================================================================================== */

/* The draw-order index of the fighter hitbox being drawn: the console code reads the caller's
 * loop register (r25) inside lbColl_80009F54. -1 outside ftDrawCommon's hitbox loop (items). */
static int hit_draw_index = -1;

/* Reverse Hitbox ID Order.asm (ftDrawCommon_800805C8+0x4c): hitbox 3 is drawn first, 0 last, so
 * the lowest ID ends up on top. */
HitCapsule* mu_tmce_code_hitbox_order(HitCapsule* hitboxes, int i)
{
    hit_draw_index = i;
    return &hitboxes[3 - i];
}

/* Main.asm DrawBubble: a sphere through lbColl_80008FC8 with the code's own two colors. */
static void draw_bubble(Vec3* pos, float radius)
{
    static GXColor colors[2] = { { 255, 255, 255, 128 }, { 128, 0, 0, 128 } };
    lbColl_80008FC8(*pos, *pos, &colors[0], &colors[1], radius);
}

/* Hitbox Visualizer/Main.asm (ftDrawCommon_800805C8+0x70, after the hitbox loop, inside the
 * "hitboxes shown" block, every render pass). */
void mu_tmce_code_hitbox_extras(Fighter* fp)
{
    hit_draw_index = -1;
    if (fp->kind == Ft_Kind_Link) {
        /* the boomerang on its way back: its catch radius around Link */
        Item_GObj* boomerang = fp->u.lk.boomerang_gobj;
        if (boomerang != NULL) {
            Item* ip = GET_ITEM(boomerang);
            if (ip->msid == 3) {
                Vec3 pos;
                be_f32* attrs = DP(ip->xC4_article_data->x4_specialAttributes);
                pos.x = fp->cur_pos.x;
                pos.y = fp->cur_pos.y + ((struct ftLk_DatAttrs*) fp->dat_attrs)->specialhi_pos_y_offset;
                pos.z = fp->cur_pos.z;
                draw_bubble(&pos, BEV(attrs[0x2C / 4]));
            }
        }
        /* the hookshot hang check (state 8) is commented out in the console code */
    } else if (fp->kind == Ft_Kind_Popo) {
        /* up-B: the partner search radius on frame 4 */
        if ((fp->motion_id == 0x160 || fp->motion_id == 0x15B) && fp->cur_anim_frame == 4.0F) {
            draw_bubble(&fp->cur_pos, ((ftIceClimberAttributes*) fp->dat_attrs)->x7C);
        }
    }
    /* the Pikachu thunder block is commented out in the console code */
}

/* Color Changes Based On Damage.asm (lbColl_80009F54+0x50, default element only): the ID's
 * hue, lighter for weak hits (5 damage or less) and full for 18 or more. The console writes the
 * shared color lbColl_804D36A0 in place; natively the code's color is its own. */
GXColor* mu_tmce_code_hit_color(HitCapsule* hit)
{
    static GXColor color;
    const float min_gb = 180.0F, light = 5.0F, dark = 18.0F;
    float dmg = hit->damage;
    int gb;

    if (!(dmg < dark)) {
        gb = 0;
    } else if (!(dmg > light)) {
        gb = (int) min_gb;
    } else {
        float range = dark - light;
        float d = dmg - light;
        float per = min_gb / range;
        float v = d * per;
        v = min_gb - v;
        gb = (int) v;
    }

    /* hit_draw_index is the draw order: 3 is ID 0 (see Reverse Hitbox ID Order) */
    switch (hit_draw_index) {
    case 3: /* ID 0: red */
        color.r = 255;
        color.g = (u8) gb;
        color.b = (u8) gb;
        break;
    case 2: /* ID 1: green */
        color.r = (u8) gb;
        color.g = 255;
        color.b = (u8) gb;
        break;
    case 1: /* ID 2: blue */
        color.r = (u8) (gb - 10);
        color.g = (u8) (gb - 10);
        color.b = 255;
        break;
    default: /* ID 3, and item hitboxes */
        color.r = 255;
        color.g = (u8) gb;
        color.b = 255;
        break;
    }
    color.a = 200;
    return &color;
}

/* ======================================================================================== */
/* Display Ungrabbable Hurtboxes                                                           */
/* ======================================================================================== */

/* Set while ftDrawCommon_800805C8 draws a fighter's hurtboxes through lbColl_8000A244. The console
 * reads +0x48 of every capsule it draws; only a fighter's (FighterHurtCapsule.is_grabbable) is a
 * meaningful flag, so natively the check runs for fighter hurtboxes only. */
static int fighter_hurt_pass;

void mu_tmce_code_fighter_hurt_pass(int on)
{
    fighter_hurt_pass = on;
}

/* Main.asm (lbColl_8000A244+0x40): a vulnerable hurtbox that cannot be grabbed is purple. */
GXColor* mu_tmce_code_hurt_color(HurtCapsule* hurt, GXColor* color)
{
    static GXColor purple = { 0xAB, 0x27, 0xFF, 0x80 };
    if (fighter_hurt_pass && hurt->state == 0 &&
        !((FighterHurtCapsule*) hurt)->is_grabbable)
    {
        return &purple;
    }
    return color;
}

/* ======================================================================================== */
/* Fighters                                                                                */
/* ======================================================================================== */

/* Disable FoD Reflection.asm (ftDrawCommon_80081140+0xc): no reflection with 4 or more fighters. */
void* mu_tmce_code_fod_reflection(void* render_cb)
{
    return ftLib_800860C4() >= 4 ? NULL : render_cb;
}

/* Hide OnHit GFX When Hitboxes Enabled (ftColl_8007A06C+0x114 and +0x3c4): no hit effect on a
 * fighter that shows its hitboxes. */
int mu_tmce_code_hide_hit_gfx(Fighter* victim)
{
    return victim->x21FC_flag.b6;
}

/* Hide GFX When Hitboxes Enabled.asm (efSync_Spawn+0x54): no effect for a fighter or item that
 * shows its hitboxes. */
int mu_tmce_code_hide_gfx(HSD_GObj* owner)
{
    if (owner == NULL) {
        return 0;
    }
    if (owner->classifier == HSD_GOBJ_CLASS_FIGHTER) {
        return GET_FIGHTER(owner)->x21FC_flag.b6;
    }
    if (owner->classifier == HSD_GOBJ_CLASS_ITEM) {
        return GET_ITEM(owner)->xDAA_flag.b6;
    }
    return 0;
}

/* ======================================================================================== */
/* Better Camera                                                                           */
/* ======================================================================================== */

/* C-Stick Pans Camera.asm (Camera_8002CB0C+0x24, before the no-pauser return): the pauser's
 * C-stick past the PlCo deadzone pans the paused camera, faster when zoomed out. Slots 4 and up
 * read the copy-status block that follows the master block, as on the console. */
void mu_tmce_code_cstick_pan(void)
{
    Camera* cam = &game_camera;
    u8 idx = (u8) cam->x2C5;
    HSD_PadStatus* pad = idx < 4 ? &HSD_PadMasterStatus[idx] : &HSD_PadCopyStatus[idx - 4];
    float dz = p_ftCommonData->horizontal_stick_deadzone;
    float ndz = -dz;
    float v;

    v = pad->nml_subStickY;
    if (v <= ndz || !(v < dz)) {
        float scale = cam->pause_eye_distance / 125.0F;
        float step = v * scale;
        cam->x314.y = cam->x314.y + step;
    }
    v = pad->nml_subStickX;
    if (v <= ndz || !(v < dz)) {
        float scale = cam->pause_eye_distance / 125.0F;
        float step = v * scale;
        cam->x314.x = cam->x314.x + step;
    }
}

/* SnapToOffscreenPlayers (Snap.asm at Camera_8002C5B4+0x3c, Always Onscreen.asm at
 * Camera_8002CDDC+0x158): whether the player on the pausing port is in the match and alive. */
int mu_tmce_code_pauser_alive(void)
{
    u8 pauser = (u8) gmVs_GetSceneController()->state.pauser;
    int i;
    for (i = 0; i < 4; i++) {
        HSD_GObj* gobj = Player_GetEntity(i);
        if (gobj == NULL) {
            continue;
        }
        if (Player_GetPadPort(i) == pauser) {
            return !GET_FIGHTER(Player_GetEntity(i))->x221F_b1;
        }
    }
    return 0;
}

/* Unrestricted Camera.asm (Camera_SetUpPauseCamera+0xe8, where the zoom limit is stored): while
 * develop mode is frame-paused, outside Training mode, the pause camera has no angle or zoom
 * limits. */
void mu_tmce_code_unrestricted_camera(void)
{
    if (gm_GetDbPauseFlag(1) && gm_GetCurrentGameMode() != GM_TRAINING) {
        game_camera.x2D0.unk28 = 0.0F;
        game_camera.x2D0.angle_up = 32768.0F;
        game_camera.x2D0.angle_down = 32768.0F;
        game_camera.x2D0.angle_right = 32768.0F;
        game_camera.x2D0.angle_left = 32768.0F;
        game_camera.x2D0.unk2C = 32768.0F;
    }
}

/* ======================================================================================== */
/* Develop mode                                                                            */
/* ======================================================================================== */

/* Slow Camera With R (Pan, Rotate, Zoom): R held on any controller slows the develop camera. */
float mu_tmce_code_slow_camera(float normal, float slow)
{
    u32 held = 0;
    int i;
    for (i = 0; i < 4; i++) {
        held |= HSD_PadMasterStatus[i].button;
    }
    return (held & HSD_PAD_R) ? slow : normal;
}

/* Enable C Stick Always, Rotate Camera with Dpad Down and C Stick (fn_CheckCameraInfo+0x6c,
 * fn_80227B64+0xc): the C-stick moves the develop camera only while D-pad down is held. */
int mu_tmce_code_dpad_down_held(int port)
{
    return (HSD_PadMasterStatus[(u8) port].button & HSD_PAD_DPADDOWN) != 0;
}

/* The move name inside a special animation's figatree name ("PlyXxx5K_Share_ACTION_NAME_figatree"):
 * the text after the first "N_" up to the next underscore. Bounded natively (the console copies
 * until the underscore into a 28-byte buffer). */
static void move_name(const char* src, char* out, size_t size)
{
    const char* p = src;
    size_t n = 0;
    for (;;) {
        char c = *++p;
        if (c == '\0') {
            out[0] = '\0';
            return;
        }
        if (c != 'N') {
            continue;
        }
        c = *++p;
        if (c == '\0') {
            out[0] = '\0';
            return;
        }
        if (c == '_') {
            break;
        }
    }
    p++;
    while (*p != '\0' && *p != '_' && n + 1 < size) {
        out[n++] = *p++;
    }
    out[n] = '\0';
}

/* Display Animation Name for Special Moves in Develop Mode (fn_UpdateAnimationInfo+0x10c): a
 * special move's animation shows its name instead of its number. */
const char* mu_tmce_code_anim_name(Fighter* fp)
{
    static char name[28];
    struct ftData_80085FD4_ret* anim = ftData_80085FD4(fp, fp->anim_id);
    const char* src = anim != NULL ? DP(anim->x0) : NULL;
    if (src == NULL) {
        name[0] = '\0';
    } else {
        move_name(src, name, sizeof name);
    }
    return name;
}

/* ---- Physics Display (db_Setup+0x9c) ---- */

typedef struct PhysicsWindow {
    DevText* text;
    int state; /* 0 hidden, 1-4 the player shown */
} PhysicsWindow;

static void physics_state_info(Fighter* fp, char* out, size_t size, float* frame, float* frames)
{
    if (fp->motion_id < 341) {
        const char* s = db_motionstate_names[fp->motion_id];
        strncpy(out, s != NULL ? s : "", size - 1);
        out[size - 1] = '\0';
    } else {
        struct ftData_80085FD4_ret* anim = ftData_80085FD4(fp, fp->anim_id);
        if (anim == NULL || DP(anim->x0) == NULL) {
            strncpy(out, "UnkState", size);
        } else {
            move_name(DP(anim->x0), out, size);
        }
    }
    *frames = fp->x590 != NULL ? fp->x590->frames : 0.0F;
    *frame = fp->cur_anim_frame;
}

/* B held and D-pad right pressed on any controller cycles hidden, P1, P2, P3, P4. Drawn in the
 * develop text pass (GX link 7), pass 0 only. */
static void physics_think(HSD_GObj* gobj, intptr_t pass)
{
    PhysicsWindow* w = gobj->user_data;
    u32 held = 0, repeat = 0;
    HSD_GObj* ft_gobj;
    Fighter* fp;
    char state[64];
    float frame, frames;
    int i;

    if (pass != 0) {
        return;
    }
    for (i = 0; i < 4; i++) {
        held |= HSD_PadMasterStatus[i].button;
        repeat |= HSD_PadMasterStatus[i].repeat;
    }
    if ((held & HSD_PAD_B) && (repeat & HSD_PAD_DPADRIGHT)) {
        w->state++;
        if (w->state <= 4) {
            DevText_ShowText(w->text);
            DevText_ShowBackground(w->text);
        } else {
            w->state = 0;
            DevText_HideText(w->text);
            DevText_HideBackground(w->text);
        }
    }
    if (w->state == 0) {
        return;
    }

    DevText_Erase(w->text);
    DevText_SetCursorXY(w->text, 0, 0);
    DevText_Printf(w->text, "Player %d X Y\n", w->state);
    ft_gobj = Player_GetEntity(w->state - 1);
    if (ft_gobj == NULL) {
        return;
    }
    fp = GET_FIGHTER(ft_gobj);
    DevText_Printf(w->text, "LStick: %1.4f %1.4f\n", fp->input.lstick[0].x, fp->input.lstick[0].y);
    DevText_Printf(w->text, "RStick: %1.4f %1.4f\n", fp->input.cstick[0].x, fp->input.cstick[0].y);
    DevText_Printf(w->text, "SelfVel: %3.4f %3.4f\n", fp->self_vel.x, fp->self_vel.y);
    DevText_Printf(w->text, "KBVel: %3.4f %3.4f\n", fp->x8c_kb_vel.x, fp->x8c_kb_vel.y);
    DevText_Printf(w->text, "Pos: %3.4f %3.4f\n\n", fp->cur_pos.x, fp->cur_pos.y);
    DevText_Printf(w->text, "ECB_Lock: %d", fp->ecb_lock);
    DevText_SetCursorX(w->text, 18);
    DevText_Printf(w->text, "ECB_Bot: %03.2f\n\n", fp->coll_data.desired_ecb.bottom.y);
    physics_state_info(fp, state, sizeof state, &frame, &frames);
    DevText_Printf(w->text, "State: %s", state);
    DevText_SetCursorX(w->text, 25);
    DevText_Printf(w->text, "f: %03.2f/%1.0f", frame, frames);
}

/* Physics Display/Main.asm: in develop mode, a hidden 39x10 develop text window and the GObj that
 * fills it. */
void mu_tmce_code_physics_display(void)
{
    DevText* text;
    HSD_GObj* gobj;
    PhysicsWindow* w;

    text = DevText_Create(12, 0, 0, 39, 10, HSD_MemAlloc(0x1000));
    if (text == NULL) {
        return;
    }
    DevText_Show(DevText_GetGObj(), text);
    text->flags = 0; /* no blinking cursor */
    DevText_SetBGColor(text, (GXColor) { 21, 20, 59, 135 });
    DevText_HideText(text);
    DevText_HideBackground(text);
    DevText_SetScale(text, 7.5F, 10.0F);

    gobj = GObj_Create(0, 0, 0);
    if (gobj == NULL) {
        return;
    }
    w = HSD_MemAlloc(64);
    memset(w, 0, 64);
    GObj_InitUserData(gobj, 4, HSD_Free, w);
    GObj_SetupGXLink(gobj, physics_think, 7, 0);
    w->text = text;
    w->state = 0;
}

/* ======================================================================================== */
/* Sites the main session wires (files owned by other agents, or outside ft/it/cm/gr/lb/db/pl/if) */
/* ======================================================================================== */

/* Hold Z for Rapid Frame Advance (gm_AnyControllerPressedZ, P1-P4): the pad's auto-repeat word
 * instead of its trigger word, so holding Z advances frame after frame. */
unsigned int mu_tmce_code_frame_advance_input(const HSD_PadStatus* pad)
{
    return mu_tmce_active() ? pad->repeat : pad->trigger;
}

/* Pause During Game Start (gm_DoPauseChecksAndRoutine+0x34, gm_DoUnpauseChecksAndRoutine+0x34):
 * the "HUD enabled" test is skipped, so the match can be paused during READY GO. */
int mu_tmce_code_pause_during_start(void)
{
    return mu_tmce_active();
}

/* Neutral Respawn.asm (fn_8016719C+0x80): on the tournament stages a respawning player uses the
 * neutral spawn point for its order among the present players. Returns the spawn point to pass
 * to fn_80167638 (the slot, as vanilla, when the code does not apply). */
int mu_tmce_code_respawn_point(int slot)
{
    static const signed char table[] = {
        0x20, 0x00, 0x01, 0x02, 0x03, /* Final Destination */
        0x1F, 0x02, 0x03, 0x00, 0x01, /* Battlefield */
        0x08, 0x00, 0x01, 0x03, 0x02, /* Yoshi's Story */
        0x1C, 0x01, 0x03, 0x00, 0x02, /* Dream Land */
        0x02, 0x00, 0x01, 0x02, 0x03, /* Fountain of Dreams */
        0x03, 0x00, 0x01, 0x02, 0x03, /* Pokemon Stadium */
        -1,
    };
    int order = 0, i, row;
    int stage;

    if (!mu_tmce_active() || gm_IsCurrently1PMode_inline() || slot >= 5) {
        return slot;
    }
    for (i = 0; i <= 4; i++) {
        if ((int) Player_GetPlayerSlotType(i) > 1) {
            continue; /* not present */
        }
        if (i == slot) {
            break;
        }
        order++;
    }
    stage = Stage_80225194();
    for (row = 0; table[row * 5] != -1; row++) {
        if (table[row * 5] == stage) {
            int at = row * 5 + 1 + order;
            return at < (int) sizeof table ? (u8) table[at] : 0xFF;
        }
    }
    return slot;
}

/* Hold A+B for Salty Runback + Hold A+X for Random Stage + Skip Result Screen
 * (gmVsMelee_ExitVs+0x24): A+B on a port replays the match, A+X replays it on a random stage,
 * otherwise the next state is the character select screen (no results). Returns the next state
 * id (vanilla_next when TM-CE is off). */
int mu_tmce_code_runback(int vanilla_next)
{
    int i;
    if (!mu_tmce_active()) {
        return vanilla_next;
    }
    for (i = 0; i < 4; i++) {
        u32 held = (u32) gm_GetButtonsPressed((u8) i);
        if (!(held & HSD_PAD_A)) {
            continue;
        }
        if (held & HSD_PAD_B) {
            return 2;
        }
        if (held & HSD_PAD_X) {
            int stkind = mnSelStageRandom();
            gmMainLib_8045A6C0.modes.table[0].start.rules.stkind = (u16) stkind;
            lbDvd_GetPreloadCacheScene()->game_cache.stkind = stkind;
            lbDvd_80018254();
            return 2;
        }
    }
    return 0;
}

/* Force Game Loop (gm_801A4D34, the "i < pad_queue_count" test of the engine loop): while the
 * lab sets EventVars.flags ForceGameLoop the loop keeps running frames without drawing. */
int mu_tmce_code_force_game_loop(void)
{
    struct {
        void* ptrs[5];
        int game_timer;
        u32 flags;
    }* ev = mu_tmce_event_vars;
    return mu_tmce_active() && ev != NULL && (ev->flags & 1);
}

/* Pad - Update During Frame Advance (gm_801A4D34+0x114): the pad update, input evaluation and
 * scene frame run even while develop mode holds the frame, so TM-CE's menu reads input. */
int mu_tmce_code_pad_update_paused(void)
{
    return mu_tmce_active();
}

/* Default Tournament Settings, Rumble Off, Music At 0% By Default (gmMainLib_DefaultGameRules
 * and gmMainLib_DefaultGamePrefs): the words the console writes over the defaults. TM-CE's
 * "Stock Mode" word is 0x00340000 (mode byte 0, time limit 0), which Enable 1P in VS Mode No Time
 * relies on; the General Codes' stock mode is replaced by it. */
void mu_tmce_code_default_rules(GameRules* rules, struct GamePrefs* prefs)
{
    int i;
    /* 803D4A48: 00340000 */
    rules->force_main_menu = 0x00;
    rules->bgm = 0x34;
    rules->mode = 0x00;
    rules->time_limit = 0x00;
    /* 803D4A4C: 04000A00 */
    rules->stock_count = 4;
    rules->handicap = 0;
    rules->damage_ratio = 0x0A;
    rules->stage_sel = 0;
    /* 803D4A50: 08010100 */
    rules->stock_time_limit = 8;
    rules->friendly_fire = 1;
    rules->pause = 1;
    rules->score_display = 0;
    /* 803D4A60: FF000000 (the three bytes after item_freq are padding) */
    prefs->item_freq = 0xFF;
    /* 803D4A70: 00000000, rumble off on every port */
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        prefs->rumble_enabled[i] = 0;
    }
    /* 803D4A74: 64000000, sound balance all effects (music 0%); the same word clears deflicker
     * and the saved language */
    prefs->sound_balance = 0x64;
    prefs->deflicker = 0;
    prefs->saved_language = 0;
    /* 803D4A78: E70000B0, tournament stages for random */
    prefs->stage_mask = 0xE70000B0;
}

/* L+R+A Returns to CSS During SSS (mnStageSel_Scene_OnFrame+0x6c): back to the current mode's
 * character select, not the main menu. */
int mu_tmce_code_sss_return_mode(int vanilla_mode)
{
    return mu_tmce_active() ? gm_GetCurrentGameMode() : vanilla_mode;
}

/* Enable 1P in VS Mode No Time (fn_80262F44+0x120): one ready player is enough in time mode with
 * no time limit. Returns the minimum ready count (2, vanilla). */
int mu_tmce_code_css_min_players(void)
{
    GameRules* rules;
    if (!mu_tmce_active()) {
        return 2;
    }
    rules = gmMainLib_GetGameRules();
    return (rules->mode == 0 && rules->time_limit == 0) ? 1 : 2;
}

/* Last Unplug Closes All CSS Doors (mnCharSel_CursorThink+0x344): nonzero when no controller is
 * plugged in (the caller then closes all four doors). */
int mu_tmce_code_css_all_unplugged(void)
{
    int i;
    if (!mu_tmce_active()) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        if (HSD_PadCopyStatus[i].err == 0) {
            return 0;
        }
    }
    return 1;
}

/* Toggle Rumble From CSS (mnCharSel_CursorThink+0x638): D-pad up turns the port's rumble on (with
 * a short rumble), down turns it off, and the cursor shakes sideways while it settles. */
void mu_tmce_code_css_rumble(int port, unsigned int pressed, unsigned char* shaking, float* x,
                             float* amplitude)
{
    float amp, flipped;
    int flip_lt;

    if (!mu_tmce_active()) {
        return;
    }
    if (*shaking == 0) {
        int enabled = GetRumbleSettingOfPort((u8) port);
        int on;
        if (pressed & HSD_PAD_DPADUP) {
            if (enabled == 1) {
                return;
            }
            HSD_PadRumbleAdd((u8) port, 0, 0xE, 0, mnVibration_804D4FF0);
            on = 1;
        } else if (pressed & HSD_PAD_DPADDOWN) {
            if (enabled == 0) {
                return;
            }
            on = 0;
        } else {
            return;
        }
        gmMainLib_SetRumbleEnabled((u8) port, on);
        *shaking = 1;
        *amplitude = -3.0F;
    }

    /* the shake: move by the amplitude, flip it, and decay it each time it turns negative */
    amp = *amplitude;
    *x = amp + *x;
    flipped = -amp;
    flip_lt = flipped < amp;
    if (!(flipped > amp)) {
        flipped = flipped * 0.8F;
    }
    *amplitude = flipped;
    if (flip_lt) {
        return;
    }
    if (flipped > 0.015625F) {
        return;
    }
    *amplitude = 0.0F;
    *shaking = 0;
}

/* Disable Rumble on Unplug (HSD_PadRenewMasterStatus+0x98, before the new error byte is stored):
 * a controller that was connected and now reports "no controller" gets its rumble setting off. */
void mu_tmce_code_pad_unplug(int port, signed char new_err, signed char old_err)
{
    if (mu_tmce_active() && new_err == -1 && old_err == 0) {
        gmMainLib_SetRumbleEnabled(port, 0);
    }
}

/* Hitbox Visualizer, Always Draw TopN For Items (mpLib_DrawSnapping+0x2bc): every item with an
 * up-to-date collision gets its TopN cross, in place of the hookshot's ECB (drawn by
 * mu_tmce_code_chain_ecbs). Returns nonzero when it drew (the caller then skips the vanilla
 * hookshot branch). The console passes an unset register as the color; the TopN color of
 * mpLib_DrawEcbs is used. */
int mu_tmce_code_item_topn(HSD_GObj* item_gobj)
{
    static const GXColor topn = { 0x80, 0x50, 0x18, 0xFF };
    Item* ip;
    if (!mu_tmce_active()) {
        return 0;
    }
    ip = GET_ITEM(item_gobj);
    mpLib_SetupDraw(topn);
    GXBegin(GX_LINES, GX_VTXFMT0, 4);
    GXPosition3f32(ip->pos.x - 1.0F, ip->pos.y, ip->pos.z);
    GXPosition3f32(1.0F + ip->pos.x, ip->pos.y, ip->pos.z);
    GXPosition3f32(ip->pos.x, ip->pos.y - 1.0F, ip->pos.z);
    GXPosition3f32(ip->pos.x, 1.0F + ip->pos.y, ip->pos.z);
    GXEnd();
    return 1;
}

/* Hitbox Visualizer, Display Chain ECBs - GObj 7 (the end of mpLib_DrawSnapping): the ECB of
 * every chain link (hookshots, GObj class 7 in p-link 10) whose collision is up to date. */
void mu_tmce_code_chain_ecbs(void)
{
    HSD_GObj* gobj;
    if (!mu_tmce_active()) {
        return;
    }
    for (gobj = HSD_GObjPLinkHead[10]; gobj != NULL; gobj = gobj->next) {
        ItemLink* link = gobj->user_data;
        if (link->coll_data.x38 == mpColl_804D64AC) {
            mpLib_DrawEcbs(&link->coll_data);
        }
    }
}
