/* Entering a network match without any menu (built only with MU_NO_SLIPPI).
 *
 * The host says a match is requested through its online_test_match callback (mu_online_abi.c). The
 * game then boots straight into the plain VS scene (GM_DEBUG_VS) instead of the title, waits there
 * until the host's match state reports a connected match with both sides ready (mu_online_test_wait
 * in mu_online.c), loads that match's fighters and stage, and starts it (mu_online_start_melee,
 * called by the scene). When the match scene ends the game goes on to the title screen, the retail
 * way into the main menu, with everything the menus need loaded as on a normal boot.
 *
 * A session can hold more than one game. When the match scene ends the host is asked whether
 * another game may follow (it does when both sides finished the game and neither quit); if so the
 * game mode is entered again from its start, exactly as at boot: the wait above runs again, which
 * also clears the whole online state, and the next game's fighters and stage are loaded from the
 * new match state. If the next game does not come, the wait ends with an error and the game goes
 * on to the title as after a single game.
 *
 * The four functions below keep the names the game sources already call at these sites.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <melee/ft/forward.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/gr/stage.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/types.h>

int mu_online_test_wait(void);
const unsigned char* mu_online_pending_game_info(void);
void mu_online_abi_log(const char* text);
void mu_gmvs_request_online_end(int pauser);
int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);

/* Where the game goes after the session's last match: the retail title screen. */
#define MU_P2P_AFTER_MATCH GM_TITLE

enum { MATCH_PLAYERS_AT = 0x60, MATCH_PLAYER_SIZE = 0x24, MATCH_STAGE_AT = 0xE, NO_FIGHTER = 33 };
/* The host's "may another game of this session follow" question: one reply byte, 1 for yes. The
 * host wants room for any reply, whatever this one's size. */
enum { CMD_SERIES_STATE = 0xC0, SERIES_REPLY_CAPACITY = 4096 };

/* Set before the match starts and constant through it, so a rollback never has to restore them. */
static int entered;        /* the scene now running was entered through here */
static int entry_failed;   /* ... and the match could not be made */

/* gm_801A4510: the first game mode of this run. */
void mu_replay_boot_mode(unsigned char* mode)
{
    if (mu_online_is_test_run()) {
        *mode = GM_DEBUG_VS;
    }
}

/* What the character and stage select screens would have loaded before a match: the four fighters'
 * files and the sound banks of every fighter and of the stage in the block. */
static void preload_match(const u8* info)
{
    PreloadedGameModeState* scene = lbDvd_GetPreloadCacheScene();
    u64 mask = 0;
    int i;

    for (i = 0; i < 4; i++) {
        const u8* p = info + MATCH_PLAYERS_AT + MATCH_PLAYER_SIZE * i;
        scene->game_cache.entries[i].char_id = (s8) p[0];
#ifdef MU_AKANEIA_FIGHTERS
        {
            const int ckind = mu_ak_ckind_from_mex((s8) p[0]);
            scene->game_cache.entries[i].char_id = ckind >= 0 ? (s8) ckind : ChKind_None;
        }
#endif
        scene->game_cache.entries[i].color = p[3];
    }
    lbDvd_80018254();
    lbDvd_80018C2C(199);
    lbDvd_80017700(4);
    lbAudioAx_80026F2C(28);
    for (i = 0; i < 6; i++) {
        const u8* p = info + MATCH_PLAYERS_AT + MATCH_PLAYER_SIZE * i;
        int ckind = (s8) p[0];
        if (ckind == NO_FIGHTER) {
            continue;
        }
#ifdef MU_AKANEIA_FIGHTERS
        ckind = mu_ak_ckind_from_mex(ckind);
        if (ckind < 0) {
            continue;
        }
#endif
        mask |= lbAudioAx_80026E84((CharacterKind) ckind);
    }
    mask |= lbAudioAx_80026EBC((StKind) (info[MATCH_STAGE_AT] << 8 | info[MATCH_STAGE_AT + 1]));
    lbAudioAx_8002702C(4, mask);
    lbAudioAx_80027168();
    lbAudioAx_80024F6C();
}

/* The VS scene is about to preload (gm_1A3F.c, only when the host requested a match): wait for the
 * match, then load what it needs. The wait returns when the host reports the match ready or failed;
 * the host must report one or the other, the game does not time out by itself. */
void mu_replay_prepare_scene(void)
{
    const u8* info = NULL;

    entered = 1;
    entry_failed = 0;
    if (mu_online_test_wait()) {
        info = mu_online_pending_game_info();
    }
    if (info == NULL) {
        /* No match: the scene has no other way out than to start, so it starts and is ended on its
         * first frame (mu_replay_scene_think), and the game goes on as after a match. */
        entry_failed = 1;
        mu_online_abi_log("p2p: no match was made; leaving the match scene");
        return;
    }
    preload_match(info);
}

/* fn_8016E730, before mu_online_start_melee: nothing of its own to restore. */
void mu_replay_start_melee(StartMeleeData* data)
{
    (void) data;
}

/* The VS scene's think, every unpaused frame. */
void mu_replay_scene_think(int match_result)
{
    if (entry_failed && match_result == 0) {
        entry_failed = 0;
        mu_gmvs_request_online_end(0);   /* ends the scene as no contest */
    }
}

/* Whether the host expects another game of this session. A host that does not know the question
 * (or answers nothing) means no. */
static int next_game_may_follow(void)
{
    unsigned char reply[SERIES_REPLY_CAPACITY];   /* on the stack: no new game-library state for a snapshot to carry */
    unsigned int got = 0;

    reply[0] = 0;
    if (mu_online_abi_command(CMD_SERIES_STATE, NULL, 0, reply, sizeof reply, &got) != 0 || got < 1) {
        return 0;
    }
    return reply[0] == 1;
}

/* gm_Scene_Vs_OnExit: a match entered through here does not continue to the VS scene's results. */
void mu_replay_match_exit(void)
{
    int made;

    if (!entered) {
        return;
    }
    made = !entry_failed;
    entered = 0;
    entry_failed = 0;
    /* The game mode is left and entered again even when it is the same one: its preloads and its
     * first state start over as at boot, and mu_replay_prepare_scene waits for the next match. A
     * scene that was only started to be ended (no match was made) never asks. */
    if (made && next_game_may_follow()) {
        mu_online_abi_log("p2p: waiting for the next game of the session");
        gm_ChangeGameModeAfterCurrentScene(GM_DEBUG_VS);
        return;
    }
    gm_ChangeGameModeAfterCurrentScene(MU_P2P_AFTER_MATCH);
}

/* Host plumbing, not game state (mu_exclusions.c lists this provider by the name the practice
 * bridge used; that file is not in this build). */
MU_EXCLUSIONS(practice, MU_EXCLUDE(entered), MU_EXCLUDE(entry_failed))
