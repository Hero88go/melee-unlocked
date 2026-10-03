/* 20XX Tournament Edition features, reimplemented as native options (M2).
 *
 * 20XX TE ships as a memory card save that loads a code list; the native game carries the same
 * behaviors as C at each site, switched by the player in the settings panel. They are offline
 * conveniences: an online match and replay playback always play the unmodified rules, so a replay
 * or an opponent never sees them. Tournament Mode keeps only the features tournaments allow. */
#include "mu_host.h"
#include "mu_hp.h"
#include "mu_native.h"

int mu_online_active(void);
int mu_online_pending(void);
int mu_replay_abi_active(void);
int mu_tmce_enabled(void);
unsigned char gm_GetCurrentGameMode(void);

/* TM-CE exercises own their CPU, camera, pause and stage rules. Keep TE's stored choices for VS,
 * but do not apply its switchable match features over an exercise in a combined profile. */
static int te_in_tmce_event(void)
{
    return mu_tmce_enabled() && gm_GetCurrentGameMode() == 0x2B; /* GM_EVENT */
}

void OSReport(const char* msg, ...);
int mu_slippi_in_online_mode(void);

/* The 20XX Hack Pack's share of the TE features (MU_HP_TE_OPTIONS, MU_HP_TE_OPTIONS2): its debug
 * menu (shim/mu_hp_menu.c) switches them without a TE save. The host then passes those bits alone,
 * without TE itself, so TE's menus and rules stay off. In effect offline with the pack loaded, or in
 * playback of a replay that recorded them. */
static int te_hp_share(unsigned int feature, unsigned int share)
{
    if ((feature & ~share) != 0 || (mu_game_options() & MU_OPTION_VANILLA)) {
        return 0;
    }
    if (!mu_hp_loaded() && !mu_replay_abi_active()) {
        return 0;
    }
    return !(mu_online_active() || mu_online_pending() || mu_slippi_in_online_mode());
}

int mu_te(unsigned int feature)
{
    const unsigned int options = mu_game_options();
    static unsigned int reported = 0xFFFFFFFFu;
    if ((options & 0x0FFFFFF0u) != reported) {
        reported = options & 0x0FFFFFF0u;
        OSReport("[20xx] options %08X (online %d, replay %d)\n", reported,
                 mu_online_active() | mu_online_pending() << 1, mu_replay_abi_active());
    }
    if (!(options & feature) || (options & MU_OPTION_VANILLA) || te_in_tmce_event()) {
        return 0;
    }
    if (!(options & MU_OPTION_TE) && !te_hp_share(feature, MU_HP_TE_OPTIONS)) {
        return 0;
    }
    if ((options & MU_OPTION_TE_TOURNAMENT) && !(feature & MU_GAME_OPTION_TE_TOURNAMENT_SAFE)) {
        return 0;
    }
    /* Never online. In replay playback the host passes the options the replay was recorded with
     * (its "muOptions"), so a replay plays back exactly as it was played. */
    if (mu_online_active() || mu_online_pending()) {
        return 0;
    }
    {
        /* One line the first time each feature takes effect, so a log shows what a session used. */
        static unsigned int used = 0;
        if (!(used & feature)) {
            used |= feature;
            OSReport("[20xx] feature %08X in effect\n", feature);
        }
    }
    return 1;
}

static int te_on_offline(void)
{
    const unsigned int options = mu_game_options();
    if (!(options & MU_OPTION_TE) || (options & MU_OPTION_VANILLA)) {
        return 0;
    }
    return !(mu_online_active() || mu_online_pending() || mu_slippi_in_online_mode());
}

int mu_te2(unsigned int feature)
{
    const unsigned int options2 = mu_game_options2();
    if (!(options2 & feature) || te_in_tmce_event()) {
        return 0;
    }
    if (!te_on_offline() && !te_hp_share(feature, MU_HP_TE_OPTIONS2)) {
        return 0;
    }
    if ((mu_game_options() & MU_OPTION_TE_TOURNAMENT) && !(feature & MU_GAME_OPTION2_TE_TOURNAMENT_SAFE)) {
        return 0;
    }
    {
        static unsigned int used = 0;
        if (!(used & feature)) {
            used |= feature;
            OSReport("[20xx] feature2 %08X in effect\n", feature);
        }
    }
    return 1;
}

int mu_te_general(void)
{
    return te_on_offline();
}

/* A plain option carried in the second word that is not a 20XX TE feature ("20XX CPUs", Game tab):
 * in effect offline in the normal game, with or without a TE save, never online or while online is
 * pending or in Slippi's online menus, never in a TM-CE event. No TE master, no Tournament Mode test.
 * In replay playback the host passes the recorded word, so a replay plays back as it was played. */
int mu_option2_offline(unsigned int bit)
{
    if (!(mu_game_options2() & bit) || (mu_game_options() & MU_OPTION_VANILLA) || te_in_tmce_event()) {
        return 0;
    }
    if (mu_online_active() || mu_online_pending() || mu_slippi_in_online_mode()) {
        return 0;
    }
    {
        static unsigned int used = 0;
        if (!(used & bit)) {
            used |= bit;
            OSReport("[20xx] option2 %08X in effect\n", bit);
        }
    }
    return 1;
}

/* The third option word (the 20XX Hack Pack training options for CPUs, shim/mu_20xx_cpu.c) when it
 * is in effect, else 0. The same limits as mu_option2_offline: the normal game, offline, not while
 * online is pending or in Slippi's online menus, never in a TM-CE event. In replay playback the
 * host passes the recorded word. */
unsigned int mu_game_options3(void);

unsigned int mu_options3_offline(void)
{
    const unsigned int word = mu_game_options3();
    if (word == 0 || (mu_game_options() & MU_OPTION_VANILLA) || te_in_tmce_event()) {
        return 0;
    }
    if (mu_online_active() || mu_online_pending() || mu_slippi_in_online_mode()) {
        return 0;
    }
    return word;
}

/* TE's conveniences that only show with Tournament Mode off (the green "Ready to Fight" banner). */
int mu_te_casual(void)
{
    return te_on_offline() && !(mu_game_options() & MU_OPTION_TE_TOURNAMENT);
}

int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);

/* 20XX TE "Set menu music in Sound Test": the last song played in Sound Test becomes the menu
 * music. TE keeps it with its settings; here the host keeps it in the settings file (command 0xF7:
 * 0x80 asks, anything else sets). The value is TE's: 0 none, -1 song 0, otherwise the song. */
static int te_menu_song(int set, int song)
{
    static unsigned char response[4096];
    unsigned char payload = set ? (unsigned char) (signed char) song : 0x80;
    unsigned int got = 0;
    if (mu_online_abi_command(0xF7, &payload, 1, response, sizeof response, &got) != 0 || got < 1) {
        return 0;
    }
    return (signed char) response[0];
}

char* getenv(const char* name);
struct NameTagData* GetPersistentNameData(int slot);

/* Tests only: MELEE_TEST_NAMETAGS=<n> puts n names (AAA, BBB, CCC, full width) in the first nametag
 * slots before a character select screen counts them. */
void mu_test_seed_nametags(void)
{
    static const char names[3][8] = {
        {(char) 0x82, (char) 0x60, (char) 0x82, (char) 0x60, (char) 0x82, (char) 0x60, 0, 0},
        {(char) 0x82, (char) 0x61, (char) 0x82, (char) 0x61, (char) 0x82, (char) 0x61, 0, 0},
        {(char) 0x82, (char) 0x62, (char) 0x82, (char) 0x62, (char) 0x82, (char) 0x62, 0, 0},
    };
    const char* spec = getenv("MELEE_TEST_NAMETAGS");
    int n, i, k;
    if (spec == NULL) {
        return;
    }
    n = spec[0] - '0';
    for (i = 0; i < n && i < 3; i++) {
        char* dst = (char*) GetPersistentNameData(i) + 0x198;   /* NameTagData.namedata */
        for (k = 0; k < 8; k++) {
            dst[k] = names[i][k];
        }
    }
}

int mu_te_menu_music(void)
{
    return mu_te_general() ? te_menu_song(0, 0) : 0;
}

void mu_te_set_menu_music(int song)
{
    if (mu_te_general()) {
        te_menu_song(1, song == 0 ? -1 : song);
    }
}

/* Training Mode CE (shim/mu_tmce.c): its disc files are in the mod profile on the vanilla game, and
 * this is not an online session. */
int mu_tmce_enabled(void)
{
    const unsigned int options = mu_game_options();
    if (!(options & MU_GAME_OPTION_TMCE) || (options & MU_OPTION_VANILLA)) {
        return 0;
    }
    return !(mu_online_active() || mu_online_pending() || mu_slippi_in_online_mode());
}
