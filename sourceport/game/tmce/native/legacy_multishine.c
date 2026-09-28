/* Training Mode CE, native build: Shined Blind (Multishine), Custom Event Code - Rewrite.asm lines
 * 134-273, rewritten in C. How many shines in 10 seconds: each grounded or aerial shine's first frame
 * scores one KO on the HUD counter; at time up the event ends with its result.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_common.h"
#include "legacy.h"

void mu_lg_Player_UpdateKOsBySlot(int slot, _Bool bool_arg, int other_slot) __asm__("Player_UpdateKOsBySlot");
int mu_lg_Player_GetKOsByPlayerIndex(int slot, int idx) __asm__("Player_GetKOsByPlayerIndex");
unsigned int mu_lg_gm_8016AEEC(void) __asm__("gm_8016AEEC"); /* MatchInfo_LoadSeconds */
unsigned short mu_lg_gm_8016AEFC(void) __asm__("gm_8016AEFC"); /* MatchInfo_LoadSubSeconds */

/* MultishineThink */
static void MultishineThink(GOBJ *event)
{
    LgPlayers pl;
    int state;

    lg_GetAllPlayerPointers(&pl);

    /* First Frame Actions: Init Positions */
    if (lg_CheckIfFirstFrame()) {
        lg_PlacePlayersCenterStage(lg_StageGetGroundID_Main());
    }

    /* Check for grounded or aerial shine, frame 1: Increment Score */
    state = pl.p1->state_id;
    if ((state == 0x168 || state == 0x16D) && (u16) pl.p1->TM.state_frame == 0) {
        mu_lg_Player_UpdateKOsBySlot(0, 0, 5);
    }

    /* Check For TimeUp: On Event End */
    if (mu_lg_gm_8016AEEC() == 0 && mu_lg_gm_8016AEFC() == 59) {
        lg_EventMatch_OnWinCondition(event);
    }

    /* Update HUD Score */
    KOCount_Update(mu_lg_Player_GetKOsByPlayerIndex(0, 5));
}

/* MultishineLoad: the match's on-start callback */
static void MultishineLoad(void)
{
    lg_StartOSDs();
    /* Schedule Think: priority 17 (after everything), no option menu */
    lg_CreateEventThinkFunction(MultishineThink, 17, 0, 0);
    lg_InitializeHighScore();
}

/* Multishine (HIJACK INFO) */
void mu_tmce_legacy_Multishine(MatchInit *match, int event_id)
{
    /* COUNT DOWN TIME: byte 0 = 0x06 */
    match->matchType = 0;
    match->hudPos = 1;
    mexbits_set_MatchInit__timer(match, 2);
    /* 10 Seconds On the Clock */
    match->timer_seconds = 10;
    /* Store Match Type to READY, GO!: byte 1 = 0x80 */
    match->timer_unk2 = 1;
    match->unk4 = 0;
    match->hideReady = 0;
    match->hideGo = 0;
    match->isDisableMusic = 0;
    match->unk3 = 0;
    match->timer_unk = 0;
    match->unk2 = 0;

    /* SET EVENT TYPE TO KOs: zero the event's time bit (0x8045ABF0 + 0xB, 0x02) */
    mu_tmce_legacy_event_set_flag(0x02, 0);

    /* Store Stage, CPU, and FDD Toggles: chosen CPU, Final Destination */
    lg_InitializeMatch(lg_EventStruct(), match, -1, 0x20, 0, 0);

    /* 1 Player */
    lg_EventStruct()->flags = 0x20;

    /* STORE THINK FUNCTION: on match load */
    match->onStartMelee = MultishineLoad;
}
