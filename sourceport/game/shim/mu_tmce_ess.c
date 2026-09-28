/* Training Mode CE, native build: the Event Select Screen.
 *
 * TM-CE's "Event Select Screen" patches (run-source/tmce-src/ASM/training-mode/Custom Events/
 * Event Select Screen) written as C against the decomp: event pages switched with the stick or
 * D-pad left/right, each page's events and descriptions from TM-CE's own tables (events.c), page
 * sized scrolling, the X/Y jumps, the L/R button actions, the page/version texts, per-page high
 * scores. Each function is called from the decomp at the point the patch lands on, and only while
 * mu_tmce_active(). Text is built with the game's own text calls (HSD_SisLib_803A6754, _803A6B98,
 * _803A7548, _803A74F0, _803A70A0).
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include <mu_native.h>
#include <mu_tmce_ess.h>

#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gmevent.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/mn/mnevent.h>
#include <melee/mn/mnmain.h>
#include <melee/mn/mnstagesw.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/state.h>

/* ---- TM-CE's own C (prefixed exports of the eventMenu module) ---- */
char* tmce_eventMenu_GetEventName(int page, int event);
char* tmce_eventMenu_GetEventDescription(int page, int event);
char* tmce_eventMenu_GetEventFile(int page, int event);
char* tmce_eventMenu_GetPageName(int page);
int tmce_eventMenu_GetPageEventNum(int page); /* the page's last event index */
int tmce_eventMenu_GetPageNum(void);          /* the last page index */
char* tmce_eventMenu_GetTMVersLong(void);
u8 tmce_eventMenu_GetScoreType(int page, int event);
int tmce_eventMenu_GetPageEventOffset(int page);

/* the current page, a byte of the save file (tmce/native/tmce_runtime.c) */
int mu_tmce_event_page(void);
void mu_tmce_set_event_page(int page);

/* the console patches' constants */
#define ESS_ANALOG_TRIGGER_THRESHOLD 43 /* Globals.s AnalogTriggerThreshold */
#define ESS_CURSOR_ROW_Y (-1.32F)       /* Better X and Y Event Jump: the cursor's step per row */
#define ESS_SCORE_SLOTS 51              /* the save file's event score array (x1A70 + its padding) */
/* Custom ESS Button Actions: L opens the OSD toggle menu on the Random Stage Select screen. That menu
 * is TM-CE's "Onscreen Display/Toggle UI" patches, not ported yet (ledger_T2.md); until they are,
 * L would land on the vanilla Random Stage Select screen, so the action stays off. */
#define ESS_OSD_MENU_PORTED 0

static int page_is_valid(int page)
{
    return page >= 0 && page <= tmce_eventMenu_GetPageNum();
}

static int event_is_valid(int page, int event)
{
    return page_is_valid(page) && event >= 0 && event <= tmce_eventMenu_GetPageEventNum(page);
}

/* The last event index on the current page (TM_GetPageEventNum). */
static int page_last_event(void)
{
    int page = mu_tmce_event_page();
    return page_is_valid(page) ? tmce_eventMenu_GetPageEventNum(page) : 0;
}

/* Adds one subtext per line of `s` at (x, y), each line `line_h` below the last, and returns the
 * first subtext's index (-1 if none). The game's text call formats into 128-byte buffers, so each
 * line goes on its own (TM-CE's descriptions run past that as one string). */
static int add_lines(HSD_Text* text, f32 x, f32 y, f32 line_h, const char* s, int max_lines)
{
    char line[64];
    int first = -1;
    int n = 0;

    if (s == NULL) {
        return -1;
    }
    while (*s != '\0' && n < max_lines) {
        int len = 0;
        int copy;
        while (s[len] != '\0' && s[len] != '\n') {
            len++;
        }
        copy = len < (int) sizeof line - 1 ? len : (int) sizeof line - 1;
        if (copy > 0) {
            int id;
            __builtin_memcpy(line, s, (unsigned) copy);
            line[copy] = '\0';
            id = HSD_SisLib_803A6B98(text, x, y + (f32) n * line_h, "%s", line);
            if (first < 0) {
                first = id;
            }
        }
        n++;
        s += len;
        if (*s == '\n') {
            s++;
        }
    }
    return first;
}

/* ================================================================================================
 * mnEvent_8024CE74: OnlyDisplayEventsOnPage (8024CFFC) and Unlock Event 1 (8024CEC4)
 * The furthest the list scrolls: the page's last event index less the 8 rows below the top one. */
s32 mu_tmce_ess_last_scroll(void)
{
    int last = page_last_event();
    return last < 8 ? last : last - 8;
}

/* mnEvent_8024E420: Never Scroll If Less than 9 Events on Page (8024E438)
 * A page with fewer than 9 events never scrolls: the cursor goes straight to the event. */
bool mu_tmce_ess_short_page(struct MnEventData* data, s32 event_idx)
{
    if (page_last_event() >= 9) {
        return false;
    }
    data->first_event = 0;
    data->page = (u8) event_idx;
    return true;
}

/* mnEvent_8024D15C: SkipDisplayingEventNames (8024D3E8)
 * A row past the page's last event shows nothing: its "Lv." text (already removed) and its name. */
bool mu_tmce_ess_skip_row(struct MnEventData* data, s32 idx, s32 event_id)
{
    if (event_id <= page_last_event()) {
        return false;
    }
    data->texts[idx] = NULL;
    if (data->icons[idx] != NULL) {
        HSD_SisLib_803A5CC4(data->icons[idx]);
    }
    data->icons[idx] = NULL;
    return true;
}

/* mnEvent_8024D15C: Display Custom Event Names on ESS (8024D470)
 * The row's name from TM-CE's table, where the game put its own name text; grey when the event has
 * no file of its own. */
void mu_tmce_ess_event_name(struct MnEventData* data, s32 idx, s32 event_id, f32 x, f32 y)
{
    static GXColor no_file_color = { 180, 180, 180, 255 };
    int page = mu_tmce_event_page();
    HSD_Text* text = HSD_SisLib_803A6754(0, 0);

    data->icons[idx] = text;
    text->pos_x = x;
    text->pos_y = y;
    text->pos_z = 17.0F;
    text->box_size_x = 364.68332F;
    text->box_size_y = 38.38772F;
    text->font_size.x = 0.035F;
    text->font_size.y = 0.035F;
    text->default_fitting = 1;
    text->default_kerning = 1;
    if (!event_is_valid(page, event_id)) {
        return;
    }
    add_lines(text, 0.0F, 0.0F, 0.0F, tmce_eventMenu_GetEventName(page, event_id), 1);
    if (tmce_eventMenu_GetEventFile(page, event_id) == NULL) {
        HSD_SisLib_803A74F0(text, 0, &no_file_color);
    }
}

/* mnEvent_8024D7E0: Display Custom Event Descriptions (8024D80C)
 * The selected event's description from TM-CE's table, in the game's description box: kerning and
 * fitting on, grey, at 0xB3/0x100 scale (the console patch's text header). */
void mu_tmce_ess_description(struct MnEventData* data)
{
    const f32 scale = (f32) 0xB3 / 256.0F;
    int page = mu_tmce_event_page();
    int event = data->page + data->first_event;
    HSD_Text* text = HSD_SisLib_803A6754(0, 0);

    data->desc_text = text;
    text->pos_x = -9.5F;
    text->pos_y = 8.0F;
    text->pos_z = 17.0F;
    text->box_size_x = 364.68332F;
    text->box_size_y = 38.38772F;
    text->font_size.x = 0.0521F;
    text->font_size.y = 0.0521F;
    text->default_kerning = 1;
    text->default_fitting = 1;
    text->text_color.r = 0xAA;
    text->text_color.g = 0xAA;
    text->text_color.b = 0xAA;
    text->x34.x = scale;
    text->x34.y = scale;
    if (event_is_valid(page, event)) {
        /* a line is 32 units at this scale, as the renderer advances on a newline */
        add_lines(text, 0.0F, 0.0F, 32.0F * scale, tmce_eventMenu_GetEventDescription(page, event), 8);
    }
}

/* ================================================================================================
 * The page texts: Event Code Version Indicator (8024E568), Remove Version Indicator 1 and 2
 * (8024E304, 8024E3B0), Toggle Tutorial Text (8024D84C). */
static HSD_Text* page_text;  /* page name, "L = OSD", "R = Help", TM-CE's version */
static HSD_Text* left_arrow;
static HSD_Text* right_arrow;

/* the help overlay (Custom ESS Button Actions) */
static struct {
    HSD_GObj* gobj;
    HSD_Text* text;
    u8 r_ignore_timer;
} help;

static HSD_Text* new_page_text(void)
{
    HSD_Text* text = HSD_SisLib_803A6754(0, 0);
    text->default_kerning = 1;
    text->default_alignment = 1;
    text->pos_z = 17.0F;
    text->font_size.x = 0.035F;
    text->font_size.y = 0.035F;
    return text;
}

static void remove_help(void)
{
    if (help.gobj != NULL) {
        HSD_GObjFree(help.gobj);
    }
    if (help.text != NULL) {
        HSD_SisLib_803A5CC4(help.text);
    }
    help.gobj = NULL;
    help.text = NULL;
}

/* mnEvent_8024E524 (8024E568): the screen is built */
void mu_tmce_ess_create_page_texts(void)
{
    static GXColor version_color = { 0x8D, 0xFF, 0x6E, 0xFF };
    int page = mu_tmce_event_page();
    int id;

    /* a help overlay from an earlier visit went with its scene (the leave hooks close it too) */
    help.gobj = NULL;
    help.text = NULL;

    page_text = new_page_text();
    add_lines(page_text, 0.0F, -240.0F, 0.0F, page_is_valid(page) ? tmce_eventMenu_GetPageName(page) : "", 1);
    id = HSD_SisLib_803A6B98(page_text, -310.0F, -230.0F, "L = OSD");
    HSD_SisLib_803A7548(page_text, id, 0.8F, 0.8F);
    id = HSD_SisLib_803A6B98(page_text, 310.0F, -230.0F, "R = Help");
    HSD_SisLib_803A7548(page_text, id, 0.8F, 0.8F);
    id = add_lines(page_text, 0.0F, -212.0F, 0.0F, tmce_eventMenu_GetTMVersLong(), 1);
    if (id >= 0) {
        HSD_SisLib_803A7548(page_text, id, 0.6F, 0.6F);
        HSD_SisLib_803A74F0(page_text, id, &version_color);
    }

    left_arrow = new_page_text();
    id = HSD_SisLib_803A6B98(left_arrow, -210.0F, -215.0F, "<");
    HSD_SisLib_803A7548(left_arrow, id, 0.7F, 0.7F);

    right_arrow = new_page_text();
    id = HSD_SisLib_803A6B98(right_arrow, 210.0F, -215.0F, ">");
    HSD_SisLib_803A7548(right_arrow, id, 0.7F, 0.7F);
}

/* mnEvent_8024E2A0 / mnEvent_8024E34C (8024E304, 8024E3B0): the screen is left. The console leaves
 * an open help overlay behind (B is handled before the button actions); it is closed here. */
void mu_tmce_ess_remove_page_texts(void)
{
    if (page_text != NULL) {
        HSD_SisLib_803A5CC4(page_text);
    }
    if (left_arrow != NULL) {
        HSD_SisLib_803A5CC4(left_arrow);
    }
    if (right_arrow != NULL) {
        HSD_SisLib_803A5CC4(right_arrow);
    }
    page_text = NULL;
    left_arrow = NULL;
    right_arrow = NULL;
    remove_help();
}

/* mnEvent_8024D7E0 end (8024D84C): the page arrows show only where there is a page to go to */
void mu_tmce_ess_page_arrows(void)
{
    int page = mu_tmce_event_page();
    if (left_arrow != NULL) {
        left_arrow->hidden = page == 0;
    }
    if (right_arrow != NULL) {
        right_arrow->hidden = page == tmce_eventMenu_GetPageNum();
    }
}

/* ================================================================================================
 * mnEvent_8024D864: Better X and Y Event Jump (8024D998, 8024DABC, and the cursor updates at
 * 8024DA98, 8024DBB4), CannotScrollPastEventsOnPage (8024DEC8). `page` in MnEventData is the
 * cursor's row, `first_event` the scroll. */

/* X: nine events down, stopping at the page's last event. False: already there, nothing to do. */
bool mu_tmce_ess_jump_down(struct MnEventData* data)
{
    int last = page_last_event();
    int row = data->page;
    int scroll = data->first_event;

    if (row + scroll >= last) {
        return false;
    }
    if (row + scroll + 9 >= last) {
        if (last >= 9) {
            row = 8;
            scroll = last - 8;
        } else {
            row = last;
            scroll = 0;
        }
    } else {
        row += 9;
    }
    if (row > 8) {
        scroll += row - 8;
        row = 8;
    }
    if (scroll > last - 8) {
        if (last > 8) {
            row = 8;
            scroll = last - 8;
        } else {
            row = last;
            scroll = 0;
        }
    }
    data->page = (u8) row;
    data->first_event = scroll;
    lbAudioAx_80024030(2);
    return true;
}

/* Y: nine events up, stopping at the first. False: already there. */
bool mu_tmce_ess_jump_up(struct MnEventData* data)
{
    int row = data->page;
    int scroll = data->first_event;

    if (row + scroll <= 0) {
        return false;
    }
    if (row + scroll - 9 <= 0) {
        row = 0;
        scroll = 0;
    } else {
        row -= 9;
    }
    if (row < 0) {
        scroll += row;
        row = 0;
    }
    data->page = (u8) row;
    data->first_event = scroll;
    lbAudioAx_80024030(2);
    return true;
}

/* after a jump the cursor (joint 11) moves to its row */
void mu_tmce_ess_cursor_to_row(HSD_GObj* ess_gobj)
{
    MnEventData* data = ess_gobj->user_data;
    HSD_JObj* cursor;
    lb_80011E24(ess_gobj->hsd_obj, &cursor, 11, -1);
    HSD_JObjSetTranslateY(cursor, (f32) data->page * ESS_CURSOR_ROW_Y);
}

/* Down: the cursor stops on the page's last event */
bool mu_tmce_ess_down_blocked(struct MnEventData* data)
{
    return data->page >= page_last_event();
}

/* ================================================================================================
 * mnEvent_8024D864: Custom ESS Button Actions (8024D92C)
 * L (a digital click or the analog trigger passing the threshold, on any controller): the OSD
 * toggles. R: the help overlay, closed by the next press. Left/right: the previous/next page. */

/* The help overlay's QR code (the "Tutorial Video Series"), 37 by 37 cells, one bit each, rows
 * first, most significant bit first; a clear bit is a dark cell. */
static const u32 help_qr[] = {
    0x018AE33C, 0x03EBF3BA, 0xAF9142D6, 0xDD448A26, 0x655A245A, 0xE02D513E, 0xCC5ADAF8, 0x05555550,
    0x1FE8309A, 0xFF7D156E, 0x898EF548, 0xDFE14117, 0x127251C4, 0x7BCC9765, 0xDCA7E6DB, 0x5F3E3731,
    0x5E26316F, 0x9885FAAF, 0xBD51D980, 0xD095D3D4, 0xDBFF1461, 0x2C521334, 0x5454FBF5, 0xED023351,
    0x95BF1517, 0x65AB0082, 0xD7809260, 0xA19B07D5, 0x55A771F2, 0x139E8795, 0xD4301AD0, 0x289F2A9C,
    0xE72686A8, 0x4201FF87, 0xB173B006, 0xF023D5CF, 0xB13FCCEF, 0x45DAC900, 0x4A2FAD85, 0xEC916FFE,
    0x65E0FB84, 0xCFEA6017, 0x3A672F7F,
};
#define HELP_QR_CELLS 37

static int help_qr_dark(int cell)
{
    return ((help_qr[cell / 32] >> (31 - cell % 32)) & 1) == 0;
}

static void help_quad(f32 x0, f32 y0, f32 x1, f32 y1, u32 color)
{
    GXPosition2f32(x0, y0);
    GXColor1u32(color);
    GXPosition2f32(x1, y0);
    GXColor1u32(color);
    GXPosition2f32(x1, y1);
    GXColor1u32(color);
    GXPosition2f32(x0, y1);
    GXColor1u32(color);
}

/* GX link 17, pass 2, as on the console: a light square with a 3-cell border, then the dark cells.
 * The console patch drew with whatever vertex format the previous draw left; here the state is set
 * the way TM-CE's own C sets it for flat colored primitives (events.c GFX_Start). */
static void help_draw(HSD_GObj* gobj, intptr_t pass)
{
    const f32 x_min = 210.0F;
    const f32 y_min = 50.0F;
    const f32 cell = 6.0F;
    const u32 dark = 0x101010FF;
    const u32 light = 0xE0E0E0FF;
    HSD_CObj* cobj;
    Mtx mtx;
    int dark_cells = 0;
    int i;
    int x;
    int y;
    (void) gobj;

    if (pass != 2) {
        return;
    }
    cobj = HSD_CObjGetCurrent();
    if (cobj == NULL) {
        return;
    }
    for (i = 0; i < HELP_QR_CELLS * HELP_QR_CELLS; i++) {
        dark_cells += help_qr_dark(i);
    }
    HSD_ClearVtxDesc();
    GXSetCurrentMtx(0);
    HSD_CObjGetViewingMtx(cobj, mtx);
    GXLoadPosMtxImm(mtx, 0);
    HSD_SetupRenderMode(0x68000002);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    HSD_StateSetCullMode(GX_CULL_NONE);

    GXBegin(GX_QUADS, GX_VTXFMT0, (u16) (4 * (1 + dark_cells)));
    help_quad(x_min - 3.0F * cell, y_min - 3.0F * cell, x_min + 40.0F * cell, y_min + 40.0F * cell, light);
    i = 0;
    for (y = 0; y < HELP_QR_CELLS; y++) {
        for (x = 0; x < HELP_QR_CELLS; x++, i++) {
            if (help_qr_dark(i)) {
                f32 px = x_min + (f32) x * cell;
                f32 py = y_min + (f32) y * cell;
                help_quad(px, py, px + cell, py + cell, dark);
            }
        }
    }
    GXEnd();
}

static void open_help(u64 trigger)
{
    static GXColor dark = { 0x10, 0x10, 0x10, 0xFF };
    static GXColor light = { 0xE0, 0xE0, 0xE0, 0xFF };
    HSD_Text* text;
    int id;

    /* opened by the analog trigger alone: the click that usually follows is not a second press */
    if (!(trigger & PAD_TRIGGER_R)) {
        help.r_ignore_timer = 15;
    }
    help.gobj = GObj_Create(0, 0, 0);
    GObj_SetupGXLink(help.gobj, help_draw, 17, 0);

    text = HSD_SisLib_803A6754(0, 1);
    help.text = text;
    text->default_kerning = 1;
    text->text_color = dark;
    text->bg_color = light;
    text->font_size.x = 0.0306F;
    text->font_size.y = 0.0306F;
    text->pos_x = -9.69F;
    text->pos_y = -7.0F;
    id = HSD_SisLib_803A6B98(text, 90.0F, 350.0F, "TM-CE Tips");
    HSD_SisLib_803A7548(text, id, 2.0F, 2.0F);
    id = HSD_SisLib_803A6B98(text, 115.0F, 430.0F, "Tutorial Video Series");
    HSD_SisLib_803A7548(text, id, 1.0F, 1.0F);
}

/* L: the OSD toggle menu (the Random Stage Select screen, opened in its TM-CE mode 3) */
static void open_osd_menu(MnEventData* data)
{
    lbAudioAx_80024030(1);
    mn_804A04F0.entering_menu = 3;
    mn_804D6BC8.cooldown = 5;
    gm_801BEB74((u8) (data->page + data->first_event));
    mnStageSw_80237410();
    HSD_GObjFree(HSD_GObj_CurrentInvokedProcGObj);
}

/* Left/right: the page before or after, back at its first event */
static void switch_page(HSD_GObj* ess_gobj, int step)
{
    MnEventData* data = ess_gobj->user_data;
    int page = mu_tmce_event_page() + step;
    HSD_JObj* cursor;
    const char* name;
    char line[64];
    int i;

    if (page > tmce_eventMenu_GetPageNum() || page < 0) {
        return;
    }
    mu_tmce_set_event_page(page);

    name = tmce_eventMenu_GetPageName(page);
    for (i = 0; name[i] != '\0' && name[i] != '\n' && i < (int) sizeof line - 1; i++) {
        line[i] = name[i];
    }
    line[i] = '\0';
    if (page_text != NULL) {
        HSD_SisLib_803A70A0(page_text, 0, "%s", line);
    }

    data->first_event = 0;
    data->page = 0;
    for (i = 0; i < 9; i++) {
        mnEvent_8024D15C(i, i);
    }
    mnEvent_8024D7E0(ess_gobj, 0);
    mnEvent_8024D5B0(ess_gobj, 0);

    lb_80011E24(ess_gobj->hsd_obj, &cursor, 11, -1);
    HSD_JObjSetTranslateY(cursor, 0.0F);
    lbAudioAx_80024030(2);
}

/* the menu-mapped buttons (the upper word of the game's input words) released this frame */
static int mapped_release(void)
{
    const u32 mapped = PAD_BUTTON_A | PAD_BUTTON_START | PAD_BUTTON_B | PAD_BUTTON_UP | PAD_BUTTON_DOWN |
                       PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_STICK_UP | PAD_STICK_DOWN | PAD_STICK_LEFT |
                       PAD_STICK_RIGHT;
    u32 release = 0;
    int i;
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        release |= HSD_PadCopyStatus[i].release;
    }
    return (release & mapped) != 0;
}

/* True when the screen's think must stop this frame (the OSD menu took over). */
bool mu_tmce_ess_button_actions(HSD_GObj* ess_gobj)
{
    static u8 prev_analog_l;
    static u8 prev_analog_r;
    u8 analog_l = 0;
    u8 analog_r = 0;
    int press_l;
    int press_r;
    u64 trigger;
    int i;

    /* the analog triggers, highest of the four controllers, count as a press when they cross */
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (HSD_PadMasterStatus[i].analogL >= analog_l) {
            analog_l = HSD_PadMasterStatus[i].analogL;
        }
        if (HSD_PadMasterStatus[i].analogR >= analog_r) {
            analog_r = HSD_PadMasterStatus[i].analogR;
        }
    }
    press_l = prev_analog_l < ESS_ANALOG_TRIGGER_THRESHOLD && analog_l >= ESS_ANALOG_TRIGGER_THRESHOLD;
    press_r = prev_analog_r < ESS_ANALOG_TRIGGER_THRESHOLD && analog_r >= ESS_ANALOG_TRIGGER_THRESHOLD;
    prev_analog_l = analog_l;
    prev_analog_r = analog_r;

    trigger = gm_GetButtonsTriggered(PAD_MAX_CONTROLLERS);
    if (trigger & PAD_TRIGGER_L) {
        press_l = 1;
    }
    if (trigger & PAD_TRIGGER_R) {
        press_r = 1;
    }

    if (help.gobj != NULL) {
        if (help.r_ignore_timer != 0) {
            help.r_ignore_timer -= 1;
            if (trigger & PAD_TRIGGER_R) {
                press_r = 0;
            }
        }
        if ((u32) (trigger >> 32) != 0 || press_l || press_r || mapped_release()) {
            remove_help();
        }
        return false;
    }

    if (press_l) {
        if (ESS_OSD_MENU_PORTED) {
            open_osd_menu(ess_gobj->user_data);
            return true;
        }
        return false;
    }
    if (press_r) {
        open_help(trigger);
        return false;
    }
    {
        u64 rapid = gm_801A36C0(PAD_MAX_CONTROLLERS);
        if (rapid & PAD_ANY_LEFT) {
            switch_page(ess_gobj, -1);
        } else if (rapid & PAD_ANY_RIGHT) {
            switch_page(ess_gobj, 1);
        }
    }
    return false;
}

/* ================================================================================================
 * mnMain_Scene_OnEnter: Remove Event Texture (8022E5E4)
 * Two materials of MnMaAll (the event screen's background and its subject table) made fully
 * transparent: the alpha (+0xC) of the materials at 0x1E754 and 0x1EC88 in the file. The console
 * patch finds the file's start from the heap layout and writes each twice, one byte apart, to cover
 * both disc languages' file sizes; here the file's start is known. */
void mu_tmce_ess_remove_event_texture(HSD_Archive* archive)
{
    static const u32 alpha_offsets[] = { 0x1E754 + 0xC, 0x1EC88 + 0xC };
    u8* file;
    unsigned i;

    if (archive == NULL || archive->top_ptr == NULL) {
        return;
    }
    file = archive->top_ptr;
    for (i = 0; i < sizeof alpha_offsets / sizeof alpha_offsets[0]; i++) {
        if (alpha_offsets[i] + 4 <= archive->header.file_size) {
            __builtin_memset(file + alpha_offsets[i], 0, 4);
        }
    }
}

/* ================================================================================================
 * gmMainLib_8015CF5C / gmMainLib_8015CF70: Load / Save Event High Scores (8015CF60, 8015CF74)
 * Each page keeps its scores after the pages before it (TM_GetPageEventOffset). */
static s32* score_slot(s32 event)
{
    int page = mu_tmce_event_page();
    int index = (page_is_valid(page) ? tmce_eventMenu_GetPageEventOffset(page) : 0) + event;
    if (index < 0 || index >= ESS_SCORE_SLOTS) {
        return NULL;
    }
    return &gmMainLib_GetCardData()->save_data.x1A70[0] + index;
}

s32 mu_tmce_ess_load_score(s32 event)
{
    s32* slot = score_slot(event);
    return slot != NULL ? *slot : 0;
}

void mu_tmce_ess_save_score(s32 event, s32 score)
{
    s32* slot = score_slot(event);
    if (slot != NULL) {
        *slot = score;
    }
}

/* gm_801BEB8C: Custom Event Highscore Types (801BEB8C)
 * The event's score kind (TM-CE SCORETYPE_KO / SCORETYPE_TIME) from TM-CE's table. */
u8 mu_tmce_ess_score_type(u8 event)
{
    int page = mu_tmce_event_page();
    return event_is_valid(page, event) ? tmce_eventMenu_GetScoreType(page, event) : 0;
}
