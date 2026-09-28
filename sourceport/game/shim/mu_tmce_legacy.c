/* Training Mode CE, native build: UnclePunch's original events (no event file), game side.
 *
 * On the console TM-CE patches onEnterVs at 0x801BB128 (Custom Event Code - Rewrite.asm): for an
 * event with no event file it runs the LegacyEvent prologue (lines 1-134), which sets the event's
 * GmEvent.dat level entry and the StartMeleeData up, then branches through EventJumpTable into the
 * event's own code; afterwards the vanilla onEnterVs carries on with the level entry it changed. Here
 * mu_tmce_legacy_enter_vs is that prologue, in the decomp's own types, and mu_tmce_legacy_run
 * (tmce/native/legacy_dispatch.c) is the jump into the event, which is TM-CE's module code.
 *
 * The rest of this file gives that module (written against the MexTK twins) the parts of the game it
 * reaches by console offset where the decomp's names say it best: fighter fields and bits, the static
 * player blocks, the savestate that copies whole fighters, and a few routines of gmevent.c that are
 * private to it there. All of it is called only from the legacy events.
 *
 * TM-CE: Training Mode - Community Edition, by UnclePunch and the TM-CE contributors. */
#include <dolphin/os.h>
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ft_0D4D.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/types.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmscdata.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/gr/ground.h>
#include <melee/gr/inlines.h>
#include <melee/gr/types.h>
#include <melee/if/ifstatus.h>
#include <melee/if/types.h>
#include <melee/it/inlines.h>
#include <melee/it/types.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/types.h>
#include <melee/mn/types.h>
#include <melee/mp/mpcoll.h>
#include <melee/mp/mplib.h>
#include <melee/mp/types.h>
#include <melee/pl/plbonuslib.h>
#include <melee/pl/player.h>
#include <melee/sfx/crowdsfx.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>

#include "mu_disc.h"
#include "mu_native.h"

/* ---- TM-CE's side (tmce/native, the eventMenu module) ---- */
int mu_tmce_legacy_run(int jump_index, void* md, int event_id);
int tmce_eventMenu_GetJumpTableOffset(int page, int event);

/* ============================================================================================
 * GmEvent.dat's level table (sqEventInitDataLevelTbl): disc data, big-endian, 4-byte pointer slots.
 * The same records as gmevent.c's gm_804D6900_t / gm_801BAB40_src.
 * ============================================================================================ */
typedef struct DISC_STRUCT LegacyPlayerInit {
    /* 0x00 */ s8 c_kind;
    /* 0x01 */ u8 slot_type;
    /* 0x02 */ u8 stocks;
    /* 0x03 */ u8 color;
    /* 0x04 */ u8 x5;
    /* 0x05 */ u8 sub_color;
    /* 0x06 */ u8 team;
    /* 0x07 */ u8 xB;
    /* 0x08 */ u8 flags;
    /* 0x09 */ u8 xE;
    /* 0x0A */ u8 cpu_level;
    /* 0x0B */ u8 pad;
    /* 0x0C */ u16 x12;
    /* 0x0E */ u16 hp;
    /* 0x10 */ f32 x18;
    /* 0x14 */ f32 x1C;
    /* 0x18 */ f32 x20;
} LegacyPlayerInit;
_Static_assert(sizeof(LegacyPlayerInit) == 0x1C, "GmEvent player record");

typedef struct DISC_STRUCT LegacyLevel {
    /* 0x00 */ u8 kind;
    /* 0x01 */ u8 flags; ///< top 3 bits: number of players
    /* 0x02 */ u8 pad2[2];
    /* 0x04 */ DISC_PTR(void) x4;
    /* 0x08 */ DISC_PTR(void) evinit;
    /* 0x0C */ DISC_PTR(void) evbonus;
    /* 0x10 */ DISC_PTR(void) evstage_table;
    /* 0x14 */ DISC_PTR(LegacyPlayerInit) player_init[6]; ///< gmevent.c declares 5; the ASM clears 6
} LegacyLevel;
_Static_assert(sizeof(LegacyLevel) == 0x2C, "GmEvent level entry");
typedef DISC_PTR(LegacyLevel) LegacyLevelSlot;

/* P1Struct / P2Struct (ASM 8600 / 8614): player records in the disc's layout, which the prologue and
 * InitializeMatch point the level entry at. External character, player type, stocks, costume; spawn
 * point, sub colour, team, voice pitch; player flags; level and starting percent; attack, defense
 * and model scale ratios. */
const unsigned char mu_tmce_legacy_P1Struct[0x1C] __attribute__((aligned(4))) = {
    0x01, 0x00, 0x02, 0x00, 0xFF, 0x00, 0x04, 0x00, 0x00, 0x04, 0x07, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00,
};
const unsigned char mu_tmce_legacy_P2Struct[0x1C] __attribute__((aligned(4))) = {
    0x01, 0x01, 0x02, 0x00, 0xFF, 0x00, 0x04, 0x00, 0x00, 0x04, 0x07, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00,
};

/* the level entry of the event being set up (the console's `lwz rX, 0x0(r29)`) */
void* mu_tmce_legacy_level_entry;

/* ============================================================================================
 * LegacyEvent (ASM lines 21-134)
 *
 * md: the StartMeleeData onEnterVs fills (r26). page: the memcard's TM-CE event page. event_id: the
 * event on that page (r25). level_slot (r29): the address of this level's 4-byte big-endian pointer
 * slot in sqEventInitDataLevelTbl, i.e. (char*) gm_804D6900[0] + 4 * level on the disc's layout.
 * ============================================================================================ */
void mu_tmce_legacy_enter_vs(StartMeleeData* md, int page, int event_id, struct gm_804D6900_t** level_slot)
{
    LegacyLevel* ev = DP(*(LegacyLevelSlot*) level_slot);
    CSSData* css;
    int i;

    mu_tmce_legacy_level_entry = ev;

    /* 1 PLAYER, NO ITEMS, TIME COUNTING UP: zero out the P2-P6 records (0x18..0x28) */
    for (i = 1; i < 6; i++) {
        DISC_SET(ev->player_init[i], NULL);
    }

    /* Disable All-Star Flag */
    ev->kind = 0;

    /* P1 = Choose Char + Normal Modifiers */
    DISC_SET(ev->player_init[0], (LegacyPlayerInit*) mu_tmce_legacy_P1Struct);

    /* STORE MATCH SETTINGS: HUD and timer behavior (0x0BB0027C), think functions (0x90800000) */
    md->rules.match_kind = 0;
    md->rules.x0_3 = 2;
    md->rules.timer_enabled = 1;
    md->rules.timer_counts_up = 1;
    md->rules.x1_0 = 1;
    md->rules.x1_1 = 0;
    md->rules.x1_2 = 1;
    md->rules.x1_3 = 1;
    md->rules.x1_4 = 0;
    md->rules.x1_5 = 0;
    md->rules.timer_shows_hours = 0;
    md->rules.friendly_fire = 0;
    md->rules.is_stock = 0;
    md->rules.x2_1 = 0;
    md->rules.x2_2 = 0;
    md->rules.single_button = 0;
    md->rules.disable_pausing = 0;
    md->rules.x2_5 = 0;
    md->rules.x2_6 = 1;
    md->rules.x2_7 = 0;
    md->rules.x3_0 = 0;
    md->rules.x3_1 = 1;
    md->rules.x3_2 = 1;
    md->rules.x3_3 = 1;
    md->rules.x3_4 = 1;
    md->rules.x3_5 = 1;
    md->rules.x3_6 = 0;
    md->rules.x3_7 = 0;
    md->rules.x4_0 = 1;
    md->rules.is_vs = 0;
    md->rules.x4_2 = 0;
    md->rules.x4_3 = 1;
    md->rules.x4_4 = 0;
    md->rules.x4_5 = 0;
    md->rules.x4_6 = 0;
    md->rules.x4_7 = 0;
    md->rules.x5_0 = 1;
    md->rules.x5_1 = 0;
    md->rules.x5_2 = 0;
    md->rules.x5_3 = 0;
    md->rules.x5_4 = 0;
    md->rules.x5_5 = 0;
    md->rules.x5_6 = 0;
    md->rules.x5_7 = 0;
    md->rules.x6 = 0;
    md->rules.x7 = 0;
    md->rules.item_freq = -1; /* items to none */
    md->rules.time_limit = 0;

    /* STORE UNLIM STOCKS */
    md->players[0].stocks = -1;

    /* SET FALL FLAG: the whole flag byte at players[0] + 0xC */
    md->players[0].rumble_enabled = 0;
    md->players[0].xC_b1 = 0;
    md->players[0].vs_metal = 0;
    md->players[0].xC_b3 = 0;
    md->players[0].vs_invisible = 0;
    md->players[0].xC_b5 = 0;
    md->players[0].xC_b6 = 0;
    md->players[0].xC_b7 = 0;

    /* SET FFA FLAG */
    md->rules.is_teams = 0;

    /* Store SSS Stage: the stage the event mode's CSS data holds (css_data, 0x80497758 + 0x1E) */
    css = gm_GetGameModeStateEnterData(&gm_Mode_Event_States[0]);
    md->rules.stkind = css->vs.start.rules.stkind;

    /* Get Event Code: the event's entry in EventJumpTable */
    mu_tmce_legacy_run(tmce_eventMenu_GetJumpTableOffset(page, event_id), md, event_id);
}

/* ============================================================================================
 * Fighter fields by console offset (legacy_common.h)
 * ============================================================================================ */
/* Every named one-bit field of fp+0x2218..0x2225 (ft/types.h), by console offset and mask */
#define LEGACY_FT_FLAGS(F) \
    F(0x2218, 0x80, allow_interrupt) \
    F(0x2218, 0x40, x2218_b1) \
    F(0x2218, 0x20, x2218_b2) \
    F(0x2218, 0x10, reflecting) \
    F(0x2218, 0x08, x2218_b4) \
    F(0x2218, 0x04, x2218_b5) \
    F(0x2218, 0x02, x2218_b6) \
    F(0x2218, 0x01, x2218_b7) \
    F(0x2219, 0x80, x2219_b0) \
    F(0x2219, 0x40, x2219_b1) \
    F(0x2219, 0x20, x2219_b2) \
    F(0x2219, 0x10, x2219_b3) \
    F(0x2219, 0x08, x2219_b4) \
    F(0x2219, 0x04, x2219_b5) \
    F(0x2219, 0x02, x2219_b6) \
    F(0x2219, 0x01, x2219_b7) \
    F(0x221A, 0x80, x221A_b0) \
    F(0x221A, 0x40, x221A_b1) \
    F(0x221A, 0x20, allow_sdi) \
    F(0x221A, 0x10, x221A_b3) \
    F(0x221A, 0x08, fall_fast) \
    F(0x221A, 0x04, x221A_b5) \
    F(0x221A, 0x02, x221A_b6) \
    F(0x221A, 0x01, x221A_b7) \
    F(0x221B, 0x80, x221B_b0) \
    F(0x221B, 0x40, x221B_b1) \
    F(0x221B, 0x20, x221B_b2) \
    F(0x221B, 0x10, x221B_b3) \
    F(0x221B, 0x08, x221B_b4) \
    F(0x221B, 0x04, x221B_b5) \
    F(0x221B, 0x02, x221B_b6) \
    F(0x221B, 0x01, x221B_b7) \
    F(0x221C, 0x80, x221C_b0) \
    F(0x221C, 0x40, x221C_b1) \
    F(0x221C, 0x20, x221C_b2) \
    F(0x221C, 0x10, x221C_b3) \
    F(0x221C, 0x08, x221C_b4) \
    F(0x221C, 0x04, x221C_b5) \
    F(0x221C, 0x02, x221C_b6) \
    F(0x221D, 0x20, x221D_b2) \
    F(0x221D, 0x10, has_prev_input) \
    F(0x221D, 0x08, input_disabled) \
    F(0x221D, 0x04, x221D_b5) \
    F(0x221D, 0x02, x221D_b6) \
    F(0x221D, 0x01, x221D_b7) \
    F(0x221E, 0x80, invisible) \
    F(0x221E, 0x40, x221E_b1) \
    F(0x221E, 0x20, x221E_b2) \
    F(0x221E, 0x10, x221E_b3) \
    F(0x221E, 0x08, x221E_b4) \
    F(0x221E, 0x04, x221E_b5) \
    F(0x221E, 0x02, x221E_b6) \
    F(0x221E, 0x01, x221E_b7) \
    F(0x221F, 0x80, x221F_b0) \
    F(0x221F, 0x40, x221F_b1) \
    F(0x221F, 0x20, x221F_b2) \
    F(0x221F, 0x10, is_sleeping) \
    F(0x221F, 0x08, is_sub_fighter) \
    F(0x221F, 0x04, x221F_b5) \
    F(0x221F, 0x02, x221F_b6) \
    F(0x221F, 0x01, x221F_b7) \
    F(0x2220, 0x10, x2220_b3) \
    F(0x2220, 0x08, x2220_b4) \
    F(0x2220, 0x04, x2220_b5) \
    F(0x2220, 0x02, x2220_b6) \
    F(0x2220, 0x01, x2220_b7) \
    F(0x2221, 0x80, x2221_b0) \
    F(0x2221, 0x40, x2221_b1) \
    F(0x2221, 0x20, x2221_b2) \
    F(0x2221, 0x10, x2221_b3) \
    F(0x2221, 0x08, x2221_b4) \
    F(0x2221, 0x04, x2221_b5) \
    F(0x2221, 0x02, x2221_b6) \
    F(0x2221, 0x01, x2221_b7) \
    F(0x2222, 0x80, x2222_b0) \
    F(0x2222, 0x40, can_multijump) \
    F(0x2222, 0x20, x2222_b2) \
    F(0x2222, 0x10, x2222_b3) \
    F(0x2222, 0x08, x2222_b4) \
    F(0x2222, 0x04, x2222_b5) \
    F(0x2222, 0x02, x2222_b6) \
    F(0x2222, 0x01, x2222_b7) \
    F(0x2223, 0x80, x2223_b0) \
    F(0x2223, 0x40, x2223_b1) \
    F(0x2223, 0x20, x2223_b2) \
    F(0x2223, 0x10, x2223_b3) \
    F(0x2223, 0x08, x2223_b4) \
    F(0x2223, 0x04, x2223_b5) \
    F(0x2223, 0x02, is_always_metal) \
    F(0x2223, 0x01, is_metal) \
    F(0x2224, 0x80, x2224_b0) \
    F(0x2224, 0x40, x2224_b1) \
    F(0x2224, 0x20, stamina_dead) \
    F(0x2224, 0x10, x2224_b3) \
    F(0x2224, 0x08, x2224_b4) \
    F(0x2224, 0x04, x2224_b5) \
    F(0x2224, 0x02, x2224_b6) \
    F(0x2224, 0x01, can_walljump) \
    F(0x2225, 0x80, x2225_b0) \
    F(0x2225, 0x40, x2225_b1) \
    F(0x2225, 0x20, x2225_b2) \
    F(0x2225, 0x10, x2225_b3) \
    F(0x2225, 0x08, x2225_b4)

static void legacy_unknown(const char* what, int offset)
{
    OSReport("[tmce] legacy events: no native field for fighter %s at 0x%X\n", what, offset);
}

int mu_tmce_legacy_ft_flag(void* data, int offset, int mask)
{
    Fighter* fp = data;
    switch ((offset << 8) | mask) {
#define LEGACY_GET(o, m, name)                                                                      \
    case ((o) << 8) | (m):                                                                          \
        return fp->name;
        LEGACY_FT_FLAGS(LEGACY_GET)
#undef LEGACY_GET
    }
    if (offset == 0x21FC) {
        return (fp->x21FC_flag.byte & mask) != 0;
    }
    legacy_unknown("flag", offset);
    return 0;
}

void mu_tmce_legacy_ft_set_flag(void* data, int offset, int mask, int on)
{
    Fighter* fp = data;
    switch ((offset << 8) | mask) {
#define LEGACY_SET(o, m, name)                                                                      \
    case ((o) << 8) | (m):                                                                          \
        fp->name = on ? 1 : 0;                                                                      \
        return;
        LEGACY_FT_FLAGS(LEGACY_SET)
#undef LEGACY_SET
    }
    if (offset == 0x21FC) {
        if (on) {
            fp->x21FC_flag.byte |= mask;
        } else {
            fp->x21FC_flag.byte &= ~mask;
        }
        return;
    }
    legacy_unknown("flag", offset);
}

/* the address of a byte field */
static u8* legacy_ft_u8_at(Fighter* fp, int offset)
{
    if (offset >= 0x670 && offset < 0x67C) {
        return (u8*) &fp->active_timer + (offset - 0x670);
    }
    if (offset >= 0x67C && offset < 0x68C) {
        return &fp->x67C + (offset - 0x67C);
    }
    if (offset >= 0x1A50 && offset < 0x1A54) {
        return (u8*) &fp->x1A50 + (offset - 0x1A50);
    }
    switch (offset) {
    case 0x0C:
        return &fp->player_idx;
    case 0x618:
        return &fp->pad_port;
    case 0x619:
        return &fp->costume_id;
    case 0x61A:
        return &fp->sub_color;
    case 0x61B:
        return &fp->team;
    case 0x1968:
        return &fp->x1968_jumpsUsed;
    case 0x1969:
        return &fp->x1969_walljumpUsed;
    case 0x1A8C:
        return (u8*) &fp->cpu.lstick.x;
    case 0x1A8D:
        return (u8*) &fp->cpu.lstick.y;
    case 0x1A8E:
        return (u8*) &fp->cpu.cstick.x;
    case 0x1A8F:
        return (u8*) &fp->cpu.cstick.y;
    case 0x1A90:
        return &fp->cpu.ltrigger;
    case 0x1A91:
        return &fp->cpu.rtrigger;
    }
    return NULL;
}

int mu_tmce_legacy_ft_u8(void* data, int offset)
{
    u8* p = legacy_ft_u8_at(data, offset);
    if (p == NULL) {
        legacy_unknown("byte", offset);
        return 0;
    }
    return *p;
}

void mu_tmce_legacy_ft_set_u8(void* data, int offset, int value)
{
    u8* p = legacy_ft_u8_at(data, offset);
    if (p == NULL) {
        legacy_unknown("byte", offset);
        return;
    }
    *p = (u8) value;
}

/* the address of a 32-bit field (an int, a float, a button word) */
static void* legacy_ft_u32_at(Fighter* fp, int offset)
{
    if (offset >= 0x74 && offset < 0x80) {
        return (u8*) &fp->x74_self_accel + (offset - 0x74);
    }
    if (offset >= 0x80 && offset < 0xE0) {
        /* self_vel, kb_vel, shield kb, xA4, cur_pos, prev_pos, pos_delta, xD4: consecutive Vec3s */
        return (u8*) &fp->self_vel + (offset - 0x80);
    }
    if (offset >= 0xE4 && offset < 0x104) {
        return (u8*) &fp->xE4_ground_accel_1 + (offset - 0xE4);
    }
    if (offset >= 0x620 && offset < 0x670) {
        return (u8*) &fp->input + (offset - 0x620);
    }
    if (offset >= 0x894 && offset < 0x8AC) {
        return (u8*) &fp->cur_anim_frame + (offset - 0x894);
    }
    if (offset >= 0x182C && offset < 0x1868) {
        /* struct dmg up to its first pointer: 4-byte fields at the console's spacing */
        return (u8*) &fp->dmg + (offset - 0x182C);
    }
    if (offset >= 0x1988 && offset < 0x19A0) {
        return (u8*) &fp->x1988 + (offset - 0x1988);
    }
    if (offset >= 0x2340 && offset < 0x23EC) {
        return (u8*) &fp->mv + (offset - 0x2340);
    }
    switch (offset) {
    case 0x04:
        return &fp->kind;
    case 0x10:
        return &fp->motion_id;
    case 0x14:
        return &fp->anim_id;
    case 0x2C:
        return &fp->facing_dir;
    case 0x30:
        return &fp->facing_dir1;
    case 0xE0:
        return &fp->ground_or_air;
    case 0x728:
        return &fp->coll_data.x38;
    case 0x83C:
        return &fp->coll_data.floor.index;
    case 0x88C:
        return &fp->ecb_lock;
    case 0xE14:
        return &fp->xDF4[0].kb_angle; /* throw hitbox 0's angle */
    case 0x18A4:
        return &fp->dmg.x18A4_knockbackMagnitude;
    case 0x18AC:
        return &fp->dmg.x18ac_time_since_hit;
    case 0x18C4:
        return &fp->dmg.x18c4_source_ply;
    case 0x1954:
        return &fp->dmg.x1954;
    case 0x1958:
        return &fp->dmg.x1958;
    case 0x195C:
        return &fp->dmg.x195c_hitlag_frames;
    case 0x1960:
        return &fp->x1960_vibrateMult;
    case 0x1A4C:
        return &fp->grab_timer;
    case 0x1A88:
        return &fp->cpu.buttons;
    case 0x1A94:
        return &fp->cpu.kind;
    case 0x1A98:
        return &fp->cpu.level;
    case 0x2064:
        return &fp->x2064_ledgeCooldown;
    case 0x21FC:
        return &fp->x21FC_flag; /* the flag byte and its 3 filler bytes, as one word */
    }
    return NULL;
}

unsigned int mu_tmce_legacy_ft_u32(void* data, int offset)
{
    void* p = legacy_ft_u32_at(data, offset);
    unsigned int v = 0;
    if (p == NULL) {
        legacy_unknown("word", offset);
        return 0;
    }
    __builtin_memcpy(&v, p, 4);
    return v;
}

void mu_tmce_legacy_ft_set_u32(void* data, int offset, unsigned int value)
{
    void* p = legacy_ft_u32_at(data, offset);
    if (p == NULL) {
        legacy_unknown("word", offset);
        return;
    }
    __builtin_memcpy(p, &value, 4);
}

/* the address of a pointer field */
static void** legacy_ft_ptr_at(Fighter* fp, int offset)
{
    if (offset >= 0x2190 && offset <= 0x21F8 && (offset & 3) == 0) {
        return (void**) &fp->grab_cb + (offset - 0x2190) / 4;
    }
    switch (offset) {
    case 0x0:
        return (void**) &fp->gobj;
    case 0x5A8:
        return (void**) &fp->x5A8;
    case 0x60C:
        return (void**) &fp->x60C;
    case 0x890:
        return (void**) &fp->x890_cameraBox;
    case 0x8AC:
        return (void**) &fp->x8AC_animSkeleton;
    case 0x988:
        return (void**) &fp->x914[0].victims_1[0].victim; /* hitbox 0's first victim */
    case 0x1974:
        return (void**) &fp->item_gobj;
    case 0x1978:
        return (void**) &fp->x1978;
    case 0x197C:
        return (void**) &fp->x197C;
    case 0x1980:
        return (void**) &fp->x1980;
    case 0x1984:
        return (void**) &fp->x1984_heldItemSpec;
    case 0x1A58:
        return (void**) &fp->victim_gobj;
    case 0x1A5C:
        return (void**) &fp->x1A5C;
    case 0x1A60:
        return (void**) &fp->target_item_gobj;
    case 0x2094:
        return (void**) &fp->x2094;
    case 0x20A0:
        return (void**) &fp->x20A0_accessory;
    }
    return NULL;
}

void* mu_tmce_legacy_ft_ptr(void* data, int offset)
{
    void** p = legacy_ft_ptr_at(data, offset);
    if (p == NULL) {
        legacy_unknown("pointer", offset);
        return NULL;
    }
    return *p;
}

void mu_tmce_legacy_ft_set_ptr(void* data, int offset, void* value)
{
    void** p = legacy_ft_ptr_at(data, offset);
    if (p == NULL) {
        legacy_unknown("pointer", offset);
        return;
    }
    *p = value;
}

/* ============================================================================================
 * Static player blocks (console 0x80453080 + 0xE90 * slot)
 * ============================================================================================ */

/* the part of a static player block the savestate keeps: the console's first 0x100 bytes, which end
 * 0x44 bytes into the stale move table (at 0xBC there) */
#define LEGACY_STATIC_BYTES (offsetof(StaticPlayer, stale_moves) + (0x100 - 0xBC))

static s16* legacy_static_s16(StaticPlayer* pl, int offset)
{
    switch (offset) {
    case 0x60:
        return &pl->staminas.byName.damage_percent;
    case 0x62:
        return &pl->staminas.byName.damage_percent_alt_or_start_hp;
    case 0x64:
        return &pl->staminas.byName.stamina;
    }
    return NULL;
}

int mu_tmce_legacy_static_get(int slot, int offset)
{
    StaticPlayer* pl = Player_GetPtrForSlot(slot);
    s16* p;
    if (offset == 0x0C) {
        return pl->transformed[0];
    }
    p = legacy_static_s16(pl, offset);
    if (p == NULL) {
        OSReport("[tmce] legacy events: no native field for static block 0x%X\n", offset);
        return 0;
    }
    return (u16) *p;
}

void mu_tmce_legacy_static_set(int slot, int offset, int value)
{
    s16* p = legacy_static_s16(Player_GetPtrForSlot(slot), offset);
    if (p == NULL) {
        OSReport("[tmce] legacy events: no native field for static block 0x%X\n", offset);
        return;
    }
    *p = (s16) value;
}

/* ============================================================================================
 * The current event's record (console 0x8045ABF0: gmMainLib_804D3EE0->vs.unk_530)
 * ============================================================================================ */
int mu_tmce_legacy_event_flag(int mask)
{
    struct EventData* ev = &gmMainLib_804D3EE0->vs.unk_530;
    switch (mask) {
    case 0x80:
        return ev->xB_0;
    case 0x40:
        return ev->xB_1;
    case 0x20:
        return ev->xB_2;
    case 0x10:
        return ev->xB_3;
    case 0x08:
        return ev->xB_4;
    case 0x04:
        return ev->xB_5;
    case 0x02:
        return ev->xB_6;
    case 0x01:
        return ev->xB_7;
    }
    return 0;
}

void mu_tmce_legacy_event_set_flag(int mask, int on)
{
    struct EventData* ev = &gmMainLib_804D3EE0->vs.unk_530;
    on = on ? 1 : 0;
    switch (mask) {
    case 0x80:
        ev->xB_0 = on;
        break;
    case 0x40:
        ev->xB_1 = on;
        break;
    case 0x20:
        ev->xB_2 = on;
        break;
    case 0x10:
        ev->xB_3 = on;
        break;
    case 0x08:
        ev->xB_4 = on;
        break;
    case 0x04:
        ev->xB_5 = on;
        break;
    case 0x02:
        ev->xB_6 = on;
        break;
    case 0x01:
        ev->xB_7 = on;
        break;
    }
}

/* ============================================================================================
 * Savestates (ASM 8805-9344): whole fighters copied into and out of a backup
 * ============================================================================================ */

/* A backup (console: HSD_MemAlloc(player block length + 0x100 + 0x10)): the fighter, then the saved
 * part of its static player block (main fighters only), then its camera box's state word. */
typedef struct LegacyBackup {
    Fighter fighter;
    u8 static_block[LEGACY_STATIC_BYTES];
    s32 camera_flag;
} LegacyBackup;

typedef struct LegacySaveSlot {
    LegacyBackup* backup[2]; /* [0] main (console +0), [1] follower (console +4) */
} LegacySaveSlot;

/* SaveState_GetPlayerDataPointer (ASM 9275): the slot of the `player`th player present (spawn order),
 * and its main fighter (follower = 0) or follower. 0xFF when there is none. */
int mu_tmce_legacy_get_player(int player, int follower, void** gobj_out, void** data_out)
{
    u8 order[8];
    int n = 0;
    int i;
    int slot;
    HSD_GObj* gobj;

    if (gobj_out != NULL) {
        *gobj_out = NULL;
    }
    if (data_out != NULL) {
        *data_out = NULL;
    }
    for (i = 0; i < 8; i++) {
        order[i] = 0xFF;
    }
    /* Make Bytefield For Player Order: an inactive player block stores 0 at offset 0 */
    for (i = 0; i < 6; i++) {
        if (Player_GetPtrForSlot(i)->player_state != 0) {
            order[n++] = i;
        }
    }
    if (player < 0 || player >= 8) {
        return 0xFF;
    }
    slot = order[player];
    if (slot == 0xFF) {
        return 0xFF;
    }
    gobj = Player_GetPtrForSlot(slot)->player_entity[follower ? 1 : 0];
    if (gobj == NULL) {
        return 0xFF;
    }
    if (gobj_out != NULL) {
        *gobj_out = gobj;
    }
    if (data_out != NULL) {
        *data_out = GET_FIGHTER(gobj);
    }
    return slot;
}

void mu_tmce_legacy_update_position(void* gobj);
void mu_tmce_legacy_update_camera_box(void* gobj);

/* SaveState_Save (ASM 8811) */
void mu_tmce_legacy_savestate_save(void* savestate, int skip_failsafe)
{
    LegacySaveSlot* ss = savestate;
    int total = gm_8016B558();
    int player;
    int sub;

    /* SaveState_OnDeathCheck: no saving while anyone has an on-death callback, an item or an accessory */
    if (skip_failsafe == 0) {
        for (player = 0; player < total; player++) {
            for (sub = 0; sub < 2; sub++) {
                Fighter* fp;
                if (mu_tmce_legacy_get_player(player, sub, NULL, (void**) &fp) == 0xFF) {
                    continue;
                }
                if (fp->death1_cb != NULL || fp->death2_cb != NULL || fp->death3_cb != NULL ||
                    fp->item_gobj != NULL || fp->x1978 != NULL || fp->x20A0_accessory != NULL)
                {
                    Ground_801C53EC(0xAF);
                    Ground_801C53EC(0xAF);
                    return;
                }
            }
        }
    }

    for (player = 0; player < total; player++) {
        for (sub = 0; sub < 2; sub++) {
            LegacyBackup** slot_backup = &ss[player].backup[sub];
            LegacyBackup* backup;
            Fighter* fp;
            int slot;

            /* Remove Old Backup. (The console leaves the freed pointer in place when the fighter is
             * gone now; it is cleared here so a later save cannot free it again.) */
            if (*slot_backup != NULL) {
                HSD_Free(*slot_backup);
                *slot_backup = NULL;
            }
            slot = mu_tmce_legacy_get_player(player, sub, NULL, (void**) &fp);
            if (slot == 0xFF) {
                continue;
            }
            backup = HSD_MemAlloc(sizeof(LegacyBackup));
            *slot_backup = backup;
            /* Copy Player Block to Backup */
            __builtin_memcpy(&backup->fighter, fp, sizeof(Fighter));
            /* Copy Static Block to Backup (not for a follower) */
            if (sub == 0) {
                __builtin_memcpy(backup->static_block, Player_GetPtrForSlot(slot), LEGACY_STATIC_BYTES);
            }
            /* Save Camera Flag */
            backup->camera_flag = fp->x890_cameraBox->state;
        }
    }
}

/* SaveState_Load (ASM 8996) */
void mu_tmce_legacy_savestate_load(void* savestate)
{
    LegacySaveSlot* ss = savestate;
    int total = gm_8016B558();
    int player;
    int sub;

    for (player = 0; player < total; player++) {
        for (sub = 0; sub < 2; sub++) {
            LegacyBackup* backup = ss[player].backup[sub];
            HSD_GObj* gobj;
            Fighter* fp;
            int slot;
            float lstick_x, lstick_y;
            HSD_Pad held;
            u8 bubbles[4];

            if (backup == NULL) {
                continue;
            }
            slot = mu_tmce_legacy_get_player(player, sub, (void**) &gobj, (void**) &fp);
            if (slot == 0xFF) {
                continue;
            }

            /* Restore Facing Direction */
            fp->facing_dir = backup->fighter.facing_dir;
            /* Enter Into Sleep */
            ftCo_800D4F24(gobj, 0);
            /* Remove On Death Function Pointer */
            fp->death2_cb = NULL;
            fp->death3_cb = NULL;
            /* Enter Into Backed Up State (blend 0) */
            Fighter_ChangeMotionState(gobj, backup->fighter.motion_id, 0, backup->fighter.cur_anim_frame,
                                      backup->fighter.frame_speed_mul, 0.0F, NULL);

            /* Keep Previous Frame Buttons From Current Block, and the collision bubble toggles */
            lstick_x = fp->input.lstick[0].x;
            lstick_y = fp->input.lstick[0].y;
            held = fp->input.held_buttons[0];
            __builtin_memcpy(bubbles, &fp->x21FC_flag, 4);

            /* Copy PlayerBlock Backup to Current */
            __builtin_memcpy(fp, &backup->fighter, sizeof(Fighter));
            /* Copy Static Block Backup to Current (not for a follower) */
            if (sub == 0) {
                __builtin_memcpy(Player_GetPtrForSlot(slot), backup->static_block, LEGACY_STATIC_BYTES);
            }

            /* Restore Previous Frame Buttons From Current Block */
            fp->input.lstick[0].x = lstick_x;
            fp->input.lstick[1].x = lstick_x;
            fp->input.lstick[0].y = lstick_y;
            fp->input.lstick[1].y = lstick_y;
            fp->input.held_buttons[0] = held;
            fp->input.held_buttons[1] = held;
            fp->input.held_buttons[2] = held;
            /* Restore Collision Bubble Toggles */
            __builtin_memcpy(&fp->x21FC_flag, bubbles, 4);

            /* Remove Cached Animation Pointer (This fixes the Fall Animation Bug) */
            fp->x5A8 = 0;
            /* Remove Respawn Platform JObj Pointer and Think Function */
            fp->x20A0_accessory = NULL;
            fp->accessory1_cb = NULL;
            /* Remove Held Item Pointer */
            fp->item_gobj = NULL;

            /* Update ECB Position */
            mu_tmce_legacy_update_position(gobj);
            /* Stop Player's SFX, the crowd's, and the fighter's effects */
            ft_80088A50(fp);
            un_80321CE8();
            ftCommon_8007DB24(gobj);

            /* Savestate_RestoreCameraFlag */
            fp->x890_cameraBox->state = backup->camera_flag;
            mu_tmce_legacy_update_camera_box(gobj);

            /* Remake HUD For Dead Players (not for a follower) */
            if (sub == 0) {
                IfDamageState* hud = &ifStatus_HudInfo.players[slot];
                if (gm_8016B094() && Player_GetStocks(slot) == 0) {
                    /* remove percent: the flag byte = 0x80 */
                    hud->flags.explode_animation = 1;
                    hud->flags.randomize_velocity = 0;
                    hud->flags.force_digit_shake = 0;
                    hud->flags.unk10 = 0;
                    hud->flags.hide_all_digits = 0;
                    hud->flags.animation_status_id = 0;
                    hud->flags.unk1 = 0;
                } else if (hud->flags.explode_animation) {
                    /* SaveState_REMAKE_PERCENT */
                    ifStatus_802F6E1C(slot);
                }
            }
        }
    }
    /* SaveState_Load_RemoveAllGFX is inside a block comment in the ASM */
}

/* DPadCPUPercent's write into a backup's saved static block (console backup + length + 0x60 / 0x62) */
void mu_tmce_legacy_backup_static_set(void* data, int offset, int value)
{
    LegacyBackup* backup = data;
    StaticPlayer* pl = (StaticPlayer*) backup->static_block; /* the saved prefix, at the block's offsets */
    s16* p = legacy_static_s16(pl, offset);
    if (p == NULL) {
        OSReport("[tmce] legacy events: no native field for static block 0x%X\n", offset);
        return;
    }
    *p = (s16) value;
}

/* ============================================================================================
 * Fighter routines that copy between the decomp's own structures
 * ============================================================================================ */

/* CheckIfPlayerHasAFollower (ASM 10230): the follower of the fighter (its slot's second fighter, if
 * the character's second fighter is a follower rather than a transformation). */
void* mu_tmce_legacy_follower(void* gobj, void** follower_data)
{
    Fighter* fp = GET_FIGHTER((HSD_GObj*) gobj);
    HSD_GObj* sub_gobj;
    Fighter* sub_fp;

    if (follower_data != NULL) {
        *follower_data = NULL;
    }
    /* the fighter's slot is passed as the player number, as the ASM does */
    if (mu_tmce_legacy_get_player(fp->player_idx, 1, (void**) &sub_gobj, (void**) &sub_fp) == 0xFF) {
        return NULL;
    }
    /* pdLoadCommonData (ftMapping_list): 3 bytes per external character, byte 2 = transformation */
    {
        typedef struct {
            s8 internal_id;
            s8 extra_internal_id;
            s8 has_transformation;
        } LegacyFtMapping;
        extern LegacyFtMapping ftMapping_list[];
        if (ftMapping_list[Player_GetPlayerCharacter(sub_fp->player_idx)].has_transformation != 0) {
            return NULL;
        }
    }
    if (follower_data != NULL) {
        *follower_data = sub_fp;
    }
    return sub_gobj;
}

/* UpdatePosition (ASM 11258): copies the position into every collision position, the collision frame
 * id, the model's translation and the static block. */
void mu_tmce_legacy_update_position(void* gobj)
{
    Fighter* fp = GET_FIGHTER((HSD_GObj*) gobj);
    HSD_JObj* jobj = GET_JOBJ((HSD_GObj*) gobj);

    fp->coll_data.cur_pos = fp->cur_pos;
    fp->coll_data.prev_pos = fp->cur_pos;
    fp->coll_data.last_pos = fp->cur_pos;
    fp->coll_data.x28_vec = fp->cur_pos;
    fp->coll_data.x38 = mpColl_804D64AC;
    /* Adjust JObj position (code copied from 8006c324), without dirtying it */
    jobj->translate = fp->cur_pos;
    Player_80032828(fp->player_idx, fp->is_sub_fighter, &fp->cur_pos);
}

/* UpdateCameraBox (ASM 11672) */
void mu_tmce_legacy_update_camera_box(void* gobj)
{
    Fighter* fp = GET_FIGHTER((HSD_GObj*) gobj);
    CmSubject* cam;

    ftCamera_UpdateCameraBox(gobj);
    /* Update Camera Box Direction Tween: the current horizontal bounds to their targets */
    cam = fp->x890_cameraBox;
    cam->ext.h.x = cam->target_ext.h.x;
    cam->ext.h.y = cam->target_ext.h.y;
    Camera_8002F3AC();
}

/* GetGroundCenter (ASM 11317): the middle of a ground line, from the stage's collision lines and
 * vertices (-0x51E4(r13) / -0x51E8(r13)). */
void mu_tmce_legacy_ground_center(int line, float* x, float* y)
{
    CollLine* lines = mpGetGroundCollLine();
    CollVtx* vtx = mpGetGroundCollVtx();
    int v0 = lines[line].x0->v0_idx;
    int v1 = lines[line].x0->v1_idx;
    *x = (vtx[v0].pos.x + vtx[v1].pos.x) / 2.0F;
    *y = (vtx[v0].pos.y + vtx[v1].pos.y) / 2.0F;
}

/* CheckForActiveHitboxes (ASM 10756): the state word of each of the 4 hitboxes */
int mu_tmce_legacy_hitboxes_active(void* data)
{
    Fighter* fp = data;
    int i;
    for (i = 0; i < 4; i++) {
        if (fp->x914[i].state != 0) {
            return 1;
        }
    }
    return 0;
}

/* ============================================================================================
 * gmevent.c and gmvs.c parts the events call by address
 * ============================================================================================ */

/* EventMatch_OnWinCondition (0x801BC4F4, gm_801BC4F4 in gmevent.c, private to that file): the event
 * is over; decide whether it beat the record, end the match with its result, free the caller. */
void mu_tmce_legacy_event_win(void* gobj)
{
    u32 temp_r30;
    u32 temp_r29;
    struct EventData* temp_r28;
    u32 var_r27;
    int i;
    bool var_r25;
    u32 var_r4;

    temp_r30 = gmMainLib_804D3EE0->vs.unk_530.unk_535;
    temp_r28 = &gmMainLib_804D3EE0->vs.unk_530;
    temp_r28->xB_1 = true;
    temp_r29 = gmMainLib_8015CF5C(temp_r30);
    var_r25 = false;
    if (temp_r28->xB_6) {
        var_r4 = gm_GetFrameCount();
        var_r4 += temp_r28->x34;
        if (temp_r30 == 0x31) {
            var_r4 = temp_r28->x34;
        }
        if (var_r4 > 0x34BBF) {
            var_r4 = 0x34BBF;
        }
        temp_r28->xC = var_r4;
        if (temp_r29 == 0 || var_r4 < temp_r29) {
            var_r25 = true;
        }
    } else {
        var_r27 = 0;
        for (i = 1; i < 6; i++) {
            if (Player_GetPlayerSlotType(i) != Gm_PKind_NA) {
                var_r27 += Player_GetKOsByPlayerIndex(0, i);
            }
        }
        if (temp_r30 == 0x1F) {
            var_r27 = Player_GetKOsByPlayerIndex(0, 1) - pl_8003FBFC(0);
        }
        if (var_r27 > (u32) -1) {
            var_r27 = -1;
        }
        temp_r28->xC = var_r27;
        if (var_r27 > temp_r29) {
            var_r25 = true;
        }
    }
    Player_80036844(0, 1);
    lbAudioAx_80028B90();
    gm_SetGameSpeed(1.0F);
    if (var_r25) {
        gm_8016B33C(2);
        gm_8016B350(0x9C40);
        gm_8016B364(0x144);
    } else {
        gm_8016B33C(2);
        gm_8016B364(0x145);
    }
    gm_8016B328();
    HSD_GObjFree(gobj);
}

/* InitializeHighScore's `stw r3, 0x2518(0x8046B6A0)`: the match's on-match-end callback */
void mu_tmce_legacy_set_match_end(void (*cb)(int outcome))
{
    gmVs_GetSceneController()->start.on_match_end = (void (*)(u8)) cb;
}

/* LedgeStallLoad: Brinstar's platform (map GObj 6): its flesh item (Ground +0xE4, the acid state's
 * material item, a 32-bit address) gets an intangible hurtbox (Item +0xACC = 2). */
void mu_tmce_legacy_zebes_platform_intangible(void* map_gobj)
{
    Ground* gp = GET_GROUND((HSD_GObj*) map_gobj);
    u32 addr;
    Item* ip;

    __builtin_memcpy(&addr, (u8*) &gp->u + (0xE4 - 0xC4), 4);
    if (addr == 0) {
        return;
    }
    ip = GET_ITEM((HSD_GObj*) MU_Z(addr));
    ip->xACC_itemHurtbox[0].state = HurtCapsule_Intangible;
}
