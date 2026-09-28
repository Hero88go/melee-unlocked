/* Training Mode CE, native build: Reaction Test, Custom Event Code - Rewrite.asm lines 275-535,
 * rewritten in C. Fox shines after 3 to 7 seconds; press any button as soon as you see or hear it.
 * Acting early plays the error sound and restarts.
 *
 * The console showed the result in the OSD window (TextCreateFunction, 120 frames); natively it is a
 * TM-CE message (event_vars->Message_Display, the same lifetime) with the same two lines and colours.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_common.h"
#include "legacy.h"

void mu_lg_ftFx_SpecialLw_Enter(GOBJ *gobj) __asm__("ftFx_SpecialLw_Enter"); /* 0x800E8560 */
void mu_lg_lbAudioAx_80025064(_Bool arg0, _Bool arg1) __asm__("lbAudioAx_80025064");

/* Constants */
#define SHINE_TIMER_MAX (7 * 60)
#define SHINE_TIMER_MIN (3 * 60)
#define RESET_TIMER (1 * 60)

/* GObj Data Offsets */
#define OFST_ShineTimer 0x0
#define OFST_ResetTimer 0x2
#define OFST_ReactionTimer 0x4

#define OSD_MISCELLANEOUS 15

static void Reaction_Reset(LgEventData *ed)
{
    /* Load State */
    lg_SaveState_Load(&ed->savestate);
    /* Set Initial Timer, Reset Timer, Initialize Reaction Timer */
    LG_EV_U16(ed, OFST_ShineTimer) = HSD_Randi(SHINE_TIMER_MAX - SHINE_TIMER_MIN) + SHINE_TIMER_MIN;
    LG_EV_U16(ed, OFST_ResetTimer) = 0;
    LG_EV_S16(ed, OFST_ReactionTimer) = -1;
}

/* ReactionThink */
static void ReactionThink(GOBJ *event)
{
    LgEventData *ed = lg_EventData(event);
    LgPlayers pl;
    int timer;

    lg_GetAllPlayerPointers(&pl);

    /* First Frame Actions */
    if (lg_CheckIfFirstFrame()) {
        lg_PlacePlayersCenterStage(lg_StageGetGroundID_Main());
        lg_SaveState_Save(&ed->savestate, 1);
        LG_EV_U16(ed, OFST_ShineTimer) = HSD_Randi(SHINE_TIMER_MAX - SHINE_TIMER_MIN) + SHINE_TIMER_MIN;
        LG_EV_S16(ed, OFST_ReactionTimer) = -1;
        /* Stop Music (0x80025064 with 0, 2) */
        mu_lg_lbAudioAx_80025064(0, 1);
    }

    lg_StoreCPUTypeAndZeroInputs(pl.p2);

    /* Give Intangibility to both chars */
    Fighter_ApplyIntang(pl.p1_gobj, 1);
    Fighter_ApplyIntang(pl.p2_gobj, 1);

    /* Check post countdown timer: dec, reset at 0 */
    timer = LG_EV_U16(ed, OFST_ResetTimer);
    if (timer > 0) {
        timer -= 1;
        LG_EV_U16(ed, OFST_ResetTimer) = timer;
        if (timer == 0) {
            Reaction_Reset(ed);
        }
        return;
    }

    /* Check shine countdown timer: dec, shine at 0 */
    timer = LG_EV_U16(ed, OFST_ShineTimer);
    if (timer > 0) {
        timer -= 1;
        LG_EV_U16(ed, OFST_ShineTimer) = timer;
        if (timer > 0) {
            /* ReactionThink_CheckIfActedEarly */
            if (pl.p1->state_id != ASID_WAIT) {
                lg_PlaySFX(0xAF);
                LG_EV_U16(ed, OFST_ResetTimer) = RESET_TIMER - 40;
            }
            return;
        }
        /* Perform down b, start reaction timer */
        mu_lg_ftFx_SpecialLw_Enter(pl.p2_gobj);
        LG_EV_U16(ed, OFST_ReactionTimer) = 0;
        return;
    }

    /* Reaction_SkipShineTimer: check if P1 reacted */
    if (LG_EV_S16(ed, OFST_ReactionTimer) >= 0 && lg_GetInputStruct(pl.p1->pad_index)->down != 0) {
        int frames = LG_EV_U16(ed, OFST_ReactionTimer);
        /* Output reaction time: green up to 15 frames, red beyond */
        event_vars->Message_Display(OSD_MISCELLANEOUS, pl.p1->ply, frames <= 15 ? MSGCOLOR_GREEN : MSGCOLOR_RED,
                                    "Reaction Time:\n%d Frames", frames + 1); /* 0-index is scary */
        /* Start post countdown timer */
        LG_EV_U16(ed, OFST_ResetTimer) = RESET_TIMER;
        return;
    }

    /* Reaction_SkipReactionTimer: Inc timer (the halfword wraps, as sth does) */
    LG_EV_U16(ed, OFST_ReactionTimer) = LG_EV_U16(ed, OFST_ReactionTimer) + 1;
}

/* ReactionLoad */
static void ReactionLoad(void)
{
    lg_StartOSDs();
    /* Schedule Think: priority 3 (interrupt), no option menu */
    lg_CreateEventThinkFunction(ReactionThink, 3, 0, 0);
}

/* Reaction (HIJACK INFO) */
void mu_tmce_legacy_Reaction(MatchInit *match, int event_id)
{
    /* SET EVENT TYPE TO KOs */
    mu_tmce_legacy_event_set_flag(0x02, 0);
    /* Store Stage, CPU, and FDD Toggles: Fox, Final Destination */
    lg_InitializeMatch(lg_EventStruct(), match, 0x2, 0x20, 0, 0);
    /* STORE THINK FUNCTION */
    match->onStartMelee = ReactionLoad;
}
