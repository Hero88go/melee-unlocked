/* Training Mode CE, native build: Ledgetech Counter, an original UnclePunch event.
 *
 * Rewritten in C from ASM/training-mode/Custom Events/Custom Event Code - Rewrite.asm
 * (LedgetechCounter, LedgetechCounterLoad, LedgetechCounterThink and its Record/Display routines,
 * LedgetechCounter_DisplayTimingMessage, LedgetechCounter_DetermineTechInputTimingMessageCategory,
 * LedgetechCounterThink_SetupStageSideAndPositions and their data). The player recovers to a ledge
 * where Marth counters; the player practices teching the counter against the wall, and a message
 * tells the timing of the L/R press against the counter's hitlag.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_c_common.h"

typedef struct LedgetechCounterVars {
    u8 event_state;                   /* 0x00 EventState */
    u8 marth_state;                   /* 0x01 MarthState */
    u8 timer;                         /* 0x02 Timer */
    u8 x3[0x40 - 0x3];
    int marth_speciallwhit_frame;     /* 0x40 MarthSpecialLwHitStartingFrame */
    int tech_input_frame;             /* 0x44 TechInputFrame */
    s8 tech_input_relative;           /* 0x48 RelativeTechInputFrameFromStartingSpecialLwHit */
    u8 tech_message_displayed;        /* 0x49 TechInputMessageDisplayed */
} LedgetechCounterVars;
_Static_assert(__builtin_offsetof(LedgetechCounterVars, tech_message_displayed) < sizeof(((LgEventData *) 0)->raw),
               "LedgetechCounter's variables fit the event data");

enum {
    STATE_ON_REBIRTH_PLAT,
    STATE_RECOVERING,
    STATE_TECH_SUCCEEDED,
};
enum {
    MARTH_WAIT,
    MARTH_ATTACKED,
};

/* LedgetechCounter_Constants */
#define COUNTER_DISTANCE 40.0f  /* Mm away from the player to counter */
#define MARTH_FORWARD 5.0f      /* Mm to move Marth forward after placing him at the ledge */
#define PLAYER_DOWN -15.0f      /* Mm to move the player down after placing him in the air */

/* LedgetechCounterWindowInfo: 1 window, Stage Side has 3 options */
static const u8 LedgetechCounterWindowInfo[4] = {0x00, 0x02, 0x00, 0x00};
static const char LedgetechCounterWindowText[] = "Stage Side\0"
                                                 "Random\0"
                                                 "Left\0"
                                                 "Right\0";

#define OPT_STAGE_SIDE 0

/* the tech input timing messages */
static const char TooEarly_Text[] = "Too Early LR\n%dF Before Hitlag";
static const char Buffered_Text[] = "Buffered LR\n%dF Before Hitlag";
static const char ReactionHitlag_Text[] = "LR on Reaction\nHitlag: %d/11";
static const char AttackHitlag_Text[] = "LR on Attack\nHitlag: %d/5";
static const char TooLate_Text[] = "Too Late LR\n%dF After Hitlag Ends";

/* STOPGAP: TM_GameFrameCounter (r13 -0x49A8 on the console: zeroed at match start, incremented once a
 * match frame, 1 on the event's first think). legacy_common.h has no accessor for it (only
 * lg_CheckIfFirstFrame reads it), so this event keeps its own count of think frames with the same
 * values: 1 on the first frame, one more each frame after. Replace with the shared counter once
 * legacy_common.h exports it. */
static int ledgetech_counter_frame;
static int LedgetechCounter_GameFrameCounter(void)
{
    return ledgetech_counter_frame;
}

/* LedgetechCounterThink_SetupStageSideAndPositions */
static void LedgetechCounter_SetupStageSideAndPositions(LgPlayers *pl, LgMenuData *md)
{
    int side;
    float offset;

    /* the stage side option: 0 random, 1 left, 2 right (anything else random) */
    switch (LG_OPTION(md, OPT_STAGE_SIDE)) {
    case 1:
        side = 0;
        break;
    case 2:
        side = 1;
        break;
    default:
        side = HSD_Randi(2);
        break;
    }
    lgc_Ledgetech_InitializePositions(pl, side);

    /* Marth forward a bit */
    offset = pl->p2->facing_direction * MARTH_FORWARD;
    pl->p2->phys.pos.X = offset + pl->p2->phys.pos.X;
    /* the player down a bit */
    pl->p1->phys.pos.Y = PLAYER_DOWN + pl->p1->phys.pos.Y;
}

/* LedgetechCounter_DetermineTechInputTimingMessageCategory: for the tech input `relative` frames
 * after the first frame of the counter (0), its message, color and the number it shows.
 *   -6 or less: untechable, too early and locked out
 *   -5 to -1:   techable, buffered before the counter
 *   0 to 9:     untechable, reaction hitlag 1 to 10 of 11 (no buffer during hitlag)
 *   10:         techable, the last frame of the reaction hitlag
 *   11 to 12:   techable, buffered on the counter's frames 2-3
 *   13:         untechable, attack hitlag 1 of 5 (not SDIable)
 *   14 to 17:   techable, attack hitlag 2 to 5 of 5 (with SDI)
 *   18 or more: untechable, too late */
static const char *LedgetechCounter_DetermineCategory(int relative, int *color, int *value)
{
    if (relative <= -6) {
        *color = MSGCOLOR_RED;
        *value = -(relative - 13); /* frames before the attack hitlag */
        return TooEarly_Text;
    }
    if (relative <= -1) {
        *color = MSGCOLOR_GREEN;
        *value = -(relative - 13);
        return Buffered_Text;
    }
    if (relative <= 10) {
        /* only the last frame of the hitlag takes the input */
        *color = relative == 10 ? MSGCOLOR_GREEN : MSGCOLOR_RED;
        *value = relative + 1; /* 1..11 of the reaction hitlag */
        return ReactionHitlag_Text;
    }
    if (relative <= 12) {
        *color = MSGCOLOR_GREEN;
        *value = -(relative - 13);
        return Buffered_Text;
    }
    if (relative <= 17) {
        /* no SDI on the first frame of hitlag */
        *color = relative == 13 ? MSGCOLOR_RED : MSGCOLOR_GREEN;
        *value = relative - 12; /* 1..5 of the attack hitlag */
        return AttackHitlag_Text;
    }
    /* after the hitlag: 11 frames of reaction hitlag, 2 before the attack, 5 of attack hitlag */
    *color = MSGCOLOR_RED;
    *value = relative - 17;
    return TooLate_Text;
}

/* LedgetechCounter_DisplayTimingMessage */
static void LedgetechCounter_DisplayTimingMessage(LedgetechCounterVars *v)
{
    LgPlayers pl;
    int color, value;
    const char *text;

    lg_GetAllPlayerPointers(&pl);
    text = LedgetechCounter_DetermineCategory(v->tech_input_relative, &color, &value);
    event_vars->Message_Display(4, pl.p1->ply, color, (char *) text, value);
}

/* LedgetechCounterThink_RecordSpecialLwHitStartingFrame: the frame Marth starts countering */
static void LedgetechCounter_RecordSpecialLwHitStartingFrame(LedgetechCounterVars *v)
{
    LgPlayers pl;
    lg_GetAllPlayerPointers(&pl);
    if (v->marth_speciallwhit_frame != 0)
        return;
    /* SpecialLwHit */
    if (pl.p2->state_id != 0x172)
        return;
    v->marth_speciallwhit_frame = LedgetechCounter_GameFrameCounter();
}

/* LedgetechCounterThink_RecordTechInputFrame: the frame the player presses L or R */
static void LedgetechCounter_RecordTechInputFrame(LedgetechCounterVars *v)
{
    LgPlayers pl;
    /* The ASM tests only the first byte of the word here (lbz of 0x44: its high byte on the
     * console), so the frame is taken again on each later L/R press. */
    if ((u8) ((u32) v->tech_input_frame >> 24) != 0)
        return;
    lg_GetAllPlayerPointers(&pl);
    if ((lg_FtU32(pl.p1, 0x668) & (PAD_TRIGGER_L | PAD_TRIGGER_R)) == 0)
        return;
    v->tech_input_frame = LedgetechCounter_GameFrameCounter();
}

/* LedgetechCounterThink_DisplayTechInputTimingMessage */
static void LedgetechCounter_DisplayTechInputTimingMessage(LedgetechCounterVars *v)
{
    int relative;
    /* Without this check a tech input during the attack hitlag always showed
     * "LR on Attack Hitlag 5/5" (the ASM's note). */
    if (v->tech_message_displayed == 1)
        return;
    if (v->tech_input_frame == 0)
        return;
    if (v->marth_speciallwhit_frame == 0)
        return;
    /* skip if the two are too far apart */
    relative = v->tech_input_frame - v->marth_speciallwhit_frame;
    if (relative < -25 || relative > 28)
        return;
    v->tech_input_relative = relative;
    LedgetechCounter_DisplayTimingMessage(v);
    v->tech_message_displayed = 1;
}

static void LedgetechCounterThink(GOBJ *gobj)
{
    LgEventData *ed = lg_EventData(gobj);
    LgMenuData *md = ed->menu;
    LedgetechCounterVars *v = (LedgetechCounterVars *) ed->raw;
    LgPlayers pl;
    FighterData *p1, *p2;

    /* STOPGAP frame counter, see above */
    ledgetech_counter_frame = lg_CheckIfFirstFrame() ? 1 : ledgetech_counter_frame + 1;

    lg_GetAllPlayerPointers(&pl);
    p1 = pl.p1;
    p2 = pl.p2;
    lg_StoreCPUTypeAndZeroInputs(p2);

    /* on the first frame */
    if (lg_CheckIfFirstFrame()) {
        /* the stage side from the menu */
        LedgetechCounter_SetupStageSideAndPositions(&pl, md);
        lg_RemoveFirstFrameInputs();
        lg_SaveState_Save(&ed->savestate, 1);
        /* the score count */
        KOCount_Update(lg_score.current);
    }

    /* reset if anyone died */
    if (lg_IsAnyoneDead())
        goto restore;
    /* D-pad left restores the state */
    if (lg_FtU32(p1, 0x668) & PAD_BUTTON_DPAD_LEFT)
        goto restore;

    LedgetechCounter_RecordTechInputFrame(v);
    LedgetechCounter_RecordSpecialLwHitStartingFrame(v);
    LedgetechCounter_DisplayTechInputTimingMessage(v);

    switch (v->event_state) {
    case STATE_ON_REBIRTH_PLAT:
        if (p1->state_id == ASID_REBIRTHWAIT) {
            /* RebirthWait lasts */
            p1->state_var.state_var1 = 2;
            break;
        }
        v->event_state = STATE_RECOVERING;
        break;

    case STATE_RECOVERING:
        /* Marth acted already */
        if (v->marth_state != MARTH_WAIT) {
            v->event_state = STATE_TECH_SUCCEEDED;
            break;
        }
        /* the player in 356 (0x164) and in range: down B */
        if (p1->state_id != 356)
            break;
        if (lg_GetDistance(&p1->phys.pos, &p2->phys.pos) > COUNTER_DISTANCE)
            break;
        p2->cpu.held = PAD_BUTTON_B;
        p2->cpu.lstickY = -127;
        v->marth_state = MARTH_ATTACKED;
        v->timer = 70;
        break;

    case STATE_TECH_SUCCEEDED:
        /* teched: more time */
        if (p1->state_id == ASID_PASSIVEWALL || p1->state_id == ASID_PASSIVEWALLJUMP)
            v->timer = 120;
        break;

    default:
        break;
    }

    /* the timer */
    if (v->timer == 0)
        return;
    if (--v->timer != 0)
        return;

restore:
    lg_SaveState_Load(&ed->savestate);
    /* the stage side from the menu */
    LedgetechCounter_SetupStageSideAndPositions(&pl, md);
    v->event_state = STATE_ON_REBIRTH_PLAT;
    v->marth_state = MARTH_WAIT;
    v->timer = 0;
    v->marth_speciallwhit_frame = 0;
    v->tech_input_frame = 0;
    v->tech_input_relative = 0;
    v->tech_message_displayed = 0;
}

static void LedgetechCounterLoad(void)
{
    lg_StartOSDs();
    lg_CreateEventThinkFunction(LedgetechCounterThink, 3, LedgetechCounterWindowInfo, LedgetechCounterWindowText);
}

void mu_tmce_legacy_LedgetechCounter(MatchInit *match, int event_id)
{
    (void) event_id;
    /* Marth on the chosen stage, no OSDs, no Sopo */
    lg_InitializeMatch(lg_EventStruct(), match, LGC_MARTH_EXT, -1, 0, 0);
    match->onStartMelee = (void *) LedgetechCounterLoad;
}
