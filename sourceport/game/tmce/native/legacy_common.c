/* Training Mode CE, native build: the shared routines of UnclePunch's original events, from the helper
 * block of Custom Event Code - Rewrite.asm (lines 8600-12371) and the few routines and tables both
 * halves of the event code use. Hand-written C, one function per routine label (legacy_common.h).
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_common.h"

/* ---- game functions MexTK has no name for (decomp names) ---- */
void mu_lg_fn_800D9CE8(GOBJ *gobj) __asm__("fn_800D9CE8");             /* AS_GrabOpponent */
void mu_lg_fn_800DAADC(GOBJ *gobj, GOBJ *grabber) __asm__("fn_800DAADC"); /* AS_Grabbed */
void mu_lg_fn_800DA1D8(GOBJ *gobj) __asm__("fn_800DA1D8");             /* AS_CatchWait */
int mu_lg_gmMainLib_8015CEFC(int event) __asm__("gmMainLib_8015CEFC");   /* Events_CheckIfEventWasPlayedYet */
double mu_frsqrte(double x);                                             /* the table-driven frsqrte (math/estimates.c) */
void tmce_eventMenu_StartOSDs(void);                                      /* TM_StartOSDs */

LgScore lg_score;

/* ============================================================================================
 * Tables
 * ============================================================================================ */

/* LedgeCliffIDs: per external stage id (Dummy, TEST, FoD, Pokemon Stadium, ...) */
const u16 lg_LedgeCliffIDs[34] = {
    0xFFFF, 0xFFFF, /* Dummy, TEST */
    0x0307, 0x3336, /* FoD, Pokemon Stadium */
    0x030D, 0x2945, /* Peach's Castle, Kongo Jungle */
    0x0511, 0x091A, /* Brinstar, Corneria */
    0x0206, 0x1517, /* Yoshi's Story, Onett */
    0x0000, 0x434C, /* Mute City, Rainbow Cruise */
    0x0000, 0x0000, /* Jungle Japes, Great Bay */
    0x0E0D, 0x0000, /* Hyrule Temple, Brinstar Depths */
    0x0005, 0x1E2E, /* Yoshi's Island, Green Greens */
    0x0C0E, 0x0204, /* Fourside, MKI */
    0x0305, 0x0000, /* MKII, Akaneia */
    0x0612, 0x0000, /* Venom, PokeFloats */
    0xD7E2, 0x0000, /* Big Blue, Icicle Mountain */
    0x0000, 0x0000, /* Icetop, Flatzone */
    0x0305, 0x030B, /* Dream Land, Yoshis Island 64 */
    0x0610, 0x0005, /* Kongo Jungle 64, Battlefield */
    0x0002, 0x0101, /* Final Destination, (the table's last half) */
};

/* StageGroundIDs_Main: ground IDs to start on */
const u16 lg_StageGroundIDs_Main[34] = {
    0xFFFF, 0xFFFF, /* Dummy, TEST */
    0x0005, 0x0022, /* FoD, Pokemon Stadium */
    0x0005, 0x0037, /* Peach's Castle, Kongo Jungle */
    0x000B, 0x0012, /* Brinstar, Corneria */
    0x0003, 0x0019, /* Yoshi's Story, Onett */
    0x0000, 0x0048, /* Mute City, Rainbow Cruise */
    0x0000, 0x0024, /* Jungle Japes, Great Bay */
    0x0019, 0x0007, /* Hyrule Temple, Brinstar Depths */
    0x000E, 0x0026, /* Yoshi's Island, Green Greens */
    0x0004, 0x0003, /* Fourside, MKI */
    0x0004, 0x0000, /* MKII, Akaneia */
    0x0001, 0x0105, /* Venom, PokeFloats */
    0x00D9, 0x015B, /* Big Blue, Icicle Mountain */
    0x0000, 0x0064, /* Icetop, Flatzone */
    0x0004, 0x0009, /* Dream Land, Yoshis Island 64 */
    0x000B, 0x0001, /* Kongo Jungle 64, Battlefield */
    0x0001, 0x0000, /* Final Destination */
};

/* StageGroundIDs_Platform: platform ground IDs to start on */
const u16 lg_StageGroundIDs_Platform[34] = {
    0xFFFF, 0xFFFF, /* Dummy, TEST */
    0x0002, 0x0023, /* FoD, Pokemon Stadium */
    0xFFFF, 0xFFFF, /* Peach's Castle, Kongo Jungle */
    0xFFFF, 0xFFFF, /* Brinstar, Corneria */
    0x0004, 0xFFFF, /* Yoshi's Story, Onett */
    0xFFFF, 0xFFFF, /* Mute City, Rainbow Cruise */
    0xFFFF, 0xFFFF, /* Jungle Japes, Great Bay */
    0xFFFF, 0xFFFF, /* Hyrule Temple, Brinstar Depths */
    0xFFFF, 0xFFFF, /* Yoshi's Island, Green Greens */
    0xFFFF, 0xFFFF, /* Fourside, MKI */
    0xFFFF, 0xFFFF, /* MKII, Akaneia */
    0xFFFF, 0xFFFF, /* Venom, PokeFloats */
    0xFFFF, 0xFFFF, /* Big Blue, Icicle Mountain */
    0xFFFF, 0xFFFF, /* Icetop, Flatzone */
    0x0002, 0xFFFF, /* Dream Land, Yoshis Island 64 */
    0xFFFF, 0x0003, /* Kongo Jungle 64, Battlefield */
    0xFFFF, 0xFFFF, /* Final Destination */
};

/* PlacePlayersCenterStage_Constants: facing direction and X offset per player slot. On the console
 * slots 2-5 read on into the next instructions (lmw r20,8(r1); lwz r0,0x104(r1)); kept as such. */
const s8 lg_PlacePlayersCenterStage_Constants[4] = { 1, -6, -1, 6 };
static const s8 place_center_constants[12] = { 1, -6, -1, 6, (s8) 0xBA, (s8) 0x81, 0x00, 0x08, (s8) 0x80, 0x01, 0x01, 0x04 };

static const u16 *stage_table_entry(const u16 *table)
{
    int ext = Stage_GetExternalID();
    if (ext < 0 || ext >= 34) {
        OSReport("[tmce] legacy events: no table entry for stage %d\n", ext);
        ext = 0;
    }
    return &table[ext];
}

int lg_StageGetGroundID_Main(void) { return *stage_table_entry(lg_StageGroundIDs_Main); }
int lg_StageGetGroundID_Platform(void) { return *stage_table_entry(lg_StageGroundIDs_Platform); }

/* ============================================================================================
 * Event and option menu GObjs
 * ============================================================================================ */

/* TM_GameFrameCounter (-0x49A8(r13)): TM-CE counts the match's frames from its start (Better VS Mode
 * Frame Counter, 0x8016E744 / 0x8016D310, ahead of the frame's GObj procs). Natively the legacy events
 * count them themselves: a proc of priority 0 runs ahead of the event's think each frame, so the
 * think sees 1 on its first frame, as on the console. */
static int lg_frame_counter;
static void lg_FrameCounterThink(GOBJ *gobj) { lg_frame_counter++; }

GOBJ *lg_CreateEventThinkFunction(void (*think)(GOBJ *), int priority, const u8 *window_info, const char *window_text)
{
    GOBJ *counter_gobj;
    GOBJ *event_gobj;
    LgEventData *ed;

    lg_frame_counter = 0;
    counter_gobj = GObj_Create(0, 7, 0);
    GObj_AddProc(counter_gobj, lg_FrameCounterThink, 0);

    /* Create Event Think */
    event_gobj = GObj_Create(6, 7, 80);
    GObj_AddProc(event_gobj, think, priority);
    ed = HSD_MemAlloc(sizeof(LgEventData));
    GObj_AddUserData(event_gobj, 0, HSD_Free, ed);
    memset(ed, 0, sizeof(LgEventData));

    /* Create Option Menu Think, if the event has one */
    if (window_info != 0) {
        GOBJ *menu_gobj = GObj_Create(6, 0, 80);
        LgMenuData *md;
        GObj_AddProc(menu_gobj, lg_OptionMenuThink, 22);
        md = HSD_MemAlloc(sizeof(LgMenuData));
        GObj_AddUserData(menu_gobj, 0, HSD_Free, md);
        memset(md, 0, sizeof(LgMenuData));
        md->window_info = window_info;
        md->window_text = window_text;
        ed->menu = md;
        md->event_data = ed;
    }

    /* CreateEventThinkFunction_NoOptionMenu */
    lg_DisableHazards();
    return event_gobj;
}

void lg_OptionMenuThink(GOBJ *menu_gobj)
{
    LgMenuData *md = menu_gobj->userdata;
    int toggled_window;
    int toggled;

    /* Check If Paused (the window lives one frame at a time, as its OSD text did) */
    if (Pause_CheckStatus(1) != 2) {
        if (md->text != 0) {
            Text_Destroy(md->text);
            md->text = 0;
        }
        return;
    }
    toggled = lg_OptionWindow(md->option_memory, md->window_info, md->window_text, &toggled_window, md);
    if (toggled != 0) {
        md->option_toggled[toggled_window] = toggled;
    }
}

void lg_ClearToggledOptions(LgMenuData *md)
{
    int i;
    for (i = md->window_info[0]; i >= 0; i--) {
        md->option_toggled[i] = 0;
    }
}

/* ============================================================================================
 * Savestates
 * ============================================================================================ */
void lg_SaveState_Save(LgSaveState *ss, int skip_failsafe) { mu_tmce_legacy_savestate_save(ss, skip_failsafe); }
void lg_SaveState_Load(LgSaveState *ss) { mu_tmce_legacy_savestate_load(ss); }

int lg_SaveState_GetPlayerDataPointer(int player, int follower, GOBJ **gobj, FighterData **data)
{
    return mu_tmce_legacy_get_player(player, follower, (void **) gobj, (void **) data);
}

int lg_CheckForSaveAndLoad(LgSaveState *ss)
{
    int port;
    for (port = 0; port < 4; port++) {
        HSD_Pad *pad;
        if (Fighter_GetGObj(port) == 0) {
            continue;
        }
        pad = lg_GetInputStruct(port);
        /* Make Sure Nothing Else Is Held (the low half's bits above Z) */
        if ((pad->held & 0xFFE0) != 0) {
            continue;
        }
        if (pad->down & PAD_BUTTON_DPAD_RIGHT) {
            lg_SaveState_Save(ss, 0);
            return 0;
        }
        if (pad->down & PAD_BUTTON_DPAD_LEFT) {
            lg_SaveState_Load(ss);
            lg_SaveState_Load(ss);
            return 1;
        }
    }
    return -1;
}

/* ============================================================================================
 * Small fighter helpers
 * ============================================================================================ */
void lg_GiveFullShields(void)
{
    GOBJ *gobj;
    for (gobj = (*stc_gobj_lookup)[MATCHPLINK_FIGHTER]; gobj != 0; gobj = gobj->next) {
        FighterData *fp = gobj->userdata;
        fp->shield.health = (*stc_ftcommon)->x260;
    }
}

void lg_UpdateAllGFX(GOBJ *p2_gobj)
{
    GOBJ *follower;
    Fighter_ColAnim_Update(p2_gobj);
    follower = lg_CheckIfPlayerHasAFollower(p2_gobj, 0);
    if (follower != 0) {
        Fighter_ColAnim_Update(follower);
    }
}

void lg_GiveInvincibility(GOBJ *gobj, int frames)
{
    GOBJ *follower;
    Fighter_GivePersistentIntangibility(gobj, frames);
    follower = lg_CheckIfPlayerHasAFollower(gobj, 0);
    if (follower != 0) {
        Fighter_GivePersistentIntangibility(follower, frames);
    }
}

static void clear_cpu_inputs(FighterData *fp)
{
    fp->cpu.held = 0;
    fp->cpu.lstickX = 0;
    fp->cpu.lstickY = 0;
    fp->cpu.cstickX = 0;
    fp->cpu.cstickY = 0;
}

void lg_StoreCPUTypeAndZeroInputs(FighterData *p2)
{
    clear_cpu_inputs(p2);
    p2->cpu.ltrigger = 0;
    p2->cpu.rtrigger = 0;
}

void lg_ClearNanaInputs(GOBJ *p1_gobj, GOBJ *p2_gobj)
{
    FighterData *follower;
    if (lg_CheckIfPlayerHasAFollower(p1_gobj, &follower) != 0) {
        clear_cpu_inputs(follower);
    }
    if (lg_CheckIfPlayerHasAFollower(p2_gobj, &follower) != 0) {
        clear_cpu_inputs(follower);
    }
}

int lg_CheckIfFirstFrame(void) { return lg_frame_counter == 1; }

void lg_CurrentInputsAsLastFramesInputs(FighterData *p1)
{
    u32 held = lg_GetInputStruct(0)->held;
    lg_FtSetU32(p1, 0x65C, held);
    lg_FtSetU32(p1, 0x664, held);
    /* Check For Z Press */
    if (held & PAD_TRIGGER_Z) {
        held |= 0x80000000 | PAD_BUTTON_A;
        lg_FtSetU32(p1, 0x65C, held);
        lg_FtSetU32(p1, 0x664, held);
    }
}

GOBJ *lg_CheckIfPlayerHasAFollower(GOBJ *gobj, FighterData **follower_data)
{
    void *data;
    GOBJ *follower = mu_tmce_legacy_follower(gobj, &data);
    if (follower_data != 0) {
        *follower_data = data;
    }
    return follower;
}

float lg_Randomize_AlwaysPositive(float f) { return __builtin_fabsf(f); }
float lg_Randomize_AlwaysNegative(float f) { return -__builtin_fabsf(f); }

int lg_GetDirectionInRelationToP1(FighterData *p1, FighterData *p2)
{
    float diff = p1->phys.pos.X - p2->phys.pos.X;
    if (0.0F < diff) {
        return -1;
    }
    return 1;
}

int lg_IsAnyoneDead(void)
{
    GOBJ *gobj;
    for (gobj = (*stc_gobj_lookup)[MATCHPLINK_FIGHTER]; gobj != 0; gobj = gobj->next) {
        FighterData *fp = gobj->userdata;
        if (lg_FtFlag(fp, 0x221F, 0x08)) {
            continue; /* a follower */
        }
        if (lg_FtFlag(fp, 0x221F, 0x40)) {
            return 1; /* dead */
        }
    }
    return 0;
}

void lg_ResetStaleMoves(void)
{
    GOBJ *gobj;
    for (gobj = (*stc_gobj_lookup)[MATCHPLINK_FIGHTER]; gobj != 0; gobj = gobj->next) {
        FighterData *fp = gobj->userdata;
        memset(Fighter_GetStaleMoveTable(fp->ply), 0, 0x2C);
    }
}

int lg_CheckForActiveHitboxes(GOBJ *gobj) { return mu_tmce_legacy_hitboxes_active(gobj->userdata); }

void lg_UpdatePosition(GOBJ *gobj) { mu_tmce_legacy_update_position(gobj); }
void lg_UpdateCameraBox(GOBJ *gobj) { mu_tmce_legacy_update_camera_box(gobj); }
void lg_GetGroundCenter(int line, float *x, float *y) { mu_tmce_legacy_ground_center(line, x, y); }
void lg_PlaySFX(int sfx) { SFX_Play(sfx); }
void lg_StartOSDs(void) { tmce_eventMenu_StartOSDs(); }
void lg_EventMatch_OnWinCondition(GOBJ *event_gobj) { mu_tmce_legacy_event_win(event_gobj); }

void lg_GetAllPlayerPointers(LgPlayers *out)
{
    GOBJ *gobj[4];
    FighterData *data[4];
    int i;
    for (i = 0; i < 4; i++) {
        gobj[i] = Fighter_GetGObj(i);
        data[i] = gobj[i] != 0 ? gobj[i]->userdata : 0;
    }
    out->p1_gobj = gobj[0];
    out->p1 = data[0];
    out->p2_gobj = gobj[1];
    out->p2 = data[1];
    out->p3_gobj = gobj[2];
    out->p3 = data[2];
    out->p4_gobj = gobj[3];
    out->p4 = data[3];
}

void lg_RemoveFirstFrameInputs(void)
{
    GOBJ *gobj;
    for (gobj = (*stc_gobj_lookup)[MATCHPLINK_FIGHTER]; gobj != 0; gobj = gobj->next) {
        FighterData *fp = gobj->userdata;
        /* Remove Input Flag, Store Current Input */
        lg_FtSetFlag(fp, 0x221D, 0x10, 1);
        lg_FtSetU32(fp, 0x65C, lg_GetInputStruct(fp->pad_index)->held);
    }
}

/* ============================================================================================
 * Option window (RAndDPadChangesEventOption)
 * ============================================================================================ */
const char *lg_RAndDPadChangesEventOption_GetNextString(const char *s)
{
    const char *p = s + strlen((char *) s);
    do {
        p++;
    } while (*p == 0);
    return p;
}

const char *lg_RAndDPadChangesEventOption_GetOptionASCII(const u8 *window_info, const char *window_text, int window,
                                                         int selection, const char **title)
{
    const char *p = window_text;
    int w = 0;
    int n;

    /* Skip every window before this one: its title and its options */
    while (w != window) {
        p = lg_RAndDPadChangesEventOption_GetNextString(p);
        for (n = 0; n != window_info[1 + w] + 1; n++) {
            p = lg_RAndDPadChangesEventOption_GetNextString(p);
        }
        w++;
    }
    /* Find Option Selection */
    *title = p;
    p = lg_RAndDPadChangesEventOption_GetNextString(p);
    for (n = 0; n != selection; n++) {
        p = lg_RAndDPadChangesEventOption_GetNextString(p);
    }
    return p;
}

/* RAndDPadChangesEventOption_Floats */
static const float option_window_floats[5] = { 16.0F, 63.0F, 94.0F, -900.0F, -1350.0F };

int lg_OptionWindow(u8 *mem, const u8 *window_info, const char *window_text, int *toggled_window, LgMenuData *menu)
{
    int toggled = 0;
    int which = -1;
    int visible = window_info[0] > 2 ? 2 : window_info[0]; /* windows onscreen at once, minus 1 */
    int down = PadGetMaster(stc_match->pauser)->down;
    int play_sfx = 0;
    int v;

    if (down == PAD_BUTTON_DPAD_DOWN) {
        /* CursorDown */
        v = mem[0] + 1;
        mem[0] = v;
        if (v <= visible) {
            play_sfx = 1;
        } else {
            mem[0] = visible;
            if (mem[0] + mem[1] < window_info[0]) {
                mem[1] += 1; /* scroll down */
                play_sfx = 1;
            }
        }
    } else if (down == PAD_BUTTON_DPAD_UP) {
        /* CursorUp */
        v = mem[0] - 1;
        mem[0] = v;
        if (v >= 0) {
            play_sfx = 1;
        } else {
            mem[0] = 0;
            if (mem[0] + mem[1] > 0) {
                mem[1] -= 1; /* scroll up */
                play_sfx = 1;
            }
        }
    } else if (down == PAD_BUTTON_DPAD_RIGHT) {
        /* Increment */
        int window = mem[0] + mem[1];
        toggled = 1;
        which = window;
        v = mem[window + 2] + 1;
        mem[window + 2] = v;
        if (v > window_info[window + 1]) {
            mem[window + 2] = 0;
        }
        play_sfx = 1;
    } else if (down == PAD_BUTTON_DPAD_LEFT) {
        /* Decrement */
        int window = mem[0] + mem[1];
        toggled = 1;
        which = window;
        v = mem[window + 2] - 1;
        mem[window + 2] = v;
        if (v < 0) {
            mem[window + 2] = window_info[window + 1];
        }
        play_sfx = 1;
    }
    if (play_sfx) {
        SFX_PlayCommon(2);
    }

    /* DisplayWindow: the OSD window of area 3 (Display Message 2.0: X 19, Y -15, size 0.04, a stretched
     * dash as its background), remade every frame the game is paused */
    {
        static GXColor black = { 0, 0, 0, 0 };
        static GXColor grey = { 0xDF, 0xDF, 0xDF, 0x00 };
        static GXColor yellow = { 0xF7, 0xFF, 0x27, 0x00 };
        Text *text;
        int bg;
        int i;

        if (menu->text != 0) {
            Text_Destroy(menu->text);
        }
        text = Text_CreateText(2, *stc_match_canvas);
        menu->text = text;
        text->align = 1;
        text->kerning = 1;
        text->trans.X = 19.0F;
        text->trans.Y = -15.0F;
        text->viewport_scale.X = 0.04F;
        text->viewport_scale.Y = 0.04F;
        bg = Text_AddSubtext(text, 0.0F, -410.0F, "\x81\x5B");
        Text_SetColor(text, bg, &black);
        Text_SetScale(text, bg, 13.5F, 28.0F);

        /* Display Option Menu: a bigger background for 2 or 3 windows */
        if (visible != 0) {
            Text_SetScale(text, 0, option_window_floats[0], option_window_floats[1 + visible - 1]);
            Text_SetPosition(text, 0, 0.0F, option_window_floats[3 + visible - 1]);
            /* Display Up/Down Arrows */
            if (mem[1] != 0) {
                Text_AddSubtext(text, -210.0F, -20.0F, "^");
            }
            if (window_info[0] >= 2 && window_info[0] - mem[1] > 2) {
                Text_AddSubtext(text, -210.0F, 320.0F, "v");
            }
        }

        /* Print Each Option */
        for (i = 0; i <= visible; i++) {
            int window = mem[1] + i;
            const char *title;
            const char *option = lg_RAndDPadChangesEventOption_GetOptionASCII(window_info, window_text, window,
                                                                              mem[2 + window], &title);
            int sub = Text_AddSubtext(text, 0.0F, 0.0F + (float) (120 * i), (char *) title);
            Text_SetColor(text, sub, &grey);
            sub = Text_AddSubtext(text, 0.0F, 40.0F + (float) (120 * i), (char *) option);
            if (mem[0] == i) {
                Text_SetColor(text, sub, &yellow);
            }
        }
    }

    *toggled_window = which;
    return toggled;
}

/* ============================================================================================
 * CPU percent and positions
 * ============================================================================================ */
void lg_DPadCPUPercent(LgSaveSlot *cpu_slot)
{
    FighterData *p1 = Fighter_GetGObj(0)->userdata;
    int percent = mu_tmce_legacy_static_get(1, 0x60);
    int is_sub = mu_tmce_legacy_static_get(1, 0x0C);
    HSD_Pad *pad = lg_GetInputStruct(p1->pad_index);
    int rapid;
    float f;
    FighterData *backup;

    /* Ensure L is pressed (either digital or lightshield) */
    if (!(pad->held & PAD_TRIGGER_L) && pad->triggerLeft < 24) {
        return;
    }
    rapid = pad->repeat;
    if (rapid & PAD_BUTTON_DPAD_RIGHT) {
        percent = percent < 999 ? percent + 1 : 999;
    } else if (rapid & PAD_BUTTON_DPAD_LEFT) {
        percent = percent > 0 ? percent - 1 : 0;
    } else if (rapid & PAD_BUTTON_DPAD_UP) {
        percent = percent < 989 ? percent + 10 : 999;
    } else if (rapid & PAD_BUTTON_DPAD_DOWN) {
        percent = percent > 9 ? percent - 10 : 0;
    } else {
        return;
    }

    /* Store to Active Static Playerblock and the active fighter */
    mu_tmce_legacy_static_set(1, 0x60, percent);
    f = (float) percent;
    ((FighterData *) Fighter_GetGObj(1)->userdata)->dmg.percent = f;
    /* Backed Up PlayerData (the main or follower backup, by the static block's transformed[0]) */
    backup = is_sub ? cpu_slot->follower : cpu_slot->main;
    if (backup != 0) {
        backup->dmg.percent = f;
    }
    /* Backed Up Static Playerblock (only the main fighter's backup has one) */
    if (cpu_slot->main != 0) {
        mu_tmce_legacy_backup_static_set(cpu_slot->main, 0x60, percent);
        mu_tmce_legacy_backup_static_set(cpu_slot->main, 0x62, percent);
    }
}

static void enter_wait_at(GOBJ *gobj, float x, float y)
{
    FighterData *fp = gobj->userdata;
    Fighter_EnterWait(gobj);
    fp->phys.pos.X = x;
    fp->phys.pos.Y = y;
    lg_UpdatePosition(gobj);
    lg_UpdateCameraBox(gobj);
}

void lg_InitializePositions(const float *xy, GOBJ *p1_gobj, GOBJ *p2_gobj)
{
    GOBJ *follower;

    /* Move P1 */
    enter_wait_at(p1_gobj, xy[0], xy[2]);
    follower = lg_CheckIfPlayerHasAFollower(p1_gobj, 0);
    if (follower != 0) {
        enter_wait_at(follower, xy[0], xy[2]);
    }
    /* Move P2 */
    enter_wait_at(p2_gobj, xy[1], xy[3]);
    follower = lg_CheckIfPlayerHasAFollower(p2_gobj, 0);
    if (follower != 0) {
        enter_wait_at(follower, xy[1], xy[3]);
    }
    lg_ClearNanaInputs(p1_gobj, p2_gobj);
    lg_CurrentInputsAsLastFramesInputs(p1_gobj->userdata);
}

void lg_Randomize_LeftorRightSide(int opposing, LgSaveState *ss)
{
    FighterData *p1 = ss->player[0].main;
    FighterData *p2 = ss->player[1].main;
    GOBJ *gobj;

    if (HSD_Randi(2) == 0) {
        /* Randomize_LeftSide */
        p1->phys.pos.X = lg_Randomize_AlwaysNegative(p1->phys.pos.X);
        p2->phys.pos.X = opposing ? lg_Randomize_AlwaysPositive(p2->phys.pos.X) : lg_Randomize_AlwaysNegative(p2->phys.pos.X);
        p1->facing_direction = 1.0F;
        p2->facing_direction = -1.0F;
    } else {
        /* Randomize_RightSide */
        p1->phys.pos.X = lg_Randomize_AlwaysPositive(p1->phys.pos.X);
        p2->phys.pos.X = opposing ? lg_Randomize_AlwaysNegative(p2->phys.pos.X) : lg_Randomize_AlwaysPositive(p2->phys.pos.X);
        p1->facing_direction = -1.0F;
        p2->facing_direction = 1.0F;
    }

    /* CheckForFollowers: a follower's backup takes its main fighter's X and facing */
    for (gobj = (*stc_gobj_lookup)[MATCHPLINK_FIGHTER]; gobj != 0; gobj = gobj->next) {
        FighterData *fp = gobj->userdata;
        LgSaveSlot *slot;
        if (lg_FtFlag(fp, 0x221F, 0x08)) {
            continue; /* not a main fighter */
        }
        if (lg_CheckIfPlayerHasAFollower(gobj, 0) == 0) {
            continue;
        }
        slot = &ss->player[fp->ply];
        slot->follower->phys.pos.X = slot->main->phys.pos.X;
        slot->follower->facing_direction = slot->main->facing_direction;
    }
}

void lg_MoveCPU(GOBJ *p1_gobj, GOBJ *p2_gobj, LgSaveState *ss)
{
    FighterData *p1 = p1_gobj->userdata;
    FighterData *p2 = p2_gobj->userdata;
    HSD_Pad *pad = lg_GetInputStruct(p1->pad_index);
    float x;
    float gx, gy;
    int line;
    GOBJ *follower;
    FighterData *follower_data;

    /* Ensure HMN In Wait, DPad Down with nothing else held (but Z) */
    if (p1->state_id != ASID_WAIT) {
        return;
    }
    if (!(pad->repeat & PAD_BUTTON_DPAD_DOWN)) {
        return;
    }
    if ((pad->held & ~(PAD_BUTTON_DPAD_DOWN | PAD_TRIGGER_Z)) != 0) {
        return;
    }
    /* Make Sure Player is Grounded */
    if (p1->phys.air_state != 0) {
        lg_PlaySFX(0xAF);
        return;
    }
    /* 10 in front of P1, on the ground */
    x = 10.0F * p1->facing_direction;
    x = x + p1->phys.pos.X;
    if (!lg_FindGroundNearPlayer(0, x, p1->phys.pos.Y, &gx, &gy, &line)) {
        lg_PlaySFX(0xAF);
        return;
    }
    p2->phys.pos.X = gx;
    p2->phys.pos.Y = gy;
    lg_FtSetU32(p2, 0x83C, line);
    p2->facing_direction = -p1->facing_direction;
    Fighter_EnterWait(p2_gobj);
    lg_UpdatePosition(p2_gobj);
    EnvironmentCollision_WaitLanding(p2_gobj);
    Fighter_SetGrounded(p2);

    follower = lg_CheckIfPlayerHasAFollower(p2_gobj, &follower_data);
    if (follower != 0) {
        /* Init Player Data Values (So CPU Init is called and nana knows where popo is) */
        Fighter_InitData(follower);
        follower_data->phys.pos.X = p2->phys.pos.X;
        follower_data->phys.pos.Y = p2->phys.pos.Y;
        lg_FtSetU32(follower_data, 0x83C, lg_FtU32(p2, 0x83C));
        follower_data->facing_direction = p2->facing_direction;
        Fighter_EnterWait(follower);
        lg_UpdatePosition(follower);
        EnvironmentCollision_WaitLanding(follower);
        Fighter_SetGrounded(follower_data);
    }
    lg_SaveState_Save(ss, 1);
    lg_PlaySFX(0xDD);
}

int lg_AdjustResetDistance(LgSaveState *ss, FighterData *p1, FighterData *p2)
{
    FighterData *b1 = ss->player[0].main;
    FighterData *b2 = ss->player[1].main;
    unsigned int held = lg_FtU32(p1, 0x660) & 0xFFFF; /* lhz 0x662: the low half of the last frame's buttons */
    unsigned int down;
    float dir;
    float p1x, p2x;

    /* Make Sure Nothing is Held (Z is let through) */
    if (!(held & PAD_TRIGGER_Z) && held != 0) {
        return -1;
    }
    down = lg_FtU32(p1, 0x668);
    if (down & PAD_BUTTON_DPAD_RIGHT) {
        /* Move Apart */
        dir = (float) lg_GetDirectionInRelationToP1(p1, p2);
        dir = -dir;
        p1x = 1.0F * dir;
        p1x = p1x + b1->phys.pos.X;
        dir = -dir;
        p2x = 1.0F * dir;
        p2x = p2x + b2->phys.pos.X;
        b1->phys.pos.X = p1x;
        b2->phys.pos.X = p2x;
        return 1;
    }
    if (down & PAD_BUTTON_DPAD_LEFT) {
        /* Move Together, not closer than 10 */
        dir = (float) lg_GetDirectionInRelationToP1(p1, p2);
        p1x = 1.0F * dir;
        p1x = p1x + b1->phys.pos.X;
        dir = -dir;
        p2x = 1.0F * dir;
        p2x = p2x + b2->phys.pos.X;
        if (__builtin_fabsf(p1x - p2x) <= 10.0F) {
            return -1;
        }
        b1->phys.pos.X = p1x;
        b2->phys.pos.X = p2x;
        return 1;
    }
    return -1;
}

/* ============================================================================================
 * Score (the HUD KO counter as the event's score)
 * ============================================================================================ */
void lg_Event_ExitFunction(int outcome)
{
    int event = stc_memcard->EventBackup.event;
    int best;

    /* Ensure No Contest/Retry */
    if (mu_tmce_legacy_event_flag(0x40)) {
        return;
    }
    best = Events_GetSavedScore(event);
    if (mu_lg_gmMainLib_8015CEFC(event) != 0 && lg_score.best <= best) {
        return;
    }
    /* Store As New High Score, Set Event As Played */
    Events_StoreEventScore(event, lg_score.best);
    Events_SetEventAsPlayed(event);
}

void lg_InitializeHighScore(void)
{
    KOCount_Init(0);
    lg_score.current = 0;
    lg_score.best = 0;
    mu_tmce_legacy_set_match_end(lg_Event_ExitFunction);
}

/* ============================================================================================
 * PerformAerialThink
 * ============================================================================================ */

/* PerformAerial_FrameData: per internal character 6 pairs (first, last frame to attack on): fair, nair,
 * dair, uair, bair, and none. 0,0 = the move is left out for that character. */
static const u8 perform_aerial_frame_data[27][12] = {
#define W(x) (u8) ((x) >> 24), (u8) ((x) >> 16), (u8) ((x) >> 8), (u8) (x)
    { W(0x00040012), W(0x00100005), W(0x0005FFFF) }, /* Mario */
    { W(0x0009000A), W(0x00080005), W(0x0005FFFF) }, /* Fox */
    { W(0x0006000E), W(0x00040005), W(0x0005FFFF) }, /* Captain Falcon */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* DK */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Kirby */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Bowser */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Link */
    { W(0x10140018), W(0x00000005), W(0x0005FFFF) }, /* Sheik */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Ness */
    { W(0x000D001A), W(0x000E0005), W(0x0005FFFF) }, /* Peach */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Popo */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Nana */
    { W(0x000C000F), W(0x00080005), W(0x0005FFFF) }, /* Pikachu */
    { W(0x00200020), W(0x080D0005), W(0x0005FFFF) }, /* Samus */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Yoshi */
    { W(0x00100010), W(0x00100005), W(0x0005FFFF) }, /* Jigglypuff */
    { W(0x00050019), W(0x00050005), W(0x0005FFFF) }, /* Mewtwo */
    { W(0x0017001B), W(0x00110005), W(0x0005FFFF) }, /* Luigi */
    { W(0x00160013), W(0x00120005), W(0x0005FFFF) }, /* Marth */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Zelda */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Young Link */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Doc */
    { W(0x070B000D), W(0x000C0005), W(0x0005FFFF) }, /* Falco */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Pichu */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Game and Watch */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Ganondorf */
    { W(0x00050005), W(0x00050005), W(0x0005FFFF) }, /* Roy */
#undef W
};

void lg_PerformAerialThink(GOBJ *cpu, u8 *vars, int attack)
{
    FighterData *fp = cpu->userdata;
    int frame = (int) fp->state.frame;
    int state;

    /* Check To Aerial */
    if (vars[0] != 0) {
        return;
    }
    state = fp->state_id;
    if (state == ASID_WAIT) {
        /* PerformAerialThink_DuringWait: input jump, pick the attack and its frame */
        const u8 *moves;
        int move;
        fp->cpu.held = PAD_BUTTON_X;
        if (fp->kind < 0 || fp->kind >= 27) {
            return;
        }
        moves = perform_aerial_frame_data[fp->kind];
        move = attack == 0 ? HSD_Randi(3) : attack - 1;
        vars[1] = move;
        /* Move Disabled For Char, Get a New One */
        while (moves[move * 2] == 0 && moves[move * 2 + 1] == 0) {
            move = HSD_Randi(3);
            vars[1] = move;
        }
        vars[2] = HSD_Randi(moves[move * 2 + 1] - moves[move * 2]) + moves[move * 2];
        return;
    }
    if (state == ASID_JUMPF || state == ASID_JUMPB) {
        /* PerformAerialThink_DuringJump: attack on the chosen frame */
        if (vars[2] == frame) {
            if (vars[1] == 1) {
                fp->cpu.held = PAD_BUTTON_A; /* Nair */
            } else if (vars[1] == 2) {
                fp->cpu.cstickY = -127; /* Dair */
            } else {
                fp->cpu.cstickX = 127 * (int) fp->facing_direction; /* Fair */
            }
        }
    } else if (state >= ASID_ATTACKAIRN && state <= ASID_ATTACKAIRLW) {
        /* PerformAerialThink_DuringAttack */
    } else if (state == ASID_LANDING || (state >= ASID_LANDINGAIRN && state <= ASID_LANDINGAIRLW)) {
        /* PerformAerialThink_DuringLanding: Set Sequence as Over */
        vars[0] = 1;
        return;
    } else {
        return;
    }

    /* InputFastfallAndLCancel: fastfall once falling (unless inputting a nair), then spoof mash L */
    if (!lg_FtFlag(fp, 0x221A, 0x08) && fp->phys.self_vel.Y < 0.0F && fp->cpu.held != PAD_BUTTON_A) {
        fp->cpu.lstickY = -127;
    }
    lg_FtSetU8(fp, 0x67F, 1);
}

float lg_RandFloat(int lower, int upper)
{
    float f = (float) (HSD_Randi(upper - lower) + lower);
    return HSD_Randf() + f;
}

void lg_Custom_InterruptRebirthWait(GOBJ *gobj)
{
    FighterData *fp = gobj->userdata;

    /* Check For Aerial Jump */
    if (Fighter_IASACheck_JumpAerial(gobj) != 0) {
        return;
    }
    /* A fresh sideways push */
    if (lg_FtU8(fp, 0x670) < 2 && __builtin_fabsf(lg_FtF32(fp, 0x620)) >= (*stc_ftcommon)->x24) {
        Fighter_EnterFall(gobj);
        return;
    }
    /* A fresh push down */
    if (lg_FtU8(fp, 0x671) < 4 && lg_FtF32(fp, 0x624) < -(*stc_ftcommon)->lstick_rebirthfall) {
        Fighter_EnterFall(gobj);
    }
}

/* ============================================================================================
 * Placing fighters
 * ============================================================================================ */
static int raycast_ground(float from_x, float from_y, float to_x, float to_y, float *out_x, float *out_y, int *out_line)
{
    Vec3 pos;
    int line;
    int kind;
    Vec3 unk;
    if (GrColl_RaycastGround(&pos, &line, &kind, &unk, -1, -1, -1, 0, from_x, from_y, to_x, to_y, 0.0F) == 0) {
        return 0;
    }
    *out_x = pos.X;
    *out_y = pos.Y;
    *out_line = line;
    return 1;
}

int lg_FindGroundNearPlayer(GOBJ *gobj, float x, float y, float *out_x, float *out_y, int *out_line)
{
    if (gobj == 0) {
        /* CoordinatesPassedIn: from 10 above to 10 below the point */
        return raycast_ground(x, y + 10.0F, x, y - 10.0F, out_x, out_y, out_line);
    }
    /* GObjPassedIn: from 10 above the fighter to its X - 1000 (the ASM loads X again for the
     * bottom's Y; kept) */
    {
        FighterData *fp = gobj->userdata;
        float px = fp->phys.pos.X;
        return raycast_ground(px, fp->phys.pos.Y + 10.0F, px, px - 1000.0F, out_x, out_y, out_line);
    }
}

int lg_FindGroundUnderCoordinate(float x, float y, float *out_x, float *out_y, int *out_line)
{
    return raycast_ground(x, y, x, y - 1000.0F, out_x, out_y, out_line);
}

void lg_PlacePlayerOnGround(GOBJ *gobj)
{
    FighterData *fp = gobj->userdata;
    float x, y;
    int line;
    if (lg_FindGroundNearPlayer(gobj, 0.0F, 0.0F, &x, &y, &line)) {
        fp->phys.pos.X = x;
        fp->phys.pos.Y = y;
        lg_FtSetU32(fp, 0x83C, line);
    }
    lg_UpdatePosition(gobj);
    EnvironmentCollision_WaitLanding(gobj);
    Fighter_SetGrounded(fp);
}

void lg_PlacePlayersCenterStage_DoStuff(GOBJ *gobj, int line)
{
    FighterData *fp = gobj->userdata;
    const s8 *constants = &place_center_constants[fp->ply * 2];
    float cx, cy;

    /* Initialize Player Data (Mainly for ICs so Nana knows where Popo is) */
    Fighter_InitData(gobj);
    lg_GetGroundCenter(line, &cx, &cy);
    fp->facing_direction = (float) constants[0];
    fp->phys.pos.X = (float) constants[1] + cx;
    fp->phys.pos.Y = cy;
    Fighter_EnterWait(gobj);
    lg_PlacePlayerOnGround(gobj);
    lg_UpdateCameraBox(gobj);
}

void lg_PlacePlayersCenterStage(int line)
{
    int slot;
    for (slot = 0; slot < 6; slot++) {
        GOBJ *gobj = Fighter_GetGObj(slot);
        GOBJ *follower;
        if (gobj == 0) {
            continue;
        }
        follower = lg_CheckIfPlayerHasAFollower(gobj, 0);
        lg_PlacePlayersCenterStage_DoStuff(gobj, line);
        if (follower != 0) {
            lg_PlacePlayersCenterStage_DoStuff(follower, line);
        }
    }
}

/* ============================================================================================
 * InitializeMatch
 * ============================================================================================ */
void lg_InitializeMatch(LgLevelEntry *ev, MatchInit *match, int cpu, int stage, int osds, int use_sopo)
{
    Preload *preload = Preload_GetTable();
    LgPlayerInit *p2;

    /* Check to override OSD Toggles */
    if (stc_memcard->TM_OSDRecommended != 1) {
        stc_memcard->TM_OSDEnabled |= osds;
    }

    /* SPAWN 2 PLAYERS, P2 = a copy of P2Struct */
    ev->flags = 0x40;
    p2 = HSD_MemAlloc(32);
    memcpy(p2, lg_P2Struct, 0x1C);
    MEX_DP_SET(ev->player_init[1], p2);

    /* Store CPU */
    if (cpu != -1) {
        p2->c_kind = cpu;
    } else {
        /* the CPU chosen on the CSS (Zelda plays as Sheik) */
        int kind = preload->queued.fighters[1].kind;
        if (kind == CKIND_ZELDA) {
            kind = CKIND_SHEIK;
        }
        p2->c_kind = kind;
        p2->color = preload->queued.fighters[1].costume;
        p2->slot_type = 1;
    }

    /* Store Stage */
    if (stage != -1) {
        match->stage = stage;
    } else {
        match->stage = preload->queued.stage;
    }

    /* Swap P1 Ice Climbers to Popo alone */
    if (use_sopo != 0 && stc_memcard->EventBackup.c_kind == CKIND_ICECLIMBERS) {
        stc_memcard->EventBackup.c_kind = CKIND_POPO;
    }
}

/* ============================================================================================
 * Geometry and knockback
 * ============================================================================================ */
float lg_GetDistance(const Vec3 *a, const Vec3 *b)
{
    float dx = b->X - a->X;
    float dy = a->Y - b->Y;
    float dx2 = dx * dx;
    float dy2 = dy * dy;
    float d2 = dx2 + dy2;
    /* frsqrte f1, f2; fmuls f1, f1, f2 */
    return (float) (mu_frsqrte((double) d2) * (double) d2);
}

void lg_GetLedgeCoordinates(int side, Vec3 *out)
{
    int ids = *stage_table_entry(lg_LedgeCliffIDs);
    if (side == 0) {
        Stage_GetLeftOfLineCoordinates((ids >> 8) & 0xFF, out);
    } else {
        Stage_GetRightOfLineCoordinates(ids & 0xFF, out);
    }
}

void lg_EnterKnockback(GOBJ *gobj, int angle_lo, int angle_hi, int mag_lo, int mag_hi)
{
    FighterData *fp = gobj->userdata;
    float angle;
    float magnitude;
    float kb;
    float x, y;
    int hitstun;

    /* Random angle between, in radians */
    angle = (float) (HSD_Randi(angle_hi - angle_lo) + angle_lo);
    angle = angle * 0.017453292F;
    /* Random magnitude between X and Y */
    magnitude = lg_RandFloat(mag_lo, mag_hi);
    kb = magnitude * (*stc_ftcommon)->force_applied_to_kb_mag_multiplier;
    /* X and Y components, X away from the facing direction */
    x = cos(angle) * kb;
    x = x * -fp->facing_direction;
    fp->phys.kb_vel.X = x;
    y = sin(angle) * kb;
    fp->phys.kb_vel.Y = y;
    /* Calculate Hitstun: 0.4 * magnitude, rounded down, into state variable 1 */
    hitstun = (int) (magnitude * (*stc_ftcommon)->x154);
    lg_FtSetF32(fp, 0x2340, (float) hitstun);
    lg_FtSetFlag(fp, 0x221C, 0x02, 1);
    /* Enable ECB Update */
    Fighter_EnableCollUpdate(fp);
}

/* ============================================================================================
 * DisableHazards
 * ============================================================================================ */
void lg_DisableHazards_RagdollFix_Think(GOBJ *gobj) { Dynamics_DecayWind(); }

void lg_DisableHazards_RagdollFix(void)
{
    GOBJ *gobj = GObj_Create(3, 5, 0);
    GObj_AddProc(gobj, lg_DisableHazards_RagdollFix_Think, 4);
}

void lg_DisableHazards(void)
{
    switch (Stage_GetExternalID()) {
    case 8: /* DisableHazards_Story: remove shyguy's map_gobj proc */
        GObj_RemoveProc(Stage_GetMapGObj(3));
        lg_DisableHazards_RagdollFix();
        break;
    case 3: /* DisableHazards_Pstadium: remove the transformation's map_gobj proc */
        GObj_RemoveProc(Stage_GetMapGObj(2));
        lg_DisableHazards_RagdollFix();
        break;
    case 0x1C: /* DisableHazards_OldDL: destroy whispy, remove its blink proc, no wind */
        Stage_DestroyMapGObj(Stage_GetMapGObj(7));
        GObj_RemoveProc(Stage_GetMapGObj(6));
        *ftchkdevice_windnum = 0;
        break;
    case 0x1D: /* DisableHazards_OldYS: destroy the clouds */
        Stage_DestroyMapGObj(Stage_GetMapGObj(2));
        break;
    case 0x1E: /* DisableHazards_OldKongo: destroy the barrel */
        Stage_DestroyMapGObj(Stage_GetMapGObj(1));
        break;
    default: /* DisableHazards_Dummy .. _FinalDestination: nothing */
        break;
    }
}

/* ============================================================================================
 * Input playback, ledges, grabs
 * ============================================================================================ */
void lg_PlaybackInputSequence(FighterData *cpu, const LgInputSeq *seq, int frame)
{
    for (;; seq++) {
        int seq_frame = (u8) seq->frame;
        if (seq->frame == -1) {
            return;
        }
        if (frame == seq_frame) {
            cpu->cpu.held = seq->buttons;
            cpu->cpu.lstickX = seq->lstick_x;
            cpu->cpu.lstickY = seq->lstick_y;
            cpu->cpu.cstickX = seq->cstick_x;
            cpu->cpu.cstickY = seq->cstick_y;
            return;
        }
        if (frame < seq_frame) {
            return;
        }
    }
}

void lg_PlaceOnLedge(GOBJ *gobj, int side)
{
    FighterData *fp = gobj->userdata;
    int ids = *stage_table_entry(lg_LedgeCliffIDs);
    int ledge;

    if (side == 0) {
        ledge = (ids >> 8) & 0xFF;
        fp->facing_direction = 1.0F;
    } else {
        ledge = ids & 0xFF;
        fp->facing_direction = -1.0F;
    }
    /* Store Ledge to Player Block, Enter CliffWait */
    lg_FtSetU32(fp, 0x2340, ledge);
    Fighter_EnterCliffWait(gobj);
    /* Init state variable (would be 1 at the start of the next frame if it ocurred naturally) */
    lg_FtSetU32(fp, 0x2348, 1);
    /* Spoof in state for 1 frame */
    fp->TM.state_frame = 1;
    /* Get Jump Back, ECB update, ECB corners */
    Fighter_SetAirborne(fp);
    Fighter_EnableCollUpdate(fp);
    Coll_CheckLedge((CollData *) &fp->coll_data);
    /* Move Player To Ledge */
    Fighter_MoveToCliff(gobj);
    lg_UpdatePosition(gobj);
    /* Kill Velocity */
    fp->phys.self_vel.X = 0.0F;
    fp->phys.self_vel.Y = 0.0F;
    /* Give Intangibility */
    Fighter_ApplyIntang(gobj, (*stc_ftcommon)->cliff_invuln_time);
}

void lg_Event_EnterGrab(const float *xy, GOBJ *p1_gobj, GOBJ *p2_gobj)
{
    FighterData *p1 = p1_gobj->userdata;
    FighterData *p2 = p2_gobj->userdata;
    GOBJ *follower;
    FighterData *follower_data;

    /* Move P1 */
    p1->phys.pos.X = xy[0];
    p1->phys.pos.Y = xy[1];
    lg_UpdatePosition(p1_gobj);
    follower = lg_CheckIfPlayerHasAFollower(p1_gobj, &follower_data);
    if (follower != 0) {
        follower_data->phys.pos.X = xy[0];
        follower_data->phys.pos.Y = xy[1];
        lg_UpdatePosition(follower);
    }
    /* Move P2 */
    p2->phys.pos.X = xy[2];
    p2->phys.pos.Y = xy[3];
    lg_UpdatePosition(p2_gobj);
    follower = lg_CheckIfPlayerHasAFollower(p2_gobj, &follower_data);
    if (follower != 0) {
        follower_data->phys.pos.X = xy[2];
        follower_data->phys.pos.Y = xy[3];
        lg_UpdatePosition(follower);
    }
    /* Store P1 into P2 Grab Pointer; P2 grabs, P1 grabbed, P2 into CatchWait, grounded */
    lg_FtSetPtr(p2, 0x1A58, p1_gobj);
    mu_lg_fn_800D9CE8(p2_gobj);
    mu_lg_fn_800DAADC(p1_gobj, p2_gobj);
    mu_lg_fn_800DA1D8(p2_gobj);
    Fighter_SetGrounded2(p2);
    /* Remove P2's GFX Pointer That Is Crashing the Game */
    lg_FtSetPtr(p2, 0x60C, 0);
}
