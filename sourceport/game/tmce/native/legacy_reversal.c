/* Training Mode CE, native build: Reversal Training, Custom Event Code - Rewrite.asm lines 1570-2350,
 * rewritten in C. Practice out-of-shield punishes: the CPU attacks you on a random side after a
 * random delay; the pause menu picks its attack, both facing directions and ground or platform.
 * DPad left/right moves the two closer together or further apart.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include "legacy_common.h"
#include "legacy.h"

/* Option windows (MenuData_OptionMenuMemory + 2 + w / MenuData_OptionMenuToggled + w) */
#define CPUAttack 0
#define P1FacingDirection 1
#define CPUFacingDirection 2
#define Position 3
/* Event data */
#define firstFrameFlag 0x0
#define timer 0x4
#define AerialThinkStruct 0x20

/* ReversalWindowInfo: 4 windows; CPU Attack has 14 options, the others 2 */
static const u8 reversal_window_info[8] = { 0x03, 0x0D, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00 };

/* ReversalWindowText */
static const char reversal_window_text[] =
    "CPU Attack\0"
    "Random Smash Attack\0"
    "Forward Smash\0"
    "Down Smash\0"
    "Up Smash\0"
    "Random Aerial\0"
    "Fair\0"
    "Nair\0"
    "Dair\0"
    "FTilt\0"
    "DTilt\0"
    "UTilt\0"
    "Dash Attack\0"
    "Getup Attack (Stomach)\0"
    "Getup Attack (Back)\0"
    "P1 Facing Direction\0"
    "Towards\0"
    "Away\0"
    "CP Facing Direction\0"
    "Towards\0"
    "Away\0"
    "Position\0"
    "Ground\0"
    "Platform\0";

/* Reversal_Blacklist: per internal character, FSmash USmash DSmash left out (1) */
static const u8 reversal_blacklist[27][4] = {
    { 0, 0, 0, 0 }, /* Mario */
    { 1, 0, 0, 0 }, /* Fox */
    { 0, 0, 0, 0 }, /* Captain Falcon */
    { 0, 1, 0, 0 }, /* DK */
    { 0, 0, 0, 0 }, /* Kirby */
    { 0, 1, 0, 0 }, /* Bowser */
    { 0, 0, 0, 0 }, /* Link */
    { 1, 0, 0, 0 }, /* Sheik */
    { 0, 1, 1, 0 }, /* Ness */
    { 0, 1, 0, 0 }, /* Peach */
    { 0, 0, 0, 0 }, /* Popo */
    { 0, 0, 0, 0 }, /* Nana */
    { 0, 0, 0, 0 }, /* Pikachu */
    { 0, 1, 0, 0 }, /* Samus */
    { 0, 1, 0, 0 }, /* Yoshi */
    { 0, 1, 0, 0 }, /* Jigglypuff */
    { 0, 1, 0, 0 }, /* Mewtwo */
    { 0, 0, 0, 0 }, /* Luigi */
    { 0, 1, 0, 0 }, /* Marth */
    { 0, 1, 0, 0 }, /* Zelda */
    { 0, 0, 0, 0 }, /* Young Link */
    { 0, 0, 0, 0 }, /* Doc */
    { 1, 0, 0, 0 }, /* Falco */
    { 0, 0, 0, 0 }, /* Pichu */
    { 0, 0, 1, 0 }, /* Game and Watch */
    { 0, 0, 0, 0 }, /* Ganondorf */
    { 0, 1, 0, 0 }, /* Roy */
};

/* the CPU's attack; 1 = the attack is done for this round (AerialThinkStruct) */
static void Reversal_Attack(LgEventData *ed, LgMenuData *md, GOBJ *p2_gobj, FighterData *p2)
{
    int attack = LG_OPTION(md, CPUAttack);
    int facing = (int) p2->facing_direction;
    u8 *done = &LG_EV_U8(ed, AerialThinkStruct);

    if (attack == 0 || attack > 0xD) {
        /* ReversalRandomSmashAttack: a smash the character is not blacklisted for */
        int move;
        do {
            move = HSD_Randi(3);
        } while (p2->kind >= 0 && p2->kind < 27 && reversal_blacklist[p2->kind][move] == 1);
        attack = move == 0 ? 1 : move == 1 ? 3 : 2;
    }
    switch (attack) {
    case 1: /* ReversalFSmash */
        p2->cpu.cstickX = 127 * facing;
        *done = 1;
        break;
    case 3: /* ReversalUSmash (the ASM branches away before marking it done) */
        p2->cpu.cstickY = 127;
        break;
    case 2: /* ReversalDSmash */
        p2->cpu.cstickY = -127;
        *done = 1;
        break;
    case 4: /* ReversalRandomAerial */
        lg_PerformAerialThink(p2_gobj, done, 0);
        break;
    case 5: /* ReversalNair / Fair / Dair: 1 fair, 2 nair, 3 dair */
    case 6:
    case 7:
        lg_PerformAerialThink(p2_gobj, done, attack - 4);
        break;
    case 8: /* ReversalFTilt */
        p2->cpu.lstickX = 45 * facing;
        p2->cpu.held = PAD_BUTTON_A;
        *done = 1;
        break;
    case 9: /* ReversalDTilt */
        p2->cpu.lstickY = -45;
        p2->cpu.held = PAD_BUTTON_A;
        *done = 1;
        break;
    case 10: /* ReversalUTilt */
        p2->cpu.lstickY = 45;
        p2->cpu.held = PAD_BUTTON_A;
        *done = 1;
        break;
    case 11: /* ReversalDashAttack: dash for 4 frames, then A */
        if (LG_EV_U8(ed, AerialThinkStruct + 1) == 4) {
            p2->cpu.held = PAD_BUTTON_A;
            *done = 1;
        } else {
            LG_EV_U8(ed, AerialThinkStruct + 1) += 1;
            p2->cpu.lstickY = -45;
            p2->cpu.lstickX = 127 * facing;
            *done = 0;
        }
        break;
    case 12: /* ReversalGetupAttackStomach */
        p2->cpu.held = PAD_BUTTON_A;
        *done = 1;
        break;
    case 13: /* ReversalGetupAttackBack: the state id as DownBoundU forces the back getup attack */
        p2->state_id = 183;
        Fighter_EnterDownWait(p2_gobj);
        p2->cpu.held = PAD_BUTTON_A;
        *done = 1;
        break;
    }
}

/* ReversalRemakeSavestate: players at the middle of the ground or platform, saved again */
static void Reversal_RemakeSavestate(LgEventData *ed, LgMenuData *md)
{
    int line;
    if (LG_OPTION(md, Position) != 0 && (line = lg_StageGetGroundID_Platform()) < 0x0FFF) {
        /* the platform */
    } else {
        line = lg_StageGetGroundID_Main();
    }
    lg_PlacePlayersCenterStage(line);
    lg_RemoveFirstFrameInputs();
    lg_SaveState_Save(&ed->savestate, 1);
    LG_EV_U8(ed, firstFrameFlag) = 1;
    LG_EV_S32(ed, timer) = -60;
}

/* ReversalReset: a random side for each, the chosen facing directions, load */
static void Reversal_Reset(LgEventData *ed, LgMenuData *md, GOBJ *p2_gobj, FighterData *p2)
{
    FighterData *b1 = ed->savestate.player[0].main;
    FighterData *b2 = ed->savestate.player[1].main;
    FighterData *left, *right;
    float lx, ly, rx, ry;

    /* ReversalSwap: the leftmost and rightmost saved fighters */
    if (b1->phys.pos.X > b2->phys.pos.X) {
        left = b2;
        right = b1;
    } else {
        left = b1;
        right = b2;
    }
    lx = left->phys.pos.X;
    ly = left->phys.pos.Y;
    rx = right->phys.pos.X;
    ry = right->phys.pos.Y;
    if (HSD_Randi(2) == 0) {
        /* ReversalReset_LeftSide: P1 left facing right, P2 right facing left */
        b1->facing_direction = 1.0F;
        b1->phys.pos.X = lx;
        b1->phys.pos.Y = ly;
        b2->facing_direction = -1.0F;
        b2->phys.pos.X = rx;
        b2->phys.pos.Y = ry;
    } else {
        /* ReversalReset_RightSide */
        b2->facing_direction = 1.0F;
        b2->phys.pos.X = lx;
        b2->phys.pos.Y = ly;
        b1->facing_direction = -1.0F;
        b1->phys.pos.X = rx;
        b1->phys.pos.Y = ry;
    }

    /* Adjust P1 / CPU Facing Direction Based on Preference (1 = away) */
    if (LG_OPTION(md, P1FacingDirection) == 1) {
        b1->facing_direction = -b1->facing_direction;
    }
    if (LG_OPTION(md, CPUFacingDirection) == 1) {
        b2->facing_direction = -b2->facing_direction;
    }

    /* ReversalLoadState, Reset Timer, Reset AerialThinkStruct */
    lg_SaveState_Load(&ed->savestate);
    LG_EV_S32(ed, timer) = 0 - HSD_Randi(30);
    LG_EV_U32(ed, AerialThinkStruct) = 0;

    /* Reversal_CheckEnterDownWait: the getup attacks start lying down */
    if (LG_OPTION(md, CPUAttack) == 0xD) {
        p2->state_id = 183; /* DownBoundU: forces the back getup attack */
        Fighter_EnterDownWait(p2_gobj);
    } else if (LG_OPTION(md, CPUAttack) == 0xC) {
        Fighter_EnterDownWait(p2_gobj);
    }
}

/* ReversalThink */
static void ReversalThink(GOBJ *event)
{
    LgEventData *ed = lg_EventData(event);
    LgMenuData *md = ed->menu;
    LgPlayers pl;
    FighterData *p2;
    int t;

    lg_GetAllPlayerPointers(&pl);
    p2 = pl.p2;

    /* stb 0xF, 0x1A94(r29): the byte is the CPU kind word's most significant one */
    p2->cpu.ai = (p2->cpu.ai & 0x00FFFFFF) | 0x0F000000;
    lg_StoreCPUTypeAndZeroInputs(p2);

    /* ON FIRST FRAME */
    if (lg_CheckIfFirstFrame()) {
        lg_PlacePlayersCenterStage(lg_StageGetGroundID_Main());
        lg_RemoveFirstFrameInputs();
        lg_SaveState_Save(&ed->savestate, 1);
        LG_EV_U8(ed, firstFrameFlag) = 1;
        LG_EV_S32(ed, timer) = -60;
    }

    /* ReversalThinkMain */
    lg_GiveFullShields();

    /* Reset when menu is toggled */
    if (LG_TOGGLED(md, P1FacingDirection) != 0 || LG_TOGGLED(md, CPUFacingDirection) != 0 ||
        LG_TOGGLED(md, CPUAttack) != 0)
    {
        Reversal_Reset(ed, md, pl.p2_gobj, p2);
    } else if (LG_TOGGLED(md, Position) != 0) {
        Reversal_RemakeSavestate(ed, md);
        Reversal_Reset(ed, md, pl.p2_gobj, p2);
    } else if (lg_AdjustResetDistance(&ed->savestate, pl.p1, p2) != -1) {
        /* Move Players Apart With DPad */
        Reversal_Reset(ed, md, pl.p2_gobj, p2);
    } else {
        /* ReversalThinkSequence: Increment Timer */
        t = LG_EV_S32(ed, timer) + 1;
        LG_EV_S32(ed, timer) = t;

        /* Give Invincibility in Wait, Squat Reverse, IASA Flag Flipped */
        if (p2->state_id == ASID_WAIT || p2->state_id == 0x29 || lg_FtFlag(p2, 0x2218, 0x80)) {
            lg_GiveInvincibility(pl.p2_gobj, 2);
        }

        /* ReversalCheckToAttack */
        if (t >= 45) {
            if (LG_EV_U8(ed, AerialThinkStruct) == 0) {
                Reversal_Attack(ed, md, pl.p2_gobj, p2);
            }
            /* ReversalCheckToReset: restore after 150 frames */
            if (t >= 150) {
                Reversal_Reset(ed, md, pl.p2_gobj, p2);
            }
        }
    }

    /* ReversalThinkExit */
    lg_ClearToggledOptions(md);
    lg_UpdateAllGFX(pl.p2_gobj);
}

/* ReversalLoad */
static void ReversalLoad(void)
{
    lg_StartOSDs();
    /* Schedule Think: priority 3 (after interrupt), with the option windows */
    lg_CreateEventThinkFunction(ReversalThink, 3, reversal_window_info, reversal_window_text);
}

/* Reversal (HIJACK INFO) */
void mu_tmce_legacy_Reversal(MatchInit *match, int event_id)
{
    /* Store Stage, CPU, and FDD Toggles: chosen CPU, SSS stage */
    lg_InitializeMatch(lg_EventStruct(), match, -1, -1, 0, 0);
    /* STORE THINK FUNCTION */
    match->onStartMelee = ReversalLoad;
}
