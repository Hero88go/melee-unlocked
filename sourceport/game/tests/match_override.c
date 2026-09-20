/* The match override the sweep depends on: an absent or empty request must
 * leave a run untouched, and a present one must fill every slot it was given.
 * Also covers mu_seed_request (M7 lockstep parity): a host predating version 3,
 * or one that declines to give a seed, must leave the RNG alone; a host that
 * gives one must apply it and always mark match start.
 * Exercised against a stub host so no disc image or game boot is needed.
 *
 * mu_apply_seed is stubbed here rather than linked from mu_snapshot.c: that file pulls in
 * random.h, and random.h drags in Runtime/platform.h, whose ssize_t conflicts with the one
 * mu_host.h gets from the Windows SDK's stdint.h in the same translation unit (the same reason
 * mu_entry.c keeps game headers out of itself). The stub only needs to record what it was given.
 */
#include <mu_host.h>
#include <stdio.h>

int mu_match_request(int* stage, int* kind, int* cpu, int* level, int* costume,
                     int count);
void mu_seed_request(void);

const MuHostApi* mu_host;
static MuHostApi g_host;
static MuMatchOverride g_over;
static const MuMatchOverride* g_result;
static int g_mark_calls;
static int g_apply_calls;
static uint32_t g_applied_seed;
static uint32_t g_stub_seed;
static int32_t g_stub_has_value;

static const MuMatchOverride* stub_override(void) { return g_result; }
static void stub_mark_match_start(void) { ++g_mark_calls; }
static void stub_rng_seed_override(uint32_t* seed, int32_t* has_value)
{
    *has_value = g_stub_has_value;
    if (g_stub_has_value) *seed = g_stub_seed;
}
void mu_apply_seed(uint32_t seed) { ++g_apply_calls; g_applied_seed = seed; }

static int failures;

static void check(int ok, const char* what)
{
    if (!ok) { printf("FAIL: %s\n", what); ++failures; }
}

int main(void)
{
    int stage, kind[6], cpu[6], level[6], costume[6];
    int i;

    g_host.version = MU_HOST_API_VERSION;
    g_host.match_override = stub_override;
    mu_host = &g_host;

    /* An ordinary run: the host has no --match, so nothing is forced. */
    g_result = NULL;
    stage = -999;
    check(mu_match_request(&stage, kind, cpu, level, costume, 6) == 0,
          "no override returns 0");
    check(stage == -999, "no override leaves the stage alone");

    /* A host too old to know about the call at all. */
    g_host.match_override = NULL;
    g_result = &g_over;
    check(mu_match_request(&stage, kind, cpu, level, costume, 6) == 0,
          "null accessor returns 0");
    g_host.match_override = stub_override;

    /* A host that predates the field: the version guard has to catch it
     * before the pointer is read. */
    g_host.version = 1;
    check(mu_match_request(&stage, kind, cpu, level, costume, 6) == 0,
          "older host version returns 0");
    g_host.version = MU_HOST_API_VERSION;

    /* A real request: two fighters, the rest empty. */
    g_over.stage = 0x14;
    for (i = 0; i < 6; i++) { g_over.players[i].kind = -1; }
    g_over.players[0].kind = 9;  g_over.players[0].cpu = 0;
    g_over.players[0].cpu_level = 0; g_over.players[0].costume = 2;
    g_over.players[1].kind = 12; g_over.players[1].cpu = 1;
    g_over.players[1].cpu_level = 9; g_over.players[1].costume = 1;

    check(mu_match_request(&stage, kind, cpu, level, costume, 6) == 1,
          "an override returns 1");
    check(stage == 0x14, "stage comes through");
    check(kind[0] == 9 && cpu[0] == 0 && costume[0] == 2, "player 1");
    check(kind[1] == 12 && cpu[1] == 1 && level[1] == 9, "player 2 as a CPU");
    for (i = 2; i < 6; i++) check(kind[i] == -1, "the rest stay empty");

    /* Fewer slots than the host carries: nothing may be written past count. */
    kind[3] = 0x5A5A;
    check(mu_match_request(&stage, kind, cpu, level, costume, 3) == 1,
          "a short request still succeeds");
    check(kind[3] == 0x5A5A, "a short request writes nothing past count");

    /* mu_seed_request: a host predating version 3 leaves both fields NULL, so nothing may be
     * called and the seed must stay untouched. */
    g_host.version = 2;
    g_host.mark_match_start = NULL;
    g_host.rng_seed_override = NULL;
    g_mark_calls = 0; g_apply_calls = 0;
    mu_seed_request();
    check(g_mark_calls == 0, "pre-v3 host: match start not marked");
    check(g_apply_calls == 0, "pre-v3 host: seed not applied");

    /* A v3 host that declines to give a seed (an ordinary run, no --rng-seed) still marks match
     * start unconditionally, but must not apply a seed. */
    g_host.version = MU_HOST_API_VERSION;
    g_host.mark_match_start = stub_mark_match_start;
    g_host.rng_seed_override = stub_rng_seed_override;
    g_stub_has_value = 0;
    g_mark_calls = 0; g_apply_calls = 0;
    mu_seed_request();
    check(g_mark_calls == 1, "v3 host, no seed: match start marked once");
    check(g_apply_calls == 0, "v3 host, no seed: seed not applied");

    /* A v3 host with --rng-seed applies the seed and still marks match start. */
    g_stub_has_value = 1;
    g_stub_seed = 0xCAFEBABEu;
    g_mark_calls = 0; g_apply_calls = 0;
    mu_seed_request();
    check(g_mark_calls == 1, "v3 host with seed: match start marked once");
    check(g_apply_calls == 1 && g_applied_seed == 0xCAFEBABEu, "v3 host with seed: seed applied");

    if (!failures) printf("match override: all cases pass\n");
    return failures != 0;
}
