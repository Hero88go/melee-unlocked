/* The online menu's two pages, included at the end of mu_slippi_menu.c (it shares that file's state).
 *
 * Owner's pick (11-18, concept 5 of the menu concepts): one page of YouGame's five rows and one of
 * Slippi's four (no Slippi Ranked in this build, owner 11-18), every label naming its service ("YouGame Ranked", "Slippi Direct"); Z flips the whole
 * page and the heading stays Online Play. slippi_submenu is the page shown: 0 YouGame, 1 Slippi. The
 * same rows under Melee Unlocked's launcher (split layout) and in the YouGame app (app layout); each
 * row keeps its layout's own pick (the split layout's 1-byte pick: 1 YouGame, 0 Slippi; the app's
 * Friends picker, Doubles home and Tournament).
 *
 * Unranked is a slider (owner 11-18): YouGame Unranked | Both Unranked | Slippi Unranked, stepped by
 * the d-pad or stick left / right or by L / R (no wrap; the Rules menu's chevrons show on a side that
 * has another value). Each page starts on its own service; the last value is remembered by the host
 * (online-menu.json) and a page turn resets it to the new page's own. The plate's label and the
 * description follow the value (Both: "Searching YouGame and Slippi."); the pick goes to that service
 * (Both sends 2, CMD_YOUGAME_MENU 0xEF, one byte). */

#include "mu_test_ui.h"
static int pages(void) { return ranked_supported || split_layout(); }
int mu_online_pages_page(void) { return slippi_submenu; }

/* UI audit preview 9 (mu_test_ui.h): a page turn keeps the old page's heading, Z hint and description
 * until the new page's rows arrive. pages_turn is set by the Z press and cleared by the first frame
 * of the new rows (another menu object, or the same one once it is past its leaving states). */
static int pages_turn;                 /* a page turn is under way (preview 9 only) */
static HSD_GObj* pages_turn_menu;      /* the menu object that showed the old page */
static int pages_turn_left;            /* that object was seen leaving */
static int pages_turn_frames;
static HSD_Text* pages_turn_text;      /* the old description, kept on screen through the turn */
static HSD_Text* pages_hint_text;      /* preview A1: the Z hint beside the heading */
static HSD_GObj* pages_sized_menu;     /* preview S5: the menu object whose rows were resized */
static int pages_hint_page = -1;
static int pages_shown(void) { return pages_turn ? !slippi_submenu : slippi_submenu; }
int mu_online_pages_heading(void) { return pages_shown(); }
int mu_slippi_online_pages(void) { return mu_slippi_menus_enabled() && pages(); }

/* The host's remembered choices (0xEF 0x44): bit 1 Unranked on both (older files), bits 2-3 Unranked's
 * value (0 the page's own, 1 YouGame, 2 Both, 3 Slippi). Bit 0, the page last shown, is no longer read
 * or written: the menu always opens on YouGame's page (owner 11-19). */
static int pages_prefs_read;
static void pages_load_prefs(void)
{
    u8 op = 0x44, r = 0;
    unsigned int got = 0;
    if (pages_prefs_read) return;
    if (mu_slippi_cmd(CMD_YOUGAME_MENU, &op, 1, &r, 1, &got) == 0 && got == 1) {
        pages_prefs_read = 1;
        unranked_val = (r >> 2 & 3) ? (r >> 2 & 3) - 1 : (r & 2) ? 1 : -1;
    }
}
static void pages_save(u8 op)
{
    mu_slippi_cmd(CMD_YOUGAME_MENU, &op, 1, NULL, 0, NULL);
}

/* YouGame's rows can be picked (the launcher's account is in, or picking one starts YouGame). */
static int pages_yougame_open(void)
{
    return ranked_supported || (yg_flags & (YG_ROWS_OPEN | YG_COMING_SOON)) != 0;
}
static int pages_slippi_open(void)
{
    return ranked_supported ? upstream_slippi_available() : slp.app_state == MU_SLP_APP_LOGGED_IN;
}
/* The page the menu opens on: always YouGame's (owner 11-19: "always open to the YouGame selectors",
 * the page Z turned to is not remembered). Its rows are always drawn; one picked before YouGame is signed
 * in says so or starts YouGame. Slippi's page is one Z away. */
static int pages_start(void)
{
    pages_load_prefs();   /* the slider's remembered value */
    /* Not signed in to YouGame but signed in to Slippi: Slippi's page (Teams, Unranked, Direct) first.
     * Players without YouGame opened on rows that all refused, and asked where Slippi Doubles went
     * (player report, 0.9.992). Signed in to YouGame: its page first, as before. Z flips either way. */
    if (!agent_copy && !(yg_flags & YG_ROWS_OPEN) && slp.app_state == MU_SLP_APP_LOGGED_IN) return 1;
    return 0;
}

static int pages_unranked_value(void)
{
    if (agent_copy) return 0;
    return unranked_val >= 0 ? unranked_val : slippi_submenu ? 2 : 0;
}
/* The hovered row's service is Slippi's (Unranked follows its slider; Both counts as YouGame's here,
 * whose sign-in it needs first). */
static int pick_slippi(void)
{
    if (pages() && mu_slippi_menu_option(mn_804A04F0.hovered_selection) == MU_SLP_MODE_UNRANKED)
        return pages_unranked_value() == 2;
    return slippi_submenu;
}

static int pages_label_of(int option, int page)
{
    switch (option) {
    case MU_SLP_MODE_RANKED: return MP_L_YG_RANKED;   /* YouGame's only: no Slippi Ranked in this build */
    case MU_SLP_MODE_UNRANKED:
        (void) page;
        switch (pages_unranked_value()) {
        case 1: return MP_L_BOTH_UNRANKED;
        case 2: return MP_L_SLP_UNRANKED;
        default: return MP_L_YG_UNRANKED;
        }
    case MU_SLP_MODE_DIRECT: return MP_L_SLP_DIRECT;
    case MU_SLP_MODE_TEAMS: return MP_L_SLP_TEAMS;
    case MU_SLP_MODE_PARTY: return MP_L_SLP_PARTY;
    case MU_SLP_OPT_FRIENDS: return MP_L_YG_FRIENDS;
#ifdef MELEE_DOUBLES
    case MU_SLP_OPT_DOUBLES: return MP_L_YG_DOUBLES;
#endif
#if MU_SQUAD
    case MU_SLP_OPT_SQUAD: return MP_L_YG_SQUAD;
#endif
    case MU_SLP_OPT_TOURNAMENT: return MP_L_TOURNAMENT;
    default: return MP_L_NONE;
    }
}
int mu_online_pages_label(int row)
{
    if (!mu_slippi_online_pages() || mn_804A04F0.cur_menu != MENU_KIND_8) return MP_L_NONE;
    return pages_label_of(mu_slippi_menu_option(row), slippi_submenu);
}

static void pages_install(void)
{
    mu_mnmain_install_online_menu();
}

static void pages_log_rows(void)
{
    char line[200];
    int n = mu_slippi_menu_count(), i, used;
    static const char* const names[] = { "Ranked", "Unranked", "Direct", "Teams", "Party" };
    used = snprintf(line, sizeof line, "online menu: %s page:", slippi_submenu ? "Slippi" : "YouGame");
    for (i = 0; i < n && used < (int) sizeof line - 24; i++) {
        const int option = mu_slippi_menu_option(i);
        const char* name = option >= 0 && option <= MU_SLP_MODE_PARTY ? names[option] :
                           option == MU_SLP_OPT_FRIENDS ? "Friends" :
#ifdef MELEE_DOUBLES
                           option == MU_SLP_OPT_DOUBLES ? "Doubles" :
#endif
#if MU_SQUAD
                           option == MU_SLP_OPT_SQUAD ? "Squad Strike" :
#endif
                           option == MU_SLP_OPT_TOURNAMENT ? "Tournament" : "?";
        used += snprintf(line + used, sizeof line - used, "%s %s %s", i ? "," : "",
                         slippi_submenu ? "Slippi" : "YouGame", name);
    }
    snprintf(line + used, sizeof line - used, "%s\n", agent_copy ? " (agent copy: Slippi page off)" : "");
    mu_online_abi_log(line);
}

/* The cursor's row after a page turn: each row's partner on the other page. Unranked is the second row
 * of both pages (owner 11-19) and goes to Unranked; the others pair row for row: YouGame Ranked and
 * Slippi Direct, YouGame Friends and Slippi Teams, YouGame Squad Strike and Slippi Party (YouGame
 * Doubles, when its row is shown, also goes to Teams, and Teams back to it; Tournament goes to Party). */
static int pages_partner(int option, int to_slippi)
{
    if (option == MU_SLP_MODE_UNRANKED) return MU_SLP_MODE_UNRANKED;
    if (to_slippi) {
        switch (option) {
        case MU_SLP_MODE_RANKED: return MU_SLP_MODE_DIRECT;
        case MU_SLP_OPT_FRIENDS: return MU_SLP_MODE_TEAMS;
#ifdef MELEE_DOUBLES
        case MU_SLP_OPT_DOUBLES: return MU_SLP_MODE_TEAMS;
#endif
#if MU_SQUAD
        case MU_SLP_OPT_SQUAD: return MU_SLP_MODE_PARTY;
#endif
        case MU_SLP_OPT_TOURNAMENT: return MU_SLP_MODE_PARTY;
        default: return -1;
        }
    }
    switch (option) {
    case MU_SLP_MODE_DIRECT: return MU_SLP_MODE_RANKED;
    case MU_SLP_MODE_TEAMS:
#ifdef MELEE_DOUBLES
        if (mu_online_menu_option(pages(), 0, mu_online_menu_row(pages(), 0, MU_SLP_OPT_DOUBLES)) == MU_SLP_OPT_DOUBLES)
            return MU_SLP_OPT_DOUBLES;
#endif
        return MU_SLP_OPT_FRIENDS;
#if MU_SQUAD
    case MU_SLP_MODE_PARTY: return MU_SLP_OPT_SQUAD;
#endif
    default: return -1;
    }
}
static int pages_turned_row(int row, int option, int to_slippi)
{
    const int count = mu_online_menu_count(pages(), to_slippi);
    const int partner = pages_partner(option, to_slippi);
    int i;
    for (i = 0; partner >= 0 && i < count; ++i)
        if (mu_online_menu_option(pages(), to_slippi, i) == partner) return i;
    /* No partner on that page (a row not built in): the same place, within the page. */
    return row < 0 ? 0 : row >= count ? count - 1 : row;
}

/* Z: the other page, the cursor on its row's pair (pages_turned_row), sliding in as a page turn.
 * to_start: the page's opening row instead (mu_slippi_first_unlocked), for the turn the menu makes
 * by itself right after it opened. */
static void pages_flip(int to_start)
{
    int row = mn_804A04F0.hovered_selection, count;
    const int option = mu_slippi_menu_option(row);
    slippi_submenu = !slippi_submenu;
    pages_install();
    if (mu_ui_fix("9")) {
        HSD_GObj* menu = HSD_GObj_CurrentInvokedProcGObj;
        MainMenuData* data = menu ? menu->user_data : NULL;
        pages_turn = 1; pages_turn_menu = menu; pages_turn_left = 0; pages_turn_frames = 0;
        if (pages_turn_text == NULL && data != NULL && data->description != NULL && data->description->sis_buffer != NULL) {
            /* The same words at the same place; shown from the frame the menu drops its own. */
            const HSD_Text* from = data->description;
            const unsigned char* src = from->sis_buffer;
            HSD_Text* copy = HSD_SisLib_803A6754(0, mn_804D6BB4);
            int n = 0;
            while (n < 1000 && src[n]) {
                /* SIS command lengths (HSD_SisLib_803A6478); a jump (8, 9) is never copied. */
                const unsigned op = src[n];
                if (op == 8 || op == 9) { n = 1000; break; }
                n += op >= 0x20 ? 2 : op == 5 ? 3 : (op == 6 || op == 7 || op == 10 || op == 14) ? 5 : op == 12 ? 4 : 1;
            }
            if (copy != NULL && n < 1000) {
                SisBuffer* alloc = (SisBuffer*) HSD_SisLib_Alloc(sizeof *alloc);
                if (alloc != NULL && (alloc->data = (u8*) HSD_SisLib_Alloc(n + 1)) != NULL) {
                    memcpy(alloc->data, src, n); alloc->data[n] = 0;
                    alloc->end = alloc->data + n; alloc->size = n + 1; alloc->count = 0;
                    copy->pos_x = from->pos_x; copy->pos_y = from->pos_y; copy->pos_z = from->pos_z;
                    copy->box_size_x = from->box_size_x; copy->box_size_y = from->box_size_y;
                    copy->x14 = from->x14; copy->font_size = from->font_size;
                    copy->bg_color = from->bg_color; copy->text_color = from->text_color;
                    copy->x34 = from->x34; copy->x3C = from->x3C; copy->x44 = from->x44; copy->x46 = from->x46;
                    copy->default_fitting = from->default_fitting; copy->default_kerning = from->default_kerning;
                    copy->default_alignment = from->default_alignment; copy->x4B = from->x4B; copy->x4C = from->x4C;
                    copy->font_idx = from->font_idx;
                    copy->alloc_data = alloc; copy->sis_buffer = alloc->data; copy->x60 = NULL;
                    copy->hidden = 1;
                    pages_turn_text = copy;
                } else {
                    if (alloc != NULL) HSD_SisLib_Free(alloc);
                    HSD_SisLib_803A5CC4(copy);
                }
            } else if (copy != NULL) HSD_SisLib_803A5CC4(copy);
        }
    }
    /* Owner 11-18: Z from YouGame or Both goes to Slippi, Z from Slippi back to YouGame; Unranked always
     * takes the new page's own value. */
    if (unranked_val >= 0) { unranked_val = -1; pages_save(0x45); }
    if (!mu_ui_fix("9")) pages_log_rows();   /* pages_install has logged the rows already */
    count = mu_slippi_menu_count();
    row = to_start ? mu_slippi_first_unlocked() : pages_turned_row(row, option, slippi_submenu);
    if (row >= count) row = count - 1;
    if (!mn_80229938(MENU_KIND_8, row)) row = mu_slippi_first_unlocked();
    mn_804D6BC8.cooldown = 5;
    mn_804A04F0.entering_menu = slippi_submenu ? 1 : 0;
    if (slippi_submenu) sfxForward(); else sfxBack();
    mn_80229894(MENU_KIND_8, (u16) row, slippi_submenu ? MENU_STATE_ENTER_TO : MENU_STATE_EXIT_TO);
    slp.force_clear = 1;
}

/* Left / right on Unranked: one step along YouGame | Both | Slippi (remembered). */
static void pages_unranked_step(int dir)
{
    const int v = pages_unranked_value() + dir;
    static const char* const names[] = { "YouGame Unranked", "Both Unranked", "Slippi Unranked" };
    if (v < 0 || v > 2) return;
    unranked_val = v;
    pages_save((u8) (0x46 + v));
    OSReport("online menu: Unranked slider: %s\n", names[v]);
    pages_refresh = 1;
    sfxMove();
}

/* A notice in the description box until the cursor moves (a row that cannot be picked yet). */
static void pages_notice_show(const char* text)
{
    pages_notice = text;
    pages_notice_row = mn_804A04F0.hovered_selection;
}

/* The slider's arrows are the game's own value arrows from the Rules menu (MenMainCursorRl_Top in
 * MnMaAll: parts 15 and 14, the chevrons beside a rule's value, with their model, material and
 * animation). One copy is parented to the hovered Unranked plate's own joint, so the arrows sit in the
 * menu's 3D scene and move, tilt and scale with the plate's hover animation. The rest of that cursor
 * (its plate, label, highlight) is hidden; the arrows keep their own yellow material. Owner's pick
 * 11-18: both chevrons after the plate's ring, 3 px from it and 4 px apart (1440x1080). Positions and
 * scale are in the plate joint's units; each chevron's mesh sits K * scale off its joint, opposite
 * ways for the two turned chevrons. */
extern StaticModelSyms MenMainCursorRl_Top;
#define PAGES_ARROW_LX 7.6384f      /* the chevron pointing left */
#define PAGES_ARROW_RX 7.8151f      /* the chevron pointing right */
#define PAGES_ARROW_Y (-0.0660f)
#define PAGES_ARROW_LY 0.0115f      /* the left chevron's own lift */
#define PAGES_ARROW_Z 0.1f
#define PAGES_ARROW_K 0.312f
#define PAGES_ARROW_SCALE 0.6f
static AnimLoopSettings pages_arrows_anim = { 0.0F, 199.0F, 0.0F };   /* the Rules menu's value arrows */
static void pages_texts_forget(void)
{
    /* A new menu scene: the last one's texts went with it. */
    pages_turn_text = NULL; pages_hint_text = NULL; pages_hint_page = -1; pages_turn = 0; pages_sized_menu = NULL;
}
/* No joint is kept from one frame to the next: the copy is found again each frame in the tree of the
 * menu object that is running. A menu left with B or by a page turn is freed with its rows, and HSD hands
 * their joints to the next menu's rows; a kept pointer then hid or moved a part of another row (Slippi
 * Party without its plate, owner 11-19). The cursor's plate group has five children of its own
 * (row_geo_apply); the copy is the sixth. */
enum { PAGES_GROUP_KIDS = 5 };
static HSD_JObj* pages_row_group(MainMenuData* data, int row)
{
    HSD_JObj *slot, *group = NULL;
    if (data == NULL || row < 0 || row >= 10 || !mn_80229938(MENU_KIND_8, row)) return NULL;
    slot = data->tree[4 + mn_80229A04(MENU_KIND_8, row)];   /* mn_803EAE68: rows hang from tree 4.. */
    if (slot == NULL || slot->child == NULL) return NULL;
    lb_80011E24(slot->child, &group, 1, -1);
    return group;
}
static HSD_JObj* pages_row_label(MainMenuData* data, int row)
{
    HSD_JObj *slot, *label = NULL;
    if (data == NULL || row < 0 || row >= 10 || !mn_80229938(MENU_KIND_8, row)) return NULL;
    slot = data->tree[4 + mn_80229A04(MENU_KIND_8, row)];
    if (slot == NULL || slot->child == NULL) return NULL;
    lb_80011E24(slot->child, &label, 3, -1);   /* the cursor part the menu's hover hands over (mn_803EAE7C[1]) */
    return label;
}
static HSD_JObj* pages_arrows_of(HSD_JObj* group)
{
    HSD_JObj *j, *found = NULL;
    int n = 0;
    for (j = group != NULL ? group->child : NULL; j != NULL; j = j->next)
        if (n++ == PAGES_GROUP_KIDS) found = j;
    return n == PAGES_GROUP_KIDS + 1 ? found : NULL;   /* the cursor's five, then ours */
}
static HSD_JObj* pages_arrows_load(HSD_JObj* plate)
{
    static const int hide[] = { 2, 7, 8, 16 };
    HSD_JObj *j, *part;
    int i, n = 0;
    for (part = plate->child; part != NULL; part = part->next) ++n;
    if (n != PAGES_GROUP_KIDS) return NULL;   /* not the cursor's plate group as the menu builds it */
    j = HSD_JObjLoadJoint(MenMainCursorRl_Top.joint);
    if (j == NULL) return NULL;
    HSD_JObjAddAnimAll(j, MenMainCursorRl_Top.animjoint, MenMainCursorRl_Top.matanim_joint,
                       MenMainCursorRl_Top.shapeanim_joint);
    HSD_JObjReqAnimAll(j, 0.0f);
    HSD_JObjAnimAll(j);
    for (i = 0; i < 4; ++i) {
        part = NULL;
        lb_80011E24(j, &part, hide[i], -1);
        if (part != NULL) HSD_JObjSetFlagsAll(part, JOBJ_HIDDEN);
    }
    {
        /* The menu finds the row's cursor parts by depth-first index (mn_803EAE7C: 2, 3, 4, 9, 11, 12,
         * 13); the copy must not move any of them. Checked once per build of the rows. */
        static const int used[] = { 2, 3, 4, 9, 11, 12, 13 };
        HSD_JObj* root = plate->parent;
        HSD_JObj* before[7];
        for (i = 0; i < 7; ++i) { before[i] = NULL; if (root != NULL) lb_80011E24(root, &before[i], used[i], -1); }
        HSD_JObjAddChild(plate, j);
        for (i = 0; i < 7 && root != NULL; ++i) {
            part = NULL;
            lb_80011E24(root, &part, used[i], -1);
            if (part != before[i]) OSReport("online menu: Unranked slider arrows moved cursor part %d\n", used[i]);
        }
    }
    return j;
}
/* v: Unranked's value (0 YouGame, 1 Both, 2 Slippi); a chevron shows on a side that has another value. */
static void pages_arrows_place(HSD_JObj* j, int v)
{
    HSD_JObj* part;
    /* UI audit previews C1 (about 1.6x) and C2 (about 2x): larger chevrons of equal height, still a
     * pair after the ring (mu_test_ui.h). */
    const int big = mu_ui_fix("C2") ? 2 : mu_ui_fix("C1") ? 1 : 0;
    const float s = big ? mu_ui_num(5, big == 2 ? 1.2f : 0.96f) : PAGES_ARROW_SCALE;
    const float sl = big ? s * mu_ui_num(8, 1.0f) : s;
    const float lx = big ? mu_ui_num(6, big == 2 ? 7.70f : 7.68f) : PAGES_ARROW_LX;
    const float rx = big ? mu_ui_num(7, big == 2 ? 8.00f : 7.93f) : PAGES_ARROW_RX;
    const float ya = big ? mu_ui_num(9, 0.0f) : 0.0f;
    const float ly = big ? mu_ui_num(10, PAGES_ARROW_LY) : PAGES_ARROW_LY;
    if (j == NULL) return;
    part = NULL;
    lb_80011E24(j, &part, 13, -1);
    if (part != NULL) {
        HSD_JObjClearFlagsAll(part, JOBJ_HIDDEN);
        mn_8022ED6C(part, &pages_arrows_anim);
    }
    part = NULL;
    lb_80011E24(j, &part, 15, -1);                 /* points left */
    if (part != NULL) {
        HSD_JObjSetTranslateX(part, lx);
        HSD_JObjSetTranslateY(part, PAGES_ARROW_Y + ya + ly - PAGES_ARROW_K * sl);
        HSD_JObjSetTranslateZ(part, PAGES_ARROW_Z);
        HSD_JObjSetScaleX(part, sl); HSD_JObjSetScaleY(part, sl);
        if (v > 0) HSD_JObjClearFlagsAll(part, JOBJ_HIDDEN); else HSD_JObjSetFlagsAll(part, JOBJ_HIDDEN);
    }
    part = NULL;
    lb_80011E24(j, &part, 14, -1);                 /* points right */
    if (part != NULL) {
        HSD_JObjSetTranslateX(part, rx);
        HSD_JObjSetTranslateY(part, PAGES_ARROW_Y + ya + PAGES_ARROW_K * s);
        HSD_JObjSetTranslateZ(part, PAGES_ARROW_Z);
        HSD_JObjSetScaleX(part, s); HSD_JObjSetScaleY(part, s);
        if (v < 2) HSD_JObjClearFlagsAll(part, JOBJ_HIDDEN); else HSD_JObjSetFlagsAll(part, JOBJ_HIDDEN);
    }
}
/* data: the menu object running now (its tree is alive). show: the arrows on its Unranked row; any
 * other copy in its rows is hidden (all of them when the object is leaving: after a page turn
 * slippi_submenu already names the new page, whose Unranked row may be another row of this one). */
static void pages_arrows_show(MainMenuData* data, int show)
{
    HSD_JObj *plate = NULL, *arrows;
    int row = -1, i;
    if (data == NULL || data->menu_kind != MENU_KIND_8) return;
    if (show) {
        row = mu_online_menu_row(pages(), slippi_submenu, MU_SLP_MODE_UNRANKED);
        /* Hang from joint 1 (the plate group), appended after its own children so the cursor's
         * depth-first part indices (labels, highlight) stay what the menu expects. */
        plate = pages_row_group(data, row);
    }
    for (i = 0; i < 10; ++i) {   /* every row slot of this object (its own page's rows) */
        HSD_JObj *slot = data->tree[4 + i], *group = NULL;
        if (slot == NULL || slot->child == NULL) continue;
        lb_80011E24(slot->child, &group, 1, -1);
        if (group == NULL || group == plate || (arrows = pages_arrows_of(group)) == NULL) continue;
        HSD_JObjSetFlagsAll(arrows, JOBJ_HIDDEN);
    }
    if (plate == NULL) return;
    arrows = pages_arrows_of(plate);
    if (arrows == NULL) {
        /* A new menu build: the copy is made with the rows' plate and freed with it. */
        arrows = pages_arrows_load(plate);
        if (arrows == NULL) return;
        OSReport("online menu: Unranked slider arrows: the Rules menu's value arrows on row %d\n", row);
    }
    pages_arrows_place(arrows, pages_unranked_value());
}

/* The description box. mu_mnmain_install_online_menu gives Friends, Doubles and Squad Strike the
 * catalog descriptions of Direct, Teams and Unranked (they share those rows' preview frames), and only
 * the YouGame app's layout writes their own words over them (mn_80229A7C, ranked_supported). Under
 * Melee Unlocked's launcher they are written here. NULL keeps the catalog's words, which are the row's
 * own: Ranked, Unranked and all of Slippi's page. Under the launcher Friends is a connect code search on
 * YouGame, Doubles' home is the app's, and Squad Strike has no Y friend invite. */
static const char* pages_row_words(int option)
{
    /* UI audit previews (mu_test_ui.h). A1: the Z hint is beside the heading, so each line is short
     * enough for the game's own letter width. A2: the hint stays, each line shortened to fit it. */
    if (mu_ui_fix("A2")) {
        switch (option) {
        case MU_SLP_MODE_RANKED: return "Play ranked.";
        case MU_SLP_MODE_UNRANKED: return "Play unranked.";
        case MU_SLP_MODE_DIRECT: return "Play by code.";
        case MU_SLP_MODE_TEAMS: return "Play teams.";
        case MU_SLP_MODE_PARTY: return "FFA with items.";
        case MU_SLP_OPT_FRIENDS: return "Play by code.";
#ifdef MELEE_DOUBLES
        case MU_SLP_OPT_DOUBLES: return "2v2 in the app.";
#endif
#if MU_SQUAD
        case MU_SLP_OPT_SQUAD: return "4 fighters each.";
#endif
        default: return NULL;
        }
    }
    if (mu_ui_fix("A1") && !ranked_supported && !slippi_submenu) {
        switch (option) {
#ifdef MELEE_DOUBLES
        case MU_SLP_OPT_DOUBLES: return "Play 2v2 in the app.";
#endif
#if MU_SQUAD
        case MU_SLP_OPT_SQUAD: return "Play 4 fighters each.";
#endif
        default: break;
        }
    }
    if (ranked_supported || slippi_submenu) return NULL;
    switch (option) {
    /* Over the local channel (split_friends) Friends is the app's friend flow, with the app's words. */
    case MU_SLP_OPT_FRIENDS: return split_friends() ? "Invite a friend to play." : "Play a friend by code.";
#ifdef MELEE_DOUBLES
    case MU_SLP_OPT_DOUBLES: return "Play 2v2 in the YouGame app.";
#endif
#if MU_SQUAD
    case MU_SLP_OPT_SQUAD: return "Play with 4 fighters each.";
#endif
    default: return NULL;
    }
}
/* A line that replaces the row's words and the Z hint: a notice, or Unranked's Both. */
static const char* pages_line(int row)
{
    const int brief = mu_ui_fix("A1") || mu_ui_fix("A2");
    if (pages_notice != NULL && row == pages_notice_row)
        return brief && !strcmp(pages_notice, "Sign in to YouGame to play online.") ? "Sign in to YouGame first." : pages_notice;
    if (mu_slippi_menu_option(row) == MU_SLP_MODE_UNRANKED && pages_unranked_value() == 1)
        return brief ? "Search both services." : "Searching YouGame and Slippi.";
    return NULL;
}
static const char* pages_hint(void)
{
    if (agent_copy || mu_ui_fix("A1")) return NULL;
    return pages_shown() ? "  Z: YouGame" : "  Z: Slippi";
}
/* Preview A1: "Z: Slippi" / "Z: YouGame" in the description's own lettering, small, in the free right
 * part of the heading tab. Made when the page is shown, remade when the page changes. */
static void pages_hint_frame(int canvas, int show)
{
    const int page = pages_shown();
    if (pages_hint_text != NULL && (!show || page != pages_hint_page)) {
        HSD_SisLib_803A5CC4(pages_hint_text);
        pages_hint_text = NULL;
    }
    if (!show || pages_hint_text != NULL) return;
    pages_hint_text = HSD_SisLib_803A6754(0, canvas);
    if (pages_hint_text == NULL) return;
    {
        static GXColor ink = { 168, 170, 210, 255 };   /* the heading's own grey */
        const float size = mu_ui_num(13, 0.030f);
        int id;
        pages_hint_text->pos_x = mu_ui_num(11, page ? 0.6f : 2.6f);
        pages_hint_text->pos_y = mu_ui_num(12, -10.35f);
        pages_hint_text->pos_z = 17.0f;
        pages_hint_text->font_size.x = size;
        pages_hint_text->font_size.y = size;
        pages_hint_text->default_kerning = 1;
        pages_hint_text->default_alignment = 0;
        id = HSD_SisLib_803A6B98(pages_hint_text, 0.0f, 0.0f, page ? "Z: YouGame" : "Z: Slippi");
        HSD_SisLib_803A74F0(pages_hint_text, id, &ink);
        pages_hint_page = page;
    }
}
static HSD_Text* pages_desc;             /* the description last written here */
static const u8* pages_desc_buffer;      /* its words as written */
static const char* pages_desc_line;      /* pages_line when written */
static void pages_describe(HSD_Text* text, int row)
{
    const char* line = pages_line(row);
    const char* own = line ? NULL : pages_row_words(mu_slippi_menu_option(row));
    if (line) mu_slippi_menu_description(text, line);
    else {
        if (own) mu_slippi_menu_description(text, own);
        if (pages_hint()) mu_menu_description_append(text, pages_hint());
    }
    pages_desc = text;
    pages_desc_buffer = text->sis_buffer;
    pages_desc_line = line;
}
/* mn_80229A7C, each new description of the online menu (a cursor move, a page turn, the re-hover). The
 * text object and its words are allocated again each time, often at the addresses just freed, so the
 * frame proc below can't tell a new description from the last one by its pointers. */
void mu_online_pages_describe(HSD_Text* text, int row)
{
    if (text == NULL || text->sis_buffer == NULL || !mu_slippi_online_pages()) return;
    pages_describe(text, row);
}

/* Each frame from the menu's own proc: the description's Z hint or a notice, the slider's arrows. */
static void pages_frame(HSD_GObj* menu, int canvas, int idle)
{
    MainMenuData* data = menu ? menu->user_data : NULL;
    const int row = mn_804A04F0.hovered_selection;
    static int since_entry;
    (void) canvas;
    if (data == NULL || data->menu_kind != MENU_KIND_8) {
        /* Another menu: the online menu's rows are leaving or freed; nothing of theirs is touched
         * (they hid their arrows on their own last frame, below). */
        since_entry = 0;
        pages_player_flipped = 0;
        pages_flip_pending = 0;
        if (mn_804A04F0.cur_menu != MENU_KIND_8) {
            pages_hint_frame(canvas, 0);
            pages_turn = 0;
            if (pages_turn_text != NULL) { HSD_SisLib_803A5CC4(pages_turn_text); pages_turn_text = NULL; }
        }
        return;
    }
    if (pages_turn) {
        /* Preview 9. The old rows: their own description stays until the menu drops it, then the kept
         * copy shows. The new rows: the turn is over, the heading and hint follow, the copy goes once
         * the new description is up. */
        const int leaving = data->state == MENU_STATE_EXIT_FROM || data->state == MENU_STATE_ENTER_FROM;
        if (menu == pages_turn_menu && leaving) pages_turn_left = 1;
        if (menu != pages_turn_menu || (pages_turn_left && !leaving) || ++pages_turn_frames > 40) pages_turn = 0;
        else {
            if (pages_turn_text != NULL) pages_turn_text->hidden = data->description != NULL;
            pages_arrows_show(data, 0);
            return;
        }
    }
    /* Preview S5: Slippi's four rows at the five-row page's plate size and spacing (the menu sizes its
     * rows by their count, so four draw larger than YouGame's five): each row takes the five-row pose. */
    if (slippi_submenu && mu_ui_fix("S5") && menu != pages_sized_menu) {
        const int count = mu_slippi_menu_count();
        int i;
        for (i = 0; i < count && i < 8; ++i) {
            HSD_JObj* slot = data->tree[4 + mn_80229A04(MENU_KIND_8, i)];
            if (slot == NULL) continue;
            HSD_JObjReqAnim(slot, mu_ui_num(14, 5.0f));
            HSD_JObjAnim(slot);
        }
        pages_sized_menu = menu;
    }
    if (pages_turn_text != NULL && (data->description != NULL || !mu_ui_fix("9"))) {
        HSD_SisLib_803A5CC4(pages_turn_text);
        pages_turn_text = NULL;
    }
    if (mu_ui_fix("A1")) pages_hint_frame(canvas, !agent_copy);
    /* The page was chosen before YouGame's sign-in finished (the launcher's first answer can take a few
     * seconds): Slippi's page, with Slippi signed out. Look again twice a second for five seconds and
     * move to YouGame's page as soon as it is open. A Z press in that time is the player's choice. */
    if (++since_entry < 300 && idle && slippi_submenu && !agent_copy && !pages_player_flipped && !pages_slippi_open() &&
        since_entry % 30 == 0) {
        mu_slippi_fetch_app_state();
        if (pages_yougame_open()) {
            OSReport("online menu: YouGame's sign-in arrived after the menu opened; YouGame page\n");
            since_entry = 300;
            pages_flip_pending = 1;   /* turned by mu_slippi_online_menu_think, not from this drawing proc */
            return;
        }
    }
    if (pages_refresh && idle && data->hovered_selection == row) {
        /* The row's label and description are set again where they are. This used to be done by
         * moving the cursor away and back for a frame, which played the leave animation on another
         * row and the hover animation again on this one: the whole menu flashed (owner 11-19). */
        mu_online_pages_relabel(pages_row_label(data, row), row);
        mu_mnmain_redescribe(menu);   /* makes the description again; pages_describe writes this page's words */
        pages_refresh = 0;
    }
    if (pages_notice != NULL && row != pages_notice_row) pages_notice = NULL;
    if (idle && data->description != NULL && data->description->sis_buffer != NULL) {
        HSD_Text* text = data->description;
        const char* line = pages_line(row);
        if (text != pages_desc) {
            pages_describe(text, row);   /* a description not made by mn_80229A7C */
        } else if (line != pages_desc_line) {
            /* A notice or Both came: written now. Both went: the slider's re-hover (pages_refresh)
             * makes the row's description again next frame. */
            if (line) pages_describe(text, row);
            else pages_desc_line = NULL;
        } else if (text->sis_buffer != pages_desc_buffer) {
            /* Another notice rewrote the words in place (the app's sign-in or tournament notice): the
             * page's line or Z hint follows it, as before. */
            if (line) mu_slippi_menu_description(text, line);
            else if (pages_hint()) mu_menu_description_append(text, pages_hint());
            pages_desc_buffer = text->sis_buffer;
        }
    }
    /* The Unranked row's arrows show whichever row is hovered (owner 11-18): they say the row has other
     * values, so they stay on its plate like its label does. */
    (void) row;
    pages_arrows_show(data, !agent_copy && mn_804A04F0.cur_menu == MENU_KIND_8 &&
                            data->state != MENU_STATE_EXIT_FROM && data->state != MENU_STATE_ENTER_FROM);
}
