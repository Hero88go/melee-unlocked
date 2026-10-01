/* Host-layout rules -> Slippi's console-layout Game Info block. Pointer slots
 * are intentionally zero: recorded host addresses must never enter a replay.
 * SPDX-License-Identifier: GPL-2.0-or-later */
static void record_rules(u8* g, const StartMeleeRules* r)
{
    memset(g, 0, 0x60);
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
}

static void record_player(u8* g, const PlayerInitData* p)
{
    memset(g, 0, 0x24);
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

/* Slippi's SendGameInfo (8016E74C): the Game Start event. Offsets below count the command byte, as
 * the replay format does. msrb is the online match state (Slippi's MSRB), NULL offline; the online
 * fields come from it exactly where SendGameInfo copies them. */
enum {
    MSRB_P1_NAME = 46, MSRB_P1_CONNECT_CODE = 201, MSRB_P1_SLIPPI_UID = 241,
    MSRB_MATCH_ID = 910, MSRB_ALT_STAGE_MODE = 961,
    GS_NAMETAG = 0x161, GS_PAL = 0x1A1, GS_FROZEN_PS = 0x1A2, GS_MINOR = 0x1A3, GS_MAJOR = 0x1A4,
    GS_NAME = 0x1A5, GS_CODE = 0x221, GS_UID = 0x249, GS_LANGUAGE = 0x2BD, GS_MATCH_ID = 0x2BE,
    GS_GAME_NUMBER = 0x2F1, GS_TIEBREAK = 0x2F5,
};
int mu_ps_frozen_toggle(void);

static void emit_game_start(const StartMeleeData* data, const u8* msrb)
{
    u8 b[0x2F9];
    int i;
    memset(b, 0, sizeof b);
    b[1] = 3; b[2] = 19; b[3] = 1;
    record_rules(b + 5, &data->rules);
    for (i = 0; i < 6; ++i) record_player(b + 5 + 0x60 + 0x24 * i, &data->players[i]);
    /* The recorded copy only: a human Zelda holding A starts as Sheik, and a costume the character
     * does not have records as the first one. */
    for (i = 0; i < 4; ++i) {
        u8* p = b + 5 + 0x60 + 0x24 * i;
        if (p[1] == 0 && p[0] == CKind_Zelda && (HSD_PadCopyStatus[i].button & HSD_PAD_A)) {
            p[0] = CKind_Seak;
        }
    }
    for (i = 0; i < 4; ++i) {
        u8* p = b + 5 + 0x60 + 0x24 * i;
        if (p[1] == 0 && (s8) p[3] >= (s8) gm_GetNumCostumesForCKind(p[0])) {
            p[3] = 0;
        }
    }
    put32(b, 0x13D, *HSD_RandSeedPtr);
    for (i = 0; i < 4; ++i) {   /* UCF dashback and shield drop, per port: the General Codes' */
        put32(b, 0x141 + i * 8, mu_general_codes() ? 1 : 0);
        put32(b, 0x145 + i * 8, mu_general_codes() ? 1 : 0);
    }
    for (i = 0; i < 4; ++i) {
        const u8* p = b + 5 + 0x60 + 0x24 * i;
        if (p[1] == 0 && p[0xA] != 0x78) {
            const char* tag = GetNameText(p[0xA]);
            if (tag != NULL) memcpy(b + GS_NAMETAG + 0x10 * i, tag, 0x10);
        }
    }
    b[GS_PAL] = 0;
    b[GS_FROZEN_PS] = (u8) (msrb != NULL ? msrb[MSRB_ALT_STAGE_MODE] : mu_ps_frozen_toggle());
    b[GS_MINOR] = gm_GetCurrentSceneIndex();
    b[GS_MAJOR] = gm_GetCurrentGameMode();
    if (msrb != NULL) {
        for (i = 0; i < 4; ++i) {
            if (b[5 + 0x60 + 0x24 * i + 1] != 0) continue;   /* humans only */
            memcpy(b + GS_NAME + 31 * i, msrb + MSRB_P1_NAME + 31 * i, 31);
            memcpy(b + GS_CODE + 10 * i, msrb + MSRB_P1_CONNECT_CODE + 10 * i, 10);
            memcpy(b + GS_UID + 29 * i, msrb + MSRB_P1_SLIPPI_UID + 29 * i, 29);
        }
    }
    b[GS_LANGUAGE] = (u8) lbLang_GetSavedLanguage();
    if (msrb != NULL) {
        memcpy(b + GS_MATCH_ID, msrb + MSRB_MATCH_ID, 51);
        /* The game number within this search (and the tiebreak number, ranked only) is kept by
         * the host, which sees the searches; it fills GS_GAME_NUMBER in. */
    }
#ifdef MU_AKANEIA_FIGHTERS
    /* Convert the final serialized copy only. Costume and Zelda checks above use native kinds;
     * the replay keeps the original disc's external ids for Dolphin compatibility. */
    for (i = 0; i < 6; ++i) {
        u8* p = b + 5 + 0x60 + 0x24 * i;
        const int ext = mu_ak_mex_external(data->players[i].ckind);
        if (MU_AK_CKIND(data->players[i].ckind) ||
            (data->players[i].ckind >= CKind_Playable_Count &&
             data->players[i].ckind < ChKind_Max))
        {
            p[0] = ext >= 0 ? (u8) ext : ChKind_None;
        }
    }
#endif
    mu_replay_abi_event(0x36, b + 1, sizeof b - 1);
}
