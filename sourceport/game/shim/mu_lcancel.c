/* What the host's L-cancel helper reads (port/runtime/host/lcancel.cpp). The recompiled build reads
 * these straight out of guest memory at the console's addresses; the native game keeps them in its
 * own structures with the host's layout, so it hands them over through MuGameApi.lcancel_view. */
#include "mu_lcancel_view.h"

#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

/* A player slot can still name its fighter after the match has freed it (scene changes, the
 * results screen): only a fighter on the live fighter list is read. */
static int fighter_alive(HSD_GObj* gobj)
{
    HSD_GObj* it;
    if (HSD_GObjPLinkHead == NULL) {
        return 0;
    }
    for (it = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; it != NULL; it = it->next) {
        if (it == gobj) {
            return 1;
        }
    }
    return 0;
}

void mu_lcancel_view(MuLcancelView* out)
{
    int slot;
    __builtin_memset(out, 0, sizeof(*out));
    out->pad_shift = HSD_PadLibData.clamp_analogLRShift;
    out->pad_max = HSD_PadLibData.clamp_analogLRMax;
    out->pad_min = HSD_PadLibData.clamp_analogLRMin;
    out->pad_scale = HSD_PadLibData.scale_analogLR;
    if (p_ftCommonData != NULL) {
        out->have_common = 1;
        out->trigger_deadzone = p_ftCommonData->analog_shoulder_deadzone;
        out->lcancel_window = p_ftCommonData->xE4;
    }
    /* As the recompiled build: human slots 0-3, first one on each controller port. */
    for (slot = 0; slot < 4; ++slot) {
        HSD_GObj* gobj;
        Fighter* fp;
        int port;
        if (Player_GetPlayerSlotType(slot) != Gm_PKind_Human)
            continue;
        port = (s8) Player_GetSubColor(slot);
        if (port < 0 || port > 3)
            continue;
        /* In a network match every player reads the same controller value here, so the second
         * player would be left out: it takes the entry of its own slot instead. The host keeps
         * only the local player's entry in such a match (lcancel.cpp gather). */
        if (out->port[port].present) {
            if (!mu_online_active() || out->port[slot].present)
                continue;
            port = slot;
        }
        if (Player_GetPlayerState(slot) == 0)
            continue;
        gobj = Player_GetEntityAtIndex(slot, 0);
        if (gobj == NULL || !fighter_alive(gobj) || (fp = gobj->user_data) == NULL || fp->gobj != gobj)
            continue;
        out->port[port].present = 1;
        out->port[port].slot = slot;
        out->port[port].motion_id = fp->motion_id;
        out->port[port].ground_or_air = fp->ground_or_air;
        out->port[port].frames_since_trigger = fp->x67F;
        out->port[port].anim_frame = fp->cur_anim_frame;
    }
}
