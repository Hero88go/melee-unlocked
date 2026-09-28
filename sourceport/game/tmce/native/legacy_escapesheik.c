/* Training Mode CE, native build: Escape Sheik, an original UnclePunch event.
 *
 * Rewritten in C from ASM/training-mode/Custom Events/Custom Event Code - Rewrite.asm (EscapeSheik,
 * EscapeSheikLoad, EscapeSheikThink, EscapeSheik_InitializePositions and their data). Sheik down-throws
 * the player on Final Destination and chases the tech or missed tech (regrab or jab); the player
 * practices escaping, and a message tells how early or late a down B (shine) input was.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_c_common.h"

typedef struct EscapeSheikVars {
    u8 event_state;         /* 0x0 EventState */
    u8 timer;               /* 0x1 Timer */
    u16 throw_timer;        /* 0x2 ThrowTimer */
    u8 jab_frame;           /* 0x4 JabFrame */
    u8 is_jab_twice;        /* 0x5 isJabTwice */
    u8 jab2_frame;          /* 0x6 Jab2Frame */
    u8 jab2_timer;          /* 0x7 Jab2Timer */
    u8 is_displayed_timing; /* 0x8 isDisplayedTiming */
} EscapeSheikVars;

enum {
    STATE_THROW_DELAY,
    STATE_THROW_ENDLAG,
    STATE_CHASE,
    STATE_JAB2,
    STATE_RESET,
};

/* constants */
#define RESET_TIMER 40
#define PERCENT_LO 0
#define PERCENT_HI 40
#define THROW_TIMER_LO 30
#define THROW_TIMER_HI 60
#define REACT_FRAME 12
#define JAB_FRAME_LO 15
#define JAB_FRAME_HI 20
#define JAB2_FRAME_LO 6
#define JAB2_FRAME_HI 18

/* EscapeSheikThink_Constants */
static const struct {
    float p1_x, p1_y, p2_x, p2_y;
    float grab_distance;        /* 0x10 */
    float hold_shield_velocity; /* 0x14 */
    float run_distance;         /* 0x18 */
} EscapeSheik_Constants = {-8.0f, 0.0f, 8.0f, 0.0f, 14.0f, 1.1f, 23.0f};

static const char EscapeSheik_TopText[] = "Down B Input";
static const char EscapeSheik_BottomText[] = "%df %s";
static const char EscapeSheik_EarlyText[] = "Early";
static const char EscapeSheik_LateText[] = "Late";

/* TextCreateFunction (0x80005928, TM-CE's Display Message 2.0): tmce_osd.c */
void *mu_tmce_legacy_message(int ply, int timeout, int area, int window_id);

/* EscapeSheik_InitializePositions: Sheik holding the player in a grab, a random percent and throw
 * delay */
static void EscapeSheik_InitializePositions(LgPlayers *pl, EscapeSheikVars *v)
{
    FighterData *p1 = pl->p1;
    FighterData *p2 = pl->p2;
    int facing = 1; /* 1 = P1 facing right, -1 = P1 facing left */

    if (HSD_Randi(2) != 0)
        facing = -1;

    p1->facing_direction = (float) facing;
    p2->facing_direction = (float) (facing * -1);

    /* The ASM reads P1's coordinates for both positions and falls through into P1's store after
     * P2's, so both players start at (P1 X, P1 Y). */
    if (facing < 0) {
        p2->phys.pos.X = EscapeSheik_Constants.p1_x;
        p2->phys.pos.Y = EscapeSheik_Constants.p1_y;
    } else {
        p1->phys.pos.X = EscapeSheik_Constants.p1_x;
        p1->phys.pos.Y = EscapeSheik_Constants.p1_y;
    }
    if (facing >= 0) {
        p2->phys.pos.X = EscapeSheik_Constants.p1_x;
        p2->phys.pos.Y = EscapeSheik_Constants.p1_y;
    }
    p1->phys.pos.X = EscapeSheik_Constants.p1_x;
    p1->phys.pos.Y = EscapeSheik_Constants.p1_y;

    lg_PlacePlayerOnGround(pl->p1_gobj);
    lg_UpdateCameraBox(pl->p1_gobj);
    lg_PlacePlayerOnGround(pl->p2_gobj);
    lg_UpdateCameraBox(pl->p2_gobj);

    /* P1 into P2's grab pointers, P1's cleared, P2's effect pointer cleared */
    lg_FtSetPtr(p2, 0x1A58, pl->p1_gobj);
    lg_FtSetPtr(p2, 0x1A5C, pl->p1_gobj);
    lg_FtSetPtr(p1, 0x1A58, 0);
    lg_FtSetPtr(p1, 0x1A5C, 0);
    lg_FtSetPtr(p2, 0x60C, 0);
    /* P2 grabs, P1 is grabbed, P2 holds */
    lgc_AS_GrabOpponent(pl->p2_gobj);
    lgc_AS_Grabbed(pl->p1_gobj, pl->p2_gobj);
    lgc_AS_CatchWait(pl->p2_gobj);
    /* a lot of grab time */
    p1->grab.grab_timer = (float) 999;

    /* a random percent */
    Fighter_SetHUDDamage(p1->ply, HSD_Randi(PERCENT_HI - PERCENT_LO) + PERCENT_LO);

    /* the variables */
    v->event_state = STATE_THROW_DELAY;
    v->timer = 0;
    v->jab_frame = 0;
    v->throw_timer = HSD_Randi(THROW_TIMER_HI - THROW_TIMER_LO) + THROW_TIMER_LO;
    v->is_displayed_timing = 0;
}

/* EscapeSheikThink_CheckForMistimedShine: a down B while still in the tech (early) or already grabbed
 * (late) shows how many frames off it was */
static void EscapeSheik_CheckForMistimedShine(LgPlayers *pl, EscapeSheikVars *v)
{
    FighterData *p1 = pl->p1;
    HSD_Pad *pad;
    const char *string;
    int frames, state;
    Text *text;

    if (v->event_state < STATE_CHASE)
        return;
    if (v->is_displayed_timing != 0)
        return;
    /* this frame's inputs. The ASM loads P1's port into r4 but GetInputStruct takes r3, which holds
     * isDisplayedTiming (0) there: the pad of port 1. */
    pad = lg_GetInputStruct(0);
    /* B, with the stick down */
    if ((pad->down & PAD_BUTTON_B) == 0)
        return;
    if (pad->stickY > -44)
        return;

    state = p1->state_id;
    if (state == ASID_PASSIVE || state == ASID_PASSIVESTANDB || state == ASID_PASSIVESTANDF) {
        /* early: the frames left in the tech */
        Figatree *anim;
        int length;
        string = EscapeSheik_EarlyText;
        anim = Fighter_GetAnimData(p1, p1->action_id);
        length = (int) anim->frame_num;
        frames = length - (u16) p1->TM.state_frame;
    } else if (state == ASID_CAPTUREPULLEDLW || state == ASID_CAPTUREPULLEDHI || state == ASID_CAPTUREWAITLW
               || state == ASID_CAPTUREWAITHI) {
        /* late: the frames since the last Wait */
        int i;
        string = EscapeSheik_LateText;
        frames = (u16) p1->TM.state_frame;
        for (i = 0; i < 6; i++) {
            if (p1->TM.state_prev[i] == ASID_WAIT)
                break;
            frames += p1->TM.state_prev_frames[i];
        }
        /* no Wait among the previous states: nothing shown */
        if (i == 6)
            return;
    } else {
        return;
    }

    /* the reaction time */
    text = mu_tmce_legacy_message(p1->ply, 120, 0, LGC_OSD_MISCELLANEOUS);
    Text_AddSubtext(text, LGC_RTOC_0F, LGC_RTOC_0F, (char *) EscapeSheik_TopText);
    Text_AddSubtext(text, LGC_RTOC_0F, LGC_RTOC_40F, (char *) EscapeSheik_BottomText, frames, string);
    v->is_displayed_timing = 1;
}

/* EscapeSheikThink_Chase: Sheik reacts to the player's tech option */
static void EscapeSheik_Chase(LgPlayers *pl, EscapeSheikVars *v)
{
    FighterData *p1 = pl->p1;
    FighterData *p2 = pl->p2;
    float distance, abs_distance;
    int direction, state, facing;

    /* the distance in front of Sheik */
    distance = p1->phys.pos.X - p2->phys.pos.X;
    distance = distance * p2->facing_direction;
    /* 1 if P1 is in front of Sheik */
    direction = distance < (float) 0 ? -1 : 1;

    state = p1->state_id;
    if (state == ASID_DOWNBOUNDD || state == ASID_DOWNBOUNDU)
        goto down_bound_think;
    /* the tech options only from frame 12 */
    if ((u16) p1->TM.state_frame < REACT_FRAME)
        return;
    if (state == ASID_PASSIVE)
        goto passive_think;
    if (state == ASID_PASSIVESTANDB || state == ASID_PASSIVESTANDF)
        goto passive_stand_think;
    return;

passive_think:
    facing = (int) p2->facing_direction;
    /* turn to face P1: slightly towards */
    if (direction <= 0)
        p2->cpu.lstickX = facing * direction * 100;
    abs_distance = LGC_FABS(distance);
    if (!(abs_distance <= EscapeSheik_Constants.grab_distance)) {
        /* over 23 run towards, else walk */
        if (abs_distance > EscapeSheik_Constants.run_distance)
            p2->cpu.lstickX = facing * direction * 127;
        else
            p2->cpu.lstickX = facing * direction * 100;
    }
    /* in range while dashing or running: shield and hold it */
    if (!(abs_distance > EscapeSheik_Constants.grab_distance)) {
        if (p2->state_id == ASID_DASH || p2->state_id == ASID_RUN) {
            lgc_AS_Guard(pl->p2_gobj);
            p2->cpu.held = PAD_TRIGGER_R;
        }
    }
    /* grab on P1's frame 20 */
    if ((u16) p1->TM.state_frame < 20)
        return;
    lgc_AS_Catch(pl->p2_gobj, 0xD4);
    v->event_state = STATE_RESET;
    return;

passive_stand_think:
    facing = (int) p2->facing_direction;
    if (direction <= 0) {
        /* but is P1 coming this way? */
        int moving = p1->phys.pos_delta.X > (float) 0 ? 1 : -1;
        if (moving == facing * direction) {
            /* run under frame 20, then slightly towards */
            int magnitude = (u16) p1->TM.state_frame < 20 ? 127 : 100;
            p2->cpu.lstickX = facing * direction * magnitude;
        }
    }
    abs_distance = LGC_FABS(distance);
    if (!(abs_distance <= EscapeSheik_Constants.grab_distance)) {
        /* not while turning, until its frame 2 */
        if (!(p2->state_id == ASID_TURN && (u16) p2->TM.state_frame >= 2)) {
            /* is P1 coming this way? */
            int moving = p1->phys.pos_delta.X > (float) 0 ? 1 : -1;
            if (moving == facing * direction)
                p2->cpu.lstickX = facing * direction * 127;
        }
    }
    /* in range while dashing or running */
    if (!(abs_distance > EscapeSheik_Constants.grab_distance)
        && (p2->state_id == ASID_DASH || p2->state_id == ASID_RUN)) {
        /* shield from P1's frame 28, or earlier if P1 is not moving much */
        if ((u16) p1->TM.state_frame >= 28
            || !(LGC_FABS(p1->phys.pos_delta.X) > EscapeSheik_Constants.hold_shield_velocity)) {
            lgc_AS_Guard(pl->p2_gobj);
            p2->cpu.held = PAD_TRIGGER_R;
        } else {
            p2->cpu.lstickX = facing * direction * 127;
        }
    }
    /* grab on P1's frame 34 */
    if ((u16) p1->TM.state_frame < 34)
        return;
    lgc_AS_Catch(pl->p2_gobj, 0xD4);
    v->event_state = STATE_RESET;
    return;

down_bound_think:
    /* the jab frame, and maybe a second jab */
    if (v->jab_frame == 0) {
        v->jab_frame = HSD_Randi(JAB_FRAME_HI - JAB_FRAME_LO) + JAB_FRAME_LO;
        v->is_jab_twice = HSD_Randi(2);
        v->jab2_frame = HSD_Randi(JAB2_FRAME_HI - JAB2_FRAME_LO) + JAB2_FRAME_LO;
        v->jab2_timer = 0;
    }
    facing = (int) p2->facing_direction;
    /* turn to face P1 */
    if (direction <= 0)
        p2->cpu.lstickX = facing * direction * 100;
    abs_distance = LGC_FABS(distance);
    /* over 14 walk towards */
    if (!(abs_distance <= EscapeSheik_Constants.grab_distance))
        p2->cpu.lstickX = facing * direction * 100;
    /* in range and facing P1: stop */
    if (!(abs_distance > EscapeSheik_Constants.grab_distance) && direction == 1) {
        p2->cpu.held = 0;
        p2->cpu.lstickX = 0;
    }
    /* jab on the jab frame */
    if ((u16) p1->TM.state_frame < v->jab_frame)
        return;
    p2->cpu.held = PAD_BUTTON_A;
    p2->cpu.lstickX = 0;
    if (v->is_jab_twice != 0) {
        /* the second jab's timer */
        v->jab2_timer = v->jab2_frame;
        v->event_state = STATE_JAB2;
        return;
    }
    v->event_state = STATE_RESET;
    v->timer = RESET_TIMER + 30;
}

static void EscapeSheikThink(GOBJ *gobj)
{
    LgEventData *ed = lg_EventData(gobj);
    EscapeSheikVars *v = (EscapeSheikVars *) ed->raw;
    LgPlayers pl;
    FighterData *p1, *p2;
    int state;

    lg_GetAllPlayerPointers(&pl);
    p1 = pl.p1;
    p2 = pl.p2;
    lg_StoreCPUTypeAndZeroInputs(p2);

    /* on the first frame */
    if (lg_CheckIfFirstFrame()) {
        EscapeSheik_InitializePositions(&pl, v);
        lg_RemoveFirstFrameInputs();
        lg_SaveState_Save(&ed->savestate, 1);
    }

    /* reset if anyone died */
    if (lg_IsAnyoneDead())
        goto restore;

    /* failed (grabbed) once the chase started */
    if (v->event_state >= STATE_CHASE && p1->state_id >= ASID_CAPTUREPULLEDHI && p1->state_id <= ASID_CAPTUREFOOT
        && !(v->timer > 0)) {
        v->timer = RESET_TIMER;
        goto check_timer;
    }

    /* the CPU was hit (maybe a high score one day, the ASM says) */
    if (p2->state_id >= ASID_DAMAGEHI1 && p2->state_id <= ASID_DAMAGEFLYROLL && !(v->timer > 0)) {
        lg_PlaySFX(0xAD);
        v->timer = RESET_TIMER + 60;
        goto check_timer;
    }

    EscapeSheik_CheckForMistimedShine(&pl, v);

    switch (v->event_state) {
    case STATE_THROW_DELAY: {
        /* throw when the delay is up: down throw */
        int throw_timer = v->throw_timer - 1;
        v->throw_timer = throw_timer;
        if (throw_timer > 0)
            break;
        p2->cpu.lstickY = -127;
        v->event_state = STATE_THROW_ENDLAG;
        break;
    }

    case STATE_THROW_ENDLAG:
        /* Sheik out of the down throw */
        if (p2->state_id != ASID_WAIT)
            break;
        /* P1 in its tech option: a tech or a missed tech */
        state = p1->state_id;
        if (!(state >= ASID_PASSIVE && state <= ASID_PASSIVESTANDB) && state != ASID_DOWNBOUNDD
            && state != ASID_DOWNBOUNDU)
            break;
        v->event_state = STATE_CHASE;
        EscapeSheik_Chase(&pl, v);
        break;

    case STATE_CHASE:
        EscapeSheik_Chase(&pl, v);
        break;

    case STATE_JAB2:
        /* waiting on the second jab, not during hitlag */
        if (v->jab2_timer == 0)
            break;
        if (LGC_FT_HITLAG(p2))
            break;
        if (--v->jab2_timer > 0)
            break;
        p2->cpu.held = PAD_BUTTON_A;
        p2->cpu.lstickX = 0;
        v->event_state = STATE_RESET;
        v->timer = RESET_TIMER + 30;
        break;

    case STATE_RESET:
        /* hold shield */
        p2->cpu.held = PAD_TRIGGER_R;
        break;

    default:
        break;
    }

check_timer:
    if (v->timer <= 0)
        return;
    v->timer--;
    if (v->timer > 0)
        return;

restore:
    lg_SaveState_Load(&ed->savestate);
    EscapeSheik_InitializePositions(&pl, v);
}

static void EscapeSheikLoad(void)
{
    lg_StartOSDs();
    lg_CreateEventThinkFunction(EscapeSheikThink, 3, 0, 0);
}

void mu_tmce_legacy_EscapeSheik(MatchInit *match, int event_id)
{
    (void) event_id;
    /* Sheik on Final Destination, no OSDs, Sopo */
    lg_InitializeMatch(lg_EventStruct(), match, LGC_SHEIK_EXT, LGC_STAGE_FINAL_DESTINATION, 0, 1);
    match->onStartMelee = (void *) EscapeSheikLoad;
}
