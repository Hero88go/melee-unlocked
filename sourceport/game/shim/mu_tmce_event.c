/* Training Mode CE, native build: event mode and its CSS, game side.
 *
 * The console patches (ASM/training-mode/Custom Events and Hooks/OnCSSLoad.asm) as hand-written C
 * in the decomp's types. Each function here is called from the decomp statement its patch address
 * lands on, and only while mu_tmce_active() (see mu_tmce_event.h for the call sites). The event
 * data itself (names, CSS type, allowed characters...) comes from TM-CE's own C, the eventMenu
 * module (tmce/src/events.c, exports prefixed tmce_eventMenu_).
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include <dolphin/os.h>
#include <melee/ft/forward.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gm_1B03.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmmenumode.h>
#include <melee/gm/gmscdata.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/types.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/sislib.h>

#include "mu_native.h"
#include "mu_tmce_event.h"

/* ---- TM-CE's side ---- */
int mu_tmce_event_page(void); /* tmce/native/tmce_runtime.c */
HSD_Archive* MEX_LoadRelArchive(char* file, void* functions, char* symbol);
void mu_tmce_legacy_enter_vs(StartMeleeData* md, int page, int event_id, struct gm_804D6900_t** level_slot);

/* events.c AllowedCharacters: CSSID bit masks, -1 = everyone */
typedef struct TmceAllowedCharacters {
    int hmn;
    int cpu;
} TmceAllowedCharacters;

char* tmce_eventMenu_GetEventName(int page, int event);
char* tmce_eventMenu_GetEventFile(int page, int event);
char* tmce_eventMenu_GetCSSFile(int page, int event);
unsigned char tmce_eventMenu_GetCSSType(int page, int event);
unsigned char tmce_eventMenu_GetIsSelectStage(int page, int event);
signed char tmce_eventMenu_GetCPUFighter(int page, int event);
short tmce_eventMenu_GetStage(int page, int event);
TmceAllowedCharacters* tmce_eventMenu_GetEventCharList(int event, int page);
void tmce_eventMenu_EventInit(int page, int event, void* match_init);

/* the event being played (MemcardData + 0x535) */
static inline struct EventData* ev_data(void)
{
    return &gmMainLib_804D3EE0->vs.unk_530;
}

static inline int ev_id(void)
{
    return ev_data()->unk_535;
}

/* TM-CE keeps its event CSS choices where the training mode keeps its own (MemcardData + 0xD10) */
static inline VsModeData* ev_backup(void)
{
    return &gmMainLib_804D3EE0->modes.table[GmVsMode_Training];
}

/* ============================================================================================
 * CSS and SSS / Load CSS + Preload CPU.asm (onEnterCss+0x20)
 * The CSS's KO counter, the event's CSS type in place of 0xE, and the event's fixed CPU and stage
 * written to the preload table.
 * ============================================================================================ */
unsigned char mu_tmce_ev_css_enter(CSSData* css)
{
    int page = mu_tmce_event_page();
    int event = ev_id();
    struct GameCache* cache = &lbDvd_GetPreloadCacheScene()->game_cache;
    unsigned char css_type;
    int cpu;
    int stage;

    css->ko_counts = gmVsMelee_GetKOCounts();
    css_type = tmce_eventMenu_GetCSSType(page, event);
    cpu = (signed char) tmce_eventMenu_GetCPUFighter(page, event);
    stage = (signed char) tmce_eventMenu_GetStage(page, event); /* extsb on the console */

    if (cpu != -1) {
        cache->entries[1].char_id = cpu;
    }
    if (stage != -1) {
        cache->stkind = stage;
    }
    return css_type;
}

/* ============================================================================================
 * CSS and SSS / Backup HMN And CPU Choices / Restore Event CPU.asm (onEnterCss+0x38)
 * In place of gm_801B06B0: the human's last choice from TM-CE's backup, then the CPU's: the backup
 * when the event allows every CPU, otherwise the first allowed one in its first costume.
 * ============================================================================================ */

/* CSSID (icon order) to external character ID */
static const u8 cssid_to_ckind[25] = {
    CKind_DrMario, CKind_Mario,   CKind_Luigi,   CKind_Koopa,     CKind_Peach,  CKind_Yoshi,
    CKind_Donkey,  CKind_Captain, CKind_Ganon,   CKind_Falco,     CKind_Fox,    CKind_Ness,
    CKind_PopoNana, CKind_Kirby,  CKind_Samus,   CKind_Zelda,     CKind_Link,   CKind_CLink,
    CKind_Pichu,   CKind_Pikachu, CKind_Purin,   CKind_Mewtwo,    CKind_GameWatch, CKind_Mars,
    CKind_Emblem,
};

void mu_tmce_ev_css_restore(CSSData* css, unsigned char css_type, unsigned char port)
{
    VsModeData* bak = ev_backup();
    TmceAllowedCharacters* allowed;
    s8 ckind;
    s8 color;
    int i;

    /* RESTORE HMN */
    gm_801B06B0(css, css_type, bak->start.players[0].ckind, 0, bak->start.players[0].color,
                bak->start.players[0].nametag, 0, port);

    /* RESTORE CPU */
    allowed = tmce_eventMenu_GetEventCharList(ev_id(), mu_tmce_event_page());
    if (allowed->cpu == -1) {
        color = bak->start.players[1].color;
        ckind = bak->start.players[1].ckind;
        if (ckind == CKind_Seak) {
            ckind = CKind_Zelda;
        }
    } else {
        /* the ASM loops until it finds a set bit; a mask with none of the 25 set would run away */
        for (i = 0; i < 25 && !(allowed->cpu & (1 << i)); i++) {
        }
        ckind = cssid_to_ckind[i < 25 ? i : 0];
        color = 0;
    }
    gm_801B07B4(css, ckind, 1, color, bak->start.players[1].nametag, 0, port);
}

/* ============================================================================================
 * Custom Event Preload Behavior / Load Character ssm Files.asm (onExitCss+0x34)
 * The sound banks of every character picked on the CSS (the event preload is off, see gmevent.c).
 * ============================================================================================ */
void mu_tmce_ev_css_load_ssm(CSSData* css)
{
    u64 mask = 0;
    int i;

    for (i = 0; i < 6; i++) {
        mask |= lbAudioAx_80026E84((CharacterKind) (s8) css->vs.start.players[i].ckind);
    }
    lbAudioAx_80026F2C(20);
    lbAudioAx_8002702C(4, mask);
    lbAudioAx_80027168();
}

/* ============================================================================================
 * CSS and SSS / Backup HMN And CPU Choices / Backup Event CPU.asm (onExitCss+0x48)
 * In place of gm_801B0730: the human's choice into TM-CE's backup and into the event data (as
 * vanilla does), the CPU's into TM-CE's backup.
 * ============================================================================================ */
void mu_tmce_ev_css_backup(CSSData* css, struct EventData* ev)
{
    VsModeData* bak = ev_backup();

    gm_801B0730(css, &bak->start.players[0].ckind, NULL, &bak->start.players[0].color,
                &bak->start.players[0].nametag, NULL);
    gm_801B0730(css, &ev->x2, NULL, &ev->x3, &ev->nametag, NULL);
    gm_801B07E8(css, &bak->start.players[1].ckind, NULL, (s8*) &bak->start.players[1].color,
                (s8*) &bak->start.players[1].nametag, NULL);
}

/* ============================================================================================
 * CSS and SSS / Load SSS For Certain Events.asm (onExitCss+0x58)
 * An event without a fixed stage goes to the SSS (state 2 of the table below) after the CSS.
 * ============================================================================================ */
void mu_tmce_ev_css_exit_sss(void)
{
    if (tmce_eventMenu_GetIsSelectStage(mu_tmce_event_page(), ev_id())) {
        gm_SetNextGameModeStateId(2);
    }
}

/* ============================================================================================
 * Custom Event Code - Rewrite.asm, lines 1-20 (onEnterVs+0x3B8)
 * An event with a file of its own is a C event: EventInit sets the match up and onEnterVs skips to
 * its end (0x801BB738). Otherwise it is one of the original events: the legacy prologue.
 * ============================================================================================ */
int mu_tmce_ev_enter_vs(StartMeleeData* md, int event_id, struct gm_804D6900_t** level_slot)
{
    int page = mu_tmce_event_page();

    if (tmce_eventMenu_GetEventFile(page, event_id) != NULL) {
        tmce_eventMenu_EventInit(page, event_id, md);
        return 1;
    }
    mu_tmce_legacy_enter_vs(md, page, event_id, level_slot);
    return 0;
}

/* ============================================================================================
 * A+B to Reset Event / Check for Reset on LRAStart.asm (onExitVs+0xEC) and
 * Check for Reset on Game End.asm (onExitVs+0x2E0): A and B held on any controller (index 4 is
 * all of them) restarts the event instead of going back to the menu.
 * ============================================================================================ */
int mu_tmce_ev_ab_reset(void)
{
    u32 held = (u32) gm_GetButtonsPressed(4);
    return (held & HSD_PAD_A) && (held & HSD_PAD_B);
}

/* ============================================================================================
 * CSS and SSS / Add SSS To Event Match Scene List.asm (runGameMode+0x11C)
 * The console overwrites the event mode's scene list pointer (0x803DB010) with a CSS / match / SSS
 * list and the main menu's (0x803DAE44) with a main menu / credits list. Natively the two tables
 * below take the vanilla entries and add the new state, and the mode table points at them while
 * TM-CE is on (and back at the vanilla ones when it is not).
 * ============================================================================================ */

/* the SSS state's own data: on the console the SSS reads and writes the event's CSS data through
 * the SSS layout, which natively is not the CSS layout (CSSData's KO pointer is 8 bytes), so the
 * choices are carried across instead */
static SSSData ev_sss_data;

static CSSData* ev_css_data(void)
{
    return gm_Mode_Event_States[0].info.enter_data;
}

/* gm_801B1EB8, the training mode's SSS enter, which the console's list points at */
static void ev_sss_on_enter(GameModeState* state)
{
    SSSData* sss = state->info.enter_data;
    sss->vs = ev_css_data()->vs;
    sss->x1 = 0;
    sss->force_stage_id = -1;
    sss->unk_stage = 0;
}

/* SSS_SceneDecide: back to the CSS, or on to the match */
static void ev_sss_on_exit(GameModeState* state)
{
    SSSData* sss = state->info.exit_data;
    if (sss->start_game == 0) {
        gm_SetNextGameModeStateId(0);
        return;
    }
    ev_css_data()->vs = sss->vs;
    gm_SetNextGameModeStateId(1);
}

/* Credits_SceneDecide stores 0 as the next minor, which the state machine has already cleared on
 * the way in: back to the main menu (the first state) */
static void ev_credits_on_exit(GameModeState* state)
{
    (void) state;
}

static GameModeState ev_event_states[4];
static GameModeState ev_menu_states[3];
static GameModeState* ev_event_vanilla;
static GameModeState* ev_menu_vanilla;
static int ev_swapped;

static GameMode* ev_find_mode(u8 kind)
{
    GameMode* m;
    for (m = gm_GetAllGameModes(); m->kind != GM_COUNT; m++) {
        if (m->kind == kind) {
            return m;
        }
    }
    return NULL;
}

void mu_tmce_ev_scene_lists(int active)
{
    GameMode* event;
    GameMode* menu;

    if (!active == !ev_swapped) {
        return; /* nothing to change (with TM-CE never on: always this) */
    }
    event = ev_find_mode(GM_EVENT);
    menu = ev_find_mode(GM_MENU);
    if (event == NULL || menu == NULL) {
        return;
    }
    if (!active) {
        event->states = ev_event_vanilla;
        menu->states = ev_menu_vanilla;
        ev_swapped = 0;
        return;
    }
    ev_event_vanilla = event->states;
    ev_menu_vanilla = menu->states;

    /* Event: CSS (0), In-Match (1), SSS (2) */
    ev_event_states[0] = ev_event_vanilla[0];
    ev_event_states[1] = ev_event_vanilla[1];
    ev_event_states[2].id = 2;
    ev_event_states[2].preload = lbDvdPreload_3;
    ev_event_states[2].flags = 0;
    ev_event_states[2].on_enter = ev_sss_on_enter;
    ev_event_states[2].on_exit = ev_sss_on_exit;
    ev_event_states[2].info.scene_kind = GS_SSS;
    ev_event_states[2].info.enter_data = &ev_sss_data;
    ev_event_states[2].info.exit_data = &ev_sss_data;
    ev_event_states[3].id = (u8) -1;

    /* MainMenu (0), Credits (1) */
    ev_menu_states[0] = ev_menu_vanilla[0];
    ev_menu_states[1].id = 1;
    ev_menu_states[1].preload = lbDvdPreload_3;
    ev_menu_states[1].flags = 0;
    ev_menu_states[1].on_enter = NULL;
    ev_menu_states[1].on_exit = ev_credits_on_exit;
    ev_menu_states[1].info.scene_kind = GS_STAFFROLL;
    ev_menu_states[1].info.enter_data = NULL;
    ev_menu_states[1].info.exit_data = NULL;
    ev_menu_states[2].id = (u8) -1;

    event->states = ev_event_states;
    menu->states = ev_menu_states;
    ev_swapped = 1;
}

/* ============================================================================================
 * CSS and SSS / Display Event Name on CSS.asm (mnCharSel_802640A0+0x464)
 * ============================================================================================ */
void mu_tmce_ev_css_event_name(void)
{
    HSD_Text* text;
    char* name;
    int line;

    if (gm_GetCurrentGameMode() != GM_EVENT) {
        return;
    }
    name = tmce_eventMenu_GetEventName(mu_tmce_event_page(), ev_id());
    if (name == NULL) {
        return;
    }
    text = HSD_SisLib_803A6754(0, 0);
    text->default_kerning = 1;   /* tight spacing */
    text->default_alignment = 1; /* centered on x */
    text->font_size.x = 0.1f;
    text->font_size.y = 0.1f;
    line = HSD_SisLib_803A6B98(text, 38.0f, -245.0f, "%s", name);
    HSD_SisLib_803A7548(text, line, 0.65f, 0.65f);
}

/* ============================================================================================
 * CSS and SSS / Toggle CSS Icon Vibibility.asm (mnCharSel_802640A0+0x4D8)
 * A proc after the CSS's own shows only the icons the event allows: the human list while the P1
 * hand is free or holds the P1 puck, the CPU list while it holds the CPU puck. With a human list,
 * P1 starts undecided.
 * ============================================================================================ */
static void ev_css_icon_proc(HSD_GObj* gobj)
{
    TmceAllowedCharacters* allowed = *(TmceAllowedCharacters**) gobj->user_data;
    unsigned char hand;
    unsigned char puck;
    int mask;
    int i;

    mu_tmce_css_cursor0(&hand, &puck);
    if (hand != 1 || puck == 0) {
        mask = allowed->hmn;
    } else if (puck == 1) {
        mask = allowed->cpu;
    } else {
        return;
    }
    for (i = 24; i >= 0; i--) {
        mu_tmce_css_icon_show(i, (mask >> i) & 1);
    }
}

void mu_tmce_ev_css_icons_init(CSSData* css)
{
    HSD_GObj* gobj;
    TmceAllowedCharacters** data;
    TmceAllowedCharacters* allowed;

    if (gm_GetCurrentGameMode() != GM_EVENT) {
        return;
    }
    gobj = GObj_Create(4, 5, 0);
    data = HSD_MemAlloc(sizeof(*data));
    GObj_InitUserData(gobj, 14, HSD_Free, data);
    HSD_GObj_SetupProc(gobj, ev_css_icon_proc, 2); /* after the CSS's input proc */

    allowed = tmce_eventMenu_GetEventCharList(ev_id(), mu_tmce_event_page());
    *data = allowed;
    if (allowed->hmn == -1) {
        return;
    }
    /* Make P1 Undecided: the player in control of the CSS (1-based) */
    if (css->unk_0x0 >= 1 && css->unk_0x0 <= 6) {
        css->vs.start.players[css->unk_0x0 - 1].ckind = ChKind_None;
    }
}

/* ============================================================================================
 * CSS and SSS / Overwrite VS Text.asm (mnCharSel_802640A0+0x2274)
 * In event mode the rules text (mnCharSel_8025BD30) is left empty.
 * ============================================================================================ */
int mu_tmce_ev_css_vs_text(void)
{
    if (gm_GetCurrentGameMode() != GM_EVENT) {
        return 0;
    }
    HSD_SisLib_803A6530(0, 0x4A, 0);
    HSD_SisLib_803A660C(0, 0x4A, 0);
    return 1;
}

/* ============================================================================================
 * Hooks/OnCSSLoad.asm (mnCharSel_802640A0+0x2770)
 * An event with a CSS file of its own (the lab's TM/labCSS.dat) runs its cssFunction.
 * ============================================================================================ */
void mu_tmce_ev_css_on_load(void)
{
    void (*css_function)(HSD_Archive*) = NULL;
    HSD_Archive* archive;
    char* file;

    if (gm_GetCurrentGameMode() != GM_EVENT) {
        return;
    }
    file = tmce_eventMenu_GetCSSFile(mu_tmce_event_page(), ev_id());
    if (file == NULL) {
        return;
    }
    archive = MEX_LoadRelArchive(file, &css_function, (char*) "cssFunction");
    if (css_function != NULL) {
        css_function(archive);
    }
}
