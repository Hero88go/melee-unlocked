/* Training Mode CE, native build: SDI Training, Custom Event Code - Rewrite.asm lines 1084-1568,
 * rewritten in C. Fox up-throws you, follows, jumps and up-airs; Smash DI out of the up-air to score.
 * The score (lg_score) counts escapes in a row; a missed one resets it.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_common.h"
#include "legacy.h"

/* Event data: 0x4 timer (s32), 0x8 state, 0x9 reset timer, 0xA SDI'd flag */
#define OFST_Timer 0x4
#define OFST_State 0x8
#define OFST_ResetTimer 0x9
#define OFST_SDIFlag 0xA

/* SDITrainingStartingPercents: per internal character (as the table's bytes, which differ from its
 * comments in places) */
static const u8 sdi_starting_percents[28] = {
    0x08, 0x5F, 0x80, 0x40, /* Mario, Fox, Captain Falcon, DK */
    0x00, 0x30, 0x19, 0x0F, /* Kirby, Bowser, Link, Sheik */
    0x00, 0x00, 0x00, 0x00, /* Ness, Peach, Popo, Nana */
    0x08, 0x05, 0x0F, 0x00, /* Pikachu, Samus, Yoshi, Jigglypuff */
    0x00, 0x00, 0x18, 0x00, /* Mewtwo, Luigi, Marth, Zelda */
    0x19, 0x08, 0x5F, 0x00, /* Young Link, Doc, Falco, Pichu */
    0x00, 0x2D, 0x3A, 0x00, /* Game and Watch, Ganondorf, Roy */
};

/* SDITrainingFloats: P1 X, P1 Y, P2 X, P2 Y (0xC02CCCCD, 0, 0x4144CCCD, 0) */
static const float sdi_floats[4] = { -2.7F, 0.0F, 12.3F, 0.0F };

/* SDITrainingInputTowardsOpponent: 1 if within `distance` of P1 in X, else push P2's stick toward P1 */
static int SDITrainingInputTowardsOpponent(int distance, FighterData *p1, FighterData *p2)
{
    float dx = p2->phys.pos.X - p1->phys.pos.X;
    if (__builtin_fabsf(dx) > (float) distance) {
        /* SDITrainingFollowOpponentThink_InputTowardsOpponent */
        p2->cpu.lstickX = -lg_GetDirectionInRelationToP1(p1, p2) * 127;
        return 0;
    }
    return 1;
}

/* SDITrainingCheckForReset */
static void SDITrainingCheckForReset(LgEventData *ed)
{
    int timer = LG_EV_U8(ed, OFST_ResetTimer);
    if (timer == 0) {
        return;
    }
    timer -= 1;
    LG_EV_U8(ed, OFST_ResetTimer) = timer;
    if (timer != 0) {
        return;
    }
    /* Load State (twice), Random Timer, Reset State and the SDI'd flag */
    lg_SaveState_Load(&ed->savestate);
    lg_SaveState_Load(&ed->savestate);
    LG_EV_S32(ed, OFST_Timer) = HSD_Randi(60) * -1;
    LG_EV_U8(ed, OFST_State) = 0;
    LG_EV_U8(ed, OFST_SDIFlag) = 0;
}

/* SDITrainingThink */
static void SDITrainingThink(GOBJ *event)
{
    LgEventData *ed = lg_EventData(event);
    LgPlayers pl;
    FighterData *p1, *p2;

    lg_GetAllPlayerPointers(&pl);
    p1 = pl.p1;
    p2 = pl.p2;

    p2->cpu.ai = 0xF;
    lg_StoreCPUTypeAndZeroInputs(p2);

    /* ON FIRST FRAME */
    if (lg_CheckIfFirstFrame()) {
        int percent;
        lg_Event_EnterGrab(sdi_floats, pl.p1_gobj, pl.p2_gobj);
        /* Store 999 To Breakout (0x44610000: 900.0) */
        p1->grab.grab_timer = 900.0F;
        /* Give Percent: shown on the HUD (main and subchar values) and the real damage */
        percent = (p1->kind >= 0 && p1->kind < 28) ? sdi_starting_percents[p1->kind] : 0;
        mu_tmce_legacy_static_set(0, 0x60, percent);
        mu_tmce_legacy_static_set(0, 0x62, percent);
        p1->dmg.percent = (float) percent;
        lg_RemoveFirstFrameInputs();
        lg_SaveState_Save(&ed->savestate, 1);
        /* Random Start Time */
        LG_EV_S32(ed, OFST_Timer) = 0 - HSD_Randi(60);
    }

    /* SDITrainingThinkMain: Inc Timer */
    LG_EV_S32(ed, OFST_Timer) += 1;

    /* Check If SDI'd UpAir (DamageAir2 without hitstun), once */
    if (LG_EV_U8(ed, OFST_SDIFlag) != 1 && p1->state_id == 0x55 && !lg_FtFlag(p1, 0x221C, 0x02)) {
        LG_EV_U8(ed, OFST_SDIFlag) = 1;
        lg_PlaySFX(0xAD);
        lg_score.current += 1;
        if (lg_score.current > lg_score.best) {
            lg_score.best = lg_score.current;
        }
    }

    /* Check If Missed SDI: Fox up-airing past frame 11 while P1 is in hitlag */
    if (p2->state_id == 0x44 && lg_FtFlag(p1, 0x221A, 0x20) && p2->state.frame >= 11.0F) {
        lg_score.current = 0;
    }

    /* Update Score */
    KOCount_Update(lg_score.current);

    /* Check State */
    switch (LG_EV_U8(ed, OFST_State)) {
    default:
    case 0:
        /* SDITrainingUpThrowThink */
        if (LG_EV_S32(ed, OFST_Timer) < 0) {
            return;
        }
        if (p2->state_id != ASID_WAIT) {
            /* SDITrainingUpThrowThink_InputUpThrow */
            p2->cpu.lstickY = 127;
            return;
        }
        LG_EV_U8(ed, OFST_State) = 1;
        /* fall through */
    case 1: {
        /* SDITrainingFollowOpponentThink */
        int in_range;
        if (p2->phys.air_state != 1) {
            /* CheckDistance: 15 while dashing, else 10 */
            in_range = SDITrainingInputTowardsOpponent(p2->state_id == 0x14 ? 15 : 10, p1, p2);
            if (p2->state_id != 0x18 && in_range != 1) {
                return;
            }
            /* CheckIfJumping: already in jumpsquat, full hop unless within 32 in Y */
            if (p2->state_id == 0x18 && __builtin_fabsf(p2->phys.pos.Y - p1->phys.pos.Y) < 32.0F) {
                return;
            }
            /* InputJump */
            p2->cpu.held = PAD_BUTTON_Y;
            return;
        }
        LG_EV_U8(ed, OFST_State) = 2;
    }
        /* fall through */
    case 2:
        /* SDITrainingJumpThink: follow, up-air when within 35 in Y */
        SDITrainingInputTowardsOpponent(5, p1, p2);
        if (__builtin_fabsf(p2->phys.pos.Y - p1->phys.pos.Y) > 35.0F) {
            /* SDITrainingJumpThink_CheckToDJ: jump again on frame 4 of the jump */
            if ((p2->state_id == 0x19 || p2->state_id == 0x1A) && 4.0F == p2->state.frame) {
                p2->cpu.held = PAD_BUTTON_Y;
            }
            return;
        }
        p2->cpu.cstickY = 127;
        LG_EV_U8(ed, OFST_ResetTimer) = 60;
        LG_EV_U8(ed, OFST_State) = 3;
        /* fall through */
    case 3:
        /* SDITrainingUpAirThink: follow until the hitboxes are over, then wait to land */
        if (p2->state.frame < 12.0F) {
            SDITrainingInputTowardsOpponent(5, p1, p2);
            if (p2->phys.air_state == 0) {
                LG_EV_U8(ed, OFST_State) = 4;
            }
        }
        /* fall through */
    case 4:
        SDITrainingCheckForReset(ed);
        return;
    }
}

/* SDITrainingLoad */
static void SDITrainingLoad(void)
{
    lg_StartOSDs();
    lg_InitializeHighScore();
    /* Schedule Think: priority 3 (after interrupt), no option menu */
    lg_CreateEventThinkFunction(SDITrainingThink, 3, 0, 0);
}

/* SDITraining (HIJACK INFO) */
void mu_tmce_legacy_SDITraining(MatchInit *match, int event_id)
{
    /* Store Stage, CPU, and FDD Toggles: Fox, Final Destination, Ice Climbers as Popo */
    lg_InitializeMatch(lg_EventStruct(), match, 0x2, 0x20, 0, 1);
    /* Make default color (P2's record) */
    lg_EventPlayer(1)->color = 0;
    /* SDITrainingStoreThink */
    match->onStartMelee = SDITrainingLoad;
}
