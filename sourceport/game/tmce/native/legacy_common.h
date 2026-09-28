/* Training Mode CE, native build: the shared routines of UnclePunch's original events.
 *
 * TM-CE's first fifteen events (Custom Event Code - Rewrite.asm) exist only as PowerPC. They are
 * rewritten here by hand in C, one routine per label of the ASM: lg_<AsmLabel>. This header covers the
 * helper block at the end of that file (lines 8600-12371) plus the few tables and routines both halves
 * of the event code share. Every event file includes it; legacy_common.c implements it.
 *
 * Register conventions of the ASM become parameters: the events keep P1Data in r27, P1GObj in r28,
 * P2Data in r29, P2GObj in r30 and EventData in r31 (from GetAllPlayerPointers), and several helpers
 * read those registers from their caller. Each prototype below says which registers it replaces.
 *
 * Console layouts that are data on the console and host memory here (the event GObj's data block, the
 * option menu's data block) keep their console offsets for the event's own bytes, so the ASM's
 * `stb r3, 0x9(r31)` becomes LG_EV_U8(ed, 0x9) = r3. Pointers in them are host pointers.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#ifndef TMCE_LEGACY_COMMON_H
#define TMCE_LEGACY_COMMON_H

#include "../MexTK/mex.h"
#include "../src/events.h"

/* ============================================================================================
 * Data blocks
 * ============================================================================================ */

/* SaveState struct (console EventData+0x10): per player (spawn order, not port) 8 bytes, the main
 * fighter's backup at +0 and the follower's (Nana) at +4. Each backup is a copy of the fighter's data
 * (castable to FighterData *, so backup->phys.pos, ->facing_direction, ->dmg.percent read the saved
 * values) followed by the saved part of its static player block and the camera flag. */
typedef struct LgSaveSlot {
    FighterData *main;     /* console +0 */
    FighterData *follower; /* console +4 */
} LgSaveSlot;
typedef struct LgSaveState {
    LgSaveSlot player[6];  /* console +0x8 * player */
} LgSaveState;

typedef struct LgMenuData LgMenuData;

/* The event GObj's data (CreateEventThinkFunction, console EventData_DataSize 0x50).
 * raw[] holds the event's own variables at their console offsets (0x00..0x4B). Console offsets
 * 0x10..0x3F are the SaveState struct, which lives in `savestate` instead: an event that passes
 * `addi r3, r31, 0x10` passes &ed->savestate, `addi r3, r31, 0x18` passes &ed->savestate.player[1],
 * `lwz rX, 0x10(r31)` is ed->savestate.player[0].main and `lwz rX, 0x18(r31)` is
 * ed->savestate.player[1].main. Event variables the ASM keeps inside 0x10..0x3F where no fighter
 * backup lands (Reversal's 0x20) stay in raw[]. Console 0x4C (EventData_MenuDataPointer) is `menu`. */
typedef struct LgEventData {
    u8 raw[0x4C];
    LgSaveState savestate;
    LgMenuData *menu;
} LgEventData;

/* The option menu GObj's data (console MenuData_DataSize 0x50), created when an event has an option
 * window. window_info: [0] = number of windows - 1, [1 + w] = number of options of window w - 1.
 * window_text: each window's title then its options, NUL separated (zero padding between strings
 * is skipped, as GetNextString does). */
struct LgMenuData {
    const u8 *window_info;   /* console 0x00 MenuData_WindowOptionCountPointer */
    const char *window_text; /* console 0x04 MenuData_ASCIIStructPointer */
    u8 option_memory[0x20];  /* console 0x08 MenuData_OptionMenuMemory: [0] cursor, [1] scroll,
                                [2 + w] selected option of window w */
    u8 option_toggled[0x24]; /* console 0x28 MenuData_OptionMenuToggled: [w] = 1 the frame window w changed */
    LgEventData *event_data; /* console 0x4C MenuData_EventDataPointer */
    Text *text;              /* native: the option window drawn while paused (the OSD window on the console) */
};

/* An event variable at its console offset in the event data (host byte order) */
#define LG_EV_U8(ed, off) (*(u8 *) &(ed)->raw[(off)])
#define LG_EV_S8(ed, off) (*(s8 *) &(ed)->raw[(off)])
#define LG_EV_U16(ed, off) (*(u16 *) &(ed)->raw[(off)])
#define LG_EV_S16(ed, off) (*(s16 *) &(ed)->raw[(off)])
#define LG_EV_U32(ed, off) (*(u32 *) &(ed)->raw[(off)])
#define LG_EV_S32(ed, off) (*(s32 *) &(ed)->raw[(off)])
#define LG_EV_F32(ed, off) (*(float *) &(ed)->raw[(off)])
#define LG_EV_PTR(ed, off) ((void *) &(ed)->raw[(off)])
/* An option window's selection (console MenuData_OptionMenuMemory + 2 + w) and its toggled flag
 * (console MenuData_OptionMenuToggled + w) */
#define LG_OPTION(md, w) ((md)->option_memory[2 + (w)])
#define LG_TOGGLED(md, w) ((md)->option_toggled[(w)])

/* The event's entry in GmEvent.dat's sqEventInitDataLevelTbl (the console's `lwz rX, 0x0(r29)`): disc
 * data, so big-endian with 4-byte pointer slots (read them with MEX_DP, write them with MEX_DP_SET).
 * The prologue (shim/mu_tmce_legacy.c) sets it before the event runs. */
typedef struct MEX_DISC_STRUCT LgPlayerInit { /* gm_801BAB40_src in gmevent.c, 0x1C bytes */
    s8 c_kind;       /* 0x00 external character, 0x21 = the player's choice */
    u8 slot_type;    /* 0x01 0 human, 1 CPU */
    u8 stocks;       /* 0x02 */
    u8 color;        /* 0x03 costume */
    u8 x4;           /* 0x04 spawn point */
    u8 sub_color;    /* 0x05 */
    u8 team;         /* 0x06 */
    u8 x7;           /* 0x07 voice pitch */
    u8 flags;        /* 0x08 player flags */
    u8 x9;           /* 0x09 */
    u8 cpu_level;    /* 0x0A */
    u8 pad;          /* 0x0B */
    u16 xC;          /* 0x0C starting percent */
    u16 hp;          /* 0x0E */
    float attack;    /* 0x10 */
    float defense;   /* 0x14 */
    float scale;     /* 0x18 */
} LgPlayerInit;
typedef struct MEX_DISC_STRUCT LgLevelEntry { /* struct gm_804D6900_t in gmevent.c */
    u8 kind;                      /* 0x00 (the prologue clears it: no All-Star) */
    u8 flags;                     /* 0x01 top 3 bits: number of players (0x20 = 1, 0x40 = 2) */
    u8 pad2[2];
    unsigned int x4;              /* 0x04 slot */
    unsigned int evinit;          /* 0x08 slot */
    unsigned int evbonus;         /* 0x0C slot */
    unsigned int evstage_table;   /* 0x10 slot */
    unsigned int player_init[5];  /* 0x14 slots: LgPlayerInit * (P1Struct at [0], P2Struct at [1]) */
} LgLevelEntry;
extern void *mu_tmce_legacy_level_entry;
static inline LgLevelEntry *lg_EventStruct(void) { return (LgLevelEntry *) mu_tmce_legacy_level_entry; }
static inline LgPlayerInit *lg_EventPlayer(int i) { return (LgPlayerInit *) MEX_DP(lg_EventStruct()->player_init[i]); }

/* GetAllPlayerPointers' eight return registers (r3..r10): the GObj and data of players 1-4 in spawn
 * order (Fighter_GetGObj(0..3)); 0 where absent. */
typedef struct LgPlayers {
    GOBJ *p1_gobj;
    FighterData *p1;
    GOBJ *p2_gobj;
    FighterData *p2;
    GOBJ *p3_gobj;
    FighterData *p3;
    GOBJ *p4_gobj;
    FighterData *p4;
} LgPlayers;

/* PlaybackInputSequence's records (console 9 bytes: frame, buttons word, 4 stick bytes), in host
 * layout. A table ends with frame == -1. */
typedef struct LgInputSeq {
    s8 frame;
    u32 buttons;
    s8 lstick_x, lstick_y, cstick_x, cstick_y;
} LgInputSeq;

/* The score the ASM keeps at -0x4EA8(r13) (current) and -0x4EA6(r13) (best): InitializeHighScore
 * zeroes both, the HUD KO counter shows it, Event_ExitFunction saves `best` as the event's record. */
typedef struct LgScore {
    u16 current;
    u16 best;
} LgScore;
extern LgScore lg_score;

/* ============================================================================================
 * Fighter fields by console offset
 *
 * The native fighter (struct Fighter, MexTK FighterData) keeps the console's fields at other offsets.
 * Use the FighterData twin where it names the field (->state_id, ->kind, ->ply, ->facing_direction,
 * ->phys.pos / self_vel / kb_vel / air_state, ->state.frame / rate, ->cpu.held / lstickX / lstickY /
 * cstickX / cstickY / ai, ->dmg.percent, ->shield.health, ->grab.grab_timer, ->TM.*). For the rest
 * (bitfields and fields the twin leaves out) these go through the decomp's own field names
 * (shim/mu_tmce_legacy.c). An offset they do not know reports and reads 0.
 *   flags (offset, mask as the ASM tests the byte): 0x2218..0x2222, 0x21FC, all named bits
 *   u8:  0x0C ply, 0x618 pad port, 0x619 costume, 0x670 / 0x671 / 0x672 stick/trigger timers,
 *        0x67C..0x68B, 0x1968 jumps used, 0x1A50..0x1A53
 *   u32 (int or float bits): 0x04 kind, 0x10 state, 0x2C facing, 0x80..0xB8 velocities/position,
 *        0xE0 air state, 0xE4..0xF4, 0x620..0x634 sticks, 0x65C/0x660/0x664 held buttons,
 *        0x668 pressed, 0x66C released, 0x728 collision frame id, 0x83C ground line, 0xE14 throw
 *        hitbox 0 angle,
 *        0x894 / 0x89C / 0x8A4 animation frame/rate/blend, 0x1830 percent, 0x1848 kb angle,
 *        0x195C hitlag frames, 0x1988..0x1994 intangibility, 0x1998 shield, 0x1A4C grab timer,
 *        0x1A88 CPU buttons, 0x1A94 CPU kind, 0x21FC collision bubble toggles word, 0x2340..0x2360
 *        state variables
 *   pointers: 0x0 GObj, 0x5A8, 0x60C effect, 0x890 camera box, 0x988 hitbox 0 victim 0, 0x1974 / 0x1978 items, 0x1A58 /
 *        0x1A5C grab, 0x20A0 accessory, 0x2190..0x21F8 callbacks
 * ============================================================================================ */
int mu_tmce_legacy_ft_flag(void *fp, int offset, int mask);
void mu_tmce_legacy_ft_set_flag(void *fp, int offset, int mask, int on);
int mu_tmce_legacy_ft_u8(void *fp, int offset);
void mu_tmce_legacy_ft_set_u8(void *fp, int offset, int value);
unsigned int mu_tmce_legacy_ft_u32(void *fp, int offset);
void mu_tmce_legacy_ft_set_u32(void *fp, int offset, unsigned int value);
void *mu_tmce_legacy_ft_ptr(void *fp, int offset);
void mu_tmce_legacy_ft_set_ptr(void *fp, int offset, void *value);
#define lg_FtFlag(fp, off, mask) mu_tmce_legacy_ft_flag((fp), (off), (mask))
#define lg_FtSetFlag(fp, off, mask, on) mu_tmce_legacy_ft_set_flag((fp), (off), (mask), (on))
#define lg_FtU8(fp, off) mu_tmce_legacy_ft_u8((fp), (off))
#define lg_FtSetU8(fp, off, v) mu_tmce_legacy_ft_set_u8((fp), (off), (v))
#define lg_FtU32(fp, off) mu_tmce_legacy_ft_u32((fp), (off))
#define lg_FtSetU32(fp, off, v) mu_tmce_legacy_ft_set_u32((fp), (off), (v))
#define lg_FtPtr(fp, off) mu_tmce_legacy_ft_ptr((fp), (off))
#define lg_FtSetPtr(fp, off, v) mu_tmce_legacy_ft_set_ptr((fp), (off), (v))
static inline float lg_FtF32(void *fp, int off)
{
    union { unsigned int u; float f; } v;
    v.u = mu_tmce_legacy_ft_u32(fp, off);
    return v.f;
}
static inline void lg_FtSetF32(void *fp, int off, float f)
{
    union { unsigned int u; float f; } v;
    v.f = f;
    mu_tmce_legacy_ft_set_u32(fp, off, v.u);
}

/* The static player block (console 0x80453080 + 0xE90 * slot) by console offset: 0x60 / 0x62 (u16,
 * damage shown on the HUD) and 0x0C (u8, transformed[0]). On a fighter backup, the saved copy. */
int mu_tmce_legacy_static_get(int slot, int offset);
void mu_tmce_legacy_static_set(int slot, int offset, int value);
void mu_tmce_legacy_backup_static_set(FighterData *backup, int offset, int value);

/* The current event's record in the save (console 0x8045ABF0, gm EventData): bit 0..7 of byte 0xB
 * as the ASM masks it (0x80 .. 0x01). */
int mu_tmce_legacy_event_flag(int mask);
void mu_tmce_legacy_event_set_flag(int mask, int on);

/* Game-side routines behind the helpers below (shim/mu_tmce_legacy.c, in the decomp's own types).
 * The events call the lg_ helpers, not these. */
void mu_tmce_legacy_savestate_save(void *savestate, int skip_failsafe);
void mu_tmce_legacy_savestate_load(void *savestate);
int mu_tmce_legacy_get_player(int player, int follower, void **gobj, void **data);
void *mu_tmce_legacy_follower(void *gobj, void **follower_data);
void mu_tmce_legacy_update_position(void *gobj);
void mu_tmce_legacy_update_camera_box(void *gobj);
void mu_tmce_legacy_ground_center(int line, float *x, float *y);
int mu_tmce_legacy_hitboxes_active(void *fp);
void mu_tmce_legacy_event_win(void *gobj);
void mu_tmce_legacy_set_match_end(void (*cb)(int outcome));
void mu_tmce_legacy_zebes_platform_intangible(void *map_gobj);

/* ============================================================================================
 * Tables shared by both halves
 * ============================================================================================ */
extern const unsigned char mu_tmce_legacy_P1Struct[0x1C]; /* P1Struct: disc layout (big-endian) */
extern const unsigned char mu_tmce_legacy_P2Struct[0x1C]; /* P2Struct: disc layout (big-endian) */
#define lg_P1Struct mu_tmce_legacy_P1Struct
#define lg_P2Struct mu_tmce_legacy_P2Struct
/* LedgeCliffIDs (ASM line 1060): per external stage id, high byte = left ledge line, low byte = right */
extern const u16 lg_LedgeCliffIDs[34];
/* StageGroundIDs_Main / _Platform (ASM 4950 / 4971, helper C's range): the ground line to start on,
 * per external stage id; 0xFFFF where there is none. Implemented in legacy_common.c for both halves:
 * do not define these (or StageGetGroundID_*) again in an event file. */
extern const u16 lg_StageGroundIDs_Main[34];
extern const u16 lg_StageGroundIDs_Platform[34];

/* ============================================================================================
 * Helpers, in ASM order. lg_<AsmLabel>; branch labels inside a routine are listed with it.
 * ============================================================================================ */

/* CreateEventThinkFunction (+ _NoOptionMenu, _Exit): creates the event GObj (think `think` at
 * `priority`, a zeroed LgEventData as user data) and, if window_info != 0, the option menu GObj
 * (OptionMenuThink, LgMenuData linked both ways); then DisableHazards. Also restarts the frame counter
 * CheckIfFirstFrame reads. r3 think, r4 priority, r5 window info, r6 window text. Returns the event GObj. */
GOBJ *lg_CreateEventThinkFunction(void (*think)(GOBJ *), int priority, const u8 *window_info, const char *window_text);
/* OptionMenuThink (+ _CheckInputs, _Exit): the option menu GObj's think: while paused, runs
 * OptionWindow and records which window changed in option_toggled. */
void lg_OptionMenuThink(GOBJ *menu_gobj);
/* ClearToggledOptions (+ _Loop, _Exit): r3 menu data; zeroes option_toggled[0..window_info[0]]. */
void lg_ClearToggledOptions(LgMenuData *md);
/* SaveState_Save (+ _OnDeathCheck*, _SaveLoop*, _Save_*, Savestate_Save_*): r3 savestate, r4 skip the
 * failsafe (0 = refuse, with the error sound, while anyone holds an item or has an on-death callback). */
void lg_SaveState_Save(LgSaveState *ss, int skip_failsafe);
/* SaveState_Load (+ _LoadLoop*, _Load_*, Savestate_Load_*, Savestate_RestoreCameraFlag,
 * SaveState_RELOAD_PERCENT_HUDS_*, SaveState_REMAKE_PERCENT, SaveState_HUD_End, _LoadEnd, _LoadExit).
 * The ASM's SaveState_Load_RemoveAllGFX* block is commented out there and is not ported. */
void lg_SaveState_Load(LgSaveState *ss);
/* SaveState_GetPlayerDataPointer (+ branch labels): r3 player (spawn order), r4 0 main / 1 follower.
 * Returns the player's slot (r3) or 0xFF if absent; *gobj = r4, *data = r5 (either may be 0). */
int lg_SaveState_GetPlayerDataPointer(int player, int follower, GOBJ **gobj, FighterData **data);
/* CheckForSaveAndLoad (+ branch labels): r3 savestate. DPad right saves, DPad left loads (twice), with
 * nothing else held, for any port with a fighter. Returns 0 saved, 1 loaded, -1 neither (the ASM
 * leaves a stale r3 there). */
int lg_CheckForSaveAndLoad(LgSaveState *ss);
/* GiveFullShields (+ branch labels): every fighter's shield to full. */
void lg_GiveFullShields(void);
/* UpdateAllGFX (+ _Exit): steps the colour animation of the fighter in r30 (P2GObj) and its follower. */
void lg_UpdateAllGFX(GOBJ *p2_gobj);
/* GiveInvincibility (+ _Exit): r3 fighter, r4 frames; the follower too. */
void lg_GiveInvincibility(GOBJ *gobj, int frames);
/* StoreCPUTypeAndZeroInputs: clears the CPU inputs of the fighter in r29 (P2Data). */
void lg_StoreCPUTypeAndZeroInputs(FighterData *p2);
/* ClearNanaInputs (+ _P2, _Exit): clears the CPU inputs of the followers of r28 (P1GObj) and r30 (P2GObj). */
void lg_ClearNanaInputs(GOBJ *p1_gobj, GOBJ *p2_gobj);
/* CheckIfFirstFrame (+ _False, _Exit): 1 on the event's first frame (TM_GameFrameCounter == 1). */
int lg_CheckIfFirstFrame(void);
/* CurrentInputsAsLastFramesInputs (+ _Exit): stores port 0's held buttons as the last frame's buttons
 * of the fighter in r27 (P1Data). */
void lg_CurrentInputsAsLastFramesInputs(FighterData *p1);
/* CPUActions_MultiShine, Multishine_StartShine, _JumpCancelShine, _ShineAir, CPUActions_MultiShine_Exit:
 * inside a block comment in the ASM (never assembled); not ported. */
/* RAndDPadChangesEventOption / OptionWindow (+ _CursorUp/_CursorDown/_CheckDPadLeftAndRight/_Increment/
 * _Decrement/_PlayScrollSFX/_DisplayWindow*, _DisplayDownArrow, _DisplayArrowExit, _Floats, _UpArrowText,
 * _DownArrowText, _Exit): the pausing player's DPad moves the cursor and changes options; draws the
 * window. r3 option memory, r4 window info, r5 window text. Returns 1 if an option changed (r3), and
 * that window in *toggled_window (r4, -1 otherwise). menu: where the window's text object is kept. */
int lg_OptionWindow(u8 *option_memory, const u8 *window_info, const char *window_text, int *toggled_window, LgMenuData *menu);
#define lg_RAndDPadChangesEventOption lg_OptionWindow
/* RAndDPadChangesEventOption_GetOptionASCII (+ branch labels): r3 text, r4 window, r5 selection, with
 * the window info of the caller. Returns the selection's string (r4) and the window's title (r3). */
const char *lg_RAndDPadChangesEventOption_GetOptionASCII(const u8 *window_info, const char *window_text, int window,
                                                         int selection, const char **title);
/* RAndDPadChangesEventOption_GetNextString (+ _Loop): the next string after s (skips its NUL padding). */
const char *lg_RAndDPadChangesEventOption_GetNextString(const char *s);
/* DPadCPUPercent (+ branch labels): r3 = the CPU's save slot (EventData + 0x10 + 8). L held with DPad
 * changes P2's percent (by 1 / 10, 0..999) live and in the savestate. */
void lg_DPadCPUPercent(LgSaveSlot *cpu_slot);
/* InitializePositions (+ _MoveP2, _Exit): r3 = {P1 X, P2 X, P1 Y, P2 Y}; P1 (r27/r28) and P2 (r29/r30)
 * and their followers enter Wait there; then ClearNanaInputs and CurrentInputsAsLastFramesInputs. */
void lg_InitializePositions(const float *xy, GOBJ *p1_gobj, GOBJ *p2_gobj);
/* CheckIfPlayerHasAFollower (+ _NoFollower, _Exit): r3 fighter. Returns the follower's GObj (r3) and
 * data (r4, in *follower_data if not 0), or 0. */
GOBJ *lg_CheckIfPlayerHasAFollower(GOBJ *gobj, FighterData **follower_data);
/* Randomize_LeftorRightSide (+ _LeftSide, _RightSide, _CheckForFollowers, branch labels): r3 0 = same
 * side, 1 = opposing sides; places the saved P1/P2 (EventData+0x10 / +0x18, r31) on a random side. */
void lg_Randomize_LeftorRightSide(int opposing, LgSaveState *ss);
float lg_Randomize_AlwaysPositive(float f); /* Randomize_AlwaysPositive: |f| */
float lg_Randomize_AlwaysNegative(float f); /* Randomize_AlwaysNegative: -|f| */
/* IntToFloat: r3 -> f1 */
static inline float lg_IntToFloat(int i) { return (float) i; }
/* GetDirectionInRelationToP1: r27 P1Data, r29 P2Data; -1 if P2 is left of P1, else 1. */
int lg_GetDirectionInRelationToP1(FighterData *p1, FighterData *p2);
/* IsAnyoneDead (+ branch labels): 1 if any main fighter is dead. */
int lg_IsAnyoneDead(void);
/* ResetStaleMoves (+ branch labels): clears every fighter's stale move table. */
void lg_ResetStaleMoves(void);
/* MoveCPU (+ _NoFollower, _NoGroundFound, MoveCPUNoSubchar, MoveCPUExit): r3 P1GObj, r4 P2GObj,
 * r5 savestate. DPad down alone with P1 grounded in Wait puts P2 in front of P1 and saves. */
void lg_MoveCPU(GOBJ *p1_gobj, GOBJ *p2_gobj, LgSaveState *ss);
/* AdjustResetDistance (+ _CheckRightDPad, _CheckLeftDPad, _NoPress, _WasPressed, _Exit): r3 savestate,
 * with r27 (P1Data) and r29 (P2Data). DPad right/left moves the saved P1 and P2 apart/together (not
 * closer than 10). Returns -1 if nothing was pressed, 1 if it moved them. */
int lg_AdjustResetDistance(LgSaveState *ss, FighterData *p1, FighterData *p2);
/* CheckForActiveHitboxes (+ branch labels): r3 fighter; 1 if any of its 4 hitboxes is active. */
int lg_CheckForActiveHitboxes(GOBJ *gobj);
/* Event_ExitFunction (+ _SaveScore, _Exit): the match-end callback InitializeHighScore installs;
 * saves lg_score.best as the event's record. */
void lg_Event_ExitFunction(int outcome);
/* InitializeHighScore: creates the HUD KO counter, zeroes lg_score, installs Event_ExitFunction. */
void lg_InitializeHighScore(void);
/* PerformAerialThink (+ _DuringWait/_GetRandomAttack/_CheckForValidAttack/_GetRandomFrame/_DuringJump/
 * _Fair/_Nair/_Dair/_DuringAttack/_DuringLanding/_InputFastfallAndLCancel/_InputLCancel/_Exit,
 * PerformAerial_FrameData): r3 CPU fighter, r4 3 bytes of state (done, attack, frame), r5 attack
 * (0 random, 1 fair, 2 nair, 3 dair). Jumps, attacks on a random frame, fastfalls, L-cancels. */
void lg_PerformAerialThink(GOBJ *cpu, u8 *vars, int attack);
/* RandFloat: r3 lower, r4 upper; HSD_Randi(upper - lower) + lower + HSD_Randf() */
float lg_RandFloat(int lower, int upper);
/* Custom_InterruptRebirthWait (+ _CheckJoystickDown, _EnterFall, _Exit): a RebirthWait interrupt
 * callback: aerial jump, or a fresh stick push sideways/down enters Fall. */
void lg_Custom_InterruptRebirthWait(GOBJ *gobj);
/* UpdatePosition: r3 fighter; copies its position into its collision positions, model and static block. */
void lg_UpdatePosition(GOBJ *gobj);
/* GetGroundCenter: r3 ground line; the middle of the line (f1, f2). */
void lg_GetGroundCenter(int line, float *x, float *y);
/* PlacePlayersCenterStage (+ _Loop, _IncLoop, _Exit): r3 ground line (from StageGetGroundID_*); every
 * fighter to that line's middle, P1 6 left facing right, P2 6 right facing left, grounded in Wait. */
void lg_PlacePlayersCenterStage(int line);
/* PlacePlayersCenterStage_DoStuff (+ _SkipGroundCorrection, _Exit): r3 fighter, r22 ground line. */
void lg_PlacePlayersCenterStage_DoStuff(GOBJ *gobj, int line);
extern const s8 lg_PlacePlayersCenterStage_Constants[4]; /* {P1 facing, P1 X offset, P2 facing, P2 X offset} */
/* FindGroundNearPlayer (+ _CoordinatesPassedIn, _GObjPassedIn, _Continue, _Exit): r3 fighter or 0,
 * f1/f2 coordinates when r3 is 0. Returns 1 if ground was found (r3), with its point (f1, f2) and line
 * (r4) in the out parameters. */
int lg_FindGroundNearPlayer(GOBJ *gobj, float x, float y, float *out_x, float *out_y, int *out_line);
/* FindGroundUnderCoordinate (+ branch labels): f1, f2; ground within 1000 below the point. */
int lg_FindGroundUnderCoordinate(float x, float y, float *out_x, float *out_y, int *out_line);
/* PlacePlayerOnGround (+ _SkipGroundCorrection, _Exit): r3 fighter onto the ground below it, grounded. */
void lg_PlacePlayerOnGround(GOBJ *gobj);
/* PlaySFX: r3 sound id at full volume. */
void lg_PlaySFX(int sfx);
/* UpdateCameraBox: r3 fighter; moves its camera box to it at once and corrects the camera. */
void lg_UpdateCameraBox(GOBJ *gobj);
/* GetAllPlayerPointers (+ branch labels) */
void lg_GetAllPlayerPointers(LgPlayers *out);
/* RemoveFirstFrameInputs (+ branch labels): every fighter takes its current buttons as already held. */
void lg_RemoveFirstFrameInputs(void);
/* InitializeMatch (+ _SkipOSDOverride, _StoreCSSCPU, _StoreStage, _StoreSSSStage, _StoreStage_End,
 * _SwapInSopo, _SwapInSopo_End, _Exit): r3 level entry (lg_EventStruct()), r4 match, r5 CPU external
 * id or -1 for the CSS's, r6 stage external id or -1 for the SSS's, r7 OSD bits to force on, r8 1 = Ice
 * Climbers play as Popo alone. Makes the event a 2 player match with P2Struct. */
void lg_InitializeMatch(LgLevelEntry *ev, MatchInit *match, int cpu, int stage, int osds, int use_sopo);
/* GetDistance: r3, r4 point at X,Y pairs (e.g. &fp->phys.pos); the distance (the ASM's frsqrte
 * estimate times the squared distance, reproduced). */
float lg_GetDistance(const Vec3 *a, const Vec3 *b);
/* GetLedgeCoordinates (+ _GetRightLedgeID, _GetLeftLedgeID, _Exit): r3 0 left / 1 right, r4 out. */
void lg_GetLedgeCoordinates(int side, Vec3 *out);
/* EnterKnockback (+ _Exit): r3 fighter, r4/r5 angle range (degrees), r6/r7 magnitude range; gives
 * it knockback velocity and hitstun (0.4 * magnitude) along a random angle and magnitude. */
void lg_EnterKnockback(GOBJ *gobj, int angle_lo, int angle_hi, int mag_lo, int mag_hi);
/* DisableHazards (+ _SkipList, _SkipList_Exit, and the per stage labels _Dummy .. _FinalDestination,
 * _Story, _Pstadium, _OldDL, _OldYS, _OldKongo): removes the hazards of the current stage. */
void lg_DisableHazards(void);
/* DisableHazards_RagdollFix (+ _Exit): re-schedules the wind decay a removed stage think ran. */
void lg_DisableHazards_RagdollFix(void);
void lg_DisableHazards_RagdollFix_Think(GOBJ *gobj);
/* PlaybackInputSequence (+ _Loop, _PlayInput, PlaybackInputSequenceExit): r3 CPU data, r4 table, r5
 * frame; plays the table's input for this frame into the CPU inputs. */
void lg_PlaybackInputSequence(FighterData *cpu, const LgInputSeq *seq, int frame);
/* PlaceOnLedge (+ _GetRightLedgeID, _GetLeftLedgeID, _StoreLedgeIDAndPosition): r3 fighter, r4 0 left /
 * 1 right; hangs it on that ledge in CliffWait with ledge intangibility. */
void lg_PlaceOnLedge(GOBJ *gobj, int side);
/* GetInputStruct: r3 port -> its engine pad status (0x804C21CC + 68 * port) */
static inline HSD_Pad *lg_GetInputStruct(int port) { return PadGetEngine(port); }

/* ---- shared routines outside the helper block ---- */
/* StageGetGroundID_Main / _Platform (ASM 4930 / 4940): the start ground line for the current stage */
int lg_StageGetGroundID_Main(void);
int lg_StageGetGroundID_Platform(void);
/* Event_EnterGrab (ASM 1470, SDI Training): r3 = {P1 X, P1 Y, P2 X, P2 Y}; P1 (r27/r28) and P2
 * (r29/r30) and followers moved there, P2 grabs P1 and holds it (CatchWait), grounded. */
void lg_Event_EnterGrab(const float *xy, GOBJ *p1_gobj, GOBJ *p2_gobj);
/* rtocbl TM_StartOSDs (each event's Load function) */
void lg_StartOSDs(void);
/* branchl EventMatch_OnWinCondition (0x801BC4F4): ends the event with its result and destroys r3 */
void lg_EventMatch_OnWinCondition(GOBJ *event_gobj);
/* The event's think data: the event GObj's user data */
static inline LgEventData *lg_EventData(GOBJ *event_gobj) { return (LgEventData *) event_gobj->userdata; }

#endif
