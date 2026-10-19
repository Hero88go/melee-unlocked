/* Volleyball: the attribute menu of the grFunction block (DbMenu_Init +2194, DbMenu_Think +274C,
 * DbMenu_Print +3004), created by Volley_Start only when DbLevel > 3 (a develop build). D-pad down
 * opens it for the human player who pressed it (that player's fighters stop taking input); L and R
 * turn the four pages (volley, court, net, ball attributes of volley_data), the stick moves the
 * cursor (16 rows, scrolling), left and right step the value (ints by 1, floats by 0.001 to 0.1 by
 * size), B or D-pad down closes it. */
#include "volleyball.h"

#include "../common/mu_gr_mex.h"

#include <string.h>

#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/if/textdraw.h>
#include <melee/if/textlib.h>
#include <melee/if/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/memory.h>

/* The menu gobj's user data: the console allocates 4 bytes (803D706C(4)) and uses 0x18 (HSD_MemAlloc
 * rounds the block up there); natively the struct's own size. */
typedef struct MexgrVbDbMenu {
    /* +0 */ s32 open;
    /* +4 */ s32 slot;
    /* +8 */ DevText* text;
    /* +C */ s32 page;
    /* +10 */ s32 cursor;
    /* +14 */ s32 scroll;
} MexgrVbDbMenu;

/* .rodata.attr_nums */
static const u8 mexgr_vb_attr_nums[4] = { 0x14, 0x08, 0x04, 0x1A };

/* .bss.volley_attr_types, .data.court_attr_types, net_attr_types, ball_attr_types: 0 int, 1 float,
 * 2 shown as N/A (the colors). The ball's coll_size words are typed int, so the menu steps their
 * float bits by 1, as on the console. */
static const u8 mexgr_vb_volley_attr_types[20] = { 0 };
static const u8 mexgr_vb_court_attr_types[8] = { 0, 0, 0, 0, 0, 0, 1, 1 };
static const u8 mexgr_vb_net_attr_types[4] = { 1, 1, 1, 1 };
static const u8 mexgr_vb_ball_attr_types[26] = {
    0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0,
};
static const u8* const mexgr_vb_attr_types[4] = {
    mexgr_vb_volley_attr_types,
    mexgr_vb_court_attr_types,
    mexgr_vb_net_attr_types,
    mexgr_vb_ball_attr_types,
};

/* .data.*_attr_names (.rodata.str1.4) */
static const char* const mexgr_vb_volley_attr_names[20] = {
    "score_max", "ball_maxhit", "landwait_timer", "crowdupset_rallymin", "crowdupset_rallymax",
    "crowdupset_sfx", "crowdcheer_rallymin", "crowdcheer_sfx", "point_sfx", "game_sfx",
    "duece_sfx", "autoreturn_anglelow", "autoreturn_anglehi", "autoreturn_dmglow",
    "autoreturn_dmghi", "autoreturn_kbglow", "autoreturn_kbghi", "autoreturn_bkb", "serve_timer",
    "serve_danger_time",
};
static const char* const mexgr_vb_court_attr_names[8] = {
    "spawn_genpoint", "netpos_genpoint", "court_collgroup", "barrier_collgroup", "ceiling_line",
    "ceiling_joint", "itembarrier_width", "teams_stage_scale",
};
static const char* const mexgr_vb_net_attr_names[4] = {
    "mag_max", "tightness", "decay", "kb_mult",
};
static const char* const mexgr_vb_ball_attr_names[26] = {
    "phys_kind", "physclassic_gravity", "physclassic_terminalvel", "physmodern_gravity",
    "physmodern_drag", "physmodern_bouncedecay", "friction", "dmg_min", "dmg_decay_amt",
    "dmg_decay_rate", "parry_up_kb", "parry_expire_frames", "parry_hit_freeze_frames",
    "parry_radius", "parry_execute_window", "land_vel", "spin_amt", "colshift_colstart",
    "colshift_colend", "colshift_pctend", "hitsfx_num", "hitsfx_start", "bouncesfx_num",
    "bouncesfx_start", "coll_size.X", "coll_size.Y",
};
static const char* const* const mexgr_vb_attr_names[4] = {
    mexgr_vb_volley_attr_names,
    mexgr_vb_court_attr_names,
    mexgr_vb_net_attr_names,
    mexgr_vb_ball_attr_names,
};

/* .rodata.page_names */
static const char* const mexgr_vb_page_names[4] = {
    "Volley Attributes", "Court Attributes", "Net Attributes", "Ball Attributes",
};

/* The words of a page in volley_data (+0, +50, +70, +80), each 4 bytes. */
static u32* mexgr_vb_page_words(s32 page)
{
    switch (page) {
    case 0: return (u32*) &mexgr_vb_data->volley;
    case 1: return (u32*) &mexgr_vb_data->court;
    case 2: return (u32*) &mexgr_vb_data->net;
    case 3: return (u32*) &mexgr_vb_data->ball;
    default:
        /* the page stays 0 to 3; the console would use an unset register */
        return (u32*) &mexgr_vb_data->volley;
    }
}

static f32 mexgr_vb_word_f32(const u32* w)
{
    f32 f;
    memcpy(&f, w, sizeof(f));
    return f;
}

/* DbMenu_Print (+3004) */
static void mexgr_vb_dbmenu_print(HSD_GObj* gobj)
{
    static const GXColor unsel = { 0xFF, 0xFF, 0xFF, 0xFF };
    static const GXColor sel = { 0xFF, 0x80, 0x20, 0xFF };
    MexgrVbDbMenu* dm = HSD_GObjGetUserData(gobj);
    DevText* text = dm->text;
    const char* name;
    s32 page;
    s32 selected;
    u8 num;
    u8 rows;
    u32* words;
    s32 row;
    DevText_Erase(text);
    DevText_SetCursorXY(text, 0, 0);
    DevText_StoreColorIndex(text, 0);
    DevText_SetTextColor(text, unsel);
    DevText_StoreColorIndex(text, 1);
    DevText_SetTextColor(text, sel);
    name = mexgr_vb_page_names[dm->page];
    DevText_StoreColorIndex(text, 0);
    DevText_Printf(text, "%*s< %s >\n\n", (int) (strlen(name) >> 1), "", name);
    page = dm->page;
    selected = dm->cursor + dm->scroll;
    num = mexgr_vb_attr_nums[page];
    rows = num > 16 ? 16 : num;
    words = mexgr_vb_page_words(page);
    if (num == 0) {
        return;
    }
    for (row = 0; rows != 0; row++, rows--) {
        const s32 idx = row + dm->scroll;
        const u8 type = mexgr_vb_attr_types[page][idx];
        const char* label = mexgr_vb_attr_names[page][idx];
        if (selected == idx) {
            DevText_StoreColorIndex(text, 1);
        } else {
            DevText_StoreColorIndex(text, 0);
        }
        if (type == 0) {
            DevText_Printf(text, " %-24s <%5d>\n", label, (int) (s32) words[idx]);
        } else if (type == 1) {
            DevText_Printf(text, " %-24s <%05.3f>\n", label, (double) mexgr_vb_word_f32(&words[idx]));
        } else if (type == 2) {
            DevText_Printf(text, " %-24s <%s>\n", label, " N/A ");
        }
    }
}

/* Closing: the player's fighters take input again, this frame's pad trigger as their pressed
 * buttons. */
static void mexgr_vb_dbmenu_close(MexgrVbDbMenu* dm, const HSD_PadStatus* pad)
{
    int idx;
    for (idx = 0; idx < 2; idx++) {
        HSD_GObj* e = Player_GetEntityAtIndex(dm->slot, idx);
        if (e != NULL) {
            Fighter* fp = GET_FIGHTER(e);
            fp->input_disabled = 0;
            fp->input.pressed_buttons = pad->trigger;
        }
    }
    DevText_HideBackground(dm->text);
    DevText_HideText(dm->text);
    dm->open = 0;
}

/* DbMenu_Think (+274C), the menu gobj's priority 0 proc. */
static void mexgr_vb_dbmenu_think(HSD_GObj* gobj)
{
    MexgrVbDbMenu* dm = HSD_GObjGetUserData(gobj);
    if (dm->open == 0) {
        s32 slot;
        for (slot = 0; slot != 6; slot++) {
            int idx;
            if (Player_GetPlayerSlotType(slot) != Gm_PKind_Human) {
                continue;
            }
            if (!(HSD_PadGameStatus[Player_GetPadPort(slot)].trigger & HSD_PAD_DPADDOWN)) {
                continue;
            }
            for (idx = 0; idx < 2; idx++) {
                HSD_GObj* e = Player_GetEntityAtIndex(slot, idx);
                if (e != NULL) {
                    GET_FIGHTER(e)->input_disabled = 1;
                }
            }
            DevText_ShowBackground(dm->text);
            DevText_ShowText(dm->text);
            mexgr_vb_dbmenu_print(gobj);
            dm->open = 1;
            dm->slot = slot;
            return;
        }
        return;
    }
    if (dm->open != 1) {
        return;
    }
    {
        const HSD_PadStatus* pad = &HSD_PadGameStatus[Player_GetPadPort(dm->slot)];
        const u32 held = pad->button;
        const u32 trigger = pad->trigger;
        u32 repeat = pad->repeat;
        if (held & HSD_PAD_Z) {
            repeat = held;
        }
        if (trigger & (HSD_PAD_B | HSD_PAD_DPADDOWN)) {
            mexgr_vb_dbmenu_close(dm, pad);
            return;
        }
        if (trigger & HSD_PAD_L) {
            dm->page -= 1;
            if (dm->page < 0) {
                dm->page = 0;
                return;
            }
            dm->scroll = 0;
            dm->cursor = 0;
            mexgr_vb_dbmenu_print(gobj);
            return;
        }
        if (trigger & HSD_PAD_R) {
            dm->page += 1;
            if ((u32) dm->page > 3) {
                dm->page = 3;
                return;
            }
            dm->scroll = 0;
            dm->cursor = 0;
            mexgr_vb_dbmenu_print(gobj);
            return;
        }
        if (repeat & 0x30000) {
            /* stick up / down */
            s32 cursor = dm->cursor;
            s32 scroll = dm->scroll;
            const u8 num = mexgr_vb_attr_nums[dm->page];
            if (repeat & 0x10000) {
                cursor -= 1;
                if (cursor >= 0) {
                    dm->cursor = cursor;
                    mexgr_vb_dbmenu_print(gobj);
                    return;
                }
                dm->cursor = 0;
                scroll -= 1;
                if (scroll >= 0) {
                    dm->scroll = scroll;
                    mexgr_vb_dbmenu_print(gobj);
                    return;
                }
                dm->scroll = 0;
                return;
            }
            if (repeat & 0x20000) {
                const u8 rows = num > 16 ? 16 : num;
                cursor += 1;
                if (cursor < rows) {
                    dm->cursor = cursor;
                    mexgr_vb_dbmenu_print(gobj);
                    return;
                }
                dm->cursor = rows - 1;
                scroll += 1;
                if (scroll > num - rows) {
                    dm->scroll = num - rows;
                    return;
                }
                dm->scroll = scroll;
                mexgr_vb_dbmenu_print(gobj);
            }
            return;
        }
        if (repeat & 0xC0000) {
            /* stick left / right: step the selected word */
            const s32 page = dm->page;
            const s32 idx = dm->cursor + dm->scroll;
            u32* words = mexgr_vb_page_words(page);
            f32 sign;
            s32 step;
            u8 type;
            if (repeat & 0x40000) {
                sign = -1.0F;
                step = -1;
            } else {
                sign = 1.0F;
                step = dm->open;   /* r31, the open flag: 1 */
            }
            type = mexgr_vb_attr_types[page][idx];
            if (type == 0) {
                words[idx] = (u32) ((s32) words[idx] + step);
                mexgr_vb_dbmenu_print(gobj);
                return;
            }
            if (type == 1) {
                f32 v = mexgr_vb_word_f32(&words[idx]);
                f32 mag = -v;
                f32 d;
                if (!(v < 0.0F)) {
                    mag = v;
                }
                if (mag < 0x1.ffffdep-1F /* 0.999999 */) {
                    d = sign * 0x1.0624dep-10F;   /* 0.001 */
                } else if (mag < 0x1.3ffffcp+2F /* 4.999999 */) {
                    d = sign * 0x1.47ae14p-7F;    /* 0.01 */
                } else if (mag < 0x1.3ffffep+3F /* 9.999999 */) {
                    d = sign * 0x1.99999ap-5F;    /* 0.05 */
                } else {
                    d = sign * 0x1.99999ap-4F;    /* 0.1 */
                }
                v = v + d;
                memcpy(&words[idx], &v, sizeof(v));
            }
            mexgr_vb_dbmenu_print(gobj);
        }
    }
}

/* DbMenu_Init (+2194): the menu gobj, its text box (id 30 at 60, 50, 35 by 20 characters, a 0x578
 * byte buffer) hidden until opened. */
void mexgr_vb_dbmenu_init(void)
{
    static const GXColor bg = { 0x15, 0x14, 0x3B, 0x87 };
    HSD_GObj* gobj = GObj_Create(0xE, 0xE, 0);
    MexgrVbDbMenu* dm = mu_gr_mex_calloc(sizeof(MexgrVbDbMenu));
    DevText* text;
    GObj_InitUserData(gobj, 4, HSD_Free, dm);
    HSD_GObj_SetupProc(gobj, mexgr_vb_dbmenu_think, 0);
    text = DevText_Create(0x1E, 0x3C, 0x32, 0x23, 0x14, HSD_MemAlloc(0x578));
    DevText_Show(NULL, text);
    text->flags &= (u8) ~0x20;
    DevText_SetBGColor(text, bg);
    DevText_SetScale(text, 15.0F, 20.0F);
    DevText_HideText(text);
    DevText_HideBackground(text);
    dm->text = text;
    dm->page = 0;
    dm->scroll = 0;
    dm->cursor = 0;
}
