/* Training Mode CE, native build: Slide Off, an original UnclePunch event.
 *
 * Rewritten in C from ASM/training-mode/Custom Events/Custom Event Code - Rewrite.asm (SlideOff,
 * SlideOffLoad, SlideOffThink, SlideOff_InitializePositions and their data). Marth up-throws the
 * player off Pokemon Stadium's side platform; the player practices sliding off the platform after the
 * tech or missed tech, and Marth punishes (up tilt) or shields.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_c_common.h"

typedef struct SlideOffVars {
    u8 event_state;  /* 0x0 EventState */
    u8 timer;        /* 0x1 Timer */
    u8 p1_state;     /* 0x2 P1State (never read) */
    u8 attack_timer; /* 0x3 AttackTimer */
} SlideOffVars;

enum {
    STATE_HITSTUN,
    STATE_DETERMINE_ATTACK,
    STATE_ATTACK_THINK,
    STATE_SHIELD,
};

/* constants */
#define RESET_TIMER 40
#define ANGLE_LO 83
#define ANGLE_HI 100
#define MAG_LO 65
#define MAG_HI 75
#define HITLAG_FRAMES 12
#define PERCENT_LO 40
#define PERCENT_HI 40
#define FRAMES_BEFORE_HITBOX 7

/* SlideOffThink_Constants */
static const struct {
    float p1_x, p1_y, p2_x, p2_y;   /* 0x00 */
    float uthrow_start_frame;       /* 0x10 Marth's up throw starts here */
    float damageflytop_start_frame; /* 0x14 */
    float magnitude_scalar;         /* 0x18 the baseline of the magnitude scaling */
    float magnitude_scalar2;        /* 0x1C */
    float magnitude_scalar3;        /* 0x20 */
    float trigger_box_x_min, trigger_box_x_max, trigger_box_y_min, trigger_box_y_max; /* 0x24 */
    /* 0x34 VulnFrameData: per state from DownBoundU to PassiveStandB, the frame P1 becomes
     * vulnerable */
    u8 vuln_frame_data[19];
} SlideOff_Constants = {
    -37.7f, 21.2f, -41.1f, 0.0f,
    13.0f,
    2.0f,
    0.1f,
    1.0f,
    0.4f,
    -8.0f, 20.0f, 18.0f, 30.0f,
    {
        23,          /* DownBoundU: knockdown */
        0,           /* DownWaitU */
        0,           /* DownDamageU */
        23,          /* DownStandU: getup */
        27,          /* DownAttackU: getup attack */
        20 - 1,      /* DownForwardU: getup roll forward */
        30 - 3,      /* DownBackU: getup roll backward */
        0xFF,        /* DownSpotU (unused state) */
        23,          /* DownBoundD */
        0,           /* DownWaitD */
        0,           /* DownDamageD */
        23,          /* DownStandD */
        27,          /* DownAttackD */
        20 - 1,      /* DownForwardD */
        30 - 3,      /* DownBackD */
        0xFF,        /* DownSpotD (unused state) */
        24 - 4,      /* Passive: tech */
        23,          /* PassiveStandF: tech roll */
        23,          /* PassiveStandB */
    },
};

/* SlideOffThink_AttackInputs: Marth's jump, up air and L cancel */
static const LgInputSeq SlideOff_AttackInputs[] = {
    {0, PAD_BUTTON_X, 0, 0, 0, 0},
    {4, 0, 0, 0, 0, 127},
    {26, PAD_TRIGGER_L, 0, -127, 0, 0},
    {-1, 0, 0, 0, 0, 0},
};

/* ftData's common attributes (disc data): the weight at 0x88 */
typedef struct MEX_DISC_STRUCT SlideOffFtAttributes {
    char x0[0x88];
    float weight;
} SlideOffFtAttributes;

/* SlideOff_InitializePositions: Marth up-throwing P1 from the platform's edge, P1 in knockback */
static void SlideOff_InitializePositions(LgPlayers *pl)
{
    FighterData *p1 = pl->p1;
    FighterData *p2 = pl->p2;
    float speed, weight, scaled, magnitude;
    int mag_lo, mag_hi, percent;

    /* the facing directions */
    p1->facing_direction = (float) -1;
    p2->facing_direction = (float) 1;

    /* the starting positions */
    p1->phys.pos.X = SlideOff_Constants.p1_x;
    p1->phys.pos.Y = SlideOff_Constants.p1_y;
    lg_UpdatePosition(pl->p1_gobj);
    lg_UpdateCameraBox(pl->p1_gobj);
    p2->phys.pos.X = SlideOff_Constants.p2_x;
    p2->phys.pos.Y = SlideOff_Constants.p2_y;
    lg_PlacePlayerOnGround(pl->p2_gobj);
    lg_UpdateCameraBox(pl->p2_gobj);

    /* P2 into its up throw, the animation speed from P1's weight */
    weight = ((SlideOffFtAttributes *) MEX_DP(p1->ftData->common_attr))->weight;
    scaled = weight * (*stc_ftcommon)->x37c;
    speed = LGC_RTOC_1F / scaled;
    ActionStateChange(SlideOff_Constants.uthrow_start_frame, speed, LGC_RTOC_0F, pl->p2_gobj, ASID_THROWHI, 0, 0);

    /* P1 into DamageFlyTop */
    ActionStateChange(SlideOff_Constants.damageflytop_start_frame, LGC_RTOC_1F, LGC_RTOC_0F, pl->p1_gobj,
                      ASID_DAMAGEFLYTOP, 0x40, 0);

    /* the knockback magnitude scaled by P1's gravity */
    magnitude = (float) MAG_LO;
    scaled = p1->attr.gravity / SlideOff_Constants.magnitude_scalar;
    scaled = scaled - SlideOff_Constants.magnitude_scalar2;
    scaled = scaled * SlideOff_Constants.magnitude_scalar3;
    scaled = scaled * magnitude;
    magnitude = magnitude + scaled;
    mag_lo = (int) magnitude;

    magnitude = (float) MAG_HI;
    scaled = p1->attr.gravity / SlideOff_Constants.magnitude_scalar;
    scaled = scaled - SlideOff_Constants.magnitude_scalar2;
    scaled = scaled * SlideOff_Constants.magnitude_scalar3;
    scaled = scaled * magnitude;
    magnitude = magnitude + scaled;
    mag_hi = (int) magnitude;

    lg_EnterKnockback(pl->p1_gobj, ANGLE_LO, ANGLE_HI, mag_lo, mag_hi);

    /* the hitstun, overridden */
    lg_FtSetF32(p1, 0x2340, (float) 50);

    /* 12 frames of hitlag each */
    p1->dmg.hitlag_frames = (float) HITLAG_FRAMES;
    LGC_FT_HITLAG_SET(p1);
    LGC_FT_FREEZE_SET(p1);
    p2->dmg.hitlag_frames = (float) HITLAG_FRAMES;
    LGC_FT_HITLAG_SET(p2);
    LGC_FT_FREEZE_SET(p2);

    /* a random percent */
    percent = HSD_Randi(PERCENT_HI - PERCENT_LO) + PERCENT_LO;
    Fighter_SetHUDDamage(p1->ply, percent);
}

static void SlideOffThink(GOBJ *gobj)
{
    LgEventData *ed = lg_EventData(gobj);
    SlideOffVars *v = (SlideOffVars *) ed->raw;
    LgPlayers pl;
    FighterData *p1, *p2;
    int state;

    lg_GetAllPlayerPointers(&pl);
    p1 = pl.p1;
    p2 = pl.p2;
    lg_StoreCPUTypeAndZeroInputs(p2);

    /* on the first frame */
    if (lg_CheckIfFirstFrame()) {
        SlideOff_InitializePositions(&pl);
        lg_RemoveFirstFrameInputs();
        lg_SaveState_Save(&ed->savestate, 1);
        v->event_state = STATE_HITSTUN;
    }

    /* reset if anyone died */
    if (lg_IsAnyoneDead())
        goto restore;

    /* failed the slide off: past the hitstun state, in hitstun (not hitlag) for 5 frames */
    if (v->event_state > STATE_HITSTUN && !LGC_FT_HITLAG(p1) && LGC_FT_HITSTUN(p1)
        && (u16) p1->TM.state_frame >= 5) {
        if (v->timer > 0)
            goto check_timer;
        v->timer = 10;
    }

    /* the CPU was hit */
    if (p2->state_id >= ASID_DAMAGEHI1 && p2->state_id <= ASID_DAMAGEFLYROLL && !(v->timer > 0))
        v->timer = RESET_TIMER;

    switch (v->event_state) {
    case STATE_HITSTUN:
        /* still in the up throw */
        if (p2->state_id == ASID_THROWHI)
            break;
        /* P1 in a tech or missed tech state */
        if (p1->state_id < ASID_DOWNBOUNDU || p1->state_id > ASID_PASSIVESTANDB)
            break;
        v->event_state = STATE_DETERMINE_ATTACK;
        break;

    case STATE_DETERMINE_ATTACK: {
        /* (the ASM keeps a turn-around check here inside a comment) */
        /* shield or attack: P1 in the trigger box above Marth */
        float y_min = p2->phys.pos.Y + SlideOff_Constants.trigger_box_y_min;
        float y_max = p2->phys.pos.Y + SlideOff_Constants.trigger_box_y_max;
        float p1_y = p1->phys.pos.Y;
        if (p1_y >= y_max) {
            /* above the trigger box */
            if (v->timer > 0)
                break;
            v->timer = 10;
            break;
        }
        if (!(p1_y >= y_min)) {
            /* below it: shield */
            v->event_state = STATE_SHIELD;
            v->timer = RESET_TIMER;
            p2->cpu.held = PAD_TRIGGER_R;
            break;
        }
        /* can attack: P1's state */
        state = p1->state_id;
        if (state >= ASID_DOWNBOUNDU && state <= ASID_PASSIVESTANDB) {
            /* in DownWait or a bound, nothing */
            if (state == ASID_DOWNWAITD || state == ASID_DOWNWAITU || state == ASID_DOWNBOUNDD || state == ASID_DOWNBOUNDU)
                break;
            /* the state's frame data: Marth's up tilt hitbox takes 7 frames */
            int frame = SlideOff_Constants.vuln_frame_data[state - ASID_DOWNBOUNDU] - FRAMES_BEFORE_HITBOX;
            if ((u16) p1->TM.state_frame < frame)
                break;
        }
        /* time to attack */
        v->event_state = STATE_ATTACK_THINK;
        break;
    }

    case STATE_ATTACK_THINK:
        /* this frame's inputs */
        lg_PlaybackInputSequence(p2, SlideOff_AttackInputs, v->attack_timer);
        /* always succeed the L cancel */
        lg_FtSetU8(p2, 0x67F, 0);
        /* no hitboxes 1, 2 and 3 (they get in the way of the slide off) */
        lgc_RemoveHitbox(pl.p2_gobj, 1);
        lgc_RemoveHitbox(pl.p2_gobj, 2);
        lgc_RemoveHitbox(pl.p2_gobj, 3);
        /* back to deciding once Marth returns to Wait, or can act out of Landing */
        if (v->attack_timer > 0) {
            state = p2->state_id;
            if (state == ASID_WAIT
                || (state == ASID_LANDING && p2->state_var.state_var1 != 0
                    && (u16) p2->TM.state_frame >= (int) p2->attr.normal_landing_lag)) {
                v->event_state = STATE_DETERMINE_ATTACK;
                v->attack_timer = 0;
                break;
            }
        }
        /* the attack timer, not in hitlag */
        if (LGC_FT_HITLAG(p2))
            break;
        v->attack_timer++;
        break;

    case STATE_SHIELD:
        /* hold shield */
        p2->cpu.held = PAD_TRIGGER_R;
        break;

    default:
        break;
    }

check_timer:
    if (v->timer <= 0)
        return;
    v->timer--;
    if (v->timer > 0)
        return;

restore:
    lg_SaveState_Load(&ed->savestate);
    SlideOff_InitializePositions(&pl);
    v->event_state = 0;
    v->timer = 0;
    v->p1_state = 0;
    v->attack_timer = 0;
}

static void SlideOffLoad(void)
{
    lg_StartOSDs();
    lg_CreateEventThinkFunction(SlideOffThink, 3, 0, 0);
}

void mu_tmce_legacy_SlideOff(MatchInit *match, int event_id)
{
    (void) event_id;
    /* Marth on Pokemon Stadium, no OSDs, Sopo */
    lg_InitializeMatch(lg_EventStruct(), match, LGC_MARTH_EXT, LGC_STAGE_POKEMON_STADIUM, 0, 1);
    match->onStartMelee = (void *) SlideOffLoad;
}
