/* Training Mode CE, native build: Attack on Shield, an original UnclePunch event.
 *
 * Rewritten in C from ASM/training-mode/Custom Events/Custom Event Code - Rewrite.asm
 * (AttackOnShield, AttackOnShieldLoad, AttackOnShieldThink and their data). The CPU shields on Final
 * Destination and answers the player's attack on its shield with the chosen out of shield option.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_c_common.h"

typedef struct AttackOnShieldVars {
    u8 first_frame_over; /* 0x0 firstFrameFlag */
    u8 x1[3];
    int timer;           /* 0x4 timer */
} AttackOnShieldVars;

/* AttackOnShield_Floats: P1 X, P2 X, P1 Y, P2 Y (the FD floor) for InitializePositions */
static const float AttackOnShield_Floats[4] = {-20.0f, 20.0f, 0.0001f, 0.0001f};
/* ... then the word 0x14460000, read as bytes: P1's random X is 20 up to 69 */
#define P1_RAND_X_START 0x14
#define P1_RAND_X_FURTHEST 0x46

/* AttackOnShieldWindowInfo: 1 window, OoS Option has 10 options */
static const u8 AttackOnShieldWindowInfo[4] = {0x00, 0x09, 0xFF, 0xFF};
static const char AttackOnShieldWindowText[] = "OoS Option\0"
                                               "Grab\0"
                                               "Nair\0"
                                               "Up B\0"
                                               "Up Smash\0"
                                               "Shine\0"
                                               "Spotdodge\0"
                                               "Roll Away\0"
                                               "Roll Towards\0"
                                               "Wavedash Away\0"
                                               "None\0";

#define OPT_OOS 0
enum {
    OOS_GRAB,
    OOS_NAIR,
    OOS_UPB,
    OOS_UPSMASH,
    OOS_SHINE,
    OOS_SPOTDODGE,
    OOS_ROLL_AWAY,
    OOS_ROLL_TOWARDS,
    OOS_WAVEDASH,
    OOS_NONE,
};

static void AttackOnShieldThink(GOBJ *gobj)
{
    LgEventData *ed = lg_EventData(gobj);
    LgMenuData *md = ed->menu;
    AttackOnShieldVars *v = (AttackOnShieldVars *) ed->raw;
    LgPlayers pl;
    FighterData *p2;
    int option;

    lg_GetAllPlayerPointers(&pl);
    p2 = pl.p2;
    lg_StoreCPUTypeAndZeroInputs(p2);

    /* on the first frame */
    if (lg_CheckIfFirstFrame()) {
        v->first_frame_over = 1;
        lg_InitializePositions(AttackOnShield_Floats, pl.p1_gobj, pl.p2_gobj);
        lg_RemoveFirstFrameInputs();
        lg_SaveState_Save(&ed->savestate, 1);
    }

    lg_GiveFullShields();

    /* reset if anyone dies */
    if (lg_IsAnyoneDead())
        goto restore_state;

    /* an option changed: no last state, so the CPU does not act at once */
    if (LG_TOGGLED(md, OPT_OOS) != 0)
        p2->TM.state_prev[0] = 0;

    option = LG_OPTION(md, OPT_OOS);

    /* performing the out of shield option */
    if (v->timer > 0) {
        switch (option) {
        case OOS_NAIR:
            if (p2->state_id == 0x19)
                p2->cpu.held = 0x100; /* nair */
            goto check_to_reset;
        case OOS_UPB:
            if (p2->state_id == 0x18) {
                p2->cpu.lstickY = 127; /* up */
                p2->cpu.held = 0x200;  /* B */
            }
            goto check_to_reset;
        case OOS_UPSMASH:
            if (p2->state_id == 0x18) {
                p2->cpu.lstickY = 127; /* up */
                p2->cpu.held = 0x100;  /* A */
            }
            goto check_to_reset;
        case OOS_SHINE:
            if (p2->state_id == 0x19) {
                p2->cpu.lstickY = -127;
                p2->cpu.held = 0x200;
            }
            goto check_to_reset;
        case OOS_WAVEDASH:
            if (p2->state_id == 0x19) {
                /* a random airdodge angle, 310 up to 339 degrees, away from P1 */
                int stick_x, stick_y;
                lgc_ConvertAngle(HSD_Randi(30) + 310, &stick_x, &stick_y);
                p2->cpu.lstickX = lg_GetDirectionInRelationToP1(pl.p1, p2) * stick_x;
                p2->cpu.lstickY = stick_y;
                p2->cpu.held = 0xC0; /* L */
            }
            goto check_to_reset;
        default:
            break;
        }
    }

    /* shield wait: always hold L */
    p2->cpu.held = 0xC0;

    /* shield poked: in hitlag in a damage state */
    if (LGC_FT_HITLAG(p2) && p2->state_id >= 0x4B && p2->state_id <= 0x5B) {
        v->timer = 48;
        goto exit;
    }

    /* in GuardWait (0xB3) right after GuardSetOff (0xB5): the out of shield option */
    if (p2->state_id != 0xB3)
        goto check_to_reset;
    if (p2->TM.state_prev[0] != 0xB5)
        goto check_to_reset;

    switch (option) {
    default:
    case OOS_GRAB:
        p2->cpu.held = 0x1C0; /* R + A */
        break;
    case OOS_NAIR:
        p2->cpu.held = 0xCC0; /* X/Y */
        break;
    case OOS_UPB:
    case OOS_UPSMASH:
        p2->cpu.lstickY = 127; /* up */
        break;
    case OOS_SHINE:
        p2->cpu.held = 0xCC0; /* X/Y */
        break;
    case OOS_SPOTDODGE:
        p2->cpu.lstickY = -127; /* down */
        p2->cpu.held = 0xC0;    /* R */
        break;
    case OOS_ROLL_AWAY:
        p2->cpu.held = 0xC0;
        p2->cpu.lstickX = lg_GetDirectionInRelationToP1(pl.p1, p2) * 127;
        break;
    case OOS_ROLL_TOWARDS:
        p2->cpu.held = 0xC0;
        p2->cpu.lstickX = lg_GetDirectionInRelationToP1(pl.p1, p2) * -127;
        break;
    case OOS_WAVEDASH:
        p2->cpu.held = 0xCC0; /* X/Y */
        break;
    case OOS_NONE:
        goto exit;
    }
    v->timer = 48;
    goto exit;

check_to_reset:
    if (v->timer <= 0)
        goto exit;
    if (--v->timer != 0)
        goto exit;

    /* a random X for P1 */
    {
        int x = HSD_Randi(P1_RAND_X_FURTHEST - P1_RAND_X_START) + P1_RAND_X_START;
        ed->savestate.player[0].main->phys.pos.X = (float) x;
    }
    /* opposite sides of the stage */
    lg_Randomize_LeftorRightSide(1, &ed->savestate);

restore_state:
    lg_SaveState_Load(&ed->savestate);

exit:
    lg_ClearToggledOptions(md);
}

static void AttackOnShieldLoad(void)
{
    lg_StartOSDs();
    lg_CreateEventThinkFunction(AttackOnShieldThink, 3, AttackOnShieldWindowInfo, AttackOnShieldWindowText);
}

void mu_tmce_legacy_AttackOnShield(MatchInit *match, int event_id)
{
    (void) event_id;
    /* the chosen CPU on Final Destination, no OSDs, no Sopo */
    lg_InitializeMatch(lg_EventStruct(), match, -1, LGC_STAGE_FINAL_DESTINATION, 0, 0);
    match->onStartMelee = (void *) AttackOnShieldLoad;
}
