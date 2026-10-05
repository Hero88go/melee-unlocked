/* The match block: the game's start data (rules and six players) in the console's own layout,
 * 0x138 bytes, big-endian. A network match is negotiated by the host as one of these blocks; the
 * game copies it into the match about to start, and writes the block of a finished match back for
 * the game report. Built only with MU_NO_SLIPPI, where the files that used to hold these two copies
 * are left out.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <string.h>
#include <melee/ft/forward.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>

void mu_online_abi_log(const char* text);
int snprintf(char* buffer, __SIZE_TYPE__ size, const char* format, ...);

enum { MATCH_RULES_SIZE = 0x60, MATCH_PLAYER_SIZE = 0x24, MATCH_PLAYERS = 6 };

static u32 be32(const u8* p) { return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3]; }
static u16 be16(const u8* p) { return (u16) (p[0] << 8 | p[1]); }
static float bef(const u8* p) { u32 v = be32(p); float f; memcpy(&f, &v, 4); return f; }

static void put32(u8* b, int off, u32 v)
{
    b[off] = (u8) (v >> 24);
    b[off + 1] = (u8) (v >> 16);
    b[off + 2] = (u8) (v >> 8);
    b[off + 3] = (u8) v;
}
static void putf(u8* b, int off, float f) { u32 v; memcpy(&v, &f, 4); put32(b, off, v); }
static void put16(u8* b, int off, u16 v) { b[off] = (u8) (v >> 8); b[off + 1] = (u8) v; }

/* ---- block -> start data ---- */

#define BIT(b, n) ((g[b] >> (7 - (n))) & 1)

static void read_rules(StartMeleeRules* r, const u8* g)
{
    r->match_kind = g[0] >> 5;
    r->x0_3 = (g[0] >> 2) & 7;
    r->timer_enabled = BIT(0, 6);
    r->timer_counts_up = BIT(0, 7);
    r->x1_0 = BIT(1, 0); r->x1_1 = BIT(1, 1); r->x1_2 = BIT(1, 2); r->x1_3 = BIT(1, 3);
    r->x1_4 = BIT(1, 4); r->x1_5 = BIT(1, 5); r->timer_shows_hours = BIT(1, 6);
    r->friendly_fire = BIT(1, 7);
    r->is_stock = BIT(2, 0); r->x2_1 = BIT(2, 1); r->x2_2 = BIT(2, 2); r->single_button = BIT(2, 3);
    r->disable_pausing = BIT(2, 4); r->x2_5 = BIT(2, 5); r->x2_6 = BIT(2, 6); r->x2_7 = BIT(2, 7);
    r->x3_0 = BIT(3, 0); r->x3_1 = BIT(3, 1); r->x3_2 = BIT(3, 2); r->x3_3 = BIT(3, 3);
    r->x3_4 = BIT(3, 4); r->x3_5 = BIT(3, 5); r->x3_6 = BIT(3, 6); r->x3_7 = BIT(3, 7);
    r->x4_0 = BIT(4, 0); r->is_vs = BIT(4, 1); r->x4_2 = BIT(4, 2); r->x4_3 = BIT(4, 3);
    r->x4_4 = BIT(4, 4); r->x4_5 = BIT(4, 5); r->x4_6 = BIT(4, 6); r->x4_7 = BIT(4, 7);
    r->x5_0 = BIT(5, 0); r->x5_1 = BIT(5, 1); r->x5_2 = BIT(5, 2); r->x5_3 = BIT(5, 3);
    r->x5_4 = BIT(5, 4); r->x5_5 = BIT(5, 5); r->x5_6 = BIT(5, 6); r->x5_7 = BIT(5, 7);
    r->x6 = g[6];
    r->x7 = g[7];
    r->is_teams = g[8];
    r->x9 = g[9];
    r->xA = g[0xA];
    r->item_freq = (s8) g[0xB];
    r->sd_penalty = (s8) g[0xC];
    r->xD = g[0xD];
    r->stkind = be16(g + 0xE);
    r->time_limit = be32(g + 0x10);
    r->x14 = g[0x14];
    r->x18 = be32(g + 0x18);
    r->x1C_pad[0] = be32(g + 0x1C);
    r->x20 = (u64) be32(g + 0x20) << 32 | be32(g + 0x24);
    r->x28 = (int) be32(g + 0x28);
    r->x2C = bef(g + 0x2C);
    r->x30 = bef(g + 0x30);
    r->game_speed = bef(g + 0x34);
    /* The nine callback slots (0x38..0x5B) hold console addresses in a block made on a console and
     * nothing in one made here. Neither can be called: every callback starts empty, and a block
     * that carried one is reported. */
    if (be32(g + 0x38) != 0 || be32(g + 0x3C) != 0) {
        char line[120];
        snprintf(line, sizeof line, "match block: pause callbacks %08X %08X are not restored",
                 (unsigned int) be32(g + 0x38), (unsigned int) be32(g + 0x3C));
        mu_online_abi_log(line);
    }
    r->on_unpause_override = NULL;
    r->on_pause_override = NULL;
    r->check_for_pauser_override = NULL;
    r->on_match_start = NULL;
    r->on_frame_start = NULL;
    r->on_frame_end = NULL;
    r->on_match_end = NULL;
    r->x54 = NULL;
    r->x58 = NULL;
}

static void read_player(PlayerInitData* p, const u8* g)
{
    p->ckind = (s8) g[0];
#ifdef MU_AKANEIA_FIGHTERS
    if ((s8) g[0] != ChKind_None) {
        const int ckind = mu_ak_ckind_from_mex((s8) g[0]);
        p->ckind = ckind >= 0 ? (s8) ckind : ChKind_None;
    }
#endif
    p->slot_type = g[1];
    p->stocks = (s8) g[2];
    p->color = g[3];
    p->slot = g[4];
    p->spawn_pos = (s8) g[5];
    p->spawn_dir = (s8) g[6];
    p->sub_color = g[7];
    p->handicap = (s8) g[8];
    p->team = g[9];
    p->nametag = g[0xA];
    p->xB = g[0xB];
    p->rumble_enabled = BIT(0xC, 0); p->xC_b1 = BIT(0xC, 1); p->vs_metal = BIT(0xC, 2);
    p->xC_b3 = BIT(0xC, 3); p->vs_invisible = BIT(0xC, 4); p->xC_b5 = BIT(0xC, 5);
    p->xC_b6 = BIT(0xC, 6); p->xC_b7 = BIT(0xC, 7);
    p->xD_b0 = BIT(0xD, 0); p->xD_b1 = BIT(0xD, 1); p->xD_b2 = BIT(0xD, 2); p->xD_b3 = BIT(0xD, 3);
    p->xD_b4 = BIT(0xD, 4); p->xD_b5 = BIT(0xD, 5); p->xD_b6 = BIT(0xD, 6); p->xD_b7 = BIT(0xD, 7);
    p->cpu_kind = g[0xE];
    p->cpu_level = g[0xF];
    p->damage = be16(g + 0x10);
    p->damage1 = be16(g + 0x12);
    p->hp = be16(g + 0x14);
    p->attack_ratio = bef(g + 0x18);
    p->defense_ratio = bef(g + 0x1C);
    p->model_scale = bef(g + 0x20);
    /* Name tags are display only, and the other side's tag is not in this memory card's table: a
     * human slot shows no tag (0x78) instead of whatever local tag has that index. */
    if (p->slot_type == 0 && p->nametag != 0x78) {
        p->nametag = 0x78;
    }
}

#undef BIT

/* The negotiated block into the match about to start (called by mu_online_start_melee). */
void mu_replay_apply_game_info(StartMeleeData* data, const unsigned char* info)
{
    int i;
    read_rules(&data->rules, info);
    for (i = 0; i < MATCH_PLAYERS; i++) {
        read_player(&data->players[i], info + MATCH_RULES_SIZE + MATCH_PLAYER_SIZE * i);
    }
}

/* ---- start data -> block ---- */

static void write_rules(u8* g, const StartMeleeRules* r)
{
    memset(g, 0, MATCH_RULES_SIZE);
    g[0] = r->match_kind << 5 | r->x0_3 << 2 | r->timer_enabled << 1 | r->timer_counts_up;
#define RB(byte, field, bit) g[byte] |= (u8) (r->field << (7 - bit))
    RB(1, x1_0, 0); RB(1, x1_1, 1); RB(1, x1_2, 2); RB(1, x1_3, 3);
    RB(1, x1_4, 4); RB(1, x1_5, 5); RB(1, timer_shows_hours, 6); RB(1, friendly_fire, 7);
    RB(2, is_stock, 0); RB(2, x2_1, 1); RB(2, x2_2, 2); RB(2, single_button, 3);
    RB(2, disable_pausing, 4); RB(2, x2_5, 5); RB(2, x2_6, 6); RB(2, x2_7, 7);
#define ROW(n) RB(n, x##n##_0, 0); RB(n, x##n##_1, 1); RB(n, x##n##_2, 2); RB(n, x##n##_3, 3); \
               RB(n, x##n##_4, 4); RB(n, x##n##_5, 5); RB(n, x##n##_6, 6); RB(n, x##n##_7, 7)
    ROW(3); ROW(5);
    RB(4, x4_0, 0); RB(4, is_vs, 1); RB(4, x4_2, 2); RB(4, x4_3, 3);
    RB(4, x4_4, 4); RB(4, x4_5, 5); RB(4, x4_6, 6); RB(4, x4_7, 7);
#undef ROW
#undef RB
    g[6] = r->x6; g[7] = r->x7; g[8] = r->is_teams; g[9] = r->x9; g[10] = r->xA;
    g[11] = (u8) r->item_freq; g[12] = (u8) r->sd_penalty; g[13] = r->xD;
    put16(g, 14, r->stkind); put32(g, 16, r->time_limit); g[20] = r->x14;
    put32(g, 0x18, r->x18); put32(g, 0x1C, r->x1C_pad[0]);
    put32(g, 0x20, (u32) (r->x20 >> 32)); put32(g, 0x24, (u32) r->x20);
    put32(g, 0x28, (u32) r->x28); putf(g, 0x2C, r->x2C); putf(g, 0x30, r->x30);
    putf(g, 0x34, r->game_speed);
    /* The callback slots stay zero: an address of this process must never leave it. */
}

static void write_player(u8* g, const PlayerInitData* p)
{
    memset(g, 0, MATCH_PLAYER_SIZE);
    g[0] = (u8) p->ckind; g[1] = p->slot_type; g[2] = (u8) p->stocks; g[3] = p->color;
    g[4] = p->slot; g[5] = (u8) p->spawn_pos; g[6] = (u8) p->spawn_dir; g[7] = p->sub_color;
    g[8] = (u8) p->handicap; g[9] = p->team; g[10] = p->nametag; g[11] = p->xB;
#define PB(byte, field, bit) g[byte] |= (u8) (p->field << (7 - bit))
    PB(12, rumble_enabled, 0); PB(12, xC_b1, 1); PB(12, vs_metal, 2); PB(12, xC_b3, 3);
    PB(12, vs_invisible, 4); PB(12, xC_b5, 5); PB(12, xC_b6, 6); PB(12, xC_b7, 7);
    PB(13, xD_b0, 0); PB(13, xD_b1, 1); PB(13, xD_b2, 2); PB(13, xD_b3, 3);
    PB(13, xD_b4, 4); PB(13, xD_b5, 5); PB(13, xD_b6, 6); PB(13, xD_b7, 7);
#undef PB
    g[14] = p->cpu_kind; g[15] = p->cpu_level;
    put16(g, 16, p->damage); put16(g, 18, p->damage1); put16(g, 20, p->hp);
    putf(g, 24, p->attack_ratio); putf(g, 28, p->defense_ratio); putf(g, 32, p->model_scale);
}

/* The running match as a block, for the game report at the end of a network match. */
void mu_replay_game_info_block(unsigned char* out, const StartMeleeData* data)
{
    int i;
    write_rules(out, &data->rules);
    for (i = 0; i < MATCH_PLAYERS; ++i) {
        write_player(out + MATCH_RULES_SIZE + MATCH_PLAYER_SIZE * i, &data->players[i]);
    }
#ifdef MU_AKANEIA_FIGHTERS
    /* Added fighters go out under the disc's own ids, as they came in (read_player). */
    for (i = 0; i < MATCH_PLAYERS; ++i) {
        u8* p = out + MATCH_RULES_SIZE + MATCH_PLAYER_SIZE * i;
        const int ext = mu_ak_mex_external(data->players[i].ckind);
        if (MU_AK_CKIND(data->players[i].ckind) ||
            (data->players[i].ckind >= CKind_Playable_Count && data->players[i].ckind < ChKind_Max))
        {
            p[0] = ext >= 0 ? (u8) ext : ChKind_None;
        }
    }
#endif
}
