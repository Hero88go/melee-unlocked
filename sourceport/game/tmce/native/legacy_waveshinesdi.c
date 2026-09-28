/* Training Mode CE, native build: Waveshine SDI, an original UnclePunch event.
 *
 * Rewritten in C from ASM/training-mode/Custom Events/Custom Event Code - Rewrite.asm
 * (WaveshineSDI, WaveshineSDILoad, WaveshineSDIThink and their data). A CPU Fox drills into the
 * player on Final Destination and waveshines them across the stage; the player practices SDI out.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_c_common.h"

typedef struct WaveshineSDIVars {
    u8 first_frame_over; /* 0x0 */
    u8 x1[3];
    int timer;           /* 0x4 counts up from a random negative start */
    u8 waveshine;        /* 0x8 0 drilling, 1 start waveshining, 2 mid waveshine */
    u8 jc_frame;         /* 0x9 the shine's jump cancel frame */
    u8 xA[2];
    int reset_timer;     /* 0xC */
} WaveshineSDIVars;

/* WaveshineSDI_Floats: P1 X, P2 X, P1 Y, P2 Y (the FD floor) for InitializePositions */
static const float WaveshineSDI_Floats[4] = {-70.0f, -76.09067f, 0.0001f, 0.0001f};
/* ... +0x10 5 (the height to L cancel under), +0x14 the distance from the player to shine, +0x18 the
 * X to stop at */
#define LCANCEL_HEIGHT 5.0f
#define SHINE_DISTANCE 7.5f
#define STOP_X 80.0f

static void WaveshineSDIThink(GOBJ *gobj)
{
    LgEventData *ed = lg_EventData(gobj);
    WaveshineSDIVars *v = (WaveshineSDIVars *) ed->raw;
    LgPlayers pl;
    FighterData *p1, *p2;
    int state;

    lg_GetAllPlayerPointers(&pl);
    p1 = pl.p1;
    p2 = pl.p2;
    lg_StoreCPUTypeAndZeroInputs(p2);

    /* on the first frame */
    if (lg_CheckIfFirstFrame()) {
        v->first_frame_over = 1;
        /* the facing directions: Fox right, the player left */
        p2->facing_direction = 1.0f;
        p1->facing_direction = -1.0f;
        lg_InitializePositions(WaveshineSDI_Floats, pl.p1_gobj, pl.p2_gobj);
        lg_RemoveFirstFrameInputs();
        /* the ground as the last grounded position */
        p2->coll_data.coll_pos.Y = p2->phys.pos.Y;
        lg_SaveState_Save(&ed->savestate, 1);
        v->timer = -60;
    }

    /* the timer */
    if (++v->timer < 0)
        goto check_to_reset;

    /* at the edge of the stage: freeze Fox and reset */
    if (!(LGC_FABS(p2->phys.pos.X) < LGC_FABS(STOP_X))) {
        LGC_FT_FREEZE_SET(p2);
        if (v->reset_timer > 0)
            goto check_to_reset;
        v->reset_timer = 60;
        goto check_to_reset;
    }

    /* Fox grabbed Marth */
    if (p2->state_id == 0xD8 && v->reset_timer <= 0)
        v->reset_timer = 60;

    if (v->waveshine >= 1)
        goto waveshine_think;

    /* ---- the drill ---- */
    state = p2->state_id;
    if (state == ASID_WAIT) {
        /* jump */
        p2->cpu.held = PAD_BUTTON_X;
        goto check_to_reset;
    }
    if (state == 0x19) {
        /* down air */
        p2->cpu.cstickY = -127;
        goto check_to_reset;
    }
    if (state != 0x45)
        goto check_to_reset;
    /* drilling: drift forward, 36 * facing direction */
    p2->cpu.lstickX = 36 * (int) p2->facing_direction;
    if (!LGC_FT_FASTFALL(p2)) {
        /* fastfall once falling */
        if (p2->phys.self_vel.Y >= LGC_RTOC_0F)
            goto check_to_reset;
        p2->cpu.lstickY = -127;
    }
    /* L cancel under 5 Mm above the ground */
    {
        float height = p2->phys.pos.Y - p2->coll_data.coll_pos.Y;
        if (height > LCANCEL_HEIGHT)
            goto check_to_reset;
    }
    p2->cpu.held = 0xC0;
    /* start waveshining */
    v->waveshine = 1;
    goto check_to_reset;

    /* ---- the waveshines ---- */
waveshine_think:
    state = p2->state_id;
    if (state == ASID_WAIT) {
        /* the first shine, or follow up with another */
        if (v->waveshine == 2)
            goto follow_opponent;
        p2->cpu.lstickY = -127;
        p2->cpu.held = PAD_BUTTON_B;
        v->waveshine = 2;
        /* the jump cancel frame */
        v->jc_frame = HSD_Randi(3);
        goto check_to_reset;
    }
    /* jump out of the shine loop on the jump cancel frame */
    if (state == 0x169) {
        if (!((float) v->jc_frame == p2->state.frame))
            goto check_to_reset;
        p2->cpu.held = PAD_BUTTON_X;
        goto check_to_reset;
    }
    /* airdodge in JumpF */
    if (state == 0x19) {
        /* a random airdodge angle, 310 up to 339 degrees, along Fox's facing direction */
        int stick_x, stick_y;
        lgc_ConvertAngle(HSD_Randi(30) + 310, &stick_x, &stick_y);
        p2->cpu.lstickX = stick_x * (int) p2->facing_direction;
        p2->cpu.lstickY = stick_y;
        p2->cpu.held = 0xC0; /* L to wavedash */
        goto check_to_reset;
    }
    /* walking: follow the opponent */
    if (state >= 0xF && state <= 0x11)
        goto follow_opponent;
    /* teetering at the edge: reset */
    if (state == 0xF5)
        goto restore_state;
    goto check_to_reset;

follow_opponent:
    /* close enough to shine? */
    {
        float distance = p1->phys.pos.X - p2->phys.pos.X;
        if (distance >= LGC_RTOC_0F) {
            /* the opponent to the right */
            if (distance > SHINE_DISTANCE)
                goto walk_towards;
        } else {
            /* the opponent to the left */
            if (distance < -SHINE_DISTANCE)
                goto walk_towards;
        }
    }
    /* shine (the ASM has an unused grab path for a Marth opponent: WaveshineSDIWaveshine_
     * FollowOpponent_CheckMarth, never branched to) */
    p2->cpu.lstickY = -127;
    p2->cpu.held = PAD_BUTTON_B;
    /* the jump cancel frame */
    v->jc_frame = HSD_Randi(3);
    goto check_to_reset;

walk_towards:
    /* the stick forward */
    p2->cpu.lstickX = 127 * (int) p2->facing_direction;
    /* the previous stick X as the facing direction, so it always walks (no smash turn/dash) */
    lg_FtSetF32(p2, 0x620, p2->facing_direction);
    goto check_to_reset;

check_to_reset:
    if (v->reset_timer <= 0)
        return;
    if (--v->reset_timer != 0)
        return;

restore_state:
    /* mirrored: the facing directions and X positions of both backups turned around */
    {
        FighterData *p1_backup = ed->savestate.player[0].main;
        FighterData *p2_backup = ed->savestate.player[1].main;
        p1_backup->facing_direction = -p1_backup->facing_direction;
        p2_backup->facing_direction = -p2_backup->facing_direction;
        p1_backup->phys.pos.X = -p1_backup->phys.pos.X;
        p2_backup->phys.pos.X = -p2_backup->phys.pos.X;
    }
    lg_SaveState_Load(&ed->savestate);
    v->timer = 0 - HSD_Randi(60);
    v->waveshine = 0;
}

static void WaveshineSDILoad(void)
{
    lg_StartOSDs();
    lg_CreateEventThinkFunction(WaveshineSDIThink, 3, 0, 0);
    /* The ASM keeps a block that made Final Destination's blast zones longer inside a comment
     * (never assembled); not ported. */
}

void mu_tmce_legacy_WaveshineSDI(MatchInit *match, int event_id)
{
    (void) event_id;
    /* Fox on Final Destination, no OSDs, Sopo */
    lg_InitializeMatch(lg_EventStruct(), match, LGC_FOX_EXT, LGC_STAGE_FINAL_DESTINATION, 0, 1);
    match->onStartMelee = (void *) WaveshineSDILoad;
}
