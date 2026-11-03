/* YouGame page pause, simulation-thread only. Shared by the Slippi and no-Slippi game shims.
 * The host gates this operation to stdio, offline, live matches. */
#ifndef MU_YOUGAME_PAUSE_H
#define MU_YOUGAME_PAUSE_H
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>

extern struct GameSceneInfo* gm_804D6720;
struct gm_80479D58_t* mu_gm_engine_state(void);

static int mu_yougame_page_pause(int* a, int n)
{
    int kind;
    int match;
    if (n < 3) return -1;
    kind = gm_804D6720 ? gm_804D6720->scene_kind : -1;
    match = (kind == GS_VS || kind == GS_SUDDEN_DEATH || kind == GS_TRAINING ||
             kind == GS_CAMERA_VS) && gmVs_GetSceneController()->state.unk_0 == 0;
    /* Start's routines use gm_Set/ClearDbPauseFlag(1/2). Context 0 freezes the
     * entire scene through that same scheduler, without pause HUD/camera callbacks.
     * Query first; the host only clears a flag it set itself. */
    if (match && a[0] == 1) gm_SetDbPauseFlag(0);
    if (match && a[0] == 0) gm_ClearDbPauseFlag(0);
    a[0] = gm_GetDbPauseFlag(0);
    a[1] = match;
    a[2] = mu_gm_engine_state()->unk_0;
    return 0;
}
#endif
