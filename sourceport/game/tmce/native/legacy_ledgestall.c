/* Training Mode CE, native build: Under Fire (Ledge Stall), Custom Event Code - Rewrite.asm lines
 * 538-1083, rewritten in C. On Brinstar's left ledge, the lava rises; ledgestall to stay invincible.
 * Time counts while you are below the lava; getting hit or dying ends the event with that time.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_common.h"
#include "legacy.h"

void mu_lg_gm_8016B378(s8 frames) __asm__("gm_8016B378"); /* frames to linger */

/* LedgeStallThink_Constants */
#define LAVA_RISE_RATE 0.4F
#define LAVA_MAX_Y 10.0F

/* Event Data Offsets */
#define EventState 0x0
#define EventState_LavaDelay 0x0
#define EventState_LavaRiseThink 0x1
#define EventState_Reset 0x2
#define Timer 0x1
#define LavaTimer 0x2
#define SurvivalTime 0x4
#define LavaPosition 0x8

/* Constants */
#define RESET_TIMER 30
#define LAVA_START_Y -100
#define LAVA_START_TIMER 123
#define SURVIVAL_TIME_COUNT_INTERVAL 5

/* fmod (0x80364340, MSL's fmodf) */
static float ledgestall_fmodf(float a, float b)
{
    s64 quotient;
    float product;
    if (__builtin_fabsf(b) > __builtin_fabsf(a)) {
        return a;
    }
    quotient = a / b;
    product = b * quotient;
    return a - product;
}

/* Ledgestall_UpdateLavaPosition: f1 = the lava's Y */
static void Ledgestall_UpdateLavaPosition(float y)
{
    /* Get Lava map_gobj and its JObj */
    JOBJ *jobj = Stage_GetMapGObjJObj(Stage_GetMapGObj(8), 0);
    /* Update JObj Y position (the jobj sits 55 below the lava's surface) */
    jobj->trans.Y = 55.0F + y;
    JOBJ_SetMtxDirtySub(jobj);
    /* Adjust hitbox position */
    Stage_SetChkDevicePos(y);
}

/* LedgeStall_InitializePositions */
static void LedgeStall_InitializePositions(GOBJ *p1_gobj, LgEventData *ed)
{
    /* Place on left ledge, Update Camera */
    lg_PlaceOnLedge(p1_gobj, 0);
    lg_UpdateCameraBox(p1_gobj);
    /* Init Lava Y */
    LG_EV_F32(ed, LavaPosition) = (float) LAVA_START_Y;
    Ledgestall_UpdateLavaPosition((float) LAVA_START_Y);
    /* Reset Variables */
    LG_EV_U8(ed, EventState) = EventState_LavaDelay;
    LG_EV_U8(ed, Timer) = 0;
    LG_EV_S32(ed, SurvivalTime) = 0;
    /* Init Lava Start Timer */
    LG_EV_U16(ed, LavaTimer) = LAVA_START_TIMER;
}

static void reset_match_timer(void)
{
    stc_match->time_seconds = 0;
    stc_match->time_ms = 0;
}

/* LedgeStallThink */
static void LedgeStallThink(GOBJ *event)
{
    LgEventData *ed = lg_EventData(event);
    LgPlayers pl;
    int state;
    int timer;

    lg_GetAllPlayerPointers(&pl);

    /* ON FIRST FRAME */
    if (lg_CheckIfFirstFrame()) {
        LedgeStall_InitializePositions(pl.p1_gobj, ed);
        lg_RemoveFirstFrameInputs();
        lg_SaveState_Save(&ed->savestate, 1);
    }

    /* Check if player was damaged (DamageHi1 .. DamageFlyRoll) or died */
    state = pl.p1->state_id;
    if ((state >= ASID_DAMAGEHI1 && state <= ASID_DAMAGEFLYROLL) || lg_FtFlag(pl.p1, 0x221F, 0x40)) {
        /* LedgeStallThink_TookDamage: once */
        if (LG_EV_U8(ed, Timer) == 0) {
            LG_EV_U8(ed, Timer) = RESET_TIMER;
            LG_EV_U8(ed, EventState) = EventState_Reset;
        }
    }

    /* Switch Case */
    switch (LG_EV_U8(ed, EventState)) {
    case EventState_LavaDelay:
        /* Reset Timer back to 0, Decrement Lava Timer */
        reset_match_timer();
        timer = LG_EV_U16(ed, LavaTimer) - 1;
        LG_EV_U16(ed, LavaTimer) = timer;
        if (timer > 0) {
            break;
        }
        LG_EV_U8(ed, EventState) = EventState_LavaRiseThink;
        /* fall through: LedgeStallThink_LavaRiseThink */
    case EventState_LavaRiseThink: {
        float lava;
        /* Ensure player is in the lava before incrementing survival time */
        if (pl.p1->phys.pos.Y < LG_EV_F32(ed, LavaPosition)) {
            LG_EV_S32(ed, SurvivalTime) += 1;
            /* Play HRC SFX every X score */
            if (ledgestall_fmodf((float) (LG_EV_S32(ed, SurvivalTime) - 1), (float) SURVIVAL_TIME_COUNT_INTERVAL) == 0.0F) {
                lg_PlaySFX(0xBB);
                lg_PlaySFX(0xBB);
            }
        } else {
            /* LedgeStallThink_LavaRiseThink_NotInLava */
            reset_match_timer();
            LG_EV_S32(ed, SurvivalTime) = 0;
        }
        /* Check if lava is at max height, else raise it */
        lava = LG_EV_F32(ed, LavaPosition);
        if (lava < LAVA_MAX_Y) {
            lava = lava + LAVA_RISE_RATE;
            LG_EV_F32(ed, LavaPosition) = lava;
            Ledgestall_UpdateLavaPosition(lava);
        }
        break;
    }
    case EventState_Reset:
        /* Effectively pause timer */
        if (stc_match->time_seconds != 0 && stc_match->time_ms == 0) {
            stc_match->time_ms = 0x3B;
            stc_match->time_seconds -= 1;
        } else {
            stc_match->time_ms -= 1;
        }
        break;
    }

    /* LedgeStallThink_CheckTimer */
    timer = LG_EV_U8(ed, Timer);
    if (timer == 0) {
        return;
    }
    timer -= 1;
    LG_EV_U8(ed, Timer) = timer;
    if (timer > 0) {
        return;
    }

    /* Pause game (game speed 1) */
    HSD_SetSpeedEasy(1.0F);
    if (LG_EV_S32(ed, SurvivalTime) > 0) {
        /* LedgeStallThink_Success: compare with the high score */
        int event_id = stc_memcard->EventBackup.event;
        int survival = LG_EV_S32(ed, SurvivalTime);
        if (survival > Events_GetSavedScore(event_id)) {
            /* NewHighScore: Success + SFX, crowd cheer, end, save */
            Match_SetEndGraphic(2);
            MatchInfo_0x0010_store(0x9C40);
            Match_SetPostMatchSFX(325);
            Match_EndImmediate();
            Events_StoreEventScore(event_id, survival);
            Events_SetEventAsPlayed(event_id);
        } else {
            /* NoHighScore */
            Match_SetEndGraphic(2);
            Match_SetPostMatchSFX(324);
            Match_EndImmediate();
        }
    } else {
        /* LedgeStallThink_Failure: Failure + SFX, crowd sigh, linger */
        Match_SetEndGraphic(6);
        Match_SetPostMatchSFX(328);
        mu_lg_gm_8016B378(40);
        Match_EndImmediate();
    }
    /* destroy event gobj */
    GObj_Destroy(event);
    /* LedgeStallThink_Restore (restore the state, place again, enable inputs) has no caller in the ASM */
}

/* LedgeStallLoad */
static void LedgeStallLoad(void)
{
    CmSubject *cam;
    Vec3 ledge;
    int ids;

    lg_StartOSDs();
    /* Schedule Think: priority 3 (after EnvCollision), no option menu */
    lg_CreateEventThinkFunction(LedgeStallThink, 3, 0, 0);

    /* Destroy Lava map_gobj proc (lava's map_gobj ID 8) */
    GObj_RemoveProc(Stage_GetMapGObj(8));
    /* Platform (map_gobj 6): its flesh item's hurtbox intangible */
    mu_tmce_legacy_zebes_platform_intangible(Stage_GetMapGObj(6));

    /* Create Camera Box behind the left ledge */
    cam = CameraSubject_Alloc();
    ids = lg_LedgeCliffIDs[Stage_GetExternalID()];
    Stage_GetLeftOfLineCoordinates((ids >> 8) & 0xFF, &ledge);
    cam->cam_pos.X = 35.0F + ledge.X;
    cam->cam_pos.Y = 20.0F + ledge.Y;
    /* Make Boundaries around ledge position */
    cam->boundleft_proj = -10.0F;
    cam->boundright_proj = 10.0F;
    cam->boundtop_proj = 10.0F;
    cam->boundbottom_proj = -10.0F;

    /* Set Camera To Be Zoomed Out More (stage_info + 0x28 = 1.8) */
    stc_stage->x28 = 1.8F;
}

/* LedgeStall (HIJACK INFO) */
void mu_tmce_legacy_LedgeStall(MatchInit *match, int event_id)
{
    /* Store Match Type to READY, GO!: byte 1 = 0x80 */
    match->timer_unk2 = 1;
    match->unk4 = 0;
    match->hideReady = 0;
    match->hideGo = 0;
    match->isDisableMusic = 0;
    match->unk3 = 0;
    match->timer_unk = 0;
    match->unk2 = 0;

    /* Store Stage, CPU, and FDD Toggles: chosen CPU, Brinstar, Ice Climbers as Popo */
    lg_InitializeMatch(lg_EventStruct(), match, -1, 0x6, 0, 1);

    /* 1 Player */
    lg_EventStruct()->flags = 0x20;

    /* LedgeStallStoreThink */
    match->onStartMelee = LedgeStallLoad;
}
