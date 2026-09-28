/* Exercise the production serializer against known console bytes and its reader. */
#include "../shim/mu_replay.c"
void mu_replay_abi_log(const char* text) { (void) text; }

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
int main(void)
{
    StartMeleeRules r = {0}, decoded = {0};
    PlayerInitData p = {0}, player = {0};
    u8 rules[0x60], again[0x60], bytes[0x24], twice[0x24];
    r.match_kind = 1; r.x0_3 = 4; r.timer_enabled = 1;
    r.friendly_fire = 1; r.is_stock = 1; r.disable_pausing = 1; r.is_vs = 1;
    r.stkind = 31; r.time_limit = 480; r.item_freq = -1; r.sd_penalty = -2;
    r.x20 = 0x0123456789ABCDEFULL; r.x28 = -1;
    r.x30 = 1.0f; r.game_speed = 1.0f;
    r.on_frame_start = (void*) 0x12345678;
    record_rules(rules, &r);
    CHECK(rules[0] == 0x32 && rules[1] == 1 && rules[2] == 0x88 && rules[4] == 0x40);
    CHECK(rules[11] == 255 && rules[12] == 254 && rules[14] == 0 && rules[15] == 31);
    CHECK(be32(rules + 16) == 480 && be32(rules + 0x20) == 0x01234567);
    CHECK(be32(rules + 0x24) == 0x89ABCDEF && be32(rules + 0x30) == 0x3F800000);
    for (int i = 0x38; i < 0x60; ++i) CHECK(rules[i] == 0);
    convert_rules(&decoded, rules); record_rules(again, &decoded);
    CHECK(memcmp(rules, again, sizeof rules) == 0);
    p.ckind = 2; p.slot_type = 1; p.stocks = 4; p.color = 3; p.slot = 1;
    p.spawn_pos = -1; p.spawn_dir = -1; p.handicap = 9; p.team = 2;
    p.rumble_enabled = 1; p.vs_metal = 1; p.vs_invisible = 1; p.xD_b7 = 1;
    p.cpu_kind = 4; p.cpu_level = 9; p.damage = 123; p.damage1 = 456; p.hp = 150;
    p.attack_ratio = 1.0f; p.defense_ratio = 2.0f; p.model_scale = 0.5f;
    record_player(bytes, &p);
    CHECK(be32(bytes) == 0x02010403 && bytes[5] == 255 && bytes[6] == 255);
    CHECK(bytes[12] == 0xA8 && bytes[13] == 1 && be16(bytes + 18) == 456);
    CHECK(be32(bytes + 24) == 0x3F800000 && be32(bytes + 32) == 0x3F000000);
    convert_player(&player, bytes); record_player(twice, &player);
    CHECK(memcmp(bytes, twice, sizeof bytes) == 0);
    return 0;
}
