/* Training Mode CE, native build: Grab Mash Out, an original UnclePunch event.
 *
 * Rewritten in C from ASM/training-mode/Custom Events/Custom Event Code - Rewrite.asm
 * (GrabMashOut, GrabMashOutLoad, GrabMashOutThink, GrabMashOut_InitializePositions and their data).
 * Marth grabs the player after a random delay on Final Destination; the player practices mashing out.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_c_common.h"

typedef struct GrabMashOutVars {
    u8 event_state;      /* 0x0 EventState */
    u8 timer;            /* 0x1 Timer */
    u16 throw_timer;     /* 0x2 ThrowTimer */
    float grab_breakout; /* 0x4 GrabBreakout */
} GrabMashOutVars;

enum {
    STATE_THROW_DELAY,
    STATE_MASH_OUT_THINK,
    STATE_RESET,
};

/* constants */
#define RESET_TIMER 80
#define PERCENT_LO 0
#define PERCENT_HI 80
#define THROW_TIMER_LO 40
#define THROW_TIMER_HI 100

/* GrabMashOutThink_Constants: P1 X, P1 Y, P2 X, P2 Y */
static const float GrabMashOut_Constants[4] = {-8.0f, 0.0f, 8.0f, 0.0f};
/* (GrabMashOut_TopText "Escaped Grab" and GrabMashOut_BottomText "Frame %d/%d" are not used) */

/* GrabMashOut_InitializePositions: the players facing each other on a random side, both in Wait,
 * P1 controlled by port 3, a random percent and throw delay */
static void GrabMashOut_InitializePositions(LgPlayers *pl, GrabMashOutVars *v)
{
    FighterData *p1 = pl->p1;
    FighterData *p2 = pl->p2;
    int facing = 1; /* 1 = P1 facing right, -1 = P1 facing left */

    if (HSD_Randi(2) != 0)
        facing = -1;

    p1->facing_direction = (float) facing;
    p2->facing_direction = (float) (facing * -1);

    /* the left position (P1's when it faces right) */
    if (facing < 0) {
        p2->phys.pos.X = GrabMashOut_Constants[0];
        p2->phys.pos.Y = GrabMashOut_Constants[1];
    } else {
        p1->phys.pos.X = GrabMashOut_Constants[0];
        p1->phys.pos.Y = GrabMashOut_Constants[1];
    }
    /* the right position */
    if (facing < 0) {
        p1->phys.pos.X = GrabMashOut_Constants[2];
        p1->phys.pos.Y = GrabMashOut_Constants[3];
    } else {
        p2->phys.pos.X = GrabMashOut_Constants[2];
        p2->phys.pos.Y = GrabMashOut_Constants[3];
    }

    lg_PlacePlayerOnGround(pl->p1_gobj);
    lg_UpdateCameraBox(pl->p1_gobj);
    lg_PlacePlayerOnGround(pl->p2_gobj);
    lg_UpdateCameraBox(pl->p2_gobj);

    Fighter_EnterWait(pl->p1_gobj);
    /* P1's pad index: port 3 controls it while grabbed */
    lg_FtSetU8(p1, 0x618, 2);
    Fighter_EnterWait(pl->p2_gobj);

    /* a random percent */
    Fighter_SetHUDDamage(p1->ply, HSD_Randi(PERCENT_HI - PERCENT_LO) + PERCENT_LO);

    /* the variables */
    v->event_state = STATE_THROW_DELAY;
    v->timer = 0;
    v->throw_timer = HSD_Randi(THROW_TIMER_HI - THROW_TIMER_LO) + THROW_TIMER_LO;
}

static void GrabMashOutThink(GOBJ *gobj)
{
    LgEventData *ed = lg_EventData(gobj);
    GrabMashOutVars *v = (GrabMashOutVars *) ed->raw;
    LgPlayers pl;
    FighterData *p1, *p2;
    int state;

    lg_GetAllPlayerPointers(&pl);
    p1 = pl.p1;
    p2 = pl.p2;
    lg_StoreCPUTypeAndZeroInputs(p2);

    /* on the first frame */
    if (lg_CheckIfFirstFrame()) {
        GrabMashOut_InitializePositions(&pl, v);
        lg_RemoveFirstFrameInputs();
        lg_SaveState_Save(&ed->savestate, 1);
    }

    /* reset if anyone died */
    if (lg_IsAnyoneDead())
        goto restore;

    switch (v->event_state) {
    case STATE_THROW_DELAY:
        /* grab when the delay is up */
        if (--v->throw_timer == 0)
            p2->cpu.held = PAD_BUTTON_A | PAD_TRIGGER_R;
        state = p1->state_id;
        if (state == ASID_CAPTUREWAITLW || state == ASID_CAPTUREWAITHI) {
            /* grabbed: the breakout timer, and mash */
            v->grab_breakout = lg_FtF32(p1, 0x2354);
            v->event_state = STATE_MASH_OUT_THINK;
        } else if (state == ASID_CAPTUREPULLEDLW || state == ASID_CAPTUREPULLEDHI) {
            /* just grabbed: P1 back on its own port */
            lg_FtSetU8(p1, 0x618, 0);
        }
        break;

    case STATE_MASH_OUT_THINK:
        /* broke out: CaptureJump or CaptureCut */
        state = p1->state_id;
        if (state == ASID_CAPTUREJUMP || state == ASID_CAPTURECUT) {
            v->timer = RESET_TIMER;
            v->event_state = STATE_RESET;
        }
        break;

    case STATE_RESET:
    default:
        break;
    }

    /* the timer */
    if (v->timer <= 0)
        return;
    v->timer--;
    if (v->timer > 0)
        return;

restore:
    lg_SaveState_Load(&ed->savestate);
    GrabMashOut_InitializePositions(&pl, v);
}

static void GrabMashOutLoad(void)
{
    lg_StartOSDs();
    lg_CreateEventThinkFunction(GrabMashOutThink, 3, 0, 0);
}

void mu_tmce_legacy_GrabMashOut(MatchInit *match, int event_id)
{
    (void) event_id;
    /* Marth on Final Destination, no OSDs, Sopo */
    lg_InitializeMatch(lg_EventStruct(), match, LGC_MARTH_EXT, LGC_STAGE_FINAL_DESTINATION, 0, 1);
    match->onStartMelee = (void *) GrabMashOutLoad;
}
