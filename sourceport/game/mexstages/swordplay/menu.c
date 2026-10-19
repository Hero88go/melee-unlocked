/* GrWiiSword DbMenu_Init/Think/Print. Its only page has three entries: enum, int, float.
 * The block's generic page machinery is specialized to that immutable table. */
#include "swordplay.h"
#include <melee/ft/types.h>
#include <melee/if/textdraw.h>
#include <melee/if/textlib.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/memory.h>

int sword_spoof = 0, sword_players = 6;
float sword_speed = 0.6F;
typedef struct SwordMenu { int open, player; DevText* text; int page, selected, scroll; } SwordMenu;
static void sword_menu_print(SwordMenu* v)
{
    DevText* t = v->text;
    int i;
    DevText_Erase(t); DevText_SetCursorXY(t, 0, 0);
    DevText_StoreColorIndex(t, 0); DevText_SetTextColor(t, (GXColor) {255,255,255,255});
    DevText_StoreColorIndex(t, 1); DevText_SetTextColor(t, (GXColor) {255,128,32,255});
    DevText_StoreColorIndex(t, 0);
    DevText_Printf(t, "%*s< %s >\n\n", 6, "", "Stage Params");
    for (i = 0; i < 3; i++) {
        DevText_StoreColorIndex(t, v->selected == i ? 1 : 0);
        if (i == 0) DevText_Printf(t, " %-24s <%s>\n", "spoof player num", sword_spoof ? "on" : "off");
        else if (i == 1) DevText_Printf(t, " %-24s <%5d>\n", "player num", sword_players);
        else DevText_Printf(t, " %-24s <%05.2f>\n", "move_speed", (double) sword_speed);
    }
}
static void sword_menu_free(void* data)
{
    SwordMenu* v = data;
    char* buffer = v->text->buf;
    mu_gr_mex_devtext_remove(v->text);
    HSD_Free(buffer); HSD_Free(v);
}
static void sword_menu_lock(int player, bool disabled, unsigned int pressed)
{
    int j;
    for (j = 0; j < 2; j++) {
        Fighter_GObj* fighter = Player_GetEntityAtIndex(player, j);
        if (fighter != NULL) {
            Fighter* fp = HSD_GObjGetUserData(fighter);
            fp->input_disabled = disabled;
            if (!disabled) fp->input.pressed_buttons = pressed;
        }
    }
}
static void sword_menu_think(HSD_GObj* gobj)
{
    SwordMenu* v = HSD_GObjGetUserData(gobj);
    HSD_PadStatus* pad;
    unsigned int buttons, pressed;
    int i;
    if (!v->open) {
        for (i = 0; i < 6; i++) if (Player_GetPlayerSlotType(i) == 0) {
            pad = &HSD_PadCopyStatus[Player_GetPadPort(i)];
            if (pad->trigger & 4) {
                sword_menu_lock(i, true, 0);
                DevText_ShowBackground(v->text); DevText_ShowText(v->text);
                sword_menu_print(v); v->open = 1; v->player = i;
                return;
            }
        }
        return;
    }
    pad = &HSD_PadCopyStatus[Player_GetPadPort(v->player)];
    buttons = (pad->button & 0x10) ? pad->button : pad->repeat;
    pressed = pad->trigger;
    if (pressed & 0x204) {
        sword_menu_lock(v->player, false, pressed);
        DevText_HideBackground(v->text); DevText_HideText(v->text); v->open = 0;
        return;
    }
    if (pressed & 0x40) { v->page = 0; return; }
    if (pressed & 0x20) { v->page = 0; return; }
    if (buttons & 0x30000) {
        if (buttons & 0x10000) { if (v->selected > 0) v->selected--; else return; }
        else if (buttons & 0x20000) { if (v->selected < 2) v->selected++; else return; }
        sword_menu_print(v); return;
    }
    if (buttons & 0xC0000) {
        int direction = (buttons & 0x40000) ? -1 : 1;
        switch (v->selected) {
        case 0: if (direction < 0 && sword_spoof > 0) sword_spoof--;
                else if (direction > 0 && sword_spoof < 1) sword_spoof++;
                break;
        case 1: sword_players += direction; break;
        case 2: if (direction < 0) sword_speed -= 0.1F; else sword_speed += 0.1F; break;
        }
        sword_menu_print(v);
    }
}
void sword_menu_init(void)
{
    HSD_GObj* gobj = GObj_Create(14, 14, 0);
    SwordMenu* v = HSD_MemAlloc(sizeof(*v));
    char* buffer = HSD_MemAlloc(0x578);
    v->text = DevText_Create(30, 60, 50, 35, 20, buffer);
    DevText_Show(NULL, v->text);
    v->text->flags |= 0x20;
    DevText_SetBGColor(v->text, (GXColor) {21,20,59,135});
    DevText_SetScale(v->text, 15.0F, 20.0F);
    DevText_HideText(v->text); DevText_HideBackground(v->text);
    v->open = v->page = v->selected = v->scroll = 0;
    GObj_InitUserData(gobj, 4, sword_menu_free, v);
    HSD_GObj_SetupProc(gobj, sword_menu_think, 0);
}
