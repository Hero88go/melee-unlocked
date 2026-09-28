/* Training Mode CE, native build: the on-screen displays' MexTK side.
 *
 * On the console TM-CE's on-screen displays (ASM/training-mode/Onscreen Display) are PowerPC patches
 * that print through the event menu's Message_Display, read the OSD toggles from the memory card
 * data and keep a small message area of their own in the score display's static struct. Natively
 * the fighter-side logic is C in the game (shim/mu_tmce_osd.c, decomp types); what needs TM-CE's own
 * types (EventVars, MsgData, Text, the memory card twin) is here, compiled with the runtime module.
 * Every function here is reached only from hooks that already checked mu_tmce_active().
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "../MexTK/mex.h"

/* The head of TM-CE's EventVars (src/events.h), up to Message_Display, and of its MsgData: only
 * these fields are used here, so events.h (and the savestate and menu types it pulls in) is not
 * needed. Same field types in the same order, so the native layout is the same. */
typedef struct OsdEventVars {
    void *event_desc;
    void *menu_assets;
    GOBJ *event_gobj;
    GOBJ *menu_gobj;
    u8 *rng; /* RNGControl: peach_item, peach_fsmash, luigi_misfire, gnw_hammer, nana_throw */
    int game_timer;
    u32 flags;
    void *Savestate_Save_v1;
    void *Savestate_Load_v1;
    GOBJ *(*Message_Display)(int msg_kind, int queue_num, int msg_color, char *format, ...);
} OsdEventVars;
typedef struct OsdMsgData {
    Text *text;
} OsdMsgData;
extern void *mu_tmce_event_vars; /* tmce_runtime.c: the event menu's EventVars */

/* ---- the event menu's messages ---- */

/* Set once TM-CE's OnStartMelee has run (it creates the message manager Message_Display uses). */
static int osd_ready;

void mu_tmce_osd_start_melee(void)
{
    osd_ready = 1;
}

/* TM_OSDEnabled (memory card data +0x1F24): one bit per OSD id */
unsigned mu_tmce_osd_enabled(void)
{
    return stc_memcard->TM_OSDEnabled;
}

/* The byte after it (+0x1F28): the legacy message system's window count, now the OSD position */
int mu_tmce_osd_max_windows(void)
{
    return stc_memcard->TM_OSDPosition;
}

/* Misc/On No Save (the memory card scene found no save): TM-CE's defaults in the new save */
void mu_tmce_osd_no_save_defaults(void)
{
    stc_memcard->TM_OSDPosition = 1; /* OSDMaxWindows */
    stc_memcard->TM_EventPage = 1;
    stc_memcard->TM_OSDRecommended = 0;
    stc_memcard->TM_OSDEnabled = 0;
}

/* Message_Display for text already formatted on the game side. Returns the message GOBJ. */
void *mu_tmce_osd_show(int kind, int queue, int color, const char *text)
{
    OsdEventVars *ev = mu_tmce_event_vars;
    if (!osd_ready || ev == 0 || ev->Message_Display == 0)
        return 0;
    return ev->Message_Display(kind, queue, color, "%s", (char *) text);
}

/* Text_ChangeTextColor on one line of a message (console color word 0xRRGGBBAA) */
void mu_tmce_osd_msg_color(void *msg_gobj, int subtext, unsigned rgba)
{
    GOBJ *gobj = msg_gobj;
    if (gobj == 0)
        return;
    OsdMsgData *data = gobj->userdata;
    GXColor color = {rgba >> 24, rgba >> 16, rgba >> 8, rgba};
    Text_SetColor(data->text, subtext, &color);
}

/* Character Randomness (Misc): the lab's pick for one RNG setting, 0 = the game's own roll */
int mu_tmce_osd_rng(int setting)
{
    OsdEventVars *ev = mu_tmce_event_vars;
    if (ev == 0 || ev->rng == 0)
        return 0;
    return ev->rng[setting];
}

/* ---- the legacy message system (Message Display System/Create Message Struct, Display Message 2.0)
 * On the console this lives in the score display's static struct at 804a1f58: its first word is
 * the HUD text canvas, the next one points to a 0x300 byte area of message queues, and the rest
 * holds the L-cancel and dashback rates and the paused stick text. TM-CE disables the score
 * display that normally owns that struct. Natively the same data is kept here. */

typedef struct LegacyMsg {
    Text *text;
    u16 timer;
    u16 id;
} LegacyMsg;

typedef struct LegacyArea {
    LegacyMsg player[4][5]; /* areas 1 and 2 (above the HUD, top of screen): 0x28 per player */
    LegacyMsg single;       /* area 3 (event popup) */
} LegacyArea;

static int legacy_canvas;
static LegacyArea *legacy_areas; /* [3], allocated per match like the console's 0x300 bytes */

/* the rest of the console struct: LC rates (+0x50), DB rates (+0x70), paused stick text (+8, +C) */
static u16 lc_rates[4][2];
static u16 db_rates[4][2];
static Text *stick_text;
static GOBJ *stick_gobj;

u16 *mu_tmce_osd_lc_rate(int ply)
{
    return lc_rates[ply & 3];
}

u16 *mu_tmce_osd_db_rate(int ply)
{
    return db_rates[ply & 3];
}

static LegacyMsg *legacy_player(int area, int ply)
{
    return legacy_areas[area].player[ply & 3];
}

static void legacy_move(LegacyMsg *msg, float dy)
{
    if (msg->text != 0)
        msg->text->trans.Y += dy;
}

/* one frame of a message's timer: false once it runs out (a timer of 0 runs out too) */
static int legacy_tick(LegacyMsg *msg)
{
    int t = msg->timer - 1;
    msg->timer = (u16) t;
    return t > 0;
}

/* MessageThink_Updater: count down each message, remove expired ones and move the later ones up
 * (the shift copies without clearing the last slot, as the console does) */
static void legacy_update(int area, int ply)
{
    static const float shift[2] = {4.5f, -4.5f};
    if (area >= 2) {
        LegacyMsg *msg = &legacy_areas[area].single;
        if (msg->text == 0 || legacy_tick(msg))
            return;
        Text_Destroy(msg->text);
        msg->text = 0;
        msg->timer = 0;
        msg->id = 0;
        return;
    }
    LegacyMsg *msgs = legacy_player(area, ply);
    for (int i = 0; i < 5; i++) {
        if (msgs[i].text == 0 || legacy_tick(&msgs[i]))
            continue;
        Text_Destroy(msgs[i].text);
        msgs[i].text = 0;
        msgs[i].timer = 0;
        msgs[i].id = 0;
        for (int j = i + 1; j < 5; j++) {
            msgs[j - 1] = msgs[j];
            legacy_move(&msgs[j - 1], shift[area]);
        }
    }
}

static void legacy_think(GOBJ *gobj)
{
    (void) gobj;
    if (legacy_areas == 0)
        return;
    for (int ply = 0; ply < 4; ply++)
        legacy_update(0, ply);
    for (int ply = 0; ply < 4; ply++)
        legacy_update(1, ply);
    legacy_update(2, 0);
}

/* un_802FF498 (the score display's init, at each match): the canvas is the one it just made.
 * Create Message Struct: allocate the queues and their think function; the rest of the console
 * struct is zeroed there too. */
void mu_tmce_osd_hud_init(int canvas)
{
    legacy_canvas = canvas;
    legacy_areas = HSD_MemAlloc(3 * sizeof(LegacyArea));
    if (legacy_areas != 0)
        __builtin_memset(legacy_areas, 0, 3 * sizeof(LegacyArea));
    __builtin_memset(lc_rates, 0, sizeof lc_rates);
    __builtin_memset(db_rates, 0, sizeof db_rates);
    stick_text = 0;
    stick_gobj = 0;
    GOBJ *gobj = GObj_Create(0xE, 7, 0);
    GObj_AddProc(gobj, legacy_think, 0);
}

/* the player's HUD position (ifAll 804a0ff0 + ply * 0xC) */
Vec3 *mu_tmce_g_hud_position(int idx) __asm__("ifAll_GetPlayerHUDPosition");

typedef struct LegacyTextProps {
    int use_hud;
    float y;
    float x;
    float size;
    float bg_y;
    float bg_x_stretch;
    float bg_y_stretch;
} LegacyTextProps;

static const LegacyTextProps legacy_props[3] = {
    {1, 11.0f, 14.0f, 0.04f, -410.0f, 12.0f, 28.0f},  /* area 1, above the HUD (X base -21) */
    {1, -20.0f, 14.0f, 0.04f, -410.0f, 9.5f, 28.0f},  /* area 2, top of the screen */
    {0, -15.0f, 19.0f, 0.04f, -410.0f, 13.5f, 28.0f}, /* area 3, event popup */
};

/* Display Message 2.0 (0x80005928): a new text in the player's queue of `area`, replacing the one
 * with the same window id and pushing the older ones down. Returns the text for the caller to
 * fill (the legacy UnclePunch events and the combo counter use it). */
void *mu_tmce_legacy_message(int ply, int timeout, int area, int window_id)
{
    static const float dup_shift[2] = {4.5f, -4.5f};
    static const float queue_shift[2] = {-4.5f, 4.5f};
    LegacyMsg *msgs;
    LegacyMsg *slot;
    int max = mu_tmce_osd_max_windows();
    if (legacy_areas == 0 || area < 0 || area > 2)
        return 0;
    if (max > 4)
        max = 4;
    if (area >= 2) {
        slot = &legacy_areas[area].single;
        if (slot->text != 0)
            Text_Destroy(slot->text);
    } else {
        msgs = legacy_player(area, ply);
        /* CheckForDuplicates: remove the message with this id and move the later ones up (both
         * loops run their body once before testing, as the console's do) */
        int i = 0;
        do {
            if (msgs[i].text != 0 && msgs[i].id == (u16) window_id) {
                Text_Destroy(msgs[i].text);
                msgs[i].text = 0;
                msgs[i].timer = 0;
                msgs[i].id = 0;
                int j = i + 1;
                do {
                    if (j < 5) {
                        msgs[j - 1] = msgs[j];
                        msgs[j].text = 0;
                        msgs[j].timer = 0;
                        msgs[j].id = 0;
                        legacy_move(&msgs[j - 1], dup_shift[area]);
                    }
                } while (++j < max);
                break;
            }
        } while (++i < max);
        /* ShiftQueue: drop the window at `max` and push windows 0..max down one (the first move
         * copies the emptied slot, clearing the one after it) */
        if (msgs[max].text != 0) {
            Text_Destroy(msgs[max].text);
            msgs[max].text = 0;
            msgs[max].timer = 0;
            msgs[max].id = 0;
        }
        for (int j = max; max != 0 && j >= 0; j--) {
            if (j + 1 >= 5)
                continue;
            msgs[j + 1] = msgs[j];
            msgs[j].text = 0;
            msgs[j].timer = 0;
            msgs[j].id = 0;
            legacy_move(&msgs[j + 1], queue_shift[area]);
        }
        slot = &msgs[0];
    }

    const LegacyTextProps *props = &legacy_props[area];
    Text *text = Text_CreateText(2, legacy_canvas);
    slot->text = text;
    slot->timer = (u16) timeout;
    slot->id = (u16) window_id;
    text->align = 1;
    text->kerning = 1;
    text->trans.X = props->use_hud ? mu_tmce_g_hud_position(ply & 3)->X : props->x;
    text->trans.Y = props->y;
    text->viewport_scale.X = props->size;
    text->viewport_scale.Y = props->size;
    Text_AddSubtext(text, 0.0f, props->bg_y, "\x81\x5B");
    GXColor black = {0, 0, 0, 0};
    Text_SetColor(text, 0, &black);
    Text_SetScale(text, 0, props->bg_x_stretch, props->bg_y_stretch);
    return text;
}

/* Combo Counter's HitstunMonitor, combo ended: the combo message (legacy window 13) of the
 * attacker `ply` turns green and lasts one more second. */
void mu_tmce_osd_legacy_combo_end(int ply)
{
    if (legacy_areas == 0)
        return;
    LegacyMsg *msgs = legacy_player(0, ply);
    for (int i = 0; i < 5; i++) {
        if (msgs[i].id == 13) {
            GXColor green = {0x8d, 0xff, 0x6e, 0xff};
            msgs[i].timer = 60;
            Text_SetColor(msgs[i].text, 1, &green);
            Text_SetColor(msgs[i].text, 2, &green);
            return;
        }
    }
}

/* ---- Misc/Display Stick Info When Paused (event mode): the pausing player's stick, per frame ---- */

extern char mu_tmce_g_pad_game_status[] __asm__("HSD_PadGameStatus"); /* 804c21cc, 0x44 per port */
extern void *mu_tmce_ref_stc_match; /* the match controller (gmvs.c): +1 is the player who paused */

#define STICK_POS_GREEN 0x8dff6effu
#define STICK_NEG_RED 0xffa2baffu

static const char *stick_pad(void)
{
    int who = ((u8 *) mu_tmce_ref_stc_match)[1];
    return mu_tmce_g_pad_game_status + (who & 3) * 0x44;
}

static void stick_line(int subtext, s8 raw, float nml)
{
    GXColor color;
    unsigned rgba = raw < 0 ? STICK_NEG_RED : STICK_POS_GREEN;
    color.r = rgba >> 24;
    color.g = rgba >> 16;
    color.b = rgba >> 8;
    color.a = rgba;
    Text_SetColor(stick_text, subtext, &color);
    int value = raw < 0 ? -raw : raw;
    Text_SetText(stick_text, subtext, subtext == 1 ? "X: %4.4f \x81\x5e %d" : "Y: %4.4f \x81\x5e %d",
                 (double) __builtin_fabsf(nml), value);
}

static void stick_think(GOBJ *gobj)
{
    (void) gobj;
    if (stick_text == 0)
        return;
    const char *pad = stick_pad();
    stick_line(1, (s8) pad[0x18], *(const float *) (pad + 0x20));
    stick_line(2, (s8) pad[0x19], *(const float *) (pad + 0x24));
}

/* gm_801A0FEC (pause opens) */
void mu_tmce_osd_pause_stick_create(void)
{
    if (stc_scene_info->major_curr != 0x2B) /* event mode only */
        return;
    const char *pad = stick_pad();
    Text *text = Text_CreateText(2, legacy_canvas);
    stick_text = text;
    text->kerning = 0;
    text->align = 0;
    text->color = (GXColor){0, 0, 0, 0};
    int sub = Text_AddSubtext(text, -29.0f, -8.0f, "\x81\x5B");
    Text_SetScale(text, sub, 0.50f, 1.08f);
    text->kerning = 1;
    sub = Text_AddSubtext(text, -27.0f, -23.0f, "X: %4.4f \x81\x5e %d", (double) *(const float *) (pad + 0x20),
                          (int) (u8) pad[0x18]);
    Text_SetScale(text, sub, 0.05f, 0.05f);
    sub = Text_AddSubtext(text, -27.0f, -21.0f, "Y: %4.4f \x81\x5e %d", (double) *(const float *) (pad + 0x24),
                          (int) (u8) pad[0x19]);
    Text_SetScale(text, sub, 0.05f, 0.05f);
    stick_gobj = GObj_Create(6, 0, 128);
    GObj_AddProc(stick_gobj, stick_think, 10);
}

/* gm_801A10FC (pause closes) */
void mu_tmce_osd_pause_stick_remove(void)
{
    if (stc_scene_info->major_curr != 0x2B)
        return;
    if (stick_text != 0)
        Text_Destroy(stick_text);
    GOBJ *gobj = stick_gobj;
    stick_text = 0;
    stick_gobj = 0;
    if (gobj != 0)
        GObj_Destroy(gobj);
}
