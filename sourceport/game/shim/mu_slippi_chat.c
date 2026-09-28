/* Native Slippi online CSS chat (the chat part of SlippiCSS.dat, major 8 state 0).
 *
 * SlippiCSS.dat is a relocatable PowerPC module that Slippi's scene-extension codeset wraps around
 * the CSS scene: its load runs after mnCharSel_Scene_OnEnter, its think after
 * mnCharSel_Scene_OnFrame. This file is the native form of its chat: the player settings fetch (C3,
 * 4 players x 16 messages x 51 bytes), the D-pad chat window built from the slpCSS chat_select
 * joint, BB, the incoming message bubbles (slpCSS chat_msg joint set, driven by the chat fields of
 * the CSS data table's MSRB), the chat-window byte and the sounds; and the module's Zelda/Sheik
 * selector (slpCSS sheik_selector joint under the CSS portrait). Constants, orderings and object
 * setups follow the module's disassembly (GObj classes, links, priorities, frame counts, positions,
 * colours, the SIS string it builds for a bubble). Rank display (E3/E4) is not ported: Melee
 * Unlocked has no Ranked play.
 *
 * mncharsel.c calls mu_slippi_chat_enter at the end of mnCharSel_Scene_OnEnter and
 * mu_slippi_chat_frame at the end of mnCharSel_Scene_OnFrame. Nothing runs unless
 * mu_slippi_menus_enabled() and the online CSS predicate hold.
 */
#include <string.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gmtoulib.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>
#include <melee/gr/ground.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/axdriver.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/sislib.h>
#include <mu_native.h>

enum {
    CMD_GET_PLAYER_SETTINGS = 0xC3,
    CHAT_PLAYERS = 4,
    CHAT_MESSAGES = 16,
    CHAT_MESSAGE_LEN = 51,
    CHAT_WINDOW_FRAMES = 150,          /* 2.5 s without input closes the window */
    CHAT_ALLOW_COMMAND_FRAMES = 12,    /* 0.2 s between two sends */
    CHAT_FRAMES = 20,                  /* bubble show/hide animation length */
    NOTIFICATION_SET_LENGTH = 10,
    CHAT_MAX_PLAYER_MESSAGES = 4,
    CHAT_MESSAGE_ID_DISABLED = 0x10,
    SND_OPEN_WINDOW = 2,               /* common sounds */
    SND_BLOCK_MESSAGE = 3,
    SND_NEW_MESSAGE = 0xB7,            /* global sound id */
    SYSTEXT_SIZE = 400,
    CHAR_MAP_SIZE = 287,
    /* MSRB (console layout, MuMatchState.raw) */
    MSRB_MM_STATE = 0,
    MSRB_LOCAL_IDX = 3,
    MSRB_USR_CHAT = 10,
    MSRB_OPP_CHAT = 11,
    MSRB_CHAT_PLAYER = 12,
    MSRB_LOCAL_NAME = 15,
    MSRB_P1_NAME = 46,
    MSRB_NAME_LEN = 31,
    MSRB_SIZE = 962,
    MM_CONNECTION_SUCCESS = 4,
};

enum { STATE_NONE, STATE_STARTING, STATE_IDLE, STATE_CLEANUP };

/* SIS text opcodes. */
enum {
    SIS_COLOR = 0x0C,
    SIS_LEFT = 0x12,
    SIS_KERN = 0x16,
    SIS_SPACE = 0x1A,
    SIS_COMMON_CHAR = 0x20,
};

/* slpCSS symbol: chat_select is a struct whose first word is the window joint; chat_msg is a
 * joint set (joint, anims, matanims, shapeanims). */
typedef struct ChatWindowDesc {
    DISC_PTR(HSD_Joint) joint;
} DISC_STRUCT ChatWindowDesc;

typedef struct SlpCssChat {
    DISC_PTR(ChatWindowDesc) chat_window;
    DISC_PTR(DynamicModelDesc) chat_message;
    DISC_PTR(void) mode;
    DISC_PTR(void) connect_help;
    DISC_PTR(void) rank_icons;
    DISC_PTR(HSD_Joint) sheik_selector;
} DISC_STRUCT SlpCssChat;

typedef struct ChatWindowData {
    HSD_Text* text;
    SlpCssChat* slpcss;
    int group_id;
    int frames_left;
    int delayed_frames;
} ChatWindowData;

typedef struct NotificationMessage {
    DynamicModelDesc* jobj_set;
    int type;
    int state;
    u8* text_data;   /* SIS string the bubble text points into */
    HSD_Text* text;
    int id;
    int animation_frames;
    int frames_left;
    int player_index;
    int message_id;
} NotificationMessage;

/* ExiSlippi_GetPlayerSettings_Response. The module allocates it per CSS load. */
static u8 player_settings[CHAT_PLAYERS][CHAT_MESSAGES][CHAT_MESSAGE_LEN];

static HSD_GObj* notifications_gobj;
static HSD_GObj* chat_notifications_gobj;
static HSD_GObj* chat_main_gobj;
static HSD_GObj* chat_window_gobj;
static int notification_set[NOTIFICATION_SET_LENGTH];
static int last_notification_id = -1;
static int chat_local_count;
static int chat_remote_count;

/* SysText's character tables as the module has them: CHAR_MAP (code points) and HEX_MAP
 * (Shift-JIS). Index i becomes the SIS "common character" i. */
static const u16 char_map[CHAR_MAP_SIZE] = {
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x0041, 0x0042,
    0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E,
    0x004F, 0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005A,
    0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B, 0x006C,
    0x006D, 0x006E, 0x006F, 0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078,
    0x0079, 0x007A, 0x3041, 0x3042, 0x3043, 0x3044, 0x3045, 0x3046, 0x3047, 0x3048, 0x3049, 0x304A,
    0x304B, 0x304C, 0x304D, 0x304E, 0x304F, 0x3050, 0x3051, 0x3052, 0x3053, 0x3054, 0x3055, 0x3056,
    0x3057, 0x3058, 0x3059, 0x305A, 0x305B, 0x305C, 0x305D, 0x305E, 0x305F, 0x3060, 0x3061, 0x3062,
    0x3063, 0x3064, 0x3065, 0x3066, 0x3067, 0x3068, 0x3069, 0x306A, 0x306B, 0x306C, 0x306D, 0x306E,
    0x306F, 0x3070, 0x3071, 0x3072, 0x3073, 0x3074, 0x3075, 0x3076, 0x3077, 0x3078, 0x3079, 0x307A,
    0x307B, 0x307C, 0x307D, 0x307E, 0x307F, 0x3080, 0x3081, 0x3082, 0x3083, 0x3084, 0x3085, 0x3086,
    0x3087, 0x3088, 0x3089, 0x308A, 0x308B, 0x308C, 0x308D, 0x308E, 0x308F, 0x3092, 0x3093, 0x30A1,
    0x30A2, 0x30A3, 0x30A4, 0x30A5, 0x30A6, 0x30A7, 0x30A8, 0x30A9, 0x30AA, 0x30AB, 0x30AC, 0x30AD,
    0x30AE, 0x30AF, 0x30B0, 0x30B1, 0x30B2, 0x30B3, 0x30B4, 0x30B5, 0x30B6, 0x30B7, 0x30B8, 0x30B9,
    0x30BA, 0x30BB, 0x30BC, 0x30BD, 0x30BE, 0x30BF, 0x30C0, 0x30C1, 0x30C2, 0x30C3, 0x30C4, 0x30C5,
    0x30C6, 0x30C7, 0x30C8, 0x30C9, 0x30CA, 0x30CB, 0x30CC, 0x30CD, 0x30CE, 0x30CF, 0x30D0, 0x30D1,
    0x30D2, 0x30D3, 0x30D4, 0x30D5, 0x30D6, 0x30D7, 0x30D8, 0x30D9, 0x30DA, 0x30DB, 0x30DC, 0x30DD,
    0x30DE, 0x30DF, 0x30E0, 0x30E1, 0x30E2, 0x30E3, 0x30E4, 0x30E5, 0x30E6, 0x30E7, 0x30E8, 0x30E9,
    0x30EA, 0x30EB, 0x30EC, 0x30ED, 0x30EE, 0x30EF, 0x30F2, 0x30F3, 0x30F4, 0x30F5, 0x30F6, 0x3000,
    0x3001, 0x3002, 0x002C, 0x002E, 0x2022, 0x003A, 0x003B, 0x003F, 0x0021, 0x005E, 0x005F, 0x2014,
    0x002F, 0x007E, 0x007C, 0x0027, 0x0022, 0x0028, 0x0029, 0x005B, 0x005D, 0x007B, 0x007D, 0x002B,
    0x002D, 0x00D7, 0x003D, 0x003C, 0x003E, 0x00A5, 0x0024, 0x0025, 0x0023, 0x0026, 0x002A, 0x0040,
    0x6271, 0x62BC, 0x8ECD, 0x6E90, 0x500B, 0x8FBC, 0x6307, 0x793A, 0x53D6, 0x66F8, 0x8A73, 0x4EBA,
    0x751F, 0x8AAC, 0x4F53, 0x56E3, 0x96FB, 0x8AAD, 0x767A, 0x629C, 0x9591, 0x672C, 0x660E,
};

static const u16 hex_map[CHAR_MAP_SIZE] = {
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x0041, 0x0042,
    0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E,
    0x004F, 0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005A,
    0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B, 0x006C,
    0x006D, 0x006E, 0x006F, 0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078,
    0x0079, 0x007A, 0x829F, 0x82A0, 0x82A1, 0x82A2, 0x82A3, 0x82A4, 0x82A5, 0x82A6, 0x82A7, 0x82A8,
    0x82A9, 0x82AA, 0x82AB, 0x82AC, 0x82AD, 0x82AE, 0x82AF, 0x82B0, 0x82B1, 0x82B2, 0x82B3, 0x82B4,
    0x82B5, 0x82B6, 0x82B7, 0x82B8, 0x82B9, 0x82BA, 0x82BB, 0x82BC, 0x82BD, 0x82BE, 0x82BF, 0x82C0,
    0x82C1, 0x82C2, 0x82C3, 0x82C4, 0x82C5, 0x82C6, 0x82C7, 0x82C8, 0x82C9, 0x82CA, 0x82CB, 0x82CC,
    0x82CD, 0x82CE, 0x82CF, 0x82D0, 0x82D1, 0x82D2, 0x82D3, 0x82D4, 0x82D5, 0x82D6, 0x82D7, 0x82D8,
    0x82D9, 0x82DA, 0x82DB, 0x82DC, 0x82DD, 0x82DE, 0x82DF, 0x82E0, 0x82E1, 0x82E2, 0x82E3, 0x82E4,
    0x82E5, 0x82E6, 0x82E7, 0x82E8, 0x82E9, 0x82EA, 0x82EB, 0x82EC, 0x82ED, 0x82F0, 0x82F1, 0x8340,
    0x8341, 0x8342, 0x8343, 0x8344, 0x8345, 0x8346, 0x8347, 0x8348, 0x8349, 0x834A, 0x834B, 0x834C,
    0x834D, 0x834E, 0x834F, 0x8350, 0x8351, 0x8352, 0x8353, 0x8354, 0x8355, 0x8356, 0x8357, 0x8358,
    0x8359, 0x835A, 0x835B, 0x835C, 0x835D, 0x835E, 0x835F, 0x8360, 0x8361, 0x8362, 0x8363, 0x8364,
    0x8365, 0x8366, 0x8367, 0x8368, 0x8369, 0x836A, 0x836B, 0x836C, 0x836D, 0x836E, 0x836F, 0x8370,
    0x8371, 0x8372, 0x8373, 0x8374, 0x8375, 0x8376, 0x8377, 0x8378, 0x8379, 0x837A, 0x837B, 0x837C,
    0x837D, 0x837E, 0x8380, 0x8381, 0x8382, 0x8383, 0x8384, 0x8385, 0x8386, 0x8387, 0x8388, 0x8389,
    0x838A, 0x838B, 0x838C, 0x838D, 0x838E, 0x838F, 0x8392, 0x8393, 0x8394, 0x8395, 0x8396, 0x8140,
    0x8141, 0x8142, 0x8143, 0x8144, 0x8145, 0x8146, 0x8147, 0x8148, 0x8149, 0x814F, 0x8151, 0x817D,
    0x815E, 0x8160, 0x8162, 0x8166, 0x8168, 0x8169, 0x816A, 0x816D, 0x816E, 0x816F, 0x8170, 0x817B,
    0x817C, 0x817E, 0x8181, 0x8183, 0x8184, 0x815F, 0x8190, 0x8193, 0x8194, 0x8195, 0x8196, 0x8197,
    0x88B5, 0x899F, 0x8C52, 0x8CB9, 0x8CC2, 0x8D9E, 0x8E77, 0x8EA6, 0x8EE6, 0x8F91, 0x8FDA, 0x906C,
    0x90B6, 0x90E0, 0x91CC, 0x9263, 0x9364, 0x93C7, 0x94AD, 0x94B2, 0x8AD5, 0x967B, 0x96BE,
};

/* Text.h MSG_COLORS */
static const GXColor color_white = { 255, 255, 255, 255 };
static const GXColor color_window = { 255, 234, 47, 255 };
static const GXColor color_window_idle = { 201, 195, 135, 255 };

static const char* const header_strings[4] = { "Page: Up", "Page: Left", "Page: Right",
                                               "Page: Down" };

/* ---- shared state ---- */

static SlpCssChat* slpcss(void)
{
    return (SlpCssChat*) mu_slippi_css_slpcss();
}

/* GetSlpCSSDT()->msrb: the CSS data table's copy, refreshed by FetchMatchInfo. */
static const u8* msrb(void)
{
    return mu_slippi_css_msrb()->raw;
}

/* strlen on the console buffer: bounded by the end of the MSRB copy. */
static int msrb_strlen(const u8* s)
{
    const u8* end = msrb() + MSRB_SIZE;
    int n = 0;
    while (s + n < end && s[n] != 0) {
        n++;
    }
    return n;
}

static int msrb_strcmp(const u8* a, const u8* b)
{
    const u8* end = msrb() + MSRB_SIZE;
    while (a < end && b < end && *a != 0 && *a == *b) {
        a++;
        b++;
    }
    if (a >= end || b >= end) {
        return 0;
    }
    return (int) *a - (int) *b;
}

static int is_widescreen(void)
{
    return 0;   /* r13-0x5020: only Slippi's widescreen code sets it; the port never does */
}

static int is_connected(void)
{
    return msrb()[MSRB_MM_STATE] == MM_CONNECTION_SUCCESS;
}

/* GetRemotePlayerCount: names that are set and are not the local name. */
static int remote_player_count(void)
{
    const u8* m = msrb();
    int i, n = 0;
    for (i = 0; i < 4; i++) {
        const u8* name = m + MSRB_P1_NAME + i * MSRB_NAME_LEN;
        if (msrb_strlen(name) > 0 && msrb_strcmp(name, m + MSRB_LOCAL_NAME) != 0) {
            n++;
        }
    }
    return n;
}

/* ---- chat text (Core/Notifications/Chat/Text.c) ---- */

/* GetGroupIndex: up 0, left 1, right 2, down 3, else -1. */
static int group_index(int group_id)
{
    switch (group_id) {
    case HSD_PAD_DPADUP: return 0;
    case HSD_PAD_DPADLEFT: return 1;
    case HSD_PAD_DPADRIGHT: return 2;
    case HSD_PAD_DPADDOWN: return 3;
    default: return -1;
    }
}

/* GetChatText: the settings string of a group's message (by D-pad bit, or by 1-based index). */
static const u8* chat_text(int group_id, int message_id, int player_idx, int use_message_index)
{
    int index = message_id;
    int slot;
    if (!use_message_index) {
        switch (message_id) {
        case HSD_PAD_DPADUP: index = 1; break;
        case HSD_PAD_DPADLEFT: index = 2; break;
        case HSD_PAD_DPADRIGHT: index = 3; break;
        case HSD_PAD_DPADDOWN: index = 4; break;
        default: break;
        }
    }
    slot = group_index(group_id) * 4 + (index - 1);
    if (player_idx < 0 || player_idx >= CHAT_PLAYERS || slot < 0 || slot >= CHAT_MESSAGES) {
        return (const u8*) "";   /* out of the console's table: never reached by valid ids */
    }
    return player_settings[player_idx][slot];
}

/* FG_CreateSubtext without outlines. The module passes the string as the format; "%s" keeps a
 * message holding '%' from reading arguments that are not there. */
static int create_subtext(HSD_Text* t, const GXColor* color, const char* s, f32 scale, f32 x, f32 y)
{
    GXColor c = *color;
    int idx = HSD_SisLib_803A6B98(t, x, y, "");
    HSD_SisLib_803A7548(t, idx, scale, scale);
    HSD_SisLib_803A74F0(t, idx, &c);
    HSD_SisLib_803A70A0(t, idx, (char*) "%s", s);
    return idx;
}

/* CreateChatWindowText: header + the group's four messages. */
static HSD_Text* create_chat_window_text(int group_id)
{
    const u8* m = msrb();
    HSD_Text* text = HSD_SisLib_803A6754(0, 0);
    f32 x, label_x;
    int i, gi;
    char title[30];

    text->default_kerning = 1;
    text->default_alignment = 0;
    text->pos_z = 0.0f;
    text->font_size.x = 0.1f;
    text->font_size.y = 0.1f;

    x = is_widescreen() ? -452.0f : -300.0f;
    label_x = x + 15.0f;

    gi = group_index(group_id);
    strcpy(title, gi >= 0 ? header_strings[gi] : "");
    create_subtext(text, is_connected() ? &color_window : &color_window_idle, title, 0.45f, x,
                   79.0f);

    for (i = 1; i <= 4; i++) {
        f32 y = 79.0f + 25.0f * (f32) (i + 1);
        const u8* label = chat_text(group_id, i, m[MSRB_LOCAL_IDX], 1);
        char buf[CHAT_MESSAGE_LEN + 1];
        memcpy(buf, label, CHAT_MESSAGE_LEN);
        buf[CHAT_MESSAGE_LEN] = 0;
        create_subtext(text, &color_white, buf, 0.45f, label_x, y);
    }
    return text;
}

/* ---- SysText: the SIS string of a bubble ---- */

typedef struct SysText {
    u8* chars;
    int count;
} SysText;

static void st_push(SysText* st, u8 c)
{
    if (st->count < SYSTEXT_SIZE - 1) {
        st->chars[st->count] = c;
    }
    st->count++;
}

static void st_color(SysText* st, u8 r, u8 g, u8 b)
{
    st_push(st, SIS_COLOR);
    st_push(st, r);
    st_push(st, g);
    st_push(st, b);
}

/* st_character: the table index as a "common character", or a space when the code is unknown. */
static void st_character(SysText* st, int ch, int is_sjis)
{
    int i;
    for (i = 0; i < CHAR_MAP_SIZE; i++) {
        if ((is_sjis && hex_map[i] == ch) || char_map[i] == ch) {
            st_push(st, (u8) (SIS_COMMON_CHAR | ((i >> 8) & 0xFF)));
            st_push(st, (u8) (i & 0xFF));
            return;
        }
    }
    st_push(st, SIS_SPACE);
}

/* st_build_text: a byte above 0x80 starts a two-byte Shift-JIS code. */
static void st_text(SysText* st, const u8* s, int len, int is_sjis)
{
    int i;
    for (i = 0; i < len; i++) {
        int ch = s[i];
        if (ch > 0x80 && i + 1 < len) {
            ch = ch << 8 | s[i + 1];
            i++;
        }
        st_character(st, ch, is_sjis);
    }
}

/* BuildChatTextData: "<LEFT><KERN><COLOR player>name:<S><COLOR white>message", or the
 * "has chat disabled" line. */
static u8* build_chat_text_data(const u8* player_name, int player_name_len, int player_index,
                                int group_id, int message_id)
{
    SysText st;
    st.chars = HSD_MemAlloc(SYSTEXT_SIZE);
    memset(st.chars, 0, SYSTEXT_SIZE);
    st.count = 0;

    st_push(&st, SIS_LEFT);
    st_push(&st, SIS_KERN);
    if (message_id == CHAT_MESSAGE_ID_DISABLED) {
        static const char disabled[] = "has chat disabled";
        st_color(&st, 0, 178, 2);
        st_text(&st, player_name, player_name_len, 1);
        st_push(&st, SIS_SPACE);
        st_color(&st, 255, 255, 255);
        st_text(&st, (const u8*) disabled, (int) sizeof disabled - 1, 0);
    } else {
        const u8* message = chat_text(group_id, message_id, player_index, 0);
        int message_len = 0;
        while (message_len < CHAT_MESSAGE_LEN && message[message_len] != 0) {
            message_len++;
        }
        switch (player_index) {
        case 1: st_color(&st, 59, 189, 255); break;
        case 2: st_color(&st, 255, 203, 4); break;
        case 3: st_color(&st, 0, 178, 2); break;
        default: st_color(&st, 229, 76, 76); break;
        }
        st_text(&st, player_name, player_name_len, 1);
        st_text(&st, (const u8*) ":", 1, 0);
        st_push(&st, SIS_SPACE);
        st_color(&st, 255, 255, 255);
        st_text(&st, message, message_len, 1);
    }
    st.chars[SYSTEXT_SIZE - 1] = 0;
    return st.chars;
}

/* CreateChatMessageTextFromLocalSysText */
static HSD_Text* create_chat_message_text(NotificationMessage* msg)
{
    const u8* m = msrb();
    int is_local = msg->player_index == m[MSRB_LOCAL_IDX];
    int group_id = msg->message_id >> 4;
    int message_id = (group_id << 4) ^ msg->message_id;
    const u8* name;
    HSD_Text* text;
    f32 x, y;

    if (msg->message_id == CHAT_MESSAGE_ID_DISABLED) {
        message_id = msg->message_id;
    }
    name = is_local ? m + MSRB_LOCAL_NAME : m + MSRB_P1_NAME + msg->player_index * MSRB_NAME_LEN;
    msg->text_data = build_chat_text_data(name, msrb_strlen(name), msg->player_index,
                                          group_id & 0xFF, message_id & 0x1F);

    x = is_widescreen() ? -44.5f : -29.5f;
    y = -23.25f + (f32) msg->id * 3.2f;

    /* The text object's GObj takes the first canvas entry's link and priority. */
    HSD_SisLib_804D797C->xE = 3;
    HSD_SisLib_804D797C->xF = 0x81;
    text = HSD_SisLib_803A5ACC(0, 0, x, y, 5.0f, 20.0f, 20.0f);
    text->hidden = 1;
    text->x34.x = 0.04f;
    text->x34.y = 0.04f;
    HSD_SisLib_803A6368(text, 0);
    text->sis_buffer = msg->text_data;
    HSD_SisLib_804D797C->xE = 1;
    HSD_SisLib_804D797C->xF = 0x80;
    return text;
}

/* ---- notifications (Core/Notifications) ---- */

static void free_notifications(void* ptr)
{
    (void) ptr;
    notifications_gobj = NULL;
    last_notification_id = -1;
}

static void update_notifications(HSD_GObj* gobj)
{
    (void) gobj;
}

/* InitNotifications (the think creates it only when missing). */
static void init_notifications(void)
{
    HSD_GObj* gobj;
    memset(notification_set, 0, sizeof notification_set);
    gobj = GObj_Create(4, 5, 0x80);
    notifications_gobj = gobj;
    GObj_InitUserData(gobj, 4, free_notifications, NULL);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 1, 0x81);
    HSD_GObj_SetupProc(gobj, update_notifications, 4);
}

static int active_notifications(void)
{
    int i, n = 0;
    for (i = 0; i < NOTIFICATION_SET_LENGTH; i++) {
        if (notification_set[i]) {
            n++;
        }
    }
    return n;
}

static int can_add_new_message(void)
{
    return active_notifications() < NOTIFICATION_SET_LENGTH;
}

/* GetNextNotificationMessageID: the slot after the last one if free (else -1), or the first free
 * slot once the last one was the end of the set. */
static int next_notification_id(void)
{
    int i = last_notification_id + 1;
    if (i < NOTIFICATION_SET_LENGTH) {
        if (!notification_set[i]) {
            notification_set[i] = 1;
            return last_notification_id = i;
        }
        return -1;
    }
    for (i = 0; i < NOTIFICATION_SET_LENGTH; i++) {
        if (!notification_set[i]) {
            notification_set[i] = 1;
            return last_notification_id = i;
        }
    }
    return -1;
}

/* FreeChatMessage + DestroyNotificationMessage */
static void free_chat_message(void* ptr)
{
    NotificationMessage* msg = ptr;
    if (msg == NULL) {
        return;
    }
    if (msg->player_index == msrb()[MSRB_LOCAL_IDX]) {
        chat_local_count--;
    } else {
        chat_remote_count--;
    }
    msg->jobj_set = NULL;
    if (msg->text_data != NULL) {
        HSD_Free(msg->text_data);
    }
    if (msg->text != NULL) {
        HSD_SisLib_803A5CC4(msg->text);
    }
    HSD_Free(msg);
}

/* UpdateNotificationMessage: STARTING (20 frames, show anim) -> IDLE (170 frames, text shown) ->
 * CLEANUP (20 frames, hide anim) -> freed. */
static void update_notification_message(HSD_GObj* gobj)
{
    NotificationMessage* msg = gobj->user_data;
    HSD_JObj* jobj = gobj->hsd_obj;
    int frames_left = msg->frames_left--;
    int i;

    HSD_JObjAnimAll(jobj);
    if (frames_left > 0) {
        return;
    }
    switch (msg->state) {
    case STATE_STARTING:
        msg->state = STATE_IDLE;
        msg->frames_left = (int) ((double) msg->animation_frames * 8.5);
        if (msg->text != NULL) {
            msg->text->hidden = 0;
        }
        break;
    case STATE_IDLE:
        msg->state = STATE_CLEANUP;
        msg->frames_left = msg->animation_frames;
        if (msg->text != NULL) {
            msg->text->hidden = 1;
        }
        gm_8016895C(jobj, msg->jobj_set, 1);
        HSD_JObjReqAnimAll(jobj, 0.0f);
        break;
    case STATE_CLEANUP:
        notification_set[msg->id] = 0;
        HSD_GObjFree(gobj);   /* deferred: this proc is running */
        for (i = 0; i < NOTIFICATION_SET_LENGTH; i++) {
            if (notification_set[i]) {
                return;
            }
        }
        last_notification_id = -1;
        break;
    default:
        break;
    }
}

/* CreateAndAddNotificationMessage. The console stalls when no slot is free; natively the message
 * is dropped instead of hanging the game. */
static int add_notification_message(SlpCssChat* css, NotificationMessage* msg)
{
    DynamicModelDesc* set = DP(css->chat_message);
    HSD_GObj* gobj = GObj_Create(4, 5, 0x80);
    HSD_JObj* jobj = HSD_JObjLoadJoint(DP(set->joint));
    int id = -1;

    if (can_add_new_message()) {
        id = next_notification_id();
    }
    if (id < 0) {
        HSD_JObjRemoveAll(jobj);
        HSD_GObjFree(gobj);
        return -1;
    }
    msg->id = id;
    msg->jobj_set = set;
    if (is_widescreen()) {
        jobj->translate.x = -14.0f;
    }
    jobj->translate.y = -((f32) msg->id * 3.2f);
    gm_8016895C(jobj, set, 0);
    HSD_JObjReqAnimAll(jobj, 0.0f);

    GObj_InitUserData(gobj, 4, free_chat_message, msg);
    HSD_GObjObject_80390A70(gobj, 4, jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 3, 0x81);
    HSD_GObj_SetupProc(gobj, update_notification_message, 4);
    return 0;
}

/* ---- chat notifications (Core/Notifications/Chat) ---- */

static void free_chat_notifications(void* ptr)
{
    (void) ptr;
    chat_notifications_gobj = NULL;
}

static int max_local_messages(void)
{
    return remote_player_count() > 1 ? CHAT_MAX_PLAYER_MESSAGES / 2 : CHAT_MAX_PLAYER_MESSAGES;
}

static int can_add_new_chat_message(void)
{
    return can_add_new_message() && chat_local_count < max_local_messages();
}

/* CreateAndAddChatMessage */
static void add_chat_message(SlpCssChat* css, int player_index, int message_id)
{
    int is_local = player_index == msrb()[MSRB_LOCAL_IDX];
    NotificationMessage* msg;

    if (chat_remote_count + chat_local_count >= NOTIFICATION_SET_LENGTH) {
        return;
    }
    msg = HSD_MemAlloc(sizeof *msg);
    memset(msg, 0, sizeof *msg);
    msg->type = 1;   /* SLP_NOT_CHAT */
    msg->state = STATE_STARTING;
    msg->frames_left = CHAT_FRAMES;
    msg->animation_frames = CHAT_FRAMES;
    msg->player_index = player_index;
    msg->message_id = message_id;
    if (add_notification_message(css, msg) < 0) {
        HSD_Free(msg);
        return;
    }
    msg->text = create_chat_message_text(msg);
    Ground_801C53EC(SND_NEW_MESSAGE);
    if (is_local) {
        chat_local_count++;
    } else {
        chat_remote_count++;
    }
}

static int valid_group_bit(int v)
{
    return v == HSD_PAD_DPADUP || v == HSD_PAD_DPADLEFT || v == HSD_PAD_DPADRIGHT ||
           v == HSD_PAD_DPADDOWN;
}

/* UpdateChatNotifications: one bubble per chat id in the MSRB (the host hands each message out
 * once). The local echo wins over the remote message. */
static void update_chat_notifications(HSD_GObj* gobj)
{
    const u8* m = msrb();
    SlpCssChat* css = slpcss();
    int message_id = m[MSRB_USR_CHAT];
    int player_index = m[MSRB_CHAT_PLAYER];
    (void) gobj;

    if (message_id == 0) {
        message_id = m[MSRB_OPP_CHAT];
    }
    if (message_id <= 0 || player_index > 3 || css == NULL) {
        return;
    }
    if (!valid_group_bit(message_id >> 4) ||
        (!valid_group_bit(message_id & 0xF) && message_id != CHAT_MESSAGE_ID_DISABLED))
    {
        return;
    }
    add_chat_message(css, player_index, message_id);
}

static void listen_for_chat_notifications(void)
{
    HSD_GObj* gobj;
    chat_local_count = 0;
    chat_remote_count = 0;
    gobj = GObj_Create(4, 5, 0x80);
    chat_notifications_gobj = gobj;
    GObj_InitUserData(gobj, 4, free_chat_notifications, NULL);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 1, 0x81);
    HSD_GObj_SetupProc(gobj, update_chat_notifications, 4);
}

/* ---- chat window (Scenes/CSS/Chat) ---- */

/* PadGetChatInput: the engine pad of the CSS port; the D-pad (up, left, right, down), and B when
 * the window is open. Returns -1 without input. */
static int chat_input(int check_for_commands)
{
    static const u32 normal_inputs[4] = { HSD_PAD_DPADUP, HSD_PAD_DPADLEFT, HSD_PAD_DPADRIGHT,
                                          HSD_PAD_DPADDOWN };
    int port = mu_slippi_css_port();
    u32 down;
    int i;
    if (port < 0 || port > 3) {
        return -1;
    }
    down = HSD_PadGameStatus[port].trigger;
    for (i = 0; i < 4; i++) {
        if (down & normal_inputs[i]) {
            return (int) normal_inputs[i];
        }
    }
    if (check_for_commands && (down & HSD_PAD_B)) {
        return HSD_PAD_B;
    }
    return -1;
}

static void free_chat_window(void* ptr)
{
    chat_window_gobj = NULL;
    if (ptr != NULL) {
        HSD_Free(ptr);
    }
}

static void free_chat(void* ptr)
{
    chat_main_gobj = NULL;
    if (ptr != NULL) {
        HSD_Free(ptr);
    }
}

/* CloseChatWindow */
static void close_chat_window(HSD_JObj* jobj, ChatWindowData* data)
{
    HSD_GObj* gobj = chat_window_gobj;
    HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);
    HSD_SisLib_803A5CC4(data->text);
    data->text = NULL;
    HSD_GObjFree(gobj);   /* deferred: this proc is running */
    HSD_GObjProc_RemoveAllProcs(gobj);
    mu_slippi_css_set_chat_open(0);
    chat_window_gobj = NULL;
}

/* UpdateChatWindow: first frame builds the text; then D-pad sends (BB) and closes, B closes, and
 * 150 frames without input close. */
static void update_chat_window(HSD_GObj* gobj)
{
    ChatWindowData* data = gobj->user_data;
    HSD_JObj* jobj = gobj->hsd_obj;
    int input;

    data->delayed_frames++;
    if (data->text == NULL) {
        gobj->render_priority = 0x80;
        data->text = create_chat_window_text(data->group_id);
        return;
    }
    data->frames_left--;

    input = chat_input(1);
    if (input == HSD_PAD_B) {
        close_chat_window(jobj, data);
        return;
    }
    if (input > 0) {
        if (data->delayed_frames < CHAT_ALLOW_COMMAND_FRAMES) {
            return;
        }
        if (!can_add_new_chat_message()) {
            data->delayed_frames = 0;
            lbAudioAx_80024030(SND_BLOCK_MESSAGE);
            return;
        }
        data->delayed_frames = 0;
        mu_slippi_send_chat((data->group_id << 4) + input);
        close_chat_window(jobj, data);
        HSD_AudioSFXStartParam(SND_NEW_MESSAGE, 127, 64, 0, 0);
        return;
    }
    if (data->frames_left <= 0) {
        close_chat_window(jobj, data);
    }
}

/* CreateAndOpenChatWindow */
static void open_chat_window(void)
{
    int input = chat_input(0);
    SlpCssChat* css = slpcss();
    ChatWindowData* data;
    HSD_GObj* gobj;
    HSD_JObj* jobj;

    if (input <= 0 || css == NULL) {
        return;
    }
    data = HSD_MemAlloc(sizeof *data);
    memset(data, 0, sizeof *data);
    data->group_id = input;
    data->slpcss = css;
    data->frames_left = CHAT_WINDOW_FRAMES;
    data->delayed_frames = CHAT_ALLOW_COMMAND_FRAMES;

    gobj = GObj_Create(4, 5, 0x80);
    chat_window_gobj = gobj;
    jobj = HSD_JObjLoadJoint(DP(DP(css->chat_window)->joint));
    jobj->translate.x = is_widescreen() ? -35.0f : -20.0f;
    jobj->translate.y = -16.5f;

    HSD_GObjObject_80390A70(gobj, 4, jobj);
    GObj_InitUserData(gobj, 4, free_chat_window, data);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 1, 0x81);
    HSD_GObj_SetupProc(gobj, update_chat_window, 4);

    lbAudioAx_80024030(SND_OPEN_WINDOW);
    mu_slippi_css_set_chat_open(1);
}

/* UpdateChat */
static void update_chat(HSD_GObj* gobj)
{
    (void) gobj;
    if (chat_window_gobj != NULL) {
        return;
    }
    open_chat_window();
}

static void listen_for_chat_input(void)
{
    HSD_GObj* gobj = GObj_Create(4, 5, 0x80);
    chat_main_gobj = gobj;
    GObj_InitUserData(gobj, 4, free_chat, NULL);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 1, 0x81);
    HSD_GObj_SetupProc(gobj, update_chat, 4);
}

/* ---- Zelda/Sheik selector (Scenes/CSS/SheikSelector.c) ---- */

enum {
    CKIND_ZELDA = 0x12,
    CKIND_SHEIK = 0x13,
};

void mu_css_sheik_cursor(f32* x, f32* y);   /* mncharsel.c */
void mu_css_sheik_set_char(u8 ckind);       /* mncharsel.c: SetSelectedChar */

static HSD_JObj* selector_jobj;
static u8 selector_in_name_entry;
static unsigned zelda_icon_load = ~0u;

/* JOBJ_SetAllAlpha: every material of the joint, its children and its later siblings. */
static void set_all_alpha(HSD_JObj* jobj, f32 alpha)
{
    for (; jobj != NULL; jobj = jobj->next) {
        HSD_DObj* dobj;
        for (dobj = jobj->u.dobj; dobj != NULL; dobj = dobj->next) {
            if (dobj->mobj != NULL && dobj->mobj->mat != NULL) {
                dobj->mobj->mat->alpha = alpha;
            }
        }
        set_all_alpha(jobj->child, alpha);
    }
}

/* GetSelectedChar: the low byte of preload cache entry 0's character (0x8043208F). */
static u8 selected_char(void)
{
    return (u8) lbDvd_GetPreloadCacheScene()->game_cache.entries[0].char_id;
}

/* InitSheikSelector: a draw-only GObj at (-8, -22.5). */
static void init_sheik_selector(void)
{
    SlpCssChat* css = slpcss();
    HSD_GObj* gobj;
    selector_jobj = NULL;
    selector_in_name_entry = mu_slippi_css_scene_request() != 0;
    if (css == NULL || DISC_NULL(css->sheik_selector)) {
        return;
    }
    gobj = GObj_Create(4, 5, 0x80);
    selector_jobj = HSD_JObjLoadJoint(DP(css->sheik_selector));
    selector_jobj->translate.x = -8.0f;
    selector_jobj->translate.y = -22.5f;
    HSD_JObjSetMtxDirtySub(selector_jobj);
    HSD_GObjObject_80390A70(gobj, 4, selector_jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 1, 0x81);
}

/* UpdateSelectorAlphas: hovered 0.75, idle 0.4, the selected character 1. */
static void update_selector_alphas(int zelda_hovered, int sheik_hovered)
{
    HSD_JObj* zelda = selector_jobj->child;
    HSD_JObj* sheik = zelda->next;
    u8 selected = selected_char();
    f32 zelda_alpha = zelda_hovered ? 0.75f : 0.4f;
    f32 sheik_alpha = sheik_hovered ? 0.75f : 0.4f;
    if (selected == CKIND_ZELDA) {
        zelda_alpha = 1.0f;
    }
    if (selected == CKIND_SHEIK) {
        sheik_alpha = 1.0f;
    }
    set_all_alpha(zelda, zelda_alpha);
    set_all_alpha(sheik, sheik_alpha);
}

/* UpdateSheikSelector: hidden unless Zelda or Sheik is the selected character; not interactive
 * while locked in. Tap targets: y in [-22.75, -19.75], Zelda x in [-16.5, -13], Sheik x in
 * [-13, -9.5]; A on one picks it (on x == -13 both run, Sheik last). */
static void update_sheik_selector(void)
{
    int was_in_name_entry = selector_in_name_entry;
    u8 selected;
    u32 down;
    f32 x, y;
    int zelda_hovered = 0, sheik_hovered = 0, i;

    selector_in_name_entry = mu_slippi_css_scene_request() != 0;
    if (selector_in_name_entry) {
        return;
    }
    if (was_in_name_entry) {
        init_sheik_selector();
    }
    if (selector_jobj == NULL) {
        return;
    }
    selected = selected_char();
    if (selected != CKIND_SHEIK && selected != CKIND_ZELDA) {
        set_all_alpha(selector_jobj, 0.0f);
        return;
    }
    if (msrb()[1] != 0) {   /* is_local_player_ready */
        update_selector_alphas(0, 0);
        return;
    }
    set_all_alpha(selector_jobj, 1.0f);

    down = fn_8018F640(mu_slippi_css_port());
    mu_css_sheik_cursor(&x, &y);
    for (i = 0; i < 2; i++) {
        int is_sheik = i == 1;
        if (y > -19.75f || y < -22.75f) {
            continue;
        }
        if (x < (is_sheik ? -13.0f : -16.5f) || x > (is_sheik ? -9.5f : -13.0f)) {
            continue;
        }
        sheik_hovered = is_sheik;
        zelda_hovered = !is_sheik;
        if (!(down & HSD_PAD_A)) {
            continue;
        }
        mu_css_sheik_set_char(is_sheik ? CKIND_SHEIK : CKIND_ZELDA);
    }
    update_selector_alphas(zelda_hovered, sheik_hovered);
}

/* The online major's OnLoad writes Sheik into the Zelda icon and OnUnload writes Zelda back; the
 * selector writes it in between. mnCharSel_Scene_OnEnter re-applies the load/unload value only
 * outside the online major or on the first CSS after an OnLoad, so a selector choice survives the
 * CSS reloads of one online session as it does on the console. */
int mu_slippi_css_zelda_icon_reset(void)
{
    unsigned loads = mu_slippi_online_load_count();
    if (!mu_slippi_zelda_is_sheik()) {
        zelda_icon_load = ~0u;
        return 1;
    }
    if (loads != zelda_icon_load) {
        zelda_icon_load = loads;
        return 1;
    }
    return 0;
}

/* ---- scene hooks ---- */

/* InitOnlineCSS: after mnCharSel_Scene_OnEnter. InitChatMessages sends C3 and keeps the reply;
 * then InitSheikSelector.
 * The console reads SlippiCSS.dat again with every CSS scene load, so the module's statics start
 * over; a scene change drops the old GObjs without running their destructors, so the pointers
 * must be cleared here rather than left to them. */
void mu_slippi_chat_enter(void)
{
    notifications_gobj = NULL;
    chat_notifications_gobj = NULL;
    chat_main_gobj = NULL;
    chat_window_gobj = NULL;
    memset(notification_set, 0, sizeof notification_set);
    last_notification_id = -1;
    chat_local_count = 0;
    chat_remote_count = 0;
    selector_jobj = NULL;
    selector_in_name_entry = 0;
    if (!mu_slippi_css_online()) {
        return;
    }
    memset(player_settings, 0, sizeof player_settings);
    mu_slippi_cmd(CMD_GET_PLAYER_SETTINGS, NULL, 0, player_settings, sizeof player_settings,
                  NULL);
    init_sheik_selector();
}

/* UpdateOnlineCSS: after mnCharSel_Scene_OnFrame. The Sheik selector first; then, unless the CSS
 * is leaving or on name entry (the scene request byte), (re)create the missing chat listeners. */
void mu_slippi_chat_frame(void)
{
    if (!mu_slippi_css_online()) {
        return;
    }
    update_sheik_selector();
    if (mu_slippi_css_scene_request() != 0) {
        return;
    }
    if (notifications_gobj == NULL) {
        init_notifications();
    }
    if (chat_notifications_gobj == NULL) {
        listen_for_chat_notifications();
    }
    if (chat_main_gobj == NULL) {
        listen_for_chat_input();
    }
}
