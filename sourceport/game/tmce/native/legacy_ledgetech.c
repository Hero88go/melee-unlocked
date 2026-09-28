/* Training Mode CE, native build: Ledgetech, an original UnclePunch event.
 *
 * Rewritten in C from ASM/training-mode/Custom Events/Custom Event Code - Rewrite.asm (Ledgetech,
 * LedgetechLoad, LedgetechThink, Ledgetech_InitializePositions, BlrFunctionPointer and their data).
 * The player starts on the respawn platform off a ledge where a crouching Falco waits to down smash,
 * and scores by teching the down smash against the wall.
 *
 * Ledgetech_InitializePositions also sets up Ledgetech Counter (legacy_ledgetechcounter.c).
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_c_common.h"

typedef struct LedgetechVars {
    u8 x0[4];
    int timer;          /* 0x4 */
    u8 left_plat;       /* 0x8 left the respawn platform already */
    u8 teched;          /* 0x9 tech bool */
    u8 gatekeeper;      /* 0xA */
} LedgetechVars;

/* Ledgetech_Floats: the distance to start the down smash */
#define DSMASH_DISTANCE 35.0f

/* LedgetechWindowInfo: 1 window, Stage Side has 3 options */
static const u8 LedgetechWindowInfo[4] = {0x00, 0x02, 0x00, 0x00};
static const char LedgetechWindowText[] = "Stage Side\0"
                                          "Random\0"
                                          "Left\0"
                                          "Right\0";

#define OPT_STAGE_SIDE 0

/* BlrFunctionPointer: an empty physics callback */
static void BlrFunctionPointer(GOBJ *fighter)
{
    (void) fighter;
}

/* The stage side option (0 random, 1 left, 2 right, anything else random) as the ledge for
 * Ledgetech_InitializePositions (0 left, 1 right) */
static int Ledgetech_StageSide(LgMenuData *md)
{
    switch (LG_OPTION(md, OPT_STAGE_SIDE)) {
    case 1:
        return 0;
    case 2:
        return 1;
    default:
        return HSD_Randi(2);
    }
}

void lgc_Ledgetech_InitializePositions(LgPlayers *pl, int ledge_side)
{
    FighterData *p1 = pl->p1;
    FighterData *p2 = pl->p2;
    Vec3 ledge;
    float offset;
    float facing;

    /* the facing directions */
    if (ledge_side != 0) {
        p1->facing_direction = (float) -1;
        p2->facing_direction = (float) 1;
    } else {
        p1->facing_direction = (float) 1;
        p2->facing_direction = (float) -1;
    }

    lg_GetLedgeCoordinates(ledge_side, &ledge);

    /* P2 a few Mm behind the ledge, on the ground below it */
    offset = (float) 10 * p2->facing_direction;
    p2->phys.pos.X = ledge.X - offset;
    p2->phys.pos.Y = ledge.Y;
    {
        float ground_x, ground_y;
        int ground_line;
        if (lg_FindGroundNearPlayer(pl->p2_gobj, 0.0f, 0.0f, &ground_x, &ground_y, &ground_line)) {
            p2->phys.pos.X = ground_x;
            p2->phys.pos.Y = ground_y;
            p2->coll_data.ground_index = ground_line;
        }
    }
    Fighter_EnterWait(pl->p2_gobj);
    lg_UpdatePosition(pl->p2_gobj);
    /* the ECB values for the ground line */
    EnvironmentCollision_WaitLanding(pl->p2_gobj);
    Fighter_SetGrounded(p2);
    lg_UpdateCameraBox(pl->p2_gobj);

    /* P1 into Rebirth, keeping its facing direction */
    facing = p1->facing_direction;
    Fighter_EnterRebirth(pl->p1_gobj);
    p1->facing_direction = facing;
    /* a few Mm in front of the ledge */
    offset = (float) 60 * p1->facing_direction;
    p1->phys.pos.X = ledge.X - offset;
    p1->phys.pos.Y = ledge.Y;
    Fighter_EnterRebirthWait(pl->p1_gobj);
    lg_UpdatePosition(pl->p1_gobj);
    /* no physics, and the custom RebirthWait interrupt */
    lg_FtSetPtr(p1, 0x21A4, (void *) BlrFunctionPointer);
    lg_FtSetPtr(p1, 0x219C, (void *) lg_Custom_InterruptRebirthWait);
    Fighter_UpdateRebirthPlatformPos(pl->p1_gobj);
    lg_UpdateCameraBox(pl->p1_gobj);
}

static void LedgetechThink(GOBJ *gobj)
{
    LgEventData *ed = lg_EventData(gobj);
    LgMenuData *md = ed->menu;
    LedgetechVars *v = (LedgetechVars *) ed->raw;
    LgPlayers pl;
    FighterData *p1, *p2;

    lg_GetAllPlayerPointers(&pl);
    p1 = pl.p1;
    p2 = pl.p2;
    lg_StoreCPUTypeAndZeroInputs(p2);

    /* on the first frame */
    if (lg_CheckIfFirstFrame()) {
        lg_RemoveFirstFrameInputs();
        lgc_Ledgetech_InitializePositions(&pl, Ledgetech_StageSide(md));
        lgc_AS_SquatWait(pl.p2_gobj);
        /* P1 has 90% */
        LGC_SET_SHOWN_PERCENT(0, 90);
        p1->dmg.percent = 90.0f;
        /* always hold down (crouch cancel) */
        p2->cpu.lstickY = -127;
        lg_SaveState_Save(&ed->savestate, 1);
    }

    lg_GiveFullShields();

    /* reset if P1 is in a dead state */
    if (LGC_FT_DEAD(p1))
        goto restore_state;
    /* D-pad left restores the state */
    if (lg_FtU32(p1, 0x668) & PAD_BUTTON_DPAD_LEFT)
        goto restore_state;

    /* always hold down (crouch cancel) */
    p2->cpu.lstickY = -127;

    /* start the timer when leaving the respawn platform */
    if (v->left_plat == 0) {
        if (p1->state_id == 0xD) {
            /* RebirthWait lasts */
            p1->state_var.state_var1 = 2;
        } else {
            v->left_plat = 1;
            /* no more respawn intangibility */
            lg_FtSetU32(p1, 0x198C, 0);
            lg_FtSetU32(p1, 0x1990, 0);
            lg_FtSetU32(p1, 0x1994, 0);
            Fighter_GFXRemoveAll(pl.p1_gobj);
            v->timer = 180;
        }
    }

    /* freeze Falco on frame 6 */
    if (p2->state_id == 0x40 && (float) 6 == p2->state.frame)
        Fighter_SetAnimRate(pl.p2_gobj, (float) 0);

    /* a ledgetech sets the timer to 3 seconds */
    if (v->gatekeeper != 1) {
        if (p1->state_id == ASID_PASSIVEWALL || p1->state_id == ASID_PASSIVEWALLJUMP) {
            v->timer = 180;
            v->teched = 1;
            v->gatekeeper = 1;
        }
    }

    /* still ledgeteching */
    if (p1->state_id == ASID_PASSIVEWALL || p1->state_id == ASID_PASSIVEWALLJUMP) {
        /* the score */
        if (v->teched == 1) {
            v->teched = 0;
            lg_score.current++;
            if (lg_score.current > lg_score.best)
                lg_score.best = lg_score.current;
        }
    } else {
        v->gatekeeper = 0;
    }
    KOCount_Update(lg_score.current);

    /* unfreeze Falco on hit */
    if ((float) 0 == p2->state.rate && LGC_FT_HITBOX0_VICTIM0(p2) != 0)
        Fighter_SetAnimRate(pl.p2_gobj, (float) 1);

    /* the down smash, only from Squat and SquatWait */
    if (p2->state_id == 0x27 || p2->state_id == 0x28) {
        float distance = lg_GetDistance(&p2->phys.pos, &p1->phys.pos);
        if (!(distance > DSMASH_DISTANCE)) {
            p2->cpu.cstickY = -127;
            /* the reset timer */
            if (v->timer == 0)
                v->timer = 60;
        }
    }

    if (v->timer <= 0)
        return;
    if (--v->timer != 0)
        return;

restore_state:
    lg_SaveState_Load(&ed->savestate);
    lgc_Ledgetech_InitializePositions(&pl, Ledgetech_StageSide(md));
    lgc_AS_SquatWait(pl.p2_gobj);
    v->timer = 0;
    v->left_plat = 0;
    v->teched = 0;
    v->gatekeeper = 0;
    lg_score.current = 0;
}

static void LedgetechLoad(void)
{
    lg_StartOSDs();
    lg_InitializeHighScore();
    lg_CreateEventThinkFunction(LedgetechThink, 3, LedgetechWindowInfo, LedgetechWindowText);
}

void mu_tmce_legacy_Ledgetech(MatchInit *match, int event_id)
{
    (void) event_id;
    /* Falco on the chosen stage, no OSDs, Sopo */
    lg_InitializeMatch(lg_EventStruct(), match, LGC_FALCO_EXT, -1, 0, 1);
    /* "BUFF DEFENSE RATIO": the ASM stores 0.5 (0x3F000000) at 0x14(r20). r20 there is still
     * onEnterVs's level table (gm_804D6900[0]: the pointers to each event's level entry), so the
     * word written is that table's entry 5, not a defense ratio, and nothing of this event reads it.
     * Not ported. */
    match->onStartMelee = (void *) LedgetechLoad;
}
