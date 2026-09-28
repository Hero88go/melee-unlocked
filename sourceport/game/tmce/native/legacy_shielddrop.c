/* Training Mode CE, native build: Shield Drop, an original UnclePunch event.
 *
 * Rewritten in C from ASM/training-mode/Custom Events/Custom Event Code - Rewrite.asm (ShieldDrop,
 * ShieldDropLoad, ShieldDropThink and their data). A CPU on the other side of Battlefield jumps and
 * does a random aerial on the player's shield from the top platform; the player practices shield
 * dropping through the platform to punish.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_c_common.h"

/* the event's variables in its data (LgEventData.raw, console offsets) */
typedef struct ShieldDropVars {
    u8 first_frame_over; /* 0x0 */
    u8 x1[3];
    int timer;           /* 0x4 */
    u8 aerial[4];        /* 0x8 AerialThinkStruct: PerformAerialThink's state */
} ShieldDropVars;

/* ShieldDrop_Floats: P1 X, P2 X, P1 Y, P2 Y (the top platform) for InitializePositions; then the Y
 * velocity to attack (0.3) and the distance from the ground to L cancel (5), not read here */
static const float ShieldDrop_Floats[6] = {-7.8f, 7.8f, 54.4001f, 54.4001f, 0.3f, 5.0f};

/* ShieldDropWindowInfo: 1 window, Facing Direction has 2 options */
static const u8 ShieldDropWindowInfo[4] = {0x00, 0x01, 0xFF, 0xFF};
static const char ShieldDropWindowText[] = "Facing Direction\0"
                                           "Towards\0"
                                           "Away\0";

/* option windows */
#define OPT_FACING_DIRECTION 0

static void ShieldDropThink(GOBJ *gobj)
{
    LgEventData *ed = lg_EventData(gobj);
    LgMenuData *md = ed->menu;
    ShieldDropVars *v = (ShieldDropVars *) ed->raw;
    LgPlayers pl;
    int timer;

    lg_GetAllPlayerPointers(&pl);
    lg_StoreCPUTypeAndZeroInputs(pl.p2);

    /* on the first frame */
    if (lg_CheckIfFirstFrame()) {
        v->first_frame_over = 1;
        lg_InitializePositions(ShieldDrop_Floats, pl.p1_gobj, pl.p2_gobj);
        lg_RemoveFirstFrameInputs();
        /* its Y position as the last grounded Y position */
        pl.p2->coll_data.coll_pos.Y = pl.p2->phys.pos.Y;
        lg_SaveState_Save(&ed->savestate, 1);
        v->timer = -60;
    }

    lg_GiveFullShields();

    if (LG_TOGGLED(md, OPT_FACING_DIRECTION) != 0)
        goto reset;
    /* the D-pad moves the players apart */
    if (lg_AdjustResetDistance(&ed->savestate, pl.p1, pl.p2) != -1)
        goto reset;

    timer = ++v->timer;

    /* intangible in Wait */
    if (pl.p2->state_id == ASID_WAIT)
        lg_GiveInvincibility(pl.p2_gobj, 2);

    if (timer < 40)
        goto exit;

    /* a random aerial */
    lg_PerformAerialThink(pl.p2_gobj, v->aerial, 0);
    /* then hold shield */
    if (v->aerial[0] != 0)
        pl.p2->cpu.held = 0xC0;

    if (timer != 140)
        goto exit;

reset:
    /* opposite sides of the stage */
    lg_Randomize_LeftorRightSide(1, &ed->savestate);
    /* facing away: P1's backup turned around */
    if (LG_OPTION(md, OPT_FACING_DIRECTION) == 1) {
        FighterData *p1_backup = ed->savestate.player[0].main;
        p1_backup->facing_direction = -p1_backup->facing_direction;
    }
    /* The ASM backs up P1's analog stick (0x620/0x624) around the load and then loads the same
     * registers again instead of storing them back, so the load keeps the saved stick. */
    lg_SaveState_Load(&ed->savestate);
    v->timer = 0 - HSD_Randi(50);
    memset(v->aerial, 0, sizeof(v->aerial));

exit:
    lg_ClearToggledOptions(md);
    lg_UpdateAllGFX(pl.p2_gobj);
}

/* on match load (MatchInit.onStartMelee) */
static void ShieldDropLoad(void)
{
    lg_StartOSDs();
    lg_CreateEventThinkFunction(ShieldDropThink, 3, ShieldDropWindowInfo, ShieldDropWindowText);
}

void mu_tmce_legacy_ShieldDrop(MatchInit *match, int event_id)
{
    (void) event_id;
    /* the chosen CPU on Battlefield, no OSDs, no Sopo */
    lg_InitializeMatch(lg_EventStruct(), match, -1, LGC_STAGE_BATTLEFIELD, 0, 0);
    match->onStartMelee = (void *) ShieldDropLoad;
}
