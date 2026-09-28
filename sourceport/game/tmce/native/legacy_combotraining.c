/* Training Mode CE, native build: Combo Training, an original UnclePunch event.
 *
 * Rewritten in C from ASM/training-mode/Custom Events/Custom Event Code - Rewrite.asm
 * (ComboTraining, ComboTrainingLoad, ComboTrainingThink, ComboTrainingDecideStickAngle and its
 * ConvertAngle, ComboTrainingCheckForThrowAngle, ComboTrainingCheckExitStates and their data). The
 * CPU is the player's combo dummy: it DIs, SDIs, techs and acts out of hitstun as the option menu
 * says, and the combo counter is the score. StageGetGroundID_Main and its tables, in this part of the
 * ASM too, are legacy_common.c's (lg_StageGetGroundID_Main).
 *
 * ConvertAngle is also used by Attack on Shield and Waveshine SDI (legacy_c_common.h).
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_c_common.h"

typedef struct ComboTrainingVars {
    u8 x0[4];
    int timer;       /* 0x4 */
    u8 event_state;  /* 0x8, EventState */
} ComboTrainingVars;

enum {
    STATE_START,
    STATE_DI_AND_TECH,
    STATE_POST_HITSTUN,
};

/* ComboTrainingWindowInfo: 5 windows; DI 7 options, SDI 4, Tech 5, Post Hitstun Action 3, Grab
 * Mash-Out 3 */
static const u8 ComboTrainingWindowInfo[8] = {0x04, 0x06, 0x03, 0x04, 0x02, 0x02, 0x00, 0x00};
/* the SJIS full-width percent (81 93) and slash (81 5E) are kept as on the console */
static const char ComboTrainingWindowText[] = "DI Behavior\0"
                                              "Random DI\0"
                                              "Survival DI\0"
                                              "Combo DI\0"
                                              "Slight DI Random\0"
                                              "Slight DI Towards\0"
                                              "Down and Away DI\0"
                                              "No DI\0"
                                              "SDI Behavior\0"
                                              "33\x81\x93 Chance to SDI\0"
                                              "66\x81\x93 Chance to SDI\0"
                                              "Always SDI\0"
                                              "No SDI\0"
                                              "Tech Option\0"
                                              "Random\0"
                                              "Missed Tech\0"
                                              "Tech In Place\0"
                                              "Tech In\0"
                                              "Tech Away\0"
                                              "Post Hitstun Action\0"
                                              "Airdodge\x81\x5ESpotdodge\0"
                                              "Invincible\0"
                                              "Attack\0"
                                              "Grab Mash-Out\0"
                                              "Random Mash\0"
                                              "Frame Perfect\0"
                                              "No Mash-Out\0";

/* option menu entries */
#define OPT_DI 0
#define OPT_SDI 1
#define OPT_TECH 2
#define OPT_POST_HITSTUN 3
#define OPT_GRAB_MASHOUT 4

/* DIBehavior */
enum {
    DI_RANDOM,
    DI_SURVIVAL,
    DI_COMBO,
    DI_SLIGHT_RANDOM,
    DI_SLIGHT_INWARDS,
    DI_DOWN_AND_AWAY,
    DI_NONE,
};
/* SDIBehavior */
enum {
    SDI_33_PERCENT,
    SDI_66_PERCENT,
    SDI_ALWAYS,
    SDI_NONE,
};

/* ComboTrainingAttackList: per internal character ID, the attack out of hitstun, high nibble on
 * the ground and low nibble in the air (0 A, 1 forward A, 2 back A, 3 down A, 4 up A, 5 down smash,
 * 6 up B, 7 down B, anything else A). The lookup has no bound on the console: IDs 28 and up read the
 * code after the table (ComboTrainingLoadExit's restore and blr), kept here up to ID 47. */
static const u8 ComboTrainingAttackList[48] = {
    0x00, 0x70, 0x04, 0x66, 0x02, 0x66, 0x00, 0x00, 0x30, 0x00, 0x03, 0x03, 0x04, 0x66,
    0x00, 0x73, 0x30, 0x00, 0x01, 0x52, 0x00, 0x00, 0x70, 0x00, 0x66, 0x04, 0x00, 0xFF,
    /* lmw r20, 8(r1); lwz r0, 0x104(r1); addi r1, r1, 0x100; mtlr r0; blr */
    0xBA, 0x81, 0x00, 0x08, 0x80, 0x01, 0x01, 0x04, 0x38, 0x21, 0x01, 0x00, 0x7C, 0x08, 0x03, 0xA6,
    0x4E, 0x80, 0x00, 0x20,
};

/* ComboTrainingDecideStickAngle_ConvertAngle */
void lgc_ConvertAngle(int angle, int *stick_x, int *stick_y)
{
    float radians = (float) angle * LGC_RTOC_DEG_TO_RAD;
    float scale = (float) 127;
    float component;
    int x, y;

    component = cos(radians) * scale;
    x = (int) component;
    component = sin(radians) * scale;
    y = (int) component;

    /* clamp the deadzones */
    if (!(LGC_FABS((float) x) >= (float) 36))
        x = 0;
    if (!(LGC_FABS((float) y) >= (float) 36))
        y = 0;

    *stick_x = x;
    *stick_y = y;
}

/* ComboTrainingCheckForThrowAngle: P1's throw angle while the CPU is thrown, else -1 */
static int ComboTrainingCheckForThrowAngle(LgPlayers *pl)
{
    if (pl->p2->state_id < 0xEF || pl->p2->state_id > 0xF3)
        return -1;
    return LGC_FT_THROW_HITBOX0_ANGLE(pl->p1);
}

/* ComboTrainingCheckExitStates: the CPU is actionable again */
static int ComboTrainingCheckExitStates(LgPlayers *pl)
{
    FighterData *p2 = pl->p2;
    int state = p2->state_id;
    if (state == 0xE     /* Wait */
        || state == 0x1D /* Fall */
        || state == 0x1B /* double jump */
        || state == 0xFD /* CliffWait */
        || state == 0xF5 /* Teeter */
        || state == 0x1C /* JumpAerialB */)
        return 1;
    /* Landing, once it can be interrupted */
    if (state == 0x2A && p2->state.frame >= p2->attr.normal_landing_lag)
        return 1;
    /* Jigglypuff's jump */
    if (p2->kind == 0xF && state == 0x155)
        return 1;
    return 0;
}

/* The stick X, pointed with `sign` (the stored byte read back unsigned, as lbz does) */
static s8 ComboTraining_StickTimes(int sign, s8 stick)
{
    return (s8) (sign * (u8) stick);
}

/* ComboTrainingDecideStickAngle: the CPU's DI for `behavior` */
static void ComboTrainingDecideStickAngle(LgPlayers *pl, int behavior)
{
    FighterData *p2 = pl->p2;
    int angle, original, stick_x, stick_y;

    p2->cpu.lstickX = 0;
    p2->cpu.lstickY = 0;

    switch (behavior) {
    case DI_SURVIVAL:
        goto survival_di;
    case DI_COMBO:
        goto combo_di;
    case DI_SLIGHT_RANDOM:
        goto slight_di_random;
    case DI_SLIGHT_INWARDS:
        goto slight_di_inwards;
    case DI_DOWN_AND_AWAY:
        goto down_and_away_di;
    case DI_NONE:
        return;
    default:
        break;
    }

    /* random DI */
    switch (HSD_Randi(6)) {
    case 1:
        goto combo_di;
    case 2:
        goto survival_di;
    case 3:
        goto slight_di_random;
    case 4:
        goto down_and_away_di;
    case 5:
        return;
    default:
        break;
    }
    /* truly random: 36 up to 126 each way */
    p2->cpu.lstickX = HSD_Randi(127 - 36) + 36;
    if (HSD_Randi(2) != 0)
        p2->cpu.lstickX = -(u8) p2->cpu.lstickX;
    p2->cpu.lstickY = HSD_Randi(127 - 36) + 36;
    if (HSD_Randi(2) != 0)
        p2->cpu.lstickY = -(u8) p2->cpu.lstickY;
    return;

slight_di_random:
    /* 36 up to 65 each way */
    p2->cpu.lstickX = HSD_Randi(66 - 36) + 36;
    if (HSD_Randi(2) != 0)
        p2->cpu.lstickX = -(u8) p2->cpu.lstickX;
    p2->cpu.lstickY = HSD_Randi(66 - 36) + 36;
    if (HSD_Randi(2) != 0)
        p2->cpu.lstickY = -(u8) p2->cpu.lstickY;
    return;

slight_di_inwards:
    /* stick X 86 up to 104: 86-95 go in front of the player, 96-105 behind */
    {
        int x = HSD_Randi(19) + 86;
        float facing = -pl->p1->facing_direction;
        p2->cpu.lstickX = x * (int) facing;
    }
    return;

survival_di:
    angle = ComboTrainingCheckForThrowAngle(pl);
    if (angle == -1)
        angle = p2->dmg.hit_log.kb_angle;
    /* perpendicular to the knockback: the Sakurai angle is taken as 45 */
    if (angle == 0x169)
        angle = 0x2D;
    if (angle >= 90 && angle < 269) {
        angle -= 90;
        if (angle <= 0)
            angle += 360;
    } else {
        angle += 90;
        if (angle >= 360)
            angle -= 360;
    }
    lgc_ConvertAngle(angle, &stick_x, &stick_y);
    p2->cpu.lstickX = stick_x;
    p2->cpu.lstickY = stick_y;
    /* towards the player */
    p2->cpu.lstickX = ComboTraining_StickTimes(lg_GetDirectionInRelationToP1(pl->p1, pl->p2), p2->cpu.lstickX);
    return;

combo_di:
    angle = ComboTrainingCheckForThrowAngle(pl);
    if (angle == -1)
        angle = p2->dmg.hit_log.kb_angle;
    original = angle;
    if (angle == 0x169)
        angle = 0x2D;
    if (angle >= 90 && angle < 269) {
        angle += 90;
        if (angle >= 360)
            angle -= 360;
    } else {
        angle -= 90;
        if (angle <= 0)
            angle += 360;
    }
    lgc_ConvertAngle(angle, &stick_x, &stick_y);
    p2->cpu.lstickX = stick_x;
    p2->cpu.lstickY = stick_y;

    /* in a throw, always DI the direction of the angle */
    if (p2->state_id >= 0xEF && p2->state_id <= 0xF3) {
        float x = LGC_FABS((float) p2->cpu.lstickX);
        /* The ASM compares the thrown state ID (0xEF..0xF3), not the angle, with 269 here, so a
         * throw angle of 90 or more always takes the left side. */
        int left_side = original >= 90 && p2->state_id < 269;
        int toward_facing = pl->p1->facing_direction > (float) 0;
        if (left_side)
            toward_facing = !toward_facing;
        /* right side: along P1's facing direction; left side: against it */
        if (!toward_facing)
            x = -x;
        p2->cpu.lstickX = (int) x;
        return;
    }
    /* towards the player */
    p2->cpu.lstickX = ComboTraining_StickTimes(lg_GetDirectionInRelationToP1(pl->p1, pl->p2), p2->cpu.lstickX);
    return;

down_and_away_di:
    p2->cpu.lstickX = lg_GetDirectionInRelationToP1(pl->p1, pl->p2) * 89; /* away from P1 */
    p2->cpu.lstickY = -89;
    p2->cpu.cstickY = -127;
    return;
}

static void ComboTrainingThink(GOBJ *gobj)
{
    LgEventData *ed = lg_EventData(gobj);
    LgMenuData *md = ed->menu;
    ComboTrainingVars *v = (ComboTrainingVars *) ed->raw;
    LgPlayers pl;
    FighterData *p1, *p2;
    int state;

    lg_GetAllPlayerPointers(&pl);
    p1 = pl.p1;
    p2 = pl.p2;

    lg_StoreCPUTypeAndZeroInputs(p2);

    /* on the first frame */
    if (lg_CheckIfFirstFrame()) {
        lg_PlacePlayersCenterStage(lg_StageGetGroundID_Main());
        lg_RemoveFirstFrameInputs();
        lg_SaveState_Save(&ed->savestate, 1);
        /* the score count */
        KOCount_Update(lg_score.current);
    }

    /* the combo is the score */
    lg_score.current = lgc_GetComboCount(0);
    if (lg_score.current > lg_score.best)
        lg_score.best = lg_score.current;
    if (lg_score.current >= 1)
        KOCount_Update(lg_score.current);

    /* D-pad right makes a new savestate, only in the start state with the CPU in Wait */
    if ((lg_FtU32(p1, 0x668) & PAD_BUTTON_DPAD_RIGHT) == 0
        || (v->event_state == STATE_START && p2->state_id == 0xE)) {
        if (lg_CheckForSaveAndLoad(&ed->savestate) == 1) {
            /* loaded: the state and the timer start over */
            v->event_state = STATE_START;
            v->timer = 0;
        }
    }

    /* D-pad down moves the CPU in front, only in the start state */
    if (v->event_state == STATE_START)
        lg_MoveCPU(pl.p1_gobj, pl.p2_gobj, &ed->savestate);

    /* L + D-pad sets the CPU's percent */
    lg_DPadCPUPercent(&ed->savestate.player[1]);

    lg_GiveFullShields();

    /* reset if anyone dies */
    if (lg_IsAnyoneDead())
        goto restore_state;

    /* ---- was the CPU hit again ---- */
    /* not during the first 6 frames of an airdodge */
    if (p2->state_id == 0xEC && (float) 6 >= p2->state.frame)
        goto check_state;
    /* grabbed (any grab state) */
    if (p2->state_id >= 0xDF && p2->state_id <= 0xE8)
        goto change_to_di_and_tech;
    /* hit: in hitlag in a damage state */
    if (!LGC_FT_HITLAG(p2))
        goto check_state;
    if (p2->state_id < ASID_DAMAGEHI1 || p2->state_id > ASID_DAMAGEFLYROLL)
        goto check_state;
change_to_di_and_tech:
    v->event_state = STATE_DI_AND_TECH;
    goto input_di_and_tech;

check_state:
    switch (v->event_state) {
    case STATE_DI_AND_TECH:
        goto input_di_and_tech;
    case STATE_POST_HITSTUN:
        goto post_hitstun;
    default:
        goto check_to_reset;
    }

    /* ---- DI and tech ---- */
input_di_and_tech:
    if (ComboTrainingCheckExitStates(&pl))
        goto change_state_to_post_hitstun;

    state = p2->state_id;
    /* missed tech: needs to roll or attack */
    if (state == 0xB8 || state == 0xC0)
        goto missed_tech_think;
    /* still grabbed */
    if (state >= ASID_SHOULDEREDWAIT && state <= ASID_SHOULDEREDTURN)
        goto mash_out_of_grab;
    if (p1->kind == LGC_DK_INT && (state == ASID_THROWNF || state == ASID_THROWNFF))
        goto mash_out_of_grab;
    if (state >= ASID_CAPTUREKOOPA && state <= ASID_CAPTUREWAITKOOPA)
        goto mash_out_of_grab;
    if (state >= ASID_CAPTUREKOOPAAIR && state <= ASID_CAPTUREWAITKOOPAAIR)
        goto mash_out_of_grab;
    if (state >= ASID_CAPTUREPULLEDHI && state <= ASID_CAPTUREFOOT)
        goto mash_out_of_grab;
    goto decide_inputs;

mash_out_of_grab:
    switch (LG_OPTION(md, OPT_GRAB_MASHOUT)) {
    case 1:
        goto mash_analog_input; /* frame perfect */
    case 2:
        goto check_to_reset; /* no mash */
    default:
        break;
    }
    /* random mash: 0-7 nothing, 8 the button only, 9 the stick and the button */
    {
        int roll = HSD_Randi(10);
        if (roll <= 7)
            goto check_to_reset;
        if (roll == 8)
            goto mash_button_press;
    }
mash_analog_input:
    p2->cpu.lstickX = 127;
    /* the stick as pushed on this frame first */
    lg_FtSetU8(p2, 0x1A50, 0xFF);
mash_button_press:
    p2->cpu.held = PAD_BUTTON_A;
    /* the previous frame's buttons as nothing pushed */
    lg_FtSetU32(p2, 0x65C, 0);
    goto check_to_reset;

change_state_to_post_hitstun:
    v->event_state = STATE_POST_HITSTUN;
    goto post_hitstun;

decide_inputs:
    /* DI an attack on the last frame of hitlag */
    if (LGC_FT_HITLAG(p2)) {
        if (!(LGC_RTOC_1F == p2->dmg.hitlag_frames))
            goto check_to_reset;
        /* slight DI towards is never used on attacks */
        int behavior = LG_OPTION(md, OPT_DI);
        if (behavior == DI_SLIGHT_INWARDS)
            behavior = DI_RANDOM;
        ComboTrainingDecideStickAngle(&pl, behavior);
        /* SDI */
        int sdi = LG_OPTION(md, OPT_SDI);
        if (sdi == SDI_NONE)
            goto no_sdi;
        if (sdi == SDI_ALWAYS)
            goto check_to_reset;
        int roll = HSD_Randi(3);
        int chance = LG_OPTION(md, OPT_SDI) == SDI_66_PERCENT ? 1 : 0;
        if (roll <= chance)
            goto get_chance_to_tech;
    no_sdi:
        /* the stick as it already was: nothing to SDI with */
        lg_FtSetF32(p2, 0x620, lgc_CPU_JoystickXAxis_Convert(p2));
        lg_FtSetF32(p2, 0x624, lgc_CPU_JoystickYAxis_Convert(p2));
        goto get_chance_to_tech;
    }

    /* DI a throw */
    state = p2->state_id;
    if (p1->kind == LGC_DK_INT && (state == ASID_THROWNF || state == ASID_THROWNFF))
        goto check_to_jump_out_of_hitstun; /* DK's cargo throw */
    if ((state >= ASID_THROWNF && state <= ASID_THROWNLWWOMEN) || (state >= ASID_THROWNFF && state <= ASID_THROWNFLW)) {
        ComboTrainingDecideStickAngle(&pl, LG_OPTION(md, OPT_DI));
        goto check_to_reset;
    }

check_to_jump_out_of_hitstun:
    /* not thrown and not the last frame of hitlag: act out of hitstun */
    state = p2->state_id;
    if (state != 0x26 /* tumble */ && (state < 0x4B || state > 0x5B))
        goto check_to_reset;

    if (p2->phys.air_state == 0) {
        /* grounded light damage states can be left once hitstun ends */
        if (LGC_FT_HITSTUN(p2))
            goto check_to_reset;
        switch (LG_OPTION(md, OPT_POST_HITSTUN)) {
        case 0:
            /* spotdodge */
            p2->cpu.held = 0xC0;
            p2->cpu.lstickY = -127;
            goto change_state_to_post_hitstun;
        default:
            goto change_state_to_post_hitstun;
        }
    }

    /* in the air, still in hitstun */
    if (LGC_FT_HITSTUN(p2))
        goto get_chance_to_tech;
    switch (LG_OPTION(md, OPT_POST_HITSTUN)) {
    case 0:
        /* airdodge: wiggle out of hitstun */
        p2->cpu.lstickX = 127;
        /* last frame's stick was centered */
        lg_FtSetU32(p2, 0x628, 0);
        lg_FtSetU32(p2, 0x62C, 0);
        lg_FtSetU8(p2, 0x670, 255);
        v->event_state = STATE_POST_HITSTUN;
        goto post_hitstun;
    case 2:
        /* attack */
        v->event_state = STATE_POST_HITSTUN;
        goto post_hitstun;
    default:
        /* invincible (jump) */
        goto change_state_to_post_hitstun;
    }

get_chance_to_tech:
    /* the tech in aerial hitstun */
    switch (LG_OPTION(md, OPT_TECH)) {
    case 1:
        goto miss_tech;
    case 2:
    case 3:
    case 4: {
        int option = LG_OPTION(md, OPT_TECH);
        /* hold L, the tech window constantly reset */
        p2->cpu.held = 0xC0;
        lg_FtSetU8(p2, 0x680, 0);
        lg_FtSetU8(p2, 0x684, 0xFF);
        /* not in hitlag, it would change the DI */
        if (LGC_FT_HITLAG(p2))
            goto check_to_reset;
        if (option == 2)
            p2->cpu.lstickX = 0; /* in place */
        else if (option == 3)
            p2->cpu.lstickX = (lg_GetDirectionInRelationToP1(pl.p1, pl.p2) * -1) * 127; /* towards the player */
        else
            p2->cpu.lstickX = lg_GetDirectionInRelationToP1(pl.p1, pl.p2) * 127; /* away */
        goto check_to_reset;
    }
    default:
        break;
    }
    /* random tech (the original behavior): the tech window constantly reset */
    lg_FtSetU8(p2, 0x680, 1);
    lg_FtSetU8(p2, 0x684, 0xFF);
    /* not in hitlag, it would change the DI */
    if (LGC_FT_HITLAG(p2))
        goto check_to_reset;
    switch (HSD_Randi(4)) {
    default:
    case 0:
        /* in place */
        p2->cpu.lstickX = 0;
        p2->cpu.lstickY = 0;
        goto check_to_reset;
    case 1:
        p2->cpu.lstickX = -127;
        goto check_to_reset;
    case 2:
        p2->cpu.lstickX = 127;
        goto check_to_reset;
    case 3:
        goto miss_tech;
    }

miss_tech:
    p2->cpu.held = 0;
    /* fail the tech cooldown */
    lg_FtSetU8(p2, 0x680, 0xFF);
    lg_FtSetU8(p2, 0x684, 0);
    goto check_to_reset;

missed_tech_think:
    /* a getup attack, a roll either way or a neutral getup */
    switch (HSD_Randi(4)) {
    case 0:
        p2->cpu.held = PAD_BUTTON_A;
        break;
    case 1:
        p2->cpu.lstickX = -127;
        break;
    case 2:
        p2->cpu.lstickX = 127;
        break;
    default:
        p2->cpu.lstickY = 127;
        break;
    }
    goto check_to_reset;

    /* ---- post hitstun ---- */
post_hitstun:
    switch (LG_OPTION(md, OPT_POST_HITSTUN)) {
    case 0:
        goto post_hitstun_airdodge_spotdodge;
    case 2:
        goto post_hitstun_attack;
    default:
        break;
    }
    /* invincible: keep pressing jump in the air */
    if (p2->phys.air_state != 0) {
        p2->cpu.held = PAD_BUTTON_Y;
        /* no jump on the previous frame */
        lg_FtSetU32(p2, 0x668, 0);
    }
apply_invincibility:
    Fighter_ApplyIntang(pl.p2_gobj, 30);
    Fighter_ColAnim_Update(pl.p2_gobj);
    if (v->timer > 0)
        goto check_to_reset;
    v->timer = 30;
    goto check_to_reset;

post_hitstun_airdodge_spotdodge:
    /* invulnerable from the airdodge or spotdodge */
    if (p2->hurt.kind_script != 0) {
        if (v->timer > 0)
            goto check_to_reset;
        v->timer = 30;
        goto check_to_reset;
    }
    /* in an exit state, except Fall, Landing and Wait: the reset timer */
    if (ComboTrainingCheckExitStates(&pl)) {
        state = p2->state_id;
        if (state != 0x1D && state != 0x2A && state != 0xE) {
            if (v->timer > 0)
                goto check_to_reset;
            v->timer = 30;
        }
    }
    if (p2->phys.air_state != 0) {
        state = p2->state_id;
        /* DamageFall-like light damage with no hitstun left (the same interrupts as Fall) */
        if (state != 0x56) {
            /* wiggle out of damage states into Fall */
            if (state >= 0x4B && state <= 0x5B) {
                p2->cpu.lstickX = 127;
                goto check_to_reset;
            }
            if (state != 0x1D)
                goto check_to_reset;
        }
        /* airdodge */
        p2->cpu.lstickX = 0;
        p2->cpu.held = 0xC0;
        /* no buttons on the previous frame */
        lg_FtSetU32(p2, 0x65C, 0);
        goto check_to_reset;
    }
    /* grounded and actionable: spotdodge */
    state = p2->state_id;
    if (state != 0xE && state != 0xB6) {
        if (state != 0x2A)
            goto check_to_reset;
        /* Landing, once it can be interrupted */
        if (p2->state.frame < p2->attr.normal_landing_lag)
            goto check_to_reset;
    }
    lg_FtSetU32(p2, 0x65C, 0);
    p2->cpu.held = 0xC0;
    p2->cpu.lstickY = -127;
    goto check_to_reset;

post_hitstun_attack:
    /* no buttons on the previous frame */
    lg_FtSetU32(p2, 0x65C, 0);
    {
        u8 attacks = (u32) p2->kind < sizeof(ComboTrainingAttackList) ? ComboTrainingAttackList[p2->kind] : 0;
        int attack = p2->phys.air_state != 0 ? (attacks & 0xF) : ((attacks >> 4) & 0xF);
        switch (attack) {
        default:
        case 0: /* A */
            p2->cpu.held = PAD_BUTTON_A;
            break;
        case 1: /* forward A: towards the player */
            p2->cpu.lstickX = (lg_GetDirectionInRelationToP1(pl.p1, pl.p2) * -1) * 60;
            p2->cpu.held = PAD_BUTTON_A;
            break;
        case 2: /* back A: away from the player */
            p2->cpu.lstickX = lg_GetDirectionInRelationToP1(pl.p1, pl.p2) * 60;
            p2->cpu.held = PAD_BUTTON_A;
            break;
        case 3: /* down A (the ASM holds the stick up here, as for up A) */
            p2->cpu.lstickY = 60;
            p2->cpu.held = PAD_BUTTON_A;
            break;
        case 4: /* up A */
            p2->cpu.lstickY = 60;
            p2->cpu.held = PAD_BUTTON_A;
            break;
        case 5: /* down smash */
            p2->cpu.cstickY = -127;
            break;
        case 6: /* up B */
            p2->cpu.lstickY = 127;
            p2->cpu.held = PAD_BUTTON_B;
            break;
        case 7: /* down B */
            p2->cpu.lstickY = -127;
            p2->cpu.held = PAD_BUTTON_B;
            break;
        }
    }
    /* an active hitbox makes the CPU invincible */
    if (lg_CheckForActiveHitboxes(pl.p2_gobj))
        goto apply_invincibility;
    /* Fox's and Falco's grounded shine, Jigglypuff's rest */
    if (p2->kind == 0x1 || p2->kind == 0x16) {
        if (p2->state_id == 0x168)
            goto apply_invincibility;
        goto check_to_reset;
    }
    if (p2->kind == 0xF && p2->state_id == 0x171)
        goto apply_invincibility;

check_to_reset:
    if (v->timer <= 0)
        goto exit;
    if (--v->timer != 0)
        goto exit;

restore_state:
    lg_SaveState_Load(&ed->savestate);
    lg_SaveState_Load(&ed->savestate);
    v->event_state = STATE_START;

exit:
    lg_ClearToggledOptions(md);
    lg_UpdateAllGFX(pl.p2_gobj);
}

static void ComboTrainingLoad(void)
{
    lg_StartOSDs();
    lg_CreateEventThinkFunction(ComboTrainingThink, 3, ComboTrainingWindowInfo, ComboTrainingWindowText);
    lg_InitializeHighScore();
}

void mu_tmce_legacy_ComboTraining(MatchInit *match, int event_id)
{
    /* chosen CPU on the chosen stage, no OSDs, no Sopo */
    (void) event_id;
    lg_InitializeMatch(lg_EventStruct(), match, -1, -1, 0, 0);
    match->onStartMelee = (void *) ComboTrainingLoad;
}
