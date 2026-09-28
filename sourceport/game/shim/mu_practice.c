/* Practice matchmaking bridge (Tab during an offline match, game API version 6).
 *
 * The host's practice coordinator (port/runtime/hle/native_practice.cpp) searches for an online
 * match while the player keeps practicing, then hands off to the online scene. On the static
 * recomp it reads and writes the console's memory directly; natively it asks the game through
 * these operations, which touch the same fields by name. */
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/pl/player.h>


/* Same values as MU_PRACTICE_* in mu_host.h (checked in mu_entry.c). */
enum { MU_PR_SCENE, MU_PR_GET_PLAYER, MU_PR_SET_PLAYER, MU_PR_GET_STAGE, MU_PR_SET_STAGE,
       MU_PR_SET_EVENT_BACKUP, MU_PR_REQUEST_MAJOR, MU_PR_SET_ONLINE_MODE, MU_PR_GET_ONLINE_MODE,
       MU_PR_DIRECT_FIRST_MATCH };

extern StaticPlayer player_slots[];
struct gm_80479D58_t* mu_gm_engine_state(void);

int mu_practice_bridge(int op, int* a, int n)
{
    MuSlippiMenuState* slp = mu_slippi_state();
    switch (op) {
    case MU_PR_SCENE:
        if (n < 2) return -1;
        a[0] = gm_GetCurrentGameMode();
        a[1] = gm_GetCurrentSceneIndex();
        return 0;
    case MU_PR_GET_PLAYER: {
        StaticPlayer* p;
        if (n < 9 || a[0] < 0 || a[0] > 3) return -1;
        p = &player_slots[a[0]];
        a[1] = p->player_state;
        a[2] = p->ckind;
        a[3] = p->pkind;
        a[4] = p->costume_id;
        a[5] = p->pad_port;
        a[6] = p->cpu_level;
        a[7] = p->staminas.byName.damage_percent;
        a[8] = p->staminas.byName.damage_percent_alt_or_start_hp;
        return 0;
    }
    case MU_PR_SET_PLAYER: {
        StaticPlayer* p;
        if (n < 9 || a[0] < 0 || a[0] > 3) return -1;
        p = &player_slots[a[0]];
        p->ckind = a[2];
        p->pkind = a[3];
        p->costume_id = (u8) a[4];
        p->pad_port = (u8) a[5];
        p->cpu_level = (u8) a[6];
        p->staminas.byName.damage_percent = (s16) a[7];
        p->staminas.byName.damage_percent_alt_or_start_hp = (s16) a[8];
        return 0;
    }
    case MU_PR_GET_STAGE:
        if (n < 1) return -1;
        a[0] = gmVs_GetSceneController()->start.stkind;
        return 0;
    case MU_PR_SET_STAGE:
        if (n < 1) return -1;
        gmVs_GetSceneController()->start.stkind = (u16) a[0];
        return 0;
    case MU_PR_SET_EVENT_BACKUP:
        /* The fighter, costume and port the character select starts from. */
        if (n < 3) return -1;
        gmMainLib_804D3EE0->vs.unk_530.x2 = (s8) a[0];
        gmMainLib_804D3EE0->vs.unk_530.x3 = (u8) a[1];
        gmMainLib_804D3EE0->vs.unk_530.x6 = (u8) a[2];
        return 0;
    case MU_PR_REQUEST_MAJOR:
        /* Leave the current scene for `major`: the pending mode plus the engine's leave flag, so a
         * Training minor that never ends on its own runs its decide path on the next frame. */
        if (n < 1) return -1;
        gm_ChangeGameModeAfterCurrentScene(a[0]);
        mu_gm_engine_state()->unk_C = 1;
        return 0;
    case MU_PR_SET_ONLINE_MODE:
        if (n < 1) return -1;
        slp->mode = (unsigned char) a[0];
        return 0;
    case MU_PR_GET_ONLINE_MODE:
        if (n < 1) return -1;
        a[0] = slp->mode;
        return 0;
    case MU_PR_DIRECT_FIRST_MATCH:
        slp->is_winner = MU_SLP_WINNER_NULL;
        slp->chose_stage = MU_SLP_STAGE_UNSET;
        return 0;
    }
    return -1;
}
