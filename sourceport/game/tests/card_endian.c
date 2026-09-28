#include <stdio.h>
#include <string.h>

#include <melee/gm/types.h>
#include <melee/lb/lbcardserial.h>

static int isBytes(const void* value, const u8* expected, size_t size)
{
    return memcmp(value, expected, size) == 0;
}

/* Independent golden transform: endian-convert every declared multi-byte
 * field while preserving byte fields and padding. Compare complete objects so
 * an omitted scalar cannot hide behind a reversible production transform. */
static void refSwap16(void* value)
{
    u8* p = value;
    u8 t = p[0]; p[0] = p[1]; p[1] = t;
}

static void refSwap32(void* value)
{
    u8* p = value;
    u8 t = p[0]; p[0] = p[3]; p[3] = t;
    t = p[1]; p[1] = p[2]; p[2] = t;
}

static void refSwap64(void* value)
{
    u8* p = value;
    u8 t = p[0]; p[0] = p[7]; p[7] = t;
    t = p[1]; p[1] = p[6]; p[6] = t;
    t = p[2]; p[2] = p[5]; p[5] = t;
    t = p[3]; p[3] = p[4]; p[4] = t;
}

static void refSwap16Array(void* values, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) refSwap16((u8*) values + 2 * i);
}

static void refSwap32Array(void* values, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) refSwap32((u8*) values + 4 * i);
}

static void refReverseBitfieldBytes(void* value)
{
    u8* p = value;
    u8 out[2] = { 0, 0 };
    size_t byte, bit;
    for (byte = 0; byte < 2; byte++)
        for (bit = 0; bit < 8; bit++)
            out[byte] |= ((p[byte] >> bit) & 1) << (7 - bit);
    p[0] = out[0]; p[1] = out[1];
}

static void refSwapStats(struct GmStats* s)
{
    refSwap16(&s->sd_count);
    refSwap32(&s->attacks_hit); refSwap32(&s->attacks_total);
    refSwap32(&s->damage_dealt); refSwap32(&s->damage_taken);
    refSwap32(&s->damage_recovered); refSwap16(&s->peak_damage);
    refSwap16(&s->match_count); refSwap16(&s->victories); refSwap16(&s->losses);
    refSwap32(&s->play_time); refSwap32(&s->total_player_count);
    refSwap32(&s->walk_distance); refSwap32(&s->run_distance);
    refSwap32(&s->fall_distance); refSwap32(&s->peak_height);
    refSwap32(&s->coins_collected); refSwap32(&s->coins_swiped);
    refSwap32(&s->coins_lost);
}

static void refSwapFighter(struct FighterData* f)
{
    refSwap16Array(f->fighter_kos, SELKIND_COUNT);
    refSwapStats(&f->stats);
    refReverseBitfieldBytes(&f->x7C);
    refSwap16(&f->x7C.x7E);
    refSwap32(&f->x7C.x84); refSwap32(&f->x7C.x88);
    refSwap32(&f->x7C.x8C); refSwap32(&f->x7C.x90);
    refSwap32(&f->x7C.x94); refSwap32(&f->x7C.x98);
    refSwap32(&f->x7C.x9C); refSwap16(&f->x7C.xA0);
    refSwap16(&f->x7C.xA2); refSwap32(&f->x7C.xA4);
    refSwap32(&f->x7C.xA8);
}

static void refConvertSave(GmSaveData* d)
{
    size_t i;
    refSwap16(&d->unlocked_characters); refSwap16(&d->x186A);
    refSwap32(&d->unk_8.x0); refSwap32(&d->unk_8.x4);
    refSwap32(&d->unk_8.xC); refSwap32(&d->unk_8.x10);
    refSwap32(&d->unk_8.x14); refSwap32(&d->unk_8.x18); refSwap32(&d->unk_8.x1C);
    refSwap32(&d->unk_28.x0); refSwap32(&d->unk_28.x4);
    refSwap32(&d->unk_30.x0); refSwap32(&d->unk_30.x4);
    refSwap32(&d->unk_30.x8); refSwap32(&d->unk_30.xC);
    refSwap32(&d->unk_30.x10); refSwap32(&d->unk_30.x14);
    refSwap16Array(d->unk_30.x18, SELKIND_COUNT);
    refSwap32Array(d->unk_30.x4C, 4);
    refSwap32Array(d->unk_30.xB0, SELKIND_COUNT);
    refSwap32Array(d->unk_30.x114, SELKIND_COUNT);
    refSwap32(&d->unk_1A8.x0);
    refSwap32(&d->time_matches); refSwap32(&d->stock_matches);
    refSwap32(&d->coin_matches); refSwap32(&d->bonus_matches);
    refSwap32(&d->stamina_matches); refSwap32(&d->match_resets);
    refSwap32(&d->x1A30); refSwap32(&d->x1A34); refSwap32(&d->x1A38);
    refSwap32(&d->x1A3C); refSwap32(&d->x1A40); refSwap32(&d->x1A44);
    refSwap32(&d->x1A48); refSwap32(&d->x1A4C); refSwap32(&d->x1A50);
    refSwap32(&d->x1A54); refSwap32(&d->x1A58); refSwap32(&d->x1A5C);
    refSwap32(&d->x1A60); refSwap32(&d->x1A64); refSwap64(&d->x1A68);
    refSwap32Array(d->x1A70, 4); refSwap32Array(d->x1B40, 3);
    refSwap32Array(d->x1B4C, 3); refSwap32Array(d->x1B58, ARRAY_SIZE(d->x1B58));
    refSwap32Array(d->x1B80, ARRAY_SIZE(d->x1B80));
    refSwap32Array(d->x1C88, ARRAY_SIZE(d->x1C88));
    refSwap64(&d->x1CB0.item_mask); refSwap32(&d->x1CB0.stage_mask);
    refSwap16(&d->trophy_count); refSwap16(&d->trophy_category_flags);
    refSwap16Array(d->trophy_flags, TY_TROPHY_COUNT);
    for (i = 0; i < SELKIND_COUNT; i++) refSwapFighter(&d->x1F2C[i]);
}

static void refConvertNameTag(struct NameTagDataBank* bank)
{
    size_t i;
    for (i = 0; i < GM_NAMETAG_BANK_SIZE; i++) {
        refSwap16Array(bank->inner[i].vs_kos, GM_NAMETAG_COUNT);
        refSwapStats(&bank->inner[i].stats);
        refSwap32Array(bank->inner[i].play_time_by_fighter, SELKIND_COUNT);
    }
}

int main(void)
{
    GmSaveData save;
    GmSaveData save_before;
    struct NameTagDataBank bank;
    struct NameTagDataBank bank_before;
    struct GmCardData live_card;
    struct GmCardData live_card_before;
    GmSaveData staged_save;
    struct NameTagDataBank staged_banks[GM_NAMETAG_BANK_COUNT];
    s32 entry_status[1 + GM_NAMETAG_BANK_COUNT + 1] = { 0 };
    const u8 be16[] = { 0x12, 0x34 };
    const u8 be32[] = { 0x01, 0x02, 0x03, 0x04 };
    const u8 be32_b[] = { 0x11, 0x22, 0x33, 0x44 };
    const u8 be64[] = { 0x01, 0x23, 0x45, 0x67,
                        0x89, 0xAB, 0xCD, 0xEF };
    size_t i;

    if (sizeof(save) != 0x1790 || sizeof(bank) != 0x1F2C) {
        return 1;
    }

    {
        GmSaveData expected_save;
        struct NameTagDataBank expected_bank;
        for (i = 0; i < sizeof(save); i++) ((u8*) &save)[i] = (u8) (i * 37 + 11);
        memcpy(&save_before, &save, sizeof(save));
        memcpy(&expected_save, &save, sizeof(save));
        lbCardGame_ConvertSaveDataByteOrder(&save);
        refConvertSave(&expected_save);
        if (memcmp(&save, &expected_save, sizeof(save)) != 0) return 10;
        lbCardGame_ConvertSaveDataByteOrder(&save);
        if (memcmp(&save, &save_before, sizeof(save)) != 0) return 11;

        for (i = 0; i < sizeof(bank); i++) ((u8*) &bank)[i] = (u8) (i * 29 + 7);
        memcpy(&bank_before, &bank, sizeof(bank));
        memcpy(&expected_bank, &bank, sizeof(bank));
        lbCardGame_ConvertNameTagByteOrder(&bank);
        refConvertNameTag(&expected_bank);
        if (memcmp(&bank, &expected_bank, sizeof(bank)) != 0) return 12;
        lbCardGame_ConvertNameTagByteOrder(&bank);
        if (memcmp(&bank, &bank_before, sizeof(bank)) != 0) return 13;
    }

    memset(&save, 0xA5, sizeof(save));
    save.unlocked_characters = 0x1234;
    save.x1A50 = 0x01020304;
    save.x1A68 = 0x0123456789ABCDEFLL;
    save.x1CB0.item_mask = 0x0123456789ABCDEFULL;
    save.x1CB0.stage_mask = 0x11223344;
    save.trophy_flags[0] = 0x1234;
    save.x1F2C[0].fighter_kos[0] = 0x1234;
    save.x1F2C[0].stats.damage_taken = 0x11223344;
    save.x1F2C[0].x7A.byte = 0;
    save.x1F2C[0].x7A.b0 = 1;
    memset(&save.x1F2C[0].x7C, 0, 2);
    save.x1F2C[0].x7C.b0 = 1;
    save.x1F2C[0].x7C.b1 = 1;
    save.x1F2C[0].x7C.b789 = 5;
    save.x1F2C[0].x7C.x84 = 0x01020304;
    memcpy(&save_before, &save, sizeof(save));

    lbCardGame_ConvertSaveDataByteOrder(&save);
    if (!isBytes(&save.unlocked_characters, be16, sizeof(be16)) ||
        !isBytes(&save.x1A50, be32, sizeof(be32)) ||
        !isBytes(&save.x1A68, be64, sizeof(be64)) ||
        !isBytes(&save.x1CB0.item_mask, be64, sizeof(be64)) ||
        !isBytes(&save.x1CB0.stage_mask, be32_b, sizeof(be32_b)) ||
        !isBytes(&save.x1F2C[0].stats.damage_taken, be32_b, sizeof(be32_b)) ||
        save.x1F2C[0].x7A.byte != 0x80 ||
        !isBytes(&save.x1F2C[0].x7C, (const u8[]) { 0xC1, 0x40 }, 2) ||
        !isBytes(&save.x1F2C[0].x7C.x84, be32, sizeof(be32))) {
        return 2;
    }
    for (i = 0x218; i < 0x2D4; i++) {
        if (((const u8*) &save)[i] != 0xA5) {
            return 3;
        }
    }
    lbCardGame_ConvertSaveDataByteOrder(&save);
    if (memcmp(&save, &save_before, sizeof(save)) != 0) {
        return 4;
    }

    memset(&bank, 0x5A, sizeof(bank));
    bank.inner[0].vs_kos[0] = 0x1234;
    bank.inner[0].stats.attacks_hit = 0x11223344;
    bank.inner[0].play_time_by_fighter[0] = 0x01020304;
    memcpy(&bank_before, &bank, sizeof(bank));
    lbCardGame_ConvertNameTagByteOrder(&bank);
    if (!isBytes(&bank.inner[0].vs_kos[0], be16, sizeof(be16)) ||
        !isBytes(&bank.inner[0].stats.attacks_hit, be32_b, sizeof(be32_b)) ||
        !isBytes(&bank.inner[0].play_time_by_fighter[0], be32, sizeof(be32))) {
        return 5;
    }
    if (bank.inner[0].namedata[0] != 0x5A ||
        bank.inner[0].padding_x1A2 != 0x5A) {
        return 6;
    }
    lbCardGame_ConvertNameTagByteOrder(&bank);
    if (memcmp(&bank, &bank_before, sizeof(bank)) != 0) {
        return 7;
    }

    /* Failed async reads must leave live defaults intact; successful entries
     * alone are converted and copied out of their stable staging buffers. */
    memset(&live_card, 0x6B, sizeof(live_card));
    memcpy(&live_card_before, &live_card, sizeof(live_card));
    memset(&staged_save, 0, sizeof(staged_save));
    memset(staged_banks, 0, sizeof(staged_banks));
    memcpy(&staged_banks[1].inner[0].vs_kos[0], be16, sizeof(be16));
    memcpy(&staged_banks[GM_NAMETAG_BANK_COUNT - 1]
                .inner[GM_NAMETAG_BANK_SIZE - 1]
                .vs_kos[GM_NAMETAG_COUNT - 1],
           be16, sizeof(be16));
    entry_status[1] = 16;
    entry_status[2] = 16;
    entry_status[3] = 0;
    for (i = 4; i < ARRAY_SIZE(entry_status); i++) {
        entry_status[i] = 16;
    }
    entry_status[1 + GM_NAMETAG_BANK_COUNT] = 0;
    lbCardGame_CommitReadEntries(&live_card, &staged_save, staged_banks,
                                 entry_status);
    if (memcmp(&live_card.save_data, &live_card_before.save_data,
               sizeof(live_card.save_data)) != 0 ||
        memcmp(&live_card.nametag_banks[0], &live_card_before.nametag_banks[0],
               sizeof(live_card.nametag_banks[0])) != 0 ||
        live_card.nametag_banks[1].inner[0].vs_kos[0] != 0x1234 ||
        memcmp(&live_card.nametag_banks[2], &live_card_before.nametag_banks[2],
               sizeof(live_card.nametag_banks[2])) != 0 ||
        live_card.nametag_banks[GM_NAMETAG_BANK_COUNT - 1]
                .inner[GM_NAMETAG_BANK_SIZE - 1]
                .vs_kos[GM_NAMETAG_COUNT - 1] != 0x1234) {
        return 8;
    }

    memset(&staged_save, 0, sizeof(staged_save));
    memcpy(&staged_save.x1A50, be32, sizeof(be32));
    entry_status[1] = 0;
    entry_status[3] = 16;
    lbCardGame_CommitReadEntries(&live_card, &staged_save, staged_banks,
                                 entry_status);
    if (live_card.save_data.x1A50 != 0x01020304) {
        return 9;
    }
    return 0;
}
