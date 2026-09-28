/* Training Mode CE, native build: Amsah Tech, an original UnclePunch event.
 *
 * Rewritten in C from ASM/training-mode/Custom Events/Custom Event Code - Rewrite.asm (AmsahTech,
 * AmsahTechLoad, AmsahTechThink and their data). A taunt places Marth in front of the player; after
 * the chosen delay Marth up-Bs and the player practices the Amsah tech.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_c_common.h"

typedef struct AmsahTechVars {
    u8 x0[4];
    int timer;      /* 0x4 the reset timer */
    int upb_timer;  /* 0x8 Timer */
} AmsahTechVars;

/* AmsahTech_Floats +0x10: the distance to place Marth from P1 (the table's P1 X, P2 X, P1 Y, FD floor
 * Y and FD boundary X are not read) */
#define MARTH_DISTANCE 16.0f

/* AmsahTechWindowInfo: 2 windows, Up B Timer has 2 options, Reset Timer has 3 options */
static const u8 AmsahTechWindowInfo[4] = {0x01, 0x01, 0x02, 0x00};
static const char AmsahTechWindowText[] = "Up B Timer Frame\0"
                                          "30\0"
                                          "50\0"
                                          "Reset Timer Frame\0"
                                          "120\0"
                                          "180\0"
                                          "240\0";

#define OPT_UPB_TIMER 0
#define OPT_RESET_TIMER 1

static void AmsahTechThink(GOBJ *gobj)
{
    LgEventData *ed = lg_EventData(gobj);
    LgMenuData *md = ed->menu;
    AmsahTechVars *v = (AmsahTechVars *) ed->raw;
    LgPlayers pl;
    FighterData *p1, *p2;
    int taunting;

    lg_GetAllPlayerPointers(&pl);
    p1 = pl.p1;
    p2 = pl.p2;
    lg_StoreCPUTypeAndZeroInputs(p2);

    /* on the first frame */
    if (lg_CheckIfFirstFrame()) {
        lg_PlacePlayersCenterStage(lg_StageGetGroundID_Main());
        /* P1 has 120% */
        LGC_SET_SHOWN_PERCENT(0, 120);
        p1->dmg.percent = (float) 120;
        lg_RemoveFirstFrameInputs();
        lg_SaveState_Save(&ed->savestate, 1);
    }

    /* P2 is not nudged */
    LGC_FT_NUDGE_DISABLE_SET(p2);
    lg_GiveInvincibility(pl.p2_gobj, 2);
    lg_UpdateAllGFX(pl.p2_gobj);

    /* reset if anyone dies */
    if (lg_IsAnyoneDead())
        goto restore_state;

    /* the reset timer runs already */
    if (v->timer != 0)
        goto check_upb_timer;

    /* P1 taunting: the taunts, Dr. Mario's 0x155, Young Link's 0x156 */
    taunting = 0;
    if (p1->state_id == 0x108 || p1->state_id == 0x109)
        taunting = 1;
    else if (p1->kind == 0x15 && p1->state_id == 0x155)
        taunting = 1;
    else if (p1->kind == 0x14 && p1->state_id == 0x156)
        taunting = 1;
    if (!taunting)
        goto check_upb_timer;

    /* on the taunt's first frame, with no up B timer set and Marth in Wait */
    if ((u16) p1->TM.state_frame != 1)
        goto check_upb_timer;
    if (v->upb_timer != 0)
        goto check_upb_timer;
    if (p2->state_id != ASID_WAIT)
        goto check_upb_timer;

    /* Marth 10 Mm in front of P1, facing him, on the ground */
    {
        float offset = (float) 10 * p1->facing_direction;
        float x = offset + p1->phys.pos.X;
        float ground_x, ground_y;
        int ground_line;
        if (!lg_FindGroundNearPlayer(0, x, p1->phys.pos.Y, &ground_x, &ground_y, &ground_line)) {
            lg_PlaySFX(0xAF); /* error */
            goto check_to_reset;
        }
        p2->phys.pos.X = ground_x;
        p2->phys.pos.Y = ground_y;
        p2->coll_data.ground_index = ground_line;
    }
    p2->facing_direction = -p1->facing_direction;
    Fighter_EnterWait(pl.p2_gobj);
    lg_UpdatePosition(pl.p2_gobj);
    /* the ECB values for the ground line */
    EnvironmentCollision_WaitLanding(pl.p2_gobj);
    Fighter_SetGrounded(p2);

    /* the up B timer */
    v->upb_timer = LG_OPTION(md, OPT_UPB_TIMER) == 1 ? 50 : 30;

    /* into the backups as well */
    {
        FighterData *p2_backup = ed->savestate.player[1].main;
        FighterData *p1_backup = ed->savestate.player[0].main;
        FighterData *follower;
        p2_backup->phys.pos.X = p2->phys.pos.X;
        p2_backup->phys.pos.Y = p2->phys.pos.Y;
        p2_backup->facing_direction = p2->facing_direction;
        p2_backup->coll_data.ground_index = p2->coll_data.ground_index;
        p1_backup->phys.pos.X = p1->phys.pos.X;
        p1_backup->phys.pos.Y = p1->phys.pos.Y;
        p1_backup->facing_direction = p1->facing_direction;
        p1_backup->coll_data.ground_index = p1->coll_data.ground_index;
        if (lg_CheckIfPlayerHasAFollower(pl.p1_gobj, &follower) != 0) {
            FighterData *follower_backup = ed->savestate.player[0].follower;
            follower_backup->phys.pos.X = follower->phys.pos.X;
            follower_backup->facing_direction = follower->facing_direction;
        }
    }
    goto check_to_reset;

check_upb_timer:
    if (v->upb_timer <= 0)
        goto check_to_reset;
    if (--v->upb_timer != 0)
        goto check_to_reset;
    /* Marth in front of P1 again */
    {
        float offset = p1->facing_direction * MARTH_DISTANCE;
        p2->phys.pos.X = p1->phys.pos.X + offset;
    }
    /* up B */
    p2->cpu.held = PAD_BUTTON_B;
    p2->cpu.lstickY = 127;
    /* the reset timer */
    switch (LG_OPTION(md, OPT_RESET_TIMER)) {
    default:
    case 0:
        v->timer = 120;
        break;
    case 1:
        v->timer = 180;
        break;
    case 2:
        v->timer = 240;
        break;
    }

check_to_reset:
    if (v->timer <= 0)
        return;
    if (--v->timer != 0)
        return;

restore_state:
    lg_SaveState_Load(&ed->savestate);
    /* the timers, just in case */
    v->timer = 0;
    v->upb_timer = 0;
}

static void AmsahTechLoad(void)
{
    lg_StartOSDs();
    lg_CreateEventThinkFunction(AmsahTechThink, 3, AmsahTechWindowInfo, AmsahTechWindowText);
}

void mu_tmce_legacy_AmsahTech(MatchInit *match, int event_id)
{
    (void) event_id;
    /* Marth on the chosen stage, no OSDs, Sopo */
    lg_InitializeMatch(lg_EventStruct(), match, LGC_MARTH_EXT, -1, 0, 1);
    match->onStartMelee = (void *) AmsahTechLoad;
}
