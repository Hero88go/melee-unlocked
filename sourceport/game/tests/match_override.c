/* The match override the sweep depends on: an absent or empty request must
 * leave a run untouched, and a present one must fill every slot it was given.
 * Exercised against a stub host so no disc image or game boot is needed.
 */
#include <mu_host.h>
#include <stdio.h>

int mu_match_request(int* stage, int* kind, int* cpu, int* level, int* costume,
                     int count);

const MuHostApi* mu_host;
static MuHostApi g_host;
static MuMatchOverride g_over;
static const MuMatchOverride* g_result;

static const MuMatchOverride* stub_override(void) { return g_result; }

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

    if (!failures) printf("match override: all cases pass\n");
    return failures != 0;
}
